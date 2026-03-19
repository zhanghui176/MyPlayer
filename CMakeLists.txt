cmake_minimum_required(VERSION 3.19)
project(MyPlayer LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets Multimedia Sql)

qt_standard_project_setup()

set(OPENCV_ROOT "D:/lib/opencv410/opencv-build-contrib")
set(FFMPEG_ROOT "D:/lib/ffmpeg-7.1.1-full_build-shared/")
set(MYPLAYER_MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models")
set(MYPLAYER_FACE_SAMPLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/faceSample")
set(OPENCV_DNN_LIB "${OPENCV_ROOT}/lib/libopencv_dnn4100.dll.a")
set(OPENCV_OBJDETECT_LIB "${OPENCV_ROOT}/lib/libopencv_objdetect4100.dll.a")

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
    AppPaths.h
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
    OnnxFrameProcessor.h OnnxFrameProcessor.cpp
    FaceBoxDrawer.h FaceBoxDrawer.cpp
    FaceDetectionRunner.h FaceDetectionRunner.cpp
    FaceRecognitionRunner.h FaceRecognitionRunner.cpp
    FaceDatabase.h FaceDatabase.cpp
    FaceSampleImporter.h FaceSampleImporter.cpp
    ImageToImageOnnxRunner.h ImageToImageOnnxRunner.cpp
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
    Qt6::Sql

    #FFMPEG 库
    "${FFMPEG_ROOT}/lib/libavcodec.dll.a"
    "${FFMPEG_ROOT}/lib/libavformat.dll.a"
    "${FFMPEG_ROOT}/lib/libswscale.dll.a"
    "${FFMPEG_ROOT}/lib/libavutil.dll.a"
    "${FFMPEG_ROOT}/lib/libswresample.dll.a"
    "${FFMPEG_ROOT}/lib/libavdevice.dll.a"
    "${FFMPEG_ROOT}/lib/libavfilter.dll.a"

    #OpenCV 库
    "${OPENCV_ROOT}/lib/libopencv_core4100.dll.a"
    "${OPENCV_ROOT}/lib/libopencv_imgproc4100.dll.a"
    "${OPENCV_ROOT}/lib/libopencv_highgui4100.dll.a"
    "${OPENCV_ROOT}/lib/libopencv_videoio4100.dll.a"

    #系统库
    opengl32
    d3d11
    d3dcompiler
)

if(EXISTS "${OPENCV_DNN_LIB}")
    target_link_libraries(MyPlayer PRIVATE "${OPENCV_DNN_LIB}")
    target_compile_definitions(MyPlayer PRIVATE MYPLAYER_ENABLE_ONNX=1)
    message(STATUS "OpenCV DNN library found. ONNX frame processing enabled.")

    if(EXISTS "${OPENCV_OBJDETECT_LIB}")
        target_link_libraries(MyPlayer PRIVATE "${OPENCV_OBJDETECT_LIB}")
        target_compile_definitions(MyPlayer PRIVATE MYPLAYER_ENABLE_OPENCV_FACE=1)
        message(STATUS "OpenCV objdetect library found. Face detection overlay enabled.")
    else()
        message(WARNING "OpenCV objdetect library not found: ${OPENCV_OBJDETECT_LIB}")
        message(WARNING "Face detection overlay will be disabled.")
    endif()
else()
    message(WARNING "OpenCV DNN library not found: ${OPENCV_DNN_LIB}")
    message(WARNING "ONNX frame processing will be disabled.")
endif()

if(EXISTS "${MYPLAYER_MODELS_DIR}")
    add_custom_command(TARGET MyPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MyPlayer>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MYPLAYER_MODELS_DIR}"
                "$<TARGET_FILE_DIR:MyPlayer>/models"
        COMMENT "Copying model assets to output directory"
    )
else()
    message(WARNING "Models directory not found: ${MYPLAYER_MODELS_DIR}")
endif()

if(EXISTS "${MYPLAYER_FACE_SAMPLE_DIR}")
    add_custom_command(TARGET MyPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MyPlayer>/faceSample"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MYPLAYER_FACE_SAMPLE_DIR}"
                "$<TARGET_FILE_DIR:MyPlayer>/faceSample"
        COMMENT "Copying face samples to output directory"
    )
else()
    message(WARNING "faceSample directory not found: ${MYPLAYER_FACE_SAMPLE_DIR}")
endif()

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
