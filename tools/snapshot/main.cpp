// samplit-snapshot <папка>: собирает во временной папке тестовые паки заводской установки (15 микрофонов) и токен
// (подписан ТЕСТОВЫМ ключом, и проверяет его тоже тестовый ключ, не зашитый в плагин), открывает окно плагина без показа
// на экране и сохраняет его снимки в разных состояниях. Звук не нужен, сеть не нужна.
//
// samplit-snapshot <папка> --installed: то же на настоящих данных этого компьютера (%APPDATA%\Samplit, боевой ключ):
// применяет заводскую установку, печатает время загрузки и объём в памяти, сохраняет снимок.

#include "PatternPalette.h"
#include "PluginEditor.h"
#include "ui/Controls.h"
#include "ui/Fader.h"
#include "ui/Knob.h"
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

    // Пак заводской установки: барабаны, которые Setup::factory() ставит из него по умолчанию
    std::vector<juce::String> factoryPackIds()
    {
        std::vector<juce::String> ids;
        for (const auto& pl : kStudio600Places)
            if (std::find (ids.begin(), ids.end(), juce::String (pl.defaultPack)) == ids.end())
                ids.push_back (pl.defaultPack);
        return ids;
    }

    bool writePack (const juce::File& file, const juce::String& packId, const pack::PackKey& key)
    {
        juce::String error;
        auto w = pack::PackWriter::create (file, packId, key, error);
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
        m.id = packId;
        m.title = packId;
        for (const auto& mic : kMics)
            m.mics.push_back ({ mic.id, mic.name, "close", mic.out });

        for (const auto& pd : kStudio600Places)
        {
            if (packId != pd.defaultPack)
                continue;
            pack::Piece p;
            p.id = pd.defaultPiece;
            p.name = juce::String::fromUTF8 (pd.label);
            p.type = pd.type;
            // Барабаны одного пака (томы) записаны на одних клавишах: на место их сдвигает установка
            const bool tom = juce::String (pd.type) == "tom";
            const int lo = tom ? 96 : pd.keyLo, hi = tom ? 97 : pd.keyHi;
            for (int k = lo; k <= hi; ++k)
                p.keys.push_back (k);
            // Малый, хэт и томы: две группы слоёв, чтобы вкладка «Инструменты» показывала строки групп.
            // Сильный удар малого и томов парный (как крещендо Studio600): во вкладке «Партия» виден переключатель «вместе»
            const bool grouped = pd.slot == 1 || pd.slot == 6 || tom;
            p.layers = { { "all", "center", 1, 127 } };
            if (grouped)
            {
                p.layers = { { "hit", "center", 1, 74 }, { "rim", "rim", 75, 127 } };
                pack::Group g1, g2;
                g1.id = "hit"; g1.name = juce::String::fromUTF8 ("Основной удар");
                g1.hint = juce::String::fromUTF8 ("Тихие и средние удары по центру");
                g1.layers = { "hit" };
                g2.id = "rim"; g2.name = juce::String::fromUTF8 ("Сильный удар");
                g2.hint = juce::String::fromUTF8 ("Громкие акценты, играются по кругу");
                g2.layers = { "rim" };
                g2.pick = "cycle"; g2.picks = { "cycle" };
                if (pd.slot == 1)
                    g2.pair = { "tom", "", "rim" };
                else if (tom)
                    g2.pair = { "snare", "", "rim" };
                p.groups = { g1, g2 };
            }
            for (const auto& layer : p.layers)
            for (int k : p.keys)
            {
                pack::Hit h;
                h.id = p.id + juce::String (k) + layer.id;
                h.layer = layer.id;
                h.key = k;
                h.velLo = layer.velLo;
                h.velHi = layer.velHi;
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
        for (const auto& id : factoryPackIds())
            packs->setProperty (id, license::wrapPackKey (id, key, fingerprint, nonce));

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

    // tab: схема установки или микшер; over: лицензия, открытая поверх окна
    enum class Tab { Kit, Pattern, Mixer };
    enum class Over { None, License, Settings };

    // Пример партии для снимков вкладки «Партия»: бочка, малый, хэт по палитре загруженного кита
    void drawDemoPattern (SamplitProcessor& processor, int bars)
    {
        Pattern p;
        p.bars = bars;
        p.bpm = 96.0;
        if (const auto kit = processor.getKit())
            for (int bar = 0; bar < bars; ++bar)
            {
                auto put = [&] (int slot, int step, bool strongest)
                {
                    const auto pal = buildPalette (*kit, slot);
                    if (! pal.empty())
                    {
                        const auto& o = strongest ? pal.back() : pal.front();
                        p.setStep (bar * Pattern::kStepsPerBar + step, slot, o.key, o.velocity);
                    }
                };
                put (0, 0, true); put (0, 8, true); put (0, 10, true);
                put (1, 4, true); put (1, 12, true); put (1, 15, false);
                for (int s = 0; s < Pattern::kStepsPerBar; s += 2)
                    put (6, s, s % 4 == 0);
            }
        processor.setPattern (p);
    }

    bool shoot (SamplitProcessor& processor, const juce::File& out, int w, int h, Tab tab = Tab::Kit, Over over = Over::None,
               const char* place = nullptr, bool details = false)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto& samplitEditor = static_cast<SamplitEditor&> (*editor);
        samplitEditor.showMainTab (tab == Tab::Mixer ? SamplitEditor::MainTab::Mixer
                                   : tab == Tab::Pattern ? SamplitEditor::MainTab::Pattern : SamplitEditor::MainTab::Kit);
        if (place != nullptr)
            samplitEditor.selectInstrument (place);
        samplitEditor.openDrumDetails (details);
        editor->setSize (w, h);
        if (over == Over::License)
            samplitEditor.openLicense();
        if (over == Over::Settings)
            samplitEditor.openSettings();
        pumpUntil ([] { return false; }, 300);  // таймеры панелей успевают обновиться
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
        std::cout << (ok ? "saved " : "FAILED ") << out.getFullPathName() << std::endl;
        return ok;
    }


    // Галерея компонентов новой визуальной системы (00-components.png): крутилки, фейдер, кнопки, сегменты, раскрывашка
    struct Gallery final : juce::Component
    {
        Gallery()
        {
            setLookAndFeel (&laf);
            for (auto* k : { &k1, &k2, &k3, &k4 })
                addAndMakeVisible (*k);
            k1.setRange (-60, 6, 0.1, 0);  k1.setValue (-3.5);
            k1.format = [] (double v) { return juce::String (v, 1) + utf8 (" дБ"); };
            k2.setRange (-1, 1, 0.01, 0);  k2.setValue (0.4);
            k2.format = [] (double v) { return v == 0 ? utf8 ("центр") : juce::String (juce::roundToInt (std::abs (v) * 100)) + (v < 0 ? " L" : " R"); };
            k3.setRange (0, 40, 1, 8);     k3.setValue (14);
            k3.format = [] (double v) { return juce::String ((int) v) + utf8 (" мс"); };
            k4.setRange (0, 100, 1, 50);   k4.setValue (0);
            k4.format = [] (double v) { return juce::String ((int) v) + " %"; };
            for (auto* k : { &k1, &k2, &k3, &k4 })
                k->refresh();
            addAndMakeVisible (fader);   fader.setRange (-60, 6, 0);  fader.setValue (-6.0);
            addAndMakeVisible (fader2);  fader2.setRange (-60, 6, 0); fader2.setValue (0.0);
            addAndMakeVisible (led1);    led1.setToggleState (true, juce::dontSendNotification);
            addAndMakeVisible (led2);    addAndMakeVisible (led3);  led3.setEnabled (false);
            addAndMakeVisible (primary); primary.setColour (juce::TextButton::buttonColourId, brand::accent);
            addAndMakeVisible (normal);  addAndMakeVisible (quiet);
            quiet.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            quiet.setColour (juce::TextButton::textColourOffId, brand::textMuted);
            addAndMakeVisible (seg);     addAndMakeVisible (seg2); seg2.setSelected (1);
            addAndMakeVisible (disc);    disc.setToggleState (true, juce::dontSendNotification);
            addAndMakeVisible (combo);   combo.addItem (utf8 ("Studio600, живой"), 1); combo.setSelectedId (1);
            setSize (760, 360);
        }
        ~Gallery() override { setLookAndFeel (nullptr); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (brand::surface);
            drawSectionCaption (g, utf8 ("Крутилки"), { 16, 12, 340, 18 });
            drawSectionCaption (g, utf8 ("Фейдеры"), { 372, 12, 130, 18 });
            drawSectionCaption (g, utf8 ("Кнопки"), { 516, 12, 230, 18 });
            drawSectionCaption (g, utf8 ("Сегменты и списки"), { 16, 160, 340, 18 });
            drawSectionCaption (g, utf8 ("Раскрывашка"), { 16, 262, 340, 18 });
        }
        void resized() override
        {
            int x = 16;
            for (auto* k : { &k1, &k2, &k3, &k4 })
            {
                k->setBounds (x, 36, k->preferredWidth(), k->preferredHeight());
                x += k->preferredWidth() + 4;
            }
            k4.setBounds (x + 8, 36, Knob::widthFor (Knob::Size::Small), Knob::heightFor (Knob::Size::Small));
            fader.setBounds (372, 32, 64, 190);
            fader2.setBounds (440, 32, 64, 190);
            led1.setBounds (516, 36, 110, 30); led2.setBounds (632, 36, 110, 30); led3.setBounds (516, 74, 110, 30);
            primary.setBounds (516, 118, 130, 32); normal.setBounds (652, 118, 90, 32); quiet.setBounds (516, 158, 130, 32);
            seg.setBounds (16, 186, seg.preferredWidth(), 30);
            seg2.setBounds (16 + seg.preferredWidth() + 16, 186, seg2.preferredWidth(), 30);
            combo.setBounds (16, 224, 230, 28);
            disc.setBounds (16, 284, 160, 24);
        }

        SamplitLookAndFeel laf;
        Knob k1 { utf8 ("Громкость"), Knob::Size::Large }, k2 { utf8 ("Панорама"), Knob::Size::Large },
             k3 { utf8 ("Время ±"), Knob::Size::Large }, k4 { utf8 ("Сила ±"), Knob::Size::Small };
        Fader fader, fader2;
        LedButton led1 { utf8 ("Цикл") }, led2 { utf8 ("Соло") }, led3 { utf8 ("Недоступно") };
        juce::TextButton primary { utf8 ("Перетащить в Cubase") }, normal { utf8 ("Играть") }, quiet { utf8 ("Сбросить") };
        SegmentedControl seg { { "1", "2", "3", "4" } }, seg2 { { utf8 ("Стерео"), utf8 ("Мульти-аут") } };
        DisclosureHeader disc { utf8 ("Подробнее") };
        juce::ComboBox combo;
    };

    bool shootGallery (const juce::File& out)
    {
        Gallery gallery;
        pumpUntil ([] { return false; }, 100);
        const auto image = gallery.createComponentSnapshot (gallery.getLocalBounds(), true, 1.0f);
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

    if (argc > 2 && juce::String (argv[2]) == "--components")
        return shootGallery (outDir.getChildFile ("00-components.png")) ? 0 : 1;

    if (argc > 2 && juce::String (argv[2]) == "--installed")
    {
        // В куче: объект процессора (движок с 256 ударами) не помещается в стек 1 МБ в Debug
        auto processorHeap = std::make_unique<SamplitProcessor>();
        auto& processor = *processorHeap;
        processor.prepareToPlay (48000.0, 512);
        auto m = std::make_unique<LicenseManager> (LicenseManager::defaultConfig());
        pumpUntil ([&] { return ! m->getInfo().busy; });
        std::cout << "license valid: " << (m->getInfo().isValid() ? "yes" : "no") << std::endl;
        processor.setLicenseManager (std::move (m));

        bool loaded = false, ok = false;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        processor.setSetup (Setup::factory());
        processor.applySetup ([&] (const KitLoadResult& r)
        {
            loaded = true;
            ok = r.ok();
            std::cout << (ok ? "loaded" : "FAILED: ") << r.error << ", " << (r.memoryBytes >> 20) << " MB, "
                      << r.warnings.size() << " warnings, " << r.missingPacks.size() << " missing packs" << std::endl;
            for (const auto& w : r.warnings)
                std::cout << "  " << w << std::endl;
        });
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
        return ok && shoot (processor, outDir.getChildFile ("06-installed.png"), 1120, 640) ? 0 : 1;
    }

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-snapshot");
    root.deleteRecursively();
    const auto withLicense = root.getChildFile ("licensed");
    const auto noLicense = root.getChildFile ("unlicensed");
    const auto fingerprint = juce::String ("snapshot:machine");

    const auto key = pack::generateKey();
    for (const auto& dir : { withLicense, noLicense })
    {
        dir.getChildFile ("packs").createDirectory();
        for (const auto& id : factoryPackIds())
            if (! writePack (dir.getChildFile ("packs/" + id + ".splpack"), id, key))
            {
                std::cout << "cannot write pack " << id << std::endl;
                return 1;
            }
    }
    writeToken (withLicense.getChildFile ("license.token"), key, fingerprint);

    int failures = 0;

    // 1. Нет лицензии: пакеты закрыты, поле активации
    {
        // В куче: объект процессора (движок с 256 ударами) не помещается в стек 1 МБ в Debug
        auto processorHeap = std::make_unique<SamplitProcessor>();
        auto& processor = *processorHeap;
        processor.prepareToPlay (44100.0, 512);
        processor.setLicenseManager (manager (noLicense, fingerprint));
        failures += shoot (processor, outDir.getChildFile ("01-no-license.png"), 1120, 640) ? 0 : 1;
    }

    // 2-4. С лицензией: до применения установки, с загруженным китом, в узком окне
    {
        // В куче: объект процессора (движок с 256 ударами) не помещается в стек 1 МБ в Debug
        auto processorHeap = std::make_unique<SamplitProcessor>();
        auto& processor = *processorHeap;
        processor.prepareToPlay (44100.0, 512);
        processor.setLicenseManager (manager (withLicense, fingerprint));
        failures += shoot (processor, outDir.getChildFile ("02-licensed-empty.png"), 1120, 640) ? 0 : 1;

        bool loaded = false;
        processor.applySetup ([&] (const KitLoadResult& r)
        {
            loaded = true;
            if (! r.ok())
                std::cout << "load failed: " << r.error << std::endl;
        });
        pumpUntil ([&] { return loaded; });

        auto m = processor.getMicState (3);
        m.mute = true;
        processor.setMicState (3, m);
        failures += shoot (processor, outDir.getChildFile ("03-kit.png"), 1120, 640) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("04-kit-narrow.png"), 920, 560) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("05-kit-wide.png"), 1400, 800) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("04-instruments.png"), 1120, 640, Tab::Kit, Over::None, "snare", true) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("04-instruments-hat.png"), 1120, 640, Tab::Kit, Over::None, "hat", true) ? 0 : 1;
        drawDemoPattern (processor, 1);
        failures += shoot (processor, outDir.getChildFile ("06-pattern.png"), 1120, 640, Tab::Pattern) ? 0 : 1;
        drawDemoPattern (processor, 4);
        failures += shoot (processor, outDir.getChildFile ("06-pattern-narrow.png"), 920, 560, Tab::Pattern) ? 0 : 1;
        drawDemoPattern (processor, 2);
        failures += shoot (processor, outDir.getChildFile ("06-pattern-wide.png"), 1400, 800, Tab::Pattern) ? 0 : 1;
        processor.setPattern (Pattern {});
        failures += shoot (processor, outDir.getChildFile ("07-mixer.png"), 1120, 640, Tab::Mixer) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("08-mixer-narrow.png"), 920, 560, Tab::Mixer) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("11-settings.png"), 1120, 640, Tab::Kit, Over::Settings) ? 0 : 1;
        failures += shoot (processor, outDir.getChildFile ("10-license.png"), 1120, 640, Tab::Kit, Over::License) ? 0 : 1;
    }

    root.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
