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

## Тесты

```powershell
ctest --test-dir build\Debug --output-on-failure
```

Тесты лежат в `tests/`:

- `pad_engine_tests.cpp` проверяют движок: точные отсчёты сэмпла, моно и стерео, velocity, конец сэмпла, пересчёт частоты, плавную остановку, перезапуск.
- `processor_tests.cpp` проверяют процессор: шины и раскладки, маршрутизацию пэдов на выходы, прослушивание в Main, старт ноты внутри блока, чтение WAV, безопасное удаление заменённых сэмплов, сохранение пэдов в проекте.

Тестовые сэмплы для ручной проверки генерирует `python tools/make_test_samples.py` (по умолчанию в `~/Music/Samplit Test Samples`).

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
