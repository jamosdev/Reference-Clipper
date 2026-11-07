cmake_minimum_required(VERSION 3.28)
project(reference-clipper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Test)
find_package(PkgConfig QUIET)

# FFmpeg from pkg-config, or from a prebuilt tree with include/ and lib/, which
# is how the release builds get one (BtbN's FFmpeg builds, see packaging/).
set(FFMPEG_ROOT "" CACHE PATH "Prebuilt FFmpeg tree to use instead of pkg-config")
add_library(rc_ffmpeg INTERFACE)
if(FFMPEG_ROOT)
    target_include_directories(rc_ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
    foreach(name avformat avcodec avfilter swresample swscale avutil)
        find_library(FFMPEG_${name}_LIBRARY NAMES ${name} PATHS "${FFMPEG_ROOT}/lib"
                     NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
        target_link_libraries(rc_ffmpeg INTERFACE "${FFMPEG_${name}_LIBRARY}")
    endforeach()
else()
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavfilter libswresample libswscale libavutil)
    target_link_libraries(rc_ffmpeg INTERFACE PkgConfig::FFMPEG)
    pkg_check_modules(SVTAV1 QUIET SvtAv1Enc)
endif()

# Monitoring audio: PulseAudio's protocol on Linux (PipeWire serves it too),
# the waveOut API on Windows.
if(WIN32)
    set(RC_AUDIO_LIBRARIES winmm)
else()
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple)
    set(RC_AUDIO_LIBRARIES PkgConfig::PULSE)
endif()

if(NOT MSVC)
    add_compile_options(-Wall -Wextra)
endif()

# core: pure C++ (spec formulas, span logic, config). No Qt, no FFmpeg.
add_library(rc_core STATIC
    src/core/config.cpp
    src/core/crop.cpp
    src/core/quality.cpp
    src/core/timecode.cpp
    src/core/timeline.cpp)
target_include_directories(rc_core PUBLIC src)

# media: everything that touches libav*. No Qt.
add_library(rc_media STATIC
    src/media/drift.cpp
    src/media/encoder.cpp
    src/media/frame_image.cpp
    src/media/pcm_source.cpp
    src/media/probe.cpp
    src/media/scene.cpp
    src/media/stream_reader.cpp
    src/media/waveform.cpp)
target_link_libraries(rc_media PUBLIC rc_core rc_ffmpeg)
if(SVTAV1_FOUND)
    target_compile_definitions(rc_media PRIVATE RC_SVTAV1_VERSION="${SVTAV1_VERSION}")
endif()

# app: the Qt UI, as a library so tests can drive the real MainWindow.
qt_add_library(rc_app STATIC
    src/app/assets.cpp
    src/app/audio_preview.cpp
    src/app/empty_view.cpp
    src/app/frame_broker.cpp
    src/app/history.cpp
    src/app/job_queue.cpp
    src/app/jobs_panel.cpp
    src/app/main_window.cpp
    src/app/output_panel.cpp
    src/app/preview_panel.cpp
    src/app/scene_search.cpp
    src/app/session.cpp
    src/app/source_view.cpp
    src/app/theme.cpp
    src/app/timeline_widget.cpp
    src/app/widgets.cpp)
set_target_properties(rc_app PROPERTIES AUTOMOC ON)
target_link_libraries(rc_app PUBLIC rc_media Qt6::Widgets Qt6::Svg ${RC_AUDIO_LIBRARIES})
qt_add_resources(rc_app "fonts"
    PREFIX "/fonts"
    BASE "assets/fonts"
    FILES
        assets/fonts/Barlow-Regular.ttf
        assets/fonts/Barlow-Medium.ttf
        assets/fonts/Barlow-Bold.ttf
        assets/fonts/BarlowCondensed-SemiBold.ttf)
qt_add_resources(rc_app "icons"
    PREFIX "/icons"
    BASE "assets/icons"
    FILES assets/icons/reference-clipper.svg)

qt_add_executable(reference-clipper WIN32 src/app/main.cpp)
target_link_libraries(reference-clipper PRIVATE rc_app)

install(TARGETS reference-clipper DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX)
    install(FILES packaging/reference-clipper.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/icons/reference-clipper.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

enable_testing()
add_subdirectory(tests)
