#pragma once

#include <QWidget>
#include <QPainter>

namespace ControlPanel {

// A vertical segmented audio level meter, green segments lighting from the
// bottom, like the level indicator beside each device in the Windows Sound
// dialog. Driven live via setLevel() from a real audio capture; no Q_OBJECT
// needed as it has no signals or slots of its own
class LevelMeter : public QWidget {
public:
    explicit LevelMeter(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(16, 42);
    }

    void setLevel(int percent)
    {
        const int clamped = qBound(0, percent, 100);
        if (clamped != m_level) {
            m_level = clamped;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const int segments = 14;
        const int gap = 1;
        const double segH = (height() - (segments - 1) * gap) / double(segments);
        const int lit = qRound(m_level / 100.0 * segments);
        QPainter painter(this);
        // From the bottom up, green as far as the level reaches and grey above
        for (int i = 0; i < segments; ++i) {
            const QRectF rect(1, height() - (i + 1) * segH - i * gap,
                              width() - 2, segH);
            QColor color("#DCE1DA");
            if (i < lit)
                color = QColor("#63C13E");
            painter.fillRect(rect, color);
        }
    }

private:
    int m_level = 0;
};

} // namespace ControlPanel
