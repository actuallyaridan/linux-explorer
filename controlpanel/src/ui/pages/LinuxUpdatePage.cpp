#include "LinuxUpdatePage.h"
#include "IconHelper.h"
#include "Win7Ui.h"
#include "Branding.h"

#include <QScrollArea>
#include <QMessageBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QFont>
#include <QEvent>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QProcess>
#include <QDateTime>
#include <QLocale>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>

QList<SidebarLink> LinuxUpdatePage::sidebarLinks()
{
    // "Check for updates" is handled specially by MainWindow (it triggers this
    // page's own refresh rather than navigating away), so it carries no target.
    return {
        Nav::plain("Check for updates"),
        Nav::plain("Change settings"),
        Nav::plain("View update history"),
        Nav::plain("Restore hidden updates"),
        Nav::plain("Updates: frequently asked questions"),
    };
}

QList<SidebarLink> LinuxUpdatePage::sidebarSeeAlso()
{
    return { Nav::to("Installed Updates", PageId::InstalledUpdates) };
}

LinuxUpdatePage::LinuxUpdatePage(QScrollArea *sidebar, QWidget *parent)
    : QWidget(parent)
{
    // ID-scoped backgrounds throughout: a declaration-only stylesheet acts as
    // `* { ... }`, matching every descendant and forcing them (scroll bars
    // included) into stylesheet rendering instead of the platform style.
    setObjectName("luPage");
    setStyleSheet("#luPage { background: #FFFFFF; }");
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // The page hosts two views in a stack: the update status view (index 0,
    // built below) and an on-demand "Select updates to install" view.
    m_stack = new QStackedWidget;
    root->addWidget(m_stack);

    auto *statusView = new QWidget;
    statusView->setObjectName("luStatusView");
    statusView->setStyleSheet("#luStatusView { background: #FFFFFF; }");
    auto *statusRoot = new QHBoxLayout(statusView);
    statusRoot->setContentsMargins(0, 0, 0, 0);
    statusRoot->setSpacing(0);
    statusRoot->addWidget(sidebar);

    auto *contentWrap = new QWidget;
    contentWrap->setObjectName("luContentWrap");
    contentWrap->setStyleSheet("#luContentWrap { background: #FFFFFF; }");
    auto *contentV = new QVBoxLayout(contentWrap);
    contentV->setContentsMargins(28, 20, 28, 20);
    contentV->setSpacing(0);

    // Page title
    auto *pageTitle = new QLabel(Branding::brand("Linux Update"));
    {
        QFont f = pageTitle->font();
        f.setPointSize(11);
        pageTitle->setFont(f);
    }
    pageTitle->setStyleSheet("color: #003399;");
    contentV->addWidget(pageTitle);
    contentV->addSpacing(16);

    // Status box
    m_statusBox = new QFrame;
    m_statusBox->setObjectName("statusBox");
    m_statusBox->setMaximumWidth(620);

    auto *statusH = new QHBoxLayout(m_statusBox);
    statusH->setContentsMargins(12, 12, 12, 12);
    statusH->setSpacing(12);

    m_iconLabel = new QLabel;
    m_iconLabel->setFixedSize(48, 48);
    m_iconLabel->setStyleSheet("background: transparent; border: none;");
    statusH->addWidget(m_iconLabel, 0, Qt::AlignTop);

    // Centre text column
    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(4);

    m_titleLabel = new QLabel;
    {
        QFont f = m_titleLabel->font();
        f.setPointSize(11);
        m_titleLabel->setFont(f);
    }
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_titleLabel->setStyleSheet("color: #000000; background: transparent; border: none;");
    textCol->addWidget(m_titleLabel);

    // Subtitle row: link text (50%) | separator | selected count (50%).
    m_subRowWidget = new QWidget;
    m_subRowWidget->setStyleSheet("background: transparent;");
    auto *subRow = new QHBoxLayout(m_subRowWidget);
    // A small top margin leaves just a little space above the subtitle; the
    // progress bar sits directly above it when an install/download is running.
    subRow->setContentsMargins(0, 2, 0, 0);
    subRow->setSpacing(14);

    // Hyperlink lines reuse the same styling and signal wiring. linkHovered
    // toggles the underline so links are plain until the pointer is over them.
    auto makeLinkLabel = [this]() {
        auto *lbl = new QLabel;
        QFont f = lbl->font();
        f.setPointSize(9);
        lbl->setFont(f);
        lbl->setWordWrap(true);
        lbl->setTextFormat(Qt::RichText);
        lbl->setOpenExternalLinks(false);
        lbl->setStyleSheet("color: #444444; background: transparent; border: none;");
        connect(lbl, &QLabel::linkActivated, this, &LinuxUpdatePage::openUpdateList);
        connect(lbl, &QLabel::linkHovered, this, [this, lbl](const QString &link) {
            setLinkLabel(lbl, lbl->property("linkText").toString(), !link.isEmpty());
        });
        return lbl;
    };

    m_subLabel  = makeLinkLabel();
    m_subLabel2 = makeLinkLabel();
    m_subLabel2->hide();

    // Stack the two hyperlink lines vertically, tightly (near single-spacing)
    // to match the reference layout.
    auto *linksCol = new QVBoxLayout;
    linksCol->setContentsMargins(0, 0, 0, 0);
    linksCol->setSpacing(2);
    linksCol->addWidget(m_subLabel);
    linksCol->addWidget(m_subLabel2);
    subRow->addLayout(linksCol, 1);

    m_subSep = new QFrame;
    m_subSep->setFixedWidth(1);
    m_subSep->setFixedHeight(34);
    m_subSep->setStyleSheet("QFrame { background: #C0C0C0; border: none; }");
    m_subSep->hide();
    subRow->addWidget(m_subSep, 0, Qt::AlignVCenter);

    m_selectedLabel = new QLabel;
    {
        QFont f = m_selectedLabel->font();
        f.setPointSize(9);
        m_selectedLabel->setFont(f);
    }
    m_selectedLabel->setWordWrap(true);
    m_selectedLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_selectedLabel->setStyleSheet("color: #333333; background: transparent; border: none; font-weight: bold;");
    m_selectedLabel->hide();
    subRow->addWidget(m_selectedLabel, 1);

    textCol->addWidget(m_subRowWidget);

    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(18);
    m_progressBar->setMaximumWidth(160);
    m_progressBar->setStyleSheet("QProgressBar::chunk { width: 80px; }");
    m_progressBar->hide();
    // Sit the bar between the title (index 0) and the subtitle row (index 1).
    textCol->insertWidget(1, m_progressBar);

    m_progressText = new QLabel;
    {
        QFont f = m_progressText->font();
        f.setPointSize(8);
        m_progressText->setFont(f);
    }
    m_progressText->setStyleSheet("color: #555555; background: transparent; border: none;");
    m_progressText->hide();
    textCol->addWidget(m_progressText);

    // Install button row, sits below the subtitle inside textCol, right-aligned.
    // Showing it naturally expands the status box height.
    m_rightWidget = new QWidget;
    m_rightWidget->setStyleSheet("background: transparent;");
    auto *btnRow = new QHBoxLayout(m_rightWidget);
    btnRow->setContentsMargins(0, 6, 0, 0);
    btnRow->setSpacing(0);
    btnRow->addStretch(1);

    m_installBtn = new QPushButton("Install updates");
    m_installBtn->setStyleSheet("QPushButton { padding-left: 12px; padding-right: 12px; }");
    connect(m_installBtn, &QPushButton::clicked, this, &LinuxUpdatePage::installUpdates);
    btnRow->addWidget(m_installBtn);

    m_rightWidget->hide();
    textCol->addWidget(m_rightWidget);

    statusH->addLayout(textCol, 1);

    contentV->addWidget(m_statusBox);
    contentV->addSpacing(20);

    // Info rows
    auto makeInfoRow = [&](const QString &key, QLabel **out) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);

        auto *left = new QLabel(key);
        {
            QFont f = left->font();
            f.setPointSize(9);
            left->setFont(f);
        }
        left->setFixedWidth(215);
        left->setStyleSheet("color: #444444;");
        row->addWidget(left);

        auto *right = new QLabel;
        {
            QFont f = right->font();
            f.setPointSize(9);
            right->setFont(f);
        }
        right->setTextFormat(Qt::RichText);
        right->setStyleSheet("color: #000000;");
        row->addWidget(right);
        row->addStretch(1);
        contentV->addLayout(row);
        contentV->addSpacing(3);
        if (out) *out = right;
    };

    makeInfoRow("Most recent check for updates:", &m_lastCheckVal);
    makeInfoRow("Updates were installed:", &m_lastInstallVal);

    QLabel *recvRight = nullptr;
    makeInfoRow("You receive updates:", &recvRight);
    recvRight->setText(
        Branding::brand("For Linux and other products from pacman and the AUR"));

    contentV->addSpacing(16);

    // FOSS hint box
    auto *fossBox = new QFrame;
    fossBox->setFrameShape(QFrame::StyledPanel);
    fossBox->setStyleSheet("QFrame { background: #F8F8FF; border: 1px solid #C8C8E0; }");
    fossBox->setMaximumWidth(620);
    {
        auto *h = new QHBoxLayout(fossBox);
        h->setContentsMargins(8, 10, 8, 10);
        auto *lbl = new QLabel(
            "Find out more about free software from the AUR.  "
            "<a href='#' style='color:#1F4E99;'>Click here for details.</a>");
        lbl->setTextFormat(Qt::RichText);
        lbl->setStyleSheet("border: none; background: transparent;");
        {
            QFont f = lbl->font();
            f.setPointSize(9);
            lbl->setFont(f);
        }
        h->addWidget(lbl);
    }
    contentV->addWidget(fossBox);
    contentV->addStretch(1);

    statusRoot->addWidget(contentWrap, 1);
    m_stack->addWidget(statusView);

    // Fade in
    auto *fadeEffect = new QGraphicsOpacityEffect(contentWrap);
    contentWrap->setGraphicsEffect(fadeEffect);
    fadeEffect->setOpacity(0.0);
    auto *anim = new QPropertyAnimation(fadeEffect, "opacity", this);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(260);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QTimer::singleShot(0, this, [anim]() { anim->start(); });

    loadState();
    updateInfoRows();
    setIdleState();
    // No auto-check, user must click "Check for updates" in the sidebar.
}

// Status box appearance
// Restyle the status box. An empty leftColor gives the plain white box shown
// while a check or download is running; otherwise a 20px stripe in the given
// colour marks the state (yellow for action needed, green for OK, red for error).
void LinuxUpdatePage::applyStatusBorder(const QString &leftColor, const QString &bg)
{
    const QString accent = leftColor.isEmpty()
        ? QString()
        : QString("  border-left: 20px solid %1;").arg(leftColor);
    m_statusBox->setStyleSheet(QString(
        "QFrame#statusBox {"
        "  background: %1;"
        "  border: 1px solid #CCCCCC;"
        "%2"
        "}"
        "QFrame#statusBox QLabel { background: transparent; border: none; }"
    ).arg(bg, accent));
}

// State setters
// Baseline for the status box: the subtitle row with a single hyperlink line
// visible and every optional widget hidden. Each state setter calls this first,
// then re-shows only the pieces it needs.
void LinuxUpdatePage::resetStatusWidgets()
{
    m_subRowWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    m_subRowWidget->show();
    m_subLabel->show();
    m_subLabel2->hide();
    m_subSep->hide();
    m_selectedLabel->hide();
    m_progressBar->hide();
    m_progressText->hide();
    m_rightWidget->hide();
}

void LinuxUpdatePage::setIdleState()
{
    applyStatusBorder("#D4A800");
    resetStatusWidgets();
    m_iconLabel->setPixmap(themeIcon({"security-medium", "view-refresh"}).pixmap(48, 48));
    m_titleLabel->setText("There may be updates available");
    m_subLabel->setText("Click “Check for updates” in the sidebar to search for available updates.");
}

void LinuxUpdatePage::setCheckingState()
{
    applyStatusBorder("");
    resetStatusWidgets();
    m_iconLabel->setPixmap(themeIcon({"system-software-update"}).pixmap(48, 48));
    m_titleLabel->setText("Checking for updates...");
    m_subRowWidget->setMaximumHeight(0);
    m_subRowWidget->hide();
    m_progressBar->setRange(0, 0);
    m_progressBar->show();
}

void LinuxUpdatePage::setNoUpdatesState()
{
    applyStatusBorder("#1DA51D");
    resetStatusWidgets();
    m_iconLabel->setPixmap(themeIcon({"security-high", "emblem-default", "dialog-ok-apply"}).pixmap(48, 48));
    m_titleLabel->setText(Branding::brand("Linux is up to date"));
    m_subLabel->setText("There are no available updates for your computer.");
}

void LinuxUpdatePage::setUpdatesAvailableState()
{
    applyStatusBorder("#D4A800");
    resetStatusWidgets();
    m_iconLabel->setPixmap(themeIcon({"security-medium", "dialog-warning", "emblem-important"}).pixmap(48, 48));

    int nImp = 0, nOpt = 0;
    for (const auto &p : m_pkgs) { if (p.aur) nOpt++; else nImp++; }
    m_titleLabel->setText("Install updates for your computer");

    // Subtitle: important and optional updates as separate hyperlinks on their
    // own lines. Each link opens the select-updates page.
    if (nImp > 0) {
        setLinkLabel(m_subLabel,
                     QString("%1 important update%2 available")
                         .arg(nImp).arg(nImp == 1 ? " is" : "s are"),
                     false);
        m_subLabel->show();
    } else {
        m_subLabel->hide();
    }
    if (nOpt > 0) {
        setLinkLabel(m_subLabel2,
                     QString("%1 optional update%2 available")
                         .arg(nOpt).arg(nOpt == 1 ? " is" : "s are"),
                     false);
        m_subLabel2->show();
    }

    QStringList parts;
    if (nImp > 0) parts << QString("%1 important update%2").arg(nImp).arg(nImp == 1 ? "" : "s");
    if (nOpt > 0) parts << QString("%1 optional update%2").arg(nOpt).arg(nOpt == 1 ? "" : "s");
    m_selectedLabel->setText(parts.join(", ") + " selected");
    m_selectedLabel->show();
    m_subSep->show();
    m_rightWidget->show();
    m_installBtn->setEnabled(true);

    // Everything is selected by default; the select-updates page can narrow it.
    m_selectedAurPkgs.clear();
    m_selectedPkgNames.clear();
    for (const auto &p : m_pkgs) {
        m_selectedPkgNames << p.name;
        if (p.aur) m_selectedAurPkgs << p.name;
    }
    m_selectedCount = m_pkgs.size();

    fetchPackageSizes();
}

void LinuxUpdatePage::setDownloadingState()
{
    applyStatusBorder("");
    resetStatusWidgets();
    m_iconLabel->setPixmap(themeIcon({"system-software-update", "view-refresh"}).pixmap(48, 48));
    m_titleLabel->setText("Downloading updates...");
    // Package sizes are not reliably available, so progress is reported as the
    // share of selected updates whose download has started, not as a byte total.
    m_subLabel->setText(QString("Downloading %1 update%2 (0% complete)")
        .arg(m_selectedCount).arg(m_selectedCount == 1 ? "" : "s"));
    m_progressBar->setRange(0, 0);
    m_progressBar->show();
}

void LinuxUpdatePage::setInstallingPhaseState()
{
    m_titleLabel->setText("Installing updates...");
    m_subLabel->setText("Installing updates...");
    m_progressBar->setRange(0, 0);
}

void LinuxUpdatePage::setDoneState(bool ok)
{
    applyStatusBorder(ok ? "#1DA51D" : "#CC3300");
    resetStatusWidgets();
    m_iconLabel->setPixmap(ok
        ? themeIcon({"security-high", "dialog-ok-apply"}).pixmap(48, 48)
        : themeIcon({"dialog-error", "security-low"}).pixmap(48, 48));
    m_titleLabel->setText(ok
        ? "The updates were installed successfully."
        : "Some updates failed to install.");
    if (ok) {
        // Anything that was available but not part of this selection is still
        // outstanding -- count both repo and AUR updates, not just optional ones.
        int remaining = 0;
        for (const auto &p : m_pkgs)
            if (!m_selectedPkgNames.contains(p.name)) remaining++;
        if (remaining > 0)
            m_subLabel->setText("There are still updates available for your computer.");
        else
            m_subLabel->setText("Your system is up to date.");
    } else {
        m_subLabel->setText("Check your internet connection and try again, or run pacman -Syu in a terminal.");
    }
}


// Info rows
QString LinuxUpdatePage::formatStamp(const QDateTime &dt)
{
    if (!dt.isValid())
        return "Never";
    const QLocale loc = QLocale::system();
    const QString time = loc.toString(dt.time(), QLocale::ShortFormat);
    if (dt.date() == QDate::currentDate())
        return QString("Today at %1").arg(time);
    if (dt.date() == QDate::currentDate().addDays(-1))
        return QString("Yesterday at %1").arg(time);
    return QString("%1 at %2").arg(loc.toString(dt.date(), QLocale::ShortFormat), time);
}

void LinuxUpdatePage::updateInfoRows()
{
    m_lastCheckVal->setText(formatStamp(m_lastCheck));

    if (m_everInstalled) {
        QString suffix = m_lastInstallOk ? "" : " (Failed).";
        m_lastInstallVal->setText(
            QString("%1%2  <a href='#' style='color:#1F4E99;'>View update history</a>")
            .arg(formatStamp(m_lastInstall), suffix));
    } else {
        m_lastInstallVal->setText("Never");
    }

    // Persist whenever the displayed stamps change so they survive navigating
    // away (the page is destroyed/recreated) and app restarts.
    saveState();
}

void LinuxUpdatePage::loadState()
{
    QSettings s;
    s.beginGroup("LinuxUpdate");
    m_lastCheck     = s.value("lastCheck").toDateTime();
    m_lastInstall   = s.value("lastInstall").toDateTime();
    m_everInstalled = s.value("everInstalled", false).toBool();
    m_lastInstallOk = s.value("lastInstallOk", false).toBool();
    s.endGroup();
}

void LinuxUpdatePage::saveState()
{
    QSettings s;
    s.beginGroup("LinuxUpdate");
    s.setValue("lastCheck",     m_lastCheck);
    s.setValue("lastInstall",   m_lastInstall);
    s.setValue("everInstalled", m_everInstalled);
    s.setValue("lastInstallOk", m_lastInstallOk);
    s.endGroup();
}

// Update list dialog
void LinuxUpdatePage::setLinkLabel(QLabel *label, const QString &text, bool underline)
{
    label->setProperty("linkText", text);
    label->setText(QString("<a href='list' style='color:#1F4E99; text-decoration:%1;'>%2</a>")
                   .arg(underline ? "underline" : "none", text));
}

void LinuxUpdatePage::openUpdateList()
{
    // The update-selection UI is now an in-window page. Ask MainWindow to
    // navigate to it; it calls showSelectView() on this same instance so the
    // checked-update state lives here and survives the view switch.
    emit selectUpdatesRequested();
}

// Select-updates page
void LinuxUpdatePage::showStatusView()
{
    if (m_stack)
        m_stack->setCurrentIndex(0);
}

void LinuxUpdatePage::showSelectView()
{
    // Rebuild the select page from the current package list each time it opens
    // so it reflects the latest check.
    if (m_selectView) {
        m_stack->removeWidget(m_selectView);
        m_selectView->deleteLater();
        m_selectView = nullptr;
    }
    m_selectView = buildSelectView();
    m_stack->addWidget(m_selectView);
    m_stack->setCurrentWidget(m_selectView);

    // The tree header can still cache a too-tall size hint that it computed
    // before its section stylesheet was polished on first show. Clicking a
    // category used to fix this by accident (rebuilding the table reset the
    // header). Re-polish the header once, now that the view is visible, so it
    // settles at the correct height on its own.
    if (m_selTree) {
        QHeaderView *hdr = m_selTree->header();
        QMetaObject::invokeMethod(hdr, [hdr]() {
            hdr->setStyleSheet(hdr->styleSheet());
        }, Qt::QueuedConnection);
    }
}

QWidget *LinuxUpdatePage::buildSelectView()
{
    // Sort important (pacman) first, optional (AUR) last.
    m_selPkgs.clear();
    for (const auto &p : m_pkgs) if (!p.aur) m_selPkgs << p;
    for (const auto &p : m_pkgs) if ( p.aur) m_selPkgs << p;

    // Seed the checked set from the status view's current selection.
    m_selChecked = QSet<QString>(m_selectedPkgNames.begin(), m_selectedPkgNames.end());
    if (m_selChecked.isEmpty())
        for (const auto &p : m_selPkgs) m_selChecked.insert(p.name);

    int nImp = 0, nOpt = 0;
    for (const auto &p : m_selPkgs) { if (p.aur) nOpt++; else nImp++; }
    m_selFilter = (nImp > 0) ? 0 : 1;

    auto *view = new QWidget;
    view->setObjectName("luSelectView");
    view->setStyleSheet("#luSelectView { background: #FFFFFF; }");
    auto *root = new QVBoxLayout(view);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto *headerBar = new QWidget;
    headerBar->setObjectName("luSelectHeader");
    headerBar->setStyleSheet("#luSelectHeader { background: #FFFFFF; }");
    auto *hh = new QHBoxLayout(headerBar);
    hh->setContentsMargins(28, 18, 28, 12);
    auto *hdr = new QLabel("Select the updates you want to install");
    {
        QFont f = hdr->font();
        f.setPointSize(12);
        hdr->setFont(f);
    }
    hdr->setStyleSheet("color: #003399;");
    hh->addWidget(hdr);
    hh->addStretch();
    root->addWidget(headerBar);

    auto *topSep = new QFrame;
    topSep->setFrameShape(QFrame::HLine);
    topSep->setStyleSheet("color: #D9D9D9;");
    root->addWidget(topSep);

    // Body: category sidebar | table | details
    auto *body = new QWidget;
    body->setObjectName("luSelectBody");
    body->setStyleSheet("#luSelectBody { background: #FFFFFF; }");
    auto *bodyH = new QHBoxLayout(body);
    bodyH->setContentsMargins(0, 0, 0, 0);
    bodyH->setSpacing(0);

    // Category sidebar (Important / Optional).
    auto *catBar = new QFrame;
    catBar->setObjectName("catBar");
    catBar->setFixedWidth(150);
    catBar->setStyleSheet("#catBar { background: #F1F4F9; border-right: 1px solid #DCE0E8; }");
    auto *catV = new QVBoxLayout(catBar);
    catV->setContentsMargins(8, 14, 8, 10);
    catV->setSpacing(2);

    auto makeCat = [this](const QString &text) {
        auto *b = new QPushButton(text);
        b->setFlat(true);
        b->setCursor(Qt::PointingHandCursor);
        QFont f = b->font();
        f.setPointSize(9);
        b->setFont(f);
        return b;
    };
    m_selImpItem = makeCat(QString("Important (%1)").arg(nImp));
    m_selOptItem = makeCat(QString("Optional (%1)").arg(nOpt));
    connect(m_selImpItem, &QPushButton::clicked, this, [this]() { setSelectFilter(0); });
    connect(m_selOptItem, &QPushButton::clicked, this, [this]() { setSelectFilter(1); });
    catV->addWidget(m_selImpItem);
    catV->addWidget(m_selOptItem);
    catV->addStretch(1);
    bodyH->addWidget(catBar);

    // Update tree: a "Linux (N)" group node with one update per child row.
    // QTreeWidget gives us the native header and a collapsible group whose
    // auto-tristate checkbox drives the whole group (no manual master logic).
    m_selTree = new QTreeWidget;
    m_selTree->setColumnCount(2);
    m_selTree->setHeaderLabels({ "Name", "Size" });
    Win7::configureListTree(m_selTree);
    // Unlike the flat ribbon lists, this tree keeps its expandable group node.
    m_selTree->setRootIsDecorated(true);
    m_selTree->setIndentation(20);
    m_selTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_selTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(m_selTree, &QTreeWidget::itemChanged, this,
        [this](QTreeWidgetItem *it, int col) {
            if (m_selUpdating || col != 0) return;
            const QString name = it->data(0, Qt::UserRole).toString();
            if (name.isEmpty()) {
                // Group node: auto-tristate has cascaded to the children, so
                // mirror their states into the checked set.
                for (int i = 0; i < it->childCount(); ++i) {
                    auto *c = it->child(i);
                    const QString n = c->data(0, Qt::UserRole).toString();
                    if (c->checkState(0) == Qt::Checked) m_selChecked.insert(n);
                    else                                 m_selChecked.remove(n);
                }
            } else {
                if (it->checkState(0) == Qt::Checked) m_selChecked.insert(name);
                else                                  m_selChecked.remove(name);
            }
            updateSelectCount();
        });

    connect(m_selTree, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
            if (!cur) return;
            const QString name = cur->data(0, Qt::UserRole).toString();
            if (name.isEmpty()) return;   // group node
            const PkgInfo *pkg = nullptr;
            for (const auto &p : m_selPkgs) if (p.name == name) { pkg = &p; break; }
            if (!pkg) return;
            m_selDetailTitle->setText(pkg->name);
            m_selDetailBody->setText(
                QString("<b>Update:</b> %1 → %2<br><br>"
                        "For more information run:<br>"
                        "<tt>pacman -Si %3</tt>")
                .arg(pkg->oldVer, pkg->newVer, pkg->name));
            m_selDetailNote->show();
        });

    bodyH->addWidget(m_selTree, 1);

    // Details panel.
    auto *detail = new QWidget;
    detail->setObjectName("luSelectDetail");
    detail->setFixedWidth(300);
    detail->setStyleSheet(
        "#luSelectDetail { background: #FFFFFF; border-left: 1px solid #E0E0E0; }");
    auto *detV = new QVBoxLayout(detail);
    detV->setContentsMargins(12, 12, 12, 12);
    detV->setSpacing(8);

    m_selDetailTitle = new QLabel;
    {
        QFont f = m_selDetailTitle->font();
        f.setPointSize(9);
        f.setBold(true);
        m_selDetailTitle->setFont(f);
    }
    m_selDetailTitle->setWordWrap(true);
    m_selDetailTitle->setStyleSheet("color: #000000; border: none;");
    detV->addWidget(m_selDetailTitle);

    m_selDetailBody = new QLabel("Select a package from the list to see details.");
    {
        QFont f = m_selDetailBody->font();
        f.setPointSize(9);
        m_selDetailBody->setFont(f);
    }
    m_selDetailBody->setWordWrap(true);
    m_selDetailBody->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_selDetailBody->setStyleSheet("color: #333333; border: none;");
    detV->addWidget(m_selDetailBody, 1);

    m_selDetailNote = new QLabel(
        "\xe2\x9a\xa0 You may need to restart your computer after installing this update.");
    {
        QFont f = m_selDetailNote->font();
        f.setPointSize(8);
        m_selDetailNote->setFont(f);
    }
    m_selDetailNote->setWordWrap(true);
    m_selDetailNote->setStyleSheet("color: #555555; border: none;");
    m_selDetailNote->hide();
    detV->addWidget(m_selDetailNote);

    bodyH->addWidget(detail);
    root->addWidget(body, 1);

    // Footer: selected-count summary + OK / Cancel.
    auto *botSep = new QFrame;
    botSep->setFrameShape(QFrame::HLine);
    botSep->setStyleSheet("color: #D9D9D9;");
    root->addWidget(botSep);

    auto *footer = new QWidget;
    footer->setObjectName("luSelectFooter");
    footer->setStyleSheet("#luSelectFooter { background: #F4F4F4; }");
    auto *fh = new QHBoxLayout(footer);
    fh->setContentsMargins(14, 8, 14, 8);
    fh->setSpacing(8);

    m_selCountLabel = new QLabel;
    {
        QFont f = m_selCountLabel->font();
        f.setPointSize(9);
        m_selCountLabel->setFont(f);
    }
    m_selCountLabel->setStyleSheet("color: #333333; border: none;");
    fh->addWidget(m_selCountLabel, 1);

    auto *okBtn     = new QPushButton("OK");
    auto *cancelBtn = new QPushButton("Cancel");
    okBtn->setDefault(true);
    okBtn->setMinimumWidth(80);
    cancelBtn->setMinimumWidth(80);
    fh->addWidget(okBtn);
    fh->addWidget(cancelBtn);
    root->addWidget(footer);

    connect(okBtn, &QPushButton::clicked, this, [this]() {
        applySelection();
        emit selectViewClosed();
    });
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        emit selectViewClosed();
    });

    setSelectFilter(m_selFilter);   // highlights the active category + fills table
    return view;
}

void LinuxUpdatePage::setSelectFilter(int filter)
{
    m_selFilter = filter;

    const QString base =
        "QPushButton { text-align: left; border: 1px solid transparent;"
        " padding: 4px 6px; background: transparent; color: #000000; }"
        "QPushButton:hover { color: #0033AA; }";
    const QString sel =
        "QPushButton { text-align: left; border: 1px solid #B8CDEA;"
        " padding: 4px 6px; background: #D6E4F5; color: #000000; }";
    if (m_selImpItem) m_selImpItem->setStyleSheet(filter == 0 ? sel : base);
    if (m_selOptItem) m_selOptItem->setStyleSheet(filter == 1 ? sel : base);

    populateSelectTable();
    updateSelectCount();
}

void LinuxUpdatePage::populateSelectTable()
{
    if (!m_selTree) return;
    m_selUpdating = true;
    m_selTree->clear();

    QList<PkgInfo> filtered;
    for (const auto &p : m_selPkgs) {
        const bool inFilter = (m_selFilter == 0) ? !p.aur : p.aur;
        if (inFilter) filtered << p;
    }

    // Group node. An empty UserRole marks it as the group (vs. a package row).
    // ItemIsAutoTristate makes its checkbox reflect and drive the children.
    auto *group = new QTreeWidgetItem(m_selTree);
    group->setText(0, QStringLiteral("%1 (%2)")
                          .arg(Branding::os())
                          .arg(filtered.size()));
    {
        QFont f = group->font(0);
        f.setBold(true);
        group->setFont(0, f);
    }
    group->setData(0, Qt::UserRole, QString());
    group->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                    | Qt::ItemIsAutoTristate);
    group->setBackground(0, QColor("#E8EBF0"));
    group->setBackground(1, QColor("#E8EBF0"));

    for (const auto &pkg : filtered) {
        auto *child = new QTreeWidgetItem(group);
        child->setText(0, pkg.name);
        child->setText(1, m_pkgSizes.value(pkg.name, QStringLiteral("-")));
        child->setForeground(1, QColor("#555555"));
        child->setData(0, Qt::UserRole, pkg.name);
        child->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                        | Qt::ItemIsSelectable);
        child->setCheckState(0, m_selChecked.contains(pkg.name)
                             ? Qt::Checked : Qt::Unchecked);
    }

    group->setExpanded(true);
    m_selUpdating = false;
}

void LinuxUpdatePage::updateSelectCount()
{
    if (!m_selCountLabel) return;
    int selImp = 0, selOpt = 0;
    for (const auto &p : m_selPkgs) {
        if (!m_selChecked.contains(p.name)) continue;
        if (p.aur) ++selOpt; else ++selImp;
    }
    QStringList fp;
    if (selImp > 0) fp << QString("%1 important update%2").arg(selImp).arg(selImp == 1 ? "" : "s");
    if (selOpt > 0) fp << QString("%1 optional update%2").arg(selOpt).arg(selOpt == 1 ? "" : "s");
    m_selCountLabel->setText(fp.isEmpty() ? "Nothing selected" : "Total selected: " + fp.join(", "));
}

void LinuxUpdatePage::applySelection()
{
    int selImp = 0, selOpt = 0;
    m_selectedAurPkgs.clear();
    m_selectedPkgNames.clear();
    for (const auto &p : m_selPkgs) {
        if (!m_selChecked.contains(p.name)) continue;
        m_selectedPkgNames << p.name;
        if (p.aur) { ++selOpt; m_selectedAurPkgs << p.name; }
        else       { ++selImp; }
    }
    m_selectedCount = selImp + selOpt;

    if (m_selectedCount > 0) {
        QStringList parts;
        if (selImp > 0) parts << QString("%1 important update%2").arg(selImp).arg(selImp == 1 ? "" : "s");
        if (selOpt > 0) parts << QString("%1 optional update%2").arg(selOpt).arg(selOpt == 1 ? "" : "s");
        m_selectedLabel->setText(parts.join(", ") + " selected");
        m_installBtn->setEnabled(true);
    } else {
        m_selectedLabel->setText("No updates selected");
        m_installBtn->setEnabled(false);
    }
}

// Input validation//
// Package names and versions are checked before they can reach a privileged
// pacman/yay invocation, so a crafted "name old -> new" line can never smuggle
// extra arguments into the command.

// Arch Linux package names: [a-zA-Z0-9@._+-], must start with alphanumeric.
static bool isValidPackageName(const QString &name)
{
    static const QRegularExpression re(R"(^[a-zA-Z0-9][a-zA-Z0-9@._+\-]*$)");
    return !name.isEmpty() && re.match(name).hasMatch();
}

// Arch version strings: epoch:pkgver-pkgrel, characters [a-zA-Z0-9.:_+-~]
static bool isValidVersion(const QString &ver)
{
    static const QRegularExpression re(R"(^[a-zA-Z0-9.:_+\-~]+$)");
    return !ver.isEmpty() && re.match(ver).hasMatch();
}

// Package size fetch
void LinuxUpdatePage::fetchPackageSizes()
{
    if (m_pkgs.isEmpty()) return;

    QStringList args = { "-Si" };
    for (const auto &pkg : m_pkgs)
        args << pkg.name;

    const QString sizeCmd = m_yayAvailable ? QString("yay") : QString("pacman");

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    int nImpSz = 0, nOptSz = 0;
    for (const auto &p : m_pkgs) { if (p.aur) nOptSz++; else nImpSz++; }
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, nImpSz, nOptSz](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAll());
        proc->deleteLater();

        // Parse each package block to capture per-package download sizes and a
        // running total. Blocks are separated by a blank line in pacman -Si.
        static const QRegularExpression nameRe(R"(Name\s*:\s*(\S+))");
        static const QRegularExpression sizeRe(
            R"(Download Size\s*:\s*([\d.]+)\s*(B|KiB|MiB|GiB))");
        auto toKiB = [](double val, const QString &unit) {
            if      (unit == "GiB") return val * 1024.0 * 1024.0;
            else if (unit == "MiB") return val * 1024.0;
            else if (unit == "B")   return val / 1024.0;
            return val;   // KiB
        };
        auto human = [](double kib) {
            return kib >= 1024.0 ? QString("%1 MB").arg(kib / 1024.0, 0, 'f', 1)
                                 : QString("%1 KB").arg(qRound(kib));
        };

        double totalKiB = 0.0;
        const QStringList blocks = out.split("\n\n", Qt::SkipEmptyParts);
        for (const QString &block : blocks) {
            auto sz = sizeRe.match(block);
            if (!sz.hasMatch()) continue;
            const double kib = toKiB(sz.captured(1).toDouble(), sz.captured(2));
            totalKiB += kib;
            auto nm = nameRe.match(block);
            if (nm.hasMatch())
                m_pkgSizes.insert(nm.captured(1), human(kib));
        }

        // If the select view is open, refresh it so the Size column fills in.
        if (m_selTree && m_stack && m_stack->currentWidget() == m_selectView)
            populateSelectTable();

        QString sizeStr;
        if (totalKiB > 0)
            sizeStr = ", " + human(totalKiB);

        QStringList parts;
        if (nImpSz > 0) parts << QString("%1 important update%2").arg(nImpSz).arg(nImpSz == 1 ? "" : "s");
        if (nOptSz > 0) parts << QString("%1 optional update%2").arg(nOptSz).arg(nOptSz == 1 ? "" : "s");
        m_selectedLabel->setText(parts.join(", ") + " selected" + sizeStr);
    });

    proc->start(sizeCmd, args);
}

// Check for updates process
void LinuxUpdatePage::parseUpdateList(const QByteArray &data, bool aur)
{
    static const QRegularExpression lineRe(R"(^(\S+)\s+(\S+)\s+->\s+(\S+)\s*$)");

    for (const QString &line : QString::fromUtf8(data).split('\n')) {
        auto m = lineRe.match(line.trimmed());
        if (!m.hasMatch()) continue;

        const QString name   = m.captured(1);
        const QString oldVer = m.captured(2);
        const QString newVer = m.captured(3);

        // Validate all three fields before they can reach a privileged operation.
        if (!isValidPackageName(name) || !isValidVersion(oldVer) || !isValidVersion(newVer))
            continue;

        m_pkgs.append({ name, oldVer, newVer, aur });
    }
}

void LinuxUpdatePage::checkForUpdates()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_pkgs.clear();
    setCheckingState();

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            m_proc->deleteLater();
            m_proc = nullptr;
            runFallbackCheck();
        }
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LinuxUpdatePage::onCheckFinished);

    m_proc->start("checkupdates", {});
}

void LinuxUpdatePage::runFallbackCheck()
{
    // checkupdates is unavailable, so we must sync the DB ourselves.
    // pacman -Sy writes to /var/lib/pacman, needs root via pkexec.
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        const QString errOut =
            QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
        m_proc->deleteLater();
        m_proc = nullptr;

        if (exitCode != 0) {
            m_lastCheck = QDateTime::currentDateTime();
            updateInfoRows();
            setNoUpdatesState();
            if (!errOut.isEmpty())
                QMessageBox::warning(this, "Update check failed",
                    QString("Could not synchronize the package database:\n\n%1")
                    .arg(errOut.toHtmlEscaped()));
            return;
        }

        // DB is now fresh, query upgradeable packages (no root needed).
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) {
            m_lastCheck = QDateTime::currentDateTime();
            parseUpdateList(m_proc->readAll(), false);
            m_proc->deleteLater();
            m_proc = nullptr;
            updateInfoRows();
            runYayCheck();
        });
        m_proc->start("pacman", {"-Qu"});
    });
    m_proc->start("pkexec", {"pacman", "-Sy"});
}

void LinuxUpdatePage::runYayCheck()
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        m_yayAvailable = false;
        m_proc->deleteLater();
        m_proc = nullptr;
        finishCheck();
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        m_yayAvailable = true;
        QByteArray out = m_proc->readAll();
        m_proc->deleteLater();
        m_proc = nullptr;
        if (exitCode == 0)
            parseUpdateList(out, true);
        finishCheck();
    });
    m_proc->start("yay", {"-Qua"});
}

void LinuxUpdatePage::finishCheck()
{
    if (m_pkgs.isEmpty()) setNoUpdatesState();
    else setUpdatesAvailableState();
}

void LinuxUpdatePage::onCheckFinished(int exitCode, QProcess::ExitStatus)
{
    if (!m_proc) return;

    m_lastCheck = QDateTime::currentDateTime();
    QByteArray out = m_proc->readAll();
    m_proc->deleteLater();
    m_proc = nullptr;

    // checkupdates exit 2 = up to date; exit 0 = updates found; exit 1 = error
    parseUpdateList((exitCode == 2) ? QByteArray{} : out, false);

    updateInfoRows();
    runYayCheck();
}

// Install process
void LinuxUpdatePage::installUpdates()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    m_installBuf.clear();
    m_inInstallPhase = false;
    m_seenInstalledPkgs.clear();
    m_seenDownloadedPkgs.clear();
    m_installProgressCount = 0;
    m_downloadProgressCount = 0;

    // Split the current selection into repository (pacman) packages and AUR
    // packages. Only the repo packages go to pacman here; the selected AUR
    // packages are handled afterwards by startYayInstall().
    QStringList repoPkgs;
    int totalRepo = 0;
    for (const auto &p : m_pkgs) {
        if (p.aur) continue;
        ++totalRepo;
        if (m_selectedPkgNames.contains(p.name))
            repoPkgs << p.name;
    }

    // Nothing from the repos is selected: skip pacman and go straight to the
    // AUR step if any AUR packages were chosen, otherwise there is nothing to do.
    if (repoPkgs.isEmpty()) {
        if (!m_selectedAurPkgs.isEmpty() && m_yayAvailable) {
            setDownloadingState();
            startYayInstall();
        } else {
            setDoneState(true);
        }
        return;
    }

    setDownloadingState();

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyRead, this, &LinuxUpdatePage::onInstallReadyRead);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LinuxUpdatePage::onInstallFinished);

    // When every repository update is selected, do a normal full upgrade
    // (-Syu) -- the safest path that avoids a partial upgrade. When only a
    // subset is selected, target just those packages with -S; the leading -y
    // refreshes the sync database so the latest versions are installed.
    QStringList pacArgs = { "pacman", "--color", "never" };
    if (repoPkgs.size() == totalRepo) {
        pacArgs << "-Syu" << "--noconfirm";
    } else {
        pacArgs << "-Sy" << "--noconfirm" << repoPkgs;
    }
    m_proc->start("pkexec", pacArgs);
}

void LinuxUpdatePage::onInstallReadyRead()
{
    m_installBuf += QString::fromUtf8(m_proc->readAll());

    // pacman draws colored, in-place output: ANSI color codes plus cursor-move
    // sequences for the parallel-download list (ParallelDownloads). Strip those
    // escapes so the (n/N) counters and package names match reliably; without
    // this the progress text never advances.
    static const QRegularExpression ansiRe(R"(\x1B\[[0-9;?]*[ -/]*[@-~])");
    QString clean = m_installBuf;
    clean.remove(ansiRe);

    if (!m_inInstallPhase &&
        clean.contains(":: Processing package changes..."))
    {
        m_inInstallPhase = true;
        setInstallingPhaseState();
    }

    if (m_inInstallPhase) {
        const QString tail = clean.mid(
            clean.indexOf(":: Processing package changes..."));

        // Over a pipe pacman does not print the "(n/N)" progress-bar prefix --
        // that is TTY-only. It prints the plain libalpm event messages instead,
        // e.g. "upgrading vlc..." / "installing foo..." / "removing bar...", with
        // no count. So match those and derive the count ourselves from how many
        // of the selected packages have been reached.
        static const QRegularExpression reInst(
            R"((?:installing|upgrading|reinstalling|removing|downgrading)\s+(\S+?)\.\.\.)");

        QString lastPkg;
        QRegularExpressionMatchIterator instIt = reInst.globalMatch(tail);
        while (instIt.hasNext()) {
            QRegularExpressionMatch match = instIt.next();
            const QString pkg = match.captured(1);
            lastPkg = pkg;
            if (m_selectedPkgNames.contains(pkg) && !m_seenInstalledPkgs.contains(pkg)) {
                m_seenInstalledPkgs.insert(pkg);
                ++m_installProgressCount;
            }
        }
        if (!lastPkg.isEmpty() && m_selectedCount > 0) {
            const int cur = qBound(1, m_installProgressCount, m_selectedCount);
            m_progressBar->setRange(0, m_selectedCount);
            m_progressBar->setValue(cur);
            // Subtitle: count on the first line, the package currently being
            // installed on the second (m_subLabel2).
            m_subLabel->setText(QString("Installing update %1 of %2...")
                .arg(cur).arg(m_selectedCount));
            m_subLabel2->setText(lastPkg.toHtmlEscaped());
            m_subLabel2->show();
        }
        return;
    }

    const int retrIdx = clean.indexOf(":: Retrieving packages...");
    if (retrIdx < 0) return;

    // With ParallelDownloads pacman redraws the download list in place, so there
    // is no stable per-line "name size" format. Instead, count a selected
    // package as downloading once its "name-<version>" filename first appears
    // anywhere in the retrieving section. Sizes are not reported, so progress is
    // expressed purely as the share of selected updates seen so far.
    const QString retrSection = clean.mid(retrIdx);
    for (const QString &name : m_selectedPkgNames) {
        if (m_seenDownloadedPkgs.contains(name)) continue;
        // Require a digit right after "name-" so a package name that is a prefix
        // of another (e.g. "gcc" vs "gcc-libs") does not get miscounted.
        const QString needle = name + QLatin1Char('-');
        int idx = retrSection.indexOf(needle);
        while (idx >= 0 &&
               !(idx + needle.size() < retrSection.size() &&
                 retrSection.at(idx + needle.size()).isDigit())) {
            idx = retrSection.indexOf(needle, idx + 1);
        }
        if (idx >= 0) {
            m_seenDownloadedPkgs.insert(name);
            ++m_downloadProgressCount;
        }
    }

    if (m_downloadProgressCount > 0 && m_selectedCount > 0) {
        const int cur = qMin(m_downloadProgressCount, m_selectedCount);
        const int pct = cur * 100 / m_selectedCount;
        m_progressBar->setRange(0, m_selectedCount);
        m_progressBar->setValue(cur);
        m_subLabel->setText(QString("Downloading %1 update%2 (%3% complete)")
            .arg(m_selectedCount).arg(m_selectedCount == 1 ? "" : "s").arg(pct));
    }
}

void LinuxUpdatePage::onInstallFinished(int exitCode, QProcess::ExitStatus)
{
    bool pacmanOk = (exitCode == 0);

    const QString errOut = m_proc
        ? QString::fromUtf8(m_proc->readAllStandardError()).trimmed()
        : QString();

    m_proc->deleteLater();
    m_proc = nullptr;

    if (pacmanOk && !m_selectedAurPkgs.isEmpty() && m_yayAvailable) {
        startYayInstall();
        return;
    }

    m_lastInstall   = QDateTime::currentDateTime();
    m_everInstalled = true;
    m_lastInstallOk = pacmanOk;
    updateInfoRows();
    setDoneState(m_lastInstallOk);

    if (!m_lastInstallOk && !errOut.isEmpty()) {
        QMessageBox::warning(this, "Update failed",
            QString("pacman reported an error:\n\n%1").arg(errOut.toHtmlEscaped()));
    }
}

void LinuxUpdatePage::startYayInstall()
{
    // Remove stale build caches so git sources are re-cloned fresh
    const QString yayCache = QDir::homePath() + "/.cache/yay/";
    for (const QString &pkg : m_selectedAurPkgs)
        QDir(yayCache + pkg).removeRecursively();

    m_installBuf.clear();
    m_inInstallPhase = false;

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyRead, this, &LinuxUpdatePage::onInstallReadyRead);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        // Drain any remaining output not yet consumed by onInstallReadyRead
        m_installBuf += QString::fromUtf8(m_proc->readAll());
        m_lastInstall   = QDateTime::currentDateTime();
        m_everInstalled = true;
        m_lastInstallOk = (exitCode == 0);
        m_proc->deleteLater();
        m_proc = nullptr;
        updateInfoRows();
        setDoneState(m_lastInstallOk);
        if (!m_lastInstallOk) {
            // Show the tail of yay output so the user can diagnose the failure
            const QString tail = m_installBuf.length() > 2000
                ? "..." + m_installBuf.right(2000) : m_installBuf;
            QMessageBox::warning(this, "AUR update failed",
                QString("yay reported an error:\n\n%1").arg(tail.toHtmlEscaped()));
        }
    });
    QStringList yayArgs = {"--noconfirm", "--sudo", "pkexec", "--cleanafter"};
    if (m_selectedAurPkgs.isEmpty()) {
        yayArgs.prepend("-Sua");
    } else {
        yayArgs.prepend("-S");
        yayArgs << m_selectedAurPkgs;
    }
    m_proc->start("yay", yayArgs);
}

// Hover underline on link labels
bool LinuxUpdatePage::eventFilter(QObject *watched, QEvent *event)
{
    if (auto *label = qobject_cast<QLabel *>(watched)) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            QFont font = label->font();
            font.setUnderline(event->type() == QEvent::Enter);
            label->setFont(font);
        }
    }
    return QWidget::eventFilter(watched, event);
}
