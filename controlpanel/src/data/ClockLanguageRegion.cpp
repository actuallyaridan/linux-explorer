#include "ClockLanguageRegion.h"

const QList<DetailGroup> &clockLanguageRegionGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "preferences-system-time", "Date and Time",
            {
                { "Set the time and date",
                  "Change the time zone",
                  "Add clocks for different time zones" },
                { "Add the Clock gadget to the desktop" },
            }
        },
        {
            "preferences-desktop-locale", "Region and Language",
            {
                { "Install or uninstall display languages",
                  "Change display language",
                  "Change location" },
                { "Change the date, time, or number format",
                  "Change keyboards or other input methods" },
            }
        },
    };
    return groups;
}
