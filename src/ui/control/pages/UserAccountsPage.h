#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include "ui/control/PageId.h"

class QScrollArea;

namespace ControlPanel {

// The "User Accounts" detail page (User Accounts and Family Safety), a
// Control-Panel rendering of the Windows 7 User Accounts main screen
//
// The account is read from the local passwd database (`getpwuid`) and the
// caller's group membership (`getgrouplist`): membership of wheel/sudo maps to
// the "Administrator" account type. The picture is the user's real avatar
// (~/.face or the AccountsService icon). Every editing action hands off to KDE's
// user-manager module (`kcm_users`), which already prompts for the credentials
// needed to change a password, name, picture or account type
class UserAccountsPage : public QWidget {
    Q_OBJECT

public:
    explicit UserAccountsPage(QScrollArea *sidebar, QWidget *parent = nullptr);

    static QList<SidebarLink> sidebarLinks();
    static QList<SidebarLink> sidebarSeeAlso();

private:
    struct Account {
        // Login name
        QString userName;
        // GECOS display name (falls back to login name)
        QString fullName;
        // "Administrator" or "Standard user"
        QString accountType;
        // Avatar image path, empty if none found
        QString picturePath;
    };
    static Account gatherAccount();
};

} // namespace ControlPanel
