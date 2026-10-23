#pragma once

#include <QWidget>
#include <QPainter>
#include <QTime>
#include <QPointF>

// A small Windows 7-style analog clock face. It reads the wall clock itself in
// paintEvent, so ticking it is just a repaint request from a caller's timer;
// there is no state to keep in sync. No Q_OBJECT is needed as it has no signals
// or slots of its own.
class AnalogClock : public QWidget {
public:
    explicit AnalogClock(int diameter = 96, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(diameter, diameter);
        setStyleSheet("background: transparent;");
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const QTime time = QTime::currentTime();

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(width() / 2.0, height() / 2.0);
        const int side = qMin(width(), height());
        p.scale(side / 130.0, side / 130.0);

        // Face: a soft radial-shaded white disc with a thin grey rim, echoing
        // the subtle 3D bezel of the Aero clock.
        QRadialGradient face(QPointF(0, -8), 78, QPointF(0, -18));
        face.setColorAt(0.0, QColor("#FFFFFF"));
        face.setColorAt(1.0, QColor("#E8ECF2"));
        p.setPen(QPen(QColor("#8595AB"), 2));
        p.setBrush(face);
        p.drawEllipse(QPointF(0, 0), 60, 60);

        // Minute ticks (thin) and hour ticks (thick).
        for (int i = 0; i < 60; ++i) {
            if (i % 5 == 0) {
                p.setPen(QPen(QColor("#33475F"), 1.6));
                p.drawLine(52, 0, 58, 0);
            } else {
                p.setPen(QPen(QColor("#9AA7BA"), 0.7));
                p.drawLine(55, 0, 58, 0);
            }
            p.rotate(6.0);
        }

        // Hands are plain straight lines: a thicker, shorter hour hand, a
        // thinner, longer minute hand, and a fine red second hand.
        p.setPen(Qt::NoPen);

        // Hour hand.
        p.save();
        p.rotate(30.0 * (time.hour() + time.minute() / 60.0));
        p.setPen(QPen(QColor("#1B2B44"), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, 6), QPointF(0, -33));
        p.restore();

        // Minute hand.
        p.save();
        p.rotate(6.0 * (time.minute() + time.second() / 60.0));
        p.setPen(QPen(QColor("#1B2B44"), 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, 8), QPointF(0, -49));
        p.restore();

        // Second hand: a thin red sweep, as on the Windows clock.
        p.save();
        p.rotate(6.0 * time.second());
        p.setPen(QPen(QColor("#C0392B"), 1.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, 8), QPointF(0, -52));
        p.restore();

        // Centre cap.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1B2B44"));
        p.drawEllipse(QPointF(0, 0), 3.2, 3.2);
    }
};
