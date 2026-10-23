#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

#include "ui/control/DeviceItem.h"
#include "model/control/DeviceData.h"      // DeviceCategory
#include "model/control/PrinterScanner.h"  // Printer

class QListWidget;
class QListWidgetItem;
class QLabel;

class DetailsPane;

namespace ControlPanel {

// The Windows-7 "Devices and Printers" shell folder, recreated as a Control
// Panel page. Unlike the other detail pages it has no left navigation pane,
// it mirrors Explorer's special-folder chrome: a command bar (Add a device /
// Add a printer), a grouped icon view (Devices, Printers and Faxes), and a
// details pane pinned to the bottom
//
// All data is real: hardware comes from the sysfs device scanner ported from
// linux-devmgmt, and print queues come from CUPS. The scan runs on a worker
// thread so navigating to the page never blocks the UI
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

    // The device a tile stands for, or null for a tile out of step with the
    // lists after a rescan
    const ShellDevice *shellDeviceFor(QListWidgetItem *item) const;
    void onSelection(QListWidget *active, QListWidget *other);
    void launchAddPrinter();
    void launchAddDevice();

    QLabel      *m_devicesHeader  = nullptr;
    QLabel      *m_printersHeader = nullptr;
    QListWidget *m_devicesList    = nullptr;
    QListWidget *m_printersList   = nullptr;
    QLabel      *m_statusLabel    = nullptr;

    // Bottom details pane
    DetailsPane *m_details = nullptr;

    QVector<ShellDevice> m_devices;
    QVector<ShellDevice> m_printers;
};

} // namespace ControlPanel
