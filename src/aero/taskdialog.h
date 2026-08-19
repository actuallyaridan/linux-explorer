#pragma once

// Win7 splits a dialog in two, a white content area and a grey command strip
// along the bottom carrying only the buttons

#include <QDialog>
#include <QIcon>
#include <QMargins>
#include <QString>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Aero {

class TaskDialog : public QDialog {
    Q_OBJECT

public:
    // With an icon the body takes Win7's warning shape and addLine fills it,
    // without one it is an ordinary column reached through contentLayout,
    // and a dialog uses one or the other rather than both
    explicit TaskDialog(QWidget *parent = nullptr, const QString &title = QString(),
                        const QIcon &icon = QIcon());

    QVBoxLayout *contentLayout() const { return m_content; }

    // Win7 pads dialog content by 16px, and content reaching the edges clears it
    void setContentMargins(const QMargins &margins);

    // A word wrapped line beside the icon
    void addLine(QLabel *line);

    QPushButton *addButton(const QString &text);

    // At the left end of the command strip, before the buttons
    void addFooterWidget(QWidget *w);

    // Width first, so wrapped text can be measured against it, and the height
    // follows from however many lines that came to
    void lockSize(int width);

private:
    QVBoxLayout *m_content = nullptr;
    QVBoxLayout *m_lines = nullptr;
    QHBoxLayout *m_footer = nullptr;
    // Widgets left of the stretch, separating footer widgets from buttons
    int m_footerLeft = 0;
};

} // namespace Aero
