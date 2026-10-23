#include "NetworkAndInternet.h"

const QList<DetailGroup> &networkAndInternetGroups()
{
    static const QList<DetailGroup> groups = {
        {
            "preferences-system-network", "Network and Sharing Center",
            {
                { "View network status and tasks",
                  "Connect to a network" },
                { "View network computers and devices",
                  "Add a wireless device to the network" },
            }
        },
        {
            "preferences-system-network-share-windows", "HomeGroup",
            {
                { "Choose homegroup and sharing options" },
            }
        },
        {
            "applications-internet", "Internet Options",
            {
                { "Change your homepage",
                  "Manage browser add-ons",
                  "Delete browsing history and cookies" },
            }
        },
    };
    return groups;
}
