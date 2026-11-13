# The PC port (docs/PORT.md; DECISIONS "PC port architecture"): the game's C units, its adapter (port/game/: the
# probes, the mods, the asm data stand-ins; game.json) and psxstack (the runtime, the Psy-Q shim, the build) linked
# into one host binary, build/port/dw2003.
#
#   cmake -S port -B build/port -G Ninja && cmake --build build/port && build/port/dw2003 --max-frames 60
#
# psxstack is the submodule at ../psxstack (PSXSTACK_DIR; scripts/worktree_init.sh checks it out at the pin, and
# offline links the sibling clone there instead); tools/port_inputs.py writes the
# build inputs psxstack_add_game() takes (build/port/gen/inputs/: the units, the overlays, the tag sites, the
# volatile ranges) from the tracked sources at configure time. The PS1 build (configure.py, build.ninja) is untouched.
cmake_minimum_required(VERSION 3.20)
project(dw2003port C)

get_filename_component(DW3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PSXSTACK_DIR "${DW3_ROOT}/psxstack" CACHE PATH "The psxstack checkout (the submodule; or another checkout of the stack)")
if(NOT EXISTS "${PSXSTACK_DIR}/cmake/psxstack.cmake")
    message(FATAL_ERROR "psxstack not found at ${PSXSTACK_DIR}: the submodule is not checked out (git submodule update "
                        "--init psxstack, or scripts/worktree_init.sh; or -DPSXSTACK_DIR=<path>)")
endif()
if(EXISTS "${DW3_ROOT}/tools/venv/bin/python" AND NOT PSXSTACK_PYTHON)
    set(PSXSTACK_PYTHON "${DW3_ROOT}/tools/venv/bin/python" CACHE FILEPATH "" FORCE)
elseif(NOT PSXSTACK_PYTHON)
    find_program(PSXSTACK_PYTHON NAMES python3 python REQUIRED)
endif()
# The built tools (SDL3, DXC) are this checkout's tools/ (the main checkout's in a worktree, linked there).
if(NOT PSXSTACK_TOOLS_DIR)
    set(PSXSTACK_TOOLS_DIR "${DW3_ROOT}/tools" CACHE PATH "" FORCE)
endif()
set(PSXSTACK_VERSION_ROOT "${DW3_ROOT}" CACHE PATH "" FORCE)   # the game's tags name the build
include("${PSXSTACK_DIR}/cmake/psxstack.cmake")

# ---- The build inputs (tools/port_inputs.py), regenerated when their sources change.
set(DW3_INPUTS "${CMAKE_BINARY_DIR}/gen/inputs")
execute_process(COMMAND "${PSXSTACK_PYTHON}" "${DW3_ROOT}/tools/port_inputs.py" --out "${DW3_INPUTS}"
                RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "tools/port_inputs.py failed:\n${out}${err}")
endif()
string(STRIP "${out}" out)
message(STATUS "${out}")
file(GLOB DW3_CONFIG_DEPS "${DW3_ROOT}/config/*.symbols.txt" "${DW3_ROOT}/config/wstag/*.symbols.txt")
file(GLOB DW3_ADAPTER_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/game/*.c")

# -O2 for the game's own C crashes at boot here: sound_init -> sound_update_loading -> cdload_get_subfile, SIGSEGV,
# reproducible (three runs out of three, and a core dump with that stack). Under -O0 it does not, so the decompilation
# has undefined behaviour the optimiser is entitled to exploit. The game's objects stay at -O0 until it is found.
# if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch")
#     set(PSXSTACK_GAME_OPT -O2)
# endif()
# A Release build (the release's packages: scripts/package_appimage.sh, scripts/package_windows.sh) would compile
# them at the build type's -O3: they stay at -O0 there too, the level the tests and the handheld run them at (the
# runtime, the shim and the renderers keep their -O3). -DPSXSTACK_GAME_OPT= (empty) gives the game -O3 back.
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT DEFINED PSXSTACK_GAME_OPT)
    set(PSXSTACK_GAME_OPT -O0)
endif()

psxstack_add_game(dw2003
    GAME_JSON "${CMAKE_CURRENT_SOURCE_DIR}/game/game.json"
    UNITS "${DW3_INPUTS}/units.txt"
    MAIN_UNIT "${DW3_ROOT}/src/main/main.c"
    OVERLAYS "${DW3_INPUTS}/overlays.txt"
    TAG_SITES "${DW3_INPUTS}/tag_sites.txt"
    EXE_SYMBOLS "${DW3_ROOT}/config/symbol_addrs.txt"
    VOLATILE "${DW3_INPUTS}/volatile.txt"
    GTEMAC "${DW3_ROOT}/include/psyq/gtemac.h"
    INCLUDE_DIRS "${DW3_ROOT}/include" "${DW3_ROOT}"
    ADAPTER ${DW3_ADAPTER_SRCS}
    MODS_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/mods"
    DEFINES NON_MATCHING
    CONFIGURE_DEPENDS "${DW3_ROOT}/tools/port_inputs.py" "${DW3_ROOT}/config/wstag.txt" "${DW3_ROOT}/config/wstag_c.txt"
                      "${DW3_ROOT}/src/main/overlay.c" "${DW3_ROOT}/src/fieldstg/fieldstg_80087DB0.c"
                      "${DW3_ROOT}/tests/replay/replay.py" ${DW3_CONFIG_DEPS})

# ---- The pinned psxstack (dw3-handheld) does not build the PC targets as it is: runtime/video.c's `video_gles` is
# unused outside the handheld's GLES build (the runtime has -Werror), and on Windows runtime/fbdev_present.c (the
# handheld's presenter, which the runtime's glob puts in every build) and runtime/main.c's <sys/syscall.h> have no
# headers. These lines let the headless, SDL3 and Windows builds through with the pin as it is; the handheld's build
# (PSXSTACK_SDL2 and PSXSTACK_EGL on) is untouched. The fix belongs in the stack (the variable under PSXSTACK_EGL,
# fbdev_present.c and render_gles.c out of the glob, <sys/syscall.h> on Linux only); once the pin has it this block
# can go, and until then it changes nothing else.
if(NOT PSXSTACK_EGL)
    set_property(SOURCE "${PSXSTACK_ROOT}/runtime/video.c" APPEND PROPERTY COMPILE_OPTIONS -Wno-error=unused-variable)
endif()
if(NOT PSXSTACK_SDL2)
    set_source_files_properties("${PSXSTACK_ROOT}/runtime/fbdev_present.c" PROPERTIES HEADER_FILE_ONLY ON)
endif()
if(WIN32)
    file(WRITE "${CMAKE_BINARY_DIR}/gen/win32/sys/syscall.h" "/* Empty: runtime/main.c uses it on Linux only. */\n")
    set_property(SOURCE "${PSXSTACK_ROOT}/runtime/main.c" APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/gen/win32")
endif()
