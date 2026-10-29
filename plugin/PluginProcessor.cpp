#include "PluginProcessor.h"
#include "Brand.h"

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
        // Шина 0 это Main (стерео). Шины 1..16 это отдельные моно-выходы групп и микрофонов. DAW включает их сама
        auto props = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (int i = 0; i < kNumOutputs; ++i)
            props = props.withOutput ("Out " + juce::String (i + 1), juce::AudioChannelSet::mono(), false);
        return props;
    }

    SamplitProcessor::SamplitProcessor() : AudioProcessor (makeBuses())
    {
        formats.registerBasicFormats();
        kitEngine.setStreamer (&streamer);
        license = std::make_unique<LicenseManager> (LicenseManager::defaultConfig());
        model.groups.push_back ({ "Group 1", 1 });
        publish();

        // Параметры автоматизации: имена общие, потому что микрофоны зависят от кита (Mic 1 = первый микрофон кита)
        for (int i = 0; i < kAutomatedMics; ++i)
        {
            const auto n = juce::String (i + 1).paddedLeft ('0', 2);
            levelParams[(size_t) i] = new juce::AudioParameterFloat (juce::ParameterID { "mic" + n + "_level", 1 }, "Mic " + n + " level",
                                                                     juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f);
            panParams[(size_t) i] = new juce::AudioParameterFloat (juce::ParameterID { "mic" + n + "_pan", 1 }, "Mic " + n + " pan",
                                                                   juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f);
            addParameter (levelParams[(size_t) i]);
            addParameter (panParams[(size_t) i]);
            levelParams[(size_t) i]->addListener (this);
            panParams[(size_t) i]->addListener (this);
        }
        startTimer (1000);
    }

    SamplitProcessor::~SamplitProcessor()
    {
        for (auto* p : levelParams)
            p->removeListener (this);
        for (auto* p : panParams)
            p->removeListener (this);
        cancelPendingUpdate();
        stopTimer();
        lifeToken.reset();
        cancelKitLoad();
        kitLoaderPool.removeAllJobs (true, 10000);
        streamer.shutdown();        // до удаления китов: поток чтения обращается к клипам звучащих голосов
        loader.removeAllJobs (true, 4000);
    }

    void SamplitProcessor::prepareToPlay (double sampleRate, int)
    {
        engine.prepare (sampleRate);
        kitEngine.prepare (sampleRate);
    }

    bool SamplitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;

        // Выходы микрофонов и групп только моно (или выключены)
        for (int i = 1; i < layouts.outputBuses.size(); ++i)
            if (! layouts.outputBuses.getReference (i).isDisabled()
                && layouts.outputBuses.getReference (i) != juce::AudioChannelSet::mono())
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

        // Куда идёт каждый выход группы или микрофона: на свою моно-шину, если DAW её включила (правый указатель
        // nullptr), иначе в стерео Main
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
            if (target.getNumChannels() < 1)
                continue;
            outL[(size_t) out] = target.getWritePointer (0);
            outR[(size_t) out] = target.getNumChannels() > 1 ? target.getWritePointer (1) : nullptr;
            enabled |= 1u << (out - 1);
        }
        enabledOutputs.store (enabled);

        const bool useKit = kitActive.load();
        auto noteOn = [&] (int note, int velocity)
        {
            if (useKit)
                kitEngine.noteOn (note, velocity);
            else
                engine.noteOn (note, velocity);
        };
        auto renderRange = [&] (int from, int to)
        {
            engine.render (outL, outR, from, to);
            kitEngine.render (outL, outR, from, to);
        };

        const int numSamples = buffer.getNumSamples();
        kitEngine.setOffline (isNonRealtime());

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
            noteOn ((int) (e & 0xff), (int) ((e >> 8) & 0xff));
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
                renderRange (pos, at);
            pos = at;
            noteOn (msg.getNoteNumber(), msg.getVelocity());
        }
        if (numSamples > pos)
            renderRange (pos, numSamples);

        engine.publishActivity();
        kitEngine.publishActivity();
        const auto samplerKeys = engine.getActiveKeys();
        const auto kitKeys = kitEngine.getActiveKeys();
        activeLo.store (samplerKeys[0] | kitKeys[0]);
        activeHi.store (samplerKeys[1] | kitKeys[1]);
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
            retired.push_back ({ std::move (old), nullptr, nullptr, juce::Time::getMillisecondCounter() });

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
            if (r.kit != nullptr && kitEngine.isUsing (r.kit.get()))
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

    // ---------------------------------------------------------------- кит

    void SamplitProcessor::setKit (std::shared_ptr<const Kit> newKit)
    {
        std::shared_ptr<const Kit> old;
        {
            const juce::ScopedLock sl (stateLock);
            old = std::move (kit);
            kit = newKit;
        }

        kitEngine.setKit (newKit.get());
        picks.clear();
        if (newKit != nullptr)
            for (const auto& c : newKit->choices)
                picks.push_back (c.pick);
        kitActive.store (newKit != nullptr && ! serviceMode.load());

        if (old != nullptr)
            retired.push_back ({ nullptr, nullptr, std::move (old), juce::Time::getMillisecondCounter() });
        collectGarbage();
        sendChangeMessage();
    }

    void SamplitProcessor::setLicenseManager (std::unique_ptr<LicenseManager> manager)
    {
        license = std::move (manager);
    }

    void SamplitProcessor::loadPreset (const Preset& preset, std::function<void (const KitLoadResult&)> done)
    {
        KitLoadOptions options;
        options.packDir = license->getPackDir();
        if (const auto root = license->getLibraryRoot(); root.isDirectory())
        {
            options.libraryRoot = root;
            options.manifestDir = license->getManifestDir();
        }
        options.keyFor = [this] (const juce::String& id, pack::PackKey& key) { return license->keyFor (id, key); };
        loadKitAsync (preset, std::move (options), std::move (done));
    }

    void SamplitProcessor::cancelKitLoad()
    {
        if (kitCancel != nullptr)
            kitCancel->store (true);
        ++kitGeneration;
        kitLoading.store (false);
    }

    void SamplitProcessor::loadKitAsync (const Preset& preset, KitLoadOptions options,
                                         std::function<void (const KitLoadResult&)> done)
    {
        cancelKitLoad();
        auto cancel = std::make_shared<std::atomic<bool>> (false);
        kitCancel = cancel;
        const auto generation = kitGeneration;
        const bool keepMix = std::exchange (restoreMixPending, false);
        kitLoading.store (true);
        kitProgress.store (0.0f);

        options.cancel = cancel.get();
        options.progress = [this] (float f) { kitProgress.store (f); };
        std::weak_ptr<int> alive = lifeToken;

        kitLoaderPool.addJob ([this, alive, preset, options = std::move (options), cancel, generation, keepMix, done = std::move (done)]
        {
            auto result = std::make_shared<KitLoadResult> (loadKit (preset, options));

            juce::MessageManager::callAsync ([this, alive, result, cancel, generation, keepMix, done]
            {
                if (alive.expired() || generation != kitGeneration)
                    return;     // устарело: пришла новая загрузка или процессор уничтожен

                kitLoading.store (false);
                kitProgress.store (1.0f);
                if (result->ok())
                {
                    // Микшер: значения пресета, а при восстановлении проекта сохранённые в нём (по id микрофона)
                    std::vector<MicState> fresh;
                    {
                        const juce::ScopedLock sl (stateLock);
                        for (size_t i = 0; i < result->kit->mics.size(); ++i)
                        {
                            const auto& mic = result->kit->mics[i];
                            const auto saved = savedMix.find (juce::String (mic.id));
                            if (keepMix && saved != savedMix.end())
                                fresh.push_back (saved->second);
                            else
                            {
                                const auto& m = result->mix[i];
                                fresh.push_back ({ m.levelDb, m.pan, m.mute, false, mic.out });
                            }
                        }
                        savedMix.clear();
                        for (size_t i = 0; i < fresh.size(); ++i)
                            savedMix[juce::String (result->kit->mics[i].id)] = fresh[i];
                    }
                    mixer = std::move (fresh);
                    applyMixer();
                    pushAllParams();
                    setKit (result->kit);

                    // Выбор удара: как в пресете, а при восстановлении проекта сохранённый (по id переключателя)
                    {
                        const juce::ScopedLock sl (stateLock);
                        const auto& choices = result->kit->choices;
                        for (size_t i = 0; i < choices.size(); ++i)
                            if (const auto saved = savedPicks.find (juce::String (choices[i].id)); keepMix && saved != savedPicks.end())
                                picks[i] = saved->second;
                        savedPicks.clear();
                        for (size_t i = 0; i < choices.size(); ++i)
                            savedPicks[juce::String (choices[i].id)] = picks[i];
                    }
                    applyPicks();

                    // Громкость барабанов: как в проекте при восстановлении, иначе 0 дБ
                    {
                        const juce::ScopedLock sl (stateLock);
                        pieceLevels.assign (result->kit->pieces.size(), 0.0f);
                        for (size_t i = 0; i < pieceLevels.size(); ++i)
                            if (const auto saved = savedPieces.find (juce::String (result->kit->pieces[i].id)); keepMix && saved != savedPieces.end())
                                pieceLevels[i] = saved->second;
                        savedPieces.clear();
                        for (size_t i = 0; i < pieceLevels.size(); ++i)
                            savedPieces[juce::String (result->kit->pieces[i].id)] = pieceLevels[i];
                    }
                    applyPieceLevels();
                }

                lastLoad = *result;
                lastLoad.kit = nullptr;
                sendChangeMessage();
                if (done)
                    done (*result);
            });
        });
    }

    std::vector<PresetEntry> SamplitProcessor::scanPresets() const
    {
        const auto root = license->getLibraryRoot();
        return samplit::scanPresets (license->getPresetDir(), license->getPackDir(),
                                     [this] (const juce::String& id, pack::PackKey& key) { return license->keyFor (id, key); },
                                     root.isDirectory() ? root : juce::File(), license->getManifestDir());
    }

    void SamplitProcessor::selectPreset (const Preset& preset, std::function<void (const KitLoadResult&)> done)
    {
        restoreMixPending = false;
        setPresetName (preset.id);

        // Микс переживает смену кита: правки пользователя переносятся по id микрофонов, выбранный микс читается с диска заново,
        // а если микс не выбирали, берётся микс пресета (поле mix). Пресет с явным блоком mics и без mix (старый формат или
        // сохранённый «Сохранить как…» после правок) играет с этими значениями
        auto withMix = preset;
        const bool explicitMics = preset.mixId.isEmpty() && ! preset.mics.empty();
        bool modified;
        juce::String chosen;
        {
            const juce::ScopedLock sl (stateLock);
            modified = mixModified;
            chosen = mixName.isNotEmpty() ? mixName : preset.mixId;
            if (modified && ! explicitMics)
                for (const auto& [id, m] : savedMix)
                    withMix.mics[id] = { m.levelDb, m.pan, m.out, m.mute };
        }
        if (explicitMics)
        {
            const juce::ScopedLock sl (stateLock);
            mixName = {};
            mixModified = false;
        }
        else if (! modified)
        {
            bool found = false;
            if (chosen.isNotEmpty())
                for (const auto& m : scanMixes())
                    if (m.id == chosen)
                    {
                        withMix.mics = m.mics;
                        found = true;
                    }
            const juce::ScopedLock sl (stateLock);
            mixName = found ? chosen : juce::String();
        }
        loadPreset (withMix, std::move (done));
    }

    juce::File SamplitProcessor::saveCurrentPresetAs (const juce::String& name, juce::String& error)
    {
        const auto title = name.trim();
        const auto current = getKit();
        if (title.isEmpty())
        {
            error = utf8 ("Введите название пресета");
            return {};
        }
        if (current == nullptr)
        {
            error = utf8 ("Сначала загрузите пресет: сохранять нечего");
            return {};
        }

        const auto currentId = getPresetName();
        Preset preset;
        bool found = false;
        for (const auto& e : scanPresets())
            if (e.preset.id == currentId)
            {
                preset = e.preset;
                found = true;
            }
        if (! found)
        {
            error = utf8 ("Исходный пресет не найден в папке пресетов");
            return {};
        }

        // Микс: выбранный и не правленный сохраняем ссылкой, иначе явными значениями микрофонов
        {
            const juce::ScopedLock sl (stateLock);
            preset.mixId = (mixName.isNotEmpty() && ! mixModified) ? mixName : juce::String();
        }
        // Микшер: значения нынешних линеек по id микрофонов
        for (int i = 0; i < (int) current->mics.size() && i < (int) mixer.size(); ++i)
        {
            const auto& m = mixer[(size_t) i];
            preset.mics[juce::String::fromUTF8 (current->mics[(size_t) i].id.c_str())] = { m.levelDb, m.pan, m.out, m.mute };
        }
        // Режимы выбора удара: правило и переключатель связаны названием
        for (auto& rule : preset.pick)
            for (int c = 0; c < (int) current->choices.size(); ++c)
                if (juce::String::fromUTF8 (current->choices[(size_t) c].name.c_str()) == rule.name)
                    rule.mode = getPickChoice (c);

        juce::String slug;
        for (auto ch : title.toLowerCase())
            slug << (juce::CharacterFunctions::isLetterOrDigit (ch) ? juce::String::charToString (ch) : juce::String ("-"));
        while (slug.contains ("--"))
            slug = slug.replace ("--", "-");
        slug = slug.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
        if (slug.isEmpty())
            slug = "preset";

        const auto dir = license->getPresetDir();
        dir.createDirectory();
        auto file = dir.getChildFile ("user-" + slug + ".json");
        for (int n = 2; file.exists(); ++n)
            file = dir.getChildFile ("user-" + slug + "-" + juce::String (n) + ".json");

        preset.id = file.getFileNameWithoutExtension();
        preset.name = title;
        if (! file.replaceWithText (preset.toJson(), false, false, "\r\n"))
        {
            error = utf8 ("Не удалось записать файл ") + file.getFullPathName();
            return {};
        }
        setPresetName (preset.id);
        return file;
    }

    std::vector<Mix> SamplitProcessor::scanMixes() const
    {
        return samplit::scanMixes (getMixDir());
    }


    namespace
    {
        // Только свои файлы (user-*.json) прямо в папке пресетов или миксов
        bool isOwnUserFile (const juce::File& f, const juce::File& presetDir, const juce::File& mixDir)
        {
            if (! f.existsAsFile() || ! f.hasFileExtension ("json") || ! f.getFileName().startsWith ("user-"))
                return false;
            const auto parent = f.getParentDirectory();
            return parent == presetDir || parent == mixDir;
        }
    }

    juce::File SamplitProcessor::getMixFile (const juce::String& id) const
    {
        const auto f = getMixDir().getChildFile (id + ".json");
        return f.existsAsFile() ? f : juce::File();
    }

    bool SamplitProcessor::renameUserFile (const juce::File& file, const juce::String& newName, juce::String& error)
    {
        const auto title = newName.trim();
        if (! isOwnUserFile (file, license->getPresetDir(), getMixDir()))
        {
            error = utf8 ("Переименовать можно только свои пресеты и миксы");
            return false;
        }
        if (title.isEmpty())
        {
            error = utf8 ("Введите название");
            return false;
        }

        const auto text = file.loadFileAsString();
        auto root = juce::JSON::parse (text);
        auto* obj = root.getDynamicObject();
        if (obj == nullptr)
        {
            error = utf8 ("Файл повреждён");
            return false;
        }
        obj->setProperty ("name", title);
        if (! file.replaceWithText (juce::JSON::toString (root), false, false, "\r\n"))
        {
            error = utf8 ("Не удалось записать файл ") + file.getFullPathName();
            return false;
        }
        sendChangeMessage();
        return true;
    }

    bool SamplitProcessor::deleteUserFile (const juce::File& file, juce::String& error)
    {
        if (! isOwnUserFile (file, license->getPresetDir(), getMixDir()))
        {
            error = utf8 ("Удалить можно только свои пресеты и миксы");
            return false;
        }
        const auto id = file.getFileNameWithoutExtension();
        if (! file.moveToTrash())
        {
            error = utf8 ("Не удалось убрать файл в корзину: ") + file.getFullPathName();
            return false;
        }
        {
            const juce::ScopedLock sl (stateLock);
            if (mixName == id)
            {
                mixName = {};
                mixModified = false;
            }
        }
        sendChangeMessage();
        return true;
    }

    juce::File SamplitProcessor::getMixDir() const
    {
        return license->getPresetDir().getChildFile ("mixes");
    }

    juce::String SamplitProcessor::getMixName() const
    {
        const juce::ScopedLock sl (stateLock);
        return mixName;
    }

    bool SamplitProcessor::isMixModified() const
    {
        const juce::ScopedLock sl (stateLock);
        return mixModified;
    }

    void SamplitProcessor::applyMix (const Mix& mix)
    {
        const auto current = getKit();
        if (current == nullptr)
            return;

        {
            const juce::ScopedValueSetter<bool> guard (applyingMix, true);
            for (int i = 0; i < (int) current->mics.size() && i < (int) mixer.size(); ++i)
            {
                const auto found = mix.mics.find (juce::String (current->mics[(size_t) i].id));
                if (found == mix.mics.end())
                    continue;
                auto s = getMicState (i);
                s.levelDb = found->second.levelDb;
                s.pan = found->second.pan;
                s.mute = found->second.mute;
                if (found->second.out >= 0)
                    s.out = found->second.out;
                setMicState (i, s);
            }
        }
        {
            const juce::ScopedLock sl (stateLock);
            mixName = mix.id;
            mixModified = false;
        }
        sendChangeMessage();
    }

    juce::File SamplitProcessor::saveCurrentMixAs (const juce::String& name, juce::String& error)
    {
        const auto title = name.trim();
        const auto current = getKit();
        if (title.isEmpty())
        {
            error = utf8 ("Введите название микса");
            return {};
        }
        if (current == nullptr)
        {
            error = utf8 ("Сначала загрузите пресет: сохранять нечего");
            return {};
        }

        Mix mix;
        mix.name = title;
        for (int i = 0; i < (int) current->mics.size() && i < (int) mixer.size(); ++i)
        {
            const auto& m = mixer[(size_t) i];
            mix.mics[juce::String::fromUTF8 (current->mics[(size_t) i].id.c_str())] = { m.levelDb, m.pan, m.out, m.mute };
        }

        juce::String slug;
        for (auto ch : title.toLowerCase())
            slug << (juce::CharacterFunctions::isLetterOrDigit (ch) ? juce::String::charToString (ch) : juce::String ("-"));
        while (slug.contains ("--"))
            slug = slug.replace ("--", "-");
        slug = slug.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
        if (slug.isEmpty())
            slug = "mix";

        const auto dir = getMixDir();
        dir.createDirectory();
        auto file = dir.getChildFile ("user-" + slug + ".json");
        for (int n = 2; file.exists(); ++n)
            file = dir.getChildFile ("user-" + slug + "-" + juce::String (n) + ".json");

        mix.id = file.getFileNameWithoutExtension();
        if (! file.replaceWithText (mix.toJson(), false, false, "\r\n"))
        {
            error = utf8 ("Не удалось записать файл ") + file.getFullPathName();
            return {};
        }
        {
            const juce::ScopedLock sl (stateLock);
            mixName = mix.id;
            mixModified = false;
        }
        sendChangeMessage();
        return file;
    }

    bool SamplitProcessor::restorePreset()
    {
        const auto id = getPresetName();
        if (id.isEmpty())
            return false;
        for (const auto& e : scanPresets())
            if (e.preset.id == id)
            {
                restoreMixPending = true;
                loadPreset (e.preset);
                return true;
            }
        return false;
    }

    SamplitProcessor::MicState SamplitProcessor::getMicState (int mic) const
    {
        return juce::isPositiveAndBelow (mic, (int) mixer.size()) ? mixer[(size_t) mic] : MicState {};
    }

    void SamplitProcessor::setMicState (int mic, const MicState& state)
    {
        const auto current = getKit();
        if (current == nullptr || ! juce::isPositiveAndBelow (mic, (int) mixer.size()) || mic >= (int) current->mics.size())
            return;

        auto s = state;
        s.levelDb = juce::jlimit (-60.0f, 12.0f, s.levelDb);
        s.pan = juce::jlimit (-1.0f, 1.0f, s.pan);
        s.out = juce::jlimit (0, kNumOutputs, s.out);
        mixer[(size_t) mic] = s;
        {
            const juce::ScopedLock sl (stateLock);
            savedMix[juce::String (current->mics[(size_t) mic].id)] = s;
            if (! applyingMix)
                mixModified = true;     // «изменён» в подписи показывается, только если микс выбран
        }
        applyMixer();
        pushParam (mic);
    }

    // ---------------------------------------------------------------- громкость барабанов и параметры

    float SamplitProcessor::getPieceLevel (int piece) const
    {
        return juce::isPositiveAndBelow (piece, (int) pieceLevels.size()) ? pieceLevels[(size_t) piece] : 0.0f;
    }

    void SamplitProcessor::setPieceLevel (int piece, float levelDb)
    {
        const auto current = getKit();
        if (current == nullptr || ! juce::isPositiveAndBelow (piece, (int) pieceLevels.size()) || piece >= (int) current->pieces.size())
            return;
        pieceLevels[(size_t) piece] = juce::jlimit (-24.0f, 12.0f, levelDb);
        {
            const juce::ScopedLock sl (stateLock);
            savedPieces[juce::String (current->pieces[(size_t) piece].id)] = pieceLevels[(size_t) piece];
        }
        applyPieceLevels();
    }

    void SamplitProcessor::applyPieceLevels()
    {
        for (int i = 0; i < KitEngine::kMaxPieces; ++i)
            kitEngine.setPieceLevel (i, i < (int) pieceLevels.size() ? pieceLevels[(size_t) i] : 0.0f);
    }

    // Микшер → параметр хоста (только если значение действительно другое: без лишних уведомлений)
    void SamplitProcessor::pushParam (int mic)
    {
        if (! juce::isPositiveAndBelow (mic, kAutomatedMics) || ! juce::isPositiveAndBelow (mic, (int) mixer.size()))
            return;
        const auto& m = mixer[(size_t) mic];
        if (std::abs (levelParams[(size_t) mic]->get() - m.levelDb) > 0.051f)
            *levelParams[(size_t) mic] = m.levelDb;
        if (std::abs (panParams[(size_t) mic]->get() - m.pan) > 0.0051f)
            *panParams[(size_t) mic] = m.pan;
    }

    void SamplitProcessor::pushAllParams()
    {
        for (int i = 0; i < kAutomatedMics; ++i)
            pushParam (i);
    }

    // Параметр → микшер (message thread). Допуск в полшага параметра: значение микшера, не попавшее на шаг, не затирается
    void SamplitProcessor::handleAsyncUpdate()
    {
        const auto current = getKit();
        if (current == nullptr)
            return;

        bool changed = false;
        const int n = juce::jmin (kAutomatedMics, (int) mixer.size(), (int) current->mics.size());
        for (int i = 0; i < n; ++i)
        {
            auto s = mixer[(size_t) i];
            bool edited = false;
            if (const float v = levelParams[(size_t) i]->get(); std::abs (v - s.levelDb) > 0.051f)
            {
                s.levelDb = v;
                edited = true;
            }
            if (const float v = panParams[(size_t) i]->get(); std::abs (v - s.pan) > 0.0051f)
            {
                s.pan = v;
                edited = true;
            }
            if (edited)
            {
                setMicState (i, s);
                changed = true;
            }
        }
        if (changed)
            sendChangeMessage();
    }

    Pick SamplitProcessor::getPickChoice (int choice) const
    {
        return juce::isPositiveAndBelow (choice, (int) picks.size()) ? picks[(size_t) choice] : Pick::Random;
    }

    void SamplitProcessor::setPickChoice (int choice, Pick pick)
    {
        const auto current = getKit();
        if (current == nullptr || ! juce::isPositiveAndBelow (choice, (int) picks.size()) || choice >= (int) current->choices.size())
            return;
        picks[(size_t) choice] = pick;
        {
            const juce::ScopedLock sl (stateLock);
            savedPicks[juce::String (current->choices[(size_t) choice].id)] = pick;
        }
        applyPicks();
    }

    void SamplitProcessor::applyPicks()
    {
        for (int i = 0; i < Kit::kMaxChoices; ++i)
            kitEngine.setPickChoice (i, i < (int) picks.size() ? (int) picks[(size_t) i] : -1);
    }

    // Solo: если хоть один микрофон в соло, остальные молчат. Mute сильнее соло
    void SamplitProcessor::applyMixer()
    {
        const bool anySolo = std::any_of (mixer.begin(), mixer.end(), [] (const MicState& m) { return m.solo; });
        for (int i = 0; i < Kit::kMaxMics; ++i)
        {
            if (i < (int) mixer.size())
            {
                const auto& m = mixer[(size_t) i];
                const bool silent = m.mute || (anySolo && ! m.solo);
                kitEngine.setMicMix (i, silent ? -100.0f : m.levelDb, m.pan);
                kitEngine.setMicOutput (i, m.out);
            }
            else
            {
                kitEngine.setMicMix (i, 0.0f, 0.0f);
                kitEngine.setMicOutput (i, -1);
            }
        }
    }

    void SamplitProcessor::setServiceMode (bool on)
    {
        serviceMode.store (on);
        kitActive.store (! on && getKit() != nullptr);
        sendChangeMessage();
    }

    std::shared_ptr<const Kit> SamplitProcessor::getKit() const
    {
        const juce::ScopedLock sl (stateLock);
        return kit;
    }

    juce::String SamplitProcessor::getPresetName() const
    {
        const juce::ScopedLock sl (stateLock);
        return presetName;
    }

    void SamplitProcessor::setPresetName (const juce::String& name)
    {
        {
            const juce::ScopedLock sl (stateLock);
            presetName = name;
        }
        sendChangeMessage();
    }

    // ---------------------------------------------------------------- прослушивание

    void SamplitProcessor::startPreviewWith (SamplePtr sample)
    {
        if (sample == nullptr)
            return;

        if (previewSample != nullptr)
            retired.push_back ({ nullptr, previewSample, nullptr, juce::Time::getMillisecondCounter() });
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
            state.setProperty ("preset", presetName, nullptr);
            state.setProperty ("mix", mixName, nullptr);
            state.setProperty ("mixModified", mixModified, nullptr);
            for (const auto& [id, m] : savedMix)
            {
                juce::ValueTree v ("MIC");
                v.setProperty ("id", id, nullptr);
                v.setProperty ("level", m.levelDb, nullptr);
                v.setProperty ("pan", m.pan, nullptr);
                v.setProperty ("mute", m.mute, nullptr);
                v.setProperty ("solo", m.solo, nullptr);
                v.setProperty ("out", m.out, nullptr);
                state.appendChild (v, nullptr);
            }
            for (const auto& [id, level] : savedPieces)
            {
                juce::ValueTree v ("PIECE");
                v.setProperty ("id", id, nullptr);
                v.setProperty ("level", level, nullptr);
                state.appendChild (v, nullptr);
            }
            for (const auto& [id, pick] : savedPicks)
            {
                juce::ValueTree v ("PICK");
                v.setProperty ("id", id, nullptr);
                v.setProperty ("mode", pickToString (pick), nullptr);
                state.appendChild (v, nullptr);
            }
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
        std::map<juce::String, MicState> mix;
        std::map<juce::String, Pick> pickModes;
        std::map<juce::String, float> pieceMap;
        for (const auto& v : state)
        {
            if (v.hasType ("PIECE"))
            {
                pieceMap[v.getProperty ("id").toString()] = juce::jlimit (-24.0f, 12.0f, (float) v.getProperty ("level", 0.0));
                continue;
            }

            if (v.hasType ("PICK"))
            {
                Pick pick;
                if (pickFromString (v.getProperty ("mode").toString(), pick))
                    pickModes[v.getProperty ("id").toString()] = pick;
                continue;
            }

            if (v.hasType ("MIC"))
            {
                MicState m;
                m.levelDb = juce::jlimit (-60.0f, 12.0f, (float) v.getProperty ("level", 0.0));
                m.pan = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("pan", 0.0));
                m.mute = (bool) v.getProperty ("mute", false);
                m.solo = (bool) v.getProperty ("solo", false);
                m.out = juce::jlimit (0, kNumOutputs, (int) v.getProperty ("out", 0));
                mix[v.getProperty ("id").toString()] = m;
                continue;
            }

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
        {
            const juce::ScopedLock sl (stateLock);
            presetName = state.getProperty ("preset").toString();
            mixName = state.getProperty ("mix").toString();
            mixModified = (bool) state.getProperty ("mixModified");
            savedMix = std::move (mix);
            savedPicks = std::move (pickModes);
            savedPieces = std::move (pieceMap);
        }

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
            restorePreset();
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
