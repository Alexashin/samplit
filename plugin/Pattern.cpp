#include "Pattern.h"

#include <algorithm>
#include <cmath>

namespace samplit
{
    namespace
    {
        uint64_t mix (uint64_t x) noexcept
        {
            x += 0x9e3779b97f4a7c15ull;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
            return x ^ (x >> 31);
        }

        // Число из [-1, 1] по зерну, шагу, месту и номеру "канала" (время или сила)
        double unit (uint32_t seed, const PatternStep& s, int channel) noexcept
        {
            const uint64_t h = mix ((uint64_t) seed ^ ((uint64_t) s.step << 20) ^ ((uint64_t) s.slot << 40) ^ ((uint64_t) channel << 56));
            return (double) (h >> 11) / (double) (1ull << 52) - 1.0;       // 53 бита → [0,2) − 1
        }
    }

    void Pattern::clampToLimits()
    {
        bars = std::clamp (bars, 1, kMaxBars);
        bpm = std::clamp (bpm, 30.0, 300.0);
        humanTimingMs = std::clamp (humanTimingMs, 0, 40);
        humanVelocity = std::clamp (humanVelocity, 0, 20);
        const int n = numSteps();
        steps.erase (std::remove_if (steps.begin(), steps.end(),
                                     [n] (const PatternStep& s) { return s.step < 0 || s.step >= n || s.key < 0 || s.key > 127; }),
                     steps.end());
        for (auto& s : steps)
            s.velocity = std::clamp (s.velocity, 1, 127);
    }

    bool Pattern::setStep (int step, int slot, int key, int velocity)
    {
        if (step < 0 || step >= numSteps() || key < 0 || key > 127)
            return false;
        velocity = std::clamp (velocity, 1, 127);
        for (auto& s : steps)
            if (s.step == step && s.slot == slot)
            {
                s.key = key;
                s.velocity = velocity;
                return true;
            }
        steps.push_back ({ step, slot, key, velocity });
        return true;
    }

    bool Pattern::removeStep (int step, int slot)
    {
        const auto it = std::remove_if (steps.begin(), steps.end(), [&] (const PatternStep& s) { return s.step == step && s.slot == slot; });
        const bool removed = it != steps.end();
        steps.erase (it, steps.end());
        return removed;
    }

    const PatternStep* Pattern::findStep (int step, int slot) const noexcept
    {
        for (const auto& s : steps)
            if (s.step == step && s.slot == slot)
                return &s;
        return nullptr;
    }

    int64_t Pattern::lengthSamples (double sampleRate) const noexcept
    {
        return (int64_t) std::llround ((double) numSteps() * 0.25 * 60.0 / bpm * sampleRate);
    }

    void Pattern::jitterFor (const PatternStep& s, double& timeMs, int& vel) const noexcept
    {
        timeMs = humanTimingMs > 0 ? unit (humanSeed, s, 0) * humanTimingMs : 0.0;
        vel = humanVelocity > 0 ? (int) std::lround (unit (humanSeed, s, 1) * humanVelocity) : 0;
    }

    std::vector<PatternEvent> Pattern::buildEvents (double sampleRate) const
    {
        std::vector<PatternEvent> out;
        out.reserve (steps.size());
        const double samplesPerStep = 0.25 * 60.0 / bpm * sampleRate;
        for (const auto& s : steps)
        {
            double ms = 0.0;
            int dv = 0;
            jitterFor (s, ms, dv);
            const double t = std::max (0.0, (double) s.step * samplesPerStep + ms * 0.001 * sampleRate);
            out.push_back ({ (int64_t) std::llround (t), s.key, std::clamp (s.velocity + dv, 1, 127), s.slot });
        }
        std::stable_sort (out.begin(), out.end(), [] (const PatternEvent& a, const PatternEvent& b)
                          { return a.sample != b.sample ? a.sample < b.sample : a.key < b.key; });
        return out;
    }

    juce::MidiFile Pattern::toMidiFile() const
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::llround (60000000.0 / bpm)), 0.0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        const double ticksPerMs = (double) kPpq * bpm / 60000.0;
        for (const auto& s : steps)
        {
            double ms = 0.0;
            int dv = 0;
            jitterFor (s, ms, dv);
            const double t = std::max (0.0, (double) s.step * kTicksPerStep + ms * ticksPerMs);
            const int vel = std::clamp (s.velocity + dv, 1, 127);
            seq.addEvent (juce::MidiMessage::noteOn (10, s.key, (juce::uint8) vel), t);
            seq.addEvent (juce::MidiMessage::noteOff (10, s.key), t + kTicksPerStep / 2.0);
        }
        seq.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (kPpq);
        file.addTrack (seq);
        return file;
    }

    juce::String Pattern::toJson() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("version", 1);
        root->setProperty ("bars", bars);
        root->setProperty ("bpm", bpm);
        root->setProperty ("humanSeed", (juce::int64) humanSeed);
        root->setProperty ("humanTimingMs", humanTimingMs);
        root->setProperty ("humanVelocity", humanVelocity);
        juce::Array<juce::var> list;
        for (const auto& s : steps)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("step", s.step);
            o->setProperty ("slot", s.slot);
            o->setProperty ("key", s.key);
            o->setProperty ("velocity", s.velocity);
            list.add (juce::var (o));
        }
        root->setProperty ("steps", list);
        return juce::JSON::toString (juce::var (root), true);
    }

    bool Pattern::fromJson (const juce::String& text, Pattern& out, juce::String& error)
    {
        const auto v = juce::JSON::parse (text);
        if (! v.isObject() || (int) v.getProperty ("version", 0) != 1)
        {
            error = "Не партия Samplit или неизвестная версия";
            return false;
        }
        Pattern p;
        p.bars = (int) v.getProperty ("bars", 1);
        p.bpm = (double) v.getProperty ("bpm", 120.0);
        p.humanSeed = (uint32_t) (juce::int64) v.getProperty ("humanSeed", 1);
        p.humanTimingMs = (int) v.getProperty ("humanTimingMs", 0);
        p.humanVelocity = (int) v.getProperty ("humanVelocity", 0);
        if (const auto* arr = v.getProperty ("steps", {}).getArray())
            for (const auto& e : *arr)
                p.steps.push_back ({ (int) e.getProperty ("step", -1), (int) e.getProperty ("slot", 0),
                                     (int) e.getProperty ("key", -1), (int) e.getProperty ("velocity", 100) });
        p.clampToLimits();
        out = std::move (p);
        return true;
    }
}
