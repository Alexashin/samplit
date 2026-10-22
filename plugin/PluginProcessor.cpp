#include "PluginProcessor.h"

#include <algorithm>

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

    SamplitProcessor::SamplitProcessor() : AudioProcessor (makeBuses())
    {
        formats.registerBasicFormats();
        startTimer (1000);
    }

    SamplitProcessor::~SamplitProcessor()
    {
        stopTimer();
        lifeToken.reset();
        loader.removeAllJobs (true, 4000);
    }

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

        // Куда идёт каждый голос: пэд на свою шину, если DAW её включила, иначе в Main. Прослушивание всегда в Main
        std::array<float*, kNumVoices> outL {};
        std::array<float*, kNumVoices> outR {};
        uint32_t separate = 0;
        for (int voice = 0; voice < kNumVoices; ++voice)
        {
            const int bus = voice + 1;
            const bool own = voice < kNumPads && bus < getBusCount (false) && getBus (false, bus)->isEnabled();
            auto target = getBusBuffer (buffer, false, own ? bus : 0);
            outL[(size_t) voice] = target.getWritePointer (0);
            outR[(size_t) voice] = target.getWritePointer (1);
            if (own)
                separate |= 1u << voice;
        }
        separateOutMask.store (separate);

        const int numSamples = buffer.getNumSamples();

        auto renderRange = [&] (int from, int to)
        {
            if (to > from)
                for (int voice = 0; voice < kNumVoices; ++voice)
                    engine.render (voice, outL[(size_t) voice], outR[(size_t) voice], from, to);
        };

        // Команды из UI: остановить и сыграть
        const auto stops = pendingStops.exchange (0);
        const auto triggers = pendingTriggers.exchange (0);
        for (int voice = 0; voice < kNumVoices; ++voice)
        {
            if (stops & (1u << voice))
                engine.stop (voice);
            if (triggers & (1u << voice))
                engine.noteOn (voice, 1.0f);
        }

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
        for (int voice = 0; voice < kNumVoices; ++voice)
            if (engine.isActive (voice))
                active |= 1u << voice;
        activeMask.store (active);
    }

    // ---------------------------------------------------------------- сэмплы

    SamplePtr SamplitProcessor::readSample (juce::AudioFormatManager& fm, const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return {};

        const auto length = reader->lengthInSamples;
        if (length < 2 || length > (juce::int64) (reader->sampleRate * 60.0))
            return {};

        auto sample = std::make_shared<Sample>();
        const int channels = reader->numChannels > 1 ? 2 : 1;
        sample->audio.setSize (channels, (int) length);
        reader->read (&sample->audio, 0, (int) length, 0, true, channels > 1);
        sample->sampleRate = reader->sampleRate;
        sample->file = file;
        return sample;
    }

    void SamplitProcessor::startLoad (int voice, const juce::File& file, bool playWhenReady)
    {
        const auto generation = ++loadGeneration[(size_t) voice];
        std::weak_ptr<int> alive = lifeToken;

        loader.addJob ([this, alive, voice, generation, file, playWhenReady]
        {
            auto sample = readSample (formats, file);

            juce::MessageManager::callAsync ([this, alive, voice, generation, sample, playWhenReady]
            {
                // Процессор удалён или пэд уже попросили загрузить другой файл
                if (alive.expired() || loadGeneration[(size_t) voice].load() != generation)
                    return;

                assignSample (voice, sample);
                if (playWhenReady && sample != nullptr)
                    pendingTriggers.fetch_or (1u << voice);
            });
        });
    }

    void SamplitProcessor::loadPadSample (int pad, const juce::File& file)
    {
        {
            const juce::ScopedLock sl (stateLock);
            padFiles[(size_t) pad] = file;
        }
        startLoad (pad, file, false);
        sendChangeMessage();
    }

    void SamplitProcessor::clearPad (int pad)
    {
        {
            const juce::ScopedLock sl (stateLock);
            padFiles[(size_t) pad] = juce::File();
        }
        ++loadGeneration[(size_t) pad];
        assignSample (pad, nullptr);
    }

    void SamplitProcessor::assignSample (int voice, SamplePtr sample)
    {
        auto& slot = owned[(size_t) voice];
        if (slot != nullptr)
            retired.push_back ({ slot, juce::Time::getMillisecondCounter() });

        slot = std::move (sample);
        engine.setSample (voice, slot.get());
        collectGarbage();
        sendChangeMessage();
    }

    void SamplitProcessor::collectGarbage (bool force)
    {
        // Сэмпл удаляем, только когда он снят со всех голосов и не звучит. Секунда запаса покрывает блок,
        // который audio thread мог начать до замены
        const auto now = juce::Time::getMillisecondCounter();
        std::erase_if (retired, [&] (const Retired& r)
        {
            return (force || now - r.since > 1000) && ! engine.isUsing (r.sample.get());
        });
    }

    void SamplitProcessor::previewFile (const juce::File& file)
    {
        stopPreview();
        if (const auto& current = owned[(size_t) kPreviewVoice]; current != nullptr && current->file == file)
        {
            ++loadGeneration[(size_t) kPreviewVoice];
            pendingTriggers.fetch_or (1u << kPreviewVoice);
            return;
        }
        startLoad (kPreviewVoice, file, true);
    }

    juce::File SamplitProcessor::getPadFile (int pad) const
    {
        const juce::ScopedLock sl (stateLock);
        return padFiles[(size_t) pad];
    }

    juce::File SamplitProcessor::getLastFolder() const
    {
        const juce::ScopedLock sl (stateLock);
        return lastFolder;
    }

    void SamplitProcessor::setLastFolder (const juce::File& folder)
    {
        const juce::ScopedLock sl (stateLock);
        lastFolder = folder;
    }

    // ---------------------------------------------------------------- состояние

    void SamplitProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        juce::ValueTree state ("SAMPLIT");
        state.setProperty ("version", 1, nullptr);
        {
            const juce::ScopedLock sl (stateLock);
            state.setProperty ("folder", lastFolder.getFullPathName(), nullptr);
            for (int pad = 0; pad < kNumPads; ++pad)
            {
                if (padFiles[(size_t) pad] == juce::File())
                    continue;
                juce::ValueTree p ("PAD");
                p.setProperty ("index", pad, nullptr);
                p.setProperty ("file", padFiles[(size_t) pad].getFullPathName(), nullptr);
                state.appendChild (p, nullptr);
            }
        }

        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void SamplitProcessor::setStateInformation (const void* data, int size)
    {
        const auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr || ! xml->hasTagName ("SAMPLIT"))
            return;

        const auto state = juce::ValueTree::fromXml (*xml);
        std::array<juce::File, kNumPads> files;
        for (const auto& p : state)
        {
            const int pad = p.getProperty ("index", -1);
            const juce::String path = p.getProperty ("file").toString();
            if (pad >= 0 && pad < kNumPads && juce::File::isAbsolutePath (path))
                files[(size_t) pad] = juce::File (path);
        }

        const juce::String folder = state.getProperty ("folder").toString();
        setLastFolder (juce::File::isAbsolutePath (folder) ? juce::File (folder) : juce::File());

        // Хост может звать setState не из message thread, а пэды меняются только там
        std::weak_ptr<int> alive = lifeToken;
        auto apply = [this, alive, files]
        {
            if (alive.expired())
                return;
            for (int pad = 0; pad < kNumPads; ++pad)
            {
                if (files[(size_t) pad] == juce::File())
                    clearPad (pad);
                else
                    loadPadSample (pad, files[(size_t) pad]);
            }
        };

        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        if (mm == nullptr || mm->isThisTheMessageThread())
            apply();
        else
            juce::MessageManager::callAsync (apply);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new samplit::SamplitProcessor();
}
