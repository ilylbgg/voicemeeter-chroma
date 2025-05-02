cmake_minimum_required(VERSION 3.30)

# --------------- #
# Project defines #
# --------------- #

project(vmtheme)
set(CMAKE_CXX_STANDARD 17)
set(TARGET_DETOURS lib_detours)
set(TARGET_VMHOOK vmtheme)
set(TARGET_LAUNCHER launcher)

if (DEFINE_FLAVOR_BANANA)
    set(FLAVOR_POSTFIX "banana")
elseif(DEFINE_FLAVOR_POTATO)
    set(FLAVOR_POSTFIX "potato")
elseif(DEFINE_FLAVOR_DEFAULT)
    set(FLAVOR_POSTFIX "default")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_POSTFIX "64")
else()
    set(ARCH_POSTFIX "32")
endif()

# ---------------- #
# Compiler defines #
# ---------------- #

add_compile_definitions(
        VMTHEME_VERSION="vmtheme 0.0.1"
        UNICODE
        FLAVOR_POSTFIX="${FLAVOR_POSTFIX}"
        ARCH_POSTFIX="${ARCH_POSTFIX}"
)

if(DEFINE_FLAVOR_BANANA)
    add_compile_definitions(FLAVOR_BANANA)
elseif (DEFINE_FLAVOR_POTATO)
    add_compile_definitions(FLAVOR_POTATO)
elseif(DEFINE_FLAVOR_DEFAULT)
    add_compile_definitions(FLAVOR_DEFAULT)
endif()

# --------------------------- #
# External: Microsoft Detours #
# --------------------------- #

include(FetchContent)
FetchContent_Declare(
        detours
        URL https://github.com/microsoft/Detours/archive/refs/tags/v4.0.1.tar.gz
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/detours
)
FetchContent_MakeAvailable(detours)
set(DETOURS_SOURCE ${CMAKE_SOURCE_DIR}/external/detours/src)
add_library(${TARGET_DETOURS} STATIC
        ${DETOURS_SOURCE}/creatwth.cpp
        ${DETOURS_SOURCE}/detours.cpp
        ${DETOURS_SOURCE}/detours.h
        ${DETOURS_SOURCE}/detver.h
        ${DETOURS_SOURCE}/disasm.cpp
        ${DETOURS_SOURCE}/disolarm.cpp
        ${DETOURS_SOURCE}/disolarm64.cpp
        ${DETOURS_SOURCE}/disolia64.cpp
        ${DETOURS_SOURCE}/disolx64.cpp
        ${DETOURS_SOURCE}/disolx86.cpp
        ${DETOURS_SOURCE}/image.cpp
        ${DETOURS_SOURCE}/modules.cpp
        ${DETOURS_SOURCE}/uimports.cpp
)
set_property(SOURCE ${DETOURS_SOURCE}/uimports.cpp APPEND PROPERTY HEADER_FILE_ONLY true)
target_compile_options(${TARGET_DETOURS} PRIVATE /W4 /WX /Zi /MT /Gy /Gm- /Zl /Od)
target_include_directories(${TARGET_DETOURS} PUBLIC ${DETOURS_SOURCE})

# ----------------------- #
# External: nlohmann JSON #
# ----------------------- #

include(FetchContent)
FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/json
)
FetchContent_MakeAvailable(json)

# -------------------------- #
# Target: vmtheme[32|64].dll #
# -------------------------- #

add_library(${TARGET_VMHOOK} SHARED src/vmtheme.cpp src/shared.h src/shared.cpp)
target_include_directories(${TARGET_VMHOOK} PUBLIC ${DETOURS_SOURCE})
target_link_libraries(${TARGET_VMHOOK} PRIVATE
        lib_detours
        nlohmann_json::nlohmann_json
        Shlwapi
)
set_target_properties(${TARGET_VMHOOK} PROPERTIES
        LINK_FLAGS "/EXPORT:DetourFinishHelperProcess,@1"
        OUTPUT_NAME "vmtheme${ARCH_POSTFIX}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# --------------------------------------------------- #
# Target: vmtheme[32|64]-[default|banana|potato].exe  #
# --------------------------------------------------- #

add_executable(${TARGET_LAUNCHER} src/launcher.cpp src/shared.h src/shared.cpp)
target_link_libraries(${TARGET_LAUNCHER} PRIVATE
        lib_detours
        Pathcch
)
set_target_properties(${TARGET_LAUNCHER} PROPERTIES
        OUTPUT_NAME "vmtheme${ARCH_POSTFIX}-${FLAVOR_POSTFIX}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        LINK_FLAGS /SUBSYSTEM:WINDOWS
)
