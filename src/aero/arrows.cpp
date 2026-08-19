#include "arrows.h"

#include <QLabel>
#include <QPainter>
#include <QPolygonF>

namespace Aero {

QPixmap arrowPixmap(Qt::ArrowType dir, const QColor &color, int width)
{
    const int w = width;
    const int h = qMax(3, (w * 4 + 3) / 7);
    const bool vertical = (dir == Qt::UpArrow || dir == Qt::DownArrow);
    const QSize size = vertical ? QSize(w, h) : QSize(h, w);

    QPixmap pm(size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    QPolygonF poly;
    switch (dir) {
    case Qt::UpArrow:
        poly << QPointF(0, h) << QPointF(w, h) << QPointF(w / 2.0, 0);
        break;
    case Qt::LeftArrow:
        poly << QPointF(h, 0) << QPointF(h, w) << QPointF(0, w / 2.0);
        break;
    case Qt::RightArrow:
        poly << QPointF(0, 0) << QPointF(0, w) << QPointF(h, w / 2.0);
        break;
    case Qt::DownArrow:
    default:
        poly << QPointF(0, 0) << QPointF(w, 0) << QPointF(w / 2.0, h);
        break;
    }
    p.drawPolygon(poly);
    return pm;
}

QLabel *arrowLabel(Qt::ArrowType dir, const QColor &color, int width)
{
    auto *l = new QLabel;
    l->setStyleSheet(QStringLiteral("background: transparent;"));
    l->setPixmap(arrowPixmap(dir, color, width));
    return l;
}

} // namespace Aero
