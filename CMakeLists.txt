cmake_minimum_required(VERSION 3.10)
project(weather-life C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Detect platform
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM "WINDOWS")
    set(SOURCES 
        src/usb_device.h
        src/usb_windows.c
        src/usb_common.c
    )
    set(LIBS setupapi winusb)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM "LINUX")
    set(SOURCES
        src/usb_device.h
        src/usb_linux.c
        src/usb_common.c
    )
    find_package(libusb-1.0 REQUIRED)
    set(LIBS ${LIBUSB_LIBRARIES})
    include_directories(${LIBUSB_INCLUDE_DIRS})
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM "MACOS")
    set(SOURCES
        src/usb_device.h
        src/usb_macos.c
        src/usb_common.c
    )
    set(LIBS
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# Main library
add_library(weather-life-usb ${SOURCES})
target_link_libraries(weather-life-usb ${LIBS})

# Example executable
add_executable(weather-life-discover src/main_discover.c)
target_link_libraries(weather-life-discover weather-life-usb)

# Weather provider library (to be added)
# add_subdirectory(src/weather)

# Installation
install(TARGETS weather-life-discover DESTINATION bin)
install(TARGETS weather-life-usb DESTINATION lib)
install(FILES src/usb_device.h DESTINATION include)

message(STATUS "Building for platform: ${PLATFORM}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
