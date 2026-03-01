include(D:/QT/demo/MyPlayer/build/Desktop_Qt_6_5_3_MinGW_64_bit-Debug/.qt/QtDeploySupport.cmake)
include("${CMAKE_CURRENT_LIST_DIR}/MyPlayer-plugins.cmake" OPTIONAL)
set(__QT_DEPLOY_ALL_MODULES_FOUND_VIA_FIND_PACKAGE "ZlibPrivate;EntryPointPrivate;Core;Gui;Widgets;OpenGL;OpenGLWidgets;Network;Multimedia")

qt6_deploy_runtime_dependencies(
    EXECUTABLE D:/QT/demo/MyPlayer/build/Desktop_Qt_6_5_3_MinGW_64_bit-Debug/MyPlayer.exe
    GENERATE_QT_CONF
)
