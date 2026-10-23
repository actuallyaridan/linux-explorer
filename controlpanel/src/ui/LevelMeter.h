#pragma once

#include <QWidget>
#include <QPainter>

// A vertical segmented audio level meter, green segments lighting from the
// bottom, like the level indicator beside each device in the Windows Sound
// dialog. Driven live via setLevel() from a real audio capture; no Q_OBJECT
// needed as it has no signals or slots of its own.
class LevelMeter : public QWidget {
public:
    explicit LevelMeter(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(16, 42);
    }

    void setLevel(int percent)
    {
        const int c = qBound(0, percent, 100);
        if (c != m_level) {
            m_level = c;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const int n = 14, gap = 1;
        const double segH = (height() - (n - 1) * gap) / double(n);
        const int lit = qRound(m_level / 100.0 * n);
        QPainter p(this);
        for (int i = 0; i < n; ++i) {
            const QRectF r(1, height() - (i + 1) * segH - i * gap,
                           width() - 2, segH);
            p.fillRect(r, i < lit ? QColor("#63C13E") : QColor("#DCE1DA"));
        }
    }

private:
    int m_level = 0;
};
