#include "artwork.h"

#include <QPainter>
#include <QPixmapCache>

namespace Aero {

QPixmap art(const QString &resource)
{
    QPixmap pm;
    if (!QPixmapCache::find(resource, &pm)) {
        pm.load(resource);
        QPixmapCache::insert(resource, pm);
    }
    return pm;
}

void drawStretchedBetweenCaps(QPainter *p, const QRect &rect,
                              const QPixmap &pm, int cap)
{
    if (rect.isEmpty() || pm.isNull())
        return;

    const QRect source(0, 0, pm.width(), pm.height());

    // Too narrow for both caps and anything between, so scale it whole
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

QString tiledBackgroundSheet(const QString &objectName, const QString &resource,
                             const QString &extra)
{
    return QStringLiteral("#%1 { background-image: url(%2);"
                          " background-repeat: repeat-x;"
                          " background-position: top left;%3 }")
        .arg(objectName, resource,
             extra.isEmpty() ? QString() : QStringLiteral(" ") + extra);
}

QString panelSheet(const QString &objectName, const char *background,
                   Qt::Edge rule, const char *ruleColor)
{
    QString sheet = QStringLiteral("#%1 { background: %2;")
                        .arg(objectName, QLatin1String(background));
    if (rule && ruleColor) {
        const char *edge = rule == Qt::TopEdge      ? "top"
                         : rule == Qt::LeftEdge     ? "left"
                         : rule == Qt::RightEdge    ? "right"
                                                    : "bottom";
        sheet += QStringLiteral(" border-%1: 1px solid %2;")
                     .arg(QLatin1String(edge), QLatin1String(ruleColor));
    }
    return sheet + QStringLiteral(" }");
}

} // namespace Aero
