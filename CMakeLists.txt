cmake_minimum_required(VERSION 3.25)
project(Samplit VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Статический рантайм MSVC: на чужой машине msvcp140.dll может быть старее той,
# с которой собран плагин, и std::mutex падает при загрузке в DAW
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Тестовый хост JUCE: открывает плагин без DAW, удобно для быстрой проверки
option(SAMPLIT_BUILD_HOST "Build JUCE AudioPluginHost" OFF)
option(SAMPLIT_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(libs/JUCE)
add_subdirectory(core)
add_subdirectory(tools/packer)
add_subdirectory(plugin)

if(SAMPLIT_BUILD_HOST)
    add_subdirectory(libs/JUCE/extras/AudioPluginHost ${CMAKE_BINARY_DIR}/AudioPluginHost)
endif()

if(SAMPLIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tools/snapshot)
endif()
