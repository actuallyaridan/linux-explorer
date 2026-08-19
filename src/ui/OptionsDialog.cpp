#include "OptionsDialog.h"
#include "Branding.h"
#include "aero/icons.h"
#include "Settings.h"
#include "aero/text.h"
#include "aero/palette.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Win7's folder options does not resize, and this is its size
constexpr int kDialogWidth = 420;
constexpr int kDialogHeight = 470;

// The indent Win7 gives the row at the top of each group box
constexpr int kGroupIndent = 4;

// Qt's default spacing crowds both the title and the frame
QGroupBox *group(const QString &title)
{
    auto *box = new QGroupBox(title);
    Aero::setPointSize(box, 9);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);
    return box;
}

QRadioButton *radio(const QString &text)
{
    auto *button = new QRadioButton(text);
    Aero::setPointSize(button, 9);
    return button;
}

QCheckBox *check(const QString &text)
{
    auto *box = new QCheckBox(text);
    Aero::setPointSize(box, 9);
    return box;
}

// Decorative, and skipped silently when the theme has no such icon
QWidget *illustrated(const QIcon &icon, QWidget *content)
{
    if (icon.isNull())
        return content;

    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(kGroupIndent, 0, 0, 0);
    layout->setSpacing(10);

    auto *label = new QLabel;
    label->setPixmap(icon.pixmap(32, 32));
    label->setFixedSize(32, 32);
    label->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    layout->addWidget(label, 0, Qt::AlignTop);
    layout->addWidget(content, 1);
    return row;
}

} // namespace

OptionsDialog::OptionsDialog(QWidget *parent)
    : QDialog(parent)
{
    // Not folder options, this carrying the naming switches too
    setWindowTitle(tr("Options"));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_tabs = new QTabWidget;
    Aero::setPointSize(m_tabs, 9);
    m_tabs->addTab(buildGeneralTab(), tr("General"));
    m_tabs->addTab(buildViewTab(), tr("View"));
    m_tabs->addTab(buildSearchTab(), tr("Search"));
    root->addWidget(m_tabs, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(6);
    buttons->addStretch(1);

    auto *ok = new QPushButton(tr("OK"));
    auto *cancel = new QPushButton(tr("Cancel"));
    m_apply = new QPushButton(tr("Apply"));
    ok->setDefault(true);
    // Win7 starts with Apply greyed and enables it once something is touched
    m_apply->setEnabled(false);
    for (QPushButton *button : {ok, cancel, m_apply}) {
        Aero::setPointSize(button, 9);
        button->setMinimumWidth(80);
        buttons->addWidget(button);
    }
    root->addLayout(buttons);

    connect(ok, &QPushButton::clicked, this, [this] {
        save();
        Q_EMIT applied();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, [this] {
        save();
        Q_EMIT applied();
        m_apply->setEnabled(false);
    });

    load();

    // After loading, so filling the controls in does not count as a change
    const auto touched = [this] { m_apply->setEnabled(true); };
    for (QRadioButton *button : {m_sameWindow, m_ownWindow, m_singleClick,
                                 m_doubleClick, m_namesOnly, m_namesAndContents}) {
        connect(button, &QRadioButton::toggled, this, touched);
    }
    connect(m_includeSubfolders, &QCheckBox::toggled, this, touched);
    connect(m_advanced, &QTreeWidget::itemChanged, this, touched);

    // The larger of Windows' size and what the layout needs, since a wider font
    // or a longer translation would otherwise clip a label with no way to see it
    setFixedSize(qMax(kDialogWidth, sizeHint().width()),
                 qMax(kDialogHeight, sizeHint().height()));
}

QWidget *OptionsDialog::buildGeneralTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QGroupBox *browse = group(tr("Browse folders"));
    m_sameWindow = radio(tr("Open each folder in the same window"));
    m_ownWindow = radio(tr("Open each folder in its own window"));
    auto *browseChoices = new QWidget;
    auto *browseLayout = new QVBoxLayout(browseChoices);
    browseLayout->setContentsMargins(0, 0, 0, 0);
    browseLayout->setSpacing(4);
    browseLayout->addWidget(m_sameWindow);
    browseLayout->addWidget(m_ownWindow);
    browse->layout()->addWidget(
        illustrated(Aero::themeIcon({"folder-open", "folder"}), browseChoices));
    layout->addWidget(browse);

    QGroupBox *click = group(tr("Click items as follows"));
    m_singleClick = radio(tr("Single-click to open an item"));
    m_doubleClick = radio(tr("Double-click to open an item (single-click to select)"));
    auto *clickChoices = new QWidget;
    auto *clickLayout = new QVBoxLayout(clickChoices);
    clickLayout->setContentsMargins(0, 0, 0, 0);
    clickLayout->setSpacing(4);
    clickLayout->addWidget(m_singleClick);
    clickLayout->addWidget(m_doubleClick);
    click->layout()->addWidget(
        illustrated(Aero::themeIcon({"input-mouse", "mouse"}), clickChoices));
    layout->addWidget(click);

    layout->addStretch(1);

    auto *restore = new QPushButton(tr("Restore Defaults"));
    Aero::setPointSize(restore, 9);
    connect(restore, &QPushButton::clicked, this, &OptionsDialog::restoreDefaults);
    auto *restoreRow = new QHBoxLayout;
    restoreRow->addStretch(1);
    restoreRow->addWidget(restore);
    layout->addLayout(restoreRow);

    return page;
}

QTreeWidgetItem *OptionsDialog::addCheck(QTreeWidgetItem *parent,
                                         const QString &text,
                                         const QString &tooltip)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, text);
    item->setCheckState(0, Qt::Unchecked);
    if (!tooltip.isEmpty())
        item->setToolTip(0, tooltip);
    return item;
}

void OptionsDialog::addRadioPair(QTreeWidgetItem *parent, const QString &offText,
                                 const QString &onText, QTreeWidgetItem **offItem,
                                 QTreeWidgetItem **onItem)
{
    *offItem = addCheck(parent, offText);
    *onItem = addCheck(parent, onText);
}

QWidget *OptionsDialog::buildViewTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QGroupBox *views = group(tr("Folder views"));
    auto *viewsText = new QLabel(
        tr("You can apply the view (such as Details or Icons) that you are "
           "using for this folder to all folders of this type."));
    viewsText->setWordWrap(true);
    Aero::setPointSize(viewsText, 9);

    auto *viewsButtons = new QHBoxLayout;
    viewsButtons->setContentsMargins(0, 6, 0, 0);
    viewsButtons->setSpacing(8);
    auto *applyToFolders = new QPushButton(tr("Apply to Folders"));
    auto *resetFolders = new QPushButton(tr("Reset Folders"));
    for (QPushButton *button : {applyToFolders, resetFolders}) {
        Aero::setPointSize(button, 9);
        button->setMinimumWidth(120);
        viewsButtons->addWidget(button);
    }
    viewsButtons->addStretch(1);

    auto *viewsContent = new QWidget;
    auto *viewsLayout = new QVBoxLayout(viewsContent);
    viewsLayout->setContentsMargins(0, 0, 0, 0);
    viewsLayout->setSpacing(2);
    viewsLayout->addWidget(viewsText);
    viewsLayout->addLayout(viewsButtons);
    views->layout()->addWidget(
        illustrated(Aero::themeIcon({"view-list-details", "folder"}), viewsContent));
    layout->addWidget(views);

    // Immediate rather than on Apply, these being operations on the stored view
    // modes with nothing to cancel back to
    connect(applyToFolders, &QPushButton::clicked, this,
            &OptionsDialog::applyViewToAllFolders);
    connect(resetFolders, &QPushButton::clicked, this,
            &OptionsDialog::resetAllFolders);

    layout->addWidget(Aero::label(tr("Advanced settings:"), 9));

    m_advanced = new QTreeWidget;
    m_advanced->setColumnCount(1);
    m_advanced->setHeaderHidden(true);
    m_advanced->setRootIsDecorated(false);
    m_advanced->setIndentation(16);
    m_advanced->setUniformRowHeights(true);
    m_advanced->setSelectionMode(QAbstractItemView::NoSelection);
    m_advanced->setEditTriggers(QAbstractItemView::NoEditTriggers);
    Aero::setPointSize(m_advanced, 9);

    auto *files = new QTreeWidgetItem(m_advanced);
    files->setText(0, tr("Files and Folders"));
    files->setIcon(0, Aero::themeIcon({"folder"}));
    files->setFlags(Qt::ItemIsEnabled);

    m_alwaysShowMenus = addCheck(files, tr("Always show menus"),
        tr("Keeps the classic menu bar on screen instead of only while Alt "
           "is held."));
    m_hideExtensions = addCheck(files, tr("Hide extensions for known file types"));
    m_checkBoxes = addCheck(files, tr("Use check boxes to select items"),
        tr("Puts a tick box on each item so several can be picked without "
           "holding Ctrl."));

    auto *hidden = new QTreeWidgetItem(files);
    hidden->setText(0, tr("Hidden files and folders"));
    hidden->setIcon(0, Aero::themeIcon({"folder"}));
    hidden->setFlags(Qt::ItemIsEnabled);
    addRadioPair(hidden, tr("Don't show hidden files, folders, or drives"),
                 tr("Show hidden files, folders, and drives"),
                 &m_hiddenOff, &m_hiddenOn);

    // Ours rather than Windows', so it gets its own heading
    auto *naming = new QTreeWidgetItem(m_advanced);
    naming->setText(0, tr("Windows-friendly naming"));
    naming->setIcon(0, Aero::themeIcon({"preferences-desktop-locale", "folder"}));
    naming->setFlags(Qt::ItemIsEnabled);

    m_friendlyMode = addCheck(naming, tr("Show system folders under Windows names"),
        tr("Shows Linux system folders under the names their Windows "
           "counterparts use, so /home reads as Users. Only the displayed "
           "name changes; every path stays what it really is."));
    m_windowsNames = addCheck(naming, tr("Name the system folder after Windows"),
        tr("Names the configuration folder Windows rather than Linux. Only "
           "meaningful with the setting above on."));

    m_advanced->expandAll();
    layout->addWidget(m_advanced, 1);

    // The exclusivity the tree cannot express, and the dependency between the
    // two naming rows
    connect(m_advanced, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int) {
        QSignalBlocker blocker(m_advanced);
        if (item == m_hiddenOff && item->checkState(0) == Qt::Checked)
            m_hiddenOn->setCheckState(0, Qt::Unchecked);
        else if (item == m_hiddenOn && item->checkState(0) == Qt::Checked)
            m_hiddenOff->setCheckState(0, Qt::Unchecked);
        else if (item == m_hiddenOff || item == m_hiddenOn) {
            // Neither set is not a state the setting has
            if (m_hiddenOff->checkState(0) == Qt::Unchecked
                && m_hiddenOn->checkState(0) == Qt::Unchecked) {
                item->setCheckState(0, Qt::Checked);
            }
        }

        if (item == m_friendlyMode) {
            const bool on = item->checkState(0) == Qt::Checked;
            m_windowsNames->setDisabled(!on);
            if (!on)
                m_windowsNames->setCheckState(0, Qt::Unchecked);
        }
    });

    auto *restore = new QPushButton(tr("Restore Defaults"));
    Aero::setPointSize(restore, 9);
    connect(restore, &QPushButton::clicked, this, &OptionsDialog::restoreDefaults);
    auto *restoreRow = new QHBoxLayout;
    restoreRow->addStretch(1);
    restoreRow->addWidget(restore);
    layout->addLayout(restoreRow);

    return page;
}

QWidget *OptionsDialog::buildSearchTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QGroupBox *what = group(tr("What to search"));
    m_namesOnly = radio(tr("Search file names only"));
    m_namesAndContents = radio(tr("Search file names and contents"));
    // Windows had an indexer, and without one the cost is stated where the
    // choice is made
    auto *cost = new QLabel(
        tr("Searching contents reads every file it walks past, so it can take "
           "several minutes over a large folder."));
    cost->setWordWrap(true);
    Aero::setPointSize(cost, 9);
    cost->setStyleSheet(QStringLiteral("color: %1;")
                        .arg(QLatin1String(Aero::Palette::MutedText)));

    auto *whatChoices = new QWidget;
    auto *whatLayout = new QVBoxLayout(whatChoices);
    whatLayout->setContentsMargins(0, 0, 0, 0);
    whatLayout->setSpacing(4);
    whatLayout->addWidget(m_namesOnly);
    whatLayout->addWidget(m_namesAndContents);
    whatLayout->addWidget(cost);
    what->layout()->addWidget(
        illustrated(Aero::themeIcon({"system-search", "edit-find"}), whatChoices));
    layout->addWidget(what);

    QGroupBox *how = group(tr("How to search"));
    m_includeSubfolders = check(tr("Include subfolders in search results"));
    m_includeSubfolders->setToolTip(
        tr("With this off, Enter in the search box leaves the current folder "
           "filtered instead of searching below it."));
    how->layout()->addWidget(m_includeSubfolders);
    layout->addWidget(how);

    layout->addStretch(1);

    auto *restore = new QPushButton(tr("Restore Defaults"));
    Aero::setPointSize(restore, 9);
    connect(restore, &QPushButton::clicked, this, &OptionsDialog::restoreDefaults);
    auto *restoreRow = new QHBoxLayout;
    restoreRow->addStretch(1);
    restoreRow->addWidget(restore);
    layout->addLayout(restoreRow);

    return page;
}

void OptionsDialog::load()
{
    QSignalBlocker blocker(m_advanced);

    const bool separate = Settings::browseInNewWindow();
    m_sameWindow->setChecked(!separate);
    m_ownWindow->setChecked(separate);

    const bool single = Settings::singleClickToOpen();
    m_singleClick->setChecked(single);
    m_doubleClick->setChecked(!single);

    const auto state = [](bool on) { return on ? Qt::Checked : Qt::Unchecked; };
    const bool showHidden = Settings::showHiddenFiles();
    m_hiddenOn->setCheckState(0, state(showHidden));
    m_hiddenOff->setCheckState(0, state(!showHidden));
    m_hideExtensions->setCheckState(0, state(Settings::hideKnownExtensions()));
    m_checkBoxes->setCheckState(0, state(Settings::useCheckBoxes()));
    m_alwaysShowMenus->setCheckState(0, state(Settings::alwaysShowMenus()));

    const bool friendly = Branding::windowsFriendlyMode();
    m_friendlyMode->setCheckState(0, state(friendly));
    m_windowsNames->setCheckState(0, state(Branding::useWindowsNames()));
    m_windowsNames->setDisabled(!friendly);

    const bool contents = Settings::searchFileContents();
    m_namesAndContents->setChecked(contents);
    m_namesOnly->setChecked(!contents);
    m_includeSubfolders->setChecked(Settings::searchSubfolders());
}

void OptionsDialog::save()
{
    Settings::setBrowseInNewWindow(m_ownWindow->isChecked());
    Settings::setSingleClickToOpen(m_singleClick->isChecked());

    const auto checked = [](QTreeWidgetItem *item) {
        return item->checkState(0) == Qt::Checked;
    };
    Settings::setShowHiddenFiles(checked(m_hiddenOn));
    Settings::setHideKnownExtensions(checked(m_hideExtensions));
    Settings::setUseCheckBoxes(checked(m_checkBoxes));
    Settings::setAlwaysShowMenus(checked(m_alwaysShowMenus));

    Branding::setWindowsFriendlyMode(checked(m_friendlyMode));
    Branding::setUseWindowsNames(checked(m_windowsNames));

    Settings::setSearchFileContents(m_namesAndContents->isChecked());
    Settings::setSearchSubfolders(m_includeSubfolders->isChecked());
}

void OptionsDialog::restoreDefaults()
{
    QSignalBlocker blocker(m_advanced);

    // Per tab, as Windows does it, and nothing is written so Cancel still undoes
    switch (m_tabs->currentIndex()) {
    case 0:
        m_sameWindow->setChecked(true);
        m_doubleClick->setChecked(true);
        break;

    case 1:
        // This project's defaults rather than Windows' own
        m_hiddenOff->setCheckState(0, Qt::Checked);
        m_hiddenOn->setCheckState(0, Qt::Unchecked);
        m_hideExtensions->setCheckState(0, Qt::Unchecked);
        m_checkBoxes->setCheckState(0, Qt::Unchecked);
        m_alwaysShowMenus->setCheckState(0, Qt::Unchecked);
        m_friendlyMode->setCheckState(0, Qt::Unchecked);
        m_windowsNames->setCheckState(0, Qt::Unchecked);
        m_windowsNames->setDisabled(true);
        break;

    case 2:
        m_namesOnly->setChecked(true);
        m_includeSubfolders->setChecked(true);
        break;

    default:
        break;
    }

    m_apply->setEnabled(true);
}
