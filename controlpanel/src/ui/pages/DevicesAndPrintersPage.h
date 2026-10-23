#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

#include "DeviceItem.h"
#include "DeviceData.h"      // DeviceCategory
#include "PrinterScanner.h"  // Printer

class QListWidget;
class QLabel;

// The Windows-7 "Devices and Printers" shell folder, recreated as a Control
// Panel page. Unlike the other detail pages it has no left navigation pane,
// it mirrors Explorer's special-folder chrome: a command bar (Add a device /
// Add a printer), a grouped icon view (Devices, Printers and Faxes), and a
// details pane pinned to the bottom.
//
// All data is real: hardware comes from the sysfs device scanner ported from
// linux-devmgmt, and print queues come from CUPS. The scan runs on a worker
// thread so navigating to the page never blocks the UI.
class DevicesAndPrintersPage : public QWidget {
    Q_OBJECT
public:
    explicit DevicesAndPrintersPage(QWidget *parent = nullptr);

private:
    struct ScanResult {
        QString                 hostName;
        QVector<DeviceCategory> categories;
        QVector<Printer>        printers;
    };
    static ScanResult runScan();

    void populate(const ScanResult &result);
    void addTiles(QListWidget *list, const QVector<ShellDevice> &items,
                  bool printers);
    void updateDetails(const ShellDevice *dev);
    void onSelection(QListWidget *active, QListWidget *other);
    void launchAddPrinter();
    void launchAddDevice();

    QLabel      *m_devicesHeader  = nullptr;
    QLabel      *m_printersHeader = nullptr;
    QListWidget *m_devicesList    = nullptr;
    QListWidget *m_printersList   = nullptr;
    QLabel      *m_statusLabel    = nullptr;

    // Bottom details pane.
    QLabel *m_detailIcon  = nullptr;
    QLabel *m_detailName  = nullptr;
    QLabel *m_detailLine1 = nullptr;
    QLabel *m_detailLine2 = nullptr;

    QVector<ShellDevice> m_devices;
    QVector<ShellDevice> m_printers;
};
