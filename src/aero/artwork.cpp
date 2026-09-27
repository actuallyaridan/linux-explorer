#include "artwork.h"

#include <QPainter>
#include <QPixmapCache>

namespace Aero {

namespace {

const char *edgeName(Qt::Edge edge)
{
    switch (edge) {
    case Qt::TopEdge:
        return "top";
    case Qt::LeftEdge:
        return "left";
    case Qt::RightEdge:
        return "right";
    case Qt::BottomEdge:
    default:
        return "bottom";
    }
}

} // namespace

QPixmap art(const QString &resource)
{
    QPixmap pixmap;
    if (!QPixmapCache::find(resource, &pixmap)) {
        pixmap.load(resource);
        QPixmapCache::insert(resource, pixmap);
    }
    return pixmap;
}

void drawStretchedBetweenCaps(QPainter *painter, const QRect &rect,
                              const QPixmap &pixmap, int cap)
{
    if (rect.isEmpty() || pixmap.isNull())
        return;

    const QRect source(0, 0, pixmap.width(), pixmap.height());

    // Too narrow for both caps and anything between, so scale it whole
    if (rect.width() <= 2 * cap || source.width() <= 2 * cap) {
        painter->drawPixmap(rect, pixmap, source);
        return;
    }

    const QRect leftCap(rect.left(), rect.top(), cap, rect.height());
    const QRect middle(rect.left() + cap, rect.top(), rect.width() - 2 * cap, rect.height());
    const QRect rightCap(rect.right() - cap + 1, rect.top(), cap, rect.height());

    const QRect sourceLeftCap(0, 0, cap, source.height());
    const QRect sourceMiddle(cap, 0, source.width() - 2 * cap, source.height());
    const QRect sourceRightCap(source.width() - cap, 0, cap, source.height());

    painter->drawPixmap(leftCap, pixmap, sourceLeftCap);
    painter->drawPixmap(middle, pixmap, sourceMiddle);
    painter->drawPixmap(rightCap, pixmap, sourceRightCap);
}

QString tiledBackgroundSheet(const QString &objectName, const QString &resource,
                             const QString &extra)
{
    const QString spacedExtra = extra.isEmpty() ? QString() : QStringLiteral(" ") + extra;
    return QStringLiteral("#%1 { background-image: url(%2);"
                          " background-repeat: repeat-x;"
                          " background-position: top left;%3 }")
        .arg(objectName, resource, spacedExtra);
}

QString panelSheet(const QString &objectName, const char *background,
                   Qt::Edge rule, const char *ruleColor)
{
    QString sheet = QStringLiteral("#%1 { background: %2;")
                        .arg(objectName, QLatin1String(background));
    if (rule && ruleColor) {
        sheet += QStringLiteral(" border-%1: 1px solid %2;")
                     .arg(QLatin1String(edgeName(rule)), QLatin1String(ruleColor));
    }
    return sheet + QStringLiteral(" }");
}

} // namespace Aero
