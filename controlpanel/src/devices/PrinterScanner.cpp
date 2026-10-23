#include "PrinterScanner.h"

#include <cups/cups.h>

namespace {

// printer-state is an IPP enum: 3 = idle, 4 = processing, 5 = stopped.
QString stateText(const QString &raw) {
    bool ok = false;
    int s = raw.toInt(&ok);
    if (!ok)
        return {};
    switch (s) {
    case 3:  return QStringLiteral("Ready");
    case 4:  return QStringLiteral("Printing");
    case 5:  return QStringLiteral("Paused");
    default: return {};
    }
}

} // namespace

QVector<Printer> scanPrinters() {
    QVector<Printer> out;

    cups_dest_t *dests = nullptr;
    int num = cupsGetDests(&dests);
    if (num <= 0 || !dests) {
        if (dests)
            cupsFreeDests(num, dests);
        return out;
    }

    for (int i = 0; i < num; ++i) {
        const cups_dest_t &d = dests[i];
        if (!d.name)
            continue;

        Printer p;
        p.name = QString::fromUtf8(d.name);
        if (d.instance && d.instance[0])
            p.name += QLatin1Char('/') + QString::fromUtf8(d.instance);
        p.isDefault = d.is_default != 0;

        auto opt = [&](const char *key) -> QString {
            const char *v = cupsGetOption(key, d.num_options, d.options);
            return v ? QString::fromUtf8(v) : QString();
        };

        p.info         = opt("printer-info");
        p.location     = opt("printer-location");
        p.makeAndModel = opt("printer-make-and-model");
        p.state        = stateText(opt("printer-state"));

        bool ok = false;
        unsigned long type = opt("printer-type").toULong(&ok);
        if (ok) {
            p.isFax   = (type & CUPS_PRINTER_FAX)   != 0;
            p.isClass = (type & CUPS_PRINTER_CLASS) != 0;
        }
        // Fall back to a name/model heuristic when printer-type is unavailable.
        if (!p.isFax) {
            const QString hay = (p.name + ' ' + p.makeAndModel).toLower();
            if (hay.contains(QLatin1String("fax")))
                p.isFax = true;
        }

        out.append(p);
    }

    cupsFreeDests(num, dests);
    return out;
}
