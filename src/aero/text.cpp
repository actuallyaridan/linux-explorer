#include "text.h"

#include <QFrame>
#include <QMouseEvent>

namespace Aero {

void setPointSize(QWidget *w, int pt)
{
    QFont font = w->font();
    font.setPointSize(pt);
    w->setFont(font);
}

QLabel *label(const QString &text, int pt, const char *color)
{
    auto *textLabel = new QLabel(text);
    setPointSize(textLabel, pt);
    textLabel->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;").arg(QLatin1String(color)));
    return textLabel;
}

QLabel *bodyLabel(const QString &text, bool link)
{
    if (link) {
        auto *linkLabel = new LinkLabel(text);
        setPointSize(linkLabel, 9);
        linkLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return linkLabel;
    }

    auto *textLabel = new QLabel(text);
    setPointSize(textLabel, 9);
    // Not through addWidget, where an alignment flag suppresses height for
    // width and clips a word wrapped label to one line
    textLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    textLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                 .arg(QLatin1String(Palette::Text)));
    return textLabel;
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
    QFont underlined = font();
    if (underlined.underline() == on)
        return;
    underlined.setUnderline(on);
    setFont(underlined);
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
