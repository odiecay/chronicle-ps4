# Links FFmpeg's libavcodec and libavutil into a target as kz::ffmpeg: the static tree kz-ffmpeg builds
# (Linux, macOS) or BtbN's win64-lgpl-shared archive (Windows, DLLs). Only LGPL builds are acceptable
# inputs; a GPL or nonfree libavcodec must never reach a Kazarov consumer.

include_guard(GLOBAL)

function(_kz_ffmpeg_default_dir out)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" os)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|x64)$")
        set(arch x64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(arch arm64)
    else()
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" arch)
    endif()
    set(${out} "${CMAKE_SOURCE_DIR}/.kazarov/ffmpeg/${os}-${arch}" PARENT_SCOPE)
endfunction()

function(_kz_ffmpeg_import)
    if(DEFINED ENV{KZ_FFMPEG_DIR})
        set(default "$ENV{KZ_FFMPEG_DIR}")
    else()
        _kz_ffmpeg_default_dir(default)
    endif()
    set(KZ_FFMPEG_DIR "${default}" CACHE PATH "FFmpeg tree: include/, lib/ and on Windows bin/")

    if(NOT EXISTS "${KZ_FFMPEG_DIR}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR "kz_ffmpeg: no include/libavcodec/avcodec.h under ${KZ_FFMPEG_DIR}. "
            "Linux: the Kazarov image provides /opt/kazarov/ffmpeg. "
            "macOS: run tools/kazarov/bin/kz-ffmpeg --out <dir>. "
            "Windows: run tools/windows/acquire.ps1 and pass -DKZ_FFMPEG_DIR=<dir>.")
    endif()

    if(WIN32)
        set(runtime_dlls "")
        foreach(lib avutil avcodec)
            file(GLOB dll "${KZ_FFMPEG_DIR}/bin/${lib}-*.dll")
            list(LENGTH dll count)
            if(NOT count EQUAL 1)
                message(FATAL_ERROR "kz_ffmpeg: expected one bin/${lib}-*.dll under ${KZ_FFMPEG_DIR}, found ${count}")
            endif()
            if(NOT EXISTS "${KZ_FFMPEG_DIR}/lib/lib${lib}.dll.a")
                message(FATAL_ERROR "kz_ffmpeg: ${KZ_FFMPEG_DIR}/lib/lib${lib}.dll.a is missing")
            endif()
            add_library(kz::${lib} SHARED IMPORTED GLOBAL)
            set_target_properties(kz::${lib} PROPERTIES
                IMPORTED_LOCATION "${dll}"
                IMPORTED_IMPLIB "${KZ_FFMPEG_DIR}/lib/lib${lib}.dll.a")
            list(APPEND runtime_dlls "${dll}")
        endforeach()
        # BtbN's avcodec imports swresample, so it is a runtime dependency although nothing links it.
        file(GLOB swresample_dll "${KZ_FFMPEG_DIR}/bin/swresample-*.dll")
        list(APPEND runtime_dlls ${swresample_dll})
        set_property(GLOBAL PROPERTY KZ_FFMPEG_RUNTIME_DLLS "${runtime_dlls}")
    else()
        foreach(lib avutil avcodec)
            if(NOT EXISTS "${KZ_FFMPEG_DIR}/lib/lib${lib}.a")
                message(FATAL_ERROR "kz_ffmpeg: ${KZ_FFMPEG_DIR}/lib/lib${lib}.a is missing")
            endif()
            add_library(kz::${lib} STATIC IMPORTED GLOBAL)
            set_target_properties(kz::${lib} PROPERTIES
                IMPORTED_LOCATION "${KZ_FFMPEG_DIR}/lib/lib${lib}.a")
        endforeach()
        # The archives reference pthreads and libm; they must follow libavutil on the link line.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_package(Threads REQUIRED)
            set_property(TARGET kz::avutil PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads m)
        endif()
    endif()

    set_property(TARGET kz::avcodec PROPERTY INTERFACE_LINK_LIBRARIES kz::avutil)

    add_library(kz::ffmpeg INTERFACE IMPORTED GLOBAL)
    set_property(TARGET kz::ffmpeg PROPERTY INTERFACE_LINK_LIBRARIES kz::avcodec)
    target_include_directories(kz::ffmpeg SYSTEM INTERFACE "${KZ_FFMPEG_DIR}/include")

    message(STATUS "Kazarov: FFmpeg from ${KZ_FFMPEG_DIR}")
endfunction()

function(kz_ffmpeg target)
    set(scope PUBLIC)
    if(ARGC GREATER 1)
        set(scope "${ARGV1}")
    endif()
    if(NOT "${scope}" MATCHES "^(PUBLIC|PRIVATE|INTERFACE)$")
        message(FATAL_ERROR "kz_ffmpeg: scope must be PUBLIC, PRIVATE or INTERFACE, got '${scope}'")
    endif()

    if(NOT TARGET kz::ffmpeg)
        _kz_ffmpeg_import()
    endif()
    target_link_libraries(${target} ${scope} kz::ffmpeg)
endfunction()
