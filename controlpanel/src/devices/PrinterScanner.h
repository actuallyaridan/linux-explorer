#pragma once

#include <QString>
#include <QVector>

// A print/fax queue as reported by CUPS. Populated by scanPrinters(), which
// enumerates the real destinations configured on the system (libcups).
struct Printer {
    QString name;          // queue name (with /instance suffix if any)
    QString info;          // printer-info, the friendly description
    QString location;      // printer-location
    QString makeAndModel;  // printer-make-and-model (the driver/PPD name)
    QString state;         // "Ready", "Printing", "Paused", or empty
    bool    isDefault = false;
    bool    isFax     = false;
    bool    isClass   = false;
};

// Real print destinations from CUPS (cupsGetDests). Returns an empty list when
// no printers are configured or the CUPS scheduler is unreachable.
QVector<Printer> scanPrinters();
