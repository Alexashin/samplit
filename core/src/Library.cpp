#include "samplit/core/Library.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <sqlite3.h>

#include <unordered_map>

namespace samplit
{
    namespace
    {
        constexpr int kSchemaVersion = 1;

        // Подготовленный запрос: освобождается сам
        class Statement
        {
        public:
            Statement (sqlite3* db, const char* sql)
            {
                if (sqlite3_prepare_v2 (db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                    stmt = nullptr;
            }

            ~Statement() { sqlite3_finalize (stmt); }
            Statement (const Statement&) = delete;
            Statement& operator= (const Statement&) = delete;

            bool ok() const { return stmt != nullptr; }
            bool step() { return sqlite3_step (stmt) == SQLITE_ROW; }

            // Выполнить запрос без результата и сбросить для повторного использования
            bool run()
            {
                auto rc = sqlite3_step (stmt);
                sqlite3_reset (stmt);
                sqlite3_clear_bindings (stmt);
                return rc == SQLITE_DONE;
            }

            void reset()
            {
                sqlite3_reset (stmt);
                sqlite3_clear_bindings (stmt);
            }

            void bind (int i, const juce::String& s) { sqlite3_bind_text (stmt, i, s.toRawUTF8(), -1, SQLITE_TRANSIENT); }
            void bind (int i, juce::int64 v) { sqlite3_bind_int64 (stmt, i, v); }
            void bind (int i, double v) { sqlite3_bind_double (stmt, i, v); }

            juce::String text (int col) const
            {
                auto p = reinterpret_cast<const char*> (sqlite3_column_text (stmt, col));
                return p != nullptr ? juce::String::fromUTF8 (p) : juce::String();
            }

            juce::int64 int64 (int col) const { return sqlite3_column_int64 (stmt, col); }
            int integer (int col) const { return sqlite3_column_int (stmt, col); }
            double real (int col) const { return sqlite3_column_double (stmt, col); }

        private:
            sqlite3_stmt* stmt = nullptr;
        };

        bool exec (sqlite3* db, const char* sql)
        {
            return sqlite3_exec (db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
        }

        bool isAudioFile (const juce::File& f)
        {
            auto ext = f.getFileExtension().toLowerCase();
            return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg";
        }

        struct Known
        {
            juce::int64 id = 0, size = 0, mtime = 0;
        };

        const char* const kSchema = R"sql(
            CREATE TABLE IF NOT EXISTS meta (
                key   TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS samples (
                id          INTEGER PRIMARY KEY,
                path        TEXT UNIQUE NOT NULL,
                folder      TEXT,
                name        TEXT,
                size        INTEGER,
                mtime       INTEGER,
                duration    REAL,
                sample_rate INTEGER,
                channels    INTEGER,
                added_at    INTEGER
            );
            CREATE INDEX IF NOT EXISTS idx_samples_name ON samples (name);
        )sql";
    }

    Library::Library (sqlite3* handle) : db (handle) {}

    Library::~Library()
    {
        sqlite3_close (db);
    }

    std::unique_ptr<Library> Library::open (const juce::File& dbFile, juce::String& error)
    {
        error = {};
        dbFile.getParentDirectory().createDirectory();

        sqlite3* handle = nullptr;
        if (sqlite3_open_v2 (dbFile.getFullPathName().toRawUTF8(), &handle,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        {
            error = handle != nullptr ? juce::String::fromUTF8 (sqlite3_errmsg (handle)) : "sqlite3_open failed";
            sqlite3_close (handle);
            return nullptr;
        }

        sqlite3_busy_timeout (handle, 5000);
        exec (handle, "PRAGMA journal_mode=WAL;");
        exec (handle, "PRAGMA synchronous=NORMAL;");

        if (! exec (handle, kSchema))
        {
            error = juce::String::fromUTF8 (sqlite3_errmsg (handle));
            sqlite3_close (handle);
            return nullptr;
        }

        {
            Statement version (handle, "INSERT OR IGNORE INTO meta (key, value) VALUES ('schema_version', ?)");
            version.bind (1, juce::String (kSchemaVersion));
            version.run();
        }

        return std::unique_ptr<Library> (new Library (handle));
    }

    ScanResult Library::scanFolder (const juce::File& folder, std::function<bool()> shouldCancel)
    {
        ScanResult result;
        if (! folder.isDirectory())
            return result;

        const auto root = folder.getFullPathName();
        const auto prefix = root + juce::File::getSeparatorString();

        // Что уже есть в индексе под этой папкой; то, что останется после обхода, с диска исчезло
        std::unordered_map<std::string, Known> known;
        {
            Statement q (db, "SELECT id, path, size, mtime FROM samples WHERE substr(path, 1, ?) = ?");
            q.bind (1, (juce::int64) prefix.length());
            q.bind (2, prefix);
            while (q.step())
                known[q.text (1).toStdString()] = { q.int64 (0), q.int64 (2), q.int64 (3) };
        }

        Statement insert (db, "INSERT INTO samples (path, folder, name, size, mtime, duration, sample_rate, channels, added_at) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        Statement update (db, "UPDATE samples SET size = ?, mtime = ?, duration = ?, sample_rate = ?, channels = ? WHERE id = ?");
        Statement remove (db, "DELETE FROM samples WHERE id = ?");

        if (! insert.ok() || ! update.ok() || ! remove.ok() || ! exec (db, "BEGIN"))
            return result;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        bool cancelled = false;
        const auto now = juce::Time::currentTimeMillis() / 1000;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles,
                                                                juce::File::FollowSymlinks::noCycles))
        {
            if (shouldCancel && shouldCancel())
            {
                cancelled = true;
                break;
            }

            const auto file = entry.getFile();
            if (! isAudioFile (file))
                continue;

            const auto path = file.getFullPathName();
            const juce::int64 size = entry.getFileSize();
            const juce::int64 mtime = entry.getModificationTime().toMilliseconds();

            auto it = known.find (path.toStdString());
            const bool isKnown = it != known.end();
            const auto old = isKnown ? it->second : Known{};
            if (isKnown)
                known.erase (it);

            if (isKnown && old.size == size && old.mtime == mtime)
            {
                ++result.unchanged;
                continue;
            }

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            {
                ++result.failed;
                if (isKnown) // файл испортился: из индекса убираем
                {
                    remove.bind (1, old.id);
                    remove.run();
                }
                continue;
            }

            const double duration = (double) reader->lengthInSamples / reader->sampleRate;
            const auto rate = (juce::int64) reader->sampleRate;
            const auto channels = (juce::int64) reader->numChannels;

            if (isKnown)
            {
                update.bind (1, size);
                update.bind (2, mtime);
                update.bind (3, duration);
                update.bind (4, rate);
                update.bind (5, channels);
                update.bind (6, old.id);
                update.run();
                ++result.updated;
            }
            else
            {
                insert.bind (1, path);
                insert.bind (2, root);
                insert.bind (3, file.getFileNameWithoutExtension());
                insert.bind (4, size);
                insert.bind (5, mtime);
                insert.bind (6, duration);
                insert.bind (7, rate);
                insert.bind (8, channels);
                insert.bind (9, (juce::int64) now);
                insert.run();
                ++result.added;
            }
        }

        // При отмене обход неполный, поэтому ничего не удаляем
        if (! cancelled)
        {
            for (const auto& [path, info] : known)
            {
                remove.bind (1, info.id);
                remove.run();
                ++result.removed;
            }
        }

        exec (db, "COMMIT");
        return result;
    }

    std::vector<SampleInfo> Library::search (const juce::String& query, int limit) const
    {
        std::vector<SampleInfo> out;

        juce::StringArray words;
        words.addTokens (query, " \t\r\n", "");
        words.removeEmptyStrings();

        Statement q (db, "SELECT id, path, folder, name, duration, sample_rate, channels FROM samples ORDER BY path");
        if (! q.ok())
            return out;

        // Фильтр в C++: lower() в SQLite понимает только ASCII, а имена бывают и русскими
        while (q.step() && (int) out.size() < limit)
        {
            const auto path = q.text (1);
            const auto folder = q.text (2);
            const auto name = q.text (3);
            const auto relative = path.startsWith (folder) ? path.substring (folder.length() + 1) : path;

            bool match = true;
            for (const auto& w : words)
                if (! name.containsIgnoreCase (w) && ! relative.containsIgnoreCase (w))
                {
                    match = false;
                    break;
                }

            if (match)
                out.push_back ({ q.int64 (0), juce::File (path), name, q.real (4), q.integer (5), q.integer (6) });
        }

        return out;
    }

    int Library::count() const
    {
        Statement q (db, "SELECT COUNT(*) FROM samples");
        return q.ok() && q.step() ? q.integer (0) : 0;
    }
}
