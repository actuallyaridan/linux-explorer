#pragma once

// Windows 7's drive capacity bar, as a painter rather than a widget: it is
// drawn from the Computer page's tile and content layouts, both item delegates
// with nothing but a QPainter and a rectangle to work with. Header-only and
// moc-free, so nothing here needs listing for AUTOMOC.
//
// Win7's own artwork rather than gradients approximated in code, which was
// settled the hard way. KCapacityBar gets the structure right, splitting its
// fill abruptly at the midpoint as Win7 does, but derives everything from one
// palette colour, so a coloured fill tints the empty trough and its highlight
// washes further toward white; least-squares fitting its input bottomed out at
// RMS ~30, all of it in a top half too pale to read as vibrant. Hand-mixed
// gradients did better but missed the sheen running lengthwise along the fill,
// which is not a gradient in either axis and is what makes the bar look lit.
//
// So: driveprogressbackground.png is the trough, drivefillblue.png the fill,
// drivefillred.png the fill Win7 switches to on a nearly-full drive. Note the
// blue is cyan-teal, considerably greener than it is usually remembered, which
// is why every hand-picked "blue" looked subtly wrong.

#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QRect>
#include <QString>

namespace Win7 {

// The artwork's own dimensions: a 189x13 trough whose 1px border encloses an
// 11px fill. Callers may draw it narrower, but not usefully taller.
constexpr int kCapacityBarWidth = 189;
constexpr int kCapacityBarHeight = 13;

// Win7 turns the bar red once a drive is nearly full.
constexpr int kLowSpacePercent = 90;

namespace detail {

// Held back from the horizontal stretch, in source pixels. The trough needs 3
// (rounded corner, inner shading, one spare); everything inward of those is
// identical column-for-column. The fill needs only its darker end column, and
// its middle must stretch rather than tile, the sheen spanning the whole fill
// however long that is.
constexpr int kTroughCap = 3;
constexpr int kFillCap = 1;

// QPixmapCache rather than a function-local static QPixmap, which would outlive
// QGuiApplication and be destroyed after the platform plugin is gone.
inline QPixmap art(const QString &resource)
{
    QPixmap pm;
    if (!QPixmapCache::find(resource, &pm)) {
        pm.load(resource);
        QPixmapCache::insert(resource, pm);
    }
    return pm;
}

// Draws `pm` across `rect`, holding `cap` columns at each end at their source
// width and stretching only what lies between. Scaling the image whole would
// soften those columns, which are the bar's edges and have to stay crisp.
inline void drawStretchedBetweenCaps(QPainter *p, const QRect &rect,
                                     const QPixmap &pm, int cap)
{
    if (rect.isEmpty() || pm.isNull())
        return;

    const QRect source(0, 0, pm.width(), pm.height());

    // Too narrow for both caps and anything between; scale it whole.
    if (rect.width() <= 2 * cap || source.width() <= 2 * cap) {
        p->drawPixmap(rect, pm, source);
        return;
    }

    p->drawPixmap(QRect(rect.left(), rect.top(), cap, rect.height()),
                  pm, QRect(0, 0, cap, source.height()));
    p->drawPixmap(QRect(rect.left() + cap, rect.top(),
                        rect.width() - 2 * cap, rect.height()),
                  pm, QRect(cap, 0, source.width() - 2 * cap, source.height()));
    p->drawPixmap(QRect(rect.right() - cap + 1, rect.top(), cap, rect.height()),
                  pm, QRect(source.width() - cap, 0, cap, source.height()));
}

} // namespace detail

// Draws the bar filling `rect`, `percentUsed` of it consumed. The caller sizes
// the rectangle; kCapacityBarWidth/Height are the original's own figures.
inline void paintCapacityBar(QPainter *p, const QRect &rect, int percentUsed)
{
    if (rect.isEmpty())
        return;

    const int percent = qBound(0, percentUsed, 100);

    p->save();
    // Both images are drawn at their own height, so the only interpolation is
    // along the stretched middle slice; smoothing stops the sheen banding.
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    p->setRenderHint(QPainter::Antialiasing, false);

    detail::drawStretchedBetweenCaps(
        p, rect, detail::art(QStringLiteral(":/win7/driveprogressbackground.png")),
        detail::kTroughCap);

    // The fill sits inside the trough's 1px border, on all four sides.
    const QRect inner = rect.adjusted(1, 1, -1, -1);
    const int filled = qRound(inner.width() * percent / 100.0);
    if (inner.isValid() && filled > 0) {
        const QString fill = percent >= kLowSpacePercent
            ? QStringLiteral(":/win7/drivefillred.png")
            : QStringLiteral(":/win7/drivefillblue.png");
        detail::drawStretchedBetweenCaps(
            p, QRect(inner.left(), inner.top(), filled, inner.height()),
            detail::art(fill), detail::kFillCap);
    }

    p->restore();
}

} // namespace Win7
