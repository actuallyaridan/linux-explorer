cmake_minimum_required(VERSION 3.16)
project(ControlPanel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Concurrent DBus OpenGL)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(AeroQt REQUIRED)
find_package(Cups REQUIRED)

add_executable(controlpanel
    src/main.cpp
    src/ui/LinkLabel.h
    src/ui/MainWindow.cpp
    src/ui/PageRegistry.cpp
    src/ui/CategoryWidget.cpp
    src/ui/pages/LinuxUpdatePage.cpp
    src/ui/pages/SystemPage.cpp
    src/ui/pages/InstalledUpdatesPage.cpp
    src/ui/pages/ProgramsFeaturesPage.cpp
    src/ui/pages/NetworkSharingPage.cpp
    src/ui/pages/FirewallPage.cpp
    src/ui/pages/ActionCenterPage.cpp
    src/ui/pages/PowerOptionsPage.cpp
    src/ui/pages/PerformancePage.cpp
    src/ui/pages/PersonalizationPage.cpp
    src/ui/pages/FontsPage.cpp
    src/ui/pages/UserAccountsPage.cpp
    src/ui/pages/EaseOfAccessPage.cpp
    src/ui/pages/DevicesAndPrintersPage.cpp
    src/ui/dialogs/DateTimeDialog.cpp
    src/ui/dialogs/SoundDialog.cpp
    src/ui/dialogs/DevicePropertiesDialog.cpp
    src/ui/dialogs/LinverConfigDialog.cpp
    src/devices/SysfsScanner.cpp
    src/devices/PrinterScanner.cpp
    src/data/WindowsTimeZones.cpp
    src/perf/WeiBenchmark.cpp
    src/data/Categories.cpp
    src/data/SystemAndSecurity.cpp
    src/data/NetworkAndInternet.cpp
    src/data/HardwareAndSound.cpp
    src/data/Programs.cpp
    src/data/UserAccounts.cpp
    src/data/AppearanceAndPersonalization.cpp
    src/data/ClockLanguageRegion.cpp
    src/data/EaseOfAccess.cpp
    src/data/Branding.cpp
)

target_include_directories(controlpanel PRIVATE
    src/ui
    src/ui/pages
    src/ui/dialogs
    src/devices
    src/data
    src
)

target_link_libraries(controlpanel PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::DBus
    Qt6::OpenGL
    KF6::WidgetsAddons
    KF6::WindowSystem
    ZLIB::ZLIB
    OpenSSL::Crypto
    AeroQt
    Cups::Cups
)

set_target_properties(controlpanel PROPERTIES
    OUTPUT_NAME control
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

install(TARGETS controlpanel RUNTIME DESTINATION bin)
install(FILES linux-controlpanel.desktop DESTINATION share/applications)
