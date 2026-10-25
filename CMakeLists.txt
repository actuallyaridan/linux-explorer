cmake_minimum_required(VERSION 3.16)
project(Explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without this a plain "cmake -B build" produces an unoptimised binary
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# DBus tells KDED to load or unload its automounter module, and OpenGL is the
# Control Panel's graphics benchmark
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Multimedia DBus OpenGL)
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
# The Control Panel's: zlib and OpenSSL time the benchmark's compression and
# encryption, and CUPS lists the printers in Devices and Printers
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Cups REQUIRED)

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
    src/data/MenuEntries.cpp
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
    src/ui/MessageBoxes.cpp
    src/ui/MountDialog.cpp
    src/ui/OptionsDialog.cpp
    src/ui/PropertiesDialog.cpp
    src/ui/MainWindow.cpp
    src/ui/NavigationPane.cpp
    src/ui/PreviewPane.cpp
    src/ui/DetailsPane.cpp

    # The Control Panel, shown inside Explorer's windows as Win7's is, with
    # each of its folders in a control folder beside Explorer's own
    src/data/control/AppearanceAndPersonalization.cpp
    src/data/control/Branding.cpp
    src/data/control/Categories.cpp
    src/data/control/ClockLanguageRegion.cpp
    src/data/control/EaseOfAccess.cpp
    src/data/control/HardwareAndSound.cpp
    src/data/control/NetworkAndInternet.cpp
    src/data/control/OldSettings.cpp
    src/data/control/Programs.cpp
    src/data/control/SystemAndSecurity.cpp
    src/data/control/UserAccounts.cpp
    src/data/control/WindowsTimeZones.cpp
    src/model/control/PrinterScanner.cpp
    src/model/control/SysfsScanner.cpp
    src/model/control/WeiBenchmark.cpp
    src/ui/control/CategoryWidget.cpp
    src/ui/control/ControlPanelView.cpp
    src/ui/control/PageRegistry.cpp
    src/ui/control/dialogs/DateTimeDialog.cpp
    src/ui/control/dialogs/DevicePropertiesDialog.cpp
    src/ui/control/dialogs/LinverConfigDialog.cpp
    src/ui/control/dialogs/SoundDialog.cpp
    src/ui/control/pages/ActionCenterPage.cpp
    src/ui/control/pages/DevicesAndPrintersPage.cpp
    src/ui/control/pages/EaseOfAccessPage.cpp
    src/ui/control/pages/FirewallPage.cpp
    src/ui/control/pages/FontsPage.cpp
    src/ui/control/pages/InstalledUpdatesPage.cpp
    src/ui/control/pages/LinuxUpdatePage.cpp
    src/ui/control/pages/NetworkSharingPage.cpp
    src/ui/control/pages/PerformancePage.cpp
    src/ui/control/pages/PersonalizationPage.cpp
    src/ui/control/pages/PowerOptionsPage.cpp
    src/ui/control/pages/ProgramsFeaturesPage.cpp
    src/ui/control/pages/SystemPage.cpp
    src/ui/control/pages/UserAccountsPage.cpp
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
    Qt6::OpenGL
    ZLIB::ZLIB
    OpenSSL::Crypto
    Cups::Cups
)

set_target_properties(explorer PROPERTIES
    OUTPUT_NAME explorer
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

install(TARGETS explorer RUNTIME DESTINATION bin)
install(FILES linux-explorer.desktop DESTINATION share/applications)
install(FILES linux-controlpanel.desktop DESTINATION share/applications)
