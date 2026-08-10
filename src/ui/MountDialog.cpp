#include "MountDialog.h"
#include "AutoMount.h"
#include "DriveLabel.h"
#include "Win7Ui.h"

#include <KFilePlacesModel>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// The row of the places model a combo entry refers to.
constexpr int kPlaceRowRole = Qt::UserRole + 1;

// The drive the running system is on, never offered for disconnection.
bool isSystemDrive(const QUrl &mountPoint)
{
    return mountPoint.isLocalFile()
        && QDir::cleanPath(mountPoint.toLocalFile()) == QLatin1String("/");
}

} // namespace

MountDialog::MountDialog(KFilePlacesModel *places, QWidget *parent)
    : QDialog(parent)
    , m_places(places)
{
    setWindowTitle(tr("Connect or Disconnect a Drive"));
    setModal(true);

    // Win7 splits a dialog in two: a white content area, and a grey command
    // strip along the bottom carrying only the buttons.
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("mountContent"));
    // ID-scoped: a declaration-only sheet acts as `* { ... }` and would drag the
    // combo box into the stylesheet engine.
    content->setStyleSheet("#mountContent { background: #FFFFFF; }");

    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(10);

    contentLayout->addWidget(Win7::label(tr("Select a drive, then mount or unmount it."), 9));

    m_drives = new QComboBox;
    Win7::setPointSize(m_drives, 9);
    contentLayout->addWidget(m_drives);

    m_status = Win7::label(QString(), 9, "#5A5A5A");
    m_status->setWordWrap(true);
    contentLayout->addWidget(m_status);

    // The desktop's own auto-mount switch, shown here because mounting a drive
    // by hand is the moment a user wants to stop having to. The All Devices /
    // On Attach box and nothing else on that page; see AutoMount.h.
    m_autoMount = new QCheckBox(tr("Mount drives automatically in the future"));
    Win7::setPointSize(m_autoMount, 9);
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
    contentLayout->addWidget(m_autoMount);
    contentLayout->addStretch(1);

    root->addWidget(content, 1);

    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("mountFooter"));
    footer->setStyleSheet(
        "#mountFooter { background: #F0F0F0; border-top: 1px solid #DFDFDF; }");

    auto *buttons = new QHBoxLayout(footer);
    buttons->setContentsMargins(12, 10, 12, 10);
    buttons->setSpacing(8);
    buttons->addStretch(1);

    m_mount = new QPushButton(tr("Mount"));
    m_unmount = new QPushButton(tr("Unmount"));
    auto *close = new QPushButton(tr("OK"));
    for (QPushButton *b : {m_mount, m_unmount, close})
        Win7::setPointSize(b, 9);
    buttons->addWidget(m_mount);
    buttons->addWidget(m_unmount);
    buttons->addWidget(close);

    root->addWidget(footer);

    connect(m_drives, &QComboBox::currentIndexChanged, this, &MountDialog::updateButtons);
    // On the spot rather than on OK, as the other two buttons are.
    connect(m_autoMount, &QCheckBox::toggled, this, [](bool on) {
        AutoMount::setOnAttachEnabled(on);
    });
    connect(m_mount, &QPushButton::clicked, this, &MountDialog::mountSelected);
    connect(m_unmount, &QPushButton::clicked, this, &MountDialog::unmountSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    // Mounting is asynchronous and may prompt for a password first, so the list
    // is rebuilt when the model reports the outcome.
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
    // The list is rebuilt whenever a drive appears, disappears or changes state.
    const int previousRow = m_drives->currentData(kPlaceRowRole).toInt();

    QSignalBlocker blocker(m_drives);
    m_drives->clear();

    for (int row = 0; row < m_places->rowCount(); ++row) {
        const QModelIndex index = m_places->index(row, 0);
        if (!m_places->isDevice(index) || m_places->isHidden(index))
            continue;

        // With the device node: this is where two identically labelled
        // partitions have to be told apart before one is mounted.
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

    // Re-checked rather than trusting the disabled button: a model reload could
    // have changed the selection between the click and this call.
    if (isSystemDrive(m_places->url(index)))
        return;

    m_status->setText(tr("Unmounting..."));
    m_places->requestTeardown(index);
}
