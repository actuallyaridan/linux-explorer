#include "Programs.h"

const QList<DetailGroup> &programsGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "application-vnd.debian.binary-package", "Programs and Features",
            {
                { "Uninstall a program",
                  "Turn Linux features on or off",
                  "View installed updates" },
                { "Run programs made for previous versions of Linux",
                  "How to install a program" },
            }
        },
        {
            "preferences-desktop-default-applications", "Default Programs",
            {
                { "Change default settings for media or devices" },
                { "Make a file type always open in a specific program",
                  "Set your default programs" },
            }
        },
        {
            "gadgets", "Desktop Gadgets",
            {
                { "Add gadgets to the desktop",
                  "Get more gadgets online",
                  "Uninstall a gadget" },
                { "Restore desktop gadgets installed with Linux" },
            }
        },
    };
    return groups;
}
