#include "../Source/AssblasterDSP.h"
#include <cstdio>
#include <vector>
#include <random>
#include <string>
using namespace assblaster;

static std::vector<float> pluck (float fs, float secs, float f0, float amp, unsigned seed = 1)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<float> n (-1.f, 1.f);
    std::vector<float> x ((size_t) (fs * secs));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float t = i / fs, e = std::exp (-t / 0.5f);
        float s = std::sin (2*kPi*f0*t) + 0.5f*std::sin (2*kPi*2*f0*t) + 0.25f*std::sin (2*kPi*3*f0*t);
        // note-off at 1.2s -> only noise floor
        if (t > 1.2f) { s = 0.f; }
        x[i] = amp * (s * e / 1.4f) + 0.0003f * n (rng);
    }
    return x;
}

struct Stats { float peak, rmsDb, dc; bool ok; };

static Stats run (const Params& p, const std::vector<float>& in, float fs, const char* = nullptr, size_t from = 0, size_t to = 0)
{
    Channel ch; ch.prepare (fs); ch.setParams (p); ch.snapSmoothers();
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = ch.process (in[i]);
    double e = 0, dc = 0; float pk = 0; bool ok = true; size_t a = from, b = to ? to : out.size(), n = 0;
    for (size_t i = 0; i < out.size(); ++i) if (! std::isfinite (out[i])) ok = false;
    for (size_t i = a; i < b; ++i) { e += out[i]*out[i]; dc += out[i]; pk = std::max (pk, std::fabs (out[i])); ++n; }
    return { pk, 10.f*std::log10 ((float) (e/n) + 1e-12f), (float) (dc/n), ok };
}

int main()
{
    const float fs = 96000.f;
    auto sig = pluck (fs, 2.0f, 110.f, 0.3f);           // loud-ish pluck
    auto quiet = pluck (fs, 2.0f, 110.f, 0.03f);
    // 0.1..0.9s = sustained part
    const size_t A = (size_t)(0.1f*fs), B = (size_t)(0.9f*fs);
    const size_t N0 = (size_t)(1.85f*fs), N1 = (size_t)(1.99f*fs); // post note-off noise floor

    struct Case { const char* name; Params p; };
    std::vector<Case> cases;
    auto add = [&] (const char* n, auto fn) { Params p; fn (p); cases.push_back ({ n, p }); };
    add ("default (preamp only)",       [](Params&){});
    add ("screen=0 (starved)",          [](Params& p){ p.screen = 0.f; });
    add ("screen=1 (full)",             [](Params& p){ p.screen = 1.f; });
    add ("input +30dB",                 [](Params& p){ p.inputDb = 30.f; });
    add ("pulser on",                   [](Params& p){ p.pulserOn = true; p.pulserAmount = 1.f; });
    add ("pulser on ratio 3.3",         [](Params& p){ p.pulserOn = true; p.pulserAmount = 1.f; p.pulserRatio = 3.3f; });
    add ("ringmod 1.0 (VCO carrier)",   [](Params& p){ p.ringMod = 1.f; });
    add ("vco on",                      [](Params& p){ p.vcoOn = true; p.vcoLevel = 1.f; });
    add ("filter on 800Hz Q3",          [](Params& p){ p.filterOn = true; });
    add ("filter env +1",               [](Params& p){ p.filterOn = true; p.filterEnv = 1.f; p.filterHz = 200.f; });
    add ("filter Q14",                  [](Params& p){ p.filterOn = true; p.filterQ = 14.f; });
    add ("gate 0.6",                    [](Params& p){ p.gate = 0.6f; });
    add ("chaos + input +20",           [](Params& p){ p.chaos = true; p.inputDb = 20.f; });
    add ("EVERYTHING on, +36dB",        [](Params& p){ p.inputDb = 36.f; p.levelDb = 12.f; p.pulserOn = true; p.pulserAmount = 1.f; p.ringMod = 1.f;
                                                       p.vcoOn = true; p.vcoLevel = 1.f; p.vcoGrid = 1.f; p.filterOn = true; p.filterQ = 14.f;
                                                       p.filterEnv = 1.f; p.chaos = true; p.masterDb = 6.f; });

    std::printf ("%-30s %8s %9s %9s  %s\n", "case (loud pluck, sustain)", "peak", "RMS dBFS", "DC", "finite");
    for (auto& c : cases)
    {
        auto s = run (c.p, sig, fs, nullptr, A, B);
        std::printf ("%-30s %8.3f %9.1f %9.4f  %s\n", c.name, s.peak, s.rmsDb, s.dc, s.ok ? "yes" : "NO!!");
    }

    std::printf ("\nGate test: RMS of noise-floor tail (1.85-1.99s, after note off), input noise ~ -70 dBFS\n");
    for (float g : { 0.f, 0.3f, 0.6f, 0.9f })
    {
        Params p; p.gate = g; p.inputDb = 20.f; p.screen = 1.f;
        auto s = run (p, sig, fs, nullptr, N0, N1);
        auto t = run (p, sig, fs, nullptr, A, B);
        std::printf ("gate=%.1f  sustain %7.1f dB   tail %7.1f dB\n", g, t.rmsDb, s.rmsDb);
    }

    std::printf ("\nQuiet-input level (0.03 amp) for sanity:\n");
    for (auto& c : { cases[0], cases[4], cases[8] })
    { auto s = run (c.p, quiet, fs, nullptr, A, B); std::printf ("%-30s peak %.3f rms %.1f dB\n", c.name, s.peak, s.rmsDb); }

    return 0;
}
