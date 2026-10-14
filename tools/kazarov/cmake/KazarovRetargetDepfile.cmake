# Renames the target of a depfile. kz-mwcc names the object the compiler wrote, and with
# RAW that is the raw object; Ninja needs the output of the edge instead.
#
#   cmake -DKZ_DEPFILE=<file> -DKZ_FROM=<path> -DKZ_TO=<path> -P KazarovRetargetDepfile.cmake
if(EXISTS "${KZ_DEPFILE}")
    file(READ "${KZ_DEPFILE}" kz_text)
    string(REPLACE "${KZ_FROM}:" "${KZ_TO}:" kz_text "${kz_text}")
    file(WRITE "${KZ_DEPFILE}" "${kz_text}")
endif()
