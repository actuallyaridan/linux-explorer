#pragma once

// Windows 7 chrome colours, as strings because most are read by stylesheets

#include <QColor>
#include <QLatin1StringView>

namespace Aero::Palette {

// Surfaces
inline constexpr auto Surface = "#FFFFFF";
inline constexpr auto FooterSurface = "#F0F0F0";
inline constexpr auto StripSurface = "#F1F5FB";
inline constexpr auto NoticeSurface = "#FFFFE1";

// Rules
inline constexpr auto Hairline = "#DDDDDD";
inline constexpr auto FooterRule = "#DFDFDF";
inline constexpr auto PaneRule = "#D9D9D9";
inline constexpr auto StripRule = "#D6DFEC";
inline constexpr auto NoticeRule = "#E3C86B";

// Text
inline constexpr auto Text = "#000000";
inline constexpr auto SoftText = "#1F1F1F";
inline constexpr auto MutedText = "#5A5A5A";
inline constexpr auto MutedHover = "#6E6E6E";
inline constexpr auto LinkText = "#1F4E99";
inline constexpr auto LinkHover = "#0033AA";
inline constexpr auto CrumbHover = "#003399";
inline constexpr auto HeadingText = "#1A3C7A";

// Painted glyphs
inline constexpr auto ArrowFill = "#3C3C3C";
inline constexpr auto CrumbArrow = "#666666";

// The glass button
inline constexpr auto GlassBorder = "#C0CEDA";
inline constexpr auto GlassTop = "#FDFEFF";
inline constexpr auto GlassBottom = "#E8F1F8";
inline constexpr auto GlassHover = "#E4EEF7";
inline constexpr auto GlassPressed = "#D6E4F0";
inline constexpr auto FocusBorder = "#7EB4EA";

// The file operation banner, left to right
inline constexpr auto BannerStart = "#DCE5F4";
inline constexpr auto BannerMid = "#A0C3E4";
inline constexpr auto BannerFar = "#3E668D";
inline constexpr auto BannerEnd = "#093D64";

inline QColor rgb(const char *hex)
{
    return QColor(QLatin1StringView(hex));
}

} // namespace Aero Palette
