cmake_minimum_required(VERSION 3.20)
project(splitscreen_support LANGUAGES C CXX)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure with MSVC and -A Win32")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CTest)
if(BUILD_TESTING)
    foreach(fixture guest input safearea)
        add_executable(${fixture}_abi_test tests/${fixture}_abi.cpp)
        target_compile_features(${fixture}_abi_test PRIVATE cxx_std_17)
        target_include_directories(${fixture}_abi_test PRIVATE include)
        # The fixtures run without the game: addresses are the researched build's constants.
        target_compile_definitions(${fixture}_abi_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS SS_FIXED_ADDRESSES)
        # The fixtures use assert to check ABI and rollback behavior. Preserve
        # those checks even when building the optimized release DLL.
        target_compile_options(${fixture}_abi_test PRIVATE /W4 /UNDEBUG /Od)
        target_compile_options(${fixture}_abi_test PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/tests/assertions_enabled.h")
        add_test(NAME ${fixture}_abi COMMAND ${fixture}_abi_test)
    endforeach()
endif()
add_library(splitscreen_support SHARED exports.def
    src/plugin.cpp src/hooks.cpp src/settings.cpp src/addresses.cpp src/signatures.cpp
    src/guest.cpp src/lobby.cpp src/input.cpp src/devices.cpp src/join.cpp
    src/identity.cpp src/presentation.cpp src/safearea.cpp
    src/hud_inventory.cpp src/hud_prompts.cpp
    vendor/minhook/src/buffer.c vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c vendor/minhook/src/hde/hde32.c)
target_compile_features(splitscreen_support PRIVATE cxx_std_17)
# include/ holds the tables of the supported game build; "." reaches vendor/.
target_include_directories(splitscreen_support PRIVATE include .)
target_compile_definitions(splitscreen_support PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    SS_NATIVE_GUEST_LOBBY=1 SS_NATIVE_IDENTITY=1 SS_NATIVE_PRESENTATION=1 SS_NATIVE_SAFEAREA=1
    SS_NATIVE_HUD_INVENTORY=1 SS_NATIVE_HUD_PROMPTS=1)
target_compile_options(splitscreen_support PRIVATE /W4)
target_link_libraries(splitscreen_support PRIVATE bcrypt shell32)

# Tells the launcher whether the plugin finds its addresses in a game folder.
add_executable(splitscreen_check src/check.cpp src/signatures.cpp)
target_compile_features(splitscreen_check PRIVATE cxx_std_17)
target_include_directories(splitscreen_check PRIVATE include)
target_compile_definitions(splitscreen_check PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(splitscreen_check PRIVATE /W4)
