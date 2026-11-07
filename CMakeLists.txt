cmake_minimum_required(VERSION 3.24)
project(mod-ritmo)
find_package(Geode REQUIRED)
add_geode_mod(${PROJECT_NAME} FILES src/main.cpp)
