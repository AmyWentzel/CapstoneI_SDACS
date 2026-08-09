cmake_minimum_required(VERSION 3.16)
project(sdacs_ei_runner LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../models/sdacs_v3_v1")
set(SDK_DIR "${MODEL_DIR}/edge-impulse-sdk")

file(GLOB_RECURSE SDK_SOURCES CONFIGURE_DEPENDS
    "${SDK_DIR}/classifier/*.c"
    "${SDK_DIR}/classifier/*.cc"
    "${SDK_DIR}/classifier/*.cpp"
    "${SDK_DIR}/dsp/*.c"
    "${SDK_DIR}/dsp/*.cc"
    "${SDK_DIR}/dsp/*.cpp"
    "${SDK_DIR}/tensorflow/lite/*.c"
    "${SDK_DIR}/tensorflow/lite/*.cc"
    "${SDK_DIR}/tensorflow/lite/*.cpp"
    "${SDK_DIR}/CMSIS/DSP/Source/*.c"
    "${SDK_DIR}/porting/posix/*.cpp"
    "${MODEL_DIR}/tflite-model/*.cpp"
)
list(FILTER SDK_SOURCES EXCLUDE REGEX "/(test|tests|examples?)/")

add_executable(sdacs_ei_runner main.cpp ${SDK_SOURCES})
target_compile_definitions(sdacs_ei_runner PRIVATE
    EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=0
    EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=0
    EI_PORTING_POSIX=1
)
target_include_directories(sdacs_ei_runner PRIVATE
    "${MODEL_DIR}"
    "${SDK_DIR}"
    "${SDK_DIR}/tensorflow"
    "${SDK_DIR}/third_party/flatbuffers/include"
    "${SDK_DIR}/third_party/gemmlowp"
    "${SDK_DIR}/third_party/ruy"
    "${SDK_DIR}/CMSIS/DSP/Include"
)
find_package(Threads REQUIRED)
target_link_libraries(sdacs_ei_runner PRIVATE Threads::Threads m)
set_target_properties(sdacs_ei_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin"
)
