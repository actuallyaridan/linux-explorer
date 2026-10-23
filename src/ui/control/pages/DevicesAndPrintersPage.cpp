#include "ui/control/pages/DevicesAndPrintersPage.h"
#include "DetailsPane.h"
#include "ui/control/dialogs/DevicePropertiesDialog.h"
#include "ui/control/IconHelper.h"
#include "ui/control/Commands.h"
#include "ui/control/Win7Ui.h"
#include "ui/control/Win7Ui.h"

#include "model/control/SysfsScanner.h"
#include "model/control/DeviceUtils.h"

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
#include "aero/palette.h"
#include "aero/artwork.h"

namespace ControlPanel {

namespace {

constexpr int kTileIcon = 40;
constexpr QSize kTileSize(96, 76);
// Index into m_devices/m_printers
constexpr int kRoleIndex   = Qt::UserRole;
// Bool: is this a printer tile?
constexpr int kRolePrinter = Qt::UserRole + 1;

// Source scanner category -> {Win7 category label, icon}. The order here is the
// order tiles appear under "Devices". Only these categories surface in the
// Devices and Printers folder, internal parts (CPUs, controllers, disks…) do
// not, matching Windows
const QVector<QPair<QString, QPair<QString, QString>>> kDeviceCatOrder = {
    { "Monitors",                        { "Monitor",         "video-display" } },
    { "Mice and other pointing devices", { "Mouse",           "input-mouse" } },
    { "Keyboards",                       { "Keyboard",        "input-keyboard" } },
    { "Portable Devices",                { "Portable Device", "smartphone" } },
    { "Universal Serial Bus devices",    { "USB device",      "drive-removable-media-usb" } },
    { "Game controllers",                { "Game controller", "input-gaming" } },
};

// A command-bar link: black by default, blue underline on hover (the Explorer
// toolbar style), reusing Aero::LinkLabel's click plumbing
Aero::LinkLabel *commandLink(const QString &text) {
    auto *link = Win7::link(text);
    link->setColors("#1A1A1A", Aero::Palette::LinkHover);
    return link;
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
    // the icon lists' scroll bars into non-native stylesheet rendering
    setObjectName("dpPage");
    setStyleSheet(
        Aero::panelSheet(QStringLiteral("dpPage"),
                         Aero::Palette::Surface));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Command bar (the shared ribbon strip)
    QHBoxLayout *cmdLayout = nullptr;
    auto *cmdBar = Win7::commandBar(&cmdLayout);
    cmdLayout->setContentsMargins(12, 0, 12, 0);
    cmdLayout->setSpacing(16);
    auto *addDevice = commandLink("Add a device");
    auto *addPrinter = commandLink("Add a printer");
    connect(addDevice, &Aero::LinkLabel::clicked,
            this, &DevicesAndPrintersPage::launchAddDevice);
    connect(addPrinter, &Aero::LinkLabel::clicked,
            this, &DevicesAndPrintersPage::launchAddPrinter);
    cmdLayout->addWidget(addDevice);
    cmdLayout->addWidget(addPrinter);
    cmdLayout->addStretch(1);
    Win7::addCommandBarIcons(cmdLayout);
    root->addWidget(cmdBar);

    // Grouped icon view
    auto *content = new QWidget;
    auto *contentV = new QVBoxLayout(content);
    contentV->setContentsMargins(14, 10, 14, 6);
    contentV->setSpacing(2);

    auto makeHeader = [](const QString &text) {
        auto *label = new QLabel(text);
        Aero::setPointSize(label, 9);
        label->setStyleSheet(
            "color: #1F5C99; background: transparent;"
            " border-bottom: 1px solid #E2E6EC; padding: 2px 0;");
        return label;
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
            QStringLiteral("QListWidget { background: %1; outline: none; }"
            "QListWidget::item { color: %2; padding: 3px; border-radius: 3px; }"
            "QListWidget::item:selected {"
            " background: #CDE6F7; border: 1px solid #A6D2F0; color: %2; }"
            "QListWidget::item:hover:!selected {"
            " background: #EAF4FC; border: 1px solid #CFE9FB; }")
                .arg(QLatin1String(Aero::Palette::Surface),
                     QLatin1String(Aero::Palette::Text)));
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

    // Details pane: Explorer's, as Win7 puts under this folder too
    m_details = new DetailsPane;
    root->addWidget(m_details);

    // Selection in one list clears the other, so there is a single global
    // selection driving one details pane
    connect(m_devicesList, &QListWidget::itemSelectionChanged, this,
            [this] { onSelection(m_devicesList, m_printersList); });
    connect(m_printersList, &QListWidget::itemSelectionChanged, this,
            [this] { onSelection(m_printersList, m_devicesList); });

    auto openFromItem = [this](QListWidgetItem *item) {
        if (!item)
            return;
        const ShellDevice *device = shellDeviceFor(item);
        if (device) {
            DevicePropertiesDialog dlg(*device, this);
            dlg.exec();
        }
    };
    connect(m_devicesList,  &QListWidget::itemActivated,     this, openFromItem);
    connect(m_devicesList,  &QListWidget::itemDoubleClicked, this, openFromItem);
    connect(m_printersList, &QListWidget::itemActivated,     this, openFromItem);
    connect(m_printersList, &QListWidget::itemDoubleClicked, this, openFromItem);

    updateDetails(nullptr);

    // Kick off the background scan
    Win7::runAsync(this, &DevicesAndPrintersPage::runScan,
                   [this](const ScanResult &result) { populate(result); });
}

void DevicesAndPrintersPage::populate(const ScanResult &result) {
    // Curate hardware into the "Devices" group
    m_devices.clear();

    // The computer itself is always the first device
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
                if (dev.iconName.isEmpty())
                    sd.iconName = icon;
                else
                    sd.iconName = dev.iconName;
                sd.category = label;
                sd.manufacturer = dev.manufacturer;
                sd.model = dev.name;
                if (dev.disabled)
                    sd.status = "This device is disabled.";
                else
                    sd.status = "This device is working properly.";
                sd.location = dev.location;
                sd.driver = dev.driver;
                sd.driverVersion = dev.driverVersion;
                sd.driverDate = dev.driverDate;
                sd.rawLocation = dev.rawLocation;
                m_devices.append(sd);
            }
        }
    }

    // Printers and faxes
    m_printers.clear();
    for (const Printer &p : result.printers) {
        ShellDevice sd;
        if (p.info.isEmpty())
            sd.name = p.name;
        else
            sd.name = p.info;
        if (p.isFax)
            sd.iconName = "printer";
        else
            sd.iconName = "printer";
        if (p.isFax)
            sd.category = "Fax";
        else if (p.isClass)
            sd.category = "Printer class";
        else
            sd.category = "Printer";
        // The make-and-model string leads with the manufacturer, e.g.
        // "HP LaserJet 4000 Foomatic/Postscript"
        sd.manufacturer = p.makeAndModel.section(' ', 0, 0);
        sd.model = p.makeAndModel;
        if (p.state.isEmpty())
            sd.status = QStringLiteral("Ready");
        else
            sd.status = p.state;
        sd.location = p.location;
        sd.isPrinter = true;
        sd.isDefaultPrinter = p.isDefault;
        m_printers.append(sd);
    }

    // Render
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
    // Clear the sibling list without recursing back into this slot
    QSignalBlocker block(other);
    other->clearSelection();
    other->setCurrentItem(nullptr);

    QListWidgetItem *item = active->selectedItems().first();
    const ShellDevice *device = shellDeviceFor(item);
    if (device)
        updateDetails(device);
}

const ShellDevice *DevicesAndPrintersPage::shellDeviceFor(QListWidgetItem *item) const {
    const int index = item->data(kRoleIndex).toInt();
    const QVector<ShellDevice> *list = &m_devices;
    if (item->data(kRolePrinter).toBool())
        list = &m_printers;
    if (index < 0 || index >= list->size())
        return nullptr;
    return &list->at(index);
}

void DevicesAndPrintersPage::updateDetails(const ShellDevice *dev) {
    if (!dev) {
        const int total = m_devices.size() + m_printers.size();
        QString count;
        if (total > 0)
            count = QString("%1 items").arg(total);
        m_details->showContent(resolveIcon("computer"), count,
                               "Select an item to view its details.");
        return;
    }

    // Win7 sets the properties side by side, which here means on one line
    QString first;
    if (dev->isPrinter) {
        first = "Status: " + dev->status;
    } else {
        QString model;
        if (dev->model.isEmpty())
            model = dev->name;
        else
            model = dev->model;
        first = "Model: " + model;
    }
    const QString properties = first + "    Category: " + dev->category;
    m_details->showContent(resolveIcon(dev->iconName), dev->name, properties);
}

void DevicesAndPrintersPage::launchAddPrinter() {
    // Prefer the desktop printer tool; fall back to CUPS' own web admin
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
    // Bluetooth pairing assistant; fall back to a general Bluetooth manager
    for (const char *tool : { "bluedevil-wizard", "blueman-manager",
                              "bluetooth-sendto" }) {
        if (!QStandardPaths::findExecutable(tool).isEmpty()) {
            QProcess::startDetached(tool, {});
            return;
        }
    }
    // Reports "not installed"
    launchDetached(this, { "bluedevil-wizard" });
}

} // namespace ControlPanel
