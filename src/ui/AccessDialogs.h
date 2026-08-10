#pragma once

#include <QString>

class QWidget;

// The two dialogs Windows puts up when it will not let you in.
//
//  - "Location is not available", the red-cross box carrying a bare "Access is
//    denied.", Windows' answer once there is nothing left to try.
//  - the elevation prompt, "You don't currently have permission to access this
//    folder", whose Continue asks for the rights the folder wants.
//
// Only the wording and the shape are Windows'. Continue is not Windows' ACL
// rewrite: the caller decides what it means, and MainWindow answers it with
// "Open as Administrator". Nothing here performs or retries an operation; these
// ask a question and report the answer.
namespace AccessDialogs {

// Whether a KIO error code means "you are not allowed". Kept here so the
// listing, the file operations and anything added later agree on which failures
// get Windows' permission dialogs and which keep KIO's wording.
bool isPermissionError(int error);

// The error box. `primary` names what failed, `secondary` is the reason
// underneath it and defaults to "Access is denied.".
void showFailure(QWidget *parent, const QString &title, const QString &primary,
                 const QString &secondary = {});

// The same box for a location that cannot be reached at all, which Windows
// titles "Location is not available".
void showLocationUnavailable(QWidget *parent, const QString &path,
                             const QString &reason = {});

// The elevation prompt. True for Continue, false for Cancel or Escape.
// `folderName` is the window title, as in Windows.
bool askForAdminAccess(QWidget *parent, const QString &folderName);

// What the administrator strip says when clicked: what an elevated window can
// do, and what that means if done carelessly. Informational, so OK only.
void showAdministratorWarning(QWidget *parent);

} // namespace AccessDialogs
