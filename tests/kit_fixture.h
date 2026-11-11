#pragma once

// Общие заготовки тестов: сборка кита по частям и процессор с китом из трёх микрофонов

#include "KitEngine.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace kittest
{
    using namespace samplit;

    constexpr double kRate = 48000.0;
    constexpr int kFixtureBlock = 512;

    inline ClipPtr constClip (float level, int frames = 48000, double rate = kRate)
    {
        return DecodedClip::fromFloat (std::vector<float> ((size_t) frames, level), rate);
    }

    // Собирает кит по частям. Уровень файла хит-а задаёт его «метку»: по уровню на выходе видно, какой удар сыграл
    struct KitBuilder
    {
        std::shared_ptr<Kit> kit = std::make_shared<Kit>();

        int mic (const std::string& id, int out)
        {
            kit->mics.push_back ({ id, id, "close", out });
            return (int) kit->mics.size() - 1;
        }

        int piece (const std::string& id, int choke = -1)
        {
            Piece p;
            p.id = id;
            p.name = id;
            p.choke = choke;
            kit->pieces.push_back (p);
            return (int) kit->pieces.size() - 1;
        }

        int layer (int piece, int velLo, int velHi)
        {
            auto& p = kit->pieces[(size_t) piece];
            p.layers.push_back ({ "l" + std::to_string (p.layers.size()), "", velLo, velHi });
            return (int) p.layers.size() - 1;
        }

        // levels: уровень файла по микрофонам, отрицательный = на этом микрофоне файла нет
        int hit (int piece, int layer, int key, std::vector<float> levels, int velLo = 0, int velHi = 0)
        {
            auto& p = kit->pieces[(size_t) piece];
            Hit h;
            h.id = "h" + std::to_string (p.hits.size());
            h.layer = layer;
            h.key = key;
            h.velLo = velLo != 0 ? velLo : p.layers[(size_t) layer].velLo;
            h.velHi = velHi != 0 ? velHi : p.layers[(size_t) layer].velHi;
            for (float level : levels)
                h.files.push_back ({ level < 0.0f ? nullptr : constClip (level), 0.0f });
            p.hits.push_back (h);
            return (int) p.hits.size() - 1;
        }

        std::shared_ptr<const Kit> done (HitSelect mode = HitSelect::Live,
                                         VelocityGain gain = VelocityGain::None)
        {
            kit->mode = mode;
            kit->velocityGain = gain;
            REQUIRE (kit->buildIndex());
            return kit;
        }
    };

    // Процессор с китом из трёх микрофонов: Out 1, Out 2 и Main. Кит ставит тест (setKit (kit)),
    // другую раскладку выходов даёт makeKit
    struct KitFixture
    {
        explicit KitFixture (std::initializer_list<int> enabledAuxBuses = {})
        {
            processor.setRateAndBufferSizeDetails (kRate, kFixtureBlock);
            for (int bus : enabledAuxBuses)
                REQUIRE (processor.getBus (false, bus)->enable (true));
            processor.prepareToPlay (kRate, kFixtureBlock);
            processor.setMultiOut (true);       // тесты проверяют раскладку по выходам
            kit = makeKit ({ 1, 2, 0 });
        }

        // Кит из одного барабана на ноте 36: три микрофона по 0,3, выходы микрофонов заданы
        static std::shared_ptr<const Kit> makeKit (std::array<int, 3> outs)
        {
            KitBuilder b;
            b.mic ("kick_in", outs[0]);
            b.mic ("kick_out", outs[1]);
            b.mic ("room", outs[2]);
            const int p = b.piece ("kick");
            const int l = b.layer (p, 1, 127);
            b.hit (p, l, 36, { 0.3f, 0.3f, 0.3f });
            return b.done();
        }

        juce::AudioBuffer<float> run (juce::MidiBuffer& midi)
        {
            juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), kFixtureBlock);
            processor.processBlock (buffer, midi);
            return buffer;
        }

        // Нота MIDI со смещением at внутри блока
        juce::AudioBuffer<float> play (int note, float velocity = 1.0f, int at = 0)
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), at);
            return run (midi);
        }

        static float peak (const juce::AudioBuffer<float>& b, int channel, int from = 0, int to = -1)
        {
            if (to < 0)
                to = b.getNumSamples();
            float p = 0.0f;
            for (int i = from; i < to; ++i)
                p = std::max (p, std::abs (b.getSample (channel, i)));
            return p;
        }

        SamplitProcessor processor;
        std::shared_ptr<const Kit> kit;
    };
}
