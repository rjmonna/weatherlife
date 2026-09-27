cmake_minimum_required(VERSION 3.10)
project(weather-life C)

include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Detect platform
set(COMMON_SOURCES
    src/usb_device.h
    src/usb_common.c
    src/weather_response.h
    src/weather_response.c
    src/device_registration.h
    src/device_registration.c
    src/bit_packer.h
    src/bit_packer.c
    src/weather_frame.h
    src/weather_frame.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM "WINDOWS")
    set(SOURCES ${COMMON_SOURCES} src/usb_windows.c)
    set(LIBS setupapi hid)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM "LINUX")
    set(SOURCES ${COMMON_SOURCES} src/hid_transport.c src/usb_linux.c)
    find_path(HIDAPI_INCLUDE_DIR hidapi/hidapi.h REQUIRED)
    find_library(HIDAPI_LIBRARY hidapi REQUIRED)
    set(LIBS ${HIDAPI_LIBRARY})
    include_directories(${HIDAPI_INCLUDE_DIR})
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM "MACOS")
    set(SOURCES ${COMMON_SOURCES} src/hid_transport.c src/usb_macos.c)
    find_path(HIDAPI_INCLUDE_DIR hidapi/hidapi.h REQUIRED)
    find_library(HIDAPI_LIBRARY hidapi REQUIRED)
    set(LIBS ${HIDAPI_LIBRARY})
endif()

# Main library
add_library(weather-life-usb ${SOURCES})
target_link_libraries(weather-life-usb ${LIBS})

# Example executable
add_executable(weather-life-discover src/main_discover.c)
target_link_libraries(weather-life-discover weather-life-usb)

if(BUILD_TESTING)
    add_executable(weather-life-native-tests tests/test_native.c)
    target_include_directories(weather-life-native-tests PRIVATE src)
    target_link_libraries(weather-life-native-tests weather-life-usb)
    if(NOT WIN32)
        target_link_libraries(weather-life-native-tests m)
    endif()
    add_test(NAME native-unit-tests COMMAND weather-life-native-tests)
    add_test(NAME python-unit-tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_python.py)
    set_tests_properties(python-unit-tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Weather provider library (to be added)
# add_subdirectory(src/weather)

# Installation
install(TARGETS weather-life-discover DESTINATION bin)
install(TARGETS weather-life-usb DESTINATION lib)
install(FILES src/usb_device.h src/weather_response.h src/device_registration.h src/bit_packer.h src/weather_frame.h DESTINATION include)

message(STATUS "Building for platform: ${PLATFORM}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
