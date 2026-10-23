#include "AppearanceAndPersonalization.h"

const QList<DetailGroup> &appearanceAndPersonalizationGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "preferences-desktop-theme", "Personalization",
            {
                { "Change the theme",
                  "Change desktop background",
                  "Change window glass colors" },
                { "Change sound effects",
                  "Change screen saver" },
            }
        },
        {
            "preferences-desktop-display", "Display",
            {
                { "Make text and other items larger or smaller",
                  "Adjust screen resolution" },
                { "Connect to an external display" },
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
        {
            "preferences-desktop-plasma-theme", "Taskbar and Start Menu",
            {
                { "Customize the Start menu",
                  "Customize icons on the taskbar" },
                { "Change the picture on the Start menu" },
            }
        },
        {
            "preferences-desktop-accessibility", "Ease of Access Center",
            {
                { "Accommodate low vision",
                  "Use screen reader",
                  "Turn on easy access keys" },
                { "Turn High Contrast on or off" },
            }
        },
        {
            "folder-templates", "Folder Options",
            {
                { "Specify single- or double-click to open",
                  "Show hidden files and folders" },
            }
        },
        {
            "preferences-desktop-font-installer", "Fonts",
            {
                { "Preview, delete, or show and hide fonts",
                  "Change Font Settings",
                  "Adjust ClearType text" },
            }
        },
    };
    return groups;
}
