#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "AssblasterDSP.h"

class AssblasterProcessor : public juce::AudioProcessor
{
public:
    AssblasterProcessor();
    ~AssblasterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Assblaster"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    assblaster::Params readParams() const;

    static constexpr int kOversampleOrder = 2; // 2^2 = 4x
    static constexpr int kMaxChannels = 2;

    juce::dsp::Oversampling<float> oversampling { kMaxChannels, kOversampleOrder,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::dsp::Oversampling<float> oversamplingSC { kMaxChannels, kOversampleOrder,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };

    assblaster::Channel channels[kMaxChannels];

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 2048 };
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> mixSmoothed;

    juce::AudioParameterBool* bypassParam = nullptr;

    // raw parameter pointers (atomic floats owned by apvts)
    std::atomic<float> *pInput, *pScreen, *pLevel, *pPulserOn, *pPulserAmt, *pPulserThr, *pPulserRatio,
        *pRing, *pVcoOn, *pVcoPitch, *pVcoLevel, *pVcoGrid, *pFiltOn, *pFiltHz, *pFiltQ, *pFiltEnv,
        *pEnvDecay, *pGate, *pChaos, *pMaster, *pMix, *pBypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssblasterProcessor)
};
