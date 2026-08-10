#pragma once

// Shared Windows 7 look-and-feel building blocks:
//
//  - Win7::label()/bodyLabel() the standard 9pt text
//  - Win7::commandBar()        the "Organize ▾" ribbon strip
//  - Win7::statusPanel()       the matching details strip along the bottom
//  - Win7::configureListTree() the flat Win7 column-list look for item views
//  - Win7::arrowPixmap()       THE dropdown arrow (one design, painted)
//  - Win7::MenuButton          flat text + arrow button that opens a menu
//
// Started as the Explorer-facing subset of the Control Panel's toolkit
// (linux-control, src/ui/Win7Ui.h) and the shared pieces still look the same,
// but the two are NOT kept in lockstep: each app matches the part of Windows 7
// it recreates, and those differ (Explorer's column headers are borderless,
// the Control Panel's list pages are not). Take a real fix from the other
// copy; do not sync styling for its own sake.
//
// Header-only and moc-free, so nothing here needs listing for AUTOMOC.

#include "IconHelper.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace Win7 {

// ---- Typography -----------------------------------------------------------

inline void setPointSize(QWidget *w, int pt)
{
    QFont f = w->font();
    f.setPointSize(pt);
    w->setFont(f);
}

// A transparent-background label at the given point size and colour.
inline QLabel *label(const QString &text, int pt = 9,
                     const char *color = "#000000")
{
    auto *l = new QLabel(text);
    setPointSize(l, pt);
    l->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;").arg(QLatin1String(color)));
    return l;
}

// 9pt body text, optionally styled as a blue task link.
inline QLabel *bodyLabel(const QString &text, bool link = false)
{
    auto *l = new QLabel(text);
    setPointSize(l, 9);
    // Aligned here rather than through addWidget: an alignment flag passed to
    // QGridLayout::addWidget suppresses height-for-width, clipping a
    // word-wrapped label to one line.
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (link) {
        l->setCursor(Qt::PointingHandCursor);
        l->setStyleSheet(
            "QLabel { color: #1F4E99; background: transparent; }"
            "QLabel:hover { color: #0033AA; }");
    } else {
        l->setStyleSheet("color: #000000; background: transparent;");
    }
    return l;
}

// A 1px hairline rule.
inline QFrame *hairline(const char *color = "#DDDDDD")
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(
        QStringLiteral("QFrame { background: %1; border: none; }").arg(QLatin1String(color)));
    return line;
}

// ---- The Aero arrow -------------------------------------------------------

// The one arrow glyph used across the app, for command-bar dropdowns, view
// menus, breadcrumbs and expanders. `width` is the triangle's base, the point
// centred opposite it at Win7's roughly 7:4 proportions. Rendered at 2x so the
// antialiased edges stay crisp.
inline QPixmap arrowPixmap(Qt::ArrowType dir,
                           const QColor &color = QColor(0x3C, 0x3C, 0x3C),
                           int width = 7)
{
    const int w = width;
    const int h = qMax(3, (w * 4 + 3) / 7);
    const bool vertical = (dir == Qt::UpArrow || dir == Qt::DownArrow);
    const QSize size = vertical ? QSize(w, h) : QSize(h, w);

    QPixmap pm(size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    QPolygonF poly;
    switch (dir) {
    case Qt::UpArrow:
        poly << QPointF(0, h) << QPointF(w, h) << QPointF(w / 2.0, 0);
        break;
    case Qt::LeftArrow:
        poly << QPointF(h, 0) << QPointF(h, w) << QPointF(0, w / 2.0);
        break;
    case Qt::RightArrow:
        poly << QPointF(0, 0) << QPointF(0, w) << QPointF(h, w / 2.0);
        break;
    case Qt::DownArrow:
    default:
        poly << QPointF(0, 0) << QPointF(w, 0) << QPointF(w / 2.0, h);
        break;
    }
    p.drawPolygon(poly);
    return pm;
}

// A label showing the shared Aero arrow.
inline QLabel *arrowLabel(Qt::ArrowType dir,
                          const QColor &color = QColor(0x3C, 0x3C, 0x3C),
                          int width = 7)
{
    auto *l = new QLabel;
    l->setStyleSheet("background: transparent;");
    l->setPixmap(arrowPixmap(dir, color, width));
    return l;
}

// A flat text button ending in the Aero arrow that opens its menu on click,
// the Win7 "Organize ▾" / "View by: Category" dropdown style.
class MenuButton : public QToolButton {
public:
    explicit MenuButton(const QString &text, QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setText(text);
        setPopupMode(QToolButton::InstantPopup);
        setToolButtonStyle(Qt::ToolButtonTextOnly);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet("QToolButton { border: none; background: transparent; }");
        Win7::setPointSize(this, 9);
    }

    void setColors(const QColor &normal, const QColor &hover)
    {
        m_normal = normal;
        m_hover = hover;
        update();
    }

    // The command bar mixes menu buttons with plain commands ("Open", "Burn"),
    // which share this class for the typography and hover colour. Only the
    // former get the arrow.
    void setShowArrow(bool show)
    {
        m_showArrow = show;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        const int arrow = m_showArrow ? kSpacing + kArrowWidth : 0;
        return QSize(kPadding + fm.horizontalAdvance(text()) + arrow + kPadding,
                     fm.height() + 4);
    }

protected:
    void enterEvent(QEnterEvent *e) override { update(); QToolButton::enterEvent(e); }
    void leaveEvent(QEvent *e) override      { update(); QToolButton::leaveEvent(e); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QColor color = underMouse() ? m_hover : m_normal;
        p.setFont(font());
        p.setPen(color);
        const int textWidth = QFontMetrics(font()).horizontalAdvance(text());
        p.drawText(QRect(kPadding, 0, textWidth, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text());

        if (!m_showArrow)
            return;

        const QPixmap arrow = arrowPixmap(Qt::DownArrow, color, kArrowWidth);
        const QSizeF as = arrow.deviceIndependentSize();
        p.drawPixmap(QPointF(kPadding + textWidth + kSpacing,
                             (height() - as.height()) / 2.0 + 1), arrow);
    }

private:
    static constexpr int kArrowWidth = 6;
    static constexpr int kSpacing = 5;
    // Measured against the real command bar: one item's arrow and the next
    // item's first letter sit about 24px apart. Carried by the buttons rather
    // than as layout spacing, so the whole padded box is clickable and the
    // strip's own 6px spacing stays free for the trailing icon buttons.
    static constexpr int kPadding = 9;
    bool m_showArrow = true;
    QColor m_normal{0x1F, 0x1F, 0x1F};
    QColor m_hover{0x00, 0x33, 0x99};
};

// The round Aero expander used by collapsible sections and the file operation
// dialog's "More details". Checked = expanded = arrow points up.
class ChevronButton : public QToolButton {
public:
    explicit ChevronButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(20, 20);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet(
            "QToolButton { border: 1px solid #C0CEDA; border-radius: 10px;"
            " background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
            " stop:0 #FDFEFF, stop:1 #E8F1F8); }"
            "QToolButton:hover { background: #E4EEF7; }"
            "QToolButton:pressed { background: #D6E4F0; }");
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QToolButton::paintEvent(e);   // the stylesheet's round bevel
        QPainter p(this);
        const QPixmap arrow = arrowPixmap(
            isChecked() ? Qt::UpArrow : Qt::DownArrow, QColor(0x1F, 0x4E, 0x99));
        const QSizeF s = arrow.deviceIndependentSize();
        p.drawPixmap(QPointF((width() - s.width()) / 2.0,
                             (height() - s.height()) / 2.0), arrow);
    }
};

// ---- The ribbon list chrome -----------------------------------------------

// The command-bar strip with its bottom hairline. The caller fills the
// returned layout ("Organize" dropdown, task links, trailing icons).
inline QFrame *commandBar(QHBoxLayout **layoutOut = nullptr)
{
    auto *bar = new QFrame;
    bar->setObjectName("win7CommandBar");
    // The artwork's own height. It is applied unscaled, so anything else would
    // clip it or leave a gap.
    bar->setFixedHeight(31);
    // repeat-x rather than a stretch: every column of the image is identical, so
    // tiling is exact at any width and costs no filtering. The bottom rule is
    // the image's last row, hence no border-bottom.
    bar->setStyleSheet(
        "#win7CommandBar { background: url(:/win7/commandbar.png) repeat-x; }");
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 0, 8, 0);
    h->setSpacing(6);
    if (layoutOut)
        *layoutOut = h;
    return bar;
}

// The matching status/details strip along the bottom (same palette, hairline
// on top). The caller fills the returned layout with its icon and counts.
inline QFrame *statusPanel(int height, QHBoxLayout **layoutOut = nullptr)
{
    auto *bar = new QFrame;
    bar->setObjectName("win7StatusPanel");
    bar->setFixedHeight(height);
    // Not pinned to its artwork's height, unlike the command bar: everything
    // below the image's first few rows is flat #F1F5FB, so the background
    // colour carries on wherever the image stops and only the top edge and its
    // hairline come from the image. background-position keeps that edge at the
    // top; repeat-x tiles the identical columns sideways.
    bar->setStyleSheet(
        "#win7StatusPanel {"
        " background-color: #F1F5FB;"
        " background-image: url(:/win7/detailsbar.png);"
        " background-repeat: repeat-x;"
        " background-position: top left; }");
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 0, 10, 0);
    h->setSpacing(8);
    if (layoutOut)
        *layoutOut = h;
    return bar;
}

// The shared column-list configuration for the Win7 details view. Styling is
// scoped to the header, so the tree body and its scroll bars keep the real Qt
// style; see main() on the scroll bars.
inline void configureListTree(QTreeView *tree)
{
    tree->setRootIsDecorated(false);
    tree->setItemsExpandable(false);
    tree->setIndentation(0);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Qt's default of DoubleClicked|SelectedClicked would turn a double-click
    // into an inline rename instead of opening the item, and KDirModel::setData
    // commits that to disk, bypassing FileOps and its undo recording. Renaming
    // goes through F2 only.
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setAlternatingRowColors(false);
    tree->setFrameShape(QFrame::NoFrame);
    // BEFORE sorting is enabled: QHeaderView starts its indicator at
    // Qt::DescendingOrder, and setSortingEnabled(true) immediately re-sorts by
    // whatever it says, leaving the list running Z to A.
    tree->header()->setSortIndicator(0, Qt::AscendingOrder);
    tree->setSortingEnabled(true);
    tree->header()->setDefaultAlignment(Qt::AlignLeft);
    tree->header()->setStretchLastSection(true);
    setPointSize(tree->header(), 9);
    // Borderless: Win7's column headers have no section dividers and no rule
    // beneath, the titles sitting on the same white as the rows.
    tree->header()->setStyleSheet(
        "QHeaderView::section {"
        "  background: #FFFFFF;"
        "  border: none;"
        "  padding: 4px;"
        "  font-size: 9pt;"
        "}");
    // Pinned rather than left to the size hint, which is computed and cached
    // before the app-wide Aero stylesheet polishes the widget, leaving the
    // header twice as tall until a sort click invalidates it. Every input is
    // known anyway: 9pt text, 4px padding, 1px bottom border.
    const QFontMetrics fm(tree->header()->font());
    tree->header()->setFixedHeight(fm.height() + 2 * 4 + 1);
}

} // namespace Win7
