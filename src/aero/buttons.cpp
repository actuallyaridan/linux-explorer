#include "buttons.h"

#include "arrows.h"
#include "artwork.h"
#include "text.h"

#include <QPainter>
#include <QPixmap>

namespace Aero {

namespace {

QString &hoverArt()
{
    static QString path;
    return path;
}

QString &pressedArt()
{
    static QString path;
    return path;
}

} // namespace

MenuButton::MenuButton(const QString &text, QWidget *parent)
    : QToolButton(parent)
{
    setText(text);
    setPopupMode(QToolButton::InstantPopup);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; }"));
    setPointSize(this, 9);
}

void MenuButton::setPillArt(const QString &hover, const QString &pressed)
{
    hoverArt() = hover;
    pressedArt() = pressed;
}

void MenuButton::setColor(const QColor &normal)
{
    m_normal = normal;
    update();
}

void MenuButton::setShowArrow(bool show)
{
    m_showArrow = show;
    updateGeometry();
    update();
}

QSize MenuButton::sizeHint() const
{
    const QFontMetrics fm(font());
    const int arrow = m_showArrow ? kSpacing + kArrowWidth : 0;
    // Never shorter than the hover pill, or the painter clips it
    return QSize(kPadding + fm.horizontalAdvance(text()) + arrow + kPadding,
                 qMax(fm.height() + 4, kPillHeight));
}

void MenuButton::enterEvent(QEnterEvent *e)
{
    update();
    QToolButton::enterEvent(e);
}

void MenuButton::leaveEvent(QEvent *e)
{
    update();
    QToolButton::leaveEvent(e);
}

void MenuButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    // Painted rather than left to a stylesheet hover rule, this class never
    // chaining to the base paintEvent, and isDown stays true for as long as an
    // InstantPopup button's menu is open
    if (isDown() || underMouse()) {
        const QPixmap pill = art(isDown() ? pressedArt() : hoverArt());
        // At its own height and centred, since stretching softens the corners
        if (!pill.isNull()) {
            drawStretchedBetweenCaps(
                &p, QRect(0, (height() - pill.height()) / 2, width(), pill.height()),
                pill, kPillCap);
        }
    }

    p.setFont(font());
    p.setPen(m_normal);
    const int textWidth = QFontMetrics(font()).horizontalAdvance(text());
    p.drawText(QRect(kPadding, 0, textWidth, height()),
               Qt::AlignLeft | Qt::AlignVCenter, text());

    if (!m_showArrow)
        return;

    const QPixmap arrow = arrowPixmap(Qt::DownArrow, m_normal, kArrowWidth);
    const QSizeF as = arrow.deviceIndependentSize();
    p.drawPixmap(QPointF(kPadding + textWidth + kSpacing,
                         (height() - as.height()) / 2.0 + 1), arrow);
}

ChevronButton::ChevronButton(QWidget *parent)
    : QToolButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(20, 20);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(
        QStringLiteral("QToolButton { border: 1px solid %1; border-radius: 10px;"
                       " background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                       " stop:0 %2, stop:1 %3); }"
                       "QToolButton:hover { background: %4; }"
                       "QToolButton:pressed { background: %5; }")
            .arg(QLatin1String(Palette::GlassBorder),
                 QLatin1String(Palette::GlassTop),
                 QLatin1String(Palette::GlassBottom),
                 QLatin1String(Palette::GlassHover),
                 QLatin1String(Palette::GlassPressed)));
}

void ChevronButton::paintEvent(QPaintEvent *e)
{
    QToolButton::paintEvent(e);   // the stylesheet's round bevel
    QPainter p(this);
    const QPixmap arrow = arrowPixmap(isChecked() ? Qt::UpArrow : Qt::DownArrow,
                                      Palette::rgb(Palette::LinkText));
    const QSizeF s = arrow.deviceIndependentSize();
    p.drawPixmap(QPointF((width() - s.width()) / 2.0,
                         (height() - s.height()) / 2.0), arrow);
}

} // namespace Aero
