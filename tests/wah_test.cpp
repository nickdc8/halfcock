// Offline check of the wah engine: sweep range, response against the circuit's
// transfer function, level controls, bypass, numerical sanity, and a rendered
// audition file.
//
//   clang++ -std=c++20 -O2 tests/wah_test.cpp -o build/wah_test && build/wah_test [out.wav]

#include "../Source/WahDSP.h"

#include <complex>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

static constexpr double kFs = 96000.0;
static constexpr double kPi = 3.14159265358979323846;
static const char* const kModeNames[3] = { "Wacked", "Jimi", "Shaft" };

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok)
        ++failures;
}

struct Peak
{
    double hz = 0.0, gain = 0.0, q = 0.0;
};

// Peak frequency, peak gain and Q (from the -3 dB width) of a sampled response.
static Peak findPeak (const std::vector<double>& hz, const std::vector<double>& mag)
{
    size_t top = 0;
    for (size_t i = 1; i < mag.size(); ++i)
        if (mag[i] > mag[top])
            top = i;

    Peak p { hz[top], mag[top], 0.0 };
    const double edge = mag[top] / std::sqrt (2.0);

    const auto crossing = [&] (size_t inside, size_t outside)
    {
        const double t = (mag[inside] - edge) / (mag[inside] - mag[outside]);
        return hz[inside] * std::pow (hz[outside] / hz[inside], t);
    };

    size_t lo = top, hi = top;
    while (lo > 0 && mag[lo] > edge) --lo;
    while (hi + 1 < mag.size() && mag[hi] > edge) ++hi;

    if (mag[lo] <= edge && mag[hi] <= edge)
        p.q = p.hz / (crossing (hi - 1, hi) - crossing (lo + 1, lo));

    return p;
}

static std::vector<double> frequencyGrid()
{
    std::vector<double> hz;
    for (double f = 60.0; f < 12000.0; f *= 1.004)
        hz.push_back (f);
    return hz;
}

// The circuit's small-signal response, written out here from the part values
// rather than calling the engine's own helper.
static std::vector<double> circuitResponse (const wah::ModeSpec& m, double position,
                                            const std::vector<double>& hz, double scale)
{
    const double rLoad = 1.0 / (1.0 / m.rc + 1.0 / wah::kPotR + 1.0 / wah::kBiasR);
    const double a = rLoad / (m.re + wah::kEmitterRe);
    const double zb = wah::kBaseR + m.beta * (m.re + wah::kEmitterRe);
    const double ak = a * (zb - wah::kBaseR) / zb;
    const double alpha = (std::pow (1.0 + ak, 1.0 - position) - 1.0) / ak;

    std::vector<double> mag;
    for (double f : hz)
    {
        const std::complex<double> s (0.0, 2.0 * kPi * f);
        const auto zIn = m.r1 + 1.0 / (s * m.c1);
        const auto zL = s * m.l + m.dcr;
        const auto y = 1.0 / zIn + 1.0 / zL + 1.0 / m.rq + 1.0 / zb + s * m.cf * (1.0 + alpha * ak);
        mag.push_back (scale * std::abs (ak / (zIn * y)));
    }
    return mag;
}

// The engine's response, from its impulse response.
static std::vector<double> engineResponse (wah::Params p, const std::vector<double>& hz)
{
    wah::Engine e;
    e.setParams (p);
    e.prepare (kFs);

    const double amp = 1.0e-4;
    std::vector<float> buf ((size_t) (kFs * 0.5), 0.0f);
    buf[0] = (float) amp;
    float* io[1] = { buf.data() };
    e.process (io, 1, (int) buf.size());

    std::vector<double> mag;
    for (double f : hz)
    {
        const std::complex<double> step = std::polar (1.0, -2.0 * kPi * f / kFs);
        std::complex<double> acc (0.0, 0.0), rot (1.0, 0.0);
        for (float v : buf)
        {
            acc += (double) v * rot;
            rot *= step;
        }
        mag.push_back (std::abs (acc) / amp);
    }
    return mag;
}

static void writeWav (const char* path, const std::vector<float>& mono)
{
    FILE* f = std::fopen (path, "wb");
    if (f == nullptr)
        return;

    const uint32_t rate = 48000, dataBytes = (uint32_t) mono.size() * 2;
    const uint32_t riff = 36 + dataBytes, fmtLen = 16, byteRate = rate * 2;
    const uint16_t pcm = 1, chans = 1, align = 2, bits = 16;

    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f);
    std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&fmtLen, 4, 1, f);
    std::fwrite (&pcm, 2, 1, f); std::fwrite (&chans, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);

    for (float s : mono)
    {
        const int16_t v = (int16_t) std::lrint (std::clamp (s, -1.0f, 1.0f) * 32767.0f);
        std::fwrite (&v, 2, 1, f);
    }

    std::fclose (f);
}

// Plucked-string chord (Karplus-Strong) at 96 kHz, as a stand-in for a guitar.
static std::vector<float> pluckedChords (double seconds)
{
    std::vector<float> out ((size_t) (seconds * kFs), 0.0f);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const double chords[2][4] = { { 110.0, 164.81, 220.0, 261.63 },    // Am
                                  { 98.0, 146.83, 196.0, 246.94 } };   // G

    const double strum = 2.0;
    for (size_t c = 0; (double) c * strum < seconds; ++c)
        for (size_t note = 0; note < 4; ++note)
        {
            const size_t len = (size_t) (kFs / chords[c % 2][note]);
            std::vector<float> line (len);
            for (auto& v : line)
                v = noise (rng);

            // Soften the excitation so it reads as a string rather than a click.
            for (int pass = 0; pass < 6; ++pass)
                for (size_t i = 1; i < len; ++i)
                    line[i] = 0.5f * (line[i] + line[i - 1]);

            size_t idx = 0;
            const size_t start = (size_t) (((double) c * strum + (double) note * 0.03) * kFs);
            for (size_t n = start; n < out.size() && n < start + (size_t) (strum * 1.5 * kFs); ++n)
            {
                const size_t next = (idx + 1) % len;
                out[n] += 0.5f * line[idx];
                line[idx] = 0.4985f * (line[idx] + line[next]);
                idx = next;
            }
        }

    return out;
}

int main (int argc, char** argv)
{
    const auto hz = frequencyGrid();

    std::printf ("Engine vs circuit transfer function (small signal)\n");
    std::printf ("  mode    knob |  circuit: peak Hz   gain dB     Q |   engine: peak Hz   gain dB     Q\n");

    for (int mode = 0; mode < 3; ++mode)
    {
        const auto& spec = wah::kModes[(size_t) mode];

        // The engine trims each mode to the same peak gain at mid sweep.
        const auto mid = findPeak (hz, circuitResponse (spec, 0.5, hz, 1.0));
        const double scale = wah::kPeakGain / mid.gain;

        bool hzOk = true, gainOk = true, qOk = true, rising = true;
        double lastHz = 0.0, firstHz = 0.0;

        for (double position : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            wah::Params p;
            p.mode = mode;
            p.frequency = (float) position;

            const auto want = findPeak (hz, circuitResponse (spec, position, hz, scale));
            const auto got = findPeak (hz, engineResponse (p, hz));

            std::printf ("  %-7s %.2f |          %8.1f  %8.2f  %5.2f |          %8.1f  %8.2f  %5.2f\n",
                         kModeNames[mode], position,
                         want.hz, 20.0 * std::log10 (want.gain), want.q,
                         got.hz, 20.0 * std::log10 (got.gain), got.q);

            hzOk = hzOk && std::abs (got.hz / want.hz - 1.0) < 0.03;
            gainOk = gainOk && std::abs (got.gain / want.gain - 1.0) < 0.10;
            qOk = qOk && want.q > 0.0 && std::abs (got.q / want.q - 1.0) < 0.10;
            rising = rising && got.hz > lastHz;
            lastHz = got.hz;
            if (position == 0.0)
                firstHz = got.hz;
        }

        std::printf ("  %s sweeps %.0f Hz to %.0f Hz\n", kModeNames[mode], firstHz, lastHz);
        check (hzOk, "peak frequency within 3% of the circuit");
        check (gainOk, "peak gain within 10% of the circuit");
        check (qOk, "Q within 10% of the circuit");
        check (rising, "frequency rises with the knob");
    }

    std::printf ("\nLevel controls (Jimi, mid sweep, small signal)\n");
    {
        wah::Params p;
        const double ref = findPeak (hz, engineResponse (p, hz)).gain;

        // +/-12 dB into the circuit, half of it made up afterwards.
        bool pickupOk = true;
        for (float pickup : { 0.0f, 0.25f, 0.75f, 1.0f })
        {
            p.pickup = pickup;
            const double g = findPeak (hz, engineResponse (p, hz)).gain;
            const double expected = (pickup - 0.5) * 12.0;
            pickupOk = pickupOk && std::abs (20.0 * std::log10 (g / ref) - expected) < 0.2;
        }
        check (pickupOk, "Pickup Compensator moves small-signal level by +/-6 dB");

        p.pickup = 0.5f;
        p.volume = 2.0f;
        const double loud = findPeak (hz, engineResponse (p, hz)).gain;
        check (std::abs (loud / ref - 2.0) < 0.02, "Volume scales the output");
        std::printf ("  peak gain at Volume noon: %.2f dB\n", 20.0 * std::log10 (ref));
    }

    std::printf ("\nDrive: a hot input compresses the peak\n");
    {
        const auto peakRatio = [] (float pickup)
        {
            wah::Engine e;
            wah::Params p;
            p.pickup = pickup;
            e.setParams (p);
            e.prepare (kFs);

            const double f = e.getResonanceHz();
            std::vector<float> buf ((size_t) kFs);
            for (size_t i = 0; i < buf.size(); ++i)
                buf[i] = 0.5f * (float) std::sin (2.0 * kPi * f * (double) i / kFs);
            float* io[1] = { buf.data() };
            e.process (io, 1, (int) buf.size());

            double peak = 0.0;
            for (size_t i = buf.size() / 2; i < buf.size(); ++i)
                peak = std::max (peak, (double) std::abs (buf[i]));
            return peak / 0.5;
        };

        const double soft = peakRatio (0.0f), hard = peakRatio (1.0f);
        std::printf ("  gain at resonance, 0.5 input: compensator min %.2f, max %.2f\n", soft, hard);
        check (hard < soft && hard < wah::kPeakGain, "more compensator means more compression");
    }

    std::printf ("\nBypass\n");
    {
        wah::Engine e;
        wah::Params p;
        p.on = false;
        e.setParams (p);
        e.prepare (kFs);

        std::mt19937 rng (5);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::vector<float> in (4096), buf;
        for (auto& v : in)
            v = noise (rng);
        buf = in;
        float* io[1] = { buf.data() };
        e.process (io, 1, (int) buf.size());

        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max (worst, std::abs (buf[i] - in[i]));
        check (worst < 1.0e-6f, "off passes the input through unchanged");
    }

    std::printf ("\nStress: full-scale noise, everything moving\n");
    {
        wah::Engine e;
        wah::Params p;
        e.setParams (p);
        e.prepare (kFs);

        std::mt19937 rng (1);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::vector<float> l (512), r (512);
        float* io[2] = { l.data(), r.data() };
        bool finite = true;
        float peak = 0.0f;

        for (int b = 0; b < 3000; ++b)
        {
            for (size_t i = 0; i < 512; ++i)
            {
                l[i] = noise (rng);
                r[i] = (i / 40) % 2 == 0 ? 1.0f : -1.0f;   // full-scale square
            }

            p.frequency = (b % 3 == 0) ? 0.0f : ((b % 3 == 1) ? 1.0f : 0.5f);
            p.mode = (b / 7) % 3;
            p.pickup = (b / 11) % 2 == 0 ? 1.0f : 0.0f;
            p.on = (b / 5) % 4 != 0;
            e.setParams (p);
            e.process (io, 2, 512);

            for (size_t i = 0; i < 512; ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
            }
        }

        std::printf ("  peak %.2f\n", peak);
        check (finite, "output stays finite");
        check (peak < 20.0f, "output stays bounded");
    }

    if (argc > 1)
    {
        // Each mode in turn: parked low, parked in the middle, then a slow sweep.
        std::vector<float> out;

        for (int mode = 0; mode < 3; ++mode)
        {
            auto wet = pluckedChords (12.0);
            wah::Engine e;
            wah::Params p;
            p.mode = mode;
            p.frequency = 0.25f;
            e.setParams (p);
            e.prepare (kFs);

            const int block = 64;
            for (size_t pos = 0; pos + block <= wet.size(); pos += block)
            {
                const double t = (double) pos / kFs;
                p.frequency = t < 4.0 ? 0.25f
                            : t < 8.0 ? 0.55f
                                      : (float) (0.5 - 0.5 * std::cos (2.0 * kPi * (t - 8.0) / 2.0));
                e.setParams (p);
                float* io[1] = { wet.data() + pos };
                e.process (io, 1, block);
            }

            // Decimate 2:1; the wah leaves very little above 10 kHz.
            for (size_t i = 0; i + 1 < wet.size(); i += 2)
                out.push_back (0.35f * (wet[i] + wet[i + 1]));
        }

        writeWav (argv[1], out);
        std::printf ("\nWrote %s (Wacked, Jimi, Shaft: 12 s each)\n", argv[1]);
    }

    std::printf ("\n%s\n", failures == 0 ? "All checks passed" : "CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
