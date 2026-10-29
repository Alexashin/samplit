// samplit-packer: сборка, проверка и просмотр пакетов .splpack.
//
//   samplit-packer build <манифест.json> --root <папка библиотеки> --out <файл.splpack>
//                        (--key <hex64> | --key-file <файл> | --gen-key <файл>)
//                        [--trim] [--trim-db <дБ>] [--allow-silent]
//     По умолчанию звук хранится целиком, без обрезки (24 бит, FLAC без потерь). С --trim или --trim-db тихий
//     хвост (RMS блока 10 мс ниже порога, по умолчанию −60 дБ от полной шкалы) отрезается.
//     Файлы из одних нулей считаются повреждёнными и останавливают сборку, если нет --allow-silent.
//   samplit-packer verify <файл.splpack> (--key <hex64> | --key-file <файл>) [--deep]
//   samplit-packer info <файл.splpack> [--key <hex64> | --key-file <файл>]
//   samplit-packer fingerprint
//     Отпечаток этой машины: его принимает tools/make_token.py, чтобы выпустить токен лицензии на этот компьютер.
//
// Коды возврата: 0 успех, 1 ошибка в данных (проверка не прошла, сборка не удалась), 2 неверные аргументы.

#include "samplit/core/License.h"
#include "samplit/core/Pack.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <map>

#if JUCE_WINDOWS
extern "C" __declspec (dllimport) int __stdcall SetConsoleOutputCP (unsigned int);
#endif

using namespace samplit::pack;

namespace
{
    void say (const juce::String& s) { std::cout << s.toRawUTF8() << std::endl; }
    void sayErr (const juce::String& s) { std::cerr << s.toRawUTF8() << std::endl; }

    int usage()
    {
        sayErr ("Использование:\n"_s
                "  samplit-packer build <манифест.json> --root <папка> --out <файл.splpack> (--key <hex64> | --key-file <файл> | --gen-key <файл>)\n"_s
                "                       [--trim] [--trim-db <дБ, по умолчанию -60>] [--allow-silent]  (обрезка хвоста только с --trim/--trim-db)\n"_s
                "  samplit-packer verify <файл.splpack> (--key <hex64> | --key-file <файл>) [--deep]\n"_s
                "  samplit-packer info <файл.splpack> [--key <hex64> | --key-file <файл>]\n"_s
                "  samplit-packer fingerprint"_s);
        return 2;
    }

    struct Args
    {
        juce::StringArray positional;
        std::map<juce::String, juce::String> options;
        bool has (const juce::String& name) const { return options.count (name) != 0; }
        juce::String get (const juce::String& name) const
        {
            const auto it = options.find (name);
            return it == options.end() ? juce::String() : it->second;
        }
    };

    Args parseArgs (const juce::StringArray& argv, int from)
    {
        Args a;
        for (int i = from; i < argv.size(); ++i)
        {
            if (argv[i].startsWith ("--"))
            {
                const auto name = argv[i].substring (2);
                const bool flag = name == "deep" || name == "trim" || name == "allow-silent";
                a.options[name] = flag || i + 1 >= argv.size() ? juce::String ("1") : argv[++i];
            }
            else
                a.positional.add (argv[i]);
        }
        return a;
    }

    juce::File asFile (const juce::String& path)
    {
        return juce::File::getCurrentWorkingDirectory().getChildFile (path);
    }

    void printReport (const VerifyReport& r)
    {
        for (const auto& e : r.errors)
            sayErr ("ОШИБКА: "_s + e);
        for (const auto& w : r.warnings)
            say ("предупреждение: "_s + w);
    }

    // Ключ из --key или --key-file
    bool loadKey (const Args& a, PackKey& key)
    {
        if (a.has ("key"))
        {
            if (keyFromHex (a.get ("key"), key))
                return true;
            sayErr ("--key: ожидалось 64 шестнадцатеричных символа"_s);
            return false;
        }
        if (a.has ("key-file"))
        {
            const auto f = asFile (a.get ("key-file"));
            if (f.existsAsFile() && keyFromHex (f.loadFileAsString(), key))
                return true;
            sayErr ("--key-file: не удалось прочитать ключ из "_s + f.getFullPathName());
            return false;
        }
        sayErr ("нужен ключ: --key или --key-file"_s);
        return false;
    }

    // ---------------------------------------------------------------- build

    int build (const Args& a)
    {
        if (a.positional.size() != 1 || ! a.has ("root") || ! a.has ("out"))
            return usage();

        PackKey key {};
        if (a.has ("gen-key"))
        {
            key = generateKey();
            const auto keyFile = asFile (a.get ("gen-key"));
            if (! keyFile.replaceWithText (keyToHex (key) + "\n"))
            {
                sayErr ("не удалось записать ключ в "_s + keyFile.getFullPathName());
                return 1;
            }
            say ("ключ записан в "_s + keyFile.getFullPathName() + " (в git не коммитить)"_s);
        }
        else if (! loadKey (a, key))
            return 2;

        const auto manifestFile = asFile (a.positional[0]);
        const auto root = asFile (a.get ("root"));
        const auto outFile = asFile (a.get ("out"));
        if (! manifestFile.existsAsFile() || ! root.isDirectory())
        {
            sayErr ("нет манифеста или папки библиотеки"_s);
            return 2;
        }

        juce::String error;
        Manifest manifest;
        if (! Manifest::fromJson (manifestFile.loadFileAsString(), manifest, error))
        {
            sayErr (error);
            return 1;
        }

        const auto check = verifyManifest (manifest);
        printReport (check);
        if (! check.ok())
            return 1;

        // Все файлы должны существовать до начала долгой работы
        int missing = 0;
        for (const auto& piece : manifest.pieces)
            for (const auto& hit : piece.hits)
                for (const auto& [micId, f] : hit.files)
                    if (f.path.isEmpty() || ! root.getChildFile (f.path).existsAsFile())
                    {
                        if (++missing <= 20)
                            sayErr ("нет файла: "_s + f.path + " (удар "_s + hit.id + ", " + micId + ")");
                    }
        if (missing > 0)
        {
            sayErr ("не найдено файлов: "_s + juce::String (missing));
            return 1;
        }

        auto writer = PackWriter::create (outFile, manifest.id, key, error);
        if (writer == nullptr)
        {
            sayErr (error);
            return 1;
        }

        double trimDb = -60.0;
        if (a.has ("trim-db"))
        {
            trimDb = a.get ("trim-db").getDoubleValue();
            if (trimDb >= 0.0 || trimDb < -120.0)
            {
                sayErr ("--trim-db: ожидалось число от -120 до 0 дБ"_s);
                return 2;
            }
        }
        const bool trim = a.has ("trim") || a.has ("trim-db");
        const bool allowSilent = a.has ("allow-silent");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        // Одинаковый файл с одинаковой обрезкой кладём в пакет один раз
        struct Stored { int chunk, frames, peak; double rate; };
        juce::StringArray silentFiles;
        juce::int64 trimmedFrames = 0, keptFrames = 0;
        std::map<juce::String, Stored> done;

        Manifest stored = manifest;
        int processed = 0;
        const int total = [&manifest]
        {
            int n = 0;
            for (const auto& p : manifest.pieces)
                for (const auto& h : p.hits)
                    n += (int) h.files.size();
            return n;
        }();
        juce::int64 wavBytes = 0, packBytes = 0;

        for (auto& piece : stored.pieces)
            for (auto& hit : piece.hits)
                for (auto& [micId, f] : hit.files)
                {
                    const auto src = root.getChildFile (f.path);
                    const auto cacheKey = src.getFullPathName() + "|" + juce::String (f.start) + "|" + juce::String (f.end);

                    if (const auto it = done.find (cacheKey); it != done.end())
                    {
                        f.chunk = it->second.chunk;
                        f.frames = it->second.frames;
                        f.rate = it->second.rate;
                        f.peak = it->second.peak;
                    }
                    else
                    {
                        DecodedAudio audio;
                        if (! readAudioFile (formats, src, f.start, f.end, audio, error))
                        {
                            sayErr (error);
                            return 1;
                        }
                        const int peak = peakOf (audio);
                        if (peak == 0)
                            silentFiles.add (f.path + " (удар "_s + hit.id + ", " + micId + ")");

                        if (trim)
                        {
                            const int keep = trimmedLength (audio, trimDb);
                            trimmedFrames += audio.frames - keep;
                            audio.samples.resize ((size_t) keep);
                            audio.frames = keep;
                        }
                        keptFrames += audio.frames;

                        const int chunk = writer->addAudio (audio, error);
                        if (chunk < 0)
                        {
                            sayErr (error);
                            return 1;
                        }
                        wavBytes += src.getSize();
                        done[cacheKey] = { chunk, audio.frames, peak, audio.sampleRate };
                        f.chunk = chunk;
                        f.frames = audio.frames;
                        f.rate = audio.sampleRate;
                        f.peak = peak;
                    }

                    // Обрезка применена физически, путь в пакет не попадает
                    f.path = juce::String();
                    f.start = 0;
                    f.end = 0;

                    if (++processed % 500 == 0)
                        say (juce::String (processed) + " из "_s + juce::String (total) + " файлов"_s);
                }

        if (! silentFiles.isEmpty())
        {
            for (int i = 0; i < std::min (20, silentFiles.size()); ++i)
                sayErr ((allowSilent ? "предупреждение: файл из нулей: "_s : "ОШИБКА: файл из нулей (повреждённый WAV?): "_s) + silentFiles[i]);
            if (! allowSilent)
            {
                sayErr ("файлов из нулей: "_s + juce::String (silentFiles.size()) + ". Пакет не записан (--allow-silent разрешает)"_s);
                return 1;
            }
        }

        if (! writer->finish (stored, error))
        {
            sayErr (error);
            return 1;
        }
        packBytes = outFile.getSize();

        say ("пакет "_s + manifest.id + ": ударов "_s + juce::String (manifest.countHits()) + ", блоков "_s
             + juce::String (writer->getChunkCount()) + ", " + juce::String (packBytes) + " байт"_s
             + (wavBytes > 0 ? ", сжатие "_s + juce::String ((double) packBytes / (double) wavBytes * 100.0, 1) + " % от WAV"_s : juce::String()));
        if (trim && trimmedFrames > 0)
            say ("обрезано тихих хвостов: "_s + juce::String ((double) trimmedFrames / (double) (trimmedFrames + keptFrames) * 100.0, 1)
                 + " % отсчётов (порог "_s + juce::String (trimDb, 0) + " дБ)"_s);
        say ("записано в "_s + outFile.getFullPathName());
        return 0;
    }

    // ---------------------------------------------------------------- verify и info

    int verify (const Args& a)
    {
        if (a.positional.size() != 1)
            return usage();
        PackKey key {};
        if (! loadKey (a, key))
            return 2;

        juce::String error;
        auto reader = PackReader::open (asFile (a.positional[0]), error);
        if (reader == nullptr)
        {
            sayErr (error);
            return 1;
        }

        const auto report = verifyPack (*reader, key, a.has ("deep"));
        printReport (report);
        say (report.ok() ? "пакет в порядке (блоков "_s + juce::String (reader->getChunkCount()) + ", предупреждений "_s
                               + juce::String ((int) report.warnings.size()) + ")"
                         : "пакет с ошибками: "_s + juce::String ((int) report.errors.size()));
        return report.ok() ? 0 : 1;
    }

    int info (const Args& a)
    {
        if (a.positional.size() != 1)
            return usage();

        juce::String error;
        auto reader = PackReader::open (asFile (a.positional[0]), error);
        if (reader == nullptr)
        {
            sayErr (error);
            return 1;
        }

        const auto& h = reader->getHeader();
        say ("id: " + h.packId);
        say ("версия формата: "_s + juce::String ((int) h.formatVersion));
        say ("блоков: "_s + juce::String ((int) h.chunkCount));
        say ("размер файла: "_s + juce::String ((juce::int64) h.fileSize) + " байт"_s);

        if (a.has ("key") || a.has ("key-file"))
        {
            PackKey key {};
            if (! loadKey (a, key))
                return 2;
            Manifest m;
            if (! reader->readManifest (key, m, error))
            {
                sayErr (error);
                return 1;
            }
            say ("название: "_s + m.title + ", уровень: "_s + m.tier + ", версия: "_s + juce::String (m.version));
            say ("микрофонов: "_s + juce::String ((int) m.mics.size()) + ", барабанов: "_s + juce::String ((int) m.pieces.size())
                 + ", ударов: "_s + juce::String (m.countHits()));
        }
        return 0;
    }
}

int main (int argc, char** argv)
{
#if JUCE_WINDOWS
    SetConsoleOutputCP (65001);  // UTF-8 в консоли
#endif

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.isEmpty())
        return usage();

    const auto parsed = parseArgs (args, 1);
    if (args[0] == "build")
        return build (parsed);
    if (args[0] == "verify")
        return verify (parsed);
    if (args[0] == "info")
        return info (parsed);
    if (args[0] == "fingerprint")
    {
        const auto fp = samplit::license::machineFingerprint();
        if (fp.isEmpty())
        {
            sayErr ("не удалось получить отпечаток машины"_s);
            return 1;
        }
        say (fp);
        return 0;
    }
    return usage();
}
