#pragma once
#include <QHash>
#include <QString>

namespace ControlPanel {

// Release dates for unmaintained/legacy drivers whose upstream release date
// cannot be determined from the local system. Each entry maps the exact version
// string (as reported by `modinfo -F version`) to the ISO 8601 date on which
// that version was officially published by the vendor
//
// To add a new entry when a driver reaches end-of-life:
//   {"<version>", "<YYYY-MM-DD>"},
//
// Sources:
//   NVIDIA legacy:  https://www.nvidia.com/en-us/drivers/unix/legacy-gpu/
//   Broadcom STA:   https://docs.broadcom.com/doc/802-11-linux-sta-wireless-driver-release-notes
inline const QHash<QString, QString> &knownDriverDates() {
    static const QHash<QString, QString> table{
        // Broadcom STA
        {"6.30.223.271", "2015-09-18"},

        // NVIDIA legacy branches (final release per branch)
        // 470.xx, last branch still maintained
        {"470.256.02",   "2024-06-04"},
        // 390.xx
        {"390.157",      "2022-11-22"},
        // 340.xx
        {"340.108",      "2019-12-23"},
        // 304.xx
        {"304.137",      "2017-09-19"},
        // 173.xx
        {"173.14.39",    "2017-05-22"},
        // 96.xx
        {"96.43.23",     "2016-05-31"},
        // 71.xx
        {"71.86.15",     "2014-01-07"},
    };
    return table;
}

} // namespace ControlPanel
