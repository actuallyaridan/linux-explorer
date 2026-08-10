#pragma once

#include <QDialog>
#include <QUrl>

class KFilePlacesModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Win7's "Map Network Drive". Separate from MountDialog, as in Windows: that
// one connects hardware already attached, this one names something that is not
// here yet.
//
// Nothing is mounted in the kernel sense. The share becomes a KIO location
// (smb://server/share and friends), which the rest of Explorer already
// navigates, and optionally a permanent places entry so it shows up under
// Network and survives a restart — Windows' "Reconnect at sign-in".
//
// UNC syntax is accepted alongside URLs: \\server\share is what the user will
// have written down.
class MapDriveDialog : public QDialog {
    Q_OBJECT

public:
    explicit MapDriveDialog(KFilePlacesModel *places, QWidget *parent = nullptr);

    // The share to navigate to once the dialog is accepted.
    QUrl mappedUrl() const { return m_mapped; }

    // Converts what the user typed into a location KIO can open: \\server\share,
    // //server/share, smb://server/share, a bare server name, or the other
    // protocols the folder-type box offers. Invalid when there is not enough
    // there to be a share. Public because it is this dialog's whole logic.
    static QUrl urlFor(const QString &input, const QString &scheme,
                       const QString &user);

private:
    void updateOkState();
    void accepted();

    KFilePlacesModel *m_places = nullptr;

    QComboBox *m_scheme = nullptr;
    QLineEdit *m_folder = nullptr;
    QLineEdit *m_label = nullptr;
    QLineEdit *m_user = nullptr;
    // Windows' "Reconnect at sign-in": whether the share is written into the
    // desktop's shared bookmarks rather than just opened once.
    QCheckBox *m_reconnect = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_ok = nullptr;

    QUrl m_mapped;
};
