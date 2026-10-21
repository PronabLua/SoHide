cmake_minimum_required(VERSION 3.22)

project(SoHide VERSION 1.0.0 LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "SoHide supports Android only")
endif()

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "SoHide supports arm64-v8a only")
endif()

option(SoHideBuildProof "Build the native CTF proof runner and payload" ON)

add_library(SoHide STATIC
    src/ElfFile.cpp
    src/LinkerList.cpp
    src/MemoryMaps.cpp
    src/SoHide.cpp
)
add_library(SoHide::SoHide ALIAS SoHide)
set_target_properties(SoHide PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(SoHide
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(SoHide PUBLIC cxx_std_20)
target_compile_definitions(SoHide PRIVATE _GNU_SOURCE $<$<CONFIG:Debug>:SoHideDiagnostics>)
target_compile_options(SoHide PRIVATE -Wall -Wextra -Werror -fvisibility=hidden -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)
target_link_libraries(SoHide PUBLIC dl log)

install(TARGETS SoHide ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

if(SoHideBuildProof)
    add_library(SoHidePublicApiSmoke OBJECT proof/PublicApiSmoke.c)
    target_include_directories(SoHidePublicApiSmoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(SoHidePublicApiSmoke PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)

    add_library(SoHideProofPayload SHARED proof/Payload.cpp)
    target_link_libraries(SoHideProofPayload PRIVATE SoHide log dl)
    target_compile_options(SoHideProofPayload PRIVATE -Wall -Wextra -Werror -fvisibility=hidden -fno-exceptions -fno-rtti)
    target_link_options(SoHideProofPayload PRIVATE -Wl,-z,now -Wl,--gc-sections)

    add_executable(SoHideProofRunner proof/Runner.cpp)
    target_include_directories(SoHideProofRunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(SoHideProofRunner PRIVATE dl)
    target_compile_options(SoHideProofRunner PRIVATE -Wall -Wextra -Werror -fvisibility=hidden -fno-exceptions -fno-rtti)
    target_link_options(SoHideProofRunner PRIVATE -Wl,-z,now -Wl,--gc-sections)
endif()
