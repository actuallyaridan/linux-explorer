#pragma once

#include <QDialog>
#include "DeviceItem.h"

class QShowEvent;

// The Windows-7 "Devices and Printers" device property sheet: a General tab
// (Device Information + Device Tasks) and a Hardware tab (Device Functions +
// summary). Opened by double-clicking a tile in DevicesAndPrintersPage.
class DevicePropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit DevicePropertiesDialog(const ShellDevice &dev,
                                    QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *e) override;

private:
    QWidget *buildGeneralTab();
    QWidget *buildHardwareTab();
    void     showFunctionProperties();

    ShellDevice m_dev;
    bool        m_sizeAdjusted = false;
};
