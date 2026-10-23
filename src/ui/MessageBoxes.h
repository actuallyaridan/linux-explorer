#pragma once

#include <QString>

class QWidget;

// Win7's message boxes, the task dialog with its icon, sound and OK button,
// for anything that has to tell the user something rather than ask
//
// Both lines are plain text, so a program's error output can be shown as it
// came without escaping it first
namespace MessageBoxes {

// The red error icon and the critical stop sound, with the reason or detail
// under the main line
void showError(QWidget *parent, const QString &title, const QString &primary,
               const QString &secondary = {});

// The same for news that is not bad, with the information icon and no alarm
void showInformation(QWidget *parent, const QString &title,
                     const QString &primary, const QString &secondary = {});

} // namespace MessageBoxes
