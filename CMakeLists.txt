cmake_minimum_required(VERSION 3.19)
project(MyPlayer LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets Multimedia)

qt_standard_project_setup()

set(OPENCV_ROOT "D:/lib/opencv/opencv-build/")
set(FFMPEG_ROOT "D:/lib/ffmpeg-7.1.1-full_build-shared/")

if(NOT "${OPENCV_ROOT}" STREQUAL "")
    message(STATUS "OpenCV root path: ${OPENCV_ROOT}")
    if(EXISTS "${OPENCV_ROOT}")
        message(STATUS "OpenCV directory exists")
    else()
        message(WARNING "OpenCV directory not found: ${OPENCV_ROOT}")
    endif()
endif()

if(NOT "${FFMPEG_ROOT}" STREQUAL "")
    message(STATUS "FFmpeg root path: ${FFMPEG_ROOT}")
    if(EXISTS "${FFMPEG_ROOT}")
        message(STATUS "FFmpeg directory exists")
    else()
        message(WARNING "FFmpeg directory not found: ${FFMPEG_ROOT}")
    endif()
endif()

include_directories("${OPENCV_ROOT}/install/include")
include_directories("${FFMPEG_ROOT}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

link_directories("${OPENCV_ROOT}/install/lib")
link_directories("${FFMPEG_ROOT}/lib")

qt_add_executable(MyPlayer
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui


    AVPlayer.h AVPlayer.cpp
    AVDemuxer.h AVDemuxer.cpp
    CodecWrapper.h CodecWrapper.cpp
    AVQueue.h
    OpenGLVideoWidget.cpp OpenGLVideoWidget.h
    AudioPlayer.h AudioPlayer.cpp
    SyncTimer.h
    FrameFilter.h FrameFilter.cpp
)

qt_add_resources(MyPlayer "resources"
    PREFIX "/"
    FILES
        fragment_yuv420p.glsl
        vertex.glsl
)

target_link_libraries(MyPlayer PRIVATE
    #QT 库
    Qt::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia

    #FFMPEG 库
    "${FFMPEG_ROOT}/lib/libavcodec.dll.a"
    "${FFMPEG_ROOT}/lib/libavformat.dll.a"
    "${FFMPEG_ROOT}/lib/libswscale.dll.a"
    "${FFMPEG_ROOT}/lib/libavutil.dll.a"
    "${FFMPEG_ROOT}/lib/libswresample.dll.a"
    "${FFMPEG_ROOT}/lib/libavdevice.dll.a"
    "${FFMPEG_ROOT}/lib/libavfilter.dll.a"

    #OpenCV 库
    "${OPENCV_ROOT}/lib/libopencv_core454.dll.a"
    "${OPENCV_ROOT}/lib/libopencv_imgproc454.dll.a"
    "${OPENCV_ROOT}/lib/libopencv_highgui454.dll.a"
    "${OPENCV_ROOT}/lib/libopencv_videoio454.dll.a"

    #系统库
    opengl32
    d3d11
    d3dcompiler
)

message(STATUS "OpenCV directory exists")

include(GNUInstallDirs)

install(TARGETS MyPlayer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET MyPlayer
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(SCRIPT ${deploy_script})
