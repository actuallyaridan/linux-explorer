#include "MessageBoxes.h"

#include "aero/icons.h"
#include "aero/sounds.h"
#include "aero/taskdialog.h"
#include "aero/text.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>

namespace MessageBoxes {

namespace {

// The width Explorer's own error dialogs are locked to
constexpr int kWidth = 360;

void show(QWidget *parent, const QString &title, const QIcon &icon,
          const QString &sound, const QString &primary,
          const QString &secondary)
{
    Aero::TaskDialog dialog(parent, title, icon);

    QLabel *primaryLine = Aero::bodyLabel(primary);
    primaryLine->setTextFormat(Qt::PlainText);
    dialog.addLine(primaryLine);

    if (!secondary.isEmpty()) {
        QLabel *secondaryLine = Aero::bodyLabel(secondary);
        secondaryLine->setTextFormat(Qt::PlainText);
        dialog.addLine(secondaryLine);
    }

    const QString ok = QCoreApplication::translate("MessageBoxes", "OK");
    QPushButton *okButton = dialog.addButton(ok);
    okButton->setDefault(true);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.lockSize(kWidth);
    Aero::playSound(sound);
    dialog.exec();
}

} // namespace

void showError(QWidget *parent, const QString &title, const QString &primary,
               const QString &secondary)
{
    const QIcon icon =
        Aero::themeIcon({"dialog-error", "messagebox_critical", "emblem-error"});
    show(parent, title, icon, QStringLiteral("Windows Critical Stop"), primary,
         secondary);
}

void showInformation(QWidget *parent, const QString &title,
                     const QString &primary, const QString &secondary)
{
    const QIcon icon =
        Aero::themeIcon({"dialog-information", "messagebox_info", "help-about"});
    // Win7's own is Windows Background, which the sound theme does not ship,
    // and its plain ding is the nearest it has
    show(parent, title, icon, QStringLiteral("Windows Ding"), primary, secondary);
}

} // namespace MessageBoxes
