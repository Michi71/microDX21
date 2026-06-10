cmake_minimum_required(VERSION 3.10)

project(microDX21 VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-constant-logical-operand")

find_package(SDL2 REQUIRED)

find_path(PORTMIDI_INCLUDE_DIR portmidi.h)
find_library(PORTMIDI_LIBRARY portmidi)

if (NOT PORTMIDI_INCLUDE_DIR OR NOT PORTMIDI_LIBRARY)
    message(FATAL_ERROR "portmidi library not found")
endif()

include_directories(${SDL2_INCLUDE_DIRS} ${PORTMIDI_INCLUDE_DIR})

add_subdirectory(src/opm)

add_executable(standalone test/standalone.cpp)

target_link_libraries(standalone ${SDL2_LIBRARIES} ${PORTMIDI_LIBRARY} opmemu)

target_include_directories(standalone PRIVATE test)

# Algorithm prober: per-CON OPM topology test. Outputs raw PCM files
# that are analyzed externally. No SDL/PortMidi needed.
add_executable(alg_prober tools/alg_prober.cpp)
target_link_libraries(alg_prober opmemu)

# Voice-stealing unit test. No SDL/PortMidi needed.
enable_testing()
add_executable(test_voice_stealing tests/test_voice_stealing.cpp)
target_include_directories(test_voice_stealing PRIVATE src)
target_link_libraries(test_voice_stealing opmemu)

add_executable(test_velocity_curve tests/test_velocity_curve.cpp)
target_include_directories(test_velocity_curve PRIVATE src)
target_link_libraries(test_velocity_curve opmemu)
add_test(NAME velocity_curve COMMAND test_velocity_curve)

add_executable(test_portamento tests/test_portamento.cpp)
target_include_directories(test_portamento PRIVATE src)
target_link_libraries(test_portamento opmemu)
add_test(NAME portamento COMMAND test_portamento)

add_test(NAME voice_stealing COMMAND test_voice_stealing)
