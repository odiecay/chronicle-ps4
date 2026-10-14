# Object rules: assembly and MWCC compiles, each with an optional post-processing chain.

# Create the directory of each object now; neither the assembler nor kz-mwcc makes one.
function(kz_object_dirs)
    set(dirs "")
    foreach(obj IN LISTS ARGV)
        kz_absolute_path("${obj}" abs)
        get_filename_component(dir "${abs}" DIRECTORY)
        list(APPEND dirs "${dir}")
    endforeach()
    list(REMOVE_DUPLICATES dirs)
    foreach(dir IN LISTS dirs)
        file(MAKE_DIRECTORY "${dir}")
    endforeach()
endfunction()

# POST COMMAND a b COMMAND c d  ->  COMMAND <env wrapper> a b COMMAND <env wrapper> c d
function(kz_post_args out_var)
    set(args "")
    set(group "")
    set(started FALSE)
    foreach(tok IN LISTS ARGN)
        if(tok STREQUAL "COMMAND")
            if(started)
                list(APPEND args COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${KZ_PYTHONPATH} ${group})
            endif()
            set(group "")
            set(started TRUE)
        elseif(NOT started)
            message(FATAL_ERROR "kz: POST takes COMMAND groups, not '${tok}'")
        else()
            list(APPEND group "${tok}")
        endif()
    endforeach()
    if(started)
        list(APPEND args COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${KZ_PYTHONPATH} ${group})
    endif()
    set(${out_var} "${args}" PARENT_SCOPE)
endfunction()

# Absolute form of a list of files, for DEPENDS.
function(kz_absolute_paths out_var)
    set(abs "")
    foreach(path IN LISTS ARGN)
        kz_absolute_path("${path}" p)
        list(APPEND abs "${p}")
    endforeach()
    set(${out_var} "${abs}" PARENT_SCOPE)
endfunction()

# kz_asm_object(<obj> <src> [FLAGS ...] [POST COMMAND ... [COMMAND ...]] [DEPENDS ...] [COMMENT text])
function(kz_asm_object obj src)
    cmake_parse_arguments(KZA "" "COMMENT" "FLAGS;POST;DEPENDS" ${ARGN})
    if(KZA_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "kz_asm_object(${obj}): unknown arguments ${KZA_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT DEFINED KZ_AS)
        message(FATAL_ERROR "kz_asm_object needs KZ_AS (the assembler)")
    endif()

    kz_absolute_path("${obj}" obj_abs)
    kz_absolute_path("${src}" src_abs)
    kz_post_args(post_args ${KZA_POST})
    kz_absolute_paths(extra ${KZA_DEPENDS})
    if(KZ_AS_PATH)
        list(APPEND extra "${KZ_AS_PATH}")
    endif()
    list(APPEND extra ${KZ_AS_FILES})

    if(KZA_COMMENT)
        set(comment "${KZA_COMMENT}")
    else()
        set(comment "AS ${src}")
    endif()

    add_custom_command(
        OUTPUT "${obj_abs}"
        COMMAND ${CMAKE_COMMAND} -E env KZ_IMAGE_ID=${KZ_IMAGE_ID}
                ${KZ_AS_CMD} ${KZ_AS_FLAGS} ${KZA_FLAGS} -o "${obj_abs}" "${src_abs}"
        ${post_args}
        DEPENDS "${src_abs}" ${KZ_TOOL_DEPENDS} ${extra}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "${comment}"
        VERBATIM)
endfunction()

# kz_mwcc_object(<obj> <src> FLAGS ... [ENV N=V ...] [POST COMMAND ... [COMMAND ...]]
#                [DEPENDS ...] [COMMENT text] [RAW <raw obj>])
#
# The compile runs through kz-mwcc, which writes <obj>.d for Ninja. With RAW the compiler
# writes <raw obj> and only the POST commands produce <obj>; the depfile still belongs to <obj>.
function(kz_mwcc_object obj src)
    cmake_parse_arguments(KZM "" "COMMENT;RAW" "FLAGS;ENV;POST;DEPENDS" ${ARGN})
    if(KZM_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "kz_mwcc_object(${obj}): unknown arguments ${KZM_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT KZ_MWCC)
        message(FATAL_ERROR "kz_mwcc_object needs KZ_MWCC (the compiler)")
    endif()

    kz_absolute_path("${obj}" obj_abs)
    kz_absolute_path("${src}" src_abs)
    kz_absolute_path("${KZ_MWCC}" mwcc_abs)
    if(KZM_RAW)
        kz_absolute_path("${KZM_RAW}" compiled_abs)
    else()
        set(compiled_abs "${obj_abs}")
    endif()
    set(depfile "${obj_abs}.d")

    set(flags ${KZM_FLAGS})
    if(NOT "-MD" IN_LIST flags)
        list(APPEND flags -MD)
    endif()

    string(JOIN " " runner ${KZ_MWCC_RUNNER})
    # An escaped \; keeps the MWCIncludes separator inside one shell word.
    string(REPLACE ";" "\\;" include_dirs "${KZ_LIB_INCLUDE_DIRS}")
    kz_post_args(post_args ${KZM_POST})
    kz_absolute_paths(extra ${KZM_DEPENDS})

    if(KZM_COMMENT)
        set(comment "${KZM_COMMENT}")
    else()
        set(comment "CC ${src}")
    endif()

    set(compile
        ${CMAKE_COMMAND} -E env
            PYTHONPATH=${KZ_PYTHONPATH}
            KZ_IMAGE_ID=${KZ_IMAGE_ID}
            KZ_MWCC=${mwcc_abs}
            "KZ_MWCC_RUNNER=${runner}"
            KZ_PROJECT_ROOT=${CMAKE_SOURCE_DIR}
            "MWCIncludes=${include_dirs}"
            ${KZM_ENV}
            ${KZ_PYTHON} ${KZ_BIN}/kz-mwcc
            --obj "${compiled_abs}" --depfile "${depfile}" --
            ${flags} "${src}")

    set(retarget "")
    if(KZM_RAW)
        set(retarget COMMAND ${CMAKE_COMMAND}
            "-DKZ_DEPFILE=${depfile}" "-DKZ_FROM=${compiled_abs}" "-DKZ_TO=${obj_abs}"
            -P "${KZ_ROOT}/cmake/KazarovRetargetDepfile.cmake")
    endif()

    add_custom_command(
        OUTPUT "${obj_abs}"
        COMMAND ${compile}
        ${retarget}
        ${post_args}
        DEPFILE "${depfile}"
        DEPENDS "${src_abs}" ${mwcc_abs} ${KZ_TOOL_DEPENDS} ${extra}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "${comment}"
        VERBATIM)
endfunction()
