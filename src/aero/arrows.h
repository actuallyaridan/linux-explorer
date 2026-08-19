#pragma once

// The Aero arrow, painted rather than shipped as artwork

#include "palette.h"

#include <QColor>
#include <QPixmap>
#include <Qt>

class QLabel;

namespace Aero {

// width is the triangle's base, rendered at 2x so the edges stay crisp
QPixmap arrowPixmap(Qt::ArrowType dir,
                    const QColor &color = Palette::rgb(Palette::ArrowFill),
                    int width = 7);

QLabel *arrowLabel(Qt::ArrowType dir,
                   const QColor &color = Palette::rgb(Palette::ArrowFill),
                   int width = 7);

} // namespace Aero
