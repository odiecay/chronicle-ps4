# Entry point of the Kazarov build rules.
#
# Set these before including it (paths are relative to CMAKE_SOURCE_DIR):
#   KZ_MWCC, KZ_MWLD       compiler and linker executables
#   KZ_LIB_INCLUDE_DIRS    list, the compiler's system-header search path
#   KZ_MWCC_RUNNER         list, the runner for the compiler (default wibo)
#   KZ_AS_FLAGS            list, extra flags for every assembly
# Optional: KZ_AS, KZ_OBJCOPY, KZ_OBJDUMP, KZ_READELF, KZ_WIBO, MIPS_TOOL_PREFIX,
# and for overlays KZ_OVERLAYS, KZ_OVERLAY_ORIGIN, KZ_OVERLAY_HEADER_SIZE,
# KZ_OVERLAY_FILE_ALIGN (see KazarovOverlays.cmake).

get_filename_component(KZ_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/KazarovToolchain.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/KazarovObjects.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/KazarovSetup.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/KazarovOverlays.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/KazarovContext.cmake")
