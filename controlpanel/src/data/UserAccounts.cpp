#include "UserAccounts.h"

const QList<DetailGroup> &userAccountsGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "system-users", "User Accounts",
            {
                { "Change your account picture",
                  "Add or remove user accounts" },
                { "Change your Linux Password" },
            }
        },
        {
            "preferences-system-login", "Credential Manager",
            {
                { "Manage Linux Credentials" },
            }
        },
    };
    return groups;
}
