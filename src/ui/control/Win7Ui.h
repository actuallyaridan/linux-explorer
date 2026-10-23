#pragma once

// The building blocks only the Control Panel's pages use. Anything Explorer
// also draws, labels, arrows, links, the expander button and the command bar,
// comes from src/aero instead, so the two cannot drift apart:
//
//  - Win7::pageScaffold()      sidebar | white content column root layout
//  - Win7::pageTitle()         the big blue page heading
//  - Win7::wrappedLabel()      9pt body text that wraps, plain or a task link
//  - Win7::setBold()           bold or regular, beside Aero::setPointSize
//  - Win7::link()              a task link, Aero::LinkLabel set up as one
//  - Win7::commandBar()        Explorer's command bar, above a list
//  - Win7::addCommandBarIcons() its Change your view and Help buttons
//  - Win7::configureListTree() Explorer's details list, single selection
//  - Win7::LinkMenuButton      blue text and arrow that opens a menu
//  - Win7::ClickableWidget     a widget with an onClick callback
//  - Win7::runAsync()          gather on a worker thread, populate on the GUI
//
// Everything is header-only and moc-free (no Q_OBJECT), so nothing here needs
// to be listed in CMakeLists for AUTOMOC

#include "ui/control/IconHelper.h"
#include "Assets.h"
#include "aero/arrows.h"
#include "aero/listview.h"
#include "aero/buttons.h"
#include "aero/palette.h"
#include "aero/strips.h"
#include "aero/text.h"

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
#include "aero/artwork.h"

namespace ControlPanel {

namespace Win7 {

// Typography

// The big blue page heading
inline QLabel *pageTitle(const QString &text, int pt = 12,
                         const char *color = Aero::Palette::HeadingText)
{
    return Aero::label(text, pt, color);
}

// 9pt word-wrapped body text; optionally styled as a blue task link. Unlike
// Aero::bodyLabel it wraps, the pages' paragraphs running to several lines
inline QLabel *wrappedLabel(const QString &text, bool link = false)
{
    auto *label = new QLabel(text);
    Aero::setPointSize(label, 9);
    label->setWordWrap(true);
    // Align via the label itself, not an addWidget alignment flag: passing an
    // alignment flag to QGridLayout::addWidget suppresses height-for-width, so
    // a word-wrapped label would be clipped to a single line of height
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    if (link) {
        label->setCursor(Qt::PointingHandCursor);
        label->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }"
            "QLabel:hover { color: %2; }")
                .arg(QLatin1String(Aero::Palette::LinkText),
                     QLatin1String(Aero::Palette::LinkHover)));
    } else {
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(QLatin1String(Aero::Palette::Text)));
    }
    return label;
}

// Bold or regular, leaving the rest of the widget's font as it was, the way
// Aero::setPointSize leaves everything but the size
inline void setBold(QWidget *widget, bool bold)
{
    QFont font = widget->font();
    font.setBold(bold);
    widget->setFont(font);
}

// A task link, the shared Aero::LinkLabel at the pages' 9pt and underlined
// while hovered
inline Aero::LinkLabel *link(const QString &text)
{
    auto *label = new Aero::LinkLabel(text);
    Aero::setPointSize(label, 9);
    label->setUnderlineOnHover(true);
    return label;
}

// Separators and page scaffold

// The full-width separator under a page's title/instruction block
inline QFrame *hSeparator(const char *color = Aero::Palette::PaneRule)
{
    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(color)));
    return sep;
}

// Root layout of a sidebar detail page: [ sidebar | white content column ].
// Returns the content column's layout. With fixedWidth > 0 the column keeps
// that width and the leftover space stays white (Firewall/Fonts/Power style);
// otherwise the column stretches to fill the window (Programs and Features)
//
// The backgrounds are set with ID-scoped rules, NOT declaration-only sheets:
// a declaration-only stylesheet ("background: #FFFFFF;") acts as `* { ... }`,
// matching every descendant, and any widget matched by any stylesheet rule
// is rendered by Qt's stylesheet engine instead of the platform style, which
// is what used to turn all the scroll bars (and buttons) non-native
inline QVBoxLayout *pageScaffold(QWidget *page, QScrollArea *sidebar,
                                 int bottomMargin = 20, int fixedWidth = -1,
                                 int topMargin = 18)
{
    page->setObjectName("win7Page");
    page->setStyleSheet(
        Aero::panelSheet(QStringLiteral("win7Page"),
                         Aero::Palette::Surface));
    auto *root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(sidebar);

    auto *content = new QWidget;
    content->setObjectName("win7PageContent");
    content->setStyleSheet(
        Aero::panelSheet(QStringLiteral("win7PageContent"),
                         Aero::Palette::Surface));
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
// right, optionally ending in a trailing widget (e.g. an expander chevron)
inline QHBoxLayout *sectionHeading(const QString &text,
                                   QWidget *trailing = nullptr,
                                   QLabel **labelOut = nullptr,
                                   const char *color = "#555555")
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    auto *heading = Aero::label(text, 9, color);
    row->addWidget(heading, 0, Qt::AlignVCenter);
    row->addWidget(Aero::hairline(), 1, Qt::AlignVCenter);
    if (trailing)
        row->addWidget(trailing, 0, Qt::AlignVCenter);
    if (labelOut)
        *labelOut = heading;
    return row;
}

// The Aero arrow

// A flat text button ending in the Aero arrow that opens its menu on click,
// the Win7 "View by: Category" dropdown style
//
// Not Aero::MenuButton, which is the command bar's pill: this one is a link
class LinkMenuButton : public QToolButton {
public:
    explicit LinkMenuButton(const QString &text, QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setText(text);
        setPopupMode(QToolButton::InstantPopup);
        setToolButtonStyle(Qt::ToolButtonTextOnly);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet("QToolButton { border: none; background: transparent; }");
        Aero::setPointSize(this, 9);
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
        QPainter painter(this);
        QColor color;
        if (underMouse())
            color = m_hover;
        else
            color = m_normal;
        painter.setFont(font());
        painter.setPen(color);
        const int textWidth = QFontMetrics(font()).horizontalAdvance(text());
        painter.drawText(QRect(0, 0, textWidth, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text());

        const QPixmap arrow = Aero::arrowPixmap(Qt::DownArrow, color, kArrowWidth);
        const QSizeF as = arrow.deviceIndependentSize();
        painter.drawPixmap(QPointF(textWidth + kSpacing,
                             (height() - as.height()) / 2.0 + 1), arrow);
    }

private:
    static constexpr int kArrowWidth = 6;
    static constexpr int kSpacing = 5;
    QColor m_normal{0x00, 0x33, 0x99};
    QColor m_hover{0xFF, 0x66, 0x00};
};

// Click plumbing

// A bare widget that runs a callback when clicked, used for collapsible
// section headers so the whole strip toggles the body below. No signals are
// needed, so this stays a plain QWidget (no Q_OBJECT / moc)
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

// The ribbon list chrome
// The command bar / list / status panel design of the Uninstall a Program
// page, shared by every page that shows a list under an Explorer-style bar

// Explorer's own command bar, which Win7 also puts above these lists. The
// caller fills the returned layout ("Organize" dropdown, task links, trailing
// icons)
inline QFrame *commandBar(QHBoxLayout **layoutOut = nullptr)
{
    Aero::Strip *bar = Aero::commandBar(Explorer::Art::CommandBar);
    if (layoutOut)
        *layoutOut = bar->row();
    return bar;
}

// The command bar's right-hand buttons, Explorer's Change your view and Help
inline void addCommandBarIcons(QHBoxLayout *h)
{
    const QIcon viewIcon =
        Aero::themeIcon({"view-list-details", "view-list-text", "view-choose"});
    h->addWidget(Aero::commandBarButton(viewIcon, QStringLiteral("Change your view")));

    const QIcon helpIcon =
        Aero::themeIcon({"help-contents", "help-browser", "system-help"});
    h->addWidget(Aero::commandBarButton(helpIcon, QStringLiteral("Help")));
}

// Explorer's details list look for the pages' QTreeWidgets, but one item at a
// time and unsorted, each page deciding for itself whether its list sorts,
// and with groups the user can fold
inline void configureListTree(QTreeWidget *tree)
{
    Aero::configureListTree(tree);
    tree->setSortingEnabled(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setItemsExpandable(true);
}

// The centred single-row "Searching for ..." placeholder shown while a worker
// thread gathers the list
inline void addTreePlaceholder(QTreeWidget *tree, const QString &text)
{
    auto *row = new QTreeWidgetItem(tree);
    row->setFirstColumnSpanned(true);
    row->setTextAlignment(0, Qt::AlignHCenter);
    row->setText(0, text);
    row->setFlags(Qt::ItemIsEnabled);
}

// Apply the 9pt list font to every column of an item
inline void setItemPointSize(QTreeWidgetItem *item, int pt = 9)
{
    for (int c = 0; c < item->columnCount(); ++c) {
        QFont font = item->font(c);
        font.setPointSize(pt);
        item->setFont(c, font);
    }
}

// Worker-thread gathering

// Run `job` on a worker thread and hand its result to `done` on the GUI
// thread. `owner` scopes the delivery: if it is destroyed first, the result is
// dropped. The pages use this to navigate first and populate second, so a
// slow pacman/sysfs query never blocks the page from appearing
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

} // namespace ControlPanel
