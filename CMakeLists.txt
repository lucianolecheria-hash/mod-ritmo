cmake_minimum_required(VERSION 3.21)

# Geode v5 exige C++23 (v3 y v4 usaban C++20).
# Si algún día volvieras a compilar para Geode v3/v4, bájalo a 20.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Solo afecta a macOS; se deja igual que el template oficial por compatibilidad.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

project(RhythmPracticeBar VERSION 1.0.0)

# Binario del mod. Añade aquí más .cpp si los creas.
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

# El SDK lo instala el CLI de Geode (en GitHub Actions lo hace la action) y
# expone su ruta en la variable de entorno GEODE_SDK.
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Enlaza Geode, lee mod.json y empaqueta el .geode.
setup_geode_mod(${PROJECT_NAME})
