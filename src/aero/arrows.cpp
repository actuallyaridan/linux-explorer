#include "arrows.h"

#include <QLabel>
#include <QPainter>
#include <QPolygonF>

namespace Aero {

QPixmap arrowPixmap(Qt::ArrowType dir, const QColor &color, int width)
{
    // The base runs across the arrow and the depth is how far its tip sticks out
    const int base = width;
    const int depth = qMax(3, (base * 4 + 3) / 7);
    const bool vertical = (dir == Qt::UpArrow || dir == Qt::DownArrow);
    const QSize size = vertical ? QSize(base, depth) : QSize(depth, base);

    QPixmap pixmap(size * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    QPolygonF triangle;
    switch (dir) {
    case Qt::UpArrow:
        triangle << QPointF(0, depth) << QPointF(base, depth) << QPointF(base / 2.0, 0);
        break;
    case Qt::LeftArrow:
        triangle << QPointF(depth, 0) << QPointF(depth, base) << QPointF(0, base / 2.0);
        break;
    case Qt::RightArrow:
        triangle << QPointF(0, 0) << QPointF(0, base) << QPointF(depth, base / 2.0);
        break;
    case Qt::DownArrow:
    default:
        triangle << QPointF(0, 0) << QPointF(base, 0) << QPointF(base / 2.0, depth);
        break;
    }
    painter.drawPolygon(triangle);
    return pixmap;
}

QLabel *arrowLabel(Qt::ArrowType dir, const QColor &color, int width)
{
    auto *label = new QLabel;
    label->setStyleSheet(QStringLiteral("background: transparent;"));
    label->setPixmap(arrowPixmap(dir, color, width));
    return label;
}

} // namespace Aero
