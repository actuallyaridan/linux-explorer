#include "capacitybar.h"

#include "artwork.h"

#include <QPainter>
#include <QRect>

namespace Aero {

namespace {

// Columns held back from the horizontal stretch, in source pixels
constexpr int kTroughCap = 3;
constexpr int kFillCap = 1;

} // namespace

void paintCapacityBar(QPainter *p, const QRect &rect, int percentUsed,
                      const CapacityBarArt &artwork)
{
    if (rect.isEmpty())
        return;

    const int percent = qBound(0, percentUsed, 100);

    p->save();
    // Smoothing stops the sheen banding along the stretched middle slice
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    p->setRenderHint(QPainter::Antialiasing, false);

    drawStretchedBetweenCaps(p, rect, art(artwork.trough), kTroughCap);

    // The fill sits inside the trough's border on all four sides
    const QRect inner = rect.adjusted(1, 1, -1, -1);
    const int filled = qRound(inner.width() * percent / 100.0);
    if (inner.isValid() && filled > 0) {
        const QString &fill = percent >= kLowSpacePercent ? artwork.lowSpaceFill
                                                          : artwork.fill;
        drawStretchedBetweenCaps(
            p, QRect(inner.left(), inner.top(), filled, inner.height()),
            art(fill), kFillCap);
    }

    p->restore();
}

} // namespace Aero
