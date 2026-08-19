#include "MapDriveDialog.h"
#include "aero/text.h"

#include <KFilePlacesModel>
#include <KProtocolInfo>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// The label, scheme and icon of each protocol offered, windows sharing first
// since that is what a network drive means coming from Windows
struct ShareType {
    const char *label;
    const char *scheme;
    const char *icon;
};

const ShareType kShareTypes[] = {
    {QT_TRANSLATE_NOOP("MapDriveDialog", "Windows share (SMB)"), "smb",  "network-server"},
    {QT_TRANSLATE_NOOP("MapDriveDialog", "SSH / SFTP"),          "sftp", "folder-remote"},
    {QT_TRANSLATE_NOOP("MapDriveDialog", "FTP"),                 "ftp",  "folder-remote"},
    {QT_TRANSLATE_NOOP("MapDriveDialog", "WebDAV"),              "webdav", "folder-remote"},
    {QT_TRANSLATE_NOOP("MapDriveDialog", "NFS"),                 "nfs",  "folder-remote"},
};

} // namespace

QUrl MapDriveDialog::urlFor(const QString &input, const QString &scheme,
                            const QString &user)
{
    QString text = input.trimmed();
    if (text.isEmpty())
        return {};

    QString actualScheme = scheme;

    // Backslashes are legal in a filename here, so only the leading pair that
    // makes it a Windows share path is converted
    if (text.startsWith(QLatin1String("\\\\"))) {
        text = text.mid(2);
        text.replace(QLatin1Char('\\'), QLatin1Char('/'));
        actualScheme = QStringLiteral("smb");
    } else if (text.startsWith(QLatin1String("//"))) {
        text = text.mid(2);
        actualScheme = QStringLiteral("smb");
    } else {
        // An explicit scheme wins over the drop down
        const int separator = text.indexOf(QLatin1String("://"));
        if (separator > 0) {
            actualScheme = text.left(separator);
            text = text.mid(separator + 3);
        }
    }

    // Or the places model files the same share twice under two spellings
    const QStringList segments = text.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.isEmpty())
        return {};

    QUrl url;
    url.setScheme(actualScheme);
    url.setHost(segments.first());
    if (segments.size() > 1)
        url.setPath(QLatin1Char('/') + segments.mid(1).join(QLatin1Char('/')));
    if (!user.trimmed().isEmpty())
        url.setUserName(user.trimmed());

    // Never a password in the location, which would land in the bookmark file
    // in plain text, and KIO asks when the share demands one
    return url.isValid() && !url.host().isEmpty() ? url : QUrl();
}

MapDriveDialog::MapDriveDialog(KFilePlacesModel *places, QWidget *parent)
    : Aero::TaskDialog(parent)
    , m_places(places)
{
    setWindowTitle(tr("Map Network Drive"));

    contentLayout()->addWidget(Aero::label(
        tr("What network folder would you like to map?"), 9));
    QLabel *hint = Aero::label(
        tr("Specify the folder address, in either Windows (\\\\server\\share) "
           "or address (smb://server/share) form."), 9, Aero::Palette::MutedText);
    hint->setWordWrap(true);
    contentLayout()->addWidget(hint);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 6, 0, 0);
    form->setSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_scheme = new QComboBox;
    for (const ShareType &type : kShareTypes) {
        // Protocols with no worker are left out rather than failing later
        if (!KProtocolInfo::isKnownProtocol(QLatin1String(type.scheme)))
            continue;
        m_scheme->addItem(tr(type.label), QLatin1String(type.scheme));
    }
    if (m_scheme->count() == 0)
        m_scheme->addItem(tr("Windows share (SMB)"), QStringLiteral("smb"));

    m_folder = new QLineEdit;
    m_folder->setPlaceholderText(QStringLiteral("\\\\server\\share"));
    m_label = new QLineEdit;
    m_label->setPlaceholderText(tr("Optional, shown in the navigation pane"));
    m_user = new QLineEdit;
    m_user->setPlaceholderText(tr("Optional"));

    for (QWidget *w : {static_cast<QWidget *>(m_scheme),
                       static_cast<QWidget *>(m_folder),
                       static_cast<QWidget *>(m_label),
                       static_cast<QWidget *>(m_user)}) {
        Aero::setPointSize(w, 9);
    }

    const auto row = [this, form](const QString &text, QWidget *field) {
        QLabel *caption = Aero::label(text, 9);
        form->addRow(caption, field);
    };
    row(tr("Folder:"), m_folder);
    row(tr("Type:"), m_scheme);
    row(tr("Drive name:"), m_label);
    row(tr("User name:"), m_user);
    contentLayout()->addLayout(form);

    m_reconnect = new QCheckBox(tr("Reconnect at sign-in"));
    Aero::setPointSize(m_reconnect, 9);
    m_reconnect->setChecked(true);
    m_reconnect->setToolTip(
        tr("Adds the folder to the navigation pane under Network, where it "
           "stays until it is removed. Shared with the rest of the desktop's "
           "bookmarks."));
    contentLayout()->addWidget(m_reconnect);

    // What the typed text resolves to, the share path conversion being the part
    // most likely to surprise
    m_preview = Aero::label(QString(), 9, Aero::Palette::MutedText);
    m_preview->setWordWrap(true);
    contentLayout()->addWidget(m_preview);
    contentLayout()->addStretch(1);

    m_ok = addButton(tr("Finish"));
    m_ok->setDefault(true);
    auto *cancel = addButton(tr("Cancel"));

    connect(m_folder, &QLineEdit::textChanged, this, &MapDriveDialog::updateOkState);
    connect(m_user, &QLineEdit::textChanged, this, &MapDriveDialog::updateOkState);
    connect(m_scheme, &QComboBox::currentIndexChanged, this,
            &MapDriveDialog::updateOkState);
    connect(m_ok, &QPushButton::clicked, this, &MapDriveDialog::accepted);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    updateOkState();
    resize(460, sizeHint().height());
}

void MapDriveDialog::updateOkState()
{
    const QUrl url = urlFor(m_folder->text(), m_scheme->currentData().toString(),
                            m_user->text());
    m_ok->setEnabled(url.isValid());
    m_preview->setText(url.isValid()
                           ? tr("Will open: %1").arg(url.toDisplayString())
                           : QString());
}

void MapDriveDialog::accepted()
{
    const QUrl url = urlFor(m_folder->text(), m_scheme->currentData().toString(),
                            m_user->text());
    if (!url.isValid())
        return;

    m_mapped = url;

    if (m_reconnect->isChecked() && m_places) {
        const QString name = m_label->text().trimmed().isEmpty()
            ? url.host() + url.path()
            : m_label->text().trimmed();
        // The desktop's shared bookmarks, so the share also turns up in the
        // file dialogs and elsewhere, filed under network
        m_places->addPlace(name, url, QStringLiteral("folder-network"));
    }

    accept();
}
