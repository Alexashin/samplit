#include "PluginProcessor.h"

#include <algorithm>

namespace samplit
{
    namespace
    {
        constexpr int kMaxZones = 4096;

        bool isAudioFile (const juce::File& f)
        {
            return f.existsAsFile() && f.hasFileExtension ("wav;aif;aiff;flac;ogg");
        }
    }

    juce::AudioProcessor::BusesProperties SamplitProcessor::makeBuses()
    {
        // Шина 0 это Main. Шины 1..16 это отдельные выходы групп. DAW включает их сама
        auto props = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (int i = 0; i < kNumOutputs; ++i)
            props = props.withOutput ("Out " + juce::String (i + 1), juce::AudioChannelSet::stereo(), false);
        return props;
    }

    SamplitProcessor::SamplitProcessor() : AudioProcessor (makeBuses())
    {
        formats.registerBasicFormats();
        model.groups.push_back ({ "Group 1", 1 });
        publish();
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

    void SamplitProcessor::triggerNote (int note, int velocity) noexcept
    {
        if (note < 0 || note > 127)
            return;
        const auto w = queueWrite.load();
        queue[w % kQueue].store ((uint32_t) note | ((uint32_t) juce::jlimit (1, 127, velocity) << 8));
        queueWrite.store (w + 1);
    }

    void SamplitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();

        auto main = getBusBuffer (buffer, false, 0);
        if (main.getNumChannels() < 2)
            return;

        // Куда идёт каждый выход группы: на свою шину, если DAW её включила, иначе в Main
        std::array<float*, kNumOutputs + 1> outL {};
        std::array<float*, kNumOutputs + 1> outR {};
        outL[0] = main.getWritePointer (0);
        outR[0] = main.getWritePointer (1);
        uint32_t enabled = 0;
        for (int out = 1; out <= kNumOutputs; ++out)
        {
            outL[(size_t) out] = outL[0];
            outR[(size_t) out] = outR[0];
            if (out >= getBusCount (false) || ! getBus (false, out)->isEnabled())
                continue;

            auto target = getBusBuffer (buffer, false, out);
            if (target.getNumChannels() < 2)
                continue;
            outL[(size_t) out] = target.getWritePointer (0);
            outR[(size_t) out] = target.getWritePointer (1);
            enabled |= 1u << (out - 1);
        }
        enabledOutputs.store (enabled);

        const int numSamples = buffer.getNumSamples();

        // Команды из UI: прослушивание и ноты с экранной клавиатуры
        if (pendingPreviewStop.exchange (false))
            engine.stopPreview();
        if (pendingPreview.exchange (false))
            engine.previewOn();

        auto r = queueRead.load();
        const auto w = queueWrite.load();
        for (; r != w; ++r)
        {
            const auto e = queue[r % kQueue].load();
            engine.noteOn ((int) (e & 0xff), (int) ((e >> 8) & 0xff));
        }
        queueRead.store (r);

        // MIDI: рендерим кусками между событиями, чтобы нота стартовала в нужном сэмпле
        int pos = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isNoteOn())
                continue;

            const int at = juce::jlimit (pos, numSamples, meta.samplePosition);
            if (at > pos)
                engine.render (outL, outR, pos, at);
            pos = at;
            engine.noteOn (msg.getNoteNumber(), msg.getVelocity());
        }
        if (numSamples > pos)
            engine.render (outL, outR, pos, numSamples);

        engine.publishActivity();
        const auto keys = engine.getActiveKeys();
        activeLo.store (keys[0]);
        activeHi.store (keys[1]);
    }

    // ---------------------------------------------------------------- загрузка

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

    void SamplitProcessor::requestLoad (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        if (cache.count (key) != 0 || loading.count (key) != 0 || failed.count (key) != 0)
            return;

        loading.insert (key);
        ++loadsInFlight;
        std::weak_ptr<int> alive = lifeToken;

        loader.addJob ([this, alive, file, key]
        {
            auto sample = readSample (formats, file);

            juce::MessageManager::callAsync ([this, alive, key, sample]
            {
                if (alive.expired())
                    return;

                loading.erase (key);
                --loadsInFlight;
                if (sample != nullptr)
                    cache[key] = sample;
                else
                    failed.insert (key);
                publish();
            });
        });
    }

    void SamplitProcessor::publish()
    {
        auto snapshot = std::make_shared<Instrument> (model);
        for (auto& zone : snapshot->zones)
            if (const auto it = cache.find (zone.file.getFullPathName()); it != cache.end())
                zone.sample = it->second;

        // Кэш держит только то, что нужно модели. Звучащие сэмплы живут в снимках, пока не доиграют
        std::set<juce::String> used;
        for (const auto& zone : model.zones)
            used.insert (zone.file.getFullPathName());
        std::erase_if (cache, [&] (const auto& item) { return used.count (item.first) == 0; });
        std::erase_if (failed, [&] (const auto& path) { return used.count (path) == 0; });

        std::shared_ptr<const Instrument> old;
        {
            const juce::ScopedLock sl (stateLock);
            old = std::move (published);
            published = snapshot;
        }
        engine.setInstrument (snapshot.get());

        if (old != nullptr)
            retired.push_back ({ std::move (old), nullptr, juce::Time::getMillisecondCounter() });

        collectGarbage();
        sendChangeMessage();
    }

    void SamplitProcessor::collectGarbage (bool force)
    {
        // Объект удаляем, только когда он не нужен audio thread. Секунда запаса покрывает блок,
        // который audio thread мог начать до замены
        const auto now = juce::Time::getMillisecondCounter();
        std::erase_if (retired, [&] (const Retired& r)
        {
            if (! force && now - r.since <= 1000)
                return false;
            if (r.snapshot != nullptr && engine.isUsing (r.snapshot.get()))
                return false;
            if (r.preview != nullptr && engine.isUsing (r.preview.get()))
                return false;
            return true;
        });
    }

    std::shared_ptr<const Instrument> SamplitProcessor::getInstrument() const
    {
        const juce::ScopedLock sl (stateLock);
        return published;
    }

    // ---------------------------------------------------------------- редактирование

    int SamplitProcessor::addGroup (const juce::String& name)
    {
        if ((int) model.groups.size() >= kMaxGroups)
            return -1;

        Group group;
        group.name = name;
        group.output = juce::jmin (kNumOutputs, (int) model.groups.size() + 1);
        model.groups.push_back (group);
        publish();
        return (int) model.groups.size() - 1;
    }

    void SamplitProcessor::removeGroup (int group)
    {
        if (group < 0 || group >= (int) model.groups.size())
            return;

        std::erase_if (model.zones, [&] (const Zone& z) { return z.group == group; });
        for (auto& z : model.zones)
            if (z.group > group)
                --z.group;
        model.groups.erase (model.groups.begin() + group);
        publish();
    }

    void SamplitProcessor::setGroup (int group, const Group& value)
    {
        if (group < 0 || group >= (int) model.groups.size())
            return;

        auto g = value;
        g.output = juce::jlimit (0, kNumOutputs, g.output);
        g.gainDb = juce::jlimit (-60.0f, 12.0f, g.gainDb);
        g.pan = juce::jlimit (-1.0f, 1.0f, g.pan);
        model.groups[(size_t) group] = g;
        publish();
    }

    juce::Array<juce::File> SamplitProcessor::sortedAudioFiles (const juce::Array<juce::File>& files)
    {
        juce::Array<juce::File> result;
        for (const auto& f : files)
            if (isAudioFile (f))
                result.add (f);

        std::sort (result.begin(), result.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        });
        return result;
    }

    int SamplitProcessor::addFilesToKey (const juce::Array<juce::File>& files, int group, int key, Spread spread)
    {
        if (group < 0 || group >= (int) model.groups.size() || key < 0 || key > 127)
            return 0;

        const auto sorted = sortedAudioFiles (files);
        const int n = std::min (sorted.size(), kMaxZones - (int) model.zones.size());
        for (int i = 0; i < n; ++i)
        {
            Zone zone;
            zone.file = sorted[i];
            zone.group = group;
            zone.lowKey = zone.highKey = zone.rootKey = key;
            if (spread == Spread::VelocityLayers)
            {
                zone.lowVel = 1 + (i * 127) / n;
                zone.highVel = ((i + 1) * 127) / n;
            }
            model.zones.push_back (zone);
            requestLoad (zone.file);
        }

        if (n > 0)
            publish();
        return n;
    }

    int SamplitProcessor::addFolderAsKit (const juce::File& folder, int group, int firstKey)
    {
        if (group < 0 || group >= (int) model.groups.size() || ! folder.isDirectory())
            return 0;

        const auto sorted = sortedAudioFiles (folder.findChildFiles (juce::File::findFiles, false, getSupportedWildcard()));
        int added = 0;
        for (int i = 0; i < sorted.size() && firstKey + i <= 127 && (int) model.zones.size() < kMaxZones; ++i)
        {
            Zone zone;
            zone.file = sorted[i];
            zone.group = group;
            zone.lowKey = zone.highKey = zone.rootKey = firstKey + i;
            model.zones.push_back (zone);
            requestLoad (zone.file);
            ++added;
        }

        if (added > 0)
            publish();
        return added;
    }

    void SamplitProcessor::removeZone (int zone)
    {
        if (zone < 0 || zone >= (int) model.zones.size())
            return;
        model.zones.erase (model.zones.begin() + zone);
        publish();
    }

    void SamplitProcessor::setZone (int index, const Zone& value)
    {
        if (index < 0 || index >= (int) model.zones.size())
            return;

        auto z = value;
        z.sample = nullptr;
        z.group = juce::jlimit (0, juce::jmax (0, (int) model.groups.size() - 1), z.group);
        z.lowKey = juce::jlimit (0, 127, z.lowKey);
        z.highKey = juce::jlimit (z.lowKey, 127, z.highKey);
        z.lowVel = juce::jlimit (1, 127, z.lowVel);
        z.highVel = juce::jlimit (z.lowVel, 127, z.highVel);
        z.rootKey = juce::jlimit (0, 127, z.rootKey);
        z.gainDb = juce::jlimit (-60.0f, 12.0f, z.gainDb);
        model.zones[(size_t) index] = z;
        requestLoad (z.file);
        publish();
    }

    void SamplitProcessor::clearKey (int group, int key)
    {
        const auto before = model.zones.size();
        std::erase_if (model.zones, [&] (const Zone& z) { return z.group == group && z.lowKey == key && z.highKey == key; });
        if (model.zones.size() != before)
            publish();
    }

    // ---------------------------------------------------------------- прослушивание

    void SamplitProcessor::startPreviewWith (SamplePtr sample)
    {
        if (sample == nullptr)
            return;

        if (previewSample != nullptr)
            retired.push_back ({ nullptr, previewSample, juce::Time::getMillisecondCounter() });
        previewSample = std::move (sample);
        engine.setPreviewSample (previewSample.get());
        pendingPreview.store (true);
        collectGarbage();
    }

    void SamplitProcessor::previewFile (const juce::File& file)
    {
        stopPreview();
        const auto generation = ++previewGeneration;

        if (previewSample != nullptr && previewSample->file == file)
        {
            pendingPreview.store (true);
            return;
        }

        std::weak_ptr<int> alive = lifeToken;
        loader.addJob ([this, alive, file, generation]
        {
            auto sample = readSample (formats, file);

            juce::MessageManager::callAsync ([this, alive, generation, sample]
            {
                // Процессор удалён или пользователь уже выбрал другой файл
                if (alive.expired() || previewGeneration.load() != generation)
                    return;
                startPreviewWith (sample);
            });
        });
    }

    // ---------------------------------------------------------------- прочее и состояние

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

    void SamplitProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        juce::ValueTree state ("SAMPLIT");
        state.setProperty ("version", 2, nullptr);

        std::shared_ptr<const Instrument> inst;
        {
            const juce::ScopedLock sl (stateLock);
            state.setProperty ("folder", lastFolder.getFullPathName(), nullptr);
            inst = published;
        }

        if (inst != nullptr)
        {
            for (const auto& g : inst->groups)
            {
                juce::ValueTree v ("GROUP");
                v.setProperty ("name", g.name, nullptr);
                v.setProperty ("output", g.output, nullptr);
                v.setProperty ("mode", (int) g.mode, nullptr);
                v.setProperty ("gain", g.gainDb, nullptr);
                v.setProperty ("pan", g.pan, nullptr);
                v.setProperty ("tracking", g.tracking, nullptr);
                state.appendChild (v, nullptr);
            }
            for (const auto& z : inst->zones)
            {
                juce::ValueTree v ("ZONE");
                v.setProperty ("file", z.file.getFullPathName(), nullptr);
                v.setProperty ("group", z.group, nullptr);
                v.setProperty ("lowKey", z.lowKey, nullptr);
                v.setProperty ("highKey", z.highKey, nullptr);
                v.setProperty ("lowVel", z.lowVel, nullptr);
                v.setProperty ("highVel", z.highVel, nullptr);
                v.setProperty ("root", z.rootKey, nullptr);
                v.setProperty ("gain", z.gainDb, nullptr);
                state.appendChild (v, nullptr);
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
        if ((int) state.getProperty ("version", 0) < 2)
            return;

        Instrument parsed;
        for (const auto& v : state)
        {
            if (v.hasType ("GROUP") && (int) parsed.groups.size() < kMaxGroups)
            {
                Group g;
                g.name = v.getProperty ("name").toString();
                g.output = juce::jlimit (0, kNumOutputs, (int) v.getProperty ("output", 0));
                g.mode = (PlayMode) juce::jlimit (0, 2, (int) v.getProperty ("mode", 2));
                g.gainDb = juce::jlimit (-60.0f, 12.0f, (float) v.getProperty ("gain", 0.0));
                g.pan = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("pan", 0.0));
                g.tracking = (bool) v.getProperty ("tracking", false);
                parsed.groups.push_back (g);
            }
            else if (v.hasType ("ZONE") && (int) parsed.zones.size() < kMaxZones)
            {
                const juce::String path = v.getProperty ("file").toString();
                if (! juce::File::isAbsolutePath (path))
                    continue;

                Zone z;
                z.file = juce::File (path);
                z.group = (int) v.getProperty ("group", 0);
                z.lowKey = juce::jlimit (0, 127, (int) v.getProperty ("lowKey", 60));
                z.highKey = juce::jlimit (z.lowKey, 127, (int) v.getProperty ("highKey", z.lowKey));
                z.lowVel = juce::jlimit (1, 127, (int) v.getProperty ("lowVel", 1));
                z.highVel = juce::jlimit (z.lowVel, 127, (int) v.getProperty ("highVel", 127));
                z.rootKey = juce::jlimit (0, 127, (int) v.getProperty ("root", z.lowKey));
                z.gainDb = juce::jlimit (-60.0f, 12.0f, (float) v.getProperty ("gain", 0.0));
                parsed.zones.push_back (z);
            }
        }

        std::erase_if (parsed.zones, [&] (const Zone& z) { return z.group < 0 || z.group >= (int) parsed.groups.size(); });
        if (parsed.groups.empty())
            parsed.groups.push_back ({ "Group 1", 1 });

        const juce::String folder = state.getProperty ("folder").toString();
        setLastFolder (juce::File::isAbsolutePath (folder) ? juce::File (folder) : juce::File());

        // Хост может звать setState не из message thread, а модель меняется только там
        std::weak_ptr<int> alive = lifeToken;
        auto apply = [this, alive, parsed = std::move (parsed)]
        {
            if (alive.expired())
                return;
            model = parsed;
            for (const auto& z : model.zones)
                requestLoad (z.file);
            publish();
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
