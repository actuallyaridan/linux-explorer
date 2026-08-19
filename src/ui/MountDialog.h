#pragma once

#include "aero/taskdialog.h"

class KFilePlacesModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// Connect and disconnect drives through the same calls the desktop's device
// notifier makes, the privileges, the prompt and the mount all living beneath
//
// The drive holding the running system is never offered
class MountDialog : public Aero::TaskDialog {
    Q_OBJECT

public:
    explicit MountDialog(KFilePlacesModel *places, QWidget *parent = nullptr);

private:
    void reloadDrives();
    void updateButtons();
    void mountSelected();
    void unmountSelected();

    // Stored per entry rather than assumed positional, the places model
    // carrying bookmarks and remote entries between the devices
    QModelIndex selectedPlace() const;

    KFilePlacesModel *m_places = nullptr;
    QComboBox   *m_drives = nullptr;
    QLabel      *m_status = nullptr;
    // The desktop's own auto mount switch rather than a setting of ours
    QCheckBox   *m_autoMount = nullptr;
    QPushButton *m_mount = nullptr;
    QPushButton *m_unmount = nullptr;
};
