# Wrapper toolchain for Nintendo 3DS (CTR) cross-compilation
# Includes the devkitPro 3DS toolchain and adds OpenMW-specific settings.
# Mirrors cmake/VitaToolchain.cmake.

if(NOT DEFINED ENV{DEVKITPRO})
    set(ENV{DEVKITPRO} "/opt/devkitpro")
    message(STATUS "DEVKITPRO not set, defaulting to /opt/devkitpro")
endif()

include("$ENV{DEVKITPRO}/cmake/3DS.cmake")

# Marker for OpenMW's CMakeLists (NINTENDO_3DS is set by the platform module,
# but only after project(); CTR is available immediately).
set(CTR TRUE CACHE BOOL "" FORCE)

# dkP sets CMAKE_FIND_PACKAGE_PREFER_CONFIG=TRUE, which makes plain
# find_package(Freetype) resolve to a config that only sets Freetype_FOUND —
# MyGUI (and CMake's own conventions) expect module-mode FREETYPE_* variables.
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG FALSE)

# newlib types int32_t as `long int` (via GCC's __INT32_TYPE__). ICU — and a
# lot of code in general — assumes int32_t==int; on newlib that breaks
# overload resolution and `override` matching everywhere. Redefine the
# compiler macro so <stdint.h> yields int32_t=int / uint32_t=unsigned int
# (same width/ABI, only the C++ mangling and overload identity change).
# All C portlibs are unaffected (no mangling in C); all C++ deps are built
# inside this tree with the same flags.
#
# dkP injects arch/common flags via CMAKE_<LANG>_FLAGS_INIT from the platform
# module at project() time — but _INIT is ignored once a CMAKE_<LANG>_FLAGS
# cache entry exists, and we must put the int32 fix into that cache. So on a
# fresh tree spell out the FULL flag set (keep in sync with dkP's
# Platform/Nintendo3DS.cmake); on an already-configured tree leave the cache
# byte-identical to avoid a spurious full rebuild.
set(_CTR_ARCH "-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft")
set(_CTR_COMMON "-mword-relocations -ffunction-sections -D__3DS__")
set(_CTR_INT32_FIX "-U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__")
macro(_ctr_ensure_flags lang)
    string(FIND "${CMAKE_${lang}_FLAGS}" "armv6k" _ctr_has_arch)
    string(FIND "${CMAKE_${lang}_FLAGS}" "__INT32_TYPE__" _ctr_has_fix)
    if(_ctr_has_arch EQUAL -1 OR _ctr_has_fix EQUAL -1)
        set(CMAKE_${lang}_FLAGS "${_CTR_ARCH} ${_CTR_COMMON} ${_CTR_INT32_FIX}" CACHE STRING "" FORCE)
    endif()
endmacro()
_ctr_ensure_flags(C)
_ctr_ensure_flags(CXX)

# ARM11 MPCore = ARMv6K, VFPv2 hard-float. The arch flags come from the
# devkitPro platform module (CTR_ARCH_SETTINGS); nothing extra needed here.
#
# Math relaxations safe for game math (same rationale as Vita): drop errno
# from libm calls, assume finite math. VFPv2 has no fused-multiply-add, and
# -ffast-math is avoided for the same reasons as on Vita.
set(_CTR_MATH_FLAGS "-fno-math-errno -fno-trapping-math -fno-signed-zeros -ffinite-math-only")

# -Os: on the 3DS every megabyte of .text is a megabyte stolen from the
# malloc arena (static image + linear heap + heap all share ~178MB), and the
# ARM11 I-cache is tiny — smaller code wins twice. Measured: -O2 text was
# ~35MB; -Os shaves several MB that go straight to the ESM store.
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG -fomit-frame-pointer -fno-stack-protector -fipa-icf ${_CTR_MATH_FLAGS}" CACHE STRING "c++ Release flags" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG -fomit-frame-pointer -fno-stack-protector -fipa-icf ${_CTR_MATH_FLAGS}" CACHE STRING "c Release flags" FORCE)

# All-static builds have circular deps between libs (NovaGL ↔ citro3d,
# avformat ↔ avcodec, etc.). Wrap link with --start/end-group so the linker
# iterates until all symbols resolve, regardless of order.
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> -Wl,--start-group <OBJECTS> <LINK_LIBRARIES> -Wl,--end-group -o <TARGET>"
    CACHE STRING "" FORCE)
