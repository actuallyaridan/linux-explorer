#pragma once

// The two buttons Win7's chrome is built from, the flat command bar dropdown
// and the round expander that opens a section

#include "palette.h"

#include <QColor>
#include <QString>
#include <QToolButton>

namespace Aero {

// A flat text button ending in the Aero arrow, the Win7 Organize style
class MenuButton : public QToolButton {
    Q_OBJECT

public:
    explicit MenuButton(const QString &text, QWidget *parent = nullptr);

    // Application wide rather than per button, a command bar being one surface
    // its buttons cannot disagree about
    static void setPillArt(const QString &hover, const QString &pressed);

    // One colour rather than a normal and hover pair, since the pill behind the
    // button is what shows the hover
    void setColor(const QColor &normal);

    // Plain commands share this class but get no arrow
    void setShowArrow(bool show);

    QSize sizeHint() const override;

protected:
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void paintEvent(QPaintEvent *) override;

private:
    static constexpr int kArrowWidth = 6;
    static constexpr int kSpacing = 5;
    static constexpr int kPillHeight = 24;
    static constexpr int kPillCap = 3;
    // Padding rather than layout spacing, so the whole box is clickable
    static constexpr int kPadding = 9;

    bool m_showArrow = true;
    QColor m_normal = Palette::rgb(Palette::SoftText);
};

// The round Aero expander, checked when expanded, its arrow pointing up
class ChevronButton : public QToolButton {
    Q_OBJECT

public:
    explicit ChevronButton(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;
};

} // namespace Aero
