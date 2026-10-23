#include "DevicesAndPrintersPage.h"
#include "DevicePropertiesDialog.h"
#include "IconHelper.h"
#include "LinkLabel.h"
#include "Win7Ui.h"

#include "SysfsScanner.h"
#include "DeviceUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFont>
#include <QSysInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>

namespace {

constexpr int kTileIcon = 40;
constexpr QSize kTileSize(96, 76);
constexpr int kRoleIndex   = Qt::UserRole;      // index into m_devices/m_printers
constexpr int kRolePrinter = Qt::UserRole + 1;  // bool: is this a printer tile?

// Source scanner category -> {Win7 category label, icon}. The order here is the
// order tiles appear under "Devices". Only these categories surface in the
// Devices and Printers folder, internal parts (CPUs, controllers, disks…) do
// not, matching Windows.
const QVector<QPair<QString, QPair<QString, QString>>> kDeviceCatOrder = {
    { "Monitors",                        { "Monitor",         "video-display" } },
    { "Mice and other pointing devices", { "Mouse",           "input-mouse" } },
    { "Keyboards",                       { "Keyboard",        "input-keyboard" } },
    { "Portable Devices",                { "Portable Device", "smartphone" } },
    { "Universal Serial Bus devices",    { "USB device",      "drive-removable-media-usb" } },
    { "Game controllers",                { "Game controller", "input-gaming" } },
};

// A command-bar link: black by default, blue underline on hover (the Explorer
// toolbar style), reusing LinkLabel's click plumbing.
LinkLabel *commandLink(const QString &text) {
    auto *l = new LinkLabel(text);
    l->setStyleSheet(
        "QLabel { color: #1A1A1A; background: transparent; }"
        "QLabel:hover { color: #0033AA; }");
    return l;
}

} // namespace

DevicesAndPrintersPage::ScanResult DevicesAndPrintersPage::runScan() {
    ScanResult r;
    r.hostName = QSysInfo::machineHostName().toUpper();
    if (r.hostName.isEmpty())
        r.hostName = QStringLiteral("LOCALHOST");
    r.categories = scanDevices();
    r.printers   = scanPrinters();
    return r;
}

DevicesAndPrintersPage::DevicesAndPrintersPage(QWidget *parent)
    : QWidget(parent) {
    // ID-scoped: a declaration-only sheet would act as `* { ... }` and drag
    // the icon lists' scroll bars into non-native stylesheet rendering.
    setObjectName("dpPage");
    setStyleSheet("#dpPage { background: #FFFFFF; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Command bar (the shared ribbon strip) ---------------------------
    QHBoxLayout *cmdLayout = nullptr;
    auto *cmdBar = Win7::commandBar(&cmdLayout);
    cmdLayout->setContentsMargins(12, 0, 12, 0);
    cmdLayout->setSpacing(16);
    auto *addDevice = commandLink("Add a device");
    auto *addPrinter = commandLink("Add a printer");
    connect(addDevice,  &LinkLabel::clicked, this, &DevicesAndPrintersPage::launchAddDevice);
    connect(addPrinter, &LinkLabel::clicked, this, &DevicesAndPrintersPage::launchAddPrinter);
    cmdLayout->addWidget(addDevice);
    cmdLayout->addWidget(addPrinter);
    cmdLayout->addStretch(1);
    Win7::addCommandBarIcons(cmdLayout);
    root->addWidget(cmdBar);

    // ---- Grouped icon view ----------------------------------------------
    auto *content = new QWidget;
    auto *contentV = new QVBoxLayout(content);
    contentV->setContentsMargins(14, 10, 14, 6);
    contentV->setSpacing(2);

    auto makeHeader = [](const QString &text) {
        auto *h = new QLabel(text);
        QFont f = h->font();
        f.setPointSize(9);
        h->setFont(f);
        h->setStyleSheet(
            "color: #1F5C99; background: transparent;"
            " border-bottom: 1px solid #E2E6EC; padding: 2px 0;");
        return h;
    };

    auto makeList = [this]() {
        auto *list = new QListWidget;
        list->setViewMode(QListView::IconMode);
        list->setFlow(QListView::LeftToRight);
        list->setWrapping(true);
        list->setResizeMode(QListView::Adjust);
        list->setMovement(QListView::Static);
        list->setUniformItemSizes(true);
        list->setIconSize(QSize(kTileIcon, kTileIcon));
        list->setGridSize(kTileSize);
        list->setSpacing(6);
        list->setWordWrap(true);
        list->setFocusPolicy(Qt::NoFocus);
        list->setFrameShape(QFrame::NoFrame);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setStyleSheet(
            "QListWidget { background: #FFFFFF; outline: none; }"
            "QListWidget::item { color: #000000; padding: 3px; border-radius: 3px; }"
            "QListWidget::item:selected {"
            " background: #CDE6F7; border: 1px solid #A6D2F0; color: #000000; }"
            "QListWidget::item:hover:!selected {"
            " background: #EAF4FC; border: 1px solid #CFE9FB; }");
        return list;
    };

    m_devicesHeader = makeHeader("Devices");
    contentV->addWidget(m_devicesHeader);
    m_devicesList = makeList();
    contentV->addWidget(m_devicesList, 1);

    contentV->addSpacing(6);

    m_printersHeader = makeHeader("Printers and Faxes");
    contentV->addWidget(m_printersHeader);
    m_printersList = makeList();
    m_printersList->setFixedHeight(kTileSize.height() + 24);
    contentV->addWidget(m_printersList);

    m_statusLabel = new QLabel("Scanning for devices…");
    m_statusLabel->setStyleSheet("color: #666666; background: transparent;");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    contentV->insertWidget(1, m_statusLabel);

    root->addWidget(content, 1);

    // ---- Details pane (the shared status strip) --------------------------
    QHBoxLayout *dl = nullptr;
    auto *details = Win7::statusPanel(64, &dl);
    dl->setContentsMargins(14, 8, 14, 8);
    dl->setSpacing(12);

    m_detailIcon = new QLabel;
    m_detailIcon->setFixedSize(48, 48);
    m_detailIcon->setAlignment(Qt::AlignCenter);
    dl->addWidget(m_detailIcon, 0, Qt::AlignVCenter);

    auto *textV = new QVBoxLayout;
    textV->setContentsMargins(0, 0, 0, 0);
    textV->setSpacing(1);
    m_detailName = new QLabel;
    QFont nf = m_detailName->font();
    nf.setBold(true);
    m_detailName->setFont(nf);
    m_detailName->setStyleSheet("background: transparent;");
    m_detailLine1 = new QLabel;
    m_detailLine1->setStyleSheet("color: #444444; background: transparent;");
    m_detailLine2 = new QLabel;
    m_detailLine2->setStyleSheet("color: #444444; background: transparent;");
    textV->addWidget(m_detailName);
    textV->addWidget(m_detailLine1);
    textV->addWidget(m_detailLine2);
    textV->addStretch(1);
    dl->addLayout(textV, 1);

    root->addWidget(details);

    // Selection in one list clears the other, so there is a single global
    // selection driving one details pane.
    connect(m_devicesList, &QListWidget::itemSelectionChanged, this,
            [this] { onSelection(m_devicesList, m_printersList); });
    connect(m_printersList, &QListWidget::itemSelectionChanged, this,
            [this] { onSelection(m_printersList, m_devicesList); });

    auto openFromItem = [this](QListWidgetItem *item) {
        if (!item) return;
        const bool printer = item->data(kRolePrinter).toBool();
        const int idx = item->data(kRoleIndex).toInt();
        const QVector<ShellDevice> &src = printer ? m_printers : m_devices;
        if (idx >= 0 && idx < src.size()) {
            DevicePropertiesDialog dlg(src[idx], this);
            dlg.exec();
        }
    };
    connect(m_devicesList,  &QListWidget::itemActivated,     this, openFromItem);
    connect(m_devicesList,  &QListWidget::itemDoubleClicked, this, openFromItem);
    connect(m_printersList, &QListWidget::itemActivated,     this, openFromItem);
    connect(m_printersList, &QListWidget::itemDoubleClicked, this, openFromItem);

    updateDetails(nullptr);

    // ---- Kick off the background scan -----------------------------------
    Win7::runAsync(this, &DevicesAndPrintersPage::runScan,
                   [this](const ScanResult &result) { populate(result); });
}

void DevicesAndPrintersPage::populate(const ScanResult &result) {
    // ---- Curate hardware into the "Devices" group -----------------------
    m_devices.clear();

    // The computer itself is always the first device.
    ShellDevice pc;
    pc.name = result.hostName;
    pc.iconName = "computer";
    pc.category = "Computer";
    QString vendor = readSysFile("/sys/class/dmi/id/sys_vendor");
    if (vendor.isEmpty())
        vendor = readSysFile("/sys/class/dmi/id/bios_vendor");
    pc.manufacturer = vendor;
    pc.model = readSysFile("/sys/class/dmi/id/product_name");
    pc.modelNumber = readSysFile("/sys/class/dmi/id/product_version");
    pc.status = "This device is working properly.";
    pc.isComputer = true;
    m_devices.append(pc);

    for (const auto &entry : kDeviceCatOrder) {
        const QString &srcName = entry.first;
        const QString &label   = entry.second.first;
        const QString &icon    = entry.second.second;
        for (const DeviceCategory &cat : result.categories) {
            if (cat.name != srcName)
                continue;
            for (const Device &dev : cat.devices) {
                ShellDevice sd;
                sd.name = dev.name;
                sd.iconName = dev.iconName.isEmpty() ? icon : dev.iconName;
                sd.category = label;
                sd.manufacturer = dev.manufacturer;
                sd.model = dev.name;
                sd.status = dev.disabled ? "This device is disabled."
                                         : "This device is working properly.";
                sd.location = dev.location;
                sd.driver = dev.driver;
                sd.driverVersion = dev.driverVersion;
                sd.driverDate = dev.driverDate;
                sd.rawLocation = dev.rawLocation;
                m_devices.append(sd);
            }
        }
    }

    // ---- Printers and faxes ---------------------------------------------
    m_printers.clear();
    for (const Printer &p : result.printers) {
        ShellDevice sd;
        sd.name = p.info.isEmpty() ? p.name : p.info;
        sd.iconName = p.isFax ? "printer" : "printer";
        sd.category = p.isFax ? "Fax" : (p.isClass ? "Printer class" : "Printer");
        // The make-and-model string leads with the manufacturer, e.g.
        // "HP LaserJet 4000 Foomatic/Postscript".
        sd.manufacturer = p.makeAndModel.section(' ', 0, 0);
        sd.model = p.makeAndModel;
        sd.status = p.state.isEmpty() ? QStringLiteral("Ready") : p.state;
        sd.location = p.location;
        sd.isPrinter = true;
        sd.isDefaultPrinter = p.isDefault;
        m_printers.append(sd);
    }

    // ---- Render ----------------------------------------------------------
    addTiles(m_devicesList, m_devices, false);
    addTiles(m_printersList, m_printers, true);

    m_devicesHeader->setText(QString("Devices (%1)").arg(m_devices.size()));
    m_printersHeader->setText(
        QString("Printers and Faxes (%1)").arg(m_printers.size()));

    m_statusLabel->hide();
    updateDetails(nullptr);
}

void DevicesAndPrintersPage::addTiles(QListWidget *list,
                                      const QVector<ShellDevice> &items,
                                      bool printers) {
    list->clear();
    for (int i = 0; i < items.size(); ++i) {
        const ShellDevice &d = items[i];
        QString label = d.name;
        if (d.isDefaultPrinter)
            label += "\n(Default)";
        auto *item = new QListWidgetItem(resolveIcon(d.iconName), label, list);
        item->setToolTip(d.name);
        item->setSizeHint(kTileSize);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setData(kRoleIndex, i);
        item->setData(kRolePrinter, printers);
    }
}

void DevicesAndPrintersPage::onSelection(QListWidget *active, QListWidget *other) {
    if (active->selectedItems().isEmpty())
        return;
    // Clear the sibling list without recursing back into this slot.
    QSignalBlocker block(other);
    other->clearSelection();
    other->setCurrentItem(nullptr);

    QListWidgetItem *item = active->selectedItems().first();
    const bool printer = item->data(kRolePrinter).toBool();
    const int idx = item->data(kRoleIndex).toInt();
    const QVector<ShellDevice> &src = printer ? m_printers : m_devices;
    if (idx >= 0 && idx < src.size())
        updateDetails(&src[idx]);
}

void DevicesAndPrintersPage::updateDetails(const ShellDevice *dev) {
    if (!dev) {
        const int total = m_devices.size() + m_printers.size();
        m_detailIcon->setPixmap(resolveIcon("computer").pixmap(48, 48));
        m_detailName->setText(total > 0 ? QString("%1 items").arg(total)
                                        : QString());
        m_detailLine1->clear();
        m_detailLine2->clear();
        m_detailLine1->setText("Select an item to view its details.");
        return;
    }

    m_detailIcon->setPixmap(resolveIcon(dev->iconName).pixmap(48, 48));
    m_detailName->setText(dev->name);
    if (dev->isPrinter) {
        m_detailLine1->setText("Status: " + dev->status);
        m_detailLine2->setText("Category: " + dev->category);
    } else {
        const QString model = dev->model.isEmpty() ? dev->name : dev->model;
        m_detailLine1->setText("Model: " + model);
        m_detailLine2->setText("Category: " + dev->category);
    }
}

void DevicesAndPrintersPage::launchAddPrinter() {
    // Prefer the desktop printer tool; fall back to CUPS' own web admin.
    for (const char *tool : { "system-config-printer", "hp-setup" }) {
        if (!QStandardPaths::findExecutable(tool).isEmpty()) {
            QProcess::startDetached(tool, {});
            return;
        }
    }
    QDesktopServices::openUrl(QUrl("http://localhost:631/admin"));
}

void DevicesAndPrintersPage::launchAddDevice() {
    // The closest analogue to Windows' "Add a device" wizard is the desktop's
    // Bluetooth pairing assistant; fall back to a general Bluetooth manager.
    for (const char *tool : { "bluedevil-wizard", "blueman-manager",
                              "bluetooth-sendto" }) {
        if (!QStandardPaths::findExecutable(tool).isEmpty()) {
            QProcess::startDetached(tool, {});
            return;
        }
    }
    launchDetached(this, { "bluedevil-wizard" });  // reports "not installed"
}
