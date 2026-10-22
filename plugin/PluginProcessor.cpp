#include "PluginProcessor.h"

namespace samplit
{
    juce::AudioProcessor::BusesProperties SamplitProcessor::makeBuses()
    {
        // Шина 0 это Main. Шины 1..8 это отдельные выходы пэдов. DAW включает их сама
        auto props = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (int i = 0; i < kNumPads; ++i)
            props = props.withOutput ("Out " + juce::String (i + 1), juce::AudioChannelSet::stereo(), false);
        return props;
    }

    SamplitProcessor::SamplitProcessor() : AudioProcessor (makeBuses()) {}

    void SamplitProcessor::prepareToPlay (double sampleRate, int)
    {
        engine.prepare (sampleRate);
    }

    bool SamplitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;

        for (const auto& out : layouts.outputBuses)
            if (! out.isDisabled() && out != juce::AudioChannelSet::stereo())
                return false;

        return layouts.inputBuses.isEmpty();
    }

    void SamplitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();

        // Куда идёт каждый пэд: на свою шину, если DAW её включила, иначе в Main
        std::array<float*, kNumPads> outL {};
        std::array<float*, kNumPads> outR {};
        uint32_t separate = 0;
        for (int pad = 0; pad < kNumPads; ++pad)
        {
            const int bus = pad + 1;
            const bool own = bus < getBusCount (false) && getBus (false, bus)->isEnabled();
            auto target = getBusBuffer (buffer, false, own ? bus : 0);
            outL[(size_t) pad] = target.getWritePointer (0);
            outR[(size_t) pad] = target.getWritePointer (1);
            if (own)
                separate |= 1u << pad;
        }
        separateOutMask.store (separate);

        const int numSamples = buffer.getNumSamples();

        auto renderRange = [&] (int from, int to)
        {
            if (to > from)
                for (int pad = 0; pad < kNumPads; ++pad)
                    engine.render (pad, outL[(size_t) pad], outR[(size_t) pad], from, to);
        };

        // Нажатия из UI
        const auto fromUi = pendingTriggers.exchange (0);
        for (int pad = 0; pad < kNumPads; ++pad)
            if (fromUi & (1u << pad))
                engine.noteOn (pad, 1.0f);

        // MIDI: рендерим кусками между событиями, чтобы нота стартовала в нужном сэмпле
        int pos = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isNoteOn())
                continue;

            const int pad = msg.getNoteNumber() - kFirstPadNote;
            if (pad < 0 || pad >= kNumPads)
                continue;

            const int at = juce::jlimit (pos, numSamples, meta.samplePosition);
            renderRange (pos, at);
            pos = at;
            engine.noteOn (pad, msg.getFloatVelocity());
        }
        renderRange (pos, numSamples);

        uint32_t active = 0;
        for (int pad = 0; pad < kNumPads; ++pad)
            if (engine.isActive (pad))
                active |= 1u << pad;
        activeMask.store (active);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new samplit::SamplitProcessor();
}
