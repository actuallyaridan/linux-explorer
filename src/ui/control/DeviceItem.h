#pragma once

#include <QString>

namespace ControlPanel {

// A single tile shown in the Devices and Printers folder. It unifies real
// hardware (from the sysfs device scanner) and CUPS print queues into one
// display model, and carries the fields the properties dialog needs
struct ShellDevice {
    // Display name / label under the icon
    QString name;
    // Theme icon name
    QString iconName;
    // Win7-style category label ("Monitor", "Mouse"…)
    QString category;
    // "Unavailable" when unknown
    QString manufacturer;
    // Usually the device name; make-and-model for printers
    QString model;
    // "Unavailable" when unknown
    QString modelNumber;
    // "Unavailable" when unknown
    QString description;
    // "This device is working properly." etc
    QString status;
    // Friendly sysfs location / printer location
    QString location;

    // Underlying device-manager detail, surfaced on the Hardware tab
    QString driver;
    QString driverVersion;
    QString driverDate;
    QString rawLocation;

    bool isComputer       = false;
    bool isPrinter        = false;
    bool isDefaultPrinter = false;
};

} // namespace ControlPanel
