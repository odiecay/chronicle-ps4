# Paths and tools the Kazarov rules run. Included by Kazarov.cmake, after KZ_ROOT is set.

file(RELATIVE_PATH KZ_ROOT_REL "${CMAKE_SOURCE_DIR}" "${KZ_ROOT}")
file(RELATIVE_PATH KZ_BUILD_REL "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
set(KZ_BIN "${KZ_ROOT}/bin")
set(KZ_INCLUDE_REL "${KZ_ROOT_REL}/include")
set(KZ_PYTHONPATH "${KZ_ROOT}/python")

find_program(KZ_PYTHON NAMES python3 python HINTS $ENV{VIRTUAL_ENV}/bin REQUIRED)

# The binutils that assemble and inspect the MIPS objects. Each can be overridden
# by setting the KZ_ variable before the include.
set(MIPS_TOOL_PREFIX mips-ps2-decompals-
    CACHE STRING "Prefix of the binutils-mips-ps2-decompals tools")
if(NOT DEFINED KZ_AS)
    set(KZ_AS ${MIPS_TOOL_PREFIX}as)
endif()
# KZ_AS may be a command list, such as "python3 tools/fake_as.py". KZ_AS_CMD holds its words.
string(JOIN " " KZ_AS_STRING ${KZ_AS})
separate_arguments(KZ_AS_CMD UNIX_COMMAND "${KZ_AS_STRING}")
list(GET KZ_AS_CMD 0 KZ_AS_PROG)
# The resolved assembler, so kz_asm_object rebuilds when it changes. Empty when not found.
find_program(KZ_AS_PATH NAMES ${KZ_AS_PROG})
# Other words of the command that name an existing file, such as a script, are inputs too.
set(KZ_AS_FILES "")
set(KZ_AS_ARGS ${KZ_AS_CMD})
list(REMOVE_AT KZ_AS_ARGS 0)
foreach(word IN LISTS KZ_AS_ARGS)
    if(IS_ABSOLUTE "${word}")
        set(word_abs "${word}")
    else()
        set(word_abs "${CMAKE_SOURCE_DIR}/${word}")
    endif()
    if(EXISTS "${word_abs}" AND NOT IS_DIRECTORY "${word_abs}")
        list(APPEND KZ_AS_FILES "${word_abs}")
    endif()
endforeach()
if(NOT DEFINED KZ_OBJCOPY)
    set(KZ_OBJCOPY ${MIPS_TOOL_PREFIX}objcopy)
endif()
if(NOT DEFINED KZ_OBJDUMP)
    set(KZ_OBJDUMP ${MIPS_TOOL_PREFIX}objdump)
endif()
if(NOT DEFINED KZ_READELF)
    set(KZ_READELF ${MIPS_TOOL_PREFIX}readelf)
endif()

# MWLD runs under wibo, as the MWCC compilers do.
set(KZ_WIBO wibo CACHE STRING "The wibo that runs the Metrowerks tools")
set(KZ_LD ${KZ_WIBO} ${KZ_MWLD})

if(NOT DEFINED KZ_MWCC_RUNNER)
    set(KZ_MWCC_RUNNER wibo)
endif()
if(NOT DEFINED KZ_AS_FLAGS)
    set(KZ_AS_FLAGS "")
endif()
if(NOT DEFINED KZ_LIB_INCLUDE_DIRS)
    set(KZ_LIB_INCLUDE_DIRS "")
endif()

# Every compile and assembly depends on these, so a changed tool or image rebuilds
# the objects. The image id file exists only inside the container image; KZ_IMAGE_ID is
# also put in each command line, so a changed image changes the command.
set(KZ_TOOL_DEPENDS ${KZ_BIN}/kz-mwcc ${KZ_BIN}/kz-mwcc-exec)
set(KZ_IMAGE_ID "")
if(EXISTS /opt/kazarov/image-id)
    file(READ /opt/kazarov/image-id KZ_IMAGE_ID)
    string(STRIP "${KZ_IMAGE_ID}" KZ_IMAGE_ID)
    list(APPEND KZ_TOOL_DEPENDS /opt/kazarov/image-id)
endif()

# Path under CMAKE_SOURCE_DIR for a relative path, unchanged if absolute.
function(kz_absolute_path path out_var)
    if(IS_ABSOLUTE "${path}")
        set(${out_var} "${path}" PARENT_SCOPE)
    else()
        set(${out_var} "${CMAKE_SOURCE_DIR}/${path}" PARENT_SCOPE)
    endif()
endfunction()
