#include "AccessDialogs.h"

#include "aero/icons.h"
#include "aero/sounds.h"
#include "aero/taskdialog.h"
#include "aero/text.h"

#include <KIO/Global>

#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>

namespace {

QString translate(const char *text)
{
    return QCoreApplication::translate("AccessDialogs", text);
}

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
    Aero::TaskDialog dialog(parent, title,
                      Aero::themeIcon({"dialog-error", "messagebox_critical",
                                 "emblem-error"}));
    dialog.addLine(Aero::bodyLabel(primary));
    dialog.addLine(Aero::bodyLabel(secondary.isEmpty()
                                       ? translate("Access is denied.")
                                       : secondary));

    QPushButton *ok = dialog.addButton(translate("OK"));
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.lockSize(360);
    Aero::playSound(QStringLiteral("Windows Critical Stop"));
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
    // The folder's own name is the title, as in Windows
    Aero::TaskDialog dialog(parent, folderName,
                      Aero::themeIcon({"dialog-warning", "messagebox_warning"}));

    // Win7's main instruction, bigger and in its own blue
    QLabel *primary = Aero::label(
        translate("You don't currently have permission to access this folder."),
        12, Aero::Palette::CrumbHover);
    dialog.addLine(primary);
    // Not Windows' wording, since Continue rewrites no permissions here and
    // opens the folder through the admin worker instead
    dialog.addLine(Aero::bodyLabel(
        translate("Click Continue to open this folder as an administrator.")));

    QPushButton *proceed = dialog.addButton(translate("Continue"));
    proceed->setDefault(true);
    QPushButton *cancel = dialog.addButton(translate("Cancel"));

    QObject::connect(proceed, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.lockSize(400);
    Aero::playSound(QStringLiteral("Windows Exclamation"));
    return dialog.exec() == QDialog::Accepted;
}

void showAdministratorWarning(QWidget *parent)
{
    Aero::TaskDialog dialog(parent, translate("Administrator"),
                      Aero::themeIcon({"dialog-warning", "messagebox_warning"}));
    dialog.addLine(Aero::bodyLabel(translate(
        "You are using File Explorer as an administrator. You can perform "
        "actions that are normally restricted, which means that you have the "
        "ability to render your computer unusable if you don't know what "
        "you're doing. Be careful!")));

    QPushButton *ok = dialog.addButton(translate("OK"));
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.lockSize(420);
    Aero::playSound(QStringLiteral("Windows Exclamation"));
    dialog.exec();
}

} // namespace AccessDialogs
