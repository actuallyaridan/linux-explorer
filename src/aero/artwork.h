#pragma once

// The bitmaps Win7's chrome is made of, and the stylesheets that put them
// behind a widget

#include <QPixmap>
#include <Qt>
#include <QRect>
#include <QString>

class QPainter;

namespace Aero {

// QPixmapCache rather than a function local static, which would outlive
// QGuiApplication and be destroyed after the platform plugin is gone
QPixmap art(const QString &resource);

// Holds cap columns at each end at their source width and stretches only what
// lies between, so rounded corners and edge shading stay crisp
void drawStretchedBetweenCaps(QPainter *p, const QRect &rect,
                              const QPixmap &pm, int cap);

// The resource tiled sideways behind objectName, with extra appended to the
// same rule
QString tiledBackgroundSheet(const QString &objectName, const QString &resource,
                             const QString &extra = QString());

// Always ID scoped, since a declaration only sheet matches everything and
// would drag every child of the widget into the stylesheet engine
QString panelSheet(const QString &objectName, const char *background,
                   Qt::Edge rule = Qt::Edge(0), const char *ruleColor = nullptr);

} // namespace Aero
