#pragma once

// Shared Windows 7 look-and-feel building blocks for the Control Panel pages.
//
// Every detail page used to hand-roll the same pieces, the sidebar/content
// scaffold, 9pt labels, the Explorer command bar, list headers, dropdown
// arrows, each slightly differently. This header is the single home for
// those pieces so the pages stay visually identical to one another:
//
//  - Win7::pageScaffold()      sidebar | white content column root layout
//  - Win7::label()/bodyLabel() the standard 9pt text (plain, link, wrapped)
//  - Win7::commandBar()        the "Organize ▾" ribbon strip above a list
//  - Win7::statusPanel()       the matching status/details strip below it
//  - Win7::configureListTree() the flat Win7 column-list look for QTreeWidget
//  - Win7::arrowPixmap()       THE dropdown arrow (one design, painted)
//  - Win7::ChevronButton       the round Aero expander for collapsible rows
//  - Win7::MenuButton          flat text + arrow button that opens a menu
//  - Win7::ClickableWidget     a widget with an onClick callback
//  - Win7::runAsync()          gather on a worker thread, populate on the GUI
//
// Everything is header-only and moc-free (no Q_OBJECT), so nothing here needs
// to be listed in CMakeLists for AUTOMOC.

#include "IconHelper.h"

#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QScrollArea>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <functional>
#include <type_traits>
#include <utility>

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

// The big blue page heading.
inline QLabel *pageTitle(const QString &text, int pt = 12,
                         const char *color = "#1A3C7A")
{
    return label(text, pt, color);
}

// 9pt word-wrapped body text; optionally styled as a blue task link.
inline QLabel *bodyLabel(const QString &text, bool link = false)
{
    auto *l = new QLabel(text);
    setPointSize(l, 9);
    l->setWordWrap(true);
    // Align via the label itself, not an addWidget alignment flag: passing an
    // alignment flag to QGridLayout::addWidget suppresses height-for-width, so
    // a word-wrapped label would be clipped to a single line of height.
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
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

// ---- Separators and page scaffold -----------------------------------------

// The full-width separator under a page's title/instruction block.
inline QFrame *hSeparator(const char *color = "#D9D9D9")
{
    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(color)));
    return sep;
}

// A 1px hairline rule (section dividers, heading rules).
inline QFrame *hairline(const char *color = "#DDDDDD")
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(
        QStringLiteral("QFrame { background: %1; border: none; }").arg(QLatin1String(color)));
    return line;
}

// Root layout of a sidebar detail page: [ sidebar | white content column ].
// Returns the content column's layout. With fixedWidth > 0 the column keeps
// that width and the leftover space stays white (Firewall/Fonts/Power style);
// otherwise the column stretches to fill the window (Programs and Features).
//
// The backgrounds are set with ID-scoped rules, NOT declaration-only sheets:
// a declaration-only stylesheet ("background: #FFFFFF;") acts as `* { ... }`,
// matching every descendant, and any widget matched by any stylesheet rule
// is rendered by Qt's stylesheet engine instead of the platform style, which
// is what used to turn all the scroll bars (and buttons) non-native.
inline QVBoxLayout *pageScaffold(QWidget *page, QScrollArea *sidebar,
                                 int bottomMargin = 20, int fixedWidth = -1,
                                 int topMargin = 18)
{
    page->setObjectName("win7Page");
    page->setStyleSheet(QStringLiteral("#win7Page { background: #FFFFFF; }"));
    auto *root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(sidebar);

    auto *content = new QWidget;
    content->setObjectName("win7PageContent");
    content->setStyleSheet(
        QStringLiteral("#win7PageContent { background: #FFFFFF; }"));
    auto *contentV = new QVBoxLayout(content);
    contentV->setContentsMargins(28, topMargin, 28, bottomMargin);
    contentV->setSpacing(0);

    if (fixedWidth > 0) {
        content->setFixedWidth(fixedWidth);
        root->addWidget(content, 0);
        root->addStretch(1);
    } else {
        root->addWidget(content, 1);
    }
    return contentV;
}

// Section heading: a muted 9pt label with a hairline rule trailing off to the
// right, optionally ending in a trailing widget (e.g. an expander chevron).
inline QHBoxLayout *sectionHeading(const QString &text,
                                   QWidget *trailing = nullptr,
                                   QLabel **labelOut = nullptr,
                                   const char *color = "#555555")
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    auto *heading = label(text, 9, color);
    row->addWidget(heading, 0, Qt::AlignVCenter);
    row->addWidget(hairline(), 1, Qt::AlignVCenter);
    if (trailing)
        row->addWidget(trailing, 0, Qt::AlignVCenter);
    if (labelOut)
        *labelOut = heading;
    return row;
}

// ---- The Aero arrow -------------------------------------------------------

// The one arrow glyph used across the app: the small solid triangle Windows 7
// draws for command-bar dropdowns ("Organize"), view menus, breadcrumbs and
// expanders. `width` is the triangle's base; the point sits centred on the
// opposite side with Win7's roughly 7:4 proportions. Rendered at 2x device
// ratio so the antialiased edges stay crisp.
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

// An "Organize ▾"-style command-bar item: text followed by the Aero arrow.
inline QWidget *dropdownLabel(const QString &text, int pt = 9,
                              const char *color = "#1F1F1F")
{
    auto *w = new QWidget;
    w->setStyleSheet("background: transparent;");
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(5);
    h->addWidget(label(text, pt, color));
    h->addWidget(arrowLabel(Qt::DownArrow, QColor(0x3C, 0x3C, 0x3C), 6),
                 0, Qt::AlignVCenter);
    return w;
}

// A flat text button ending in the Aero arrow that opens its menu on click,
// the Win7 "View by: Category" dropdown style.
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

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return QSize(fm.horizontalAdvance(text()) + kSpacing + kArrowWidth + 2,
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
        p.drawText(QRect(0, 0, textWidth, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text());

        const QPixmap arrow = arrowPixmap(Qt::DownArrow, color, kArrowWidth);
        const QSizeF as = arrow.deviceIndependentSize();
        p.drawPixmap(QPointF(textWidth + kSpacing,
                             (height() - as.height()) / 2.0 + 1), arrow);
    }

private:
    static constexpr int kArrowWidth = 6;
    static constexpr int kSpacing = 5;
    QColor m_normal{0x00, 0x33, 0x99};
    QColor m_hover{0xFF, 0x66, 0x00};
};

// The round Aero expander button used by every collapsible section (Power
// Options' "Show additional plans", the Action Center and Firewall section
// headers). Checked = expanded = arrow points up. When the whole header strip
// is the click target, pass interactive = false so clicks fall through to it.
class ChevronButton : public QToolButton {
public:
    explicit ChevronButton(bool interactive = true, QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(20, 20);
        setFocusPolicy(Qt::NoFocus);
        if (!interactive)
            setAttribute(Qt::WA_TransparentForMouseEvents);
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

// ---- Click plumbing -------------------------------------------------------

// A bare widget that runs a callback when clicked, used for collapsible
// section headers so the whole strip toggles the body below. No signals are
// needed, so this stays a plain QWidget (no Q_OBJECT / moc).
class ClickableWidget : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClick)
            onClick();
        QWidget::mousePressEvent(event);
    }
};

// ---- The ribbon list chrome -----------------------------------------------
// The command bar / list / status panel design of the Uninstall a Program
// page, shared by every page that shows a list under an Explorer-style bar.

// The flat 28px command-bar strip with its bottom hairline. The caller fills
// the returned layout ("Organize" dropdown, task links, trailing icons).
inline QFrame *commandBar(QHBoxLayout **layoutOut = nullptr)
{
    auto *bar = new QFrame;
    bar->setObjectName("win7CommandBar");
    bar->setFixedHeight(28);
    bar->setStyleSheet(
        "#win7CommandBar { background: #F4F7FB; border-bottom: 1px solid #D9D9D9; }");
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 0, 8, 0);
    h->setSpacing(6);
    if (layoutOut)
        *layoutOut = h;
    return bar;
}

// The command bar's right-hand ornaments: the change-view and help icons.
inline void addCommandBarIcons(QHBoxLayout *h)
{
    auto *viewIcon = new QLabel;
    viewIcon->setStyleSheet("background: transparent;");
    viewIcon->setPixmap(themeIcon({"view-list-details", "view-list-text",
                                   "view-choose"}).pixmap(16, 16));
    h->addWidget(viewIcon);

    auto *helpIcon = new QLabel;
    helpIcon->setStyleSheet("background: transparent;");
    helpIcon->setPixmap(themeIcon({"help-contents", "help-browser",
                                   "system-help"}).pixmap(16, 16));
    h->addWidget(helpIcon);
}

// The matching status/details strip under the list (same palette, hairline on
// top). The caller fills the returned layout with its icon and counts.
inline QFrame *statusPanel(int height, QHBoxLayout **layoutOut = nullptr)
{
    auto *bar = new QFrame;
    bar->setObjectName("win7StatusPanel");
    bar->setFixedHeight(height);
    bar->setStyleSheet(
        "#win7StatusPanel { background: #F4F7FB; border-top: 1px solid #D9D9D9; }");
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 0, 10, 0);
    h->setSpacing(8);
    if (layoutOut)
        *layoutOut = h;
    return bar;
}

// The shared column-list configuration for the ribbon pages' QTreeWidgets:
// frameless, flat Win7 header, single selection. Styling is scoped to the
// header only, so the tree body and its scroll bars keep the real Qt style
// (the scroll bars are native app-wide: main() strips AeroQt's QScrollBar
// rules out of the application stylesheet).
inline void configureListTree(QTreeWidget *tree)
{
    tree->setRootIsDecorated(false);
    tree->setIndentation(0);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setAlternatingRowColors(false);
    tree->setFrameShape(QFrame::NoFrame);
    tree->header()->setDefaultAlignment(Qt::AlignLeft);
    tree->header()->setStretchLastSection(true);
    setPointSize(tree->header(), 9);
    tree->header()->setStyleSheet(
        "QHeaderView::section {"
        "  background: #F0F0F0;"
        "  border: none;"
        "  border-bottom: 1px solid #CCCCCC;"
        "  border-right: 1px solid #CCCCCC;"
        "  padding: 4px;"
        "  font-size: 9pt;"
        "}");
    // Pin the header height instead of trusting its size hint: the hint is
    // computed once before the app-wide Aero stylesheet polishes the widget
    // and cached, leaving the header roughly twice as tall until a sort click
    // happens to invalidate the cache. Every input is known here anyway,
    // 9pt text plus the stylesheet's 4px padding and 1px bottom border.
    const QFontMetrics fm(tree->header()->font());
    tree->header()->setFixedHeight(fm.height() + 2 * 4 + 1);
}

// The centred single-row "Searching for ..." placeholder shown while a worker
// thread gathers the list.
inline void addTreePlaceholder(QTreeWidget *tree, const QString &text)
{
    auto *row = new QTreeWidgetItem(tree);
    row->setFirstColumnSpanned(true);
    row->setTextAlignment(0, Qt::AlignHCenter);
    row->setText(0, text);
    row->setFlags(Qt::ItemIsEnabled);
}

// Apply the 9pt list font to every column of an item.
inline void setItemPointSize(QTreeWidgetItem *item, int pt = 9)
{
    for (int c = 0; c < item->columnCount(); ++c) {
        QFont f = item->font(c);
        f.setPointSize(pt);
        item->setFont(c, f);
    }
}

// ---- Worker-thread gathering ----------------------------------------------

// Run `job` on a worker thread and hand its result to `done` on the GUI
// thread. `owner` scopes the delivery: if it is destroyed first, the result is
// dropped. The pages use this to navigate first and populate second, so a
// slow pacman/sysfs query never blocks the page from appearing.
template <typename Job, typename Done>
inline void runAsync(QObject *owner, Job job, Done done)
{
    using T = std::invoke_result_t<Job>;
    auto *watcher = new QFutureWatcher<T>(owner);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, owner,
                     [watcher, done = std::move(done)]() {
        done(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(std::move(job)));
}

} // namespace Win7
