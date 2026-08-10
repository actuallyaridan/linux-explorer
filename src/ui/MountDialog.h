#pragma once

#include <QDialog>

class KFilePlacesModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// Connect and disconnect drives, through KFilePlacesModel's requestSetup() and
// requestTeardown() — the same calls the desktop's device notifier makes.
// Nothing mounts anything directly: privileges, the polkit prompt and the mount
// itself all live in Solid underneath.
//
// The drive holding the running system is never offered for disconnection.
class MountDialog : public QDialog {
    Q_OBJECT

public:
    explicit MountDialog(KFilePlacesModel *places, QWidget *parent = nullptr);

private:
    void reloadDrives();
    void updateButtons();
    void mountSelected();
    void unmountSelected();

    // The places row currently chosen, or an invalid index. Stored per entry
    // rather than assumed positional: the places model carries bookmarks and
    // remote entries between the devices.
    QModelIndex selectedPlace() const;

    KFilePlacesModel *m_places = nullptr;
    QComboBox   *m_drives = nullptr;
    QLabel      *m_status = nullptr;
    // The desktop's Device Auto-Mount switch, not a setting of this app's own.
    QCheckBox   *m_autoMount = nullptr;
    QPushButton *m_mount = nullptr;
    QPushButton *m_unmount = nullptr;
};
