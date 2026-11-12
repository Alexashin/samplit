# UX-аудит барабанных VST-инструментов

**Дата исследования:** 12 ноября 2026 года  
**Контекст:** Windows / VST3 / Cubase; далее — macOS  
**Продукт:** мультимикрофонный барабанный инструмент на одной библиотеке живой установки  
**Аудитория:** барабанщики-продюсеры и звукорежиссёры, включая начинающих

> **Как читать документ.** «Факт» — подтверждено документацией, официальной страницей или конкретным пользовательским сообщением. «Мнение / рекомендация» — проектный вывод из сравнительного анализа. Отзывы — качественная выборка, **не** репрезентативная статистика. Неподтверждённая возможность не приравнивается к отсутствующей.

## Краткое резюме

**Рекомендация:** объединить визуальную концепцию [EZdrummer 3](https://www.toontrack.com/product/ezdrummer-3/) с организацией быстрого редактирования [Addictive Drums 2](https://support.xlnaudio.com/hc/en-us/sections/16382380172573-User-Guide) и глубиной микшера [Superior Drummer 3](https://www.sweetwater.com/sweetcare/articles/getting-started-with-superior-drummer-3/). Это **мнение / проектная рекомендация**, а не результат пользовательского тестирования.

Ключевой принцип: **не показывать одновременно детальную установку, 15 полноразмерных канальных фейдеров и четырёхтактовый MIDI-редактор**. Разделять задачи по режимам, но оставлять часто используемые действия доступными в контексте.

Три наиболее полезных референса:

1. **EZdrummer 3:** схема установки, явные разделы Drums / Grooves / Grid Editor / Mixer, масштабирование окна. [Toontrack](https://www.toontrack.com/product/ezdrummer-3/).
2. **Addictive Drums 2:** быстрый доступ к отдельным инструментам, компактный микшер, контекстные подсказки. [XLN Audio User Guide](https://support.xlnaudio.com/hc/en-us/sections/16382380172573-User-Guide).
3. **Superior Drummer 3:** развитый микшер, маршрутизация, редактирование. Обратная сторона — более высокий порог освоения по свидетельствам некоторых пользователей. [Руководство](https://www.sweetwater.com/sweetcare/articles/getting-started-with-superior-drummer-3/); [Reddit](https://www.reddit.com/r/audioengineering/comments/1ranym4/superior_drummer_vs_ezdrummer_with_routing/).

---

## 1. Структура интерфейсов конкурентов

В таблице отражены доступные материалы по указанным линейкам. Для SSD использовано официальное руководство SSD5; не все детали независимо подтверждены именно для 5.5.

| Продукт | Главный экран, выбор и настройка | Микшер, MIDI, масштабирование и обучение |
|---|---|---|
| **EZdrummer 3** | Фотореалистичная установка; клик выбирает/прослушивает инструмент. Вкладки Drums, Grooves, Grid Editor, Mixer, Bandmate. | Микшер с маршрутизацией, MIDI-редактор, гуманизация и MIDI drag. Изменение размера и масштаба, обучающие материалы. [Toontrack](https://www.toontrack.com/product/ezdrummer-3/), [Sweetwater](https://www.sweetwater.com/sweetcare/articles/how-to-use-ezdrummer-3/). |
| **Superior Drummer 3** | Drums, Grooves, Mixer, Tracker. Выбор барабана на схеме, глубокая настройка артикуляций и слоёв. | Подробный микшер микрофонов, bleed, multi-out; Groove Browser, Grid Editor, MIDI drag. Пресеты и пользовательские размеры; добавлен режим масштаба 125%. [Руководство](https://www.sweetwater.com/sweetcare/articles/getting-started-with-superior-drummer-3/), [release notes](https://www.toontrack.com/release-notes/superior-drummer-3/). |
| **Addictive Drums 2 / 2.5** | Explore, Kit, Edit, FX, Beats. Вместо единой сцены — ячейки инструментов с изображениями. Клик для прослушивания; в ячейке есть mute/solo, выбор и громкость. | Компактный микшер и детальные настройки; Beats Browser, Transformer, MIDI drag. Меню UI Scaling, подсказки при наведении (HelpTips). [Kit](https://support.xlnaudio.com/hc/en-us/articles/16592881519389-Interface-overview-Kit-page), [Beats](https://support.xlnaudio.com/hc/en-us/articles/16593258822429-Interface-overview-Beats-page), [Top](https://support.xlnaudio.com/hc/en-us/articles/16567658507549-Interface-overview-Top-section). |
| **Steven Slate Drums 5.5** | Construct Kit, Edit Instrument, Mixer, Mapping, Grooves, Settings. В документации SSD5 предусмотрены графическая установка и pads. | Фейдеры, панорама, mute/solo, маршрутизация; браузер грувов, MIDI drag. Для SSD5 описан Respect OS Display Scaling; поведение конкретной версии 5.5 нуждается в тестировании. [SSD5 Manual PDF](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf). |
| **BFD3 / BFD3.5** | Схема установки, Browser, Drum Editor, Mixer, Effects, Groove Editor, Key Map, редакторы Tech/Model/Mics. | Детальный микшер, Groove Palette/Editor, MIDI drag. В 3.5 изменён ресайз, но пользователи продолжают сообщать о проблемах масштабирования содержимого. [Manual](https://internal.bfddrums.com/manuals/BFD30/using_bfd3.htm), [Gearspace](https://gearspace.com/threads/bfd-drums-releases-bfd-3-5.1444020/page-2). |
| **GetGood Drums** | На примере Modern & Massive 2: Drums с подсветкой выбранного барабана, настройки Levels, отдельные Mixer и Grooves. | Управление уровнем конкретного барабана в микрофонах, общий микшер, эффекты, маршрутизация, MIDI drag и карты. Точные режимы масштабирования не подтверждены. [GGD Support](https://support.ggd.co/hc/en-us/categories/31443955854103-Modern-Massive-2). |
| **Ugritone** | На примере RIOT Drums 2: графический набор, пресеты, настройки инструментов. | Уровни микрофонов, панорама, эффекты, до 16 выходов; заявлено масштабирование GUI. MIDI редактируется через DAW. Встроенный редактор и MIDI drag для RIOT Drums 2 надёжно не подтверждены. [Продукт](https://ugritone.com/products/riot-drums-2), [Quick Start](https://support.ugritone.com/portal/en/kb/articles/how-to-use-ugritone-drums-in-a-daw). |
| **MODO Drum 1.5** | Визуальная установка; Customize для геометрии, материалов, настройки барабанов, стилистики игры. | Микшер/эффекты, менеджер грувов, MIDI drag. Заявлен resizable GUI. [IK Multimedia](https://www.ikmultimedia.com/products/mododrum/index.php?p=info). |
| **Native Instruments Abbey Road Drummers / Kontakt** | Drum, Studio/Mixer, Grooves; выбор отдельных элементов, варианты инструментов и настройки. | Микшер микрофонов, bleed, эффекты, MIDI drag. У Kontakt 8 есть фиксированные уровни zoom; масштаб самого интерфейса библиотеки зависит от реализации. [Abbey Road](https://www.native-instruments.com/products/abbey-road-modern-drummer/?tab=mixer-section), [NI Community](https://community.native-instruments.com/discussion/46800/reszie-or-scale-kontakt-instrument-editor). |
| **Logic Drummer / Drum Kit Designer** | В Drum Kit Designer — 3D-представление; клик открывает Exchange/Edit. Партия редактируется в Session Player Editor внутри Logic. | Многоканальные Producer Kits через Logic. Партии могут превращаться в MIDI-регионы — это не эквивалент MIDI drag из внешнего VST. Масштаб определяется средой Logic. [Apple Kit Designer](https://support.apple.com/en-gb/guide/logicpro/lgsi825fc878/mac), [Session Player](https://support.apple.com/guide/logicpro/the-session-player-editor-lgcp32d55998/mac). |

**Вывод (мнение):** инструменты, ориентированные на сочинение партий, выделяют браузер/редактор MIDI; инструменты глубокой звуковой работы — отдельные микшеры и редакторы. Для нашей одной библиотеки разумно поддержать оба сценария без одновременной экспозиции всех функций. Сравнить: [EZdrummer](https://www.toontrack.com/product/ezdrummer-3/), [BFD](https://internal.bfddrums.com/manuals/BFD30/using_bfd3.htm), [GGD](https://support.ggd.co/hc/en-us/categories/31443955854103-Modern-Massive-2).

---

## 2. Жалобы и похвалы пользователей

**Метод:** качественная выборка отдельных сообщений Reddit, KVR, Gearspace и форумов. Проблемы MIDI, установки или производительности могут зависеть от DAW, драйвера и машины, не обязательно от одного плагина. Цитаты короткие, дословные; они не означают массового мнения.

### 2.1. Жалобы по темам

| Тема | Свидетельство |
|---|---|
| **Перегруженность и обучение** | Пользователь r/audioengineering про Superior Drummer: *“been overwhelmed by - SD”*. Глубина настроек ощущается как барьер для быстрого результата. [Reddit](https://www.reddit.com/r/audioengineering/comments/1ranym4/superior_drummer_vs_ezdrummer_with_routing/). |
| **Мелкие элементы, навигация** | В пользовательском обзоре BFD3 на Gearspace интерфейс назван *“unintuitive, messy”*, кнопки — чрезмерно мелкими. Это отзыв о BFD3, не проверка улучшений 3.5. [Gearspace](https://gearspace.com/rms/bfd-drums-bfd3.68752/user-reviews). |
| **Размер и HiDPI** | В KVR пользователь BFD3.5 сообщает: *“the bottom 1/3 disappears”* на ноутбуке; другой упоминает 13,3-дюймовый MacBook Air. [KVR](https://www.kvraudio.com/forum/viewtopic.php?t=623347). |
| **Термины, артикуляции, MIDI mapping** | В r/edrums обсуждают непонятные CC хай-хэта SSD5.5; в r/drums — наложение нескольких ударов при неверном маппинге SSD5. [Reddit CC](https://www.reddit.com/r/edrums/comments/1hz4fiq), [Reddit mapping](https://www.reddit.com/r/drums/comments/1665h6c). |
| **Cubase / Drum Map** | Пользователи создавали карты Superior Drummer 3 самостоятельно. На форуме Steinberg в 2026 обсуждались безымянные/обобщённые выходы SD3 вида *“Output 7”*. [Drum Map](https://forums.steinberg.net/t/superior-drummer-3-drum-map/134328), [Output labels](https://forums.steinberg.net/t/need-help-with-sd3-vsti-multichannel-out-labels/1042999). |
| **Сложность конфигурации в DAW** | В r/edrums пользователь SSD5.5 + Cubase 14 описывает артикуляцию, которую не удаётся правильно вызвать через MIDI mapping. Отдельный случай, не подтверждённый системный дефект. [Reddit](https://www.reddit.com/r/edrums/comments/1rzsxw0/ssd5_trouble_midi_mapping/). |
| **Загрузка и производительность** | Пользователи BFD3.5 на KVR жалуются на сканирование контента и библиотеки; Gearspace обсуждает длительную первичную проверку авторизации. [KVR](https://www.kvraudio.com/forum/viewtopic.php?t=623347), [Gearspace](https://gearspace.com/threads/bfd-drums-releases-bfd-3-5.1444020/page-2). |

### 2.2. Положительные отзывы

| Продукт | Положительный сигнал |
|---|---|
| **EZdrummer 3** | В r/edrums его рекомендуют как простой инструмент для знакомства, а настройки громкости и высоты считают понятнее некоторых настроек аппаратных модулей. [Reddit](https://www.reddit.com/r/edrums/comments/1ae3b28). |
| **Addictive Drums 2.5** | В KVR пользователи отмечали визуальное обновление и Beat Transformer; прежний UI некоторым казался неудобным. [KVR](https://www.kvraudio.com/forum/viewtopic.php?start=240&t=409423). |
| **Superior Drummer 3** | На r/audioengineering положительно описывали глубокую настройку микрофонов, velocity и bleed — одновременно признавая сложность. [Reddit](https://www.reddit.com/r/audioengineering/comments/1ranym4/superior_drummer_vs_ezdrummer_with_routing/). |
| **BFD3.5** | На Gearspace положительно отмечена возможность менять размер окна при наличии других проблем. [Gearspace](https://gearspace.com/threads/bfd-drums-releases-bfd-3-5.1444020/). |
| **Kontakt 8** | Пользователь NI Community положительно оценил возможность увеличить интерфейс; сотрудник NI уточнил ограничения Edit View. [NI](https://community.native-instruments.com/discussion/46800/reszie-or-scale-kontakt-instrument-editor). |

**Русскоязычные форумы:** найдено [обсуждение BFD3 на RMmedia](https://rmmedia.ru/threads/105984/) с техническими проблемами загрузки пресетов и совместимости. Сопоставимой выборки UX-жалоб о навигации, мелком тексте и масштабировании по перечисленным продуктам на русскоязычных площадках не обнаружено. Для **Ugritone, MODO Drum и отдельных Kontakt-библиотек** выборка конкретных UX-отзывов тоже недостаточна. Это не утверждение об отсутствии жалоб.

---

## 3. Принципы и антипаттерны

### 3.1. Четырнадцать полезных приёмов

> Это **рекомендации**; источники служат примерами реализации приёма.

| № | Приём | Пример |
|---:|---|---|
| 1 | Постоянная верхняя навигация по задачам | [EZdrummer 3](https://www.toontrack.com/product/ezdrummer-3/) |
| 2 | Видимое название пресета и сохранение | [AD2 Top section](https://support.xlnaudio.com/hc/en-us/articles/16567658507549-Interface-overview-Top-section) |
| 3 | Клик по барабану открывает его настройки | [Logic Drum Kit Designer](https://support.apple.com/en-gb/guide/logicpro/lgsi825fc878/mac) |
| 4 | Прослушивание с зависимостью velocity от положения клика | [AD2 Kit](https://support.xlnaudio.com/hc/en-us/articles/16592881519389-Interface-overview-Kit-page) |
| 5 | Mute/solo и уровень барабана без перехода в микшер | [AD2 Kit](https://support.xlnaudio.com/hc/en-us/articles/16592881519389-Interface-overview-Kit-page) |
| 6 | Отдельная профессиональная страница микшера | [Superior Drummer 3](https://www.sweetwater.com/sweetcare/articles/getting-started-with-superior-drummer-3/) |
| 7 | Разделение уровня барабана в Room/OH и общего уровня канала | [GGD Levels](https://support.ggd.co/hc/en-us/articles/31476648207127-Adjust-the-volume-of-individual-kit-pieces-and-mics) |
| 8 | Пресеты маршрутизации, сокращающие multi-out настройку | [SSD5 Manual](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf) |
| 9 | Перетаскивание MIDI непосредственно в DAW | [EZdrummer 3](https://www.toontrack.com/product/ezdrummer-3/) |
| 10 | Humanize и акценты отдельно от ручного ввода | [AD2 Beats](https://support.xlnaudio.com/hc/en-us/articles/16593258822429-Interface-overview-Beats-page) |
| 11 | Изменяемый, сохраняемый MIDI mapping | [SSD5 Manual](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf) |
| 12 | Краткие подсказки при наведении | [AD2 HelpTips](https://support.xlnaudio.com/hc/en-us/articles/16567626357917-Overview-of-Addictive-Drums-2) |
| 13 | Независимая настройка размеров окна и масштаба содержимого | [SD3 Release Notes](https://www.toontrack.com/release-notes/superior-drummer-3/) |
| 14 | Старт с готового набора и постепенное открытие глубокой настройки | [MODO Drum](https://www.ikmultimedia.com/products/mododrum/index.php?p=info) |

### 3.2. Десять антипаттернов

> Это **UX-риски** или конкретные жалобы, но не глобальные оценки качества продуктов.

| № | Антипаттерн | Пример / основание |
|---:|---|---|
| 1 | Окно растягивается, а текст и контролы масштабируются некорректно | [BFD3.5 Gearspace](https://gearspace.com/threads/bfd-drums-releases-bfd-3-5.1444020/page-2) |
| 2 | Мелкие зоны нажатия | [Отзывы BFD3](https://gearspace.com/rms/bfd-drums-bfd3.68752/user-reviews) |
| 3 | Экспертные настройки вытесняют повседневные | [SD3 / Reddit](https://www.reddit.com/r/audioengineering/comments/1ranym4/superior_drummer_vs_ezdrummer_with_routing/) |
| 4 | Похожие режимы без ясного различия | Сложность Grid Search / Replace / Search&Replace в [AD2 Beats](https://support.xlnaudio.com/hc/en-us/articles/16593258822429-Interface-overview-Beats-page) |
| 5 | Неясно, регулируется барабан или микрофон | Показательная терминология в [GGD](https://support.ggd.co/hc/en-us/articles/31476648207127-Adjust-the-volume-of-individual-kit-pieces-and-mics) |
| 6 | Каналы с именами Out 1, Out 2 без контекста | [SD3 + Cubase](https://forums.steinberg.net/t/need-help-with-sd3-vsti-multichannel-out-labels/1042999) |
| 7 | Для первого звука нужна сложная настройка MIDI | [Опыт SSD5.5](https://www.reddit.com/r/edrums/comments/1kyvitk) |
| 8 | Смена библиотек/наборов неожиданно меняет привычный маппинг | [Пользовательское обсуждение](https://www.reddit.com/r/edrums/comments/1ae3b28) |
| 9 | Минимальное окно не помещается на экран | [BFD3.5 / KVR](https://www.kvraudio.com/forum/viewtopic.php?t=623347) |
| 10 | Реклама или демонстрация других продуктов конкурирует с рабочим сценарием | Наличие таких разделов обсуждается как **проектный риск**, пример: [RIOT Drums 2](https://ugritone.com/products/riot-drums-2) |

---

## 4. Схема установки: фото, вектор, 3D

| Подход | Плюсы | Минусы | Пример |
|---|---|---|---|
| **Фото/фотореалистичный рендер** | Узнаваемость, реалистичность, сильное первое впечатление | Перекрытия, растровое масштабирование, зоны клика | [EZdrummer 3](https://www.toontrack.com/product/ezdrummer-3/) |
| **Детальный 2D-вектор** | Чёткие контуры, отдельные интерактивные области, гибкая подсветка | Художественная сложность, визуальный шум при чрезмерной детализации | **Рекомендация для нашего продукта** |
| **Интерактивная 3D-сцена** | Глубина, ракурсы, анимация | Сложная реализация, тестирование, дополнительные ресурсы | [Logic Drum Kit Designer](https://support.apple.com/guide/logicpro/drum-kit-designer-overview-lgsi2fb2509e/10.7/mac/11.0) — 3D-представление; это не доказательство real-time 3D-рендеринга |

### Рекомендованная реализация

- Каждый из **12 барабанов и тарелок** — отдельная визуальная группа с достаточно крупной невидимой зоной клика. Для перекрытий предусмотреть приоритет выбора и альтернативный список.
- **Hover:** контур и название. **Click:** прослушивание. **Select:** постоянная подсветка, визуально отличная от короткой индикации удара.
- **Velocity:** по высоте клика внутри зоны (ниже — тише, выше — сильнее), с альтернативным режимом фиксированной velocity. Подобный принцип есть в [AD2](https://support.xlnaudio.com/hc/en-us/articles/16592881519389-Interface-overview-Kit-page).
- Выключенный инструмент слегка затемнять, но оставлять выбираемым.
- При входящем MIDI кратко подсвечивать соответствующий инструмент.
- Графическая детализация не равна детализации hitbox: мелкий векторный рисунок не должен дробить область нажатия.
- Статические элементы/геометрию кэшировать, при ударе менять только состояние подсветки. Аудиопоток не должен ждать отрисовки UI.

**Ограничение:** достоверных сопоставимых замеров производительности фото/SVG/3D именно в указанных продуктах не найдено. Нельзя без тестов утверждать, что вектор всегда быстрее растровой или 3D-графики.

---

## 5. Контролы и жесты

| Контрол | Назначение | Референс |
|---|---|---|
| Вертикальный фейдер | Уровни 15 микрофонов, сравнительная оценка | [SSD5 Manual](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf) |
| Крутилка | Панорама, громкость инструмента, непрерывные параметры | [Logic](https://help.apple.com/logicpro/mac/10.1/en.lproj/lgcpafa3d1a9.html) |
| Горизонтальный слайдер | Humanize, swing, вероятности, диапазон | [AD2 Beats](https://support.xlnaudio.com/hc/en-us/articles/16593258822429-Interface-overview-Beats-page) |
| Сегментированный переключатель | Stereo/Multi-out и режим выбора записей | **Наше проектное решение** |

**Факты о жестах у конкурентов:**

- **AD2:** Shift для тонкой настройки, Ctrl+Click в Windows для сброса, у большинства контролов есть управление колесом. [XLN](https://support.xlnaudio.com/hc/en-us/articles/16593137207581-Interface-overview-Edit-page-overview).
- **SSD5:** двойной клик сбрасывает knob/fader; Ctrl/Cmd помогает точной настройке; Alt+Click тоже может сбросить значение. [Manual](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf).
- **Logic:** вертикальное движение для крутилки и двойной клик по числовому полю для ручного ввода. [Apple](https://help.apple.com/logicpro/mac/10.1/en.lproj/lgcpafa3d1a9.html).

**Предлагаемый единый стандарт нашего плагина:**

| Действие | Реакция |
|---|---|
| Перетащить крутилку вверх/вниз | Увеличить/уменьшить |
| Shift + drag | Точный шаг |
| Двойной клик по контролу | Сброс к значению по умолчанию |
| Двойной клик по числу | Ручной ввод |
| Колесо над активным контролом | Изменение небольшим шагом |
| Правый клик | Reset / Copy / Paste |
| Tab и стрелки | Фокус, навигация и изменение с клавиатуры |

Нужны текстовые подписи и числовые значения, видимый фокус клавиатуры, достаточные hitbox и нецветовая индикация mute/solo. Не стоит задавать разные модификаторы Shift/Ctrl для идентичных операций на разных страницах.

---

## 6. Масштаб окна, HiDPI, Cubase

### 6.1. Что подтверждено

| Продукт | Решение / ограничение | Источник |
|---|---|---|
| EZdrummer 3 | Изменение размера и масштаба | [Toontrack](https://www.toontrack.com/product/ezdrummer-3/) |
| Superior Drummer 3 | Scale/Size, пользовательский размер, режим 125%, обработка DPI | [Release Notes](https://www.toontrack.com/release-notes/superior-drummer-3/) |
| Addictive Drums 2 | UI Scaling & Settings; точные пределы не подтверждены | [XLN](https://support.xlnaudio.com/hc/en-us/articles/16567658507549-Interface-overview-Top-section) |
| BFD3.5 | Ресайз окна; жалобы на масштабирование остаются | [Gearspace](https://gearspace.com/threads/bfd-drums-releases-bfd-3-5.1444020/page-2) |
| SSD5 / SSD5.5 | Respect OS Display Scaling описан для SSD5; диапазон 5.5 не подтверждён | [Manual](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf) |
| MODO Drum | Заявлен resizable GUI, диапазон не указан | [IK](https://www.ikmultimedia.com/products/mododrum/index.php?p=specs) |
| Kontakt 8 | Фиксированные уровни 50–200%; ограничения для Edit View | [NI](https://community.native-instruments.com/discussion/46800/reszie-or-scale-kontakt-instrument-editor) |

### 6.2. Расчёт полезного пространства

Если монитор физически 1920×1080, то при стандартной DPI-пересчётной модели получаем следующие **логические** размеры (до учёта рамок Cubase и ОС):

| Масштаб Windows | Логические пиксели |
|---|---|
| 100% | 1920×1080 |
| 125% | 1536×864 |
| 150% | 1280×720 |

Это **арифметическая оценка**, не измерение доступного клиентского прямоугольника плагина в Cubase.

### 6.3. Предлагаемые целевые размеры

| Пресет | Размер логического интерфейса | Назначение |
|---|---|---|
| **Compact** | 920×560 | Ноутбуки, Windows 150%, укороченные панели |
| **Standard** | 1120×640 | Базовый режим |
| **Expanded** | 1400×800 | Большие экраны, полный микшер |

Это **проектные размеры**, а не замеры конкурентов. Рекомендуется свободное изменение размеров в допустимых границах, запоминание последнего размера/масштаба и автоматическое компактное состояние при нехватке места. Текст и кнопки не должны уменьшаться вместе с детальной графикой установки до нечитабельных размеров.

Ресайз VST3 требует корректного взаимодействия с хостом: [VST3 Resize View Call Sequence](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Workflow%2BDiagrams/Resize%2BView%2BCall%2BSequence.html), [VSTGUI Editor API](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_v_s_t3_editor.html), [Steinberg HiDPI](https://helpcenter.steinberg.de/hc/it/articles/32061688285842-Windows-Custom-scaling-option-in-Cubase-and-Nuendo).

**Обязательные тесты:** Windows 100/125/150%, Cubase в оконном/максимизированном режиме, перенос между DPI-разными мониторами, повторное открытие, проверка точных координат зон клика, MIDI drag-and-drop при каждом режиме, панель из 15 микрофонов и разные размеры шрифтов.

---

## 7. Онбординг и помощь без регистрации

**Цель (рекомендация):** первый звук сразу после открытия, без регистрации или обязательного обучения. Ориентиры: [AD2 HelpTips](https://support.xlnaudio.com/hc/en-us/articles/16567626357917-Overview-of-Addictive-Drums-2) и [EZdrummer 3](https://www.toontrack.com/product/ezdrummer-3/).

| Ситуация | Предлагаемое поведение |
|---|---|
| Первый запуск | Стандартная установка уже загружена. Ненавязчивые подсказки «Нажмите на барабан», «Откройте микшер», «Создайте ритм». |
| Наведение на параметр | Название, значение, действие, жест тонкой настройки. |
| Пустой MIDI-редактор | «Нажмите на сетку, чтобы добавить удар», действие «Создать простой ритм». |
| Нет звука | Явное различение: библиотека не загружена / MIDI не приходит / инструмент выключен / mute активен. |
| Multi-out не настроен | Короткая локальная инструкция для Cubase. |
| Неочевидный параметр | `?` рядом с группой, встроенная справка. |
| Первый MIDI drag | «Перетащите MIDI на инструментальную дорожку Cubase». |

Обязательный мастер при каждом создании экземпляра VST не нужен. В меню оставить «Повторить обучение».

Термины **velocity, round robin, bleed, articulation, multi-out** объяснять в контексте. Например, вместо одиночного `RR Mode` писать «Порядок выбора записанных ударов» с кратким пояснением.

---

## 8. Архитектура нашего плагина: два варианта

### Вариант A. **Kit-first** — рекомендуемый

```text
┌───────────────────────────────────────────────────────────────┐
│ DRUM INSTRUMENT                         Preset: Studio Default│
├───────────────────────────────────────────────────────────────┤
│             KIT             MIXER            GROOVE           │
├──────────────────────────────────────┬────────────────────────┤
│                                      │ SNARE 1                │
│       ДЕТАЛЬНАЯ ВЕКТОРНАЯ            │ Enabled [✓]            │
│       УДАРНАЯ УСТАНОВКА              │ Sound: Natural         │
│                                      │ Volume: 0.0 dB         │
│       12 интерактивных зон            │ Playback: Round Robin  │
│       выбор / прослушивание / LED     │ More settings...       │
├──────────────────────────────────────┴────────────────────────┤
│ Mini Groove / Transport       [Play]              [Drag MIDI] │
└───────────────────────────────────────────────────────────────┘
```

- В режиме **Kit** установка — центральный элемент, справа контекстная панель выбранного барабана.
- **Mixer** открывает полноценный микшер 15 каналов с M/S, pan, routing и stereo/multi-out.
- **Groove** раскрывает редактор 1–4 тактов, velocity, humanize и drag MIDI.
- На ноутбуках панель выбранного барабана может сворачиваться, а сама установка масштабируется отдельно от подписи параметров.

**Плюсы:** понятный первый запуск, удовлетворяет требованию владельца библиотеки, быстрота прослушивания. **Минус:** в микшер нужно переходить. Образцы: [EZdrummer](https://www.toontrack.com/product/ezdrummer-3/), [Logic](https://support.apple.com/en-gb/guide/logicpro/lgsi825fc878/mac).

### Вариант B. **Studio-first**

```text
┌───────────────────────────────────────────────────────────────┐
│ DRUM STUDIO                           Preset: Studio Default │
├───────────────┬───────────────────────────────────────────────┤
│ INSTRUMENTS   │ MIC MIXER                                     │
│  Kick         │ [K In][K Out][Sn Top][Sn Btm][OH][Room] ...   │
│ >Snare        │  │     │      │       │      │   │           │
│  Hi-Hat       │  ▓     ▓      ▓       ▓      ▓   ▓           │
│  Tom 1        │  M/S   M/S    M/S     M/S    M/S M/S          │
│  Tom 2        │                                 Stereo / Out │
│  Ride         │                                               │
├───────────────┴───────────────────────────────────────────────┤
│ Выбранный барабан / MIDI Pattern / сворачиваемая схема        │
└───────────────────────────────────────────────────────────────┘
```

**Плюсы:** удобен при регулярной работе с микрофонами, быстро видны уровни и маршруты. **Минусы:** для новичка сложнее; 15 каналов требуют горизонтальной прокрутки/группировки; детальная установка менее заметна. Образцы: [SSD5](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf), [GGD](https://support.ggd.co/hc/en-us/articles/31476648207127-Adjust-the-volume-of-individual-kit-pieces-and-mics).

**Итог:** реализовать **A** как основную структуру, а логику **B** — как содержимое страницы Mixer, не создавая две параллельные навигационные системы.

### 8.1. «На виду / под рукой / в меню»

| На виду постоянно | Под рукой в активном режиме | В меню / настройках |
|---|---|---|
| Название текущей установки | Вариант звука барабана | Пути к библиотеке |
| Kit / Mixer / Groove | Enable, Volume, режим выбора записи | Подробный MIDI mapping |
| Схема в режиме Kit | 15 микрофонов во вкладке Mixer | Импорт/экспорт MIDI mapping |
| Play/Stop, состояние DAW | Pan, Mute, Solo | Диагностика выходов |
| Видимое сохранение пресета | Stereo / Multi-out в Mixer | UI scale |
| Прослушивание кликом | 1–4 такта, velocity, humanize | Системные параметры |
| Drag MIDI | Группы ударов, Round Robin | Сохранение по умолчанию / в файл |

Сохранение в файл должно быть и через очевидное выпадающее меню у кнопки **Save**, а не только в глубоком меню.

**Для Cubase:** включить готовую карту барабанов `.drm`, соответствующую MIDI-маппингу продукта. Это **проектная рекомендация**, основанная на том, что пользователи [создавали карты SD3 вручную](https://forums.steinberg.net/t/superior-drummer-3-drum-map/134328).

---

## 9. План внедрения и проверки

1. **Приоритет 1 — информационная архитектура и размеры.** Три режима, контекстная панель, адаптивный компактный вариант, текст и кликабельные элементы нормального размера.
2. **Приоритет 2 — схема установки.** Отдельные hitbox, выбор, динамическая MIDI-подсветка, velocity и отключённые инструменты.
3. **Приоритет 3 — микшер и MIDI-редактор.** Единые жесты, ясные названия, удобная маршрутизация, multi-out, Drag MIDI.
4. **Приоритет 4 — тесты в Cubase.** HiDPI 125–150%, перенос окна, Drum Map, VST3 resize, drag-and-drop, корректность маршрутизации.

Не стоит добавлять полноценный real-time 3D, большой магазин библиотек или сложные эффекты только ради функционального паритета с крупнейшими продуктами. Для **одной живой установки** ключевыми ценностями остаются быстрый выбор звука, реалистичное чередование сэмплов, контроль микрофонов и передача MIDI в DAW. Это **мнение / стратегия продукта**.

---

## 10. Список источников

### Официальные руководства и страницы продуктов

**Toontrack**

- [EZdrummer 3](https://www.toontrack.com/product/ezdrummer-3/)
- [EZdrummer Quick Start (Sweetwater)](https://www.sweetwater.com/sweetcare/articles/how-to-use-ezdrummer-3/)
- [Superior Drummer 3 Quick Start (Sweetwater)](https://www.sweetwater.com/sweetcare/articles/getting-started-with-superior-drummer-3/)
- [Superior Drummer 3 Release Notes](https://www.toontrack.com/release-notes/superior-drummer-3/)

**XLN Audio**

- [AD2 User Guide](https://support.xlnaudio.com/hc/en-us/sections/16382380172573-User-Guide)
- [Kit](https://support.xlnaudio.com/hc/en-us/articles/16592881519389-Interface-overview-Kit-page)
- [Edit](https://support.xlnaudio.com/hc/en-us/articles/16593137207581-Interface-overview-Edit-page-overview)
- [Beats](https://support.xlnaudio.com/hc/en-us/articles/16593258822429-Interface-overview-Beats-page)
- [Top Section](https://support.xlnaudio.com/hc/en-us/articles/16567658507549-Interface-overview-Top-section)
- [HelpTips](https://support.xlnaudio.com/hc/en-us/articles/16567626357917-Overview-of-Addictive-Drums-2)

**Steven Slate Drums**

- [SSD5 User Manual — PDF](https://download.stevenslatedrums.com/ssd5/SSD5%20User%20Manual.pdf)
- [SSD5.5](https://stevenslatedrums.com/ssd5)

**BFD**

- [BFD3 Interface](https://internal.bfddrums.com/manuals/BFD30/using_bfd3.htm)
- [BFD3 Groove Page](https://internal.bfddrums.com/manuals/BFD30/grooves_page.htm)
- [BFD Groove Export](https://support.bfddrums.com/en/support/solutions/articles/69000871285-bfd3-how-to-export-a-groove-and-load-it-into-your-daw)
- [BFD3 Product](https://www.bfddrums.com/bfd3/)

**GetGood Drums**

- [Modern & Massive 2 Support](https://support.ggd.co/hc/en-us/categories/31443955854103-Modern-Massive-2)
- [Individual Kit Pieces and Mics](https://support.ggd.co/hc/en-us/articles/31476648207127-Adjust-the-volume-of-individual-kit-pieces-and-mics)
- [Preview Grooves](https://support.ggd.co/hc/en-us/articles/31476662575767-Preview-grooves)
- [Use Grooves in Your Song](https://support.ggd.co/hc/en-us/articles/31476645988247-Use-grooves-in-your-song)

**Ugritone**

- [RIOT Drums 2](https://ugritone.com/products/riot-drums-2)
- [Quick Start](https://support.ugritone.com/portal/en/kb/articles/how-to-use-ugritone-drums-in-a-daw)

**IK Multimedia**

- [MODO Drum — info](https://www.ikmultimedia.com/products/mododrum/index.php?p=info)
- [MODO Drum — specs](https://www.ikmultimedia.com/products/mododrum/index.php?p=specs)

**Native Instruments**

- [Abbey Road Modern Drummer](https://www.native-instruments.com/products/abbey-road-modern-drummer/?tab=mixer-section)
- [Kontakt 8 Scaling Discussion](https://community.native-instruments.com/discussion/46800/reszie-or-scale-kontakt-instrument-editor)

**Apple**

- [Drum Kit Designer](https://support.apple.com/en-gb/guide/logicpro/lgsi825fc878/mac)
- [Drum Kit Designer Overview](https://support.apple.com/guide/logicpro/drum-kit-designer-overview-lgsi2fb2509e/10.7/mac/11.0)
- [Knobs / Interactive Parameters](https://help.apple.com/logicpro/mac/10.1/en.lproj/lgcpafa3d1a9.html)
- [Session Player Editor](https://support.apple.com/guide/logicpro/the-session-player-editor-lgcp32d55998/mac)

### Пользовательские обсуждения

**Reddit**

- [Superior Drummer vs EZdrummer / Audioengineering](https://www.reddit.com/r/audioengineering/comments/1ranym4/superior_drummer_vs_ezdrummer_with_routing/)
- [EZdrummer / Mapping / Edrums](https://www.reddit.com/r/edrums/comments/1ae3b28)
- [SSD5.5 in Cubase / Edrums](https://www.reddit.com/r/edrums/comments/1rzsxw0/ssd5_trouble_midi_mapping/)
- [SSD5.5 Hi-Hat CC / Edrums](https://www.reddit.com/r/edrums/comments/1hz4fiq)
- [SSD5 Mapping / Drums](https://www.reddit.com/r/drums/comments/1665h6c)
- [SSD5.5 Configuration](https://www.reddit.com/r/edrums/comments/1kyvitk)

**KVR**

- [BFD3.5 Frustrating](https://www.kvraudio.com/forum/viewtopic.php?t=623347)
- [Addictive Drums 2.5 UI Update Discussion](https://www.kvraudio.com/forum/viewtopic.php?start=240&t=409423)

**Gearspace**

- [BFD3 User Reviews](https://gearspace.com/rms/bfd-drums-bfd3.68752/user-reviews)
- [BFD3.5 Discussion](https://gearspace.com/threads/bfd-drums-releases-bfd-3-5.1444020/)
- [BFD3.5 Discussion, page 2](https://gearspace.com/threads/bfd-drums-releases-bfd-3-5.1444020/page-2)

**Steinberg Forums**

- [SD3 Drum Map](https://forums.steinberg.net/t/superior-drummer-3-drum-map/134328)
- [SD3 Multichannel Output Labels](https://forums.steinberg.net/t/need-help-with-sd3-vsti-multichannel-out-labels/1042999)

**Русскоязычный форум**

- [RMmedia — BFD3](https://rmmedia.ru/threads/105984/)

### Технические источники

- [Steinberg — VST3 Resize View Call Sequence](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Workflow%2BDiagrams/Resize%2BView%2BCall%2BSequence.html)
- [Steinberg — VSTGUI Editor API](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_v_s_t3_editor.html)
- [Steinberg — HiDPI / Custom Scaling in Cubase and Nuendo](https://helpcenter.steinberg.de/hc/it/articles/32061688285842-Windows-Custom-scaling-option-in-Cubase-and-Nuendo)

---

**Ограничение исследования:** анализ основан на опубликованной документации и отдельных пользовательских свидетельствах, а не на независимом тестировании всех продуктов на одинаковом оборудовании. Размеры окна, работу multi-out, корректность масштабирования и скорость отрисовки следует проверять на прототипе в Cubase.
