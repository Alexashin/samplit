// samplit-snapshot <папка>: собирает во временной папке тестовый пакет на 15 микрофонов, пресет и токен
// (подписан ТЕСТОВЫМ ключом, и проверяет его тоже тестовый ключ, не зашитый в плагин), открывает окно плагина без показа
// на экране и сохраняет его снимки в разных состояниях. Звук не нужен, сеть не нужна.
//
// samplit-snapshot <папка> --installed [id пресета]: то же на настоящих данных этого компьютера (%APPDATA%\Samplit, боевой ключ):
// загружает пресет studio600-default, печатает время загрузки и объём в памяти, сохраняет снимок.

#include "PluginEditor.h"
#include "samplit/core/LicenseSigning.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

using namespace samplit;
using samplit::pack::operator""_s;

namespace
{
    bool pumpUntil (const std::function<bool()>& done, int timeoutMs = 15000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        return done();
    }

    struct MicDef { const char* id; const char* name; int out; };

    const MicDef kMics[] = {
        { "kick_in", "Kick In", 1 },   { "kick_out", "Kick Out", 2 }, { "sn_top", "Snare Top", 3 },
        { "sn_bot", "Snare Bottom", 4 }, { "oh_1", "Overhead L", 5 }, { "oh_2", "Overhead R", 6 },
        { "room_1", "Room L", 7 },     { "room_2", "Room R", 8 },     { "room_mid", "Room Mid", 9 },
        { "hat", "Hi-Hat", 10 },       { "ride", "Ride", 11 },        { "tom_a1", "Tom 1", 12 },
        { "tom_a2", "Tom 2", 13 },     { "tom_a3", "Tom 3", 14 },     { "tom_a4", "Tom 4", 15 }
    };

    struct PieceDef { const char* id; const char* name; std::vector<int> keys; };

    bool writePack (const juce::File& file, const pack::PackKey& key)
    {
        juce::String error;
        auto w = pack::PackWriter::create (file, "studio600-core", key, error);
        if (w == nullptr)
            return false;

        pack::DecodedAudio a;
        a.sampleRate = 44100.0;
        a.frames = 4410;
        a.samples.resize ((size_t) a.frames);
        for (int i = 0; i < a.frames; ++i)
            a.samples[(size_t) i] = (int32_t) (std::sin (i * 0.05) * std::exp (-i / 800.0) * 4000000.0);
        if (w->addAudio (a, error) != 0)
            return false;

        pack::Manifest m;
        m.id = "studio600-core";
        m.title = "Studio600: core"_s;
        for (const auto& mic : kMics)
            m.mics.push_back ({ mic.id, mic.name, "close", mic.out });

        const std::vector<PieceDef> pieces = {
            { "kick", "Бочка", { 36 } },   { "tom_10", "Том 10\"", { 48 } }, { "tom_14", "Том 14\"", { 45 } },
            { "tom_16m", "Том 16\"", { 43 } }, { "hat", "Хай-хэт", { 42, 44, 46 } }, { "ride", "Райд", { 51, 53 } },
            { "crash_a", "Крэш A", { 49 } }, { "crash_b", "Крэш B", { 57 } }
        };
        for (const auto& pd : pieces)
        {
            pack::Piece p;
            p.id = pd.id;
            p.name = juce::String::fromUTF8 (pd.name);
            p.keys = pd.keys;
            p.layers = { { "all", "center", 1, 127 } };
            for (int k : pd.keys)
            {
                pack::Hit h;
                h.id = p.id + juce::String (k);
                h.layer = "all";
                h.key = k;
                for (const auto& mic : kMics)
                {
                    pack::HitFile f;
                    f.chunk = 0;
                    f.frames = a.frames;
                    f.rate = a.sampleRate;
                    f.peak = 4000000;
                    h.files[mic.id] = f;
                }
                p.hits.push_back (h);
            }
            m.pieces.push_back (p);
        }
        return w->finish (m, error);
    }

    // Тестовый ключ подписи (сид 00 01 .. 1f, как в tests/): боевой ключ здесь не нужен
    std::array<uint8_t, 64> testSecret (license::Key32& pub)
    {
        license::Key32 seed {};
        for (size_t i = 0; i < seed.size(); ++i)
            seed[i] = (uint8_t) i;
        return license::secretKeyFromSeed (seed, pub);
    }

    void writeToken (const juce::File& file, const pack::PackKey& key, const juce::String& fingerprint)
    {
        license::Key32 pub;
        const auto secret = testSecret (pub);

        std::array<uint8_t, 24> nonce {};
        pack::fillRandom (nonce.data(), nonce.size());
        auto* packs = new juce::DynamicObject();
        packs->setProperty ("studio600-core", license::wrapPackKey ("studio600-core", key, fingerprint, nonce));

        const auto now = juce::Time::currentTimeMillis() / 1000;
        auto* p = new juce::DynamicObject();
        p->setProperty ("licenseId", "L-DEMO");
        p->setProperty ("user", juce::String::fromUTF8 ("ВГТУ, стенд"));
        p->setProperty ("machine", license::machineId (fingerprint));
        p->setProperty ("notBefore", (juce::int64) (now - 86400));
        p->setProperty ("expires", (juce::int64) (now + 30 * 86400));
        p->setProperty ("packs", juce::var (packs));
        file.replaceWithText (license::signToken (juce::var (p), secret));
    }

    // tab: схема установки или микшер; over: список пресетов или лицензия, открытые поверх окна
    enum class Tab { Kit, Mixer };
    enum class Over { None, Presets, License };

    bool shoot (SamplitProcessor& processor, const juce::File& out, int w, int h, Tab tab = Tab::Kit, Over over = Over::None)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto& samplitEditor = static_cast<SamplitEditor&> (*editor);
        samplitEditor.showMainTab (tab == Tab::Mixer);
        editor->setSize (w, h);
        if (over == Over::Presets)
            samplitEditor.openPresets();
        else if (over == Over::License)
            samplitEditor.openLicense();
        pumpUntil ([] { return false; }, 300);  // таймеры панелей успевают обновиться
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
        std::cout << (ok ? "saved " : "FAILED ") << out.getFullPathName() << std::endl;
        return ok;
    }

    std::unique_ptr<LicenseManager> manager (const juce::File& dataDir, const juce::String& fingerprint)
    {
        LicenseManager::Config c;
        c.enforce = true;       // снимки показывают экран лицензии
        c.dataDir = dataDir;
        testSecret (c.publicKey);
        c.fingerprint = fingerprint;
        auto m = std::make_unique<LicenseManager> (c);
        pumpUntil ([&] { return ! m->getInfo().busy; });
        return m;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cout << "usage: samplit-snapshot <output folder>" << std::endl;
        return 2;
    }
    const juce::File outDir (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])));
    outDir.createDirectory();

    if (argc > 2 && juce::String (argv[2]) == "--installed")
    {
        SamplitProcessor processor;
        processor.prepareToPlay (48000.0, 512);
        auto m = std::make_unique<LicenseManager> (LicenseManager::defaultConfig());
        pumpUntil ([&] { return ! m->getInfo().busy; });
        std::cout << "license valid: " << (m->getInfo().isValid() ? "yes" : "no") << std::endl;
        processor.setLicenseManager (std::move (m));

        const juce::String wanted = argc > 3 ? juce::String (argv[3]) : juce::String ("studio600-default");
        bool loaded = false, ok = false;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        for (const auto& e : processor.scanPresets())
        {
            std::cout << "preset " << e.preset.id << (e.isComplete() ? " complete" : e.isPlayable() ? " partial" : " silent") << std::endl;
            if (e.preset.id == wanted)
                processor.selectPreset (e.preset, [&] (const KitLoadResult& r)
                {
                    loaded = true;
                    ok = r.ok();
                    std::cout << (ok ? "loaded" : "FAILED: ") << r.error << ", " << (r.memoryBytes >> 20) << " MB, "
                              << r.warnings.size() << " warnings, " << r.missingPacks.size() << " missing packs" << std::endl;
                    for (const auto& w : r.warnings)
                        std::cout << "  " << w << std::endl;
                });
        }
        if (! pumpUntil ([&] { return loaded; }, 600000))
        {
            std::cout << "load timeout" << std::endl;
            return 1;
        }
        std::cout << "load time " << juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0) << " s" << std::endl;
        if (const auto kit = processor.getKit())
        {
            int hits = 0;
            for (const auto& piece : kit->pieces)
                hits += (int) piece.hits.size();
            std::cout << kit->pieces.size() << " pieces, " << hits << " hits, " << kit->mics.size() << " mics" << std::endl;
        }
        return ok && shoot (processor, outDir.getChildFile ("06-installed.png"), 1180, 780) ? 0 : 1;
    }

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-snapshot");
    root.deleteRecursively();
    const auto withLicense = root.getChildFile ("licensed");
    const auto noLicense = root.getChildFile ("unlicensed");
    const auto fingerprint = juce::String ("snapshot:machine");

    const auto key = pack::generateKey();
    const auto presetSource = juce::File::getCurrentWorkingDirectory().getChildFile ("tools/presets/studio600-default.json");
    for (const auto& dir : { withLicense, noLicense })
    {
        dir.getChildFile ("packs").createDirectory();
        dir.getChildFile ("presets").createDirectory();
        if (! writePack (dir.getChildFile ("packs/studio600-core.splpack"), key))
        {
            std::cout << "cannot write pack" << std::endl;
            return 1;
        }
        presetSource.copyFileTo (dir.getChildFile ("presets/studio600-default.json"));
        presetSource.getParentDirectory().getChildFile ("mixes").copyDirectoryTo (dir.getChildFile ("presets/mixes"));
        dir.getChildFile ("presets/studio600-full.json").replaceWithText (
            R"json({ "format": 1, "id": "studio600-full", "name": "Studio600: Full (все пакеты)", "packs": ["studio600-core", "studio600-snare-live", "studio600-hat", "studio600-ride", "studio600-cymbals", "studio600-unverified"], "includeUnverified": true })json"_s);
    }
    writeToken (withLicense.getChildFile ("license.token"), key, fingerprint);

    int failures = 0;

    // 1. Нет лицензии: пресеты видны, пакеты закрыты, поле активации
    {
        SamplitProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        processor.setLicenseManager (manager (noLicense, fingerprint));
        failures += shoot (processor, outDir.getChildFile ("01-no-license.png"), 1180, 780) ? 0 : 1;
    }

    // 2-4. С лицензией: до выбора пресета, с загруженным китом, в узком окне
    {
        SamplitProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        processor.setLicenseManager (manager (withLicense, fingerprint));
        failures += shoot (processor, outDir.getChildFile ("02-licensed-empty.png"), 1180, 780) ? 0 : 1;

        const auto presets = processor.scanPresets();
        bool loaded = false;
        for (const auto& e : presets)
            if (e.preset.id == "studio600-default")
                processor.selectPreset (e.preset, [&] (const KitLoadResult& r)
                {
                    loaded = true;
                    if (! r.ok())
                        std::cout << "load failed: " << r.error << std::endl;
                });
        pumpUntil ([&] { return loaded; });

        auto m = processor.getMicState (3);
        m.mute = true;
        processor.setMicState (3, m);
        failures += shoot (processor, outDir.getChildFile ("03-kit.png"), 1180, 780) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("04-kit-narrow.png"), 900, 620) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("05-kit-wide.png"), 1600, 900) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("07-mixer.png"), 1180, 780, Tab::Mixer) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("08-mixer-narrow.png"), 900, 620, Tab::Mixer) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("09-presets.png"), 1180, 780, Tab::Kit, Over::Presets) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("10-license.png"), 1180, 780, Tab::Kit, Over::License) ? 0 : 1;
    }

    root.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
