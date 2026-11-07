cmake_minimum_required(VERSION 3.21)
 
# Geode v3 y v4 exigen C++20.
# (Si algún día migras a Geode v5 tendrás que subir esto a 23.)
set(CMAKE_CXX_STANDARD 20)
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
 
