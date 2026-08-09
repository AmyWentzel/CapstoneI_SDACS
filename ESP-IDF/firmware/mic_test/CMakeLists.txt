# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.24)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(mic_test)

set(IDF_TARGET esp32s3)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)



