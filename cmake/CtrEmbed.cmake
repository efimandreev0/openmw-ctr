# Helpers for the Nintendo 3DS build: PICA200 shaders (picasso) embedded into the executable.

# Converts a binary file into a C source defining `const unsigned char <symbol>[]` and `<symbol>_size`.
# Invoked in script mode: cmake -DINPUT=... -DOUTPUT=... -DSYMBOL=... -P CtrEmbed.cmake
if (CMAKE_SCRIPT_MODE_FILE)
    file(READ "${INPUT}" _hex HEX)
    string(LENGTH "${_hex}" _len)
    math(EXPR _size "${_len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    file(WRITE "${OUTPUT}" "/* generated from ${INPUT} */\n#include <stddef.h>\n__attribute__((aligned(4))) const unsigned char ${SYMBOL}[] = {${_bytes}};\nconst size_t ${SYMBOL}_size = ${_size};\n")
    return()
endif()

set(CTR_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_FILE}")

# ctr_add_shaders(<out_sources_var> <shader.v.pica>...): compiles each shader with picasso and appends the generated
# C sources to <out_sources_var>. The symbol of foo.v.pica is ctr_shader_foo (see components/ctr/gpu/shaders.hpp).
function(ctr_add_shaders out_var)
    find_program(PICASSO picasso HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
    set(_sources)
    foreach(_shader ${ARGN})
        get_filename_component(_name "${_shader}" NAME)
        string(REGEX REPLACE "\\.v\\.pica$" "" _base "${_name}")
        set(_shbin "${CMAKE_CURRENT_BINARY_DIR}/ctrshaders/${_base}.shbin")
        set(_c "${CMAKE_CURRENT_BINARY_DIR}/ctrshaders/${_base}_shbin.c")
        add_custom_command(
            OUTPUT "${_c}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/ctrshaders"
            COMMAND "${PICASSO}" -o "${_shbin}" "${_shader}"
            COMMAND ${CMAKE_COMMAND} -DINPUT=${_shbin} -DOUTPUT=${_c} -DSYMBOL=ctr_shader_${_base} -P "${CTR_EMBED_SCRIPT}"
            DEPENDS "${_shader}" "${CTR_EMBED_SCRIPT}"
            COMMENT "Assembling PICA200 shader ${_name}"
            VERBATIM)
        list(APPEND _sources "${_c}")
    endforeach()
    set(${out_var} ${${out_var}} ${_sources} PARENT_SCOPE)
endfunction()
