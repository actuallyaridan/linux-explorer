#pragma once

#include <QString>
#include <QVector>

namespace ControlPanel {

// A print/fax queue as reported by CUPS. Populated by scanPrinters(), which
// enumerates the real destinations configured on the system (libcups)
struct Printer {
    // Queue name (with /instance suffix if any)
    QString name;
    // Printer-info, the friendly description
    QString info;
    // Printer-location
    QString location;
    // Printer-make-and-model (the driver/PPD name)
    QString makeAndModel;
    // "Ready", "Printing", "Paused", or empty
    QString state;
    bool    isDefault = false;
    bool    isFax     = false;
    bool    isClass   = false;
};

// Real print destinations from CUPS (cupsGetDests). Returns an empty list when
// no printers are configured or the CUPS scheduler is unreachable
QVector<Printer> scanPrinters();

} // namespace ControlPanel
