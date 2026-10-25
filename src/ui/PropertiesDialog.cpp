#include "PropertiesDialog.h"
#include "MessageBoxes.h"
#include "aero/text.h"

#include <KApplicationTrader>
#include <KIO/CopyJob>
#include <KIO/DirectorySizeJob>
#include <KIO/FileUndoManager>
#include <KIO/Global>
#include <KOpenWithDialog>

#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include <sys/stat.h>
#include <unistd.h>

namespace {

// Win7's Properties does not resize, and this is its size inside the frame
constexpr int kDialogWidth = 362;
constexpr int kDialogHeight = 450;

// Win7's buttons are all this size, wider labels included
constexpr int kButtonWidth = 75;
constexpr int kButtonHeight = 23;

// Where the labels start inside the tab, and how far the values sit from them
constexpr int kTabMargin = 14;
constexpr int kLabelColumnWidth = 62;
constexpr int kColumnGap = 10;

// Sampled from Win7's separator lines
constexpr auto kSeparatorColor = "#A0A0A0";

// Often enough that the numbers look like they are counting, without
// relaying out the labels on every file the walk passes
constexpr int kCountInterval = 150;

// Win7 writes a size as "10,1 MB (10 629 632 bytes)"
QString sizeText(KIO::filesize_t bytes)
{
    if (bytes < 1024) {
        return QObject::tr("%1 bytes (%1 bytes)").arg(bytes);
    }

    double value = bytes;
    const QStringList units = {QObject::tr("KB"), QObject::tr("MB"), QObject::tr("GB"), QObject::tr("TB")};
    int unit = 0;
    value = value / 1024;
    while (value >= 1024) {
        value = value / 1024;
        unit++;
    }

    int decimals = 0;
    if (value <10) {
        decimals = 2;
    } else if (value <100) {
        decimals = 1;
    } else { 
        decimals = 0;
    }

    const double factor = std::pow(10, decimals);
    value = std::floor(value * factor) / factor;
    const QString number = QLocale::system().toString(value, 'f', decimals);
    const QString shortText = QObject::tr("%1 %2").arg(number).arg(units[unit]);

    const QString exact = QLocale::system().toString(qulonglong(bytes));
    return QObject::tr("%1 (%2 bytes)").arg(shortText, exact);
}

// Win7 gives the long date with the seconds, as in "14 July 2009, 05:20:08"
QString dateText(const QDateTime &when)
{
    const QLocale locale = QLocale::system();

    // The long time format ends in the time zone, which Win7 leaves off
    QString timeFormat = locale.timeFormat(QLocale::LongFormat);
    timeFormat.remove(QLatin1Char('t'));
    timeFormat = timeFormat.trimmed();

    const QString date = locale.toString(when.date(), QLocale::LongFormat);
    const QString time = locale.toString(when.time(), timeFormat);
    return QObject::tr("%1, %2").arg(date, time);
}

// Allocated blocks rather than the length, so a sparse or compressed file
// reads smaller here than its size, as it does on Windows
std::optional<KIO::filesize_t> sizeOnDisk(const QString &path)
{
    struct stat info;
    const QByteArray encoded = QFile::encodeName(path);
    if (::stat(encoded.constData(), &info) != 0)
        return std::nullopt;

    // st_blocks always counts 512 byte units, whatever the block size
    return KIO::filesize_t(info.st_blocks) * 512;
}

// Win7 names the type and then the extension, as in "Text Document (.txt)"
QString typeText(const KFileItem &item)
{
    if (item.isDir())
        return QObject::tr("File folder");

    const QString suffix = QFileInfo(item.name()).suffix();
    if (suffix.isEmpty())
        return item.mimeComment();

    return QObject::tr("%1 (.%2)").arg(item.mimeComment(), suffix);
}

// The values can be selected and copied, as the location often is
QLabel *valueLabel(const QString &text)
{
    QLabel *value = Aero::label(text);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return value;
}

QCheckBox *check(const QString &text)
{
    auto *box = new QCheckBox(text);
    Aero::setPointSize(box, 9);
    return box;
}

// On Linux a file is hidden when its name starts with a dot, so ticking
// Hidden turns "notes.txt" into ".notes.txt" and unticking it turns it back
QString hiddenName(const QString &name, bool hidden)
{
    if (hidden) {
        return "."+name;
    } else {
        return name.mid(1);
    }
}

} // namespace

PropertiesDialog::PropertiesDialog(const KFileItem &item, QWidget *parent)
    : QDialog(parent)
    , m_item(item)
{
    // Win7 names the window after the file, as in "Notes.txt Properties"
    setWindowTitle(tr("%1 Properties").arg(item.name()));
    setWindowIcon(QIcon::fromTheme(item.iconName()));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(7, 7, 7, 9);
    root->setSpacing(9);

    auto *tabs = new QTabWidget;
    Aero::setPointSize(tabs, 9);
    tabs->addTab(buildGeneralTab(), tr("General"));
    root->addWidget(tabs, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);

    m_ok = new QPushButton(tr("OK"));
    m_cancel = new QPushButton(tr("Cancel"));
    m_apply = new QPushButton(tr("Apply"));
    m_ok->setDefault(true);
    // Win7 starts with Apply greyed and enables it once something is touched
    m_apply->setEnabled(false);
    for (QPushButton *button : {m_ok, m_cancel, m_apply}) {
        Aero::setPointSize(button, 9);
        button->setFixedSize(kButtonWidth, kButtonHeight);
        buttons->addWidget(button);
    }
    root->addLayout(buttons);

    connect(m_ok, &QPushButton::clicked, this, [this] {
        applyChanges(true);
    });
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, [this] {
        applyChanges(false);
    });

    const auto touched = [this] { m_apply->setEnabled(true); };
    connect(m_readOnly, &QCheckBox::toggled, this, touched);
    connect(m_hidden, &QCheckBox::toggled, this, touched);
    connect(m_name, &QLineEdit::textEdited, this, touched);

    setFixedSize(kDialogWidth, kDialogHeight);

    if (m_item.isDir())
        startCounting();
}

PropertiesDialog::~PropertiesDialog()
{
    // Closing the dialog partway through a big folder would otherwise leave
    // the walk running with nobody to read the answer
    if (m_sizeJob)
        m_sizeJob->kill();
}

QWidget *PropertiesDialog::buildGeneralTab()
{
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);
    grid->setContentsMargins(kTabMargin, 12, kTabMargin, kTabMargin);
    grid->setHorizontalSpacing(kColumnGap);
    grid->setVerticalSpacing(10);
    grid->setColumnMinimumWidth(0, kLabelColumnWidth);
    grid->setColumnStretch(1, 1);

    int row = 0;

    grid->addWidget(buildHeader(), row, 0, 1, 2);
    ++row;
    addSeparator(grid, row);

    // Folders and files word the first row differently, and a file adds
    // the program it opens in
    if (m_item.isDir()) {
        addRow(grid, row, tr("Type:"), typeText(m_item));
    } else {
        addRow(grid, row, tr("Type of file:"), typeText(m_item));

        // Centred rather than on the top line, the Change... button making
        // this row taller than the others
        auto *opensWithKey = Aero::label(tr("Opens with:"));
        grid->addWidget(opensWithKey, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(buildOpensWith(), row, 1);
        ++row;
    }
    addSeparator(grid, row);

    // A borderless read-only box, as Win7's is, so a long path can be
    // scrolled through and copied instead of being cut off
    const QString parentFolder = QFileInfo(m_item.localPath()).absolutePath();
    auto *location = new QLineEdit(parentFolder);
    Aero::setPointSize(location, 9);
    location->setReadOnly(true);
    location->setFrame(false);
    location->setStyleSheet(
        QStringLiteral("QLineEdit { color: %1; background: transparent;"
                       " border: none; padding: 0; border-image: none; }")
            .arg(QLatin1String(Aero::Palette::Text)));
    // Qt keeps a small margin inside every text box, which would set the path
    // a little to the right of the values under it
    location->setTextMargins(-2, 0, 0, 0);
    location->setCursorPosition(0);
    grid->addWidget(Aero::label(tr("Location:")), row, 0,
                    Qt::AlignLeft | Qt::AlignTop);
    grid->addWidget(location, row, 1, Qt::AlignTop);
    ++row;

    // A folder's three start at nothing and count up, see startCounting()
    if (m_item.isDir()) {
        m_sizeValue = addRow(grid, row, tr("Size:"), sizeText(0));
        m_sizeOnDiskValue = addRow(grid, row, tr("Size on disk:"), sizeText(0));
        m_containsValue = addRow(grid, row, tr("Contains:"), QString());
    } else {
        addRow(grid, row, tr("Size:"), sizeText(m_item.size()));

        const std::optional<KIO::filesize_t> onDisk =
            sizeOnDisk(m_item.localPath());
        if (onDisk)
            addRow(grid, row, tr("Size on disk:"), sizeText(*onDisk));
    }
    addSeparator(grid, row);

    // Win7 shows a folder's creation date only, and a filesystem that never
    // recorded one leaves the row out rather than inventing it
    const QDateTime created = m_item.time(KFileItem::CreationTime);
    if (created.isValid())
        addRow(grid, row, tr("Created:"), dateText(created));

    if (!m_item.isDir()) {
        const QDateTime modified = m_item.time(KFileItem::ModificationTime);
        if (modified.isValid())
            addRow(grid, row, tr("Modified:"), dateText(modified));

        const QDateTime accessed = m_item.time(KFileItem::AccessTime);
        if (accessed.isValid())
            addRow(grid, row, tr("Accessed:"), dateText(accessed));
    }
    addSeparator(grid, row);

    // On Linux, read-only is whether you may write to it, and hidden is a
    // name starting with a dot
    m_readOnly = check(tr("Read-only"));
    m_readOnly->setChecked(!m_item.isWritable());
    m_hidden = check(tr("Hidden"));
    m_hidden->setChecked(m_item.isHidden());

    // Only the owner may change a file's permissions, and hiding renames it,
    // which needs the folder it is in to be writable, so a box that could
    // only ever fail is greyed instead
    const QFileInfo info(m_item.localPath());
    const bool ownsIt = info.ownerId() == ::getuid();
    const bool canRename = QFileInfo(info.absolutePath()).isWritable();
    m_readOnly->setEnabled(ownsIt);
    m_hidden->setEnabled(canRename);

    auto *attributes = new QWidget;
    auto *attributesLayout = new QHBoxLayout(attributes);
    attributesLayout->setContentsMargins(0, 0, 0, 0);
    attributesLayout->setSpacing(16);
    attributesLayout->addWidget(m_readOnly);
    attributesLayout->addWidget(m_hidden);
    attributesLayout->addStretch(1);

    auto *attributesKey = Aero::label(tr("Attributes:"));
    grid->addWidget(attributesKey, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(attributes, row, 1);
    ++row;

    // Everything above keeps to the top, the space taking up the rest
    grid->setRowStretch(row, 1);

    return page;
}

QWidget *PropertiesDialog::buildHeader()
{
    auto *header = new QWidget;
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kColumnGap);

    auto *icon = new QLabel;
    icon->setPixmap(QIcon::fromTheme(m_item.iconName()).pixmap(32, 32));
    // As wide as the label column, so the name box lines up with the values
    icon->setFixedSize(kLabelColumnWidth, 32);
    icon->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(icon);

    m_name = new QLineEdit(m_item.name());
    Aero::setPointSize(m_name, 9);
    // Renaming needs the folder the item is in to be writable
    const QString folder = QFileInfo(m_item.localPath()).absolutePath();
    m_name->setReadOnly(!QFileInfo(folder).isWritable());
    layout->addWidget(m_name, 1);

    return header;
}

QWidget *PropertiesDialog::buildOpensWith()
{
    auto *opensWith = new QWidget;
    auto *layout = new QHBoxLayout(opensWith);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_opensWithIcon = new QLabel;
    m_opensWithIcon->setFixedSize(16, 16);
    layout->addWidget(m_opensWithIcon);

    m_opensWithName = Aero::label(QString());
    layout->addWidget(m_opensWithName, 1);

    auto *change = new QPushButton(tr("Change..."));
    Aero::setPointSize(change, 9);
    change->setFixedSize(kButtonWidth, kButtonHeight);
    connect(change, &QPushButton::clicked,
            this, &PropertiesDialog::changeOpensWith);
    layout->addWidget(change);

    const KService::Ptr current =
        KApplicationTrader::preferredService(m_item.mimetype());
    showOpensWith(current);
    return opensWith;
}

void PropertiesDialog::showOpensWith(const KService::Ptr &service)
{
    if (!service) {
        m_opensWithName->setText(tr("Unknown application"));
    } else {
        m_opensWithName->setText(service->name());
        m_opensWithIcon->setPixmap(QIcon::fromTheme(service->icon()).pixmap(16, 16));
    }
}

void PropertiesDialog::changeOpensWith()
{
    const QString mimeType = m_item.mimetype();

    // KDE's chooser for now, which a Win7 one could replace later on
    KOpenWithDialog chooser(mimeType, QString(), this);
    // A command typed in rather than picked from the list gets a launcher
    // made for it, since only a launcher can be remembered as the default
    chooser.setSaveNewApplications(true);
    if (chooser.exec() != QDialog::Accepted)
        return;

    const KService::Ptr service = chooser.service();
    if (!service)
        return;

    KApplicationTrader::setPreferredService(mimeType, service);
    showOpensWith(service);
}

QLabel *PropertiesDialog::addRow(QGridLayout *grid, int &row,
                                 const QString &key, const QString &value)
{
    QLabel *keyLabel = Aero::label(key);
    QLabel *valueText = valueLabel(value);
    grid->addWidget(keyLabel, row, 0, Qt::AlignLeft | Qt::AlignTop);
    grid->addWidget(valueText, row, 1, Qt::AlignTop);
    ++row;
    return valueText;
}

void PropertiesDialog::addSeparator(QGridLayout *grid, int &row)
{
    grid->addWidget(Aero::hairline(kSeparatorColor), row, 0, 1, 2);
    ++row;
}

void PropertiesDialog::startCounting()
{
    // A KIO job walks the folder on another thread and keeps running totals,
    // which the timer reads so the numbers climb as Win7's do
    m_sizeJob = KIO::directorySize(m_item.url());

    m_countTimer = new QTimer(this);
    m_countTimer->setInterval(kCountInterval);
    connect(m_countTimer, &QTimer::timeout, this, &PropertiesDialog::showCount);

    // The job deletes itself after this, so the final totals are read here
    connect(m_sizeJob, &KJob::result, this, [this] {
        m_countTimer->stop();
        showCount();
    });

    showCount();
    m_countTimer->start();
}

void PropertiesDialog::showCount()
{
    if (!m_sizeJob)
        return;

    m_sizeValue->setText(sizeText(m_sizeJob->totalSize()));

    // Only local folders can say how much space they take up
    const std::optional<KIO::filesize_t> onDisk = m_sizeJob->totalSizeOnDisk();
    if (onDisk)
        m_sizeOnDiskValue->setText(sizeText(*onDisk));
    else
        m_sizeOnDiskValue->setText(tr("Unknown"));

    const QLocale locale = QLocale::system();
    const QString files = locale.toString(qulonglong(m_sizeJob->totalFiles()));
    const QString folders = locale.toString(qulonglong(m_sizeJob->totalSubdirs()));
    m_containsValue->setText(tr("%1 Files, %2 Folders").arg(files, folders));
}

void PropertiesDialog::applyChanges(bool closeWhenDone)
{
    const QString renameTitle = tr("Error Renaming File or Folder");

    // Checked before anything is changed, so a bad name leaves read-only as
    // it was too
    const QString typedName = m_name->text();
    if (typedName.isEmpty()) {
        MessageBoxes::showError(this, renameTitle,
                                tr("You must type a file name."));
        return;
    }
    const bool isDotName = typedName == QLatin1String(".")
        || typedName == QLatin1String("..");
    if (typedName.contains(QLatin1Char('/')) || isDotName) {
        MessageBoxes::showError(this, renameTitle,
                                tr("A file name cannot contain a / or be "
                                   "just dots."));
        return;
    }

    // Read-only first, while the item is still under its old name
    const bool wantReadOnly = m_readOnly->isChecked();
    const bool isReadOnly = !m_item.isWritable();
    if (wantReadOnly != isReadOnly) {
        // Read-only takes write away from everyone, and clearing it gives
        // the owner write back, leaving whatever the group and others had
        mode_t mode = m_item.permissions();
        if (wantReadOnly)
            mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
        else
            mode |= S_IWUSR;

        const QByteArray encoded = QFile::encodeName(m_item.localPath());
        if (::chmod(encoded.constData(), mode) != 0) {
            MessageBoxes::showError(
                this, tr("Error Applying Attributes"),
                tr("Cannot change the attributes of %1.").arg(m_item.name()),
                qt_error_string(errno));
            return;
        }
        m_item.refresh();
    }

    // Hidden is part of the name on Linux, so only touched when the box was
    // actually changed, leaving a dot typed into the name alone
    QString newName = typedName;
    const bool wantHidden = m_hidden->isChecked();
    if (wantHidden != m_item.isHidden())
        newName = hiddenName(newName, wantHidden);

    if (newName == m_item.name()) {
        finishApplying(closeWhenDone);
        return;
    }

    QUrl target = m_item.url().adjusted(QUrl::RemoveFilename);
    target.setPath(target.path() + newName);

    // Win7 refuses rather than offering to overwrite, which KIO's own
    // prompt would do if the job were left to find out
    if (QFileInfo::exists(target.toLocalFile())) {
        MessageBoxes::showError(this, renameTitle,
                                tr("There is already a file with the same "
                                   "name in this location."));
        return;
    }

    KIO::CopyJob *job = KIO::moveAs(m_item.url(), target, KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    setBusy(true);

    connect(job, &KJob::result, this,
            [this, target, newName, closeWhenDone, renameTitle](KJob *finished) {
        setBusy(false);

        if (finished->error()) {
            MessageBoxes::showError(
                this, renameTitle,
                tr("Cannot rename %1.").arg(m_item.name()),
                finished->errorString());
            return;
        }

        // The item now lives under its new name, so a second Apply starts
        // from there rather than renaming a file that is gone
        m_item.setUrl(target);
        m_item.setName(newName);
        m_item.refresh();
        m_name->setText(newName);
        m_hidden->setChecked(m_item.isHidden());
        setWindowTitle(tr("%1 Properties").arg(newName));

        finishApplying(closeWhenDone);
    });
}

void PropertiesDialog::finishApplying(bool closeWhenDone)
{
    if (closeWhenDone) {
        accept();
        return;
    }

    // Everything on screen is now what is on disk
    m_apply->setEnabled(false);
}

void PropertiesDialog::setBusy(bool busy)
{
    m_ok->setEnabled(!busy);
    m_apply->setEnabled(!busy);
}
