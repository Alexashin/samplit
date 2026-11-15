#include "KitEngine.h"
#include "DiskStreamer.h"
#include "Interpolation.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace samplit
{
    namespace
    {
        // Один микрофонный голос на [start, end). false: голос закончился
        // Данные клипа голоса: голова в памяти и, если клип потоковый, кольцо с прочитанным хвостом.
        // available: сколько кадров от начала уже можно читать (у обычного клипа весь клип)
        struct ClipView
        {
            const float* head = nullptr;
            int headFrames = 0;
            const float* ring = nullptr;
            int frames = 0;
            int available = 0;
        };

        template <bool Mono>
        bool renderVoice (const ClipView& clip, double& position, double step, int& fadeLeft, int fadeLength,
                          float* left, float* right, float gainL, float gainR, float dL, float dR, int start, int end) noexcept
        {
            const int frames = clip.frames;
            // Данные кончаются раньше конца клипа: дальше читать нельзя (запас на Эрмит и затухание проверен раньше)
            const int readable = clip.available < frames ? clip.available - 3 : frames;
            // Частоты совпадают и высота не сдвинута: позиция целая, интерполировать нечего
            const bool exact = step == 1.0;

            auto at = [&] (int k) noexcept -> float
            {
                return k < clip.headFrames ? clip.head[k] : clip.ring[k & DiskStreamer::kRingMask];
            };

            for (int i = start; i < end; ++i)
            {
                const int idx = (int) position;
                if (idx >= frames || idx >= readable)      // последний отсчёт клипа (frames - 1) тоже играет
                    return false;

                float fade = 1.0f;
                if (fadeLeft >= 0)
                {
                    if (fadeLeft == 0)
                        return false;
                    fade = (float) fadeLeft / (float) fadeLength;
                    --fadeLeft;
                }

                // Кубическая интерполяция Эрмита; на краях клипа недостающие соседи заменяются ближайшими
                float value;
                if (exact)
                {
                    value = at (idx);
                }
                else
                {
                    const float frac = (float) (position - idx);
                    const int im1 = idx > 0 ? idx - 1 : idx;
                    const int i1 = idx + 1 < frames ? idx + 1 : idx;
                    const int i2 = idx + 2 < frames ? idx + 2 : i1;
                    value = hermite (at (im1), at (idx), at (i1), at (i2), frac);
                }

                const float s = value * fade;
                left[i] += s * gainL;
                gainL += dL;
                if constexpr (! Mono)
                {
                    right[i] += s * gainR;
                    gainR += dR;
                }
                position += step;
            }
            return true;
        }
    }

    KitEngine::KitEngine()
    {
        for (auto& l : micLevel)
            l.store (1.0f);
        for (auto& p : micPan)
            p.store (0.0f);
        for (auto& o : micOut)
            o.store (-1);
        for (auto& c : choicePick)
            c.store (-1);
        for (auto& g : placeGain)
            g.store (1.0f, std::memory_order_relaxed);
        lastHit.fill (-1);
    }

    void KitEngine::prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        fadeLength = std::max (1, (int) (0.005 * sr));  // 5 мс: недогрузка диска
        stealFade = std::max (1, (int) (0.010 * sr));   // 10 мс: choke и вытеснение
        gateFade = std::max (1, (int) (0.005 * sr));    // 5 мс: Note Off удара группы -D
        releaseAll();
        lastStrikePos = -1;
        samePosCount = 0;
        offlineGaveUp = false;
        lastHit.fill (-1);
        for (auto& c : cyclePos)
            c.fill (0);
        lastKitSerial = 0;
        mixPrimed = false;
    }

    void KitEngine::releaseAll() noexcept
    {
        for (int i = 0; i < kMaxStrikes; ++i)
            release (i);
        mixPrimed = false;      // bypass/остановка: curLevel/curPan устарели, первый блок после них берёт цель без рампы
    }

    void KitEngine::fadeAll() noexcept
    {
        for (int i = 0; i < kMaxStrikes; ++i)
            if (strikeKit[(size_t) i].load() != nullptr && ! strikes[(size_t) i].fading)
                fadeStrike (strikes[(size_t) i], stealFade);
    }

    StreamHealth KitEngine::getHealth() const
    {
        StreamHealth h;
        h.steals = steals.load (std::memory_order_relaxed);
        h.hardSteals = hardSteals.load (std::memory_order_relaxed);
        h.offlineTimeouts = offlineTimeouts.load (std::memory_order_relaxed);
        if (streamer != nullptr)
        {
            h.underruns = streamer->underruns.load (std::memory_order_relaxed);
            h.noSlot = streamer->noSlot.load (std::memory_order_relaxed);
            h.readErrors = streamer->readErrors.load (std::memory_order_relaxed);
            h.lastError = streamer->getLastError();
        }
        return h;
    }

    void KitEngine::resetHealth()
    {
        steals.store (0);
        hardSteals.store (0);
        offlineTimeouts.store (0);
        if (streamer != nullptr)
            streamer->resetCounters();
    }

    void KitEngine::setKit (const Kit* kit) noexcept
    {
        if (kit != nullptr && streamer != nullptr)
        {
            // Кит со стримящимися клипами: память под хвосты выделяет поток чтения, пока кит ещё не играет
            bool streamed = false;
            for (const auto& p : kit->pieces)
                for (const auto& h : p.hits)
                    for (const auto& f : h.files)
                        streamed = streamed || (f.clip != nullptr && f.clip->isStreamed());
            if (streamed)
                streamer->reserve (256);      // 256 ячеек по 128 КБ = 32 МБ: запас на плотную игру, чтобы не вытеснять хвосты
        }
        if (kit != nullptr)
        {
            selectMode.store ((int) kit->mode);
        }
        for (auto& c : choicePick)
            c.store (-1);
        current.store (kit);
    }

    void KitEngine::setPickChoice (int choice, int pick) noexcept
    {
        if (choice >= 0 && choice < Kit::kMaxChoices)
            choicePick[(size_t) choice].store (pick < 0 ? -1 : std::min (pick, (int) Pick::Exact));
    }

    void KitEngine::setMicMix (int mic, float levelDb, float pan) noexcept
    {
        if (mic < 0 || mic >= Kit::kMaxMics)
            return;
        micLevel[(size_t) mic].store (juce::Decibels::decibelsToGain (levelDb, -100.0f));
        micPan[(size_t) mic].store (std::clamp (pan, -1.0f, 1.0f));
    }

    void KitEngine::setMicOutput (int mic, int out) noexcept
    {
        if (mic >= 0 && mic < Kit::kMaxMics)
            micOut[(size_t) mic].store (out < 0 ? -1 : std::min (out, kNumOutputs));
    }

    void KitEngine::setPlaceLevel (int slot, float levelDb) noexcept
    {
        if (slot >= 0 && slot < kMaxPlaces)
            placeGain[(size_t) slot].store (juce::Decibels::decibelsToGain (levelDb, -100.0f), std::memory_order_relaxed);
    }

    bool KitEngine::isUsing (const Kit* kit) const noexcept
    {
        if (kit == nullptr)
            return false;
        // Порядок важен (см. протокол в KitEngine.h): current, hazard, потом удары и ячейки
        if (current.load() == kit || hazard.load() == kit)
            return true;
        for (const auto& k : strikeKit)
            if (k.load() == kit)
                return true;
        return streamer != nullptr && streamer->isUsing (kit);
    }

    void KitEngine::beginBlock (int rampSamples) noexcept
    {
        // Рампа микрофонов: шаг на отсчёт постоянен на весь блок, куски (между MIDI-событиями) продолжают его
        rampLen = std::clamp (rampSamples, 0, kMaxRampSamples);
        offlineBudgetMs = offlineBudgetMax;
        for (int m = 0; m < Kit::kMaxMics; ++m)
        {
            const float tl = micLevel[(size_t) m].load(), tp = micPan[(size_t) m].load();
            if (! mixPrimed || rampLen == 0)
            {
                curLevel[(size_t) m] = tl;
                curPan[(size_t) m] = tp;
            }
            stepLevel[(size_t) m] = rampLen > 0 ? (tl - curLevel[(size_t) m]) / (float) rampLen : 0.0f;
            stepPan[(size_t) m] = rampLen > 0 ? (tp - curPan[(size_t) m]) / (float) rampLen : 0.0f;
        }
        mixPrimed = true;
        const Kit* k = current.load();
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            hazard.store (k, std::memory_order_seq_cst);
            const Kit* again = current.load();
            if (again == k)
            {
                blockKit = k;
                inBlock = true;
                return;
            }
            k = again;
        }
        // Кит менялся 8 раз подряд (при одном message thread не бывает): блок играет без кита, но уже звучащие удары доигрываются
        hazard.store (nullptr, std::memory_order_release);
        blockKit = nullptr;
        inBlock = true;
    }

    void KitEngine::endBlock() noexcept
    {
        if (rampLen > 0)        // рампа дошла до цели: гасим накопленную погрешность float
            for (int m = 0; m < Kit::kMaxMics; ++m)
            {
                curLevel[(size_t) m] = micLevel[(size_t) m].load();
                curPan[(size_t) m] = micPan[(size_t) m].load();
            }
        rampLen = 0;
        inBlock = false;
        blockKit = nullptr;
        hazard.store (nullptr, std::memory_order_release);
    }

    int KitEngine::getNumActiveStrikes() const noexcept
    {
        int n = 0;
        for (const auto& k : strikeKit)
            n += k.load() != nullptr ? 1 : 0;
        return n;
    }

    uint32_t KitEngine::nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    void KitEngine::release (int index) noexcept
    {
        auto& s = strikes[(size_t) index];
        s.numActive = 0;
        s.piece = -1;
        s.note = -1;
        s.fading = false;
        for (auto& v : s.voices)
            dropVoice (v);
        strikeKit[(size_t) index].store (nullptr);
    }

    void KitEngine::dropVoice (MicVoice& v) noexcept
    {
        if (v.stream >= 0 && streamer != nullptr)
            streamer->release (v.stream);
        v.stream = -1;
        v.clip = nullptr;
    }

    void KitEngine::fadeStrike (Strike& s, int frames) noexcept
    {
        for (auto& v : s.voices)
            if (v.clip != nullptr && v.fadeLeft < 0)
            {
                v.fadeLeft = frames;
                v.fadeTotal = frames;
                v.wantStream = false;       // голос затихает: хвост ему уже не нужен
            }
        s.fading = true;
    }

    // Ячейки отдаёт только затухающий удар: они освободятся, когда голоса дойдут до конца затухания (dropVoice)
    int KitEngine::stealStreams (int needed, int exclude) noexcept
    {
        int freed = 0;
        while (freed < needed)
        {
            int best = -1, bestCount = 0;
            for (int i = 0; i < kMaxStrikes; ++i)
            {
                if (i == exclude || strikeKit[(size_t) i].load() == nullptr || strikes[(size_t) i].fading)
                    continue;
                int count = 0;
                for (const auto& v : strikes[(size_t) i].voices)
                    count += (v.clip != nullptr && v.stream >= 0 && v.fadeLeft < 0) ? 1 : 0;
                if (count > 0 && (best < 0 || strikes[(size_t) i].age < strikes[(size_t) best].age))
                {
                    best = i;
                    bestCount = count;
                }
            }
            if (best < 0)
                break;
            fadeStrike (strikes[(size_t) best], stealFade);
            steals.fetch_add (1, std::memory_order_relaxed);
            freed += bestCount;
        }
        return freed;
    }

    // Мало свободных мест под удары: старейший удар заранее уходит в затухание, чтобы не вытеснять жёстко
    void KitEngine::softEvictStrike() noexcept
    {
        int free = 0, fading = 0, oldest = -1;
        for (int i = 0; i < kMaxStrikes; ++i)
        {
            if (strikeKit[(size_t) i].load() == nullptr)
                ++free;
            else if (strikes[(size_t) i].fading)
                ++fading;
            else if (oldest < 0 || strikes[(size_t) i].age < strikes[(size_t) oldest].age)
                oldest = i;
        }
        if (free + fading < kSoftFreeStrikes && oldest >= 0)
        {
            fadeStrike (strikes[(size_t) oldest], stealFade);
            steals.fetch_add (1, std::memory_order_relaxed);
        }
    }

    // Офлайн-рендер: ждёт хвост (или свободную ячейку) вместо затухания. Возвращает, сколько кадров стало доступно.
    // Предел: 5 с на голос и общий offlineBudgetMs на вызов render()
    int KitEngine::waitForStream (MicVoice& v, const Kit* kit, double target) noexcept
    {
        const auto begin = juce::Time::getMillisecondCounter();
        int limit = offlineGaveUp ? 0 : std::max (0, std::min (v.stream >= 0 ? 5000 : 250, offlineBudgetMs));
        const auto availableNow = [&]() -> int
        {
            return v.stream >= 0 ? (int) streamer->available (v.stream) : v.clip->headFrames();
        };

        int available = availableNow();
        bool failed = false;
        while ((double) available < target && (int) (juce::Time::getMillisecondCounter() - begin) < limit)
        {
            if (v.stream < 0)
            {
                v.stream = streamer->acquire (*v.clip, kit, false);
                if (v.stream >= 0)
                {
                    v.wantStream = false;
                    // Ячейка получена: открытие файла и первая порция получают полный предел 5 с (в рамках общего бюджета)
                    const int elapsed = (int) (juce::Time::getMillisecondCounter() - begin);
                    limit = std::max (limit, std::min (elapsed + 5000, offlineBudgetMs));
                }
            }
            else if (streamer->hasFailed (v.stream))
            {
                failed = true;
                break;
            }
            streamer->kick();
            std::this_thread::sleep_for (std::chrono::microseconds (200));
            available = availableNow();
        }

        const int spent = (int) (juce::Time::getMillisecondCounter() - begin);
        offlineBudgetMs = std::max (0, offlineBudgetMs - spent);
        if ((double) available < target && ! failed)
            offlineTimeouts.fetch_add (1, std::memory_order_relaxed);
        if ((double) available < target && offlineBudgetMs == 0)
            offlineGaveUp = true;           // бюджет блока исчерпан, а данных нет: диск завис, до конца экспорта не ждём
        return available;
    }

    int KitEngine::selectHit (const Kit& kit, const NoteMap& map, int note, int velocity) noexcept
    {
        const auto& piece = kit.pieces[(size_t) map.piece];

        if (isKontakt())
        {
            // Точное окно velocity; пересечения окон разрешаются первым подходящим ударом
            for (const auto& l : map.layers)
                for (int h : l.hits)
                {
                    const auto& hit = piece.hits[(size_t) h];
                    if (velocity >= hit.velLo && velocity <= hit.velHi)
                        return h;
                }
            return -1;
        }

        // В live-режиме пустое значение velocity всегда молчит. Даже если окно слоя включает
        // значение, после исключения unverified ударов оно может остаться без файла.
        const int numLayers = (int) map.layers.size();
        int chosen = -1;
        for (int i = 0; i < numLayers; ++i)
        {
            const auto& l = map.layers[(size_t) i];
            if (velocity < l.velLo || velocity > l.velHi)
                continue;
            for (int h : l.hits)
            {
                const auto& hit = piece.hits[(size_t) h];
                if (velocity >= hit.velLo && velocity <= hit.velHi)
                {
                    chosen = i;
                    break;
                }
            }
            if (chosen >= 0)
                break;
        }
        if (chosen < 0)
            return -1;

        const auto& layer = map.layers[(size_t) chosen];
        Pick pick = layer.pick;
        if (layer.choice >= 0 && layer.choice < Kit::kMaxChoices)
            if (const int p = choicePick[(size_t) layer.choice].load(); p >= 0)
                pick = (Pick) p;

        // Повторяемый выбор: запись зависит только от позиции в песне, клавиши и слоя (без состояния)
        if (pick != Pick::Exact && strikePosValid && repeatable.load() && ! layer.hits.empty())
        {
            uint64_t x = (uint64_t) strikePos * 0x9e3779b97f4a7c15ull ^ ((uint64_t) note << 32) ^ ((uint64_t) chosen << 48)
                         ^ ((uint64_t) samePosCount << 52);
            x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
            x ^= x >> 27; x *= 0x94d049bb133111ebull;
            x ^= x >> 31;
            return layer.hits[(size_t) (x % (uint64_t) layer.hits.size())];
        }

        if (pick == Pick::Cycle)
        {
            if (layer.hits.empty())
                return -1;
            auto& pos = cyclePos[(size_t) note][(size_t) std::min (chosen, kMaxCycleLayers - 1)];
            const int h = layer.hits[(size_t) (pos % (uint32_t) layer.hits.size())];
            pos = (pos + 1) % (uint32_t) layer.hits.size();
            return h;
        }

        if (pick == Pick::Exact)
        {
            for (int h : map.layers[(size_t) chosen].hits)
            {
                const auto& hit = piece.hits[(size_t) h];
                if (velocity >= hit.velLo && velocity <= hit.velHi)
                    return h;
            }
            return -1;
        }

        // Случайный удар только из выбранного слоя, без повтора подряд.
        const int previous = lastHit[(size_t) note];
        int total = 0;
        bool hasPrevious = false;
        for (int h : layer.hits)
        {
            ++total;
            hasPrevious = hasPrevious || h == previous;
        }

        const int candidates = hasPrevious && total > 1 ? total - 1 : total;
        if (candidates <= 0)
            return -1;

        int k = (int) (nextRandom() % (uint32_t) candidates);
        for (int h : layer.hits)
        {
            if (hasPrevious && total > 1 && h == previous)
                continue;
            if (k-- == 0)
                return h;
        }
        return -1;
    }

    void KitEngine::noteOff (int note, int channel) noexcept
    {
        int oldest = -1;
        for (int i = 0; i < kMaxStrikes; ++i)
        {
            const auto& s = strikes[(size_t) i];
            if (strikeKit[(size_t) i].load() != nullptr && s.gated && s.held && ! s.fading && s.note == note && s.channel == channel
                && (oldest < 0 || s.age < strikes[(size_t) oldest].age))
                oldest = i;
        }
        if (oldest >= 0)
        {
            strikes[(size_t) oldest].held = false;
            fadeStrike (strikes[(size_t) oldest], gateFade);
        }
    }

    void KitEngine::chokeMetal() noexcept
    {
        for (int i = 0; i < kMaxStrikes; ++i)
        {
            const Kit* other = strikeKit[(size_t) i].load();
            if (other != nullptr && ! strikes[(size_t) i].fading && other->pieces[(size_t) strikes[(size_t) i].piece].metal)
                fadeStrike (strikes[(size_t) i], stealFade);
        }
    }

    KitEngine::StrikeInfo KitEngine::noteOn (int note, int velocity, int channel) noexcept
    {
        StrikeInfo info;
        const Kit* kit = inBlock ? blockKit : current.load();   // в блоке только защищённый кит
        if (kit == nullptr || note < 0 || note > 127)
            return info;

        if (note == kit->metalChokeKey)
        {
            chokeMetal();       // управляющая нота: звука нет
            return info;
        }

        kontaktNow = partMode.load() || selectMode.load() == (int) HitSelect::Kontakt;
        if (strikePosValid && strikePos == lastStrikePos)
            ++samePosCount;
        else
            samePosCount = 0;
        lastStrikePos = strikePosValid ? strikePos : -1;

        if (kit->serial.value != lastKitSerial)
        {
            lastKitSerial = kit->serial.value;
            lastHit.fill (-1);
            for (auto& c : cyclePos)
                c.fill (0);
        }

        velocity = std::clamp (velocity, 1, 127);
        const auto& map = kit->notes[(size_t) note];
        if (map.piece < 0)
            return info;

        const int playedVelocity = velocity;
        const int h = selectHit (*kit, map, note, velocity);
        if (h < 0)
            return info;

        const auto& piece = kit->pieces[(size_t) map.piece];
        const auto& hit = piece.hits[(size_t) h];
        lastHit[(size_t) note] = h;
        info.piece = map.piece;
        info.hit = h;
        info.layer = hit.layer;

        // Choke: барабан этой группы заглушает остальные барабаны группы
        if (piece.choke >= 0)
            for (int i = 0; i < kMaxStrikes; ++i)
            {
                // Звучащий удар мог начаться в прежнем ките (после замены): группа сверяется по его собственным барабанам
                const Kit* other = strikeKit[(size_t) i].load();
                if (other == nullptr)
                    continue;
                const bool sameKit = other == kit;
                if ((! sameKit || strikes[(size_t) i].piece != map.piece)
                    && other->pieces[(size_t) strikes[(size_t) i].piece].choke == piece.choke)
                    fadeStrike (strikes[(size_t) i], stealFade);
            }

        // Мест под удары почти не осталось: старейший удар мягко затухает заранее
        softEvictStrike();

        // Свободное место или, в крайнем случае, жёсткое вытеснение (предпочтительно уже затухающего, затем самого старого)
        int slot = -1;
        for (int i = 0; i < kMaxStrikes; ++i)
            if (strikeKit[(size_t) i].load() == nullptr)
            {
                slot = i;
                break;
            }
        if (slot < 0)
        {
            for (int pass = 0; pass < 2 && slot < 0; ++pass)
            {
                for (int i = 0; i < kMaxStrikes; ++i)
                    if ((pass == 1 || strikes[(size_t) i].fading) && (slot < 0 || strikes[(size_t) i].age < strikes[(size_t) slot].age))
                        slot = i;
            }
            hardSteals.fetch_add (1, std::memory_order_relaxed);
        }

        auto& s = strikes[(size_t) slot];
        float velGain = kit->velocityGain == VelocityGain::Linear ? (float) playedVelocity / 127.0f : 1.0f;
        if (piece.slot >= 0 && piece.slot < (int) placeGain.size())
            velGain *= placeGain[(size_t) piece.slot].load (std::memory_order_relaxed);
        int started = 0, missing = 0;
        for (int m = 0; m < (int) hit.files.size(); ++m)
        {
            auto& v = s.voices[(size_t) m];
            dropVoice (v);
            const auto& file = hit.files[(size_t) m];
            if (file.clip == nullptr || file.clip->frames() < 2)
                continue;

            v.clip = file.clip.get();
            v.position = 0.0;
            v.step = file.clip->sampleRate / sr;
            v.gain = velGain * juce::Decibels::decibelsToGain (file.gainDb, -100.0f);
            v.mic = m;
            {
                const int over = micOut[(size_t) m].load();
                v.bus = std::clamp (over >= 0 ? over : kit->mics[(size_t) m].out, 0, kNumOutputs);
            }
            v.fadeLeft = -1;
            v.fadeTotal = fadeLength;
            v.wantStream = false;
            v.stealWait = 0;
            v.underrunCounted = false;
            if (file.clip->isStreamed() && streamer != nullptr)
            {
                v.stream = streamer->acquire (*file.clip, kit);
                if (v.stream < 0)
                {
                    // Ячейки нет: голос играет голову и повторяет захват на каждом куске рендера, а старый удар уступает ячейки
                    v.wantStream = true;
                    v.stealWait = std::max (1, (int) (0.03 * sr));
                    ++missing;
                }
            }
            ++started;
        }
        // Не вытесняем, пока поток чтения может вырасти (отложенный захват сам получит ячейку через миллисекунды),
        // (в офлайне рендер ждёт ячейку, но если предел достигнут, ждать бесполезно: освободить ячейку некому)
        if (missing > 0 && ! streamer->canGrow())
            stealStreams (missing, slot);
        for (int m = (int) hit.files.size(); m < Kit::kMaxMics; ++m)
            dropVoice (s.voices[(size_t) m]);

        info.voices = started;
        if (started == 0)
        {
            release (slot);
            return info;
        }

        s.piece = map.piece;
        s.note = note;
        s.channel = channel;
        s.gated = hit.gate;
        s.held = hit.gate;
        s.numActive = started;
        s.fading = false;
        s.age = ++ageCounter;
        strikeKit[(size_t) slot].store (kit);
        return info;
    }

    void KitEngine::render (const std::array<float*, kNumOutputs + 1>& outL, const std::array<float*, kNumOutputs + 1>& outR,
                            int start, int end) noexcept
    {
        if (! inBlock)
            offlineBudgetMs = offlineBudgetMax;        // вне блока (тесты) бюджет на вызов; в блоке его задаёт beginBlock
        for (int from = start; from < end;)
        {
            int to = std::min (end, from + kRenderChunk);
            if (rampLen > 0 && from < rampLen && to > rampLen)
                to = rampLen;               // рампа кончается на границе куска: дальше уровень стоит на цели
            renderChunk (outL, outR, from, to);
            from = to;
        }
    }

    void KitEngine::renderChunk (const std::array<float*, kNumOutputs + 1>& outL, const std::array<float*, kNumOutputs + 1>& outR,
                                 int start, int end) noexcept
    {
        // Вне блока (тесты, прямой render) рампы нет: берём целевые значения. В блоке: линейно с постоянным шагом на отсчёт
        const float len = (float) (end - start);
        // start: позиция в блоке. Рампа идёт только первые rampLen отсчётов блока, дальше шаг нулевой
        const float rampPart = rampLen > 0 ? (float) std::clamp (rampLen - start, 0, end - start) : 0.0f;
        std::array<float, Kit::kMaxMics> lvl0, lvl1, pan0, pan1;
        for (int m = 0; m < Kit::kMaxMics; ++m)
        {
            if (rampLen == 0)
            {
                curLevel[(size_t) m] = micLevel[(size_t) m].load();
                curPan[(size_t) m] = micPan[(size_t) m].load();
            }
            lvl0[(size_t) m] = curLevel[(size_t) m];
            pan0[(size_t) m] = curPan[(size_t) m];
            lvl1[(size_t) m] = lvl0[(size_t) m] + stepLevel[(size_t) m] * rampPart;
            pan1[(size_t) m] = pan0[(size_t) m] + stepPan[(size_t) m] * rampPart;
        }

        for (int si = 0; si < kMaxStrikes; ++si)
        {
            const Kit* kit = strikeKit[(size_t) si].load();
            if (kit == nullptr)
                continue;

            auto& s = strikes[(size_t) si];
            for (auto& v : s.voices)
            {
                if (v.clip == nullptr)
                    continue;

                const int bus = v.bus;
                float* left = outL[(size_t) bus];
                float* right = outR[(size_t) bus];
                const bool mono = right == nullptr;

                // Панорама как баланс: центр не меняет уровень, крайнее положение глушит противоположный канал.
                // Гейн каждого канала линейно идёт от значения в начале куска к значению в конце (делений в цикле по отсчётам нет)
                const size_t mi = (size_t) v.mic;
                const float g0 = v.gain * lvl0[mi], g1 = v.gain * lvl1[mi];
                const float gainL = mono ? g0 : g0 * std::min (1.0f, 1.0f - pan0[mi]);
                const float gainR = mono ? 0.0f : g0 * std::min (1.0f, 1.0f + pan0[mi]);
                const float endL = mono ? g1 : g1 * std::min (1.0f, 1.0f - pan1[mi]);
                const float endR = mono ? 0.0f : g1 * std::min (1.0f, 1.0f + pan1[mi]);
                const float dL = (endL - gainL) / std::max (1.0f, len);
                const float dR = (endR - gainR) / std::max (1.0f, len);

                ClipView view;
                view.head = v.clip->f32.data();
                view.headFrames = v.clip->headFrames();
                view.frames = v.clip->frames();
                view.available = view.frames;
                if (v.clip->isStreamed())
                {
                    // Отложенный захват: ячейки не было при ударе. Хвост читается с головы, поэтому начать поздно можно
                    if (v.stream < 0 && v.wantStream && streamer != nullptr)
                    {
                        v.stream = streamer->acquire (*v.clip, kit, false);
                        if (v.stream >= 0)
                            v.wantStream = false;
                        else if ((v.stealWait -= end - start) <= 0 && ! offline.load (std::memory_order_relaxed))
                        {
                            stealStreams (1, si);       // раз в 30 мс, пока ячейки нет
                            v.stealWait = std::max (1, (int) (0.03 * sr));
                        }
                    }

                    view.available = v.stream >= 0 ? (int) streamer->available (v.stream) : view.headFrames;
                    view.ring = v.stream >= 0 ? streamer->ring (v.stream) : nullptr;

                    // Хвост не успевает: затухание 5 мс, пока данные ещё есть (запас на блок и само затухание). Офлайн-рендер
                    // вместо этого ждёт диск, иначе в экспорте будут дыры
                    // Уже затухающему голосу (steal/choke, недогрузка) нужно только то, что останется до конца затухания
                    const bool fading = v.fadeLeft >= 0;
                    const int span = fading ? std::min (end - start, v.fadeLeft + 1) : end - start + fadeLength;
                    const double need = v.position + (double) span * v.step + 4.0;
                    if (need > (double) view.available && view.available < view.frames)
                    {
                        if (streamer != nullptr && offline.load (std::memory_order_relaxed)
                            && (v.stream >= 0 ? ! streamer->hasFailed (v.stream) : v.wantStream))
                        {
                            view.available = waitForStream (v, kit, std::min (need, (double) view.frames));
                            view.ring = v.stream >= 0 ? streamer->ring (v.stream) : nullptr;
                        }
                        if (need > (double) view.available)
                        {
                            // Настоящий обрыв: диск не успел или ячейка не нашлась до конца головы
                            if (fading)
                            {
                                // Огибающая должна закончиться на имеющихся данных: укорачиваем остаток затухания,
                                // сохраняя текущий уровень (fadeLeft / fadeTotal), наклон становится круче
                                const int fit = std::max (0, (int) ((view.available - 5.0 - v.position) / v.step));
                                if (fit < v.fadeLeft)
                                {
                                    v.fadeTotal = std::max (1, (int) ((double) fit * v.fadeTotal / std::max (1, v.fadeLeft)));
                                    v.fadeLeft = fit;
                                }
                            }
                            else
                            {
                                v.fadeLeft = fadeLength;
                                v.fadeTotal = fadeLength;
                            }
                            v.wantStream = false;
                            if (streamer != nullptr && ! v.underrunCounted)
                                streamer->underruns.fetch_add (1, std::memory_order_relaxed);
                            v.underrunCounted = true;
                        }
                    }
                }

                bool alive;
                alive = mono ? renderVoice<true> (view, v.position, v.step, v.fadeLeft, v.fadeTotal, left, right, gainL, gainR, dL, dR, start, end)
                             : renderVoice<false> (view, v.position, v.step, v.fadeLeft, v.fadeTotal, left, right, gainL, gainR, dL, dR, start, end);

                if (alive && v.stream >= 0)
                    streamer->setConsumed (v.stream, std::max<int64_t> (0, (int64_t) v.position - 2));

                if (! alive)
                {
                    dropVoice (v);
                    --s.numActive;
                }
            }

            if (s.numActive <= 0)
                release (si);
        }

        for (int m = 0; m < Kit::kMaxMics; ++m)
        {
            curLevel[(size_t) m] = lvl1[(size_t) m];
            curPan[(size_t) m] = pan1[(size_t) m];
        }
    }

    void KitEngine::publishActivity() noexcept
    {
        uint64_t lo = 0, hi = 0;
        for (int i = 0; i < kMaxStrikes; ++i)
        {
            if (strikeKit[(size_t) i].load() == nullptr)
                continue;
            const int note = strikes[(size_t) i].note;
            if (note < 0)
                continue;
            if (note < 64)
                lo |= 1ull << note;
            else
                hi |= 1ull << (note - 64);
        }
        activeLo.store (lo);
        activeHi.store (hi);
    }
}
