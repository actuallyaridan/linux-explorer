#include "text.h"

#include <QFrame>
#include <QMouseEvent>

namespace Aero {

void setPointSize(QWidget *w, int pt)
{
    QFont f = w->font();
    f.setPointSize(pt);
    w->setFont(f);
}

QLabel *label(const QString &text, int pt, const char *color)
{
    auto *l = new QLabel(text);
    setPointSize(l, pt);
    l->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;").arg(QLatin1String(color)));
    return l;
}

QLabel *bodyLabel(const QString &text, bool link)
{
    if (link) {
        auto *l = new LinkLabel(text);
        setPointSize(l, 9);
        l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return l;
    }

    auto *l = new QLabel(text);
    setPointSize(l, 9);
    // Not through addWidget, where an alignment flag suppresses height for
    // width and clips a word wrapped label to one line
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    l->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                         .arg(QLatin1String(Palette::Text)));
    return l;
}

QFrame *hairline(const char *color)
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(
        QStringLiteral("QFrame { background: %1; border: none; }").arg(QLatin1String(color)));
    return line;
}

LinkLabel::LinkLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    setColors(Palette::LinkText, Palette::LinkHover);
}

void LinkLabel::setColors(const char *normal, const char *hover)
{
    setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }"
                                 "QLabel:hover { color: %2; }")
                      .arg(QLatin1String(normal), QLatin1String(hover)));
}

void LinkLabel::setUnderlineOnHover(bool on)
{
    m_underlineOnHover = on;
    if (!on)
        setUnderlined(false);
}

void LinkLabel::setUnderlined(bool on)
{
    QFont f = font();
    if (f.underline() == on)
        return;
    f.setUnderline(on);
    setFont(f);
}

void LinkLabel::enterEvent(QEnterEvent *e)
{
    if (m_underlineOnHover)
        setUnderlined(true);
    QLabel::enterEvent(e);
}

void LinkLabel::leaveEvent(QEvent *e)
{
    if (m_underlineOnHover)
        setUnderlined(false);
    QLabel::leaveEvent(e);
}

void LinkLabel::mouseReleaseEvent(QMouseEvent *e)
{
    // Dragging off the label is how Windows lets a click be taken back
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
        Q_EMIT clicked();
        e->accept();
        return;
    }
    QLabel::mouseReleaseEvent(e);
}

} // namespace Aero
