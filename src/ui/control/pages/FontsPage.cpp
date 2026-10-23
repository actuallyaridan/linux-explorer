#include "ui/control/pages/FontsPage.h"
#include "DetailsPane.h"
#include "ui/control/Commands.h"
#include "ui/control/Commands.h"
#include "ui/control/Win7Ui.h"
#include "ui/control/Win7Ui.h"

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
#include "aero/palette.h"

namespace ControlPanel {

namespace {

// A single Windows 7 font tile: a stacked "document" icon carrying an "Abg"
// preview drawn in the family's own typeface, with the family name centred
// underneath. Selection/hover paint a rounded highlight behind the whole tile
//
// Kept a plain QWidget (no Q_OBJECT/signals): FontsPage drives selection and
// activation through an installed event filter, exactly like the old rows, so
// no moc is needed for this in-.cpp helper
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
        // 'A' has nothing to say with that sample, so fall back to its name
        QFontMetrics fm(m_previewFont);
        if (fm.inFont(QChar(u'A')))
            m_sample = QStringLiteral("Abg");
        else
            m_sample = m_family;
    }

    void setSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        update();
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        // Whole-tile highlight for selection/hover
        if (m_selected || m_hover) {
            // Selection is the stronger blue of the two
            QColor fill(0xE9, 0xF4, 0xFE);
            QColor edge(0xB8, 0xDD, 0xF6);
            if (m_selected) {
                fill = QColor(0xCC, 0xE8, 0xFF);
                edge = QColor(0x99, 0xC7, 0xF0);
            }
            const QRectF hi = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
            painter.setPen(edge);
            painter.setBrush(fill);
            painter.drawRoundedRect(hi, 3, 3);
        }

        // Document icon: a front sheet with a folded top-right corner, plus a
        // second sheet peeking behind it for the "stack of faces" look
        const qreal iw = 44, ih = 54, fold = 11;
        const qreal ix = (width() - iw - 3) / 2.0, iy = 9;
        const QRectF front(ix, iy, iw, ih);
        const QRectF back = front.translated(3, 3);

        painter.setPen(QColor(0xC4, 0xC8, 0xD0));
        painter.setBrush(Qt::white);
        painter.drawPath(pagePath(back, fold));

        const QPainterPath frontPath = pagePath(front, fold);
        painter.setPen(QColor(0xA9, 0xAE, 0xB8));
        painter.setBrush(Qt::white);
        painter.drawPath(frontPath);

        // The dog-eared corner
        QPainterPath ear;
        ear.moveTo(front.right() - fold, front.top());
        ear.lineTo(front.right() - fold, front.top() + fold);
        ear.lineTo(front.right(),        front.top() + fold);
        ear.closeSubpath();
        painter.setPen(QColor(0xC4, 0xC8, 0xD0));
        painter.setBrush(QColor(0xD9, 0xDD, 0xE5));
        painter.drawPath(ear);

        // The preview sample, clipped to the sheet so wide glyphs don't spill
        painter.save();
        painter.setClipPath(frontPath);
        painter.setFont(m_previewFont);
        painter.setPen(Qt::black);
        painter.drawText(front.adjusted(2, fold, -2, -2), Qt::AlignCenter, m_sample);
        painter.restore();

        // The family name, centred and word-wrapped to (at most) two lines
        QFont nameFont = font();
        nameFont.setPointSize(8);
        painter.setFont(nameFont);
        painter.setPen(QColor(0x1E, 0x1E, 0x1E));
        painter.drawText(QRect(2, int(iy + ih + 6), width() - 4, 38),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_family);
    }

private:
    // A page outline with a folded top-right corner
    static QPainterPath pagePath(const QRectF &page, qreal fold)
    {
        QPainterPath path;
        path.moveTo(page.left(),         page.top());
        path.lineTo(page.right() - fold, page.top());
        path.lineTo(page.right(),        page.top() + fold);
        path.lineTo(page.right(),        page.bottom());
        path.lineTo(page.left(),         page.bottom());
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
    // face, so de-duplicating by name yields one entry per installed family
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
    // indented, via its own inset sub-layout
    contentV->setContentsMargins(0, 18, 0, 0);

    // Page title (the only indented block)
    auto *headerV = new QVBoxLayout;
    headerV->setContentsMargins(28, 0, 28, 0);
    headerV->setSpacing(0);
    auto *pageTitle = Win7::pageTitle(
        "Preview, delete, or show and hide the fonts installed on your computer");
    pageTitle->setWordWrap(true);
    headerV->addWidget(pageTitle);
    contentV->addLayout(headerV);
    contentV->addSpacing(10);

    // "Organize" command bar spanning the content width
    QHBoxLayout *cmdH = nullptr;
    auto *cmdBar = Win7::commandBar(&cmdH);
    cmdH->addWidget(new Aero::MenuButton("Organize"));
    cmdH->addStretch(1);
    Win7::addCommandBarIcons(cmdH);
    contentV->addWidget(cmdBar);

    // The grid of font tiles between the command bar and the status bar
    auto *listFrame = new QFrame;
    listFrame->setObjectName("fontList");
    listFrame->setStyleSheet(QStringLiteral("#fontList { background: %1; border: none; }")
        .arg(QLatin1String(Aero::Palette::Surface)));
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
        Aero::setPointSize(empty, 9);
        empty->setStyleSheet("color: #555555; background: transparent; padding: 14px;");
        grid->addWidget(empty, 0, 0);
    }

    contentV->addWidget(listFrame);

    // Explorer's details pane with the item count, as in the Windows 7 Fonts
    // folder
    auto *details = new DetailsPane;
    const QIcon fontsIcon = Aero::themeIcon({"preferences-desktop-font", "folder"});
    details->showContent(fontsIcon, QStringLiteral("%1 items").arg(m_families.size()));
    contentV->addWidget(details);
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

} // namespace ControlPanel
