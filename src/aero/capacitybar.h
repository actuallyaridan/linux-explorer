#pragma once

// Win7's drive capacity bar, as a painter rather than a widget, since item
// delegates have only a painter and a rectangle to work with

#include <QString>

class QPainter;
class QRect;

namespace Aero {

// The artwork's own dimensions, and callers may draw it narrower but not taller
inline constexpr int kCapacityBarWidth = 189;
inline constexpr int kCapacityBarHeight = 13;

// Win7 turns the bar red once a drive is nearly full
inline constexpr int kLowSpacePercent = 90;

struct CapacityBarArt {
    QString trough;
    QString fill;
    // Used instead of fill from kLowSpacePercent upward
    QString lowSpaceFill;
};

void paintCapacityBar(QPainter *p, const QRect &rect, int percentUsed,
                      const CapacityBarArt &art);

} // namespace Aero
