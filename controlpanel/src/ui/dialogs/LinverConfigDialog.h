#pragma once

#include <QDialog>

class QComboBox;
class QCheckBox;

// The "Control Panel Settings" dialog, opened by clicking the distributor logo
// on the System page. A branding-style dropdown (real distro vs fake "Windows 7"
// identity, name, logo, copyright) plus one checkbox that renames every
// "Linux X" mention to "Windows X".
//
// On OK the choices are persisted through the Branding namespace; the caller
// re-navigates to pick them up (pages are rebuilt on navigation).
class LinverConfigDialog : public QDialog {
    Q_OBJECT

public:
    // `distroName` is the real distribution name shown as the first dropdown
    // entry (e.g. "CachyOS").
    explicit LinverConfigDialog(const QString &distroName,
                                QWidget *parent = nullptr);

private:
    void applyToSettings();

    QComboBox *m_styleCombo = nullptr;
    QCheckBox *m_windowsNames = nullptr;
};
