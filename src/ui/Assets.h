#pragma once

// Explorer's own artwork, by resource path, named here and nowhere else so the
// chrome stays free of this application's assets

#include "aero/capacitybar.h"

#include <QLatin1StringView>
#include <QString>

namespace Explorer::Art {

// The navigation pane's branch expanders
inline constexpr auto BranchClosed = ":/win7/branch-closed.png";
inline constexpr auto BranchOpen = ":/win7/branch-open.png";

// The command bar's strip, and the pill behind a button
inline constexpr auto CommandBar = ":/win7/commandbar.png";
inline constexpr auto CommandHover = ":/win7/commandhover.png";
inline constexpr auto CommandPress = ":/win7/commandpress.png";

inline constexpr auto DetailsBar = ":/win7/detailsbar.png";

inline Aero::CapacityBarArt capacityBar()
{
    return { QStringLiteral(":/win7/driveprogressbackground.png"),
             QStringLiteral(":/win7/drivefillblue.png"),
             QStringLiteral(":/win7/drivefillred.png") };
}

} // namespace Explorer Art
