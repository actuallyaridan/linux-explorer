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


} // namespace Aero
