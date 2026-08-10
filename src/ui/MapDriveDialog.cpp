#include "MapDriveDialog.h"
#include "Win7Ui.h"

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

// The protocols offered, as (label, scheme, icon). SMB is first and the
// default: it is what "network drive" means coming from Windows.
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

    // Backslashes are legal in a Unix filename, so the conversion is confined to
    // the leading double backslash that makes it a UNC path.
    if (text.startsWith(QLatin1String("\\\\"))) {
        text = text.mid(2);
        text.replace(QLatin1Char('\\'), QLatin1Char('/'));
        actualScheme = QStringLiteral("smb");
    } else if (text.startsWith(QLatin1String("//"))) {
        text = text.mid(2);
        actualScheme = QStringLiteral("smb");
    } else {
        // An explicit scheme wins over the box: someone who typed sftp://host
        // meant sftp, whatever the drop-down says.
        const int separator = text.indexOf(QLatin1String("://"));
        if (separator > 0) {
            actualScheme = text.left(separator);
            text = text.mid(separator + 3);
        }
    }

    // So smb://server//share/ and smb://server/share resolve to the same place;
    // the places model would treat them as two bookmarks.
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

    // Never a password in the URL: it would land in the places bookmark file in
    // plain text. KIO asks when the share demands one and hands it to the
    // desktop's wallet.
    return url.isValid() && !url.host().isEmpty() ? url : QUrl();
}

MapDriveDialog::MapDriveDialog(KFilePlacesModel *places, QWidget *parent)
    : QDialog(parent)
    , m_places(places)
{
    setWindowTitle(tr("Map Network Drive"));
    setModal(true);

    // Win7's two-part dialog: white content above, grey command strip below.
    // Identical to MountDialog's.
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("mapContent"));
    // ID-scoped: a declaration-only sheet acts as `* { ... }` and would drag the
    // line edits into the stylesheet engine.
    content->setStyleSheet("#mapContent { background: #FFFFFF; }");

    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(10);

    contentLayout->addWidget(Win7::label(
        tr("What network folder would you like to map?"), 9));
    QLabel *hint = Win7::label(
        tr("Specify the folder address, in either Windows (\\\\server\\share) "
           "or address (smb://server/share) form."), 9, "#5A5A5A");
    hint->setWordWrap(true);
    contentLayout->addWidget(hint);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 6, 0, 0);
    form->setSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_scheme = new QComboBox;
    for (const ShareType &type : kShareTypes) {
        // Protocols with no worker are left out rather than shown and then
        // failing with "unsupported protocol".
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
        Win7::setPointSize(w, 9);
    }

    const auto row = [this, form](const QString &text, QWidget *field) {
        QLabel *caption = Win7::label(text, 9);
        form->addRow(caption, field);
    };
    row(tr("Folder:"), m_folder);
    row(tr("Type:"), m_scheme);
    row(tr("Drive name:"), m_label);
    row(tr("User name:"), m_user);
    contentLayout->addLayout(form);

    m_reconnect = new QCheckBox(tr("Reconnect at sign-in"));
    Win7::setPointSize(m_reconnect, 9);
    m_reconnect->setChecked(true);
    m_reconnect->setToolTip(
        tr("Adds the folder to the navigation pane under Network, where it "
           "stays until it is removed. Shared with the rest of the desktop's "
           "bookmarks."));
    contentLayout->addWidget(m_reconnect);

    // What the typed text actually resolves to, the UNC conversion being the
    // part most likely to surprise.
    m_preview = Win7::label(QString(), 9, "#5A5A5A");
    m_preview->setWordWrap(true);
    contentLayout->addWidget(m_preview);
    contentLayout->addStretch(1);

    root->addWidget(content, 1);

    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("mapFooter"));
    footer->setStyleSheet(
        "#mapFooter { background: #F0F0F0; border-top: 1px solid #DFDFDF; }");

    auto *buttons = new QHBoxLayout(footer);
    buttons->setContentsMargins(12, 10, 12, 10);
    buttons->setSpacing(8);
    buttons->addStretch(1);

    m_ok = new QPushButton(tr("Finish"));
    m_ok->setDefault(true);
    auto *cancel = new QPushButton(tr("Cancel"));
    for (QPushButton *b : {m_ok, cancel})
        Win7::setPointSize(b, 9);
    buttons->addWidget(m_ok);
    buttons->addWidget(cancel);

    root->addWidget(footer);

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
        // The desktop's shared bookmarks rather than a list of our own, so the
        // share also turns up in the file dialogs and Dolphin. KFilePlacesModel
        // files anything remote under Network, where the pane then shows it.
        m_places->addPlace(name, url, QStringLiteral("folder-network"));
    }

    accept();
}
