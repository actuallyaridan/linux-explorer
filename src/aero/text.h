#pragma once

// Win7 text pieces, the 9pt labels, the rule, and a clickable label

#include "palette.h"

#include <QLabel>
#include <QString>

class QFrame;
class QWidget;

namespace Aero {

void setPointSize(QWidget *w, int pt);

QLabel *label(const QString &text, int pt = 9, const char *color = Palette::Text);

QLabel *bodyLabel(const QString &text, bool link = false);

QFrame *hairline(const char *color = Palette::Hairline);

// A label that answers the mouse with a hand cursor, a hover colour, and a
// clicked signal on release
class LinkLabel : public QLabel {
    Q_OBJECT

public:
    explicit LinkLabel(const QString &text = QString(), QWidget *parent = nullptr);

    // Palette entries, applied through the stylesheet's hover state
    void setColors(const char *normal, const char *hover);

    // Not a stylesheet rule, QLabel ignoring text decoration, so this toggles
    // the font's underline flag as the cursor comes and goes
    void setUnderlineOnHover(bool on);

Q_SIGNALS:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void setUnderlined(bool on);

    bool m_underlineOnHover = false;
};

} // namespace Aero
