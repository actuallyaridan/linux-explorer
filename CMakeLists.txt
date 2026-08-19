cmake_minimum_required(VERSION 3.16)
project(Explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# DBus tells KDED to load or unload its automounter module
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Multimedia DBus)
# Every file operation goes through KIO, nothing is reimplemented here
find_package(KF6KIO REQUIRED)
find_package(KF6CoreAddons REQUIRED)
# KConfig reads and writes the desktop's own config files in their format
find_package(KF6Config REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6WindowSystem REQUIRED)
# Solid knows a places entry's block device, which stands in for a drive letter
find_package(KF6Solid REQUIRED)
find_package(AeroQt REQUIRED)

add_executable(explorer
    src/main.cpp
    src/ui/win7ui.qrc
    src/aero/artwork.cpp
    src/aero/arrows.cpp
    src/aero/capacitybar.cpp
    src/aero/icons.cpp
    src/aero/listview.cpp
    src/aero/sounds.cpp
    src/aero/buttons.cpp
    src/aero/strips.cpp
    src/aero/taskdialog.cpp
    src/aero/text.cpp
    src/ui/AboutDialog.cpp
    src/ui/AccessDialogs.cpp
    src/data/AutoMount.cpp
    src/data/Branding.cpp
    src/data/FileManagerService.cpp
    src/data/Settings.cpp
    src/model/Archives.cpp
    src/model/ComputerModel.cpp
    src/model/DirectoryModel.cpp
    src/model/DriveLabel.cpp
    src/model/FileOps.cpp
    src/model/GroupingProxy.cpp
    src/ui/ComputerView.cpp
    src/ui/FileProgressDialog.cpp
    src/ui/FileView.cpp
    src/ui/MapDriveDialog.cpp
    src/ui/MountDialog.cpp
    src/ui/OptionsDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/NavigationPane.cpp
    src/ui/PreviewPane.cpp
    src/ui/DetailsPane.cpp
)

target_include_directories(explorer PRIVATE
    src/data
    src/model
    src/ui
    src
)

target_link_libraries(explorer PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Multimedia
    Qt6::DBus
    KF6::KIOCore
    KF6::KIOGui
    KF6::KIOWidgets
    KF6::KIOFileWidgets
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::WidgetsAddons
    KF6::WindowSystem
    KF6::Solid
    AeroQt
)

set_target_properties(explorer PROPERTIES
    OUTPUT_NAME explorer
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

install(TARGETS explorer RUNTIME DESTINATION bin)
install(FILES linux-explorer.desktop DESTINATION share/applications)
