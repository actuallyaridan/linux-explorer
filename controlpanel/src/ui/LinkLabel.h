#pragma once

#include <QLabel>
#include <QFont>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QStringList>
#include <QProcess>
#include <QStandardPaths>
#include <QMessageBox>
#include <QWidget>

// A blue Control-Panel task link. Gives the pointing-hand cursor, the app's
// link colours, an underline on hover, and a clicked() signal on left release,
// so the detail pages don't each have to reimplement the "QLabel that behaves
// like a link" event plumbing that MainWindow drives for the home/category
// grids.
//
// A plain QLabel ignores the mouse press (it has no text-interaction flags), so
// its parent grabs the mouse and the release never reaches the label. We accept
// the press ourselves to become the grabber and receive the matching release.
class LinkLabel : public QLabel {
    Q_OBJECT
public:
    explicit LinkLabel(const QString &text, QWidget *parent = nullptr)
        : QLabel(text, parent)
    {
        QFont f = font();
        f.setPointSize(9);
        setFont(f);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(
            "QLabel { color: #1F4E99; background: transparent; }"
            "QLabel:hover { color: #0033AA; }");
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            e->accept();
        else
            QLabel::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
            emit clicked();
        QLabel::mouseReleaseEvent(e);
    }

    // Qt's QLabel ignores `text-decoration` in style sheets, so the hover
    // underline is driven by toggling the font's underline flag directly.
    void enterEvent(QEnterEvent *e) override
    {
        QFont f = font();
        f.setUnderline(true);
        setFont(f);
        QLabel::enterEvent(e);
    }

    void leaveEvent(QEvent *e) override
    {
        QFont f = font();
        f.setUnderline(false);
        setFont(f);
        QLabel::leaveEvent(e);
    }
};

// Launch an external helper (a kcmshell6 module, kfontview, a settings applet…)
// detached. If the program isn't on PATH, tell the user rather than failing
// silently, the same courtesy MainWindow extends to its own command links.
inline void launchDetached(QWidget *parent, const QStringList &cmd)
{
    if (cmd.isEmpty())
        return;
    QStringList args = cmd;
    const QString program = args.takeFirst();
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Control Panel"),
            QStringLiteral("\"%1\" is not installed.").arg(program));
        return;
    }
    QProcess::startDetached(program, args);
}
