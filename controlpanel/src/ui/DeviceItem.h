#pragma once

#include <QString>

// A single tile shown in the Devices and Printers folder. It unifies real
// hardware (from the sysfs device scanner) and CUPS print queues into one
// display model, and carries the fields the properties dialog needs.
struct ShellDevice {
    QString name;          // display name / label under the icon
    QString iconName;      // theme icon name
    QString category;      // Win7-style category label ("Monitor", "Mouse"…)
    QString manufacturer;  // "Unavailable" when unknown
    QString model;         // usually the device name; make-and-model for printers
    QString modelNumber;   // "Unavailable" when unknown
    QString description;   // "Unavailable" when unknown
    QString status;        // "This device is working properly." etc.
    QString location;      // friendly sysfs location / printer location

    // Underlying device-manager detail, surfaced on the Hardware tab.
    QString driver;
    QString driverVersion;
    QString driverDate;
    QString rawLocation;

    bool isComputer       = false;
    bool isPrinter        = false;
    bool isDefaultPrinter = false;
};
