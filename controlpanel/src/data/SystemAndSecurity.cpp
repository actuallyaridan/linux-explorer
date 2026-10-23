#include "SystemAndSecurity.h"

const QList<DetailGroup> &systemAndSecurityGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "software-update-available", "Action Center",
            {
                { "Review your computer's status and resolve issues" },
                { "Change User Account Control settings",
                  "Troubleshoot common computer problems" },
                { "Restore your computer to an earlier time" },
            }
        },
        {
            "preferences-security-firewall", "Linux Firewall",
            {
                { "Check firewall status",
                  "Allow a program through Linux Firewall" },
            }
        },
        {
            "redhat-system_tools", "System",
            {
                { "View amount of RAM and processor speed",
                  "Check the Linux Experience Index" },
                { "Allow remote access",
                  "See the name of this computer",
                  "Device Manager" },
            }
        },
        {
            "system-software-update", "Linux Update",
            {
                { "Turn automatic updating on or off",
                  "Check for updates",
                  "View installed updates" },
            }
        },
        {
            "preferences-system-power-management", "Power Options",
            {
                { "Require a password when the computer wakes",
                  "Change what the power buttons do" },
                { "Change when the computer sleeps" },
            }
        },
        {
            "preferences-system-backup", "Backup and Restore",
            {
                { "Back up your computer",
                  "Restore files from backup" },
            }
        },
        {
            "drive-harddisk-encrypted", "BitLocker Drive Encryption",
            {
                { "Protect your computer by encrypting data on your disk",
                  "Manage BitLocker" },
            }
        },
        {
            "preferences-desktop-activities", "Administrative Tools",
            {
                { "Free up disk space",
                  "Defragment your hard drive" },
                { "Create and format hard disk partitions",
                  "View event logs",
                  "Schedule tasks" },
            }
        },
    };
    return groups;
}
