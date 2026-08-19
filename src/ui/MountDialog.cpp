#include "MountDialog.h"
#include "AutoMount.h"
#include "DriveLabel.h"
#include "aero/text.h"

#include <KFilePlacesModel>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int kPlaceRowRole = Qt::UserRole + 1;

// The drive the running system is on, never offered for disconnection
bool isSystemDrive(const QUrl &mountPoint)
{
    return mountPoint.isLocalFile()
        && QDir::cleanPath(mountPoint.toLocalFile()) == QLatin1String("/");
}

} // namespace

MountDialog::MountDialog(KFilePlacesModel *places, QWidget *parent)
    : Aero::TaskDialog(parent)
    , m_places(places)
{
    setWindowTitle(tr("Connect or Disconnect a Drive"));

    contentLayout()->addWidget(Aero::label(tr("Select a drive, then mount or unmount it."), 9));

    m_drives = new QComboBox;
    Aero::setPointSize(m_drives, 9);
    contentLayout()->addWidget(m_drives);

    m_status = Aero::label(QString(), 9, Aero::Palette::MutedText);
    m_status->setWordWrap(true);
    contentLayout()->addWidget(m_status);

    // The desktop's own auto mount switch rather than a setting of ours
    m_autoMount = new QCheckBox(tr("Mount drives automatically in the future"));
    Aero::setPointSize(m_autoMount, 9);
    m_autoMount->setChecked(AutoMount::onAttachEnabled());
    if (AutoMount::isConfigurable()) {
        m_autoMount->setToolTip(
            tr("Mounts every drive as soon as it is attached, without asking. "
               "This is the desktop's Device Auto-Mount setting, shared with "
               "System Settings."));
    } else {
        m_autoMount->setEnabled(false);
        m_autoMount->setToolTip(
            tr("This setting has been locked by the system administrator."));
    }
    contentLayout()->addWidget(m_autoMount);
    contentLayout()->addStretch(1);

    m_mount = addButton(tr("Mount"));
    m_unmount = addButton(tr("Unmount"));
    auto *close = addButton(tr("OK"));

    connect(m_drives, &QComboBox::currentIndexChanged, this, &MountDialog::updateButtons);
    // On the spot rather than on OK, as the other two buttons are
    connect(m_autoMount, &QCheckBox::toggled, this, [](bool on) {
        AutoMount::setOnAttachEnabled(on);
    });
    connect(m_mount, &QPushButton::clicked, this, &MountDialog::mountSelected);
    connect(m_unmount, &QPushButton::clicked, this, &MountDialog::unmountSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    // Mounting is asynchronous and may prompt first, so the list is rebuilt
    // when the model reports the outcome
    connect(m_places, &KFilePlacesModel::setupDone, this,
            [this](const QModelIndex &, bool success) {
        m_status->setText(success ? tr("Drive mounted.")
                                  : tr("The drive could not be mounted."));
        reloadDrives();
    });
    connect(m_places, &QAbstractItemModel::dataChanged, this, &MountDialog::reloadDrives);
    connect(m_places, &QAbstractItemModel::rowsInserted, this, &MountDialog::reloadDrives);
    connect(m_places, &QAbstractItemModel::rowsRemoved, this, &MountDialog::reloadDrives);

    reloadDrives();
    resize(420, sizeHint().height());
}

void MountDialog::reloadDrives()
{
    const int previousRow = m_drives->currentData(kPlaceRowRole).toInt();

    QSignalBlocker blocker(m_drives);
    m_drives->clear();

    for (int row = 0; row < m_places->rowCount(); ++row) {
        const QModelIndex index = m_places->index(row, 0);
        if (!m_places->isDevice(index) || m_places->isHidden(index))
            continue;

        // With the device node, so two identically labelled partitions can be
        // told apart before one is mounted
        const QString name = DriveLabel::forPlace(m_places, index);
        const bool needsSetup = m_places->setupNeeded(index);
        const QString label = needsSetup ? tr("%1 (not mounted)").arg(name) : name;

        m_drives->addItem(m_places->icon(index), label);
        m_drives->setItemData(m_drives->count() - 1, row, kPlaceRowRole);
    }

    if (m_drives->count() == 0) {
        m_drives->addItem(tr("No drives found"));
        m_drives->setEnabled(false);
    } else {
        m_drives->setEnabled(true);
        for (int i = 0; i < m_drives->count(); ++i) {
            if (m_drives->itemData(i, kPlaceRowRole).toInt() == previousRow) {
                m_drives->setCurrentIndex(i);
                break;
            }
        }
    }

    blocker.unblock();
    updateButtons();
}

QModelIndex MountDialog::selectedPlace() const
{
    const QVariant row = m_drives->currentData(kPlaceRowRole);
    if (!row.isValid())
        return {};
    return m_places->index(row.toInt(), 0);
}

void MountDialog::updateButtons()
{
    const QModelIndex index = selectedPlace();
    if (!index.isValid()) {
        m_mount->setEnabled(false);
        m_unmount->setEnabled(false);
        return;
    }

    const bool needsSetup = m_places->setupNeeded(index);
    const bool systemDrive = isSystemDrive(m_places->url(index));

    m_mount->setEnabled(needsSetup);
    m_unmount->setEnabled(!needsSetup && !systemDrive);

    if (systemDrive) {
        m_status->setText(tr("This is a system drive and cannot "
                             "be unmounted."));
    } else if (needsSetup) {
        m_status->setText(tr("This drive is not mounted."));
    } else {
        m_status->setText(tr("Mounted at %1.").arg(
            m_places->url(index).toLocalFile()));
    }
}

void MountDialog::mountSelected()
{
    const QModelIndex index = selectedPlace();
    if (!index.isValid() || !m_places->setupNeeded(index))
        return;

    m_status->setText(tr("Mounting..."));
    m_places->requestSetup(index);
}

void MountDialog::unmountSelected()
{
    const QModelIndex index = selectedPlace();
    if (!index.isValid() || m_places->setupNeeded(index))
        return;

    // A model reload could have changed the selection since the click
    if (isSystemDrive(m_places->url(index)))
        return;

    m_status->setText(tr("Unmounting..."));
    m_places->requestTeardown(index);
}
