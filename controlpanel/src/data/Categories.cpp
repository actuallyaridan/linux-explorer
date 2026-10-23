#include "Categories.h"

#include "SystemAndSecurity.h"
#include "NetworkAndInternet.h"
#include "HardwareAndSound.h"
#include "Programs.h"
#include "UserAccounts.h"
#include "AppearanceAndPersonalization.h"
#include "ClockLanguageRegion.h"
#include "EaseOfAccess.h"

const QList<CategoryItem> &homeCategories()
{
    static const QList<CategoryItem> items = {
        {
            "dialog-password",
            "System and Security",
            { "Review your computer's status", "Back up your computer", "Find and fix problems" }
        },
        {
            "system-users",
            "User Accounts and Family Safety",
            { "Add or remove user accounts", "Set up parental controls for any user" }
        },
        {
            "folder-network",
            "Network and Internet",
            { "View network status and tasks", "Choose homegroup and sharing options" }
        },
        {
            "preferences-desktop-theme-global",
            "Appearance and Personalization",
            { "Change the theme", "Change desktop background", "Adjust screen resolution" }
        },
        {
            "input_devices_settings",
            "Hardware and Sound",
            { "View devices and printers", "Add a device" }
        },
        {
            "preferences-desktop-locale",
            "Clock, Language, and Region",
            { "Change keyboards or other input methods", "Change display language" }
        },
        {
            "application-vnd.debian.binary-package",
            "Programs",
            { "Uninstall a program" }
        },
        {
            "preferences-desktop-accessibility",
            "Ease of Access",
            { "Let Linux suggest settings", "Optimize visual display" }
        },
    };
    return items;
}

const QStringList &navOrder()
{
    static const QStringList order = {
        "System and Security",
        "Network and Internet",
        "Hardware and Sound",
        "Programs",
        "User Accounts and Family Safety",
        "Appearance and Personalization",
        "Clock, Language, and Region",
        "Ease of Access",
    };
    return order;
}

const QList<DetailGroup> *detailGroupsFor(const QString &category)
{
    if (category == "System and Security")
        return &systemAndSecurityGroups();
    if (category == "Network and Internet")
        return &networkAndInternetGroups();
    if (category == "Hardware and Sound")
        return &hardwareAndSoundGroups();
    if (category == "Programs")
        return &programsGroups();
    if (category == "User Accounts and Family Safety")
        return &userAccountsGroups();
    if (category == "Appearance and Personalization")
        return &appearanceAndPersonalizationGroups();
    if (category == "Clock, Language, and Region")
        return &clockLanguageRegionGroups();
    if (category == "Ease of Access")
        return &easeOfAccessGroups();
    return nullptr;
}
