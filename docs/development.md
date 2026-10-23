# Разработка

## Что нужно на машине

- Visual Studio с нагрузкой «Разработка классических приложений на C++» (MSVC)
- CMake 3.25+ и Ninja
- Git
- Reaper для ручной проверки, pluginval для валидации (положить `pluginval.exe` куда угодно и указать путь)

## Сборка

```powershell
git clone --recurse-submodules <адрес репозитория>
cd samplit
.\tools\build.ps1                    # Debug
.\tools\build.ps1 -Config Release
.\tools\build.ps1 -WithHost          # заодно JUCE AudioPluginHost (тестовый хост)
```

Если репозиторий склонировали без `--recurse-submodules`:

```powershell
git submodule update --init --recursive
```

Результаты в `build\<Config>\`:

| Что | Где |
|---|---|
| VST3 | `plugin\Samplit_artefacts\<Config>\VST3\Samplit.vst3` (копируется в `%LOCALAPPDATA%\Programs\Common\VST3`) |
| Отдельное приложение | `plugin\Samplit_artefacts\<Config>\Standalone\Samplit.exe` |
| Тесты | `tests\SamplitTests_artefacts\<Config>\SamplitTests.exe` |

Чтобы не копировать плагин в системную папку, добавьте `-DSAMPLIT_COPY_PLUGIN=OFF` к вызову CMake.

Если сборка падает с «файл занят», закройте Samplit.exe и DAW с загруженным плагином: Windows не даёт перезаписать загруженную DLL.

## Visual Studio

1. File → Open → Folder… → папка репозитория. VS сам найдёт `CMakeLists.txt` и `CMakePresets.json`.
2. В панели сверху выбрать конфигурацию **Debug** (сборка идёт в тот же `build\Debug`, что и у `tools\build.ps1`).
3. Рядом с зелёной стрелкой выбрать, что запускать: `Samplit.exe` (отдельное приложение) или `SamplitTests.exe` (тесты).
4. F5 запускает с отладчиком, Ctrl+F5 без. Тесты также видны в Test → Test Explorer.

Отладка плагина внутри Reaper: собрать Debug, открыть Reaper, в VS Debug → Attach to Process… → `reaper.exe`. Точки останова в `plugin/` сработают, когда Reaper вызовет код плагина.

## Тесты

```powershell
ctest --test-dir build\Debug --output-on-failure
```

Тесты лежат в `tests/`:

- `sampler_engine_tests.cpp` проверяют сэмплер: диапазоны клавиш и velocity, режимы группы, выходы, высоту, полифонию.
- `kit_engine_tests.cpp` проверяют движок ударов: все микрофоны удара вместе, режимы «как в Kontakt» и «Живой», choke, выходы микрофонов.
- `processor_tests.cpp` проверяют процессор: шины и раскладки (Main стерео, 16 моно-выходов), маршрутизацию на выходы, прослушивание в Main, чтение WAV, состояние проекта.
- `pack_tests.cpp` проверяют формат пакета: запись и чтение, шифрование, подмену байта, манифест.
- `license_tests.cpp` проверяют токен лицензии: подпись, срок, машину, откат часов, совместимость с Python-сервером.
- `core_tests.cpp` проверяют индекс папок в SQLite.
- Python: `python tests/test_mark_kit.py` (разметка), `cd tools/license_server; .venv\Scripts\python.exe -m pytest tests` (сервер).

Catch2 скачивается при первой конфигурации через CMake. Для самой работы плагина сеть не нужна.

## Валидация

```powershell
pluginval.exe --strictness-level 5 --skip-gui-tests --validate build\Debug\plugin\Samplit_artefacts\Debug\VST3\Samplit.vst3
```

Ожидаем `SUCCESS`. Проверка на уровне 5 прогоняет разные частоты и размеры блока, включение и выключение шин.

## Проверка в DAW

Инструкции по каждой DAW лежат в `docs/daw/`. Сейчас есть Reaper.

## Правила

- Аудиопоток: без аллокаций, блокировок и файлового ввода-вывода.
- Всё, что не про звук и интерфейс, пишем в `core/` без зависимости от JUCE GUI.
- Кроссплатформенность: без Windows-специфики в коде, пути и системные вызовы через JUCE.
- Сэмплы в git не коммитим.
- Коммит это одна законченная мысль, сообщение объясняет, что и зачем. Задача закрыта: тесты зелёные, CI зелёный.
