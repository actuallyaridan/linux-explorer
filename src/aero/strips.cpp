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

Strip *statusPanel(const QString &art, int height, const char *backgroundColor)
{
    auto *bar = new Strip;
    // Unlike the command bar this is not pinned to the artwork, only the top
    // edge and hairline coming from the image and the rest from the flat tone
    bar->setFixedHeight(height);
    bar->setArt(art, backgroundColor);
    bar->row()->setContentsMargins(10, 0, 10, 0);
    bar->row()->setSpacing(8);
    return bar;
}

NotificationStrip::NotificationStrip(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aeroNotification"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#aeroNotification { background: %1;"
                                 " border-bottom: 1px solid %2; }")
                      .arg(QLatin1String(Palette::NoticeSurface),
                           QLatin1String(Palette::NoticeRule)));
    QWidget::hide();

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(10, 6, 8, 6);
    row->setSpacing(8);

    m_text = new LinkLabel;
    m_text->setColors(Palette::Text, Palette::Text);
    setPointSize(m_text, 9);
    row->addWidget(m_text, 1);

    m_dismiss = new LinkLabel(QStringLiteral("✕"));
    m_dismiss->setColors(Palette::Text, Palette::MutedHover);
    setPointSize(m_dismiss, 9);
    row->addWidget(m_dismiss, 0, Qt::AlignVCenter);

    m_slide = new QPropertyAnimation(this, "maximumHeight", this);
    m_slide->setDuration(kSlideDuration);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QPropertyAnimation::finished, this, [this] {
        // Released so the strip can grow if its text wraps to a second line
        if (isVisible())
            setMaximumHeight(QWIDGETSIZE_MAX);
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kDelay);
    connect(m_timer, &QTimer::timeout, this, [this] {
        const int target = sizeHint().height();
        setMaximumHeight(0);
        QWidget::show();
        m_slide->setStartValue(0);
        m_slide->setEndValue(target);
        m_slide->start();
    });

    // The close box is a child, so dismissing must not also count as a click
    connect(m_text, &LinkLabel::clicked, this, &NotificationStrip::clicked);
    connect(m_dismiss, &LinkLabel::clicked, this, [this] {
        clear();
        Q_EMIT dismissed();
    });
}

void NotificationStrip::showMessage(const QString &text, bool dismissable)
{
    m_text->setText(text);
    m_dismiss->setVisible(dismissable);

    // Already up or on its way, so the refreshed text is all that changes
    if (isVisible() || m_timer->isActive())
        return;

    m_timer->start();
}

void NotificationStrip::clear()
{
    m_timer->stop();
    m_slide->stop();
    QWidget::hide();
    setMaximumHeight(QWIDGETSIZE_MAX);
}

void NotificationStrip::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
        Q_EMIT clicked();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

} // namespace Aero
