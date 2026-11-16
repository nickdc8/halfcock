// Parked-wah engine: a circuit model of the classic two-transistor inductor wah.
//
// No JUCE dependency, so the offline test harness can compile it on its own.
//
// The circuit (Vox Clyde McCoy / V846 topology, shared by the early CryBaby):
//
//   in --R1--C1--+-- X --Rb--> Q1 base      Q1: common emitter, Rc to +9 V,
//                |                              Re to ground, gain -A
//                +-- L (+DCR) --> AC ground
//                +-- Rq ---------> AC ground
//                +-- Cf --< Q2 emitter      Q2: follower fed from the pot wiper,
//                                               which taps Q1's collector
//
// The pot passes a fraction alpha of Q1's inverted, amplified output back
// through Cf, so Cf looks like Cf * (1 + alpha * A) at node X (Miller effect)
// and resonates with L. Turning the pot moves the resonance.
//
// Small signal, with Yin = sC1 / (1 + s R1 C1), k = base divider (about 0.99):
//
//   Vc / Vin = -A k Yin / (Yin + 1/(sL + DCR) + 1/Rq + 1/Zb + s Cf (1 + alpha A k))
//
// The engine does not run that formula. It solves node X every sample with
// trapezoidal companion models for C1, L and Cf, and Newton's method for the
// two nonlinear parts: Q1 running out of headroom, and the inductor core
// saturating. tests/wah_test.cpp checks the result against the formula.
//
// Signal scale: 1.0 is treated as 1 volt.
//
// Part values: R1, C1, Cf, Rq, Rb, the pot, L = 500 mH and the 22 k / 470 k
// collector network are the standard published Vox values. Everything else
// (DCR, transistor beta and re, clip levels, flux limits, the pot taper, and
// all of the Wacked and Shaft differences) is an estimate. See the README.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>

namespace wah
{

struct ModeSpec
{
    double r1;       // input resistor (ohm)
    double c1;       // input capacitor (F)
    double cf;       // feedback (sweep) capacitor (F)
    double l;        // inductance (H)
    double dcr;      // inductor winding resistance (ohm)
    double rq;       // resistor across the inductor (ohm)
    double rc;       // Q1 collector resistor (ohm)
    double re;       // Q1 emitter resistor (ohm)
    double beta;     // Q1 current gain
    double fluxSat;  // flux linkage where the core starts to saturate (V s)
};

enum Mode { wacked = 0, jimi = 1, shaft = 2 };

inline constexpr std::array<ModeSpec, 3> kModes {{
    // Wacked: Colorsound-style. Smaller sweep cap, hotter Q1, lightly damped.
    { 68.0e3, 10.0e-9, 6.8e-9, 0.50, 45.0, 100.0e3, 22.0e3, 330.0, 350.0, 1.2e-4 },
    // Jimi: Vox Clyde McCoy, Halo-style inductor.
    { 68.0e3, 10.0e-9, 10.0e-9, 0.50, 33.0, 33.0e3, 22.0e3, 510.0, 300.0, 1.5e-4 },
    // Shaft: early-70s CryBaby. Larger inductor, more gain, so it sits lower.
    { 68.0e3, 10.0e-9, 10.0e-9, 0.62, 70.0, 33.0e3, 22.0e3, 390.0, 450.0, 2.5e-4 },
}};

inline constexpr double kPotR      = 100.0e3;  // wah pot, loads Q1's collector
inline constexpr double kBiasR     = 470.0e3;  // collector-to-bias resistor
inline constexpr double kBaseR     = 1.5e3;    // Rb, in series with Q1's base
inline constexpr double kEmitterRe = 120.0;    // Q1 intrinsic re at about 0.2 mA
inline constexpr double kClipVolts = 3.5;      // Q1 collector headroom
inline constexpr double kClipBias  = 0.15;     // asymmetry of that headroom
inline constexpr double kPeakGain  = 2.5;      // output trim: peak gain at mid sweep

inline constexpr double kPi = 3.14159265358979323846;

struct Derived
{
    double gain;  // A * k: X to (inverted) collector
    double gp;    // resistive load on X: 1/Rq + 1/Zb
};

inline Derived derive (const ModeSpec& m)
{
    const double rLoad = 1.0 / (1.0 / m.rc + 1.0 / kPotR + 1.0 / kBiasR);
    const double a = rLoad / (m.re + kEmitterRe);
    const double zb = kBaseR + m.beta * (m.re + kEmitterRe);
    return { a * (zb - kBaseR) / zb, 1.0 / m.rq + 1.0 / zb };
}

// Knob position (0 = lowest, 1 = highest) to the fraction of Q1's output fed
// back. The taper makes the resonance move evenly in pitch across the knob.
inline double potFraction (double gain, double position)
{
    return (std::pow (1.0 + gain, 1.0 - std::clamp (position, 0.0, 1.0)) - 1.0) / gain;
}

inline double resonanceHz (const ModeSpec& m, double gain, double alpha)
{
    return 1.0 / (2.0 * kPi * std::sqrt (m.l * m.cf * (1.0 + alpha * gain)));
}

// Magnitude of the small-signal transfer function in the header comment.
inline double smallSignalGain (const ModeSpec& m, double alpha, double hz)
{
    const auto d = derive (m);
    const std::complex<double> s (0.0, 2.0 * kPi * hz);
    const auto yin = s * m.c1 / (1.0 + s * m.r1 * m.c1);
    const auto y = yin + 1.0 / (s * m.l + m.dcr) + d.gp + s * m.cf * (1.0 + alpha * d.gain);
    return std::abs (d.gain * yin / y);
}

struct Params
{
    float frequency = 0.5f;  // 0..1, the big knob
    int   mode      = jimi;
    float pickup    = 0.5f;  // 0..1, level into the wah circuit
    float volume    = 1.0f;  // linear output gain of the effect
    bool  on        = true;
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kControlInterval = 8;  // samples between coefficient updates

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dt = 1.0 / fs;

        paramCoef   = 1.0 - std::exp (-(kControlInterval * dt) / 0.03);
        gainCoef    = 1.0 - std::exp (-dt / 0.02);
        bypassCoef  = 1.0 - std::exp (-dt / 0.01);
        inputHpCoef = onePoleCoef (20.0);
        dcBlockCoef = onePoleCoef (8.0);

        const double tb = std::tanh (kClipBias);
        clipTanhBias = tb;
        clipNorm  = kClipVolts / (1.0 - tb * tb);
        clipSlope = 1.0 / (1.0 - tb * tb);

        reset();
    }

    void reset()
    {
        positionSm = target.frequency;
        spec = kModes[modeIndex()];
        pickupSm = pickupGain (target.pickup);
        volumeSm = target.volume;
        onSm = target.on ? 1.0 : 0.0;

        updateCoefficients();
        alpha = alphaTarget;
        alphaInc = 0.0;
        controlCounter = 0;

        for (auto& c : channels)
            c = {};
    }

    void setParams (const Params& p) { target = p; }

    // Where the resonance currently sits, for the UI and the tests.
    float getResonanceHz() const { return resonanceForUi.load (std::memory_order_relaxed); }

    void process (float* const* io, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, kMaxChannels);

        const double pickupTarget = pickupGain (target.pickup);
        const double onTarget = target.on ? 1.0 : 0.0;

        for (int n = 0; n < numSamples; ++n)
        {
            if (controlCounter == 0)
                controlTick();

            controlCounter = (controlCounter + 1) % kControlInterval;

            alpha    += alphaInc;
            pickupSm += gainCoef * (pickupTarget - pickupSm);
            volumeSm += gainCoef * (target.volume - volumeSm);
            onSm     += bypassCoef * (onTarget - onSm);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& c = channels[(size_t) ch];
                const double dry = (double) io[ch][n];

                c.inputHp += inputHpCoef * (dry - c.inputHp);
                const double x = (dry - c.inputHp) * pickupSm;

                // The compensator sets how hard the circuit is driven. Half of
                // its gain (in dB) is taken back out afterwards, so it still
                // gets louder clockwise as the pedal does, but by 6 dB, not 12.
                double y = solveNode (c, x) * trim / std::sqrt (pickupSm);

                // The clipping bias leaves a signal-dependent DC offset.
                c.dcBlock += dcBlockCoef * (y - c.dcBlock);
                y -= c.dcBlock;

                io[ch][n] = (float) (onSm * y * volumeSm + (1.0 - onSm) * dry);
            }
        }
    }

private:
    struct ChannelState
    {
        double inputHp = 0.0, dcBlock = 0.0;
        double v = 0.0;        // node X
        double e1 = 0.0;       // C1 companion source
        double flux = 0.0;     // inductor flux linkage
        double iL = 0.0;       // inductor current
        double u = 0.0;        // voltage across Cf
        double iC = 0.0;       // current through Cf
    };

    double onePoleCoef (double hz) const { return 1.0 - std::exp (-2.0 * kPi * hz * dt); }

    // The compensator spans +/-12 dB, unity at noon.
    static double pickupGain (double knob) { return std::pow (10.0, (knob - 0.5) * 24.0 / 20.0); }

    size_t modeIndex() const { return (size_t) std::clamp (target.mode, 0, 2); }

    // Solves node X for one sample and returns Q1's collector swing, with the
    // inversion taken out so the effect is in phase with the dry signal.
    double solveNode (ChannelState& c, double x)
    {
        // Trapezoidal flux update; the small DCR drop uses last sample's current.
        const double fluxBase = c.flux + halfDt * c.v - dt * spec.dcr * c.iL;

        double v = c.v, collector = 0.0, iL = 0.0, u = 0.0;

        for (int iteration = 0; iteration < 6; ++iteration)
        {
            // Q1: biased tanh, unity slope at rest (same form as Sighs' stages).
            const double th = std::tanh (gain * v / kClipVolts + kClipBias);
            collector = (th - clipTanhBias) * clipNorm;
            const double collectorSlope = gain * (1.0 - th * th) * clipSlope;

            // Inductor: current rises faster than flux once the core saturates.
            const double flux = fluxBase + halfDt * v;
            const double f2 = flux * flux * invFluxSat2;
            iL = flux * invL * (1.0 + f2);
            const double iLSlope = halfDt * invL * (1.0 + 3.0 * f2);

            u = v + alpha * collector;

            const double f = gIn * (x - v - c.e1) - iL - gp * v - gC * (u - c.u) + c.iC;
            const double df = -gIn - iLSlope - gp - gC * (1.0 + alpha * collectorSlope);

            if (iteration == 5)
                break;

            const double step = std::clamp (f / df, -1.0, 1.0);
            v -= step;

            if (std::abs (step) < 1.0e-10 && iteration >= 1)
            {
                // One more pass so the states below match the final v.
                iteration = 4;
            }
        }

        const double iIn = gIn * (x - v - c.e1);
        c.e1 += twoRc1 * iIn;
        c.flux = fluxBase + halfDt * v;
        c.iL = iL;
        c.iC = gC * (u - c.u) - c.iC;
        c.u = u;
        c.v = v;

        return collector;
    }

    void updateCoefficients()
    {
        const auto d = derive (spec);
        gain = d.gain;
        gp = d.gp;

        halfDt = 0.5 * dt;
        gIn = 1.0 / (spec.r1 + halfDt / spec.c1);
        twoRc1 = dt / spec.c1;
        gC = 2.0 * spec.cf / dt;
        invL = 1.0 / spec.l;
        invFluxSat2 = 1.0 / (spec.fluxSat * spec.fluxSat);

        alphaTarget = potFraction (gain, positionSm);

        // Level the modes against each other at the middle of the sweep.
        const double mid = potFraction (gain, 0.5);
        trim = kPeakGain / smallSignalGain (spec, mid, resonanceHz (spec, gain, mid));

        resonanceForUi.store ((float) resonanceHz (spec, gain, alphaTarget), std::memory_order_relaxed);
    }

    void controlTick()
    {
        positionSm += paramCoef * ((double) target.frequency - positionSm);

        // Switching mode glides the part values rather than swapping them.
        const auto& t = kModes[modeIndex()];
        const auto glide = [this] (double& value, double to) { value += paramCoef * (to - value); };
        glide (spec.r1, t.r1);     glide (spec.c1, t.c1);     glide (spec.cf, t.cf);
        glide (spec.l, t.l);       glide (spec.dcr, t.dcr);   glide (spec.rq, t.rq);
        glide (spec.rc, t.rc);     glide (spec.re, t.re);     glide (spec.beta, t.beta);
        glide (spec.fluxSat, t.fluxSat);

        updateCoefficients();
        alphaInc = (alphaTarget - alpha) / kControlInterval;
    }

    double fs = 96000.0, dt = 1.0 / 96000.0, halfDt = 0.5 / 96000.0;
    double paramCoef = 0.0, gainCoef = 0.0, bypassCoef = 0.0;
    double inputHpCoef = 0.0, dcBlockCoef = 0.0;
    double clipTanhBias = 0.0, clipNorm = 1.0, clipSlope = 1.0;

    Params target;
    ModeSpec spec = kModes[jimi];
    double positionSm = 0.5, pickupSm = 1.0, volumeSm = 1.0, onSm = 1.0;

    double gain = 1.0, gp = 0.0, gIn = 0.0, twoRc1 = 0.0, gC = 0.0;
    double invL = 2.0, invFluxSat2 = 0.0, trim = 1.0;
    double alpha = 0.0, alphaTarget = 0.0, alphaInc = 0.0;

    int controlCounter = 0;
    std::array<ChannelState, kMaxChannels> channels {};

    std::atomic<float> resonanceForUi { 1000.0f };
};

} // namespace wah
