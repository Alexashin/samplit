include(FetchContent)

# SQLite берём официальной amalgamation (один .c), хеш SHA3-256 с sqlite.org/download.html
FetchContent_Declare(sqlite_amalgamation
    URL      https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
    URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite_amalgamation)

add_library(sqlite3 STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Monocypher: шифрование блоков пакетов (XChaCha20-Poly1305) и подпись токена лицензии (Ed25519 на SHA-512,
# совместима со стандартными библиотеками, в том числе на сервере). Тарболл зафиксирован хешем
FetchContent_Declare(monocypher
    URL      https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.2.tar.gz
    URL_HASH SHA256=BC1CA30B1B2654E4E7DAF2492C0D204200E55137F23FDA6B7142FD7D523BD6B4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(monocypher)

add_library(monocypher STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC ${monocypher_SOURCE_DIR}/src ${monocypher_SOURCE_DIR}/src/optional)
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Ядро без графики. Модули JUCE (juce_core, juce_audio_formats) подключаются как к обычной
# статической библиотеке: код модулей компилируется в неё, а потребители (плагин, тесты) линкуют те же
# модули и получают свои копии; компоновщик берёт символы из первого определения, дубликатов нет.
add_library(samplit_core STATIC
    include/samplit/core/Library.h
    src/Library.cpp
    include/samplit/core/Manifest.h
    src/Manifest.cpp
    include/samplit/core/Pack.h
    src/Pack.cpp
    src/PackRandom.cpp
    include/samplit/core/Wav.h
    src/Wav.cpp
    include/samplit/core/License.h
    src/License.cpp
    src/LicenseKey.cpp
    include/samplit/core/LicenseClient.h
    src/LicenseClient.cpp)
set_target_properties(samplit_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(samplit_core PUBLIC include)
target_compile_options(samplit_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(samplit_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(samplit_core
    PUBLIC
        sqlite3
        monocypher
        juce::juce_core
        juce::juce_audio_formats
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Генератор случайных чисел ОС (PackRandom.cpp)
if(WIN32)
    target_link_libraries(samplit_core PRIVATE bcrypt)
endif()
