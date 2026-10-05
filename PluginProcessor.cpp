#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [lo, hi] (float, float, float v) { return lo * std::pow (hi / lo, v); },
             [lo, hi] (float, float, float v) { return std::log (v / lo) / std::log (hi / lo); },
             [lo, hi] (float, float, float v) { return juce::jlimit (lo, hi, v); } };
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AssblasterProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using Attr = juce::AudioParameterFloatAttributes;
    using juce::NormalisableRange;
    using juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto f = [&l] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit = "", int decimals = 2)
    {
        l.add (std::make_unique<F> (ParameterID { id, 1 }, name, r, def,
                                    Attr().withLabel (unit)
                                          .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); })
                                          .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));
    };
    auto b = [&l] (const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<B> (ParameterID { id, 1 }, name, def));
    };

    // preamp
    f ("in_db",      "Input",         { -24.0f, 36.0f, 0.1f }, 6.0f, " dB", 1);
    f ("screen",     "V2 Screen",     { 0.0f, 1.0f, 0.001f },  0.5f);
    f ("level_db",   "Preamp Level",  { -36.0f, 12.0f, 0.1f }, 0.0f, " dB", 1);
    // pulser / ring
    b ("pulser_on",  "Pulser",        false);
    f ("pulser_amt", "Pulser Amount", { 0.0f, 1.0f, 0.001f }, 0.7f);
    f ("pulser_thr", "Pulser Thresh", { 0.0f, 1.0f, 0.001f }, 0.3f);
    f ("pulser_rat", "Pulser Ratio",  logRange (0.5f, 8.0f),  1.5f, "x", 2);
    f ("ring",       "Ring Mod",      { 0.0f, 1.0f, 0.001f }, 0.0f);
    // vco
    b ("vco_on",     "VCO",           false);
    f ("vco_pitch",  "VCO Pitch",     logRange (20.0f, 5000.0f), 110.0f, " Hz", 0);
    f ("vco_level",  "VCO Level",     { 0.0f, 1.0f, 0.001f }, 0.4f);
    f ("vco_grid",   "VCO Grid",      { 0.0f, 1.0f, 0.001f }, 0.4f);
    // filter
    b ("filt_on",    "Filter",        false);
    f ("filt_hz",    "Filter Freq",   logRange (60.0f, 12000.0f), 800.0f, " Hz", 0);
    f ("filt_q",     "Filter Q",      logRange (0.6f, 14.0f), 3.0f, "", 1);
    f ("filt_env",   "Filter Env",    { -1.0f, 1.0f, 0.001f }, 0.0f);
    f ("env_decay",  "Env Decay",     logRange (10.0f, 1000.0f), 120.0f, " ms", 0);
    // gate / chaos / out
    f ("gate",       "Gate",          { 0.0f, 1.0f, 0.001f }, 0.0f);
    b ("chaos",      "Chaos",         false);
    f ("master_db",  "Master",        { -36.0f, 12.0f, 0.1f }, 0.0f, " dB", 1);
    f ("mix",        "Mix",           { 0.0f, 1.0f, 0.001f }, 1.0f);
    b ("bypass",     "Bypass",        false);

    return l;
}

AssblasterProcessor::AssblasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",   juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Carrier", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pInput = get ("in_db");       pScreen = get ("screen");        pLevel = get ("level_db");
    pPulserOn = get ("pulser_on"); pPulserAmt = get ("pulser_amt"); pPulserThr = get ("pulser_thr");
    pPulserRatio = get ("pulser_rat"); pRing = get ("ring");
    pVcoOn = get ("vco_on");      pVcoPitch = get ("vco_pitch");   pVcoLevel = get ("vco_level");
    pVcoGrid = get ("vco_grid");  pFiltOn = get ("filt_on");       pFiltHz = get ("filt_hz");
    pFiltQ = get ("filt_q");      pFiltEnv = get ("filt_env");     pEnvDecay = get ("env_decay");
    pGate = get ("gate");         pChaos = get ("chaos");          pMaster = get ("master_db");
    pMix = get ("mix");           pBypass = get ("bypass");

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

bool AssblasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (mainIn != mainOut)
        return false;

    const auto& sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void AssblasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    oversampling.initProcessing (static_cast<size_t> (samplesPerBlock));
    oversamplingSC.initProcessing (static_cast<size_t> (samplesPerBlock));
    oversampling.reset();
    oversamplingSC.reset();

    const double osRate = sampleRate * static_cast<double> (1 << kOversampleOrder);
    for (auto& c : channels)
    {
        c.prepare (osRate);
        c.setParams (readParams());
        c.snapSmoothers();
    }

    const int latency = juce::roundToInt (oversampling.getLatencyInSamples());
    setLatencySamples (latency);

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlock), 2 };
    dryDelay.prepare (spec);
    dryDelay.setDelay (static_cast<float> (latency));
    dryDelay.reset();

    dryBuffer.setSize (2, samplesPerBlock, false, false, true);
    mixSmoothed.reset (sampleRate, 0.015);
    mixSmoothed.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 0.0f : pMix->load());
}

assblaster::Params AssblasterProcessor::readParams() const
{
    assblaster::Params p;
    p.inputDb      = pInput->load();
    p.screen       = pScreen->load();
    p.levelDb      = pLevel->load();
    p.pulserOn     = pPulserOn->load() > 0.5f;
    p.pulserAmount = pPulserAmt->load();
    p.pulserThresh = pPulserThr->load();
    p.pulserRatio  = pPulserRatio->load();
    p.ringMod      = pRing->load();
    p.vcoOn        = pVcoOn->load() > 0.5f;
    p.vcoPitchHz   = pVcoPitch->load();
    p.vcoLevel     = pVcoLevel->load();
    p.vcoGrid      = pVcoGrid->load();
    p.filterOn     = pFiltOn->load() > 0.5f;
    p.filterHz     = pFiltHz->load();
    p.filterQ      = pFiltQ->load();
    p.filterEnv    = pFiltEnv->load();
    p.envDecayMs   = pEnvDecay->load();
    p.gate         = pGate->load();
    p.chaos        = pChaos->load() > 0.5f;
    p.masterDb     = pMaster->load();
    return p;
}

void AssblasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer (buffer, true, 0);
    const int numCh = juce::jmin (mainBus.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numCh == 0 || numSamples == 0)
        return;

    // ---- keep an aligned dry copy for Mix / Bypass
    dryBuffer.setSize (2, numSamples, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        dryBuffer.copyFrom (ch, 0, mainBus, juce::jmin (ch, numCh - 1), 0, numSamples);
    {
        juce::dsp::AudioBlock<float> dryBlock (dryBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx (dryBlock);
        dryDelay.process (ctx);
    }

    // ---- optional ring-mod carrier (sidechain)
    auto* scBus = getBus (true, 1);
    const bool hasCarrier = scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0;
    juce::dsp::AudioBlock<float> scOsBlock;
    int scChannels = 0;
    if (hasCarrier)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        scChannels = juce::jmin (sc.getNumChannels(), kMaxChannels);
        juce::dsp::AudioBlock<float> scBlock (sc.getArrayOfWritePointers(), static_cast<size_t> (scChannels),
                                              static_cast<size_t> (numSamples));
        scOsBlock = oversamplingSC.processSamplesUp (scBlock);
    }

    // ---- wet path, oversampled
    const auto params = readParams();
    juce::dsp::AudioBlock<float> block (mainBus.getArrayOfWritePointers(), static_cast<size_t> (numCh),
                                        static_cast<size_t> (numSamples));
    auto osBlock = oversampling.processSamplesUp (block);
    const size_t osN = osBlock.getNumSamples();

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& core = channels[ch];
        core.setParams (params);
        float* d = osBlock.getChannelPointer (static_cast<size_t> (ch));
        const float* c = hasCarrier ? scOsBlock.getChannelPointer (static_cast<size_t> (juce::jmin (ch, scChannels - 1)))
                                    : nullptr;
        for (size_t i = 0; i < osN; ++i)
            d[i] = core.process (d[i], c != nullptr ? c[i] : 0.0f, hasCarrier);
    }

    oversampling.processSamplesDown (block);

    // The downsampling filter can ring slightly past the in-loop safety clip on
    // pathological settings, so clamp the wet signal once more at the base rate.
    // (Transparent below -1.9 dBFS; applied before Mix so bypass stays bit-exact.)
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* w = mainBus.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            w[i] = assblaster::safetyClip (w[i]);
    }

    // ---- Mix / Bypass crossfade
    mixSmoothed.setTargetValue (pBypass->load() > 0.5f ? 0.0f : pMix->load());
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* w = mainBus.getWritePointer (ch);
            const float dry = dryBuffer.getSample (juce::jmin (ch, 1), i);
            w[i] = dry + m * (w[i] - dry);
        }
    }
}

juce::AudioProcessorEditor* AssblasterProcessor::createEditor() { return new AssblasterEditor (*this); }

void AssblasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AssblasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AssblasterProcessor(); }
