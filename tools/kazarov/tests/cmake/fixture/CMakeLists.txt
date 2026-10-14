# Graph fixture for tests/cmake/test_graph.py: two compiled units sharing a header, one with
# a private header, a RAW unit, one assembled unit split out of a checksummed input, and a POST
# tool.
cmake_minimum_required(VERSION 3.20)
project(kz_graph_fixture NONE)

set(FIX ${CMAKE_CURRENT_SOURCE_DIR})

# Stand-ins for the compiler and the assembler; both are python3 scripts.
set(KZ_MWCC ${FIX}/../fake_cc.py)
set(KZ_MWCC_RUNNER python3)
set(KZ_AS "python3 ${FIX}/tools/fake_as.py")

include(${FIX}/../../../cmake/Kazarov.cmake)

set(ASM_DIR ${CMAKE_BINARY_DIR}/asm)
set(OBJ_DIR ${CMAKE_BINARY_DIR}/obj)

# The disc path can be overridden on the configure line, to test a missing directory.
if(NOT DEFINED FIXTURE_ISO)
    set(FIXTURE_ISO ${FIX}/rom/no-disc.iso)
endif()

kz_disc_setup(
    ISO ${FIXTURE_ISO}
    ISO_CHECKSUM ${FIX}/rom/input.sha256
    EXTRACT_DIR ${CMAKE_BINARY_DIR}/extracted
    INPUTS ${FIX}/rom/input.bin
    INPUTS_CHECKSUM ${FIX}/rom/input.sha256
    INPUTS_CHECKSUM_DIR ${FIX}/rom
    SPLIT_DIR ${ASM_DIR}
    SPLIT_STAMP ${CMAKE_BINARY_DIR}/split.stamp
    SPLIT_COMMAND COMMAND ${KZ_PYTHON} ${FIX}/tools/split.py ${ASM_DIR}
    SPLIT_DEPENDS ${FIX}/tools/split.py
    SPLIT_BYPRODUCTS ${ASM_DIR}/unit_c.s)

set(POST_TOOL ${FIX}/tools/post.py)
set(FLAGS -O2 -c)

kz_mwcc_object(${OBJ_DIR}/a.o src/a.c
    FLAGS ${FLAGS}
    POST COMMAND ${KZ_PYTHON} ${POST_TOOL} ${OBJ_DIR}/a.o
    DEPENDS ${POST_TOOL})

kz_mwcc_object(${OBJ_DIR}/b.o src/b.c
    FLAGS ${FLAGS}
    POST COMMAND ${KZ_PYTHON} ${POST_TOOL} ${OBJ_DIR}/b.o
    DEPENDS ${POST_TOOL})

# A RAW object: the compiler writes c.raw.o, and only the POST copy makes c.o.
kz_mwcc_object(${OBJ_DIR}/c.o src/c.c
    FLAGS ${FLAGS}
    RAW ${OBJ_DIR}/c.raw.o
    ENV FIXTURE_RAW=1
    POST COMMAND ${CMAKE_COMMAND} -E copy ${OBJ_DIR}/c.raw.o ${OBJ_DIR}/c.o)

kz_asm_object(${OBJ_DIR}/unit_c.o ${ASM_DIR}/unit_c.s
    DEPENDS ${KZ_SPLIT_STAMP})

kz_object_dirs(${OBJ_DIR}/a.o ${OBJ_DIR}/b.o ${OBJ_DIR}/c.o ${OBJ_DIR}/unit_c.o)

add_custom_target(objects ALL DEPENDS
    ${OBJ_DIR}/a.o ${OBJ_DIR}/b.o ${OBJ_DIR}/c.o ${OBJ_DIR}/unit_c.o)
