# The m2c declaration context, for decompiling against the project's headers.

# kz_ctx(OUTPUT ctx.c ctx.cpp HEADERS f... [ARGS ...])
#
# Creates the target `ctx`. scripts/m2ctx.py writes the context to the first OUTPUT (-o),
# and the second must be the same name with a .cpp extension, which it writes beside it.
# HEADERS are the files whose change regenerates the context. ARGS are passed on to
# m2ctx.py after -o.
function(kz_ctx)
    cmake_parse_arguments(KZC "" "" "OUTPUT;HEADERS;ARGS" ${ARGN})
    if(KZC_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "kz_ctx: unknown arguments ${KZC_UNPARSED_ARGUMENTS}")
    endif()
    list(LENGTH KZC_OUTPUT count)
    if(NOT count EQUAL 2)
        message(FATAL_ERROR "kz_ctx: OUTPUT takes two files, ctx.c and ctx.cpp")
    endif()
    list(GET KZC_OUTPUT 0 c_rel)
    list(GET KZC_OUTPUT 1 cpp_rel)
    string(REGEX REPLACE "\\.[^./]*$" ".cpp" expected_cpp "${c_rel}")
    if(NOT cpp_rel STREQUAL expected_cpp)
        message(FATAL_ERROR "kz_ctx: ${cpp_rel} must be ${expected_cpp}, which m2ctx.py writes beside ${c_rel}")
    endif()

    kz_absolute_path("${c_rel}" c_abs)
    kz_absolute_path("${cpp_rel}" cpp_abs)
    kz_absolute_paths(headers ${KZC_HEADERS})
    get_filename_component(c_dir "${c_abs}" DIRECTORY)
    file(MAKE_DIRECTORY "${c_dir}")

    add_custom_command(
        OUTPUT "${c_abs}" "${cpp_abs}"
        COMMAND ${KZ_PYTHON} ${KZ_ROOT}/scripts/m2ctx.py -o "${c_abs}" ${KZC_ARGS}
        DEPENDS ${headers} ${KZ_ROOT}/scripts/m2ctx.py
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating m2c context"
        VERBATIM)
    add_custom_target(ctx DEPENDS "${c_abs}" "${cpp_abs}")
endfunction()
