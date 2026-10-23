#include "ui/control/ControlPanelView.h"
#include "ui/control/CategoryWidget.h"
#include "ui/control/IconHelper.h"
#include "ui/control/Win7Ui.h"

#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QWidget>
#include <QFrame>
#include <QIcon>
#include <QFont>
#include <QMenu>
#include <QAction>
#include <QEvent>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QProcess>
#include <QStandardPaths>
#include "aero/sounds.h"
#include "data/control/Categories.h"
#include "data/control/Branding.h"
#include "ui/control/PageId.h"
#include "ui/control/PageRegistry.h"
#include "ui/control/Commands.h"
#include "ui/control/pages/LinuxUpdatePage.h"
#include "ui/control/pages/SystemPage.h"
#include "ui/control/pages/InstalledUpdatesPage.h"
#include "ui/control/pages/ProgramsFeaturesPage.h"
#include "ui/control/pages/NetworkSharingPage.h"
#include "ui/control/pages/FirewallPage.h"
#include "ui/control/pages/ActionCenterPage.h"
#include "ui/control/pages/PowerOptionsPage.h"
#include "ui/control/pages/PerformancePage.h"
#include "ui/control/dialogs/SoundDialog.h"
#include "ui/control/pages/PersonalizationPage.h"
#include "ui/control/pages/FontsPage.h"
#include "ui/control/pages/UserAccountsPage.h"
#include "ui/control/pages/EaseOfAccessPage.h"
#include "ui/control/pages/DevicesAndPrintersPage.h"
#include "ui/control/dialogs/DateTimeDialog.h"
#include "aero/palette.h"
#include "aero/artwork.h"

namespace ControlPanel {

View::View(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: %1; }")
        .arg(QLatin1String(Aero::Palette::Surface)));
    layout->addWidget(m_scroll, 1);
}

bool View::eventFilter(QObject *watched, QEvent *event)
{
    // Qt's QLabel ignores `text-decoration` in style sheets, so underline-on-
    // hover has to be driven by toggling the font's underline flag directly.
    // Only applies to labels explicitly registered as interactive links
    if (auto *label = qobject_cast<QLabel *>(watched)) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            if (label == m_checkUpdatesLabel
                || m_navLinks.contains(watched)
                || m_subpageLinks.contains(watched)
                || m_commandLinks.contains(watched)
                || m_appletLinks.contains(watched))
            {
                QFont font = label->font();
                font.setUnderline(event->type() == QEvent::Enter);
                label->setFont(font);
            }
        }
    }

    // "Check for updates" sidebar link on the Linux Update page
    if (watched == m_checkUpdatesLabel &&
        event->type() == QEvent::MouseButtonRelease &&
        m_updatePage)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton)
            QMetaObject::invokeMethod(m_updatePage,
                &LinuxUpdatePage::checkForUpdates, Qt::QueuedConnection);
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            // External-launch links (e.g. Desktop Gadgets -> KDE widget panels)
            auto cmdIt = m_commandLinks.constFind(watched);
            if (cmdIt != m_commandLinks.constEnd()) {
                launchDetached(this, cmdIt.value());
                return true;
            }
            auto appletIt = m_appletLinks.constFind(watched);
            if (appletIt != m_appletLinks.constEnd()) {
                const QString id = appletIt.value();
                QMetaObject::invokeMethod(this,
                    [this, id]() { openApplet(id); },
                    Qt::QueuedConnection);
                return true;
            }
            auto navIt = m_navLinks.constFind(watched);
            if (navIt != m_navLinks.constEnd()) {
                const QString path = navIt.value();
                QMetaObject::invokeMethod(this,
                    [this, path]() { requestPath(path); },
                    Qt::QueuedConnection);
                return true;
            }
            auto subIt = m_subpageLinks.constFind(watched);
            if (subIt != m_subpageLinks.constEnd()) {
                // An empty target means "Control Panel Home"
                const QString path = subIt.value();
                auto go = [this, path]() {
                    requestPath(path);
                };
                if (m_sidebarTextEffect) {
                    auto *anim = new QPropertyAnimation(m_sidebarTextEffect, "opacity");
                    anim->setStartValue(1.0);
                    anim->setEndValue(0.0);
                    anim->setDuration(300);
                    anim->setEasingCurve(QEasingCurve::InCubic);
                    QObject::connect(anim, &QPropertyAnimation::finished, this, go);
                    anim->start(QAbstractAnimation::DeleteWhenStopped);
                } else {
                    QMetaObject::invokeMethod(this, go, Qt::QueuedConnection);
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// Breadcrumb paths for the pages View routes on, derived from each page's
// canonical PageId so the strings live in exactly one place (PageRegistry). The
// select-updates entry is Linux Update's in-window "select updates" sub-view
static const QString kUpdatePath           = PageRegistry::pathFor(PageId::LinuxUpdate);
static const QString kSelectPath           = PageRegistry::pathFor(PageId::SelectUpdates);
static const QString kProgramsFeaturesPath = PageRegistry::pathFor(PageId::ProgramsFeatures);
static const QString kInstalledUpdatesPath = PageRegistry::pathFor(PageId::InstalledUpdates);
static const QString kNetworkSharingPath   = PageRegistry::pathFor(PageId::NetworkSharing);
static const QString kFirewallPath         = PageRegistry::pathFor(PageId::Firewall);
static const QString kActionCenterPath     = PageRegistry::pathFor(PageId::ActionCenter);
static const QString kPowerOptionsPath     = PageRegistry::pathFor(PageId::PowerOptions);
static const QString kPerformancePath      = PageRegistry::pathFor(PageId::Performance);
static const QString kPersonalizationPath  = PageRegistry::pathFor(PageId::Personalization);
static const QString kFontsPath            = PageRegistry::pathFor(PageId::Fonts);
static const QString kUserAccountsPath     = PageRegistry::pathFor(PageId::UserAccounts);
static const QString kEaseOfAccessPath     = PageRegistry::pathFor(PageId::EaseOfAccess);
static const QString kDevicesPrintersPath  = PageRegistry::pathFor(PageId::DevicesPrinters);

// A sub-path (one containing '/') is routable only if showPath knows how to
// render it. Category-level paths are validated separately via detailGroupsFor
static bool isRoutableSubPath(const QString &path)
{
    return path == kUpdatePath || path == kSelectPath
        || path == kProgramsFeaturesPath
        || path == kInstalledUpdatesPath
        || path == kNetworkSharingPath
        || path == kFirewallPath
        || path == kActionCenterPath
        || path == kPowerOptionsPath
        || path == kPerformancePath
        || path == kPersonalizationPath
        || path == kFontsPath
        || path == kUserAccountsPath
        || path == kEaseOfAccessPath
        || path == kDevicesPrintersPath
        || path == QStringLiteral("System and Security/System");
}

QString View::resolvePath(const QString &path)
{
    if (path.isEmpty())
        return QString();

    if (!path.contains('/')) {
        if (detailGroupsFor(path))
            return path;
        return QString();
    }

    if (isRoutableSubPath(path))
        return path;

    // An intermediate crumb such as "Programs/Programs and Features" has no
    // page of its own, so it shows its category rather than a blank pane
    const QString category = path.section('/', 0, 0);
    if (detailGroupsFor(category))
        return category;
    return QString();
}

QString View::displayName(const QString &segment)
{
    return Branding::brand(segment);
}

QStringList View::categories()
{
    return navOrder();
}

void View::requestPath(const QString &path)
{
    const QString target = resolvePath(path);

    // A link to a page that does not exist yet stays where it is, rather than
    // falling back to the home page
    if (target.isEmpty() && !path.isEmpty())
        return;

    emit pathRequested(target);
}

QString View::currentPath() const
{
    return m_path;
}

void View::openApplet(const QString &id)
{
    // Applet ids are "base[:view]", e.g. "datetime:additional"
    const QString base = id.section(':', 0, 0);
    const QString view = id.section(':', 1, 1);
    Aero::playSound(QStringLiteral("Windows Navigation Start"));

    if (base == QLatin1String("folderoptions")) {
        emit folderOptionsRequested(view);
    } else if (base == QLatin1String("datetime")) {
        DateTimeDialog dlg(this);
        if (view == QLatin1String("additional"))
            dlg.showTab(DateTimeDialog::TabAdditionalClocks);
        else if (view == QLatin1String("internet"))
            dlg.showTab(DateTimeDialog::TabInternetTime);
        dlg.exec();
    } else if (base == QLatin1String("sound")) {
        SoundDialog dlg(this);
        if (view == QLatin1String("recording"))
            dlg.showTab(SoundDialog::TabRecording);
        else if (view == QLatin1String("sounds"))
            dlg.showTab(SoundDialog::TabSounds);
        else if (view == QLatin1String("communications"))
            dlg.showTab(SoundDialog::TabCommunications);
        dlg.exec();
    }
}

void View::showPath(const QString &path)
{
    const QString entry = resolvePath(path);

    // Moving between the status view and the select-updates view reuses the
    // live page so the checked-update state survives; only the page's internal
    // view switches. Recreating it would reset the checks
    const bool isUpdateEntry = entry == kUpdatePath || entry == kSelectPath;
    if (isUpdateEntry && m_updatePage && m_scroll->widget() == m_updatePage) {
        m_path = entry;
        if (entry == kSelectPath)
            m_updatePage->showSelectView();
        else
            m_updatePage->showStatusView();
        return;
    }

    m_path = entry;

    // The page about to be replaced owns the old nav-link labels
    m_navLinks.clear();
    m_subpageLinks.clear();
    m_commandLinks.clear();
    m_appletLinks.clear();
    m_sidebarTextEffect  = nullptr;
    m_updatePage         = nullptr;
    m_checkUpdatesLabel  = nullptr;

    if (entry.isEmpty()) {
        m_scroll->setWidget(buildHomePage());
    } else if (entry.contains('/')) {
        // Sub-page path: "Category/SubPage[/SubView]"
        QStringList parts = entry.split('/');
        if (parts.size() >= 2 && parts[0] == "System and Security"
            && parts[1] == "Linux Update") {
            auto *sidebar = buildSubpageSidebar(
                LinuxUpdatePage::sidebarLinks(),
                LinuxUpdatePage::sidebarSeeAlso());
            auto *wuPage = new LinuxUpdatePage(sidebar);
            m_updatePage = wuPage;
            QObject::connect(wuPage, &LinuxUpdatePage::navigateHomeRequested,
                             this, [this]() { requestPath(QString()); },
                             Qt::QueuedConnection);
            QObject::connect(wuPage, &LinuxUpdatePage::selectUpdatesRequested,
                             this, [this]() { requestPath(kSelectPath); },
                             Qt::QueuedConnection);
            QObject::connect(wuPage, &LinuxUpdatePage::selectViewClosed,
                             this, &View::backRequested, Qt::QueuedConnection);
            QObject::connect(wuPage, &QObject::destroyed, this,
                             [this]() { m_updatePage = nullptr; });
            m_scroll->setWidget(wuPage);
            if (entry == kSelectPath)
                m_updatePage->showSelectView();
        } else if (parts.size() == 2 && parts[0] == "System and Security"
                   && parts[1] == "System") {
            auto *sidebar = buildSubpageSidebar(
                SystemPage::sidebarLinks(),
                SystemPage::sidebarSeeAlso());
            auto *sysPage = new SystemPage(sidebar);
            QObject::connect(sysPage, &SystemPage::performanceRequested, this,
                             [this]() { requestPath(kPerformancePath); },
                             Qt::QueuedConnection);
            // Redrawn after the branding dialog so the new wording and badge
            // take effect live, in the window's address bar too
            QObject::connect(sysPage, &SystemPage::brandingChanged,
                             this, &View::reloadRequested, Qt::QueuedConnection);
            m_scroll->setWidget(sysPage);
        } else if (entry == kPerformancePath) {
            auto *sidebar = buildSubpageSidebar(
                PerformancePage::sidebarLinks(),
                PerformancePage::sidebarSeeAlso());
            m_scroll->setWidget(new PerformancePage(sidebar));
        } else if (entry == kInstalledUpdatesPath) {
            auto *sidebar = buildSubpageSidebar(
                InstalledUpdatesPage::sidebarLinks(),
                InstalledUpdatesPage::sidebarSeeAlso());
            m_scroll->setWidget(new InstalledUpdatesPage(sidebar));
        } else if (entry == kProgramsFeaturesPath) {
            auto *sidebar = buildSubpageSidebar(
                ProgramsFeaturesPage::sidebarLinks(),
                ProgramsFeaturesPage::sidebarSeeAlso());
            m_scroll->setWidget(new ProgramsFeaturesPage(sidebar));
        } else if (entry == kNetworkSharingPath) {
            auto *sidebar = buildSubpageSidebar(
                NetworkSharingPage::sidebarLinks(),
                NetworkSharingPage::sidebarSeeAlso());
            m_scroll->setWidget(new NetworkSharingPage(sidebar));
        } else if (entry == kFirewallPath) {
            auto *sidebar = buildSubpageSidebar(
                FirewallPage::sidebarLinks(),
                FirewallPage::sidebarSeeAlso());
            m_scroll->setWidget(new FirewallPage(sidebar));
        } else if (entry == kActionCenterPath) {
            auto *sidebar = buildSubpageSidebar(
                ActionCenterPage::sidebarLinks(),
                ActionCenterPage::sidebarSeeAlso());
            m_scroll->setWidget(new ActionCenterPage(sidebar));
        } else if (entry == kPowerOptionsPath) {
            auto *sidebar = buildSubpageSidebar(
                PowerOptionsPage::sidebarLinks(),
                PowerOptionsPage::sidebarSeeAlso());
            m_scroll->setWidget(new PowerOptionsPage(sidebar));
        } else if (entry == kPersonalizationPath) {
            auto *sidebar = buildSubpageSidebar(
                PersonalizationPage::sidebarLinks(),
                PersonalizationPage::sidebarSeeAlso());
            m_scroll->setWidget(new PersonalizationPage(sidebar));
        } else if (entry == kFontsPath) {
            auto *sidebar = buildSubpageSidebar(
                FontsPage::sidebarLinks(),
                FontsPage::sidebarSeeAlso());
            m_scroll->setWidget(new FontsPage(sidebar));
        } else if (entry == kUserAccountsPath) {
            auto *sidebar = buildSubpageSidebar(
                UserAccountsPage::sidebarLinks(),
                UserAccountsPage::sidebarSeeAlso());
            m_scroll->setWidget(new UserAccountsPage(sidebar));
        } else if (entry == kEaseOfAccessPath) {
            auto *sidebar = buildSubpageSidebar(
                EaseOfAccessPage::sidebarLinks(),
                EaseOfAccessPage::sidebarSeeAlso());
            m_scroll->setWidget(new EaseOfAccessPage(sidebar));
        } else if (entry == kDevicesPrintersPath) {
            // A shell folder, not a Control Panel item: no left-nav sidebar
            m_scroll->setWidget(new DevicesAndPrintersPage());
        }
    } else {
        m_scroll->setWidget(buildCategoryPage(entry));
    }
}

QWidget *View::buildHomePage()
{
    // Outer: full-width white, centers the inner column
    auto *content = new QWidget;
    content->setObjectName("homeContent");
    content->setStyleSheet(
        Aero::panelSheet(QStringLiteral("homeContent"),
                         Aero::Palette::Surface));
    auto *outerH = new QHBoxLayout(content);
    outerH->setContentsMargins(0, 0, 0, 0);
    outerH->setSpacing(0);

    // Inner column: sized to its content (the two-column grid), holds heading
    // row + grid. Centred in the white area like the real Control Panel
    auto *inner = new QWidget;
    inner->setStyleSheet("background: transparent;");
    inner->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    outerH->addStretch(1);
    outerH->addWidget(inner, 0, Qt::AlignTop);
    outerH->addStretch(1);

    auto *innerV = new QVBoxLayout(inner);
    innerV->setContentsMargins(0, 15, 0, 10);
    innerV->setSpacing(0);

    // Heading row: "Adjust your computer's settings"  |  View by: Category
    auto *headRow = new QHBoxLayout;
    headRow->setContentsMargins(0, 0, 0, 22);

    auto *settingsLabel = new QLabel("Adjust your computer's settings");
    Aero::setPointSize(settingsLabel, 12);
    settingsLabel->setStyleSheet(QStringLiteral("color: %1;")
        .arg(QLatin1String(Aero::Palette::CrumbHover)));
    headRow->addWidget(settingsLabel);
    headRow->addStretch();

    auto *viewByLabel = new QLabel("View by:");
    viewByLabel->setStyleSheet("color: #333333; font-size: 9pt;");
    headRow->addWidget(viewByLabel);

    // The Win7 "View by:" dropdown: blue text ending in the shared Aero arrow,
    // orange on hover, opening its menu on click
    auto *categoryBtn = new Win7::LinkMenuButton("Category");
    auto *viewMenu = new QMenu(categoryBtn);
    viewMenu->addAction("Category");
    viewMenu->addAction("Large icons");
    viewMenu->addAction("Small icons");
    categoryBtn->setMenu(viewMenu);
    headRow->addWidget(categoryBtn);

    innerV->addLayout(headRow);

    // Two independent columns. The real Control Panel stacks each column as its
    // own list with even gaps; it does NOT align rows across the two columns, so
    // a short item never gets padded to match a tall item beside it
    auto *columnsWidget = new QWidget;
    columnsWidget->setStyleSheet("background: transparent;");
    auto *columns = new QHBoxLayout(columnsWidget);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(28);

    auto *colLeft = new QVBoxLayout;
    colLeft->setContentsMargins(0, 0, 0, 0);
    colLeft->setSpacing(0);
    auto *colRight = new QVBoxLayout;
    colRight->setContentsMargins(0, 0, 0, 0);
    colRight->setSpacing(0);

    const QList<CategoryItem> &categories = homeCategories();
    for (int i = 0; i < categories.size(); ++i) {
        auto *category = new CategoryWidget(categories[i]);
        // Queued: the swap deletes this widget, so it must not run while the
        // widget's own click event is still on the call stack
        QObject::connect(category, &CategoryWidget::titleActivated,
                         this, &View::requestPath, Qt::QueuedConnection);
        // Task sub-links: known tasks deep-link to their dedicated page; any
        // other task falls back to its parent category page
        QObject::connect(category, &CategoryWidget::taskActivated, this,
            [this](const QString &categoryName, const QString &task) {
                static const QHash<QString, QString> knownTaskPaths = {
                    { "Uninstall a program",           kProgramsFeaturesPath },
                    { "View network status and tasks", kNetworkSharingPath },
                    { "Add or remove user accounts",   kUserAccountsPath },
                    { "Change the theme",              kPersonalizationPath },
                    { "Let Linux suggest settings",    kEaseOfAccessPath },
                    { "Optimize visual display",       kEaseOfAccessPath },
                };
                const auto it = knownTaskPaths.constFind(task);
                if (it != knownTaskPaths.constEnd())
                    requestPath(it.value());
                else
                    requestPath(categoryName);
            }, Qt::QueuedConnection);

        // Alternating between the two columns, left first
        if (i % 2 == 0)
            colLeft->addWidget(category);
        else
            colRight->addWidget(category);
    }
    colLeft->addStretch(1);
    colRight->addStretch(1);

    columns->addLayout(colLeft);
    columns->addLayout(colRight);

    innerV->addWidget(columnsWidget);
    innerV->addStretch(1);

    return content;
}

View::Sidebar View::buildSidebarShell(int initialWidth)
{
    auto *clip = new QScrollArea;
    clip->setFixedWidth(initialWidth);
    clip->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    clip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    clip->setFrameShape(QFrame::NoFrame);
    clip->setWidgetResizable(true);
    clip->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto *pane = new QFrame;
    pane->setObjectName("navPane");
    pane->setFixedWidth(195);
    pane->setStyleSheet(
        "#navPane { background: #F1F4F9; border-right: 1px solid #DCE0E8; }"
    );
    auto *outerV = new QVBoxLayout(pane);
    outerV->setContentsMargins(0, 0, 0, 0);
    outerV->setSpacing(0);

    // Text lives in a child widget so a fade effect touches only the text,
    // never the pane background
    auto *textWrap = new QWidget;
    textWrap->setStyleSheet("background: transparent;");
    auto *navV = new QVBoxLayout(textWrap);
    navV->setContentsMargins(12, 14, 8, 10);
    navV->setSpacing(0);
    outerV->addWidget(textWrap);

    auto *controlHome = new QPushButton("Control Panel Home");
    controlHome->setCursor(Qt::PointingHandCursor);
    controlHome->setFlat(true);
    controlHome->setStyleSheet(
        // padding-left 12 matches the category links' contentsMargins(12, …) so
        // "Control Panel Home" lines up with the rest of the sidebar options
        QStringLiteral("QPushButton { border: none; background: transparent; color: %1;"
        " text-align: left; padding: 0 0 0 12; font-size: 9pt; }"
        "QPushButton:hover { color: %2; }")
            .arg(QLatin1String(Aero::Palette::Text),
                 QLatin1String(Aero::Palette::LinkHover))
    );
    QObject::connect(controlHome, &QPushButton::clicked, this,
                     [this]() { requestPath(QString()); }, Qt::QueuedConnection);
    navV->addWidget(controlHome);
    navV->addSpacing(16);

    clip->setWidget(pane);
    return { clip, textWrap, navV };
}

QScrollArea *View::buildNavSidebar(const QString &currentCategory)
{
    // Width 0: buildCategoryPage animates the clip open to 195px
    Sidebar bar = buildSidebarShell(0);

    for (const QString &cat : navOrder()) {
        if (cat == currentCategory) {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 3, 0, 3);
            row->setSpacing(4);
            auto *bullet = new QLabel("•");
            bullet->setStyleSheet(
                QStringLiteral("color: %1; font-size: 9pt; background: transparent;")
                    .arg(QLatin1String(Aero::Palette::Text)));
            row->addWidget(bullet, 0, Qt::AlignTop);
            auto *label = new QLabel(cat);
            Aero::setPointSize(label, 9);
            Win7::setBold(label, true);
            label->setWordWrap(true);
            label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                .arg(QLatin1String(Aero::Palette::Text)));
            row->addWidget(label, 1);
            bar.navV->addLayout(row);
        } else {
            auto *link = new QLabel(cat);
            Aero::setPointSize(link, 9);
            link->setWordWrap(true);
            link->setCursor(Qt::PointingHandCursor);
            link->setContentsMargins(12, 3, 0, 3);
            link->setStyleSheet(
                QStringLiteral("QLabel { color: %1; background: transparent; }"
                "QLabel:hover { color: %2; }")
                    .arg(QLatin1String(Aero::Palette::Text),
                         QLatin1String(Aero::Palette::LinkHover))
            );
            link->installEventFilter(this);
            m_navLinks.insert(link, cat);
            bar.navV->addWidget(link);
        }
    }
    bar.navV->addStretch(1);

    // Static sidebar (shown at full opacity). The effect is kept so that
    // navigating into a subpage can fade this text out first
    m_sidebarTextEffect = new QGraphicsOpacityEffect(bar.textWrap);
    bar.textWrap->setGraphicsEffect(m_sidebarTextEffect);
    m_sidebarTextEffect->setOpacity(1.0);

    return bar.clip;
}

// Wire a sidebar label to the destination its link declares. Home and page
// targets share m_subpageLinks (an empty path means "Control Panel Home"),
// commands and applets go to their own maps, and a None target is left inert
void View::registerLinkTarget(QLabel *label, const LinkTarget &target)
{
    switch (target.kind) {
    case LinkTarget::Home:
        m_subpageLinks.insert(label, QString());
        break;
    case LinkTarget::Page:
        m_subpageLinks.insert(label, PageRegistry::pathFor(target.page));
        break;
    case LinkTarget::Command:
        m_commandLinks.insert(label, target.command);
        break;
    case LinkTarget::Applet:
        m_appletLinks.insert(label, target.applet);
        break;
    case LinkTarget::None:
        break;
    }
}

QScrollArea *View::buildSubpageSidebar(const QList<SidebarLink> &links,
                                              const QList<SidebarLink> &seeAlso)
{
    Sidebar bar = buildSidebarShell(195);

    auto addLink = [&](const SidebarLink &sl) {
        // Show the branded wording, but match routing on the canonical text
        auto *label = new QLabel(Branding::brand(sl.text));
        Aero::setPointSize(label, 9);
        label->setWordWrap(true);
        label->setCursor(Qt::PointingHandCursor);
        label->setContentsMargins(12, 3, 0, 3);
        label->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }"
            "QLabel:hover { color: %2; }")
                .arg(QLatin1String(Aero::Palette::Text),
                     QLatin1String(Aero::Palette::LinkHover))
        );
        label->installEventFilter(this);
        if (sl.text == "Check for updates")
            m_checkUpdatesLabel = label;
        // The link already carries its own destination, so there is no title
        // string to reverse-map: just wire the label to the target it declares
        registerLinkTarget(label, sl.target);
        bar.navV->addWidget(label);
    };

    for (const SidebarLink &sl : links)
        addLink(sl);

    bar.navV->addStretch(1);

    if (!seeAlso.isEmpty()) {
        auto *sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #B8C4D8;");
        bar.navV->addWidget(sep);
        bar.navV->addSpacing(4);

        auto *seeAlsoLabel = new QLabel("See also");
        Aero::setPointSize(seeAlsoLabel, 8);
        seeAlsoLabel->setContentsMargins(12, 0, 0, 0);
        seeAlsoLabel->setStyleSheet("color: #666666; background: transparent;");
        bar.navV->addWidget(seeAlsoLabel);

        for (const SidebarLink &sl : seeAlso)
            addLink(sl);
    }

    // Subpage sidebars are full width from the start and fade their text in
    auto *fadeEffect = new QGraphicsOpacityEffect(bar.textWrap);
    bar.textWrap->setGraphicsEffect(fadeEffect);
    fadeEffect->setOpacity(0.0);
    auto *fadeAnim = new QPropertyAnimation(fadeEffect, "opacity", bar.clip);
    fadeAnim->setStartValue(0.0);
    fadeAnim->setEndValue(1.0);
    fadeAnim->setDuration(2000);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    QTimer::singleShot(0, bar.clip, [fadeAnim]() { fadeAnim->start(); });

    return bar.clip;
}

QWidget *View::buildCategoryPage(const QString &currentCategory)
{
    auto *page = new QWidget;
    page->setObjectName("categoryPage");
    page->setStyleSheet(
        Aero::panelSheet(QStringLiteral("categoryPage"),
                         Aero::Palette::Surface));
    auto *root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *sidebarClip = buildNavSidebar(currentCategory);
    root->addWidget(sidebarClip);

    // Right content pane
    auto *contentWrap = new QWidget;
    contentWrap->setObjectName("categoryContent");
    contentWrap->setStyleSheet(
        Aero::panelSheet(QStringLiteral("categoryContent"),
                         Aero::Palette::Surface));
    auto *contentV = new QVBoxLayout(contentWrap);
    contentV->setContentsMargins(18, 14, 18, 14);
    contentV->setSpacing(0);

    auto makeSep = []() {
        auto *label = new QLabel("|");
        Aero::setPointSize(label, 9);
        label->setStyleSheet("color: #B0B0B0;");
        return label;
    };
    auto makeTask = [this](const QString &text) {
        // Show the branded wording, but match routing on the canonical text
        auto *label = new QLabel(Branding::brand(text));
        Aero::setPointSize(label, 9);
        label->setCursor(Qt::PointingHandCursor);
        label->setStyleSheet(
            QStringLiteral("QLabel { color: %1; }"
            "QLabel:hover { color: %2; }")
                .arg(QLatin1String(Aero::Palette::LinkText),
                     QLatin1String(Aero::Palette::LinkHover))
        );
        label->installEventFilter(this);
        // Known task links navigate to their page (others are decorative)
        if (text == "View installed updates")
            m_subpageLinks.insert(label, kInstalledUpdatesPath);
        else if (text == "Review your computer's status and resolve issues")
            m_subpageLinks.insert(label, kActionCenterPath);
        else if (text == "Check firewall status"
                 || text == "Allow a program through Linux Firewall")
            m_subpageLinks.insert(label, kFirewallPath);
        else if (text == "View network status and tasks")
            m_subpageLinks.insert(label, kNetworkSharingPath);
        else if (text == "Choose a power plan")
            m_subpageLinks.insert(label, kPowerOptionsPath);
        else if (text == "Uninstall a program")
            m_subpageLinks.insert(label, kProgramsFeaturesPath);
        else if (text == "Add gadgets to the desktop")
            m_commandLinks.insert(label, kWidgetExplorerCmd);
        else if (text == "Get more gadgets online")
            m_commandLinks.insert(label, kGetWidgetsCmd);
        else if (text == "Device Manager")
            m_commandLinks.insert(label, kDeviceManagerCmd);

        // Remaining task links: deep-links into the new detail pages, and
        // launchers that open the matching KDE settings module. Consulted only
        // when the explicit cases above didn't already claim this label.
        // Task links that open an in-app applet dialog (mirroring Windows,
        // which opens a dialog rather than a page for these)
        static const QHash<QString, QString> taskApplet = {
            { "Set the time and date",                       "datetime" },
            { "Change the time zone",                        "datetime" },
            { "Add clocks for different time zones",         "datetime:additional" },
            { "Manage audio devices",                        "sound" },
            { "Adjust system volume",                        "sound" },
            { "Change system sounds",                        "sound:sounds" },
            { "Change sound effects",                        "sound:sounds" },
            { "Specify single- or double-click to open",     "folderoptions" },
            { "Show hidden files and folders",               "folderoptions:view" },
        };
        static const QHash<QString, QString> taskNav = {
            { "Add a device",                                kDevicesPrintersPath },
            { "Add a printer",                               kDevicesPrintersPath },
            { "Change the theme",                            kPersonalizationPath },
            { "Preview, delete, or show and hide fonts",     kFontsPath },
            { "Change your account picture",                 kUserAccountsPath },
            { "Add or remove user accounts",                 kUserAccountsPath },
            { "Change your Linux Password",                  kUserAccountsPath },
            { "Let Linux suggest settings",                  kEaseOfAccessPath },
            { "Optimize visual display",                     kEaseOfAccessPath },
        };
        static const QHash<QString, QStringList> taskCmd = {
            { "Mouse",                                       kcm("kcm_mouse") },
            { "Change desktop background",                   kcm("kcm_wallpaper") },
            { "Change window glass colors",                  kcm("kcm_colors") },
            { "Change screen saver",                         kcm("kcm_screenlocker") },
            { "Adjust screen resolution",                    kcm("kcm_kscreen") },
            { "Make text and other items larger or smaller", kcm("kcm_kscreen") },
            { "Connect to an external display",              kcm("kcm_kscreen") },
            { "Change Font Settings",                        kcm("kcm_fonts") },
            { "Adjust ClearType text",                       kcm("kcm_fonts") },
            { "Set your default programs",                   kcm("kcm_componentchooser") },
            { "Make a file type always open in a specific program",
              kcm("kcm_filetypes") },
            { "Change keyboards or other input methods",     kcm("kcm_keyboard") },
            { "Change display language",                     kcm("kcm_regionandlang") },
            { "Install or uninstall display languages",      kcm("kcm_regionandlang") },
            { "Change the date, time, or number format",     kcm("kcm_regionandlang") },
            { "Change location",                             kcm("kcm_regionandlang") },
            { "Manage Linux Credentials",                    kcm("kcm_kwallet5") },
            { "Change how your mouse works",                 kcm("kcm_mouse") },
            { "Change how your keyboard works",              kcm("kcm_access") },
            { "Start speech recognition",                    kcm("kcm_access") },
            { "Set up a microphone",                         kcm("kcm_pulseaudio") },
        };
        if (!m_subpageLinks.contains(label) && !m_commandLinks.contains(label)) {
            const auto appIt = taskApplet.constFind(text);
            if (appIt != taskApplet.constEnd())
                m_appletLinks.insert(label, appIt.value());
            else {
                const auto navIt = taskNav.constFind(text);
                if (navIt != taskNav.constEnd())
                    m_subpageLinks.insert(label, navIt.value());
                else {
                    const auto cmdIt = taskCmd.constFind(text);
                    if (cmdIt != taskCmd.constEnd())
                        m_commandLinks.insert(label, cmdIt.value());
                }
            }
        }
        return label;
    };

    for (const DetailGroup &group : *detailGroupsFor(currentCategory)) {
        auto *groupRow = new QHBoxLayout;
        groupRow->setContentsMargins(0, 0, 0, 12);
        groupRow->setSpacing(12);

        auto *iconLabel = new QLabel;
        iconLabel->setPixmap(resolveIcon(group.iconName).pixmap(32, 32));
        iconLabel->setFixedSize(32, 32);
        groupRow->addWidget(iconLabel, 0, Qt::AlignTop);

        auto *textBlock = new QVBoxLayout;
        textBlock->setContentsMargins(0, 0, 0, 0);
        textBlock->setSpacing(2);

        auto *title = new QLabel(Branding::brand(group.title));
        Aero::setPointSize(title, 11);
        title->setCursor(Qt::PointingHandCursor);
        title->setStyleSheet(
            "QLabel { color: #1E7B1E; }"
            "QLabel:hover { color: #145214; }"
        );
        title->installEventFilter(this);

        // Known subpages: register the title label so clicking navigates there
        static const QHash<QString, QString> knownSubpages = {
            { "Linux Update",              "System and Security/Linux Update" },
            { "Action Center",             "System and Security/Action Center" },
            { "Linux Firewall",            "System and Security/Linux Firewall" },
            { "System",                    "System and Security/System" },
            { "Programs and Features",     "Programs/Programs and Features" },
            { "Power Options",             "System and Security/Power Options" },
            { "Network and Sharing Center",
              "Network and Internet/Network and Sharing Center" },
            { "Personalization",           kPersonalizationPath },
            { "Fonts",                     kFontsPath },
            { "User Accounts",             kUserAccountsPath },
            { "Ease of Access Center",     kEaseOfAccessPath },
            { "Devices and Printers",      kDevicesPrintersPath },
        };
        if (knownSubpages.contains(group.title))
            m_subpageLinks.insert(title, knownSubpages.value(group.title));
        else if (group.title == "Desktop Gadgets")
            m_commandLinks.insert(title, kWidgetExplorerCmd);
        else if (group.title == "Date and Time")
            m_appletLinks.insert(title, QStringLiteral("datetime"));
        else if (group.title == "Sound")
            m_appletLinks.insert(title, QStringLiteral("sound"));
        else if (group.title == "Folder Options")
            m_appletLinks.insert(title, QStringLiteral("folderoptions"));

        textBlock->addWidget(title);

        for (const QStringList &line : group.lines) {
            auto *lineRow = new QHBoxLayout;
            lineRow->setContentsMargins(0, 0, 0, 0);
            lineRow->setSpacing(6);
            for (int i = 0; i < line.size(); ++i) {
                if (i > 0)
                    lineRow->addWidget(makeSep());
                lineRow->addWidget(makeTask(line[i]));
            }
            lineRow->addStretch(1);
            textBlock->addLayout(lineRow);
        }

        groupRow->addLayout(textBlock, 1);
        contentV->addLayout(groupRow);
    }

    contentV->addStretch(1);
    root->addWidget(contentWrap, 1);

    // sidebarClip slides from 0 to 195; sidebar inside stays full-width so text
    // never reflows. The content pane rides rightward naturally as the clip grows
    auto *sidebarAnim = new QVariantAnimation(page);
    sidebarAnim->setStartValue(0);
    sidebarAnim->setEndValue(195);
    sidebarAnim->setDuration(250);
    sidebarAnim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(sidebarAnim, &QVariantAnimation::valueChanged, sidebarClip,
        [sidebarClip](const QVariant &v) { sidebarClip->setFixedWidth(v.toInt()); });

    // Content fades in over the same span
    auto *fadeEffect = new QGraphicsOpacityEffect(contentWrap);
    contentWrap->setGraphicsEffect(fadeEffect);
    fadeEffect->setOpacity(0.0);
    auto *contentAnim = new QPropertyAnimation(fadeEffect, "opacity", page);
    contentAnim->setStartValue(0.0);
    contentAnim->setEndValue(1.0);
    contentAnim->setDuration(260);
    contentAnim->setEasingCurve(QEasingCurve::OutCubic);

    QTimer::singleShot(0, page, [sidebarAnim, contentAnim]() {
        sidebarAnim->start();
        contentAnim->start();
    });

    return page;
}

} // namespace ControlPanel
