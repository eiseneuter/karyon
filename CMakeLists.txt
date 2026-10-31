cmake_minimum_required(VERSION 3.20)
project(karyon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml DBus Widgets)
find_package(KF6WindowSystem REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6KIO REQUIRED)

qt_policy(SET QTP0001 NEW)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.8.0")
    qt_policy(SET QTP0004 NEW)
endif()

add_compile_options(-g -rdynamic)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

qt_add_executable(karyon
    src/main.cpp
    src/DebugLogger.h
    src/DebugLogger.cpp
    src/CrashHandler.h
    src/CrashHandler.cpp
    src/CircleOverlay.h
    src/CircleOverlay.cpp
    src/InputMonitor.h
    src/InputMonitor.cpp
    src/KWinBridge.h
    src/KWinBridge.cpp
    src/KaryonGlyphs.h
    src/KaryonGlyphs.cpp
    src/AppManager.h
    src/AppManager.cpp
    src/TrayManager.h
    src/TrayManager.cpp
    src/RecentFilesManager.h
    src/RecentFilesManager.cpp
    src/PinnedManager.h
    src/PinnedManager.cpp
    src/MediaManager.h
    src/MediaManager.cpp
    src/VolumeManager.h
    src/VolumeManager.cpp
    src/SessionManager.h
    src/SessionManager.cpp
    src/SessionDialogController.h
    src/SessionDialogController.cpp
    src/SettingsManager.h
    src/SettingsManager.cpp
    src/SettingsController.h
    src/SettingsController.cpp
    src/GestureManager.h
    src/GestureManager.cpp
    src/GestureTrail.h
    src/GestureTrail.cpp
    src/JobProgressManager.h
    src/JobProgressManager.cpp
    src/PermissionManager.h
    src/PermissionManager.cpp
    src/PermissionDialogController.h
    src/PermissionDialogController.cpp
    src/InfoDialogController.h
    src/InfoDialogController.cpp
    src/GameModeDetector.h
    src/GameModeDetector.cpp
    src/ThemeManager.h
    src/ThemeManager.cpp
)

qt_add_qml_module(karyon
    URI karyon
    VERSION 1.0
    QML_FILES
        qml/Circle.qml
        qml/SessionDialog.qml
        qml/SettingsDialog.qml
        qml/GestureTrail.qml
        qml/PermissionDialog.qml
        qml/InfoDialog.qml
    RESOURCES
        resources/karyon.svg
)

target_link_libraries(karyon
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::DBus
        Qt6::Widgets
        KF6::WindowSystem
        KF6::Service
        KF6::KIOWidgets
        sqlite3
        xcb
        xcb-shape
        ${CMAKE_DL_LIBS}
)
