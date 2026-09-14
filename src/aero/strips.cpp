#include "strips.h"

#include "artwork.h"
#include "text.h"

#include <QEasingCurve>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QTimer>

namespace Aero {

Strip::Strip(QWidget *parent)
    : QFrame(parent)
{
    // Named so its stylesheet can be ID scoped, a declaration only sheet
    // matching everything and dragging every child into the stylesheet engine
    setObjectName(QStringLiteral("aeroStrip"));
    m_row = new QHBoxLayout(this);
}

void Strip::setArt(const QString &resource, const char *backgroundColor)
{
    const QString extra = backgroundColor
        ? QStringLiteral("background-color: %1;").arg(QLatin1String(backgroundColor))
        : QString();
    setStyleSheet(tiledBackgroundSheet(objectName(), resource, extra));
}

Strip *commandBar(const QString &art, int height)
{
    auto *bar = new Strip;
    // The artwork's own height, since it is applied unscaled, and the bottom
    // rule is the image's last row rather than a border
    bar->setFixedHeight(height);
    bar->setArt(art);
    bar->row()->setContentsMargins(8, 0, 8, 0);
    bar->row()->setSpacing(6);
    return bar;
}


} // namespace Aero
