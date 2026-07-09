# Custom FindOpenGL for Vita cross-compilation.
# On Vita, GL is provided by vitaGL — no system OpenGL package exists.
# This module satisfies find_package(OpenGL) for all subdirectories
# (MyGUI, OSG, Bullet) that call it internally.

if (VITA)
    set(OPENGL_FOUND TRUE)
    set(OpenGL_FOUND TRUE)
    set(OPENGL_INCLUDE_DIR "")
    set(OPENGL_LIBRARIES "vitaGL")
    set(OPENGL_gl_LIBRARY "vitaGL")
    set(OPENGL_opengl_LIBRARY "vitaGL")
    set(OPENGL_glx_LIBRARY "vitaGL")
    set(OPENGL_INCLUDE_DIR "${VITASDK}/arm-vita-eabi/include")

    # Report all requested components as found
    set(OPENGL_GLX_FOUND TRUE)
    set(OpenGL_GLX_FOUND TRUE)
    set(OPENGL_OpenGL_FOUND TRUE)
    set(OpenGL_OpenGL_FOUND TRUE)
    set(OPENGL_GLU_FOUND FALSE)
    set(OPENGL_EGL_FOUND FALSE)

    # Create imported targets that downstream CMake expects
    if (NOT TARGET OpenGL::GL)
        add_library(OpenGL::GL INTERFACE IMPORTED)
        set_target_properties(OpenGL::GL PROPERTIES
            INTERFACE_LINK_LIBRARIES "vitaGL"
        )
    endif()
    if (NOT TARGET OpenGL::OpenGL)
        add_library(OpenGL::OpenGL INTERFACE IMPORTED)
        set_target_properties(OpenGL::OpenGL PROPERTIES
            INTERFACE_LINK_LIBRARIES "vitaGL"
        )
    endif()
    if (NOT TARGET OpenGL::GLX)
        add_library(OpenGL::GLX INTERFACE IMPORTED)
        set_target_properties(OpenGL::GLX PROPERTIES
            INTERFACE_LINK_LIBRARIES "vitaGL"
        )
    endif()
elseif (CTR)
    # Nintendo 3DS — GL is provided by NovaGL (GLES1.1-class wrapper over citro3d).
    set(OPENGL_FOUND TRUE)
    set(OpenGL_FOUND TRUE)
    set(OPENGL_LIBRARIES "NovaGL;citro3d")
    set(OPENGL_gl_LIBRARY "NovaGL;citro3d")
    set(OPENGL_opengl_LIBRARY "NovaGL;citro3d")
    set(OPENGL_glx_LIBRARY "NovaGL;citro3d")
    set(OPENGL_INCLUDE_DIR "$ENV{DEVKITPRO}/portlibs/3ds/include")

    set(OPENGL_GLX_FOUND TRUE)
    set(OpenGL_GLX_FOUND TRUE)
    set(OPENGL_OpenGL_FOUND TRUE)
    set(OpenGL_OpenGL_FOUND TRUE)
    set(OPENGL_GLU_FOUND FALSE)
    set(OPENGL_EGL_FOUND FALSE)

    foreach(_gl_tgt GL OpenGL GLX)
        if (NOT TARGET OpenGL::${_gl_tgt})
            add_library(OpenGL::${_gl_tgt} INTERFACE IMPORTED)
            set_target_properties(OpenGL::${_gl_tgt} PROPERTIES
                INTERFACE_LINK_LIBRARIES "NovaGL;citro3d"
            )
        endif()
    endforeach()
else()
    # Not Vita/CTR — delegate to CMake's built-in FindOpenGL
    include(${CMAKE_ROOT}/Modules/FindOpenGL.cmake)
endif()
