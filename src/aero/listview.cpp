#include "listview.h"

#include "palette.h"
#include "text.h"

#include <QAbstractItemView>
#include <QFontMetrics>
#include <QFrame>
#include <QHeaderView>
#include <QTreeView>

namespace Aero {

void configureListTree(QTreeView *tree)
{
    tree->setRootIsDecorated(false);
    tree->setItemsExpandable(false);
    tree->setIndentation(0);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Qt's default would turn a double click into an inline rename instead of
    // opening the item, and renaming is left to the application on F2
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setAlternatingRowColors(false);
    tree->setFrameShape(QFrame::NoFrame);
    // Before sorting is enabled, which immediately re sorts by the indicator,
    // and the indicator starts out descending
    tree->header()->setSortIndicator(0, Qt::AscendingOrder);
    tree->setSortingEnabled(true);
    tree->header()->setDefaultAlignment(Qt::AlignLeft);
    tree->header()->setStretchLastSection(true);
    setPointSize(tree->header(), 9);
    // Win7 headers have no dividers and no rule beneath
    tree->header()->setStyleSheet(
        QStringLiteral("QHeaderView::section {"
                       "  background: %1;"
                       "  border: none;"
                       "  padding: 4px;"
                       "  font-size: 9pt;"
                       "}").arg(QLatin1String(Palette::Surface)));
    // Pinned rather than left to the size hint, which is cached before the app
    // wide stylesheet polishes the widget and comes out twice as tall
    const QFontMetrics fm(tree->header()->font());
    tree->header()->setFixedHeight(fm.height() + 2 * 4 + 1);
}

} // namespace Aero
