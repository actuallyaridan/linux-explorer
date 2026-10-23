#include "PersonalizationPage.h"
#include "Commands.h"
#include "LinkLabel.h"
#include "IconHelper.h"
#include "Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QProcess>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSet>
#include <QMouseEvent>
#include <QEvent>
#include <algorithm>

// Data gathering
// Parse a "r,g,b" (optionally with a trailing alpha) triple into a QColor.
static QColor parseRgb(const QString &value, const QColor &fallback)
{
    const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return fallback;
    return QColor(parts[0].trimmed().toInt(),
                  parts[1].trimmed().toInt(),
                  parts[2].trimmed().toInt());
}

QList<PersonalizationPage::Scheme> PersonalizationPage::gatherSchemes()
{
    QList<Scheme> schemes;
    QSet<QString> seenIds;

    // Search every standard data dir's color-schemes folder; a user scheme in
    // ~/.local/share shadows a system one of the same id.
    QStringList dirs;
    for (const QString &base :
         QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        dirs << base + QStringLiteral("/color-schemes");

    for (const QString &dirPath : dirs) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList({QStringLiteral("*.colors")},
                                                QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const QString id = QFileInfo(file).completeBaseName();
            if (seenIds.contains(id))
                continue;
            seenIds.insert(id);

            QSettings ini(dir.filePath(file), QSettings::IniFormat);
            Scheme s;
            s.id = id;
            s.name = ini.value(QStringLiteral("General/Name"), id).toString();

            s.window = parseRgb(
                ini.value(QStringLiteral("Colors:Window/BackgroundNormal"))
                    .toString(), QColor("#F0F0F0"));
            s.view = parseRgb(
                ini.value(QStringLiteral("Colors:View/BackgroundNormal"))
                    .toString(), QColor("#FFFFFF"));
            s.accent = parseRgb(
                ini.value(QStringLiteral("Colors:Selection/BackgroundNormal"))
                    .toString(), QColor("#3399FF"));
            s.titlebar = parseRgb(
                ini.value(QStringLiteral("WM/activeBackground")).toString(),
                s.window);
            s.text = parseRgb(
                ini.value(QStringLiteral("Colors:Window/ForegroundNormal"))
                    .toString(), QColor("#202020"));

            schemes << s;
        }
    }

    std::sort(schemes.begin(), schemes.end(),
              [](const Scheme &a, const Scheme &b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    return schemes;
}

QString PersonalizationPage::currentSchemeId()
{
    // The active scheme id lives in kdeglobals [General] ColorScheme.
    QProcess proc;
    proc.start(QStringLiteral("kreadconfig6"),
               { QStringLiteral("--group"), QStringLiteral("General"),
                 QStringLiteral("--key"), QStringLiteral("ColorScheme") });
    if (proc.waitForFinished(2000)) {
        const QString id = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

// Paint a miniature window preview from the scheme's own colours.
QPixmap PersonalizationPage::swatchPixmap(const Scheme &s) const
{
    const int w = 116, h = 78;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // Window body.
    QRectF body(0.5, 0.5, w - 1, h - 1);
    p.setPen(QPen(QColor("#7F7F7F"), 1));
    p.setBrush(s.window);
    p.drawRoundedRect(body, 3, 3);

    // Title bar band.
    QRectF title(1, 1, w - 2, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(s.titlebar);
    QPainterPath titlePath;
    titlePath.addRoundedRect(title, 3, 3);
    // Square off the bottom of the titlebar so only the top corners are round.
    titlePath.addRect(QRectF(1, 9, w - 2, 8));
    p.drawPath(titlePath.simplified());

    // Three faux window buttons.
    p.setBrush(QColor(255, 255, 255, 150));
    for (int i = 0; i < 3; ++i)
        p.drawEllipse(QPointF(w - 10 - i * 8, 9), 2.2, 2.2);

    // Content view with a highlighted "selected" row in the accent colour.
    QRectF view(9, 24, w - 18, h - 33);
    p.setPen(QPen(QColor(0, 0, 0, 40), 1));
    p.setBrush(s.view);
    p.drawRect(view);

    p.setPen(Qt::NoPen);
    p.setBrush(s.accent);
    p.drawRect(QRectF(view.left() + 4, view.top() + 6, view.width() - 8, 9));

    // A couple of faux text lines in the foreground colour.
    QColor line = s.text;
    line.setAlpha(120);
    p.setBrush(line);
    p.drawRect(QRectF(view.left() + 4, view.top() + 21, view.width() - 24, 4));
    p.drawRect(QRectF(view.left() + 4, view.top() + 29, view.width() - 36, 4));

    return pm;
}

// Sidebar
QList<SidebarLink> PersonalizationPage::sidebarLinks()
{
    return {
        Nav::command("Change desktop icons", kcm("kcm_icons")),
        Nav::command("Change mouse pointers", kcm("kcm_cursortheme")),
        Nav::command("Change your account picture", kcm("kcm_users")),
    };
}

QList<SidebarLink> PersonalizationPage::sidebarSeeAlso()
{
    return {
        Nav::command("Display", kcm("kcm_kscreen")),
        Nav::plain("Taskbar and Start Menu"),
        Nav::to("Ease of Access Center", PageId::EaseOfAccess),
    };
}

// Page
PersonalizationPage::PersonalizationPage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    m_schemes   = gatherSchemes();
    m_currentId = currentSchemeId();

    // Windows 7 lays the content out at a fixed width and leaves the rest of
    // the window blank on the right rather than stretching to fill it.
    auto *contentV = Win7::pageScaffold(this, sidebar, /*bottomMargin=*/20,
                                        /*fixedWidth=*/700);

    // Page title.
    contentV->addWidget(
        Win7::pageTitle("Change the visuals and sounds on your computer"));
    contentV->addSpacing(6);

    auto *intro = Win7::label(
        "Click a theme to change the desktop background, window color, sounds, "
        "and screen saver all at once.", 9, "#1A1A1A");
    intro->setWordWrap(true);
    contentV->addWidget(intro);
    contentV->addSpacing(14);

    // Section heading with a hairline rule.
    auto addHeading = [&](const QString &text) {
        contentV->addLayout(
            Win7::sectionHeading(text, nullptr, nullptr, "#000000"));
        contentV->addSpacing(10);
    };

    addHeading(QStringLiteral("Aero Themes (%1)").arg(m_schemes.size()));

    // Swatch grid: four themes per row.
    auto *grid = new QGridLayout;
    grid->setContentsMargins(6, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(14);

    const int columns = 4;
    for (int i = 0; i < m_schemes.size(); ++i) {
        const Scheme &s = m_schemes[i];

        auto *cell = new QFrame;
        cell->setObjectName("themeCell");
        cell->setCursor(Qt::PointingHandCursor);
        auto *cellV = new QVBoxLayout(cell);
        cellV->setContentsMargins(4, 4, 4, 4);
        cellV->setSpacing(3);

        auto *swatch = new QLabel;
        swatch->setPixmap(swatchPixmap(s));
        swatch->setFixedSize(116, 78);
        swatch->setStyleSheet("background: transparent;");
        cellV->addWidget(swatch, 0, Qt::AlignHCenter);

        auto *name = new QLabel(s.name);
        {
            QFont f = name->font();
            f.setPointSize(8);
            name->setFont(f);
        }
        name->setAlignment(Qt::AlignHCenter);
        name->setWordWrap(true);
        name->setFixedWidth(120);
        name->setStyleSheet("color: #000000; background: transparent;");
        cellV->addWidget(name, 0, Qt::AlignHCenter);

        cell->installEventFilter(this);
        m_frameToIndex.insert(cell, i);
        m_frames << cell;

        grid->addWidget(cell, i / columns, i % columns, Qt::AlignTop);
    }
    contentV->addLayout(grid);
    contentV->addSpacing(20);

    // Bottom action row: the four Windows Personalization shortcuts.
    auto *actionsRow = new QHBoxLayout;
    actionsRow->setContentsMargins(6, 0, 0, 0);
    actionsRow->setSpacing(24);

    auto addAction = [&](const QString &iconName, const QString &text,
                         const QStringList &cmd) {
        auto *col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(4);

        auto *icon = new QLabel;
        icon->setFixedSize(32, 32);
        icon->setPixmap(themeIcon({iconName.toUtf8().constData(),
                                    "preferences-desktop"}).pixmap(32, 32));
        icon->setStyleSheet("background: transparent;");
        col->addWidget(icon, 0, Qt::AlignHCenter);

        auto *link = new LinkLabel(text);
        link->setAlignment(Qt::AlignHCenter);
        QObject::connect(link, &LinkLabel::clicked, this, [this, cmd]() {
            launchDetached(this, cmd);
        });
        col->addWidget(link, 0, Qt::AlignHCenter);

        actionsRow->addLayout(col);
    };

    addAction("preferences-desktop-wallpaper", "Desktop\nBackground",
              { "kcmshell6", "kcm_wallpaper" });
    addAction("preferences-desktop-color", "Window\nColor",
              { "kcmshell6", "kcm_colors" });
    addAction("preferences-desktop-sound", "Sounds",
              { "kcmshell6", "kcm_soundtheme" });
    addAction("preferences-desktop-screensaver", "Screen\nSaver",
              { "kcmshell6", "kcm_screenlocker" });
    actionsRow->addStretch(1);

    contentV->addLayout(actionsRow);
    contentV->addStretch(1);

    refreshHighlight();
}

void PersonalizationPage::applyScheme(int index)
{
    if (index < 0 || index >= m_schemes.size())
        return;
    const Scheme &s = m_schemes[index];
    // plasma-apply-colorscheme writes the user's own config; no polkit needed.
    QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"),
                            { s.id });
    m_currentId = s.id;
    refreshHighlight();
}

void PersonalizationPage::refreshHighlight()
{
    for (auto it = m_frameToIndex.constBegin();
         it != m_frameToIndex.constEnd(); ++it) {
        auto *frame = qobject_cast<QFrame *>(const_cast<QObject *>(it.key()));
        if (!frame)
            continue;
        const bool cur = (m_schemes[it.value()].id == m_currentId);
        frame->setStyleSheet(cur
            ? "#themeCell { background: #DCEBFB; border: 1px solid #7DA2CE;"
              " border-radius: 3px; }"
            : "#themeCell { background: transparent; border: 1px solid transparent; }");
    }
}

bool PersonalizationPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonDblClick) {
        auto it = m_frameToIndex.constFind(watched);
        if (it != m_frameToIndex.constEnd()) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                applyScheme(it.value());
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
