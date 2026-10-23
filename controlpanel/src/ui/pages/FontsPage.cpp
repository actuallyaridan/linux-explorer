#include "FontsPage.h"
#include "Commands.h"
#include "LinkLabel.h"
#include "Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QFontMetrics>
#include <QPushButton>
#include <QProcess>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QSet>
#include <algorithm>

namespace {

// A single Windows 7 font tile: a stacked "document" icon carrying an "Abg"
// preview drawn in the family's own typeface, with the family name centred
// underneath. Selection/hover paint a rounded highlight behind the whole tile.
//
// Kept a plain QWidget (no Q_OBJECT/signals): FontsPage drives selection and
// activation through an installed event filter, exactly like the old rows, so
// no moc is needed for this in-.cpp helper.
class FontTile : public QWidget {
public:
    explicit FontTile(const QString &family, QWidget *parent = nullptr)
        : QWidget(parent), m_family(family)
    {
        setFixedSize(88, 116);
        setCursor(Qt::PointingHandCursor);

        m_previewFont = QFont(family);
        m_previewFont.setPointSize(13);

        // Latin faces preview as "Abg" (as in Windows 7); a face with no Latin
        // 'A' has nothing to say with that sample, so fall back to its name.
        QFontMetrics fm(m_previewFont);
        m_sample = fm.inFont(QChar(u'A')) ? QStringLiteral("Abg") : m_family;
    }

    void setSelected(bool sel)
    {
        if (m_selected != sel) { m_selected = sel; update(); }
    }

protected:
    void enterEvent(QEnterEvent *e) override { m_hover = true; update(); QWidget::enterEvent(e); }
    void leaveEvent(QEvent *e) override      { m_hover = false; update(); QWidget::leaveEvent(e); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        // Whole-tile highlight for selection/hover.
        if (m_selected || m_hover) {
            const QColor fill  = m_selected ? QColor(0xCC, 0xE8, 0xFF) : QColor(0xE9, 0xF4, 0xFE);
            const QColor edge  = m_selected ? QColor(0x99, 0xC7, 0xF0) : QColor(0xB8, 0xDD, 0xF6);
            const QRectF hi = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
            p.setPen(edge);
            p.setBrush(fill);
            p.drawRoundedRect(hi, 3, 3);
        }

        // Document icon: a front sheet with a folded top-right corner, plus a
        // second sheet peeking behind it for the "stack of faces" look.
        const qreal iw = 44, ih = 54, fold = 11;
        const qreal ix = (width() - iw - 3) / 2.0, iy = 9;
        const QRectF front(ix, iy, iw, ih);
        const QRectF back = front.translated(3, 3);

        p.setPen(QColor(0xC4, 0xC8, 0xD0));
        p.setBrush(Qt::white);
        p.drawPath(pagePath(back, fold));

        const QPainterPath frontPath = pagePath(front, fold);
        p.setPen(QColor(0xA9, 0xAE, 0xB8));
        p.setBrush(Qt::white);
        p.drawPath(frontPath);

        // The dog-eared corner.
        QPainterPath ear;
        ear.moveTo(front.right() - fold, front.top());
        ear.lineTo(front.right() - fold, front.top() + fold);
        ear.lineTo(front.right(),        front.top() + fold);
        ear.closeSubpath();
        p.setPen(QColor(0xC4, 0xC8, 0xD0));
        p.setBrush(QColor(0xD9, 0xDD, 0xE5));
        p.drawPath(ear);

        // The preview sample, clipped to the sheet so wide glyphs don't spill.
        p.save();
        p.setClipPath(frontPath);
        p.setFont(m_previewFont);
        p.setPen(Qt::black);
        p.drawText(front.adjusted(2, fold, -2, -2), Qt::AlignCenter, m_sample);
        p.restore();

        // The family name, centred and word-wrapped to (at most) two lines.
        QFont nameFont = font();
        nameFont.setPointSize(8);
        p.setFont(nameFont);
        p.setPen(QColor(0x1E, 0x1E, 0x1E));
        p.drawText(QRect(2, int(iy + ih + 6), width() - 4, 38),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_family);
    }

private:
    // A page outline with a folded top-right corner.
    static QPainterPath pagePath(const QRectF &r, qreal fold)
    {
        QPainterPath path;
        path.moveTo(r.left(),         r.top());
        path.lineTo(r.right() - fold, r.top());
        path.lineTo(r.right(),        r.top() + fold);
        path.lineTo(r.right(),        r.bottom());
        path.lineTo(r.left(),         r.bottom());
        path.closeSubpath();
        return path;
    }

    QString m_family;
    QString m_sample;
    QFont   m_previewFont;
    bool    m_selected = false;
    bool    m_hover    = false;
};

} // namespace

// Data gathering
QList<FontsPage::Family> FontsPage::gatherFamilies()
{
    QList<Family> families;

    QProcess proc;
    proc.start(QStringLiteral("fc-list"),
               { QStringLiteral("--format=%{family[0]}\t%{file}\n") });
    if (!proc.waitForFinished(4000))
        return families;

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    const QList<QStringView> lines = QStringView(out).split(u'\n', Qt::SkipEmptyParts);

    // Keep the first file seen for each family; fc-list lists a family once per
    // face, so de-duplicating by name yields one entry per installed family.
    QSet<QString> seen;
    for (const QStringView &line : lines) {
        const int tab = line.indexOf(u'\t');
        if (tab < 0)
            continue;
        const QString name = line.left(tab).trimmed().toString();
        const QString file = line.mid(tab + 1).trimmed().toString();
        if (name.isEmpty() || seen.contains(name))
            continue;
        seen.insert(name);
        families << Family{ name, file };
    }

    std::sort(families.begin(), families.end(),
              [](const Family &a, const Family &b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    return families;
}

// Sidebar
QList<SidebarLink> FontsPage::sidebarLinks()
{
    return {
        Nav::command("Font settings", kcm("kcm_fonts")),
        Nav::plain("Get more font information online"),
        Nav::command("Adjust ClearType text", kcm("kcm_fonts")),
        Nav::plain("Find a character"),
        Nav::plain("Change font size"),
    };
}

QList<SidebarLink> FontsPage::sidebarSeeAlso()
{
    return {
        Nav::plain("Text Services and Input Language"),
        Nav::to("Personalization", PageId::Personalization),
    };
}

// Page
FontsPage::FontsPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    m_families = gatherFamilies();

    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/0,
                                        /*fixedWidth=*/700);
    // The list frame (command bar + font grid + status strip) is full-bleed, so
    // the column itself carries no horizontal padding; only the header text is
    // indented, via its own inset sub-layout.
    contentV->setContentsMargins(0, 18, 0, 0);

    // Page title (the only indented block).
    auto *headerV = new QVBoxLayout;
    headerV->setContentsMargins(28, 0, 28, 0);
    headerV->setSpacing(0);
    auto *pageTitle = Win7::pageTitle(
        "Preview, delete, or show and hide the fonts installed on your computer");
    pageTitle->setWordWrap(true);
    headerV->addWidget(pageTitle);
    contentV->addLayout(headerV);
    contentV->addSpacing(10);

    // "Organize" command bar spanning the content width.
    QHBoxLayout *cmdH = nullptr;
    auto *cmdBar = Win7::commandBar(&cmdH);
    cmdH->addWidget(Win7::dropdownLabel("Organize"));
    cmdH->addStretch(1);
    Win7::addCommandBarIcons(cmdH);
    contentV->addWidget(cmdBar);

    // The grid of font tiles between the command bar and the status bar.
    auto *listFrame = new QFrame;
    listFrame->setObjectName("fontList");
    listFrame->setStyleSheet("#fontList { background: #FFFFFF; border: none; }");
    auto *grid = new QGridLayout(listFrame);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(8);

    const int kColumns = 7;
    for (int i = 0; i < m_families.size(); ++i) {
        auto *tile = new FontTile(m_families[i].name);
        tile->installEventFilter(this);
        m_rowToIndex.insert(tile, i);
        m_rows << tile;
        grid->addWidget(tile, i / kColumns, i % kColumns, Qt::AlignLeft | Qt::AlignTop);
    }
    grid->setColumnStretch(kColumns, 1);
    grid->setRowStretch(m_families.size() / kColumns + 1, 1);

    if (m_families.isEmpty()) {
        auto *empty = new QLabel("No fonts were found.");
        QFont f = empty->font();
        f.setPointSize(9);
        empty->setFont(f);
        empty->setStyleSheet("color: #555555; background: transparent; padding: 14px;");
        grid->addWidget(empty, 0, 0);
    }

    contentV->addWidget(listFrame);

    // Status bar with the item count, as in the Windows 7 Fonts folder.
    QHBoxLayout *statusH = nullptr;
    auto *statusBar = Win7::statusPanel(32, &statusH);

    auto *folderIcon = new QLabel(QStringLiteral("A"));
    {
        QFont f("Serif");
        f.setPointSize(15);
        f.setItalic(true);
        f.setBold(true);
        folderIcon->setFont(f);
    }
    folderIcon->setStyleSheet("color: #3A6EA5; background: transparent;");
    statusH->addWidget(folderIcon, 0, Qt::AlignVCenter);

    statusH->addWidget(Win7::label(QStringLiteral("%1 items").arg(m_families.size()),
                                   9, "#333333"),
                       0, Qt::AlignVCenter);
    statusH->addStretch(1);

    contentV->addWidget(statusBar);
    contentV->addSpacing(20);
}

void FontsPage::selectRow(int index)
{
    if (index < 0 || index >= m_rows.size())
        return;
    m_selected = index;
    for (int i = 0; i < m_rows.size(); ++i)
        static_cast<FontTile *>(m_rows[i])->setSelected(i == index);
}

void FontsPage::openSelected()
{
    if (m_selected < 0 || m_selected >= m_families.size())
        return;
    const QString file = m_families[m_selected].file;
    if (file.isEmpty())
        return;
    launchDetached(this, { "kfontview", file });
}

bool FontsPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto it = m_rowToIndex.constFind(watched);
        if (it != m_rowToIndex.constEnd()) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                selectRow(it.value());
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto it = m_rowToIndex.constFind(watched);
        if (it != m_rowToIndex.constEnd()) {
            selectRow(it.value());
            openSelected();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
