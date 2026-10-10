# Nintendo 3DS (Old 3DS, 64 MB) native build.
#
# Included from the top-level CMakeLists.txt when CTR is set; it replaces the
# whole desktop dependency/target setup. The 3DS executable links no
# OpenSceneGraph (only the math-only "osg-lite"), no Lua/sol3, ICU, FFmpeg,
# OpenAL, SDL, Recast/Detour or SQLite. Third-party libraries come from
# devkitPro portlibs and from ${CTR_DEPS_PREFIX} (scripts/ctr-deps/build-all.sh).

set(CTR_DEPS_PREFIX "/opt/ctr-deps" CACHE PATH "Prefix of the 3DS third-party builds (scripts/ctr-deps)")
set(CTR_PORTLIBS "$ENV{DEVKITPRO}/portlibs/3ds")
set(CTR_LIBCTRU "$ENV{DEVKITPRO}/libctru")

set(OPENMW_NO_LUA ON)
set(BUILD_COMPONENTS_TESTS OFF)
set(BUILD_OPENMW_TESTS OFF)
set(BUILD_BENCHMARKS OFF)

add_compile_options(-ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden
    -Wall -Wextra -Wundef -Wno-unused-parameter -Wnon-virtual-dtor -Wno-psabi)
add_link_options(-Wl,--gc-sections)
# std::thread/pthread placement wrapper (apps/openmw/ctr/CtrThreads.cpp).
add_link_options(-Wl,--wrap=__syscall_thread_create)
# Reads through corrupted file handles fail instead of aborting (apps/openmw/ctr/ctr_iocheck.c).
add_link_options(-Wl,--wrap=_read_r)

add_compile_definitions(
    _DEFAULT_SOURCE
    OPENMW_NO_LUA
    OPENMW_CTR_NATIVE
    OSG_LIBRARY_STATIC
    OT_LIBRARY_STATIC
    MYGUI_STATIC
    BOOST_ALL_NO_LIB
)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

include_directories(BEFORE "${CMAKE_SOURCE_DIR}/components/ctr")
include_directories(BEFORE SYSTEM
    "${CTR_DEPS_PREFIX}/include"
    "${CTR_DEPS_PREFIX}/include/bullet"
    "${CTR_DEPS_PREFIX}/include/SDL2"
    "${CTR_DEPS_PREFIX}/include/MYGUI"
    "${CTR_DEPS_PREFIX}/include/recastnavigation"
    "${CTR_PORTLIBS}/include"
    "${CTR_PORTLIBS}/include/freetype2"
    "${CTR_LIBCTRU}/include"
)
include_directories(".")
link_directories("${CTR_DEPS_PREFIX}/lib" "${CTR_PORTLIBS}/lib" "${CTR_LIBCTRU}/lib")

set(OPENMW_RESOURCES_ROOT ${OpenMW_BINARY_DIR})
set(OPENMW_RESOURCE_FILES "resources" CACHE PATH "location of OpenMW resources files")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OpenMW_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OpenMW_BINARY_DIR}")

pack_resource_file(${OpenMW_SOURCE_DIR}/files/settings-default.cfg "${OpenMW_BINARY_DIR}" "defaults.bin")

# The engine keeps SDL's types (components/ctr/sdlshim implements the calls);
# nothing is linked for SDL itself.
add_library(SDL2::SDL2 INTERFACE IMPORTED)

add_subdirectory(extern/oics)
add_subdirectory(extern/Base64)
add_subdirectory(extern/smhasher)
add_subdirectory(components)
add_subdirectory(apps/openmw)

# Engine resources (MyGUI layouts and skins, fonts, l10n, builtin textures) — packed into the romfs by
# scripts/ctr/make-cia.sh. No shaders and no Lua API: the 3DS build has neither.
add_subdirectory(files/data)
add_subdirectory(files/data-mw)
