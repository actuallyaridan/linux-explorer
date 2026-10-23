#include "HardwareAndSound.h"

const QList<DetailGroup> &hardwareAndSoundGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "preferences-desktop-peripherals", "Devices and Printers",
            {
                { "Add a device",
                  "Add a printer",
                  "Mouse",
                  "Device Manager" },
            }
        },
        {
            "exec", "AutoPlay",
            {
                { "Change default settings for media or devices",
                  "Play CDs or other media automatically" },
            }
        },
        {
            "preferences-desktop-sound", "Sound",
            {
                { "Adjust system volume",
                  "Change system sounds",
                  "Manage audio devices" },
            }
        },
        {
            "preferences-system-power-management", "Power Options",
            {
                { "Change power-saving settings",
                  "Change what the power buttons do" },
                { "Require a password when the computer wakes",
                  "Change when the computer sleeps" },
                { "Choose a power plan" },
            }
        },
        {
            "preferences-desktop-display", "Display",
            {
                { "Make text and other items larger or smaller",
                  "Adjust screen resolution" },
                { "Connect to an external display",
                  "How to correct monitor flicker (refresh rate)" },
            }
        },
    };
    return groups;
}
