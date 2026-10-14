# The disc, what is extracted from it, and the split of the executable into assembly.

# kz_disc_setup(ISO p ISO_CHECKSUM f EXTRACT_DIR d INPUTS f... INPUTS_CHECKSUM f
#               INPUTS_CHECKSUM_DIR d SPLIT_DIR d SPLIT_STAMP f
#               SPLIT_COMMAND COMMAND ... [COMMAND ...] SPLIT_DEPENDS f...
#               [SPLIT_BYPRODUCTS f...])
#
# Targets:
#   extract       only if ISO exists at configure time; writes the EXTRACT_DIR id file
#                 and the INPUTS
#   setup         builds SPLIT_STAMP
#   disassemble   runs the split again, whether or not anything changed
#
# The split chain is: check INPUTS_CHECKSUM, snapshot SPLIT_DIR, SPLIT_COMMAND, restore
# SPLIT_DIR. The restore keeps the mtimes of unchanged files and rewrites SPLIT_STAMP only
# when the content changed, so a re-split that changes nothing rebuilds nothing.
#
# Sets KZ_EXTRACT_ID (the id file) and KZ_SPLIT_STAMP in the caller's scope, absolute.
function(kz_disc_setup)
    cmake_parse_arguments(KZD ""
        "ISO;ISO_CHECKSUM;EXTRACT_DIR;INPUTS_CHECKSUM;INPUTS_CHECKSUM_DIR;SPLIT_DIR;SPLIT_STAMP"
        "INPUTS;SPLIT_COMMAND;SPLIT_DEPENDS;SPLIT_BYPRODUCTS" ${ARGN})
    if(KZD_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "kz_disc_setup: unknown arguments ${KZD_UNPARSED_ARGUMENTS}")
    endif()
    foreach(key ISO ISO_CHECKSUM EXTRACT_DIR INPUTS_CHECKSUM INPUTS_CHECKSUM_DIR
                SPLIT_DIR SPLIT_STAMP SPLIT_COMMAND)
        if(NOT KZD_${key})
            message(FATAL_ERROR "kz_disc_setup needs ${key}")
        endif()
    endforeach()

    kz_absolute_path("${KZD_ISO}" iso)
    kz_absolute_path("${KZD_ISO_CHECKSUM}" iso_checksum)
    kz_absolute_path("${KZD_EXTRACT_DIR}" extract_dir)
    kz_absolute_path("${KZD_INPUTS_CHECKSUM}" inputs_checksum)
    kz_absolute_path("${KZD_INPUTS_CHECKSUM_DIR}" inputs_checksum_dir)
    kz_absolute_path("${KZD_SPLIT_DIR}" split_dir)
    kz_absolute_path("${KZD_SPLIT_STAMP}" split_stamp)
    kz_absolute_paths(inputs ${KZD_INPUTS})
    kz_absolute_paths(split_depends ${KZD_SPLIT_DEPENDS})
    kz_absolute_paths(split_byproducts ${KZD_SPLIT_BYPRODUCTS})

    set(extract_id "${extract_dir}/.kazarov-extract.id")
    get_filename_component(iso_dir "${iso}" DIRECTORY)
    get_filename_component(stamp_dir "${split_stamp}" DIRECTORY)
    file(MAKE_DIRECTORY "${stamp_dir}")
    file(MAKE_DIRECTORY "${split_dir}")

    if(EXISTS "${iso}")
        add_custom_command(
            OUTPUT "${extract_id}"
            BYPRODUCTS ${inputs}
            COMMAND ${KZ_PYTHON} ${KZ_BIN}/kz-extract
                    --iso "${iso}" --checksum "${iso_checksum}"
                    --out "${extract_dir}" --id-file "${extract_id}"
            DEPENDS "${iso}" "${iso_checksum}" ${KZ_BIN}/kz-extract
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Extracting ${iso}"
            USES_TERMINAL
            VERBATIM)
        add_custom_target(extract DEPENDS "${extract_id}")
        set(split_input "${extract_id}")
    else()
        set(split_input ${inputs})
    endif()
    # Reconfigure when the disc is added to its directory, which creates the extract rule.
    # While that directory is missing, its nearest existing ancestor is watched instead.
    set(watch "${iso_dir}")
    while(NOT EXISTS "${watch}")
        get_filename_component(parent "${watch}" DIRECTORY)
        if(parent STREQUAL watch)
            break()
        endif()
        set(watch "${parent}")
    endwhile()
    if(EXISTS "${watch}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${watch}")
    endif()
    if(EXISTS "${split_stamp}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${split_stamp}")
    endif()

    set(snapshot "${split_stamp}.snapshot")
    set(chain
        COMMAND ${CMAKE_COMMAND} -E chdir "${inputs_checksum_dir}"
                sha256sum -c "${inputs_checksum}"
        COMMAND ${KZ_PYTHON} ${KZ_BIN}/kz-split-stamp snapshot "${split_dir}" "${snapshot}"
        ${KZD_SPLIT_COMMAND}
        COMMAND ${KZ_PYTHON} ${KZ_BIN}/kz-split-stamp restore "${split_dir}" "${snapshot}" "${split_stamp}")

    add_custom_command(
        OUTPUT "${split_stamp}"
        BYPRODUCTS ${split_byproducts}
        ${chain}
        DEPENDS ${split_input} "${inputs_checksum}" ${inputs} ${split_depends}
                ${KZ_BIN}/kz-split-stamp
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Splitting ${split_dir}"
        USES_TERMINAL
        VERBATIM)
    add_custom_target(setup DEPENDS "${split_stamp}")

    add_custom_target(disassemble
        ${chain}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM)
    # The forced split runs after the incremental one, never alongside it.
    add_dependencies(disassemble setup)

    set(KZ_EXTRACT_ID "${extract_id}" PARENT_SCOPE)
    set(KZ_SPLIT_STAMP "${split_stamp}" PARENT_SCOPE)
endfunction()
