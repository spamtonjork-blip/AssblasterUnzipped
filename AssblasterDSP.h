// AssblasterDSP.h
// ---------------------------------------------------------------------------
// An *attempted* software model of the Metasonix KV-100 "Assblaster".
// Header-only, no JUCE dependency, one Channel instance per audio channel.
//
// Signal chain (per Metasonix's own description of the hardware):
//
//   in -> [TM-7 style two-stage pentode preamp, screen-voltage knob on V2]
//      -> [TM-1 style "pulser" waveshaper  + ring modulator]
//      -> [TM-3 style thyratron VCO (signal-degradation, not a synth osc)]
//      -> [wide-sweep bandpass filter, envelope-follower / wah controlled]
//      -> [vactrol noise gate driven by the envelope follower]
//      -> out
//
// This is NOT a circuit-level (SPICE / WDF) emulation. It is a behavioural
// model built from public descriptions of the unit, so treat it as "inspired
// by", not "sounds identical to". Run it at an oversampled rate (4x is a
// good default); everything here is deliberately nonlinear.
// ---------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>

namespace assblaster
{

constexpr float kPi = 3.14159265358979323846f;

inline float fastTanh (float x) noexcept
{
    // Pade approximant, exact-ish to |x|<3 and saturates to +-1 exactly at 3.
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

// Transparent below ~0.8, smoothly saturating to 1.0. Pure safety net so a
// hot setting can't send +20 dBFS into somebody's monitors.
inline float safetyClip (float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::fabs (x);
    if (a <= knee)
        return x;
    const float y = knee + (1.0f - knee) * fastTanh ((a - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

// ------------------------------------------------------------------ helpers
struct Smooth
{
    float cur = 0.0f, tgt = 0.0f, a = 1.0f;

    void setTime (float fs, float ms) noexcept { a = 1.0f - std::exp (-1.0f / (0.001f * ms * fs)); }
    void snap (float v) noexcept { cur = tgt = v; }
    void set (float v) noexcept { tgt = v; }
    float next() noexcept { cur += a * (tgt - cur); return cur; }
};

struct OnePoleLP
{
    float a = 1.0f, y = 0.0f;

    void setCutoff (float fs, float hz) noexcept
    {
        hz = std::min (hz, 0.45f * fs);
        a = 1.0f - std::exp (-2.0f * kPi * hz / fs);
    }
    void reset() noexcept { y = 0.0f; }
    float process (float x) noexcept { y += a * (x - y); return y; }
};

struct DCBlock
{
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;

    void setCutoff (float fs, float hz) noexcept { r = std::exp (-2.0f * kPi * hz / fs); }
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

// ---------------------------------------------------------------- user params
struct Params
{
    // Preamp (TM-7 style)
    float inputDb   = 6.0f;    // -24 .. +36
    float screen    = 0.5f;    // 0 .. 1   V2 screen voltage (low = starved, gritty, quieter)
    float levelDb   = 0.0f;    // -36 .. +12  preamp output level into the pulser

    // Pulser (TM-1 style) + ring mod
    bool  pulserOn     = false;
    float pulserAmount = 0.6f; // 0 .. 1
    float pulserThresh = 0.3f; // 0 .. 1   Schmitt trigger threshold
    float pulserRatio  = 1.5f; // 0.5 .. 8  sync-osc frequency as a multiple of detected input pitch
    float ringMod      = 0.0f; // 0 .. 1   carrier = sidechain if present, else the VCO

    // Thyratron VCO (TM-3 style)
    bool  vcoOn      = false;
    float vcoPitchHz = 110.0f; // 20 .. 5000
    float vcoLevel   = 0.4f;   // 0 .. 1
    float vcoGrid    = 0.4f;   // 0 .. 1   how hard the signal drives the thyratron's control grid

    // Filter
    bool  filterOn   = false;
    float filterHz   = 800.0f; // 60 .. 12000
    float filterQ    = 3.0f;   // 0.6 .. 14
    float filterEnv  = 0.0f;   // -1 .. 1  envelope -> cutoff (5 octaves at full)
    float envDecayMs = 120.0f; // 10 .. 1000

    // Gate (vactrol)
    float gate = 0.0f;         // 0 = off, 1 = very aggressive

    // "Terminal edition" chaos switch
    bool  chaos = false;

    float masterDb = 0.0f;     // plugin-side convenience, not on the hardware
};

// ------------------------------------------------------------- pentode stage
// Behavioural pentode: 3/2-power plate-current law with a smooth cutoff,
// grid-current limiting on positive excursions, and a plate-load knee.
struct PentodeStage
{
    static float process (float x, float bias, float headroom, float gain) noexcept
    {
        x *= gain;
        // positive grid excursions draw grid current through the source impedance
        const float g = x < 0.0f ? x : x / std::sqrt (1.0f + 0.25f * x * x);
        const float v = bias + g;
        const float s = 0.5f * (v + std::sqrt (v * v + 0.01f)); // smooth cutoff
        const float ip = s * std::sqrt (s);                      // Child-Langmuir 3/2
        const float y = ip / (1.0f + ip / headroom);             // plate load line knee
        return -y;                                               // inverting stage
    }
};

// ---------------------------------------------------------------- pulser
// Schmitt trigger + minimum-pulse hold ("monostable-ish") tracks the input,
// and a hard-synced saw runs at ratio * detected pitch. Result: "like an
// oscillator that is forced to sync to the input waveform".
struct Pulser
{
    float fs = 48000.0f;
    bool high = false;
    int sinceFlip = 1 << 28, sinceRise = 1 << 28;
    float period = 436.0f, phase = 0.0f, sawGate = 0.0f, gateA = 0.01f;
    float peak = 0.0f, peakDecay = 0.9999f;
    OnePoleLP sqLP;
    DCBlock dc;

    void prepare (float sampleRate) noexcept
    {
        fs = sampleRate;
        sqLP.setCutoff (fs, 9000.0f);
        dc.setCutoff (fs, 15.0f);
        gateA = 1.0f - std::exp (-1.0f / (0.004f * fs));
        peakDecay = std::exp (-1.0f / (0.4f * fs));
        reset();
    }

    void reset() noexcept
    {
        high = false;
        sinceFlip = sinceRise = 1 << 28;
        period = fs / 110.0f;
        phase = sawGate = peak = 0.0f;
        sqLP.reset();
        dc.reset();
    }

    float process (float x, float thresh, float ratio) noexcept
    {
        // threshold is relative to the recent signal peak so the pulser keeps
        // tracking quiet playing instead of falling silent
        peak = std::max (std::fabs (x), peak * peakDecay);
        const float th = (0.03f + thresh * 0.8f) * std::max (peak, 0.05f);
        const int hold = static_cast<int> (0.00025f * fs);
        ++sinceFlip;
        if (sinceRise < (1 << 28))
            ++sinceRise;

        bool rising = false;
        if (! high && x > th && sinceFlip > hold)       { high = true;  rising = true; sinceFlip = 0; }
        else if (high && x < -0.4f * th && sinceFlip > hold) { high = false; sinceFlip = 0; }

        if (rising)
        {
            const float p = static_cast<float> (sinceRise);
            if (p > fs / 6000.0f && p < fs / 25.0f)
                period += 0.35f * (p - period);
            sinceRise = 0;
            phase = 0.0f; // hard sync
        }

        phase += ratio / std::max (period, 8.0f);
        phase -= std::floor (phase);

        const float alive = (sinceRise < static_cast<int> (0.05f * fs)) ? 1.0f : 0.0f;
        sawGate += gateA * (alive - sawGate);

        const float sq = sqLP.process (high ? 1.0f : -1.0f);
        const float saw = (2.0f * phase - 1.0f) * sawGate;
        return dc.process (0.35f * (0.55f * sq + 0.45f * saw));
    }
};

// ---------------------------------------------------------------- thyratron
// Relaxation oscillator: cap charges exponentially toward the supply; the
// gas tube strikes at a voltage set by its control grid, then dumps the cap
// down to the extinction voltage. The input signal rides on the grid, so
// the strike point (and therefore pitch + amplitude) is modulated by the audio.
struct Thyratron
{
    float fs = 48000.0f, v = 0.15f;
    static constexpr float supply = 1.6f, extinct = 0.15f;
    DCBlock dc;

    void prepare (float sampleRate) noexcept { fs = sampleRate; dc.setCutoff (fs, 12.0f); reset(); }
    void reset() noexcept { v = extinct; dc.reset(); }

    float process (float freqHz, float grid, float gridAmt) noexcept
    {
        // exponential FM from the grid, up to +-1.5 octaves at full
        float f = freqHz * std::exp2 (gridAmt * 1.5f * std::clamp (grid, -1.5f, 1.5f));
        f = std::clamp (f, 5.0f, 0.2f * fs);

        // charging from extinct->1.0 toward 'supply' takes 1/f seconds
        constexpr float lnRatio = 0.882f; // ln((supply-1.0)/(supply-extinct))^-1
        const float k = 1.0f - std::exp (-lnRatio * f / fs);

        const float strike = std::max (0.3f, 1.0f + gridAmt * 0.7f * std::clamp (grid, -1.5f, 1.5f));
        v += (supply - v) * k;
        if (v >= strike)
            v = extinct;

        return dc.process ((v - 0.55f) * 1.6f);
    }
};

// ---------------------------------------------------------------- SVF (TPT)
struct BandpassSVF
{
    float ic1 = 0.0f, ic2 = 0.0f;
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    // returns peak-normalised bandpass
    float process (float x, float g, float k) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return k * v1;
    }
};

// ---------------------------------------------------------------- Channel
class Channel
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = static_cast<float> (sampleRate);

        for (auto* s : { &sIn, &sLevel, &sMaster, &sScreen, &sPulser, &sRing, &sVco, &sVcoPitch,
                         &sFilterMix, &sFilterLog, &sQ, &sChaos, &sGate })
            s->setTime (fs, 8.0f);
        sFilterLog.setTime (fs, 15.0f);

        dc1.setCutoff (fs, 25.0f);
        dc2.setCutoff (fs, 25.0f);
        dcOut.setCutoff (fs, 10.0f);
        miller.setCutoff (fs, 14000.0f);
        pulser.prepare (fs);
        vco.prepare (fs);

        envAtk = 1.0f - std::exp (-1.0f / (0.0015f * fs));
        vacAtk = 1.0f - std::exp (-1.0f / (0.008f * fs));
        vacRel = 1.0f - std::exp (-1.0f / (0.090f * fs));
        setParams (Params {});
        snapSmoothers();
        reset();
    }

    void reset() noexcept
    {
        dc1.reset(); dc2.reset(); dcOut.reset(); miller.reset();
        pulser.reset(); vco.reset(); bp.reset();
        env = 0.0f; vactrol = 1.0f; gateOpen = true; feedback = 0.0f;
    }

    void setParams (const Params& p) noexcept
    {
        sIn.set (dbToGain (p.inputDb));
        sLevel.set (dbToGain (p.levelDb));
        sMaster.set (dbToGain (p.masterDb));
        sScreen.set (p.screen);
        sPulser.set (p.pulserOn ? p.pulserAmount : 0.0f);
        sRing.set (p.ringMod);
        sVco.set (p.vcoOn ? p.vcoLevel : 0.0f);
        sVcoPitch.set (p.vcoPitchHz);
        sFilterMix.set (p.filterOn ? 1.0f : 0.0f);
        sFilterLog.set (std::log2 (std::max (p.filterHz, 20.0f)));
        sQ.set (p.filterQ);
        sChaos.set (p.chaos ? 0.7f : 0.0f);
        sGate.set (p.gate);

        pulserThresh = p.pulserThresh;
        pulserRatio = p.pulserRatio;
        vcoGrid = p.vcoGrid;
        filterEnv = p.filterEnv;
        envRel = 1.0f - std::exp (-1.0f / (0.001f * std::max (p.envDecayMs, 5.0f) * fs));
    }

    void snapSmoothers() noexcept
    {
        for (auto* s : { &sIn, &sLevel, &sMaster, &sScreen, &sPulser, &sRing, &sVco, &sVcoPitch,
                         &sFilterMix, &sFilterLog, &sQ, &sChaos, &sGate })
            s->snap (s->tgt);
    }

    // carrier / hasCarrier: optional external ring-mod carrier (sidechain)
    float process (float in, float carrier = 0.0f, bool hasCarrier = false) noexcept
    {
        const float inGain = sIn.next();
        const float levelGain = sLevel.next();
        const float master = sMaster.next();
        const float screen = sScreen.next();
        const float pulserAmt = sPulser.next();
        const float ringAmt = sRing.next();
        const float vcoLevel = sVco.next();
        const float vcoHz = sVcoPitch.next();
        const float filtMix = sFilterMix.next();
        const float filtLog = sFilterLog.next();
        const float q = sQ.next();
        const float chaosAmt = sChaos.next();
        const float gateAmt = sGate.next();

        // ---- envelope follower (post input-gain, pre-tube)
        float x = in * inGain;
        const float ax = std::fabs (x);
        env += (ax > env ? envAtk : envRel) * (ax - env);

        // ---- terminal-edition chaos: filter output fed back to the grid
        x += chaosAmt * fastTanh (3.0f * feedback);

        // ---- TM-7 style preamp: V1 -> V2 (screen voltage on V2)
        float a = PentodeStage::process (x, 0.45f, 2.0f, 2.5f);
        a = miller.process (dc1.process (a));

        // starved screen: bias nearer cutoff, less headroom (=> grit, less level)
        const float bias2 = 0.10f + 0.50f * screen;
        const float headroom2 = 0.35f + 1.65f * screen;
        float b = dc2.process (PentodeStage::process (a, bias2, headroom2, 2.0f));
        b *= levelGain * kPreNorm;

        // ---- TM-1 style pulser + ring mod
        const float pOut = pulser.process (b, pulserThresh, pulserRatio);
        float sig = b * (1.0f - pulserAmt) + pOut * pulserAmt;

        // ---- TM-3 style thyratron (always runs so ring-mod can borrow it)
        const float vOut = vco.process (vcoHz, std::clamp (sig, -1.5f, 1.5f), vcoGrid);
        const float car = hasCarrier ? std::clamp (carrier, -2.0f, 2.0f) : vOut * 1.4f;
        sig = sig * (1.0f - ringAmt) + ringAmt * (sig * car * 2.5f);
        sig += vcoLevel * 0.5f * vOut;

        // ---- bandpass filter (env / wah)
        const float envCV = fastTanh (env * 2.5f);
        float fc = std::exp2 (filtLog + filterEnv * envCV * 5.0f);
        fc = std::clamp (fc, 40.0f, std::min (18000.0f, 0.45f * fs));
        const float g = std::tan (kPi * fc / fs);
        const float k = 1.0f / std::max (q, 0.5f);
        const float bpOut = fastTanh (bp.process (sig, g, k) * (3.0f * std::sqrt (q)));
        sig = sig * (1.0f - filtMix) + bpOut * filtMix;

        feedback = sig;

        // ---- vactrol noise gate (always in circuit; gate==0 means threshold floor)
        if (gateAmt > 0.001f)
        {
            const float thr = dbToGain (-85.0f + gateAmt * 65.0f); // -85 .. -20 dB
            if (! gateOpen && env > thr)          gateOpen = true;
            else if (gateOpen && env < 0.7f * thr) gateOpen = false;
        }
        else
            gateOpen = true;

        const float target = gateOpen ? 1.0f : 0.0f;
        vactrol += (target > vactrol ? vacAtk : vacRel) * (target - vactrol);
        sig *= vactrol * vactrol;

        float y = safetyClip (dcOut.process (sig) * master);
        if (! std::isfinite (y))
        {
            reset();
            y = 0.0f;
        }
        return y;
    }

private:
    static constexpr float kPreNorm = 1.0f;

    float fs = 48000.0f;

    Smooth sIn, sLevel, sMaster, sScreen, sPulser, sRing, sVco, sVcoPitch,
           sFilterMix, sFilterLog, sQ, sChaos, sGate;

    DCBlock dc1, dc2, dcOut;
    OnePoleLP miller;
    Pulser pulser;
    Thyratron vco;
    BandpassSVF bp;

    float pulserThresh = 0.3f, pulserRatio = 1.5f, vcoGrid = 0.4f, filterEnv = 0.0f;
    float env = 0.0f, envAtk = 0.1f, envRel = 0.001f;
    float vactrol = 0.0f, vacAtk = 0.01f, vacRel = 0.001f;
    bool  gateOpen = false;
    float feedback = 0.0f;
};

} // namespace assblaster
