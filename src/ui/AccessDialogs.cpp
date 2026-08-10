#include "AccessDialogs.h"
#include "IconHelper.h"
#include "Win7Ui.h"

#include <KIO/Global>

#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QSoundEffect>
#include <QVBoxLayout>

namespace {

QString translate(const char *text)
{
    return QCoreApplication::translate("AccessDialogs", text);
}

// The Win7 sound theme, the same one the navigation click comes from. A missing
// file is silently no sound.
//
// Cached for the life of the application: a QSoundEffect has to outlive the
// play() call, and these dialogs run their own event loop and then go away.
void playSound(const QString &name)
{
    static QHash<QString, QSoundEffect *> effects;
    QSoundEffect *&effect = effects[name];
    if (!effect) {
        effect = new QSoundEffect(qApp);
        effect->setSource(QUrl::fromLocalFile(
            QStringLiteral("/usr/share/sounds/Windows 7/og/%1.wav").arg(name)));
        effect->setVolume(1.0f);
    }
    effect->play();
}

// Win7's task dialog, the shape both of these take: a white body with the icon
// and message, a grey command strip along the bottom for the buttons. The same
// two-part layout as MountDialog and MapDriveDialog.
class TaskDialog : public QDialog {
public:
    TaskDialog(QWidget *parent, const QString &title, const QIcon &icon)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setModal(true);
        setSizeGripEnabled(false);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *content = new QWidget;
        content->setObjectName(QStringLiteral("taskContent"));
        // ID-scoped: a declaration-only sheet acts as `* { ... }` and would drag
        // the labels into the stylesheet engine.
        content->setStyleSheet("#taskContent { background: #FFFFFF; }");

        auto *body = new QHBoxLayout(content);
        body->setContentsMargins(18, 18, 18, 18);
        body->setSpacing(14);

        auto *iconLabel = new QLabel;
        iconLabel->setStyleSheet("background: transparent;");
        iconLabel->setPixmap(icon.pixmap(32, 32));
        body->addWidget(iconLabel, 0, Qt::AlignTop);

        m_lines = new QVBoxLayout;
        m_lines->setSpacing(10);
        body->addLayout(m_lines, 1);

        root->addWidget(content, 1);

        auto *footer = new QWidget;
        footer->setObjectName(QStringLiteral("taskFooter"));
        footer->setStyleSheet(
            "#taskFooter { background: #F0F0F0; border-top: 1px solid #DFDFDF; }");

        m_buttons = new QHBoxLayout(footer);
        m_buttons->setContentsMargins(12, 10, 12, 10);
        m_buttons->setSpacing(8);
        m_buttons->addStretch(1);

        root->addWidget(footer);
    }

    void addLine(QLabel *line)
    {
        line->setWordWrap(true);
        m_lines->addWidget(line);
    }

    QPushButton *addButton(const QString &text)
    {
        auto *button = new QPushButton(text);
        Win7::setPointSize(button, 9);
        // Win7's own metric: nothing narrower than 75px.
        button->setMinimumWidth(75);
        m_buttons->addWidget(button);
        return button;
    }

    // Windows' copies do not resize, and nothing here would reflow usefully. The
    // width is fixed first so the wrapped text can be measured against it, and
    // the height follows from however many lines that came to.
    void lockSize(int width)
    {
        setFixedWidth(width);
        layout()->activate();
        setFixedHeight(sizeHint().height());
    }

private:
    QVBoxLayout *m_lines = nullptr;
    QHBoxLayout *m_buttons = nullptr;
};

} // namespace

namespace AccessDialogs {

bool isPermissionError(int error)
{
    switch (error) {
    case KIO::ERR_ACCESS_DENIED:
    case KIO::ERR_WRITE_ACCESS_DENIED:
    case KIO::ERR_CANNOT_ENTER_DIRECTORY:
    case KIO::ERR_CANNOT_OPEN_FOR_READING:
    case KIO::ERR_CANNOT_OPEN_FOR_WRITING:
        return true;
    default:
        return false;
    }
}

void showFailure(QWidget *parent, const QString &title, const QString &primary,
                 const QString &secondary)
{
    TaskDialog dialog(parent, title,
                      themeIcon({"dialog-error", "messagebox_critical",
                                 "emblem-error"}));
    dialog.addLine(Win7::bodyLabel(primary));
    dialog.addLine(Win7::bodyLabel(secondary.isEmpty()
                                       ? translate("Access is denied.")
                                       : secondary));

    QPushButton *ok = dialog.addButton(translate("OK"));
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.lockSize(360);
    playSound(QStringLiteral("Windows Critical Stop"));
    dialog.exec();
}

void showLocationUnavailable(QWidget *parent, const QString &path,
                             const QString &reason)
{
    showFailure(parent, translate("Location is not available"),
                translate("%1 is not accessible.").arg(path), reason);
}

bool askForAdminAccess(QWidget *parent, const QString &folderName)
{
    // The folder's own name is the title, which is what Windows puts there.
    TaskDialog dialog(parent, folderName,
                      themeIcon({"dialog-warning", "messagebox_warning"}));

    // Win7's main instruction: bigger, in the blue its task dialogs use for the
    // one sentence stating the problem.
    QLabel *primary = Win7::label(
        translate("You don't currently have permission to access this folder."),
        12, "#003399");
    dialog.addLine(primary);
    // Not Windows' "permanently get access to this folder": Continue rewrites no
    // permissions here, it opens the folder through the admin worker.
    dialog.addLine(Win7::bodyLabel(
        translate("Click Continue to open this folder as an administrator.")));

    QPushButton *proceed = dialog.addButton(translate("Continue"));
    proceed->setDefault(true);
    QPushButton *cancel = dialog.addButton(translate("Cancel"));

    QObject::connect(proceed, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.lockSize(400);
    playSound(QStringLiteral("Windows Exclamation"));
    return dialog.exec() == QDialog::Accepted;
}

void showAdministratorWarning(QWidget *parent)
{
    TaskDialog dialog(parent, translate("Administrator"),
                      themeIcon({"dialog-warning", "messagebox_warning"}));
    dialog.addLine(Win7::bodyLabel(translate(
        "You are using File Explorer as an administrator. You can perform "
        "actions that are normally restricted, which means that you have the "
        "ability to render your computer unusable if you don't know what "
        "you're doing. Be careful!")));

    QPushButton *ok = dialog.addButton(translate("OK"));
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.lockSize(420);
    playSound(QStringLiteral("Windows Exclamation"));
    dialog.exec();
}

} // namespace AccessDialogs
