#include "PluginProcessor.h"
#include "Studio600Names.h"
#include "Brand.h"

#include <algorithm>

namespace samplit
{
    static_assert (Kit::kMaxMics <= 32, "маски mute/solo: по 32 бита в одном uint64");
    static_assert (SamplitProcessor::kAutomatedMics == kStudio600MicCount, "параметры хоста по слотам Studio600");
    namespace
    {
        juce::File& dataDirOverrideRef() { static juce::File f; return f; }
        juce::File dataDirOverride() { return dataDirOverrideRef(); }
    }

    juce::AudioProcessor::BusesProperties SamplitProcessor::makeBuses()
    {
        // Шина 0 это Main (стерео). Шины 1..7 моно (отдельные микрофоны), 8..10 стерео (Toms, Over, Room). DAW включает их сама
        auto props = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (int i = 0; i < kNumOutputs; ++i)
            props = props.withOutput (juce::String::fromUTF8 (studio600OutputName (i + 1)),
                                      isStereoOutput (i + 1) ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), false);
        return props;
    }

    SamplitProcessor::SamplitProcessor() : AudioProcessor (makeBuses())
    {
        kitEngine.setStreamer (&streamer);
        license = std::make_unique<LicenseManager> (LicenseManager::defaultConfig());
        license->addChangeListener (this);

        const auto over = dataDirOverride();
        dataDir = over != juce::File() ? over : license->getTokenFile().getParentDirectory();
        serviceMode = juce::SystemStats::getEnvironmentVariable ("SAMPLIT_SERVICE", {}) == "1";
        setup = loadDefaultSetup();
        pushPlaceLevels (setup);

        // Параметры автоматизации: по слоту микрофона Studio600 (kStudio600Mics), не по порядку паков. Идентификатор
        // "mic_<id>_level" / "mic_<id>_pan" остаётся за микрофоном при любом составе установки
        for (int i = 0; i < kAutomatedMics; ++i)
        {
            const juce::String id (std::string (kStudio600Mics[i].id));
            const juce::String name (studio600MicName (kStudio600Mics[i].id));
            levelParams[(size_t) i] = new juce::AudioParameterFloat (juce::ParameterID { "mic_" + id + "_level", 1 }, name + " level",
                                                                     juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f);
            panParams[(size_t) i] = new juce::AudioParameterFloat (juce::ParameterID { "mic_" + id + "_pan", 1 }, name + " pan",
                                                                   juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), defaultMicState (i).pan);
            addParameter (levelParams[(size_t) i]);
            addParameter (panParams[(size_t) i]);
            levelParams[(size_t) i]->addListener (this);
            panParams[(size_t) i]->addListener (this);
        }
        mixer.assign ((size_t) Kit::kMaxMics, MicState {});
        for (int i = 0; i < kAutomatedMics; ++i)
        {
            mixer[(size_t) i] = defaultMicState (i);
            mixer[(size_t) i].out = -1;      // до загрузки кита выход берётся из манифеста (как в движке по умолчанию)
        }
        startTimer (50);

        // Новый экземпляр без состояния играет стандартную установку. Любое явное действие (состояние проекта, setSetup,
        // свой кит, другая лицензия) снимает флаг раньше, чем message thread дойдёт до этого отложенного вызова
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        {
            std::weak_ptr<int> alive = lifeToken;
            juce::MessageManager::callAsync ([this, alive]
            {
                if (! alive.expired() && initialApplyPending)
                    applySetup();
            });
        }
    }

    SamplitProcessor::~SamplitProcessor()
    {
        closeAllGestures();
        for (auto* p : levelParams)
            p->removeListener (this);
        for (auto* p : panParams)
            p->removeListener (this);
        stopTimer();
        if (license != nullptr)
            license->removeChangeListener (this);
        lifeToken.reset();
        cancelKitLoad();
        kitLoaderPool.removeAllJobs (true, 10000);
        streamer.shutdown();        // до удаления китов: поток чтения обращается к клипам звучащих голосов
    }

    void SamplitProcessor::prepareToPlay (double sampleRate, int)
    {
        kitEngine.prepare (sampleRate);
        patternPlayer.setSampleRate (sampleRate);
    }

    bool SamplitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;

        // Отдельные выходы: 1..7 моно, 8..10 стерео (или выключены)
        for (int i = 1; i < layouts.outputBuses.size(); ++i)
        {
            const auto& set = layouts.outputBuses.getReference (i);
            if (! set.isDisabled() && set != (isStereoOutput (i) ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono()))
                return false;
        }

        return layouts.inputBuses.isEmpty();
    }

    void SamplitProcessor::releaseResources()
    {
        // Движок остановлен: удары старых китов иначе числились бы звучащими и киты не освобождались (collectGarbage по таймеру)
        kitEngine.releaseAll();
    }

    // Bypass: звук не рендерится, голоса не должны «замерзать» и доигрываться после снятия bypass; накопленные экранные ноты тоже сброс
    void SamplitProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        buffer.clear();
        kitEngine.releaseAll();
        queueRead.store (queueWrite.load());
        activeLo.store (0);
        activeHi.store (0);
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

        // Параметры хоста читаются здесь, в начале блока: автоматизация привязана к блоку, а не к таймеру (офлайн повторяема)
        pushMicTargets();

        // Кит защищён от удаления на весь блок (hazard pointer), все выходы из функции снимают защиту.
        // Уровень и панорама микрофонов идут линейной рампой на длину блока
        struct BlockScope
        {
            KitEngine& e;
            BlockScope (KitEngine& engine, int ramp) noexcept : e (engine) { e.beginBlock (ramp); }
            ~BlockScope() { e.endBlock(); }
        } blockScope (kitEngine, buffer.getNumSamples());

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

        // Без кита KitEngine::noteOn выходит сразу, а render ничего не пишет: блок остаётся тишиной
        // Позиция транспорта в сэмплах: нужна повторяемому выбору удара. Без транспорта (ноты с экрана) выбор живой
        int64_t transportStart = 0;
        bool transportValid = false;
        if (auto* head = getPlayHead())
            if (const auto info = head->getPosition(); info && info->getIsPlaying())
            {
                if (const auto t = info->getTimeInSamples())
                {
                    transportStart = *t;
                    transportValid = true;
                }
                else if (const auto ppq = info->getPpqPosition(); ppq && info->getBpm() && *info->getBpm() > 0.0)
                {
                    // Хост даёт только PPQ: позиция в сэмплах из долей и темпа
                    transportStart = (int64_t) std::llround (*ppq * 60.0 / *info->getBpm() * getSampleRate());
                    transportValid = true;
                }
            }
        auto noteOn = [&] (int note, int velocity) { kitEngine.noteOn (note, velocity); };
        auto renderRange = [&] (int from, int to) { kitEngine.render (outL, outR, from, to); };

        const int numSamples = buffer.getNumSamples();
        kitEngine.setOffline (isNonRealtime());

        // Ноты с экранной клавиатуры
        // Если audio thread стоял и пользователь нажал больше kQueue клавиш, устаревшие ноты отбрасываются (не играть залпом)
        kitEngine.setStrikePosition (0, false);
        auto r = queueRead.load();
        const auto w = queueWrite.load();
        if (w - r > (uint32_t) kQueue)
            r = w - (uint32_t) kQueue;
        for (; r != w; ++r)
        {
            const auto e = queue[r % kQueue].load();
            noteOn ((int) (e & 0xff), (int) ((e >> 8) & 0xff));
        }
        queueRead.store (r);

        // Партия плагина: удары по расписанию, сливаются с MIDI по отсчёту блока (при равенстве раньше MIDI-событие)
        std::array<PatternPlayer::Hit, PatternPlayer::kMaxHits> hits;
        const int numHits = patternPlayer.process (numSamples, hits.data(), (int) hits.size());
        int nextHit = 0;

        // MIDI: рендерим кусками между событиями, чтобы нота стартовала в нужном сэмпле
        int pos = 0;
        auto playHitsBefore = [&] (int limit)
        {
            for (; nextHit < numHits && hits[(size_t) nextHit].offset < limit; ++nextHit)
            {
                const auto& h = hits[(size_t) nextHit];
                const int at = juce::jlimit (pos, numSamples, h.offset);
                if (at > pos)
                    renderRange (pos, at);
                pos = at;
                kitEngine.setStrikePosition (h.seedPos, true);
                noteOn (h.key, h.velocity);
            }
        };
        for (const auto meta : midi)
        {
            playHitsBefore (meta.samplePosition);
            const auto msg = meta.getMessage();
            if (msg.isAllSoundOff() || msg.isAllNotesOff())
            {
                // CC120 / CC123: все удары затухают за ~10 мс с этой позиции блока
                const int at = juce::jlimit (pos, numSamples, meta.samplePosition);
                if (at > pos)
                    renderRange (pos, at);
                pos = at;
                kitEngine.fadeAll();
                continue;
            }
            if (! msg.isNoteOn())
                continue;

            const int at = juce::jlimit (pos, numSamples, meta.samplePosition);
            if (at > pos)
                renderRange (pos, at);
            pos = at;
            kitEngine.setStrikePosition (transportStart + meta.samplePosition, transportValid);
            noteOn (msg.getNoteNumber(), msg.getVelocity());
        }
        playHitsBefore (numSamples);
        if (numSamples > pos)
            renderRange (pos, numSamples);

        kitEngine.publishActivity();
        const auto kitKeys = kitEngine.getActiveKeys();
        activeLo.store (kitKeys[0]);
        activeHi.store (kitKeys[1]);
    }

    void SamplitProcessor::pushMicTargets() noexcept
    {
        const auto masks = muteSoloMask.load (std::memory_order_relaxed);      // одна загрузка: mute и solo согласованы
        const auto mute = (uint32_t) (masks & 0xffffffffu);
        const auto solo = (uint32_t) (masks >> 32);
        for (int i = 0; i < Kit::kMaxMics; ++i)
        {
            const bool silent = ((mute >> i) & 1u) != 0 || (solo != 0 && ((solo >> i) & 1u) == 0);
            const float level = i < kAutomatedMics ? levelParams[(size_t) i]->get() : mixLevelDb[(size_t) i].load (std::memory_order_relaxed);
            const float pan = i < kAutomatedMics ? panParams[(size_t) i]->get() : mixPan[(size_t) i].load (std::memory_order_relaxed);
            kitEngine.setMicMix (i, silent ? -100.0f : level, pan);
        }
    }

    void SamplitProcessor::collectGarbage()
    {
        // Кит удаляется, только когда он не текущий, не защищён блоком (hazard), не звучит и не читается стримером:
        // гарантия протокола KitEngine::isUsing, время не используется
        std::erase_if (retired, [&] (const Retired& r)
        {
            return ! (r.kit != nullptr && kitEngine.isUsing (r.kit.get()));
        });
    }

    // Message thread, каждые 50 мс: параметры хоста в микшер (флаг ставит parameterValueChanged) и уборка китов раз в секунду
    void SamplitProcessor::timerCallback()
    {
        if (paramsDirty.exchange (false, std::memory_order_acq_rel))
            applyHostParams();
        if (++garbageTicks >= 20)
        {
            garbageTicks = 0;
            collectGarbage();
            patternPlayer.collectGarbage();
        }
    }

    // ---------------------------------------------------------------- кит

    void SamplitProcessor::setKit (std::shared_ptr<const Kit> newKit)
    {
        initialApplyPending = false;
        std::shared_ptr<const Kit> old;
        {
            const juce::ScopedLock sl (stateLock);
            old = std::move (kit);
            kit = newKit;
        }

        {
            // Оценка хвоста для хоста: самый длинный клип кита (без блокировок, пишется до публикации в движок)
            double longest = 0.0;
            if (newKit != nullptr)
                for (const auto& piece : newKit->pieces)
                    for (const auto& hit : piece.hits)
                        for (const auto& f : hit.files)
                            if (f.clip != nullptr && f.clip->sampleRate > 0.0)
                                longest = std::max (longest, f.clip->frames() / f.clip->sampleRate);
            tailSeconds.store (newKit != nullptr ? longest : 30.0, std::memory_order_relaxed);
        }
        kitEngine.setKit (newKit.get());
        kitEngine.resetHealth();        // счётчики относятся к прежнему киту (после setKit: новый кит начинает с нуля)
        picks.clear();
        if (newKit != nullptr)
            for (const auto& c : newKit->choices)
                picks.push_back (c.pick);

        if (old != nullptr)
            retired.push_back ({ std::move (old) });
        collectGarbage();
        sendChangeMessage();
    }

    void SamplitProcessor::setLicenseManager (std::unique_ptr<LicenseManager> manager)
    {
        // Загрузка могла начаться со старым менеджером (ключи, папки): ждём её конца до замены
        initialApplyPending = false;
        cancelKitLoad();
        kitLoaderPool.removeAllJobs (true, 10000);
        if (license != nullptr)
            license->removeChangeListener (this);
        license = std::move (manager);
        setupRetryPending = false;
        ++catalogEpoch;
        if (license != nullptr)
        {
            license->addChangeListener (this);
            const auto over = dataDirOverride();
            dataDir = over != juce::File() ? over : license->getTokenFile().getParentDirectory();
        }
    }

    // Лицензия проверилась или обновилась (message thread). Источник каталога мог измениться; если проект открылся раньше
    // и пакеты не нашлись, повторяем
    void SamplitProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        ++catalogEpoch;
        if (license == nullptr || ! license->isEnforced() || ! license->getInfo().isValid())
            return;
        if (setupRetryPending)
        {
            setupRetryPending = false;
            applySetup();
        }
    }

    KitLoadOptions SamplitProcessor::makeLoadOptions() const
    {
        KitLoadOptions options;
        options.packDir = license->getPackDir();
        if (const auto root = license->getLibraryRoot(); root.isDirectory())
        {
            options.libraryRoot = root;
            options.manifestDir = license->getManifestDir();
        }
        options.requireLibrary = ! license->isEnforced();
        options.keyFor = [this] (const juce::String& id, pack::PackKey& key) { return license->keyFor (id, key); };
        return options;
    }

    void SamplitProcessor::cancelKitLoad()
    {
        if (kitCancel != nullptr)
            kitCancel->store (true);
        ++kitGeneration;
        kitLoading.store (false);
    }

    void SamplitProcessor::loadKitAsync (const KitSpec& spec, KitLoadOptions options,
                                         std::function<void (const KitLoadResult&)> done)
    {
        startLoad ([spec] (const KitLoadOptions& o) { return loadKit (spec, o); },
                   std::move (options), nullptr, std::move (done));
    }

    void SamplitProcessor::startLoad (std::function<KitLoadResult (const KitLoadOptions&)> work,
                                      KitLoadOptions options, std::shared_ptr<std::vector<CatalogPack>> catalogOut,
                                      std::function<void (const KitLoadResult&)> done)
    {
        initialApplyPending = false;
        cancelKitLoad();
        auto cancel = std::make_shared<std::atomic<bool>> (false);
        kitCancel = cancel;
        const auto generation = kitGeneration;
        kitLoading.store (true);
        kitProgress->store (0.0f);

        options.cancel = cancel.get();
        // Задача может пережить процессор (таймаут в деструкторе): прогресс пишется в общий атомик, а не в член класса
        options.progress = [sink = kitProgress] (float f) { sink->store (f); };
        std::weak_ptr<int> alive = lifeToken;

        kitLoaderPool.addJob ([this, alive, work = std::move (work), options = std::move (options), catalogOut, cancel,
                               generation, done = std::move (done)]
        {
            auto result = std::make_shared<KitLoadResult> (work (options));

            juce::MessageManager::callAsync ([this, alive, result, catalogOut, cancel, generation, done]
            {
                if (alive.expired())
                    return;     // процессор уничтожен
                if (generation != kitGeneration)
                {
                    // устарело или отменено: сообщаем вызвавшему, что результата не будет
                    if (done)
                    {
                        KitLoadResult stale;
                        stale.cancelled = true;
                        done (stale);
                    }
                    return;
                }

                kitLoading.store (false);
                kitProgress->store (1.0f);
                if (catalogOut != nullptr)
                    catalog = *catalogOut;
                if (result->ok())
                {
                    // Микшер по слотам микрофонов, не зависит от состава кита: сохранённое значение по id микрофона, иначе
                    // умолчание (для микрофона кита: выход и панорама из манифеста, для отсутствующего: таблица Studio600).
                    // Прежние записи не стираются: микрофоны ушедшего пака вернутся с прежними значениями
                    std::vector<MicState> fresh ((size_t) Kit::kMaxMics);
                    {
                        const juce::ScopedLock sl (stateLock);
                        for (int i = 0; i < Kit::kMaxMics; ++i)
                        {
                            auto& f = fresh[(size_t) i];
                            f = i < kAutomatedMics ? defaultMicState (i) : MicState {};
                            if (i >= (int) result->kit->mics.size() || result->kit->mics[(size_t) i].id.empty())
                            {
                                // Микрофона нет в ките: значение берём по id слота (известные слоты Studio600)
                                if (i < kAutomatedMics)
                                    if (const auto saved = savedMix.find (juce::String (std::string (kStudio600Mics[i].id))); saved != savedMix.end())
                                    {
                                        f = saved->second;
                                        if (f.out < 0)
                                            f.out = defaultMicState (i).out;
                                    }
                                continue;
                            }
                            const auto& mic = result->kit->mics[(size_t) i];
                            const auto saved = savedMix.find (juce::String (mic.id));
                            f = saved != savedMix.end() ? saved->second : MicState { 0.0f, mic.pan, false, false, mic.out };
                            if (f.out < 0)
                                f.out = mic.out;
                            savedMix[juce::String (mic.id)] = f;
                        }
                    }
                    const auto sameMix = [] (const MicState& a, const MicState& b)
                    {
                        return a.levelDb == b.levelDb && a.pan == b.pan && a.mute == b.mute && a.solo == b.solo && a.out == b.out;
                    };
                    bool mixChanged = fresh.size() != mixer.size();
                    for (size_t i = 0; ! mixChanged && i < fresh.size(); ++i)
                        mixChanged = ! sameMix (fresh[i], mixer[i]);
                    mixer = std::move (fresh);
                    applyMixer();           // до публикации: движок уже получил микшер, звучащие хвосты не меняют шину и панораму
                    setKit (result->kit);
                    // Параметры хоста перезаписываются только если микшер действительно изменился (загрузка проекта),
                    // а не при каждой смене кита
                    if (mixChanged)
                        pushAllParams();

                    const auto current = getSetup();
                    pushPlaceLevels (current);
                    // Выбор режима мог измениться во время загрузки: берём актуальную установку, не снимок загрузчика
                    for (size_t i = 0; i < result->kit->choices.size() && i < picks.size(); ++i)
                    {
                        const juce::String id (result->kit->choices[i].id);
                        const auto colon = id.indexOfChar (':');
                        if (colon <= 0)
                            continue;
                        const auto placeId = id.substring (0, colon);
                        const auto groupId = id.substring (colon + 1).toStdString();
                        if (const auto* place = current.find (placeId.toStdString()))
                            if (const auto it = place->picks.find (groupId); it != place->picks.end())
                                picks[i] = it->second;
                    }
                    applyPicks();
                }

                setupRetryPending = license->isEnforced() && ! result->cancelled && ! result->missingPacks.empty()
                                    && ! license->getInfo().isValid();

                lastLoad = *result;
                lastLoad.kit = nullptr;
                sendChangeMessage();
                if (done)
                    done (*result);
            });
        });
    }

    // ---------------------------------------------------------------- установка

    Setup SamplitProcessor::getSetup() const
    {
        const juce::ScopedLock sl (stateLock);
        return setup;
    }

    std::vector<CatalogPack> SamplitProcessor::getCatalog() const
    {
        return catalog;
    }

    void SamplitProcessor::pushPlaceLevels (const Setup& s)
    {
        for (const auto& p : s.places)
            if (const auto* def = findPlace (p.placeId))
                kitEngine.setPlaceLevel (def->slot, p.levelDb);
    }

    std::vector<CatalogPack> SamplitProcessor::scanCatalogCached (const KitLoadOptions& o, bool includeUnverified)
    {
        const auto key = o.libraryRoot.getFullPathName() + "|" + o.manifestDir.getFullPathName() + "|" + o.packDir.getFullPathName()
                         + "|" + juce::String ((int) o.requireLibrary) + "|" + juce::String ((int) includeUnverified);
        const auto epoch = catalogEpoch.load();
        {
            const juce::ScopedLock sl (catalogLock);
            if (catalogCacheValid && catalogCacheEpoch == epoch && catalogCacheKey == key)
                return catalogCache;
        }
        auto fresh = scanPackCatalog (o, includeUnverified);
        if (! fresh.empty())        // пустой каталог не запоминаем: источник мог ещё не подключиться
        {
            const juce::ScopedLock sl (catalogLock);
            catalogCache = fresh;
            catalogCacheKey = key;
            catalogCacheEpoch = epoch;
            catalogCacheValid = true;
        }
        return fresh;
    }

    void SamplitProcessor::applySetup (std::function<void (const KitLoadResult&)> done)
    {
        const auto snapshot = getSetup();
        auto options = makeLoadOptions();
        options.reuse = getKit();           // клипы того же источника берутся из текущего кита без декодирования
        const bool service = serviceMode;
        pushPlaceLevels (snapshot);

        auto catalogOut = std::make_shared<std::vector<CatalogPack>>();
        auto work = [this, snapshot, service, catalogOut] (const KitLoadOptions& o)
        {
            *catalogOut = scanCatalogCached (o, service);
            std::vector<juce::String> warnings;
            const auto spec = makeKitSpec (snapshot, *catalogOut, service, warnings);
            auto res = loadKit (spec, o);
            res.warnings.insert (res.warnings.begin(), warnings.begin(), warnings.end());
            return res;
        };
        startLoad (std::move (work), std::move (options), catalogOut, std::move (done));
    }

    void SamplitProcessor::applySetupSoon()
    {
        std::weak_ptr<int> alive = lifeToken;
        auto apply = [this, alive]
        {
            if (! alive.expired())
                applySetup();
        };
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        if (mm == nullptr || mm->isThisTheMessageThread())
            apply();
        else
            juce::MessageManager::callAsync (apply);
    }

    void SamplitProcessor::setSetup (const Setup& s)
    {
        initialApplyPending = false;
        auto fixed = Setup::factory();      // места в порядке slot; чего нет во входной установке, берётся заводское
        for (auto& p : fixed.places)
            if (const auto* in = s.find (p.placeId))
            {
                p = *in;
                p.levelDb = juce::jlimit (kPlaceLevelMinDb, kPlaceLevelMaxDb, p.levelDb);
            }
        {
            const juce::ScopedLock sl (stateLock);
            setup = std::move (fixed);
        }
        applySetup();
    }

    void SamplitProcessor::setPlaceEnabled (const juce::String& placeId, bool enabled)
    {
        {
            const juce::ScopedLock sl (stateLock);
            auto it = std::find_if (setup.places.begin(), setup.places.end(), [&] (const PlaceSetup& p) { return placeId == p.placeId.c_str(); });
            if (it == setup.places.end() || it->enabled == enabled)
                return;
            it->enabled = enabled;
        }
        initialApplyPending = false;
        applySetup();
    }

    void SamplitProcessor::setPlacePack (const juce::String& placeId, const juce::String& packId, const juce::String& pieceId)
    {
        {
            const juce::ScopedLock sl (stateLock);
            auto it = std::find_if (setup.places.begin(), setup.places.end(), [&] (const PlaceSetup& p) { return placeId == p.placeId.c_str(); });
            if (it == setup.places.end())
                return;
            if (packId != it->packId.c_str())
                it->picks.clear();          // группы другого пака: старый выбор к ним не относится
            it->packId = packId.toStdString();
            it->pieceId = pieceId.toStdString();
        }
        initialApplyPending = false;
        applySetup();
    }

    void SamplitProcessor::setPlaceLevel (const juce::String& placeId, float levelDb)
    {
        const auto level = juce::jlimit (kPlaceLevelMinDb, kPlaceLevelMaxDb, levelDb);
        const auto* def = findPlace (placeId.toStdString());
        if (def == nullptr)
            return;
        {
            const juce::ScopedLock sl (stateLock);
            for (auto& p : setup.places)
                if (placeId == p.placeId.c_str())
                    p.levelDb = level;
        }
        kitEngine.setPlaceLevel (def->slot, level);
    }

    void SamplitProcessor::setGroupPick (const juce::String& choiceId, Pick pick)
    {
        const auto colon = choiceId.indexOfChar (':');
        if (colon <= 0)
            return;
        const auto placeId = choiceId.substring (0, colon);
        const auto groupId = choiceId.substring (colon + 1).toStdString();
        {
            const juce::ScopedLock sl (stateLock);
            for (auto& p : setup.places)
                if (placeId == p.placeId.c_str())
                    p.picks[groupId] = pick;
        }
        if (const auto current = getKit())
            for (int i = 0; i < (int) current->choices.size(); ++i)
                if (choiceId == current->choices[(size_t) i].id.c_str())
                    setPickChoice (i, pick);
    }

    void SamplitProcessor::setDataDirOverride (const juce::File& f) { dataDirOverrideRef() = f; }

    Setup SamplitProcessor::loadDefaultSetup() const
    {
        const auto file = defaultSetupFile();
        Setup loaded;
        juce::String error;
        if (file.existsAsFile() && Setup::fromJson (file.loadFileAsString(), loaded, error))
            return loaded;
        return Setup::factory();
    }

    bool SamplitProcessor::saveAsDefaultSetup()
    {
        dataDir.createDirectory();
        return defaultSetupFile().replaceWithText (getSetup().toJson(), false, false, "\r\n");
    }

    void SamplitProcessor::resetToDefaultSetup()
    {
        setSetup (loadDefaultSetup());
    }

    void SamplitProcessor::restoreFactorySetup()
    {
        defaultSetupFile().deleteFile();
        setSetup (Setup::factory());
    }

    void SamplitProcessor::rescanCatalog()
    {
        ++catalogEpoch;
        applySetup();
    }

    // Умолчание слота Studio600: уровень 0 дБ, панорама и выход из таблицы
    SamplitProcessor::MicState SamplitProcessor::defaultMicState (int slot)
    {
        MicState m;
        if (juce::isPositiveAndBelow (slot, kStudio600MicCount))
        {
            m.pan = studio600MicPan (kStudio600Mics[slot].id);
            m.out = kStudio600Mics[slot].defaultOut;
        }
        return m;
    }

    // id микрофона слота: для известных слотов Studio600 из таблицы, для остальных из текущего кита (пусто, если слот свободен)
    juce::String SamplitProcessor::micIdForSlot (int slot) const
    {
        if (juce::isPositiveAndBelow (slot, kStudio600MicCount))
            return juce::String (std::string (kStudio600Mics[slot].id));
        const auto current = getKit();
        if (current != nullptr && slot < (int) current->mics.size())
            return juce::String (current->mics[(size_t) slot].id);
        return {};
    }

    SamplitProcessor::MicState SamplitProcessor::getMicState (int mic) const
    {
        return juce::isPositiveAndBelow (mic, (int) mixer.size()) ? mixer[(size_t) mic] : MicState {};
    }

    void SamplitProcessor::setMicState (int mic, const MicState& state, bool fromUi)
    {
        if (! juce::isPositiveAndBelow (mic, (int) mixer.size()))
            return;

        auto s = state;
        s.levelDb = juce::jlimit (-60.0f, 12.0f, s.levelDb);
        s.pan = juce::jlimit (-1.0f, 1.0f, s.pan);
        s.out = juce::jlimit (0, kNumOutputs, s.out);
        mixer[(size_t) mic] = s;
        {
            const juce::ScopedLock sl (stateLock);
            if (const auto id = micIdForSlot (mic); id.isNotEmpty())
                savedMix[id] = s;
        }
        applyMixer();
        pushParam (mic, fromUi);
    }

    juce::String SamplitProcessor::exportSetupFile() const
    {
        juce::var root;
        juce::String error;
        juce::JSON::parse (getSetup().toJson(), root);
        juce::Array<juce::var> mix;
        for (int i = 0; i < kStudio600MicCount; ++i)
        {
            const auto m = getMicState (i);
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", juce::String (std::string (kStudio600Mics[i].id)));
            o->setProperty ("levelDb", (double) m.levelDb);
            o->setProperty ("pan", (double) m.pan);
            o->setProperty ("mute", m.mute);
            o->setProperty ("solo", m.solo);
            o->setProperty ("out", m.out);
            mix.add (juce::var (o));
        }
        if (auto* obj = root.getDynamicObject())
            obj->setProperty ("mix", mix);
        return juce::JSON::toString (root);
    }

    bool SamplitProcessor::importSetupFile (const juce::String& json, juce::String& error)
    {
        Setup s;
        if (! Setup::fromJson (json, s, error))
            return false;
        juce::var v;
        juce::JSON::parse (json, v);
        setSetup (s);
        if (const auto* arr = v.getProperty ("mix", {}).getArray())
            for (const auto& item : *arr)
            {
                if (! item.isObject())
                    continue;
                const auto id = item.getProperty ("id", {}).toString();
                for (int i = 0; i < kStudio600MicCount; ++i)
                    if (id == juce::String (std::string (kStudio600Mics[i].id)))
                    {
                        auto m = getMicState (i);
                        m.levelDb = (float) (double) item.getProperty ("levelDb", m.levelDb);
                        m.pan = (float) (double) item.getProperty ("pan", m.pan);
                        m.mute = (bool) item.getProperty ("mute", m.mute);
                        m.solo = (bool) item.getProperty ("solo", m.solo);
                        m.out = (int) item.getProperty ("out", m.out);
                        setMicState (i, m);
                    }
            }
        return true;
    }

    void SamplitProcessor::beginMicGesture (int mic, bool pan)
    {
        if (! juce::isPositiveAndBelow (mic, kAutomatedMics) || gestureOpen[(size_t) (mic * 2 + (pan ? 1 : 0))])
            return;
        gestureOpen[(size_t) (mic * 2 + (pan ? 1 : 0))] = true;
        (pan ? static_cast<juce::AudioProcessorParameter*> (panParams[(size_t) mic]) : levelParams[(size_t) mic])->beginChangeGesture();
    }

    void SamplitProcessor::endMicGesture (int mic, bool pan)
    {
        if (! juce::isPositiveAndBelow (mic, kAutomatedMics) || ! gestureOpen[(size_t) (mic * 2 + (pan ? 1 : 0))])
            return;
        gestureOpen[(size_t) (mic * 2 + (pan ? 1 : 0))] = false;
        (pan ? static_cast<juce::AudioProcessorParameter*> (panParams[(size_t) mic]) : levelParams[(size_t) mic])->endChangeGesture();
    }

    void SamplitProcessor::closeAllGestures()
    {
        for (int mic = 0; mic < kAutomatedMics; ++mic)
        {
            endMicGesture (mic, false);
            endMicGesture (mic, true);
        }
    }

    // Микшер → параметр хоста (только если значение действительно другое: без лишних уведомлений)
    void SamplitProcessor::pushParam (int mic, bool fromUi)
    {
        if (! juce::isPositiveAndBelow (mic, kAutomatedMics) || ! juce::isPositiveAndBelow (mic, (int) mixer.size()))
            return;
        const auto& m = mixer[(size_t) mic];
        // fromUi: запись внутри gesture (если жест мыши уже открыт ползунком, он и есть gesture). Таймер и загрузка проекта: без gesture.
        // Обратная связь (parameterValueChanged -> applyHostParams) гаснет: значение микшера уже равно параметру
        const auto write = [&] (juce::AudioParameterFloat* p, float v, bool pan)
        {
            const bool open = gestureOpen[(size_t) (mic * 2 + (pan ? 1 : 0))];
            if (fromUi && ! open)
                p->beginChangeGesture();
            *p = v;
            if (fromUi && ! open)
                p->endChangeGesture();
        };
        if (std::abs (levelParams[(size_t) mic]->get() - m.levelDb) > 0.051f)
            write (levelParams[(size_t) mic], m.levelDb, false);
        if (std::abs (panParams[(size_t) mic]->get() - m.pan) > 0.0051f)
            write (panParams[(size_t) mic], m.pan, true);
    }

    void SamplitProcessor::pushAllParams()
    {
        for (int i = 0; i < kAutomatedMics; ++i)
            pushParam (i);
    }

    // Параметр → микшер (message thread). Допуск в полшага параметра: значение микшера, не попавшее на шаг, не затирается
    void SamplitProcessor::applyHostParams()
    {
        bool changed = false;
        const int n = juce::jmin (kAutomatedMics, (int) mixer.size());
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
        applyPicks();
    }

    void SamplitProcessor::applyPicks()
    {
        for (int i = 0; i < Kit::kMaxChoices; ++i)
            kitEngine.setPickChoice (i, i < (int) picks.size() ? (int) picks[(size_t) i] : -1);
    }

    // Solo: если хоть один микрофон в соло, остальные молчат. Mute сильнее соло. Маски и значения читает audio thread
    // (pushMicTargets), здесь только выходы микрофонов и публикация состояния микшера
    void SamplitProcessor::applyMixer()
    {
        uint32_t mute = 0, solo = 0;
        for (int i = 0; i < Kit::kMaxMics; ++i)
        {
            if (i < (int) mixer.size())
            {
                const auto& m = mixer[(size_t) i];
                mute |= m.mute ? 1u << i : 0u;
                solo |= m.solo ? 1u << i : 0u;
                mixLevelDb[(size_t) i].store (m.levelDb);
                mixPan[(size_t) i].store (m.pan);
                kitEngine.setMicOutput (i, multiOut.load() ? m.out : 0);
            }
            else
            {
                mixLevelDb[(size_t) i].store (0.0f);
                mixPan[(size_t) i].store (0.0f);
                kitEngine.setMicOutput (i, multiOut.load() ? -1 : 0);
            }
        }
        muteSoloMask.store ((uint64_t) mute | ((uint64_t) solo << 32));
    }

    void SamplitProcessor::setMultiOut (bool multi)
    {
        if (multiOut.exchange (multi) == multi)
            return;
        applyMixer();
        sendChangeMessage();
    }

    std::shared_ptr<const Kit> SamplitProcessor::getKit() const
    {
        const juce::ScopedLock sl (stateLock);
        return kit;
    }

    // ---------------------------------------------------------------- состояние проекта

    void SamplitProcessor::setPartMode (bool on)
    {
        partMode.store (on);
        kitEngine.setPartMode (on);
    }

    void SamplitProcessor::setRepeatable (bool on)
    {
        repeatable.store (on);
        kitEngine.setRepeatable (on);
    }

    Pattern SamplitProcessor::getPattern() const
    {
        const juce::ScopedLock sl (stateLock);
        return pattern;
    }

    void SamplitProcessor::setPattern (const Pattern& p)
    {
        Pattern copy = p;
        copy.clampToLimits();
        {
            const juce::ScopedLock sl (stateLock);
            pattern = copy;
        }
        patternPlayer.setPattern (copy, getSampleRate() > 0.0 ? getSampleRate() : 44100.0);
        sendChangeMessage();
    }

    void SamplitProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        juce::ValueTree state ("SAMPLIT");
        state.setProperty ("version", 5, nullptr);
        state.setProperty ("multiOut", multiOut.load(), nullptr);
        state.setProperty ("partMode", partMode.load(), nullptr);
        state.setProperty ("repeatable", repeatable.load(), nullptr);

        Setup copy;
        {
            const juce::ScopedLock sl (stateLock);
            copy = setup;
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
        }
        state.setProperty ("setup", copy.toJson(), nullptr);
        {
            const juce::ScopedLock sl (stateLock);
            if (! pattern.steps.empty())
                state.setProperty ("pattern", pattern.toJson(), nullptr);
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
        const int version = (int) state.getProperty ("version", 0);
        if (version < 2)
            return;

        // До версии 4 выходы были другими (до 15 моно): сохранённый out не переносим, а проект, где отдельные выходы
        // использовались, остаётся в режиме «Мульти-аут» с выходами по умолчанию
        bool oldRouted = false;
        for (const auto& v : state)
            if (v.hasType ("MIC") && (int) v.getProperty ("out", 0) > 0)
                oldRouted = true;
        multiOut.store (version >= 4 ? (bool) state.getProperty ("multiOut", false) : oldRouted);
        setPartMode ((bool) state.getProperty ("partMode", false));
        setRepeatable ((bool) state.getProperty ("repeatable", true));

        {
            Pattern restoredPattern;
            juce::String patternError;
            const auto json = state.getProperty ("pattern").toString();
            if (json.isEmpty() || ! Pattern::fromJson (json, restoredPattern, patternError))
                restoredPattern = Pattern();
            setPattern (restoredPattern);
        }

        std::map<juce::String, MicState> mix;
        for (const auto& v : state)
        {
            if (! v.hasType ("MIC"))
                continue;       // PRESET/PICK/PIECE/GROUP/ZONE и атрибуты старых версий не переносятся
            MicState m;
            m.levelDb = juce::jlimit (-60.0f, 12.0f, (float) v.getProperty ("level", 0.0));
            m.pan = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("pan", 0.0));
            m.mute = (bool) v.getProperty ("mute", false);
            m.solo = (bool) v.getProperty ("solo", false);
            m.out = version < 4 ? -1        // -1: выход по умолчанию из манифеста пака
                                : juce::jlimit (-1, kNumOutputs, (int) v.getProperty ("out", 0));
            mix[v.getProperty ("id").toString()] = m;
        }

        Setup restored = Setup::factory();
        if (version >= 3)
        {
            Setup parsed;
            juce::String error;
            if (Setup::fromJson (state.getProperty ("setup").toString(), parsed, error))
                restored = std::move (parsed);
        }
        else
        {
            // Версия 2: был пресет. Заводская установка; малый барабан по id пресета, остальное пресет не задавал
            const auto presetId = state.getProperty ("preset").toString();
            const char* snarePack = "studio600-snare-live";
            const char* snarePiece = "snare_live";
            if (presetId == "studio600-ahead-low")
            {
                snarePack = "studio600-snare-low";
                snarePiece = "snare_low";
            }
            else if (presetId == "studio600-ambasador")
            {
                snarePack = "studio600-snare-hi";
                snarePiece = "snare_hi";
            }
            for (auto& p : restored.places)
                if (p.placeId == "snare")
                {
                    p.packId = snarePack;
                    p.pieceId = snarePiece;
                }
        }

        bool sameSetup = false;
        {
            const juce::ScopedLock sl (stateLock);
            sameSetup = setup.toJson() == restored.toJson();
            setup = std::move (restored);
            savedMix = std::move (mix);
        }
        initialApplyPending = false;
        {
            // multiOut мог измениться: выходы движка обновляются сразу (mixer принадлежит message thread), не дожидаясь загрузки кита
            std::weak_ptr<int> alive = lifeToken;
            auto apply = [this, alive] { if (! alive.expired()) applyMixer(); };
            auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            if (mm == nullptr || mm->isThisTheMessageThread())
                apply();
            else
                juce::MessageManager::callAsync (apply);
        }
        // Хост (Cubase) часто отдаёт то же состояние: кит уже загружен или грузится, заново читать пакеты не нужно
        if (! (sameSetup && (getKit() != nullptr || kitLoading.load())))
            applySetupSoon();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new samplit::SamplitProcessor();
}
