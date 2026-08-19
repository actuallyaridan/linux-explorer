#pragma once

// The horizontal bars framing a Win7 window, the command bar, the status panel,
// and the notification strip that slides in between them

#include "palette.h"

#include <QFrame>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPropertyAnimation;
class QTimer;

namespace Aero {

class LinkLabel;

// A bar whose background is Win7 artwork tiled sideways, the framing strips
// differing only in height and in which edge carries the hairline
class Strip : public QFrame {
    Q_OBJECT

public:
    explicit Strip(QWidget *parent = nullptr);

    // The background colour shows wherever the image stops, so a strip taller
    // than its artwork keeps going in a flat tone
    void setArt(const QString &resource, const char *backgroundColor = nullptr);

    // Not named layout, which QWidget already has and does not return this type
    QHBoxLayout *row() const { return m_row; }

private:
    QHBoxLayout *m_row = nullptr;
};

// Pinned to its artwork's height, since it is applied unscaled
Strip *commandBar(const QString &art, int height = 31);

Strip *statusPanel(const QString &art, int height,
                   const char *backgroundColor = Palette::StripSurface);

// The pale yellow strip beneath the command bar, which knows only how to arrive
// and how to be waved away
class NotificationStrip : public QWidget {
    Q_OBJECT

public:
    explicit NotificationStrip(QWidget *parent = nullptr);

    // Called again while visible this only swaps the text, since restarting the
    // timer would let a changing message postpone the strip forever
    void showMessage(const QString &text, bool dismissable);

    // Hides it at once, with no animation on the way out
    void clear();

Q_SIGNALS:
    void clicked();
    void dismissed();

protected:
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    static constexpr int kDelay = 1000;
    static constexpr int kSlideDuration = 180;

    LinkLabel *m_text = nullptr;
    LinkLabel *m_dismiss = nullptr;
    QTimer *m_timer = nullptr;
    QPropertyAnimation *m_slide = nullptr;
};

} // namespace Aero
