#pragma once

#include "aero/taskdialog.h"
#include <QUrl>

class KFilePlacesModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Win7's map network drive, where nothing is mounted in the kernel sense, the
// share becoming a KIO location and optionally a permanent places entry
class MapDriveDialog : public Aero::TaskDialog {
    Q_OBJECT

public:
    explicit MapDriveDialog(KFilePlacesModel *places, QWidget *parent = nullptr);

    QUrl mappedUrl() const { return m_mapped; }

    // What the user typed as a location KIO can open, or invalid when there is
    // not enough there to be a share, and public because it is the whole logic
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
    // Windows reconnect at sign in
    QCheckBox *m_reconnect = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_ok = nullptr;

    QUrl m_mapped;
};
