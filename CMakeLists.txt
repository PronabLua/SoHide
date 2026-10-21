cmake_minimum_required(VERSION 3.22.1)

project(HideSo VERSION 1.1.0 LANGUAGES CXX)

if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "HideSo requires Android ARM64")
endif()

option(HideSoBuildCompileTest "Compile the public API integration test" ON)

add_library(HideSo STATIC
    src/ElfFile.cpp
    src/HideSo.cpp
    src/LinkerList.cpp
    src/MemoryMaps.cpp
)
add_library(HideSo::HideSo ALIAS HideSo)

set_target_properties(HideSo PROPERTIES ARCHIVE_OUTPUT_NAME hideso PREFIX "" POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

target_include_directories(HideSo
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(HideSo PUBLIC cxx_std_20)
target_compile_definitions(HideSo PRIVATE _GNU_SOURCE)
target_compile_options(HideSo PRIVATE -Wall -Wextra -Werror -fvisibility=hidden -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)
target_link_options(HideSo INTERFACE "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")

find_library(DynamicLinkerLibrary dl REQUIRED)
target_link_libraries(HideSo INTERFACE ${DynamicLinkerLibrary})

if(HideSoBuildCompileTest)
    add_library(HideSoPublicApiCompileTest OBJECT tests/PublicApiSmoke.cpp)
    target_link_libraries(HideSoPublicApiCompileTest PRIVATE HideSo::HideSo)
    target_compile_options(HideSoPublicApiCompileTest PRIVATE -Wall -Wextra -Werror -fvisibility=hidden -fno-exceptions -fno-rtti)
endif()

install(TARGETS HideSo ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
