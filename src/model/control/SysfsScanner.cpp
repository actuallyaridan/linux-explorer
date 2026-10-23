#include "model/control/SysfsScanner.h"
#include "model/control/DeviceUtils.h"
#include "model/control/DeviceOps.h"
#include "model/control/KnownDriverDates.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QSysInfo>
#include <QProcess>
#include <QRegularExpression>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace ControlPanel {

namespace {

// Resolve data-file directories via environment variables so that Nix (and
// other non-FHS layouts) can point us at the right store paths
QString hwdataPath(const QString &filename) {
    QByteArray dir = qgetenv("HWDATA_DIR");
    if (!dir.isEmpty())
        return QString::fromUtf8(dir) + "/" + filename;
    return QStringLiteral("/usr/share/hwdata/") + filename;
}

QString libdrmPath(const QString &filename) {
    QByteArray dir = qgetenv("LIBDRM_DIR");
    if (!dir.isEmpty())
        return QString::fromUtf8(dir) + "/" + filename;
    return QStringLiteral("/usr/share/libdrm/") + filename;
}

QString kernelRelease() {
    return QSysInfo::kernelVersion();
}

QString runCmd(const QString &cmd, const QStringList &args,
               int timeoutMs = 2000) {
    QProcess proc;
    proc.start(cmd, args);
    if (!proc.waitForFinished(timeoutMs))
        return {};
    if (proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

enum class PkgMgr { None, Pacman, Dpkg, Rpm };

PkgMgr detectPkgMgr() {
    // Detect by database directory, not binary, so we correctly identify the
    // host distro even when foreign tools are installed (e.g. via distrobox)
    if (QDir("/var/lib/pacman").exists())
        return PkgMgr::Pacman;
    if (QDir("/var/lib/dpkg").exists())
        return PkgMgr::Dpkg;
    if (QDir("/var/lib/rpm").exists())
        return PkgMgr::Rpm;
    return PkgMgr::None;
}

PkgMgr activePkgMgr() {
    static PkgMgr mgr = detectPkgMgr();
    return mgr;
}

struct DriverInfo {
    QString version;
    QString date;
    QString author;
    bool isDkms = false;
};

struct ModinfoResult {
    QString version;
    QString filename;
    QString author;
};

ModinfoResult queryModinfo(const QString &moduleName) {
    QProcess proc;
    proc.start("modinfo", {"--", moduleName});
    ModinfoResult r;
    if (!proc.waitForFinished(2000) || proc.exitCode() != 0)
        return r;
    for (const QString &line :
         QString::fromUtf8(proc.readAllStandardOutput()).split('\n')) {
        int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        QString key = line.left(colon).trimmed();
        QString val = line.mid(colon + 1).trimmed();
        if (key == "version")
            r.version = val;
        else if (key == "filename")
            r.filename = val;
        else if (key == "author")
            r.author = val;
    }
    return r;
}

QString pacmanFieldFrom(const QString &output, const QString &field) {
    for (const QString &line : output.split('\n')) {
        if (line.startsWith(field)) {
            int colon = line.indexOf(':');
            if (colon > 0)
                return line.mid(colon + 1).trimmed();
        }
    }
    return {};
}

QString pacmanField(const QString &package, const QString &field) {
    return pacmanFieldFrom(runCmd("pacman", {"-Qi", package}), field);
}

QString pacmanBuildDate(const QString &package, const QString &version) {
    static const QRegularExpression kPkgRe("^[a-zA-Z0-9_.+@-]+$");
    if (version.isEmpty()
            || !kPkgRe.match(package).hasMatch()
            || !kPkgRe.match(version).hasMatch())
        return {};
    QString dirPath = QStringLiteral("/var/lib/pacman/local/%1-%2")
                          .arg(package, version);
    QFile file(dirPath + "/desc");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QString content = QString::fromUtf8(file.read(65536));
    int idx = content.indexOf("%BUILDDATE%");
    if (idx < 0)
        return {};
    int nl = content.indexOf('\n', idx);
    if (nl < 0)
        return {};
    QString tsLine = content.mid(nl + 1,
                                 content.indexOf('\n', nl + 1) - nl - 1)
                         .trimmed();
    bool ok = false;
    qint64 ts = tsLine.toLongLong(&ok);
    if (!ok)
        return {};
    return formatDate(QDateTime::fromSecsSinceEpoch(ts));
}

QString knownVersionDate(const QString &version) {
    auto it = knownDriverDates().constFind(version);
    if (it == knownDriverDates().constEnd())
        return {};
    QDate d = QDate::fromString(*it, "yyyy-MM-dd");
    if (!d.isValid())
        return *it;
    return formatDate(d);
}

DriverInfo dkmsInfo(const QString &moduleName) {
    DriverInfo info;
    QDir dkmsRoot("/var/lib/dkms");
    if (!dkmsRoot.exists())
        return info;

    QString kver = kernelRelease();

    for (const QString &source : dkmsRoot.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir sourceDir(dkmsRoot.absoluteFilePath(source));
        QStringList versions;
        for (const QString &entry : sourceDir.entryList(
                 QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!entry.startsWith("kernel-"))
                versions.append(entry);
        }
        if (versions.isEmpty())
            continue;
        versions.sort();
        QString version = versions.last();

        QDir kernelDir(sourceDir.absoluteFilePath(version + "/" + kver));
        if (!kernelDir.exists())
            continue;

        for (const QString &arch : kernelDir.entryList(
                 QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir modDir(kernelDir.absoluteFilePath(arch + "/module"));
            if (!modDir.exists())
                continue;
            QStringList matches = modDir.entryList(
                {moduleName + ".ko",
                 moduleName + ".ko.zst",
                 moduleName + ".ko.xz",
                 moduleName + ".ko.gz"},
                QDir::Files);
            if (!matches.isEmpty()) {
                info.version = version;
                QString known = knownVersionDate(version);
                if (!known.isEmpty()) {
                    info.date = known;
                } else {
                    QFileInfo vfi(sourceDir.absoluteFilePath(version));
                    info.date = formatDate(vfi.lastModified());
                }
                return info;
            }
        }
    }
    return info;
}


struct PkgVersionDate { QString version; QString date; };

QString pkgOwnerOf(const QString &filepath) {
    switch (activePkgMgr()) {
    case PkgMgr::Pacman:
        return runCmd("pacman", {"-Qoq", filepath});
    case PkgMgr::Dpkg: {
        // Output: "pkgname: /path/to/file" (first match wins)
        QString out = runCmd("dpkg", {"-S", filepath});
        int colon = out.indexOf(':');
        if (colon < 0) return {};
        return out.left(colon).trimmed();
    }
    case PkgMgr::Rpm:
        return runCmd("rpm", {"-qf", "--queryformat", "%{NAME}", filepath});
    default:
        return {};
    }
}

PkgVersionDate queryPkgVersionDate(const QString &pkg) {
    switch (activePkgMgr()) {
    case PkgMgr::Pacman: {
        QString out = runCmd("pacman", {"-Qi", pkg});
        QString ver = pacmanFieldFrom(out, "Version");
        return {ver, pacmanBuildDate(pkg, ver)};
    }
    case PkgMgr::Dpkg: {
        // dpkg has no build date in its metadata; version only
        QString ver = runCmd("dpkg-query",
                             {"-W", "--showformat=${Version}", pkg});
        return {ver, {}};
    }
    case PkgMgr::Rpm: {
        QString out = runCmd("rpm", {"-q", "--queryformat",
                                     "%{VERSION}-%{RELEASE}\t%{BUILDTIME}",
                                     pkg});
        if (out.isEmpty()) return {};
        QStringList parts = out.split('\t');
        QString ver = parts.value(0).trimmed();
        QString date;
        if (parts.size() >= 2) {
            bool ok = false;
            qint64 ts = parts[1].trimmed().toLongLong(&ok);
            if (ok && ts > 0)
                date = formatDate(QDateTime::fromSecsSinceEpoch(ts));
        }
        return {ver, date};
    }
    default:
        return {};
    }
}

DriverInfo moduleDriverInfo(const QString &moduleName) {
    DriverInfo info;
    if (moduleName.isEmpty() || moduleName == "(kernel)") {
        info.version = kernelRelease();
        return info;
    }

    ModinfoResult mi = queryModinfo(moduleName);

    if (mi.filename == "(builtin)" || mi.filename.isEmpty()) {
        if (mi.version.isEmpty())
            info.version = kernelRelease();
        else
            info.version = mi.version;
        info.author = mi.author;
        return info;
    }

    if (mi.filename.contains("/dkms/")) {
        DriverInfo dkms = dkmsInfo(moduleName);
        if (!dkms.version.isEmpty()) {
            if (mi.version.isEmpty())
                info.version = dkms.version;
            else
                info.version = mi.version;
            info.date = dkms.date;
            info.author = mi.author;
            info.isDkms = true;
            return info;
        }
    }

    QString pkg = pkgOwnerOf(mi.filename);
    if (!pkg.isEmpty()) {
        PkgVersionDate pvd = queryPkgVersionDate(pkg);
        // The module's own version, else the package's, else the kernel's
        if (!mi.version.isEmpty())
            info.version = mi.version;
        else if (!pvd.version.isEmpty())
            info.version = pvd.version;
        else
            info.version = kernelRelease();
        QString known = knownVersionDate(info.version);
        if (known.isEmpty())
            info.date = pvd.date;
        else
            info.date = known;
        info.author = mi.author;
        return info;
    }

    if (mi.version.isEmpty())
        info.version = kernelRelease();
    else
        info.version = mi.version;
    info.author = mi.author;
    QFileInfo fi(mi.filename);
    if (fi.exists())
        info.date = formatDate(fi.lastModified());
    return info;
}

struct IdsDb {
    QHash<QString, QString> vendors;
    QHash<QString, QString> devices;
};

IdsDb loadIds(const QString &path) {
    IdsDb db;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return db;
    QTextStream in(&file);
    QString currentVendor;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith('#') || line.trimmed().isEmpty())
            continue;
        if (line.startsWith("\t\t"))
            continue;
        if (line.startsWith('\t')) {
            QString trimmed = line.mid(1);
            QString id = trimmed.left(4).toLower();
            QString name = trimmed.mid(6).trimmed();
            if (!currentVendor.isEmpty())
                db.devices.insert(currentVendor + ":" + id, name);
        } else {
            if (line.length() < 6)
                continue;
            if (line.startsWith("C "))
                break;
            currentVendor = line.left(4).toLower();
            QString name = line.mid(6).trimmed();
            db.vendors.insert(currentVendor, name);
        }
    }
    return db;
}

const IdsDb &pciIds() {
    static IdsDb db = loadIds(hwdataPath("pci.ids"));
    return db;
}

const IdsDb &usbIds() {
    static IdsDb db = loadIds(hwdataPath("usb.ids"));
    return db;
}

// Loads /usr/share/libdrm/amdgpu.ids: "DEVICE_HEX, REVISION_HEX, Marketing Name"
// Returns a hash keyed by "device:revision" (uppercase, no 0x prefix)
QHash<QString, QString> loadAmdgpuIds() {
    QHash<QString, QString> db;
    QFile file(libdrmPath("amdgpu.ids"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return db;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith('#') || line.isEmpty())
            continue;
        QStringList parts = line.split(',');
        if (parts.size() < 3)
            continue;
        QString devId  = parts[0].trimmed().toUpper();
        QString revId  = parts[1].trimmed().toUpper();
        QString name   = parts[2].trimmed();
        if (!name.isEmpty())
            db.insert(devId + ":" + revId, name);
    }
    return db;
}

const QHash<QString, QString> &amdgpuIds() {
    static QHash<QString, QString> db = loadAmdgpuIds();
    return db;
}

QString usbVendorName(const QString &vendorHex) {
    QString key = vendorHex.trimmed().toLower();
    return usbIds().vendors.value(key, key);
}

// Bluetooth SIG company identifiers, a separate registry from USB vendor IDs.
// Source: https://www.bluetooth.com/specifications/assigned-numbers/
QString btCompanyName(const QString &hexId) {
    static const QHash<quint16, QString> kCompanies = {
        {0x0000, "Ericsson AB"},
        {0x0001, "Nokia Mobile Phones"},
        {0x0002, "Intel Corp."},
        {0x0003, "IBM Corp."},
        {0x0004, "Toshiba Corp."},
        {0x0006, "Microsoft Corp."},
        {0x000F, "Broadcom Corporation"},
        {0x0046, "LG Electronics"},
        {0x004C, "Apple, Inc."},
        {0x0059, "Nordic Semiconductor ASA"},
        {0x0075, "Samsung Electronics Co. Ltd."},
        {0x009E, "Bose Corporation"},
        {0x00E0, "Google"},
        {0x012D, "Sony Corporation"},
        {0x0131, "Beats Electronics"},
        {0x0217, "Jabra (GN Audio)"},
    };
    bool ok;
    quint16 id = hexId.trimmed().toUShort(&ok, 16);
    if (!ok)
        return {};
    return kCompanies.value(id);
}

QString readHexId(const QString &path) {
    QString id = readSysFile(path);
    if (id.startsWith("0x"))
        id = id.mid(2);
    return id.toLower();
}

// "Vendor Model", or just the model when the vendor is unknown
QString vendorAndModel(const QString &vendor, const QString &model) {
    if (vendor.isEmpty())
        return model.trimmed();
    return (vendor + " " + model).trimmed();
}

// The database's own name for the adapter, else its vendor's adapter, else a
// plain one
QString bluetoothAdapterName(const QString &deviceName, const QString &vendorName) {
    if (!deviceName.isEmpty())
        return deviceName;
    if (!vendorName.isEmpty())
        return vendorName + QStringLiteral(" Bluetooth Adapter");
    return QStringLiteral("Bluetooth Adapter");
}

QString driverNameFor(const QString &sysfsPath) {
    QFileInfo link(sysfsPath + "/driver");
    if (!link.exists())
        return {};
    return QFileInfo(link.symLinkTarget()).fileName();
}

QString dmiProductName() {
    static const QString name = readSysFile("/sys/class/dmi/id/product_name").trimmed();
    return name;
}

QString friendlyLocation(const QString &sysfsPath,
                         const QString &rawLocation) {
    if (sysfsPath.isEmpty() && rawLocation.isEmpty())
        return "Unknown";

    QString path;
    if (sysfsPath.isEmpty())
        path = rawLocation;
    else
        path = sysfsPath;

    if (path.contains("i8042") || path.contains("serio"))
        return "plugged in to PS/2 mouse port";

    if (path.contains("/bluetooth/"))
        return "connected via Bluetooth";

    if (path.contains("/usb")) {
        QRegularExpression usbRe(R"(/usb\d+/(\d+-[\d.]+))");
        auto match = usbRe.match(path);
        if (match.hasMatch())
            return QString("plugged in to USB port %1")
                .arg(match.captured(1));
        return "plugged in to USB port";
    }

    QRegularExpression pciRe(
        R"((\d+):([0-9a-f]+):([0-9a-f]+)\.([0-9a-f]+))");
    auto match = pciRe.match(path);
    if (match.hasMatch()) {
        bool ok1, ok2, ok3;
        int bus = match.captured(2).toInt(&ok1, 16);
        int dev = match.captured(3).toInt(&ok2, 16);
        int fn  = match.captured(4).toInt(&ok3, 16);
        if (!ok1 || !ok2 || !ok3)
            return {};
        return QString("PCI bus %1, device %2, function %3")
            .arg(bus).arg(dev).arg(fn);
    }

    if (path.contains("/platform/") || path.contains("LNXSYSTM")
        || path.contains("ACPI")) {
        QString host = dmiProductName();
        if (host.isEmpty())
            return "ACPI system device";
        return "on " + host;
    }

    if (path.contains("/hid/"))
        return "plugged in via HID";

    if (path.contains("/sound/card")) {
        QRegularExpression cardRe(R"(card(\d+))");
        auto match = cardRe.match(rawLocation);
        if (match.hasMatch())
            return QString("Audio card %1").arg(match.captured(1));
        return "Audio device";
    }

    if (!rawLocation.isEmpty() && !rawLocation.startsWith("/sys"))
        return rawLocation;

    return {};
}

QString monitorLocation(const QString &entry, const QString &gpuName = {}) {
    // DRM connector names: "card0-HDMI-A-1", "card1-DP-3", "card0-eDP-1"
    static QRegularExpression re(R"(card(\d+)-(.+)-(\d+))");
    auto match = re.match(entry);
    if (!match.hasMatch())
        return entry;
    QString type = match.captured(2);
    int port = match.captured(3).toInt();
    QString suffix;
    if (!gpuName.isEmpty())
        suffix = " on " + gpuName;
    if (type == "eDP" || type == "LVDS")
        return "Built-in display" + suffix;
    if (type.startsWith("HDMI"))
        return QString("plugged in to HDMI port %1").arg(port) + suffix;
    if (type == "DP")
        return QString("plugged in to DisplayPort %1").arg(port) + suffix;
    if (type.startsWith("DVI"))
        return QString("plugged in to DVI port %1").arg(port) + suffix;
    if (type == "VGA")
        return QString("plugged in to VGA port %1").arg(port) + suffix;
    return entry;
}

Device makePciDevice(const QString &sysfsPath) {
    Device device;
    QString vendor = readHexId(sysfsPath + "/vendor");
    QString deviceId = readHexId(sysfsPath + "/device");
    QString cls = readHexId(sysfsPath + "/class");
    const auto &db = pciIds();
    device.manufacturer = db.vendors.value(vendor,
                        QStringLiteral("Vendor %1").arg(vendor));
    device.name = db.devices.value(vendor + ":" + deviceId,
                  QStringLiteral("Device %1:%2").arg(vendor, deviceId));
    // For AMD GPUs, amdgpu.ids maps deviceId+revision to a specific marketing name
    // (e.g. "AMD Radeon RX 6700 XT" instead of "Navi 22 [Radeon RX 6700/6700 XT/...]")
    if (vendor == "1002") {
        QString rev = readSysFile(sysfsPath + "/revision").trimmed()
                          .remove("0x").toUpper();
        QString devUpper = deviceId.toUpper();
        QString marketing = amdgpuIds().value(devUpper + ":" + rev);
        if (!marketing.isEmpty())
            device.name = marketing;
    }
    device.driver = driverNameFor(sysfsPath);
    // PCI bridges (06xx), IOMMUs (0806xx), and generic system peripherals (0800xx)
    // are managed entirely by the kernel and never need a userspace driver
    bool isKernelManaged = cls.startsWith("06")
                           || cls.startsWith("0806")
                           || cls.startsWith("0800");
    device.noDriverNeeded = isKernelManaged && device.driver.isEmpty();
    if (device.driver.isEmpty() && !device.noDriverNeeded)
        device.status = "No driver loaded";
    else
        device.status = "Working properly";
    DriverInfo di = moduleDriverInfo(device.driver);
    device.driverVersion = di.version;
    device.driverDate = di.date;
    device.driverAuthor = di.author;
    device.isDkms = di.isDkms;

    // Non-PCI devices (platform/AMBA on ARM SoCs) have no vendor sysfs file,
    // leaving the name as "Device :". Use the driver's modinfo description as
    // a friendlier fallback, stripping the conventional trailing "driver" word
    if (vendor.isEmpty() && !device.driver.isEmpty()) {
        QString desc = runCmd("modinfo", {"-F", "description", "--", device.driver});
        if (!desc.isEmpty()) {
            static const QRegularExpression kDriverSuffix(
                R"(\s+[Dd]river[.]*\s*$)");
            desc.remove(kDriverSuffix);
            desc = desc.trimmed();
        }
        if (!desc.isEmpty())
            device.name = desc;
        else
            device.name = device.driver;
        if (device.manufacturer == "Vendor ")
            device.manufacturer = "Unknown";
    }

    device.location = friendlyLocation(sysfsPath, {});
    device.rawLocation = QFileInfo(sysfsPath).fileName();
    device.sysfsPciPath = sysfsPath;
    return device;
}

QVector<Device> scanPciByClass(const QString &classPrefix) {
    QVector<Device> out;
    QDir base("/sys/bus/pci/devices");
    const QString lowerPrefix = classPrefix.toLower();
    for (const QString &entry : base.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString path = base.absoluteFilePath(entry);
        QString cls = readHexId(path + "/class");
        if (cls.startsWith(lowerPrefix)) {
            Device device = makePciDevice(path);
            device.rawLocation = entry;
            out.append(device);
        }
    }
    return out;
}

QVector<Device> scanCpus() {
    QVector<Device> out;
    QFile file("/proc/cpuinfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;

    QString modelName;
    QString vendorId;

    QString content = QString::fromUtf8(file.read(524288));
    for (const QString &line : content.split('\n')) {
        if (line.startsWith("model name"))
            modelName = line.section(':', 1).trimmed();
        else if (line.startsWith("vendor_id"))
            vendorId = line.section(':', 1).trimmed();
        else if (line.trimmed().isEmpty() && !modelName.isEmpty()) {
            Device device;
            device.name = modelName;
            device.manufacturer = vendorId;
            device.status = "Working properly";
            device.driver = "(kernel)";
            device.driverVersion = kernelRelease();
            QString host = dmiProductName();
            if (host.isEmpty())
                device.location = QString();
            else
                device.location = "on " + host;
            out.append(device);
            modelName.clear();
            vendorId.clear();
        }
    }

    return out;
}

QVector<Device> scanDisks() {
    QVector<Device> out;
    QDir base("/sys/block");
    for (const QString &entry : base.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.startsWith("loop") || entry.startsWith("ram")
            || entry.startsWith("dm-") || entry.startsWith("sr")
            || entry.startsWith("zram"))
            continue;

        QString path = base.absoluteFilePath(entry);
        QString model;
        QString vendor;

        if (entry.startsWith("nvme")) {
            model = readSysFile(path + "/device/model");
            // For NVMe, vendor comes from the PCI subsystem
            QString sysPath = QFileInfo(path + "/device").canonicalFilePath();
            QString vid = readHexId(sysPath + "/../vendor");
            vendor = pciIds().vendors.value(vid, "");
        } else {
            model = readSysFile(path + "/device/model");
            vendor = readSysFile(path + "/device/vendor");
        }

        if (model.isEmpty())
            continue;

        Device device;
        device.name = vendorAndModel(vendor, model);
        if (vendor.isEmpty())
            device.manufacturer = "(Standard disk drives)";
        else
            device.manufacturer = vendor;
        device.status = "Working properly";
        QString driverLink = QFileInfo(
            path + "/device/driver").symLinkTarget();
        device.driver = QFileInfo(driverLink).fileName();
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        device.location = friendlyLocation(
            QFileInfo(path + "/device").canonicalFilePath(), entry);
        out.append(device);
    }
    return out;
}

// Returns true if this USB device entry is a Bluetooth adapter, detected by
// interface class E0/01 (Wireless/Bluetooth) or by the btusb driver binding.
// Such devices are shown under "Bluetooth Radios" and should be excluded from
// the generic USB device and unknown-device lists to avoid duplicates
bool isBluetoothUsbDevice(const QString &entry) {
    QDir base("/sys/bus/usb/devices");
    const QString ifPrefix = entry + ":";
    for (const QString &iface : base.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (!iface.startsWith(ifPrefix))
            continue;
        QString ifPath = QFileInfo(base.absoluteFilePath(iface)).canonicalFilePath();
        // Class e0 subclass 01 is a Bluetooth radio
        const QString ifClass = readSysFile(ifPath + "/bInterfaceClass").trimmed();
        const QString ifSubClass = readSysFile(ifPath + "/bInterfaceSubClass").trimmed();
        const bool isWireless =
            ifClass.compare(QStringLiteral("e0"), Qt::CaseInsensitive) == 0;
        const bool isRadio = isWireless && ifSubClass == QLatin1String("01");
        if (isRadio)
            return true;
        if (QFileInfo(QFileInfo(ifPath + "/driver").symLinkTarget()).fileName()
                == QLatin1String("btusb"))
            return true;
    }
    return false;
}

QVector<Device> scanUsbDevices(const QSet<QString> &excluded = {}) {
    QVector<Device> out;
    QDir base("/sys/bus/usb/devices");
    if (!base.exists())
        return out;

    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        // Skip interfaces (contain colon) and root hubs
        if (entry.contains(':') || entry.startsWith("usb"))
            continue;
        if (excluded.contains(entry))
            continue;
        if (isBluetoothUsbDevice(entry))
            continue;

        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();

        QString product = readSysFile(path + "/product");
        QString mfr = readSysFile(path + "/manufacturer");
        QString devClass = readSysFile(path + "/bDeviceClass");

        // Skip hubs (class 09)
        if (devClass == "09")
            continue;

        if (product.isEmpty())
            continue;

        Device device;
        device.name = product;
        if (mfr.isEmpty())
            device.manufacturer = "(Standard USB device)";
        else
            device.manufacturer = mfr;
        device.status = "Working properly";
        device.driver = QFileInfo(
            QFileInfo(path + "/driver").symLinkTarget()).fileName();
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        device.location = friendlyLocation(path, entry);
        out.append(device);
    }
    return out;
}

struct iOSBatteryInfo {
    int  capacity = -1;
    bool charging = false;
};

iOSBatteryInfo queryiOSBattery(const QString &udid) {
    QProcess proc;
    proc.start("ideviceinfo", {"-u", udid, "-q", "com.apple.mobile.battery"});
    iOSBatteryInfo info;
    if (!proc.waitForFinished(4000) || proc.exitCode() != 0)
        return info;
    for (const QString &line :
         QString::fromUtf8(proc.readAllStandardOutput()).split('\n')) {
        if (line.startsWith("BatteryCurrentCapacity:")) {
            bool ok;
            const int capacity = line.section(':', 1).trimmed().toInt(&ok);
            if (ok)
                info.capacity = capacity;
        } else if (line.startsWith("BatteryIsCharging:")) {
            info.charging = line.section(':', 1).trimmed().toLower() == "true";
        }
    }
    return info;
}

// Returns a map keyed by UDID (with and without dashes) → battery info.
// The sysfs /serial file contains the UDID without dashes; idevice_id -l may
// return it with dashes (new-style UDIDs). Both forms are stored so the lookup
// in scanPortableDevices() works regardless of which format sysfs exposes
QHash<QString, iOSBatteryInfo> scaniOSBatteries() {
    QHash<QString, iOSBatteryInfo> result;
    QProcess idProc;
    idProc.start("idevice_id", {"-l"});
    if (!idProc.waitForFinished(3000) || idProc.exitCode() != 0)
        return result;
    const QString udidList = QString::fromUtf8(
        idProc.readAllStandardOutput()).trimmed();
    if (udidList.isEmpty())
        return result;
    for (const QString &rawUdid : udidList.split('\n', Qt::SkipEmptyParts)) {
        const QString udid = rawUdid.trimmed();
        if (udid.isEmpty())
            continue;
        iOSBatteryInfo batt = queryiOSBattery(udid);
        result.insert(udid, batt);
        const QString udidNoDash = QString(udid).remove('-');
        if (udidNoDash != udid)
            result.insert(udidNoDash, batt);
    }
    return result;
}

bool isPortableUsbDevice(const QString &entry, const QString &canonPath) {
    // Devices with a Still Image (MTP/PTP) interface, class 06
    QDir base("/sys/bus/usb/devices");
    QString prefix = entry + ":";
    for (const QString &e : base.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (!e.startsWith(prefix))
            continue;
        QString ifPath = QFileInfo(base.absoluteFilePath(e)).canonicalFilePath();
        if (readSysFile(ifPath + "/bInterfaceClass").trimmed() == "06")
            return true;
        // ipheth: iPhone/iPad ethernet-over-USB interface
        QString drv = QFileInfo(
            QFileInfo(ifPath + "/driver").symLinkTarget()).fileName();
        if (drv == "ipheth")
            return true;
    }
    // Apple iOS devices connected before the user trusts the host show only
    // vendor-specific interfaces, catch them by vendor + product name
    if (readSysFile(canonPath + "/idVendor").trimmed().toLower() == "05ac") {
        QString product = readSysFile(canonPath + "/product").toLower();
        if (product.contains("iphone") || product.contains("ipad")
                || product.contains("ipod") || product.contains("apple tv"))
            return true;
    }
    return false;
}

QVector<Device> scanPortableDevices(QSet<QString> *usedEntries) {
    QVector<Device> out;
    QDir base("/sys/bus/usb/devices");
    if (!base.exists())
        return out;

    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (entry.contains(':') || entry.startsWith("usb"))
            continue;

        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();

        if (!isPortableUsbDevice(entry, path))
            continue;

        QString product = readSysFile(path + "/product");
        QString mfr = readSysFile(path + "/manufacturer");
        if (product.isEmpty())
            continue;

        Device device;
        device.name = product;
        if (mfr.isEmpty())
            device.manufacturer = "(Standard portable device)";
        else
            device.manufacturer = mfr;
        device.status = "Working properly";
        device.driver = QFileInfo(
            QFileInfo(path + "/driver").symLinkTarget()).fileName();
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        device.location = friendlyLocation(path, entry);
        QString productLower = product.toLower();
        if (productLower.contains("ipad"))
            device.iconName = "computer-apple-ipad";
        else if (productLower.contains("iphone"))
            device.iconName = "phone-apple-iphone";
        else if (productLower.contains("ipod"))
            device.iconName = "multimedia-player-apple-ipod-touch";

        if (usedEntries)
            usedEntries->insert(entry);
        out.append(device);
    }
    return out;
}

QVector<Device> scanNetwork() {
    QVector<Device> out;
    QDir base("/sys/class/net");
    for (const QString &entry : base.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry == "lo")
            continue;
        QString path = base.absoluteFilePath(entry);
        QString devLink = QFileInfo(path + "/device").symLinkTarget();
        if (devLink.isEmpty())
            continue;
        Device device = makePciDevice(
            QFileInfo(path + "/device").canonicalFilePath());
        if (device.name.isEmpty())
            continue;
        device.rawLocation = entry;
        out.append(device);
    }
    return out;
}

// Three letters packed five bits each into two bytes, A being 1
QString decodePnpId(quint8 b1, quint8 b2) {
    const int packed = (b1 << 8) | b2;
    auto letter = [](int bits) -> QChar {
        const int index = bits & 0x1f;
        if (index < 1 || index > 26)
            return QChar('?');
        return QChar('A' + index - 1);
    };
    QString id;
    id.append(letter(packed >> 10));
    id.append(letter(packed >> 5));
    id.append(letter(packed));
    return id;
}

const QHash<QString, QString> &pnpIds() {
    static const QHash<QString, QString> db = [] {
        QHash<QString, QString> h;
        QFile file(hwdataPath("pnp.ids"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (line.startsWith('#') || line.trimmed().isEmpty())
                    continue;
                if (line.length() < 4)
                    continue;
                h.insert(line.left(3).toUpper(), line.mid(4).trimmed());
            }
        }
        return h;
    }();
    return db;
}

QString pnpVendorName(const QString &pnpId) {
    return pnpIds().value(pnpId.toUpper(), pnpId);
}

QString parseEdid(const QByteArray &edid, QString *vendorOut) {
    if (edid.size() < 128)
        return {};
    QString pnp = decodePnpId(quint8(edid[8]), quint8(edid[9]));
    if (vendorOut)
        *vendorOut = pnpVendorName(pnp);
    for (int i = 0; i < 4; ++i) {
        int off = 54 + i * 18;
        if (off + 18 > edid.size())
            break;
        if (edid[off] == 0 && edid[off + 1] == 0
            && edid[off + 2] == 0 && edid[off + 3] == char(0xFC)) {
            QString name = QString::fromLatin1(
                edid.mid(off + 5, 13)).trimmed();
            while (name.endsWith(QChar(0x0a)))
                name.chop(1);
            if (!name.isEmpty())
                return name;
        }
    }
    return {};
}

QVector<Device> scanMonitors() {
    QVector<Device> out;
    QDir base("/sys/class/drm");
    for (const QString &entry : base.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString edidPath = base.absoluteFilePath(entry) + "/edid";
        QFile file(edidPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QByteArray edid = file.read(32768);
        if (edid.isEmpty())
            continue;
        QString vendor;
        QString name = parseEdid(edid, &vendor);
        if (name.isEmpty())
            name = "Generic monitor";
        Device device;
        device.name = name;
        if (vendor.isEmpty())
            device.manufacturer = "(Standard monitor types)";
        else
            device.manufacturer = vendor;
        device.status = "Working properly";
        device.driver = "drm";
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.rawLocation = entry;
        QString gpuName;
        {
            static QRegularExpression cardRe(R"(card(\d+)-)");
            auto cm = cardRe.match(entry);
            if (cm.hasMatch()) {
                QString cardPath = QFileInfo(
                    base.absoluteFilePath("card" + cm.captured(1))
                ).canonicalFilePath();
                QString vid = readHexId(cardPath + "/device/vendor");
                QString did = readHexId(cardPath + "/device/device");
                if (!vid.isEmpty()) {
                    if (vid == "1002") {
                        QString rev = readSysFile(cardPath + "/device/revision")
                                          .trimmed().remove("0x").toUpper();
                        gpuName = amdgpuIds().value(did.toUpper() + ":" + rev);
                    }
                    if (gpuName.isEmpty())
                        gpuName = pciIds().devices.value(vid + ":" + did, {});
                }
            }
        }
        device.location = monitorLocation(entry, gpuName);
        out.append(device);
    }
    return out;
}

QVector<Device> scanMice() {
    QVector<Device> out;
    QDir base("/sys/class/input");
    QSet<QString> seenPhys;
    for (const QString &entry : base.entryList(
             {"input*"}, QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString name = readSysFile(path + "/name");
        if (name.isEmpty())
            continue;

        // Kernel reports relative-axis capability as a hex bitmap; bits 0–1 are REL_X/REL_Y
        QString rel = readSysFile(path + "/capabilities/rel");
        if (rel.isEmpty())
            continue;
        bool ok;
        quint64 relBits = rel.trimmed().split(' ').last().toULongLong(&ok, 16);
        if (!ok || (relBits & 0x3) != 0x3)
            continue;

        QString phys = readSysFile(path + "/phys");
        if (!phys.isEmpty() && seenPhys.contains(phys))
            continue;
        if (!phys.isEmpty())
            seenPhys.insert(phys);

        Device device;
        device.name = name;
        device.manufacturer = usbVendorName(readSysFile(path + "/id/vendor"));
        device.status = "Working properly";
        device.driver = "input";
        device.driverVersion = kernelRelease();
        device.location = friendlyLocation(path, phys);
        out.append(device);
    }
    return out;
}

int popcountll(quint64 v) {
#ifdef __GNUC__
    return __builtin_popcountll(v);
#else
    int count = 0;
    while (v) {
        ++count;
        // Clears the lowest set bit
        v &= v - 1;
    }
    return count;
#endif
}

QVector<Device> scanKeyboards() {
    QVector<Device> out;
    QDir base("/sys/class/input");
    QSet<QString> seenPhys;
    for (const QString &entry : base.entryList(
             {"input*"}, QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString name = readSysFile(path + "/name");
        if (name.isEmpty())
            continue;

        // Skip devices with REL_X+REL_Y capability, those are mice
        QString rel = readSysFile(path + "/capabilities/rel");
        if (!rel.isEmpty()) {
            bool ok;
            quint64 relBits = rel.trimmed().split(' ').last()
                                  .toULongLong(&ok, 16);
            if (ok && (relBits & 0x3) == 0x3)
                continue;
        }

        // Real keyboards have many key bits set; use total popcount as the
        // heuristic (power buttons, knobs, etc. have far fewer than 20)
        QString key = readSysFile(path + "/capabilities/key");
        if (key.isEmpty())
            continue;
        int totalBits = 0;
        for (const QString &word : key.trimmed().split(' ')) {
            bool ok;
            quint64 v = word.toULongLong(&ok, 16);
            if (ok)
                totalBits += popcountll(v);
        }
        if (totalBits < 20)
            continue;

        // Reject media-key-only devices (e.g. Bluetooth AVRCP remotes): real
        // keyboards have letter keys (KEY_Q=16 … KEY_P=25) in the low word
        {
            const QStringList keyWords = key.trimmed().split(' ');
            if (!keyWords.isEmpty()) {
                bool ok;
                quint64 lastWord = keyWords.last().toULongLong(&ok, 16);
                // Mask covers bits 16-25 (KEY_Q through KEY_P)
                if (!ok || (lastWord & 0x3FF0000ULL) == 0)
                    continue;
            }
        }

        QString phys = readSysFile(path + "/phys");
        if (!phys.isEmpty() && seenPhys.contains(phys))
            continue;
        if (!phys.isEmpty())
            seenPhys.insert(phys);

        Device device;
        device.name = name;
        device.manufacturer = usbVendorName(readSysFile(path + "/id/vendor"));
        device.status = "Working properly";
        device.driver = "input";
        device.driverVersion = kernelRelease();
        device.location = friendlyLocation(path, phys);
        out.append(device);
    }
    return out;
}

// known HID drivers for game controllers
const QSet<QString> kControllerDrivers = {
    "playstation", "sony", "nintendo", "xpad", "xpadneo",
};

QVector<Device> scanHidGeneric() {
    QVector<Device> out;
    QDir base("/sys/bus/hid/devices");
    if (!base.exists())
        return out;
    QSet<QString> seenNames;
    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString uevent = readSysFile(path + "/uevent");
        QString hidName;
        QString hidId;
        for (const QString &line : uevent.split('\n')) {
            if (line.startsWith("HID_NAME="))
                hidName = line.mid(9).trimmed();
            else if (line.startsWith("HID_ID="))
                hidId = line.mid(7).trimmed();
        }
        // USB HID devices (bus 0003) are already shown under Universal Serial
        // Bus devices, suppress the duplicate here, mirroring Win7's PnP tree
        if (hidId.left(4).toUpper() == "0003")
            continue;
        if (hidName.isEmpty())
            hidName = entry;
        if (seenNames.contains(hidName))
            continue;
        seenNames.insert(hidName);
        Device device;
        device.name = hidName;
        device.manufacturer = "(Standard HID device)";
        device.status = "Working properly";
        device.driver = QFileInfo(
            QFileInfo(path + "/driver").symLinkTarget()).fileName();
        // game controllers get their own category
        if (kControllerDrivers.contains(device.driver))
            continue;
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        device.location = friendlyLocation(path, entry);
        out.append(device);
    }
    return out;
}

QVector<Device> scanBluetooth() {
    QVector<Device> out;
    QDir base("/sys/class/bluetooth");
    if (!base.exists())
        return out;

    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        // Connection handles (e.g. "hci0:12") share the hci prefix but are
        // per-device ACL links, not local adapters, skip them
        if (!entry.startsWith("hci") || entry.contains(':'))
            continue;

        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();

        QString name;
        QString mfr;
        // Path used for friendlyLocation()
        QString devicePath;
        QString actualDriver;
        QDir deviceDir(path);
        int depth = 0;

        // USB root hubs (usb1, usb2, …) carry "xHCI Host Controller" etc. as
        // their product string.  Walking past the real BT USB device into the
        // root hub would misname the adapter, so we skip those directories
        static const QRegularExpression kUsbRootHub(QStringLiteral(R"(^usb\d+$)"));

        while (deviceDir.cdUp() && ++depth <= 20) {
            const QString dirPath = deviceDir.absolutePath();

            // Capture the driver at the first sysfs level that exposes one
            // (typically the USB interface, e.g. 1-14:1.0)
            if (actualDriver.isEmpty())
                actualDriver = driverNameFor(dirPath);

            if (kUsbRootHub.match(QFileInfo(dirPath).fileName()).hasMatch())
                continue;

            // USB device with a product string, use it directly
            QString product = readSysFile(dirPath + "/product");
            if (!product.isEmpty()) {
                name = product;
                mfr  = readSysFile(dirPath + "/manufacturer");
                devicePath = dirPath;
                break;
            }

            // USB device without a product string, look up via USB ID database
            const QString idVendor =
                readSysFile(dirPath + "/idVendor").trimmed().toLower();
            if (!idVendor.isEmpty()) {
                const QString idProduct =
                    readSysFile(dirPath + "/idProduct").trimmed().toLower();
                const IdsDb &db = usbIds();
                const QString devName    = db.devices.value(idVendor + ":" + idProduct);
                const QString vendorName = db.vendors.value(idVendor);
                name = bluetoothAdapterName(devName, vendorName);
                mfr = vendorName;
                devicePath = dirPath;
                break;
            }

            // PCIe-native Bluetooth, look up via PCI ID database
            if (QFile::exists(dirPath + "/vendor") && QFile::exists(dirPath + "/class")) {
                const QString pciVendor  = readHexId(dirPath + "/vendor");
                const QString pciDevice  = readHexId(dirPath + "/device");
                const IdsDb &db = pciIds();
                const QString devName    = db.devices.value(pciVendor + ":" + pciDevice);
                const QString vendorName = db.vendors.value(pciVendor);
                name = bluetoothAdapterName(devName, vendorName);
                mfr = vendorName;
                devicePath = dirPath;
                break;
            }

            if (dirPath == "/sys/devices")
                break;
        }

        // Driver modinfo description as fallback when the ID database yielded
        // nothing specific.  Strips " driver" and any trailing version token so
        // "Generic Bluetooth USB driver ver 0.8" becomes "Generic Bluetooth USB"
        if ((name.isEmpty() || name == QStringLiteral("Bluetooth Adapter"))
                && !actualDriver.isEmpty()) {
            QString desc = runCmd("modinfo", {"-F", "description", "--", actualDriver});
            if (!desc.isEmpty()) {
                static const QRegularExpression kDriverSuffix(
                    QStringLiteral(R"(\s+[Dd]river\b.*)"));
                desc.remove(kDriverSuffix);
                desc = desc.trimmed();
            }
            if (!desc.isEmpty())
                name = desc;
        }

        if (name.isEmpty())
            name = entry;

        Device device;
        device.name = name;
        device.status = "Working properly";
        if (actualDriver.isEmpty())
            device.driver = QStringLiteral("btusb");
        else
            device.driver = actualDriver;
        if (mfr.isEmpty())
            device.manufacturer = "(Standard Bluetooth device)";
        else
            device.manufacturer = mfr;
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        bool ok;
        int hciNum = entry.mid(3).toInt(&ok);
        if (!devicePath.isEmpty())
            device.location = friendlyLocation(devicePath, {});
        else if (ok)
            device.location = QString("Bluetooth adapter %1").arg(hciNum);
        else
            device.location = entry;
        out.append(device);
    }
    return out;
}

QVector<Device> scanTpm() {
    QVector<Device> out;
    QDir base("/sys/class/tpm");
    if (!base.exists())
        return out;
    for (const QString &entry : base.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = base.absoluteFilePath(entry);
        QString major = readSysFile(path + "/tpm_version_major");
        QString name = "Trusted Platform Module";
        if (major == "2")
            name = "Trusted Platform Module 2.0";
        Device device;
        device.name = name;
        device.manufacturer = "(Standard)";
        device.status = "Working properly";
        device.driver = "tpm";
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.rawLocation = entry;
        device.location = friendlyLocation(
            QFileInfo(path).canonicalFilePath(), entry);
        out.append(device);
    }
    return out;
}

QVector<Device> scanOpticalDrives() {
    QVector<Device> out;
    QDir base("/sys/block");
    for (const QString &entry : base.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith("sr"))
            continue;
        QString path = base.absoluteFilePath(entry);
        QString model = readSysFile(path + "/device/model");
        QString vendor = readSysFile(path + "/device/vendor");
        Device device;
        device.name = vendorAndModel(vendor, model);
        if (device.name.isEmpty())
            device.name = entry;
        if (vendor.isEmpty())
            device.manufacturer = "(Standard CD-ROM drives)";
        else
            device.manufacturer = vendor;
        device.status = "Working properly";
        QString drv = QFileInfo(
            path + "/device/driver").symLinkTarget();
        device.driver = QFileInfo(drv).fileName();
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        device.location = friendlyLocation(
            QFileInfo(path + "/device").canonicalFilePath(), entry);
        out.append(device);
    }
    return out;
}


// checks whether a power_supply battery at the given canonical path is
// parented by a game controller HID driver
bool isControllerBattery(const QString &canonicalPath) {
    QDir dir(canonicalPath);
    int depth = 0;
    while (dir.cdUp() && ++depth <= 20) {
        QString uevent = readSysFile(dir.absolutePath() + "/uevent");
        for (const QString &line : uevent.split('\n')) {
            if (line.startsWith("DRIVER=")) {
                if (kControllerDrivers.contains(line.mid(7).trimmed()))
                    return true;
            }
        }
        if (dir.absolutePath() == "/sys/devices")
            break;
    }
    return false;
}

QVector<Device> scanHidBatteries() {
    QVector<Device> out;
    QDir base("/sys/class/power_supply");
    if (!base.exists())
        return out;

    for (const QString &entry : base.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString type = readSysFile(path + "/type");

        if (type != "Battery")
            continue;
        if (!path.contains("hid", Qt::CaseInsensitive))
            continue;
        if (isControllerBattery(path))
            continue;

        QString name;
        QString mfr;
        QDir dir(path);
        int depth = 0;
        while (dir.cdUp() && ++depth <= 20) {
            QString uevent = readSysFile(dir.absolutePath() + "/uevent");
            for (const QString &line : uevent.split('\n')) {
                if (line.startsWith("HID_NAME=")) {
                    name = line.mid(9).trimmed();
                    break;
                }
            }
            if (!name.isEmpty())
                break;
            if (dir.absolutePath() == "/sys/devices")
                break;
        }

        if (name.isEmpty())
            name = entry;

        QString capacity = readSysFile(path + "/capacity");
        QString capacityLevel = readSysFile(path + "/capacity_level");
        QString deviceStatus = "Working properly";
        if (!capacity.isEmpty())
            deviceStatus = QString("Battery level: %1%").arg(capacity);
        else if (!capacityLevel.isEmpty() && capacityLevel != "Unknown")
            deviceStatus = QString("Battery level: %1").arg(capacityLevel);

        Device device;
        device.name = "Battery on " + name;
        if (mfr.isEmpty())
            device.manufacturer = "(Standard)";
        else
            device.manufacturer = mfr;
        device.status = deviceStatus;
        device.driver = "usbhid";
        device.driverVersion = kernelRelease();
        device.rawLocation = entry;
        if (name.isEmpty())
            device.location = friendlyLocation(path, entry);
        else
            device.location = "on " + name;
        out.append(device);
    }
    return out;
}

QVector<Device> scanSoundCards() {
    QVector<Device> out;
    QDir base("/sys/class/sound");
    QSet<QString> seen;

    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (!entry.startsWith("card"))
            continue;
        bool isNum = false;
        entry.mid(4).toInt(&isNum);
        if (!isNum)
            continue;

        QString cardPath = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString devicePath = QFileInfo(
            cardPath + "/device").canonicalFilePath();
        if (devicePath.isEmpty() || devicePath == cardPath)
            continue;

        QString name;
        QString mfr;

        QDir deviceDir(devicePath);
        if (deviceDir.cdUp()) {
            QString product = readSysFile(
                deviceDir.absolutePath() + "/product");
            QString manufacturer = readSysFile(
                deviceDir.absolutePath() + "/manufacturer");
            if (!product.isEmpty()) {
                name = product;
                mfr = manufacturer;
            }
        }

        if (name.isEmpty()) {
            QString vendor = readHexId(devicePath + "/vendor");
            QString device = readHexId(devicePath + "/device");
            if (!vendor.isEmpty()) {
                mfr = pciIds().vendors.value(vendor, "(Standard)");
                name = pciIds().devices.value(
                    vendor + ":" + device,
                    QStringLiteral("Audio Device %1:%2")
                        .arg(vendor, device));
            }
        }

        if (name.isEmpty())
            name = readSysFile(cardPath + "/id");
        if (name.isEmpty())
            name = entry;

        if (seen.contains(name))
            continue;
        seen.insert(name);

        QString driver = driverNameFor(devicePath);

        Device device;
        device.name = name;
        if (mfr.isEmpty())
            device.manufacturer = "(Standard)";
        else
            device.manufacturer = mfr;
        device.status = "Working properly";
        if (driver.isEmpty())
            device.driver = "snd";
        else
            device.driver = driver;
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        device.location = friendlyLocation(devicePath, entry);
        if (devicePath.contains("/pci"))
            device.sysfsPciPath = devicePath;
        out.append(device);
    }
    return out;
}

QVector<Device> scanBatteries() {
    QVector<Device> out;
    QDir base("/sys/class/power_supply");
    if (!base.exists())
        return out;
    const QHash<QString, iOSBatteryInfo> iosBatteries = scaniOSBatteries();
    QString hostModel = dmiProductName();
    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = base.absoluteFilePath(entry);
        QString type = readSysFile(path + "/type");
        if (type != "Battery")
            continue;
        if (entry.contains("hidpp", Qt::CaseInsensitive))
            continue;

        QString canonicalPath = QFileInfo(path).canonicalFilePath();
        QString model = readSysFile(path + "/model_name");
        QString mfr = readSysFile(path + "/manufacturer");
        Device device;

        if (entry.startsWith("apple_mfi_fastcharge_")) {
            // Walk up two levels: power_supply/<entry> → power_supply/ → USB device dir
            QDir dir(canonicalPath);
            dir.cdUp();
            dir.cdUp();
            QString usbProduct = readSysFile(dir.absolutePath() + "/product").trimmed();
            QString usbMfr = readSysFile(dir.absolutePath() + "/manufacturer").trimmed();
            if (usbProduct.isEmpty())
                usbProduct = "Apple Device";
            device.name = "Battery on " + usbProduct;
            device.location = "on " + usbProduct;
            if (usbMfr.isEmpty())
                device.manufacturer = "Apple Inc.";
            else
                device.manufacturer = usbMfr;
            // Read battery level via libimobiledevice if the device is trusted
            device.status = "No battery level";
            if (!iosBatteries.isEmpty()) {
                const QString usbSerial = readSysFile(
                    dir.absolutePath() + "/serial").trimmed();
                if (!usbSerial.isEmpty() && iosBatteries.contains(usbSerial)) {
                    const iOSBatteryInfo &batt = iosBatteries.value(usbSerial);
                    if (batt.capacity >= 0) {
                        device.status = QString("Battery level: %1%").arg(batt.capacity);
                        if (batt.charging)
                            device.status += " (charging)";
                    }
                }
            }
            device.driver = "apple-mfi-fastcharge";
            DriverInfo di = moduleDriverInfo(device.driver);
            device.driverVersion = di.version;
            device.driverAuthor = di.author;
            device.driverDate = di.date;
            device.rawLocation = entry;
            out.append(device);
            continue;
        }

        if (isControllerBattery(canonicalPath)) {
            QString hidName;
            QDir dir(canonicalPath);
            int depth = 0;
            while (dir.cdUp() && ++depth <= 20) {
                QString uevent = readSysFile(dir.absolutePath() + "/uevent");
                for (const QString &line : uevent.split('\n')) {
                    if (line.startsWith("HID_NAME=")) {
                        hidName = line.mid(9).trimmed();
                        break;
                    }
                }
                if (!hidName.isEmpty())
                    break;
                if (dir.absolutePath() == "/sys/devices")
                    break;
            }
            if (hidName.isEmpty())
                hidName = model;
            if (hidName.isEmpty())
                hidName = entry;
            device.name = "Battery on " + hidName;
            device.location = "on " + hidName;
            QString capacity = readSysFile(path + "/capacity");
            QString capacityLevel = readSysFile(path + "/capacity_level");
            if (!capacity.isEmpty())
                device.status = QString("Battery level: %1%").arg(capacity);
            else if (!capacityLevel.isEmpty() && capacityLevel != "Unknown")
                device.status = QString("Battery level: %1").arg(capacityLevel);
            else
                device.status = "Working properly";
        } else {
            if (model.isEmpty())
                device.name = entry;
            else
                device.name = model;
            if (hostModel.isEmpty())
                device.location = friendlyLocation(canonicalPath, entry);
            else
                device.location = "on " + hostModel;
            device.status = "Working properly";
        }

        if (mfr.isEmpty())
            device.manufacturer = "(Standard)";
        else
            device.manufacturer = mfr;
        device.driver = "battery";
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.rawLocation = entry;
        out.append(device);
    }
    return out;
}

QVector<Device> scanGameControllers() {
    QVector<Device> out;
    QDir base("/sys/class/power_supply");
    if (!base.exists())
        return out;

    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString type = readSysFile(path + "/type");
        if (type != "Battery")
            continue;
        if (!isControllerBattery(path))
            continue;

        // walk up sysfs to find the HID device name, driver, and bus type
        QString name;
        QString mfr;
        QString driverName;
        // "BUS:VENDOR:PRODUCT", BUS 0005=BT, 0003=USB
        QString hidId;
        // Unique address (BT MAC for Bluetooth devices)
        QString hidUniq;
        QDir dir(path);
        int depth = 0;
        while (dir.cdUp() && ++depth <= 20) {
            QString dirPath = dir.absolutePath();
            QString uevent = readSysFile(dirPath + "/uevent");
            for (const QString &line : uevent.split('\n')) {
                if (line.startsWith("HID_NAME=") && name.isEmpty())
                    name = line.mid(9).trimmed();
                else if (line.startsWith("DRIVER=") && driverName.isEmpty())
                    driverName = line.mid(7).trimmed();
                else if (line.startsWith("HID_ID=") && hidId.isEmpty())
                    hidId = line.mid(7).trimmed();
                else if (line.startsWith("HID_UNIQ=") && hidUniq.isEmpty())
                    hidUniq = line.mid(9).trimmed();
            }
            if (mfr.isEmpty()) {
                const QString manufacturer = readSysFile(dirPath + "/manufacturer");
                if (!manufacturer.isEmpty())
                    mfr = manufacturer;
            }
            if (!name.isEmpty())
                break;
            if (dirPath == "/sys/devices")
                break;
        }

        if (name.isEmpty())
            name = readSysFile(path + "/model_name");
        if (name.isEmpty())
            name = entry;

        // HID_UNIQ holds the BT MAC for Bluetooth devices; fall back to
        // extracting a MAC from the entry name for older kernel layouts
        QString btAddr = hidUniq;
        if (btAddr.isEmpty()) {
            static const QRegularExpression btAddrRe(
                R"(([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})$)");
            auto addrMatch = btAddrRe.match(entry);
            if (addrMatch.hasMatch())
                btAddr = addrMatch.captured(1);
        }

        QString deviceStatus = "Working properly";

        // Derive manufacturer from HID_ID vendor field (USB vendor ID registry).
        // HID_ID format: "BUS:VENDOR:PRODUCT", vendor field is 8 hex digits,
        // last 4 match the USB/BT vendor IDs used in usb.ids
        QString manufacturer = mfr;
        if (manufacturer.isEmpty() && !hidId.isEmpty()) {
            QStringList hidParts = hidId.split(':');
            if (hidParts.size() >= 2) {
                QString vendorHex = hidParts[1].right(4).toLower();
                QString looked = usbIds().vendors.value(vendorHex);
                if (!looked.isEmpty())
                    manufacturer = looked;
            }
        }

        Device device;
        device.name = name;
        device.manufacturer = manufacturer.isEmpty()
            ? "(Standard game controller)" : manufacturer;
        device.status = deviceStatus;
        device.driver = driverName;
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = entry;
        device.btAddress = btAddr;
        // HID_ID first field is the kernel bus type: 0005=BT, 0003=USB.
        // Fall back to sysfs path heuristics if HID_ID is unavailable
        if (!hidId.isEmpty()) {
            QString busHex = hidId.left(4).toUpper();
            if (busHex == "0005")
                device.location = "connected via Bluetooth";
            else if (busHex == "0003")
                device.location = "connected via USB";
            else
                device.location = "Unknown";
        } else if (!btAddr.isEmpty() || path.contains("/bluetooth/")) {
            device.location = "connected via Bluetooth";
        } else if (path.contains("/usb")) {
            device.location = "connected via USB";
        } else {
            device.location = "Unknown";
        }
        out.append(device);
    }
    return out;
}

QVector<Device> scanRtc() {
    QVector<Device> out;
    QDir base("/sys/class/rtc");
    if (!base.exists())
        return out;
    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString rtcName = readSysFile(path + "/name");
        QString drvToken = rtcName.section(' ', 0, 0);
        Device device;
        device.name = "System CMOS/real time clock";
        device.manufacturer = "(Standard system devices)";
        device.status = "Working properly";
        if (drvToken.isEmpty())
            device.driver = "rtc_cmos";
        else
            device.driver = drvToken;
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.rawLocation = entry;
        device.location = []{
            QString host = dmiProductName();
            if (host.isEmpty())
                return QStringLiteral("ACPI system device");
            return "on " + host;
        }();
        out.append(device);
    }
    return out;
}

QVector<Device> scanPlatformDevices() {
    static const QHash<QString, QString> kKnownPlatform = {
        {"pcspkr",    "System speaker"},
        {"eeepc-wmi", "ASUS WMI device"},
    };
    QVector<Device> out;
    QDir base("/sys/bus/platform/devices");
    if (!base.exists())
        return out;
    QSet<QString> seenDrivers;
    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        QString drvLink = QFileInfo(path + "/driver").symLinkTarget();
        QString drv = QFileInfo(drvLink).fileName();
        if (drv.isEmpty() || seenDrivers.contains(drv))
            continue;
        auto it = kKnownPlatform.constFind(drv);
        if (it == kKnownPlatform.constEnd())
            continue;
        seenDrivers.insert(drv);
        Device device;
        device.name = *it;
        device.manufacturer = "(Standard system devices)";
        device.status = "Working properly";
        device.driver = drv;
        DriverInfo di = moduleDriverInfo(drv);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.rawLocation = entry;
        device.location = []{
            QString host = dmiProductName();
            if (host.isEmpty())
                return QStringLiteral("ACPI system device");
            return "on " + host;
        }();
        out.append(device);
    }
    return out;
}

QVector<Device> scanStorageControllers() {
    return scanPciByClass("01");
}

QVector<Device> scanIdeControllers() {
    return scanPciByClass("0101");
}

QVector<Device> scanUsbControllers() {
    return scanPciByClass("0c03");
}

QVector<Device> scanSystemDevices() {
    QVector<Device> out;
    QDir base("/sys/bus/pci/devices");
    for (const QString &entry : base.entryList(
             QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString path = base.absoluteFilePath(entry);
        QString cls = readHexId(path + "/class");
        QString prefix2 = cls.left(2);
        if (prefix2 == "01" || prefix2 == "02" || prefix2 == "03"
            || prefix2 == "04" || cls.startsWith("0c03"))
            continue;

        Device device = makePciDevice(path);

        if (device.name.contains("Dummy", Qt::CaseInsensitive))
            continue;
        if (device.name.contains("Placeholder", Qt::CaseInsensitive))
            continue;

        device.rawLocation = entry;
        out.append(device);
    }
    return out;
}

QVector<Device> scanBlacklistedMissing(
    const QVector<DeviceCategory> &found) {

    QSet<QString> foundDrivers;
    for (const auto &cat : found)
        for (const auto &dev : cat.devices)
            foundDrivers.insert(dev.driver);

    QFile file(kModprobeConf);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QVector<Device> out;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.startsWith("blacklist "))
            continue;
        QString mod = line.mid(10).trimmed();
        if (mod.isEmpty() || foundDrivers.contains(mod))
            continue;
        static const QRegularExpression kModRe("^[a-zA-Z0-9_-]+$");
        if (!kModRe.match(mod).hasMatch())
            continue;

        Device device;
        device.driver = mod;
        device.disabled = true;

        QString desc = runCmd("modinfo", {"-F", "description", "--", mod});
        if (desc.isEmpty())
            device.name = mod;
        else
            device.name = desc;

        DriverInfo di = moduleDriverInfo(mod);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.status = "Working properly";
        device.manufacturer = "(Standard)";
        device.iconName = "preferences-system";
        out.append(device);
    }
    return out;
}

QVector<Device> scanBluetoothAudio() {
    QVector<Device> out;

    QString devList = runCmd("bluetoothctl", {"devices"}, 3000);
    if (devList.isEmpty())
        return out;

    static const QRegularExpression devRe(
        R"(Device\s+([0-9A-Fa-f:]{17})\s+(.+))");
    static const QSet<QString> kAudioUuids = {
        // Audio Source
        "0000110a",
        // Audio Sink (A2DP)
        "0000110b",
        // Headset
        "00001108",
        // Handsfree (HFP)
        "0000111e",
        // Headset AG
        "00001112",
        // Handsfree AG
        "0000111f",
    };

    for (const QString &line : devList.split('\n')) {
        auto match = devRe.match(line);
        if (!match.hasMatch())
            continue;

        QString addr = match.captured(1);
        QString name = match.captured(2).trimmed();

        QString info = runCmd("bluetoothctl", {"info", addr}, 3000);
        if (info.isEmpty())
            continue;

        bool connected = false;
        bool isAudio = false;
        QString mfr;

        static const QRegularExpression modRe(
            R"(bluetooth:v([0-9A-Fa-f]{4})p)");

        for (const QString &infoLine : info.split('\n')) {
            const QString line = infoLine.trimmed();
            if (line.startsWith("Connected:") && line.contains("yes"))
                connected = true;
            if (line.startsWith("Icon:") && line.contains("audio"))
                isAudio = true;
            for (const QString &uuid : kAudioUuids) {
                if (line.contains(uuid, Qt::CaseInsensitive)) {
                    isAudio = true;
                    break;
                }
            }
            auto mm = modRe.match(line);
            if (mm.hasMatch() && mfr.isEmpty()) {
                QString vid = mm.captured(1).toLower();
                mfr = btCompanyName(vid);
                if (mfr.isEmpty())
                    mfr = usbVendorName(vid);
            }
        }

        if (!connected || !isAudio)
            continue;

        Device device;
        device.name = name;
        if (mfr.isEmpty())
            device.manufacturer = "(Bluetooth audio device)";
        else
            device.manufacturer = mfr;
        device.status = "Working properly";
        device.driver = "btusb";
        DriverInfo di = moduleDriverInfo(device.driver);
        device.driverVersion = di.version;
        device.driverAuthor = di.author;
        device.driverDate = di.date;
        device.isDkms = di.isDkms;
        device.rawLocation = addr;
        device.btAddress = addr;
        device.location = "connected via Bluetooth";
        out.append(device);
    }
    return out;
}

QVector<Device> scanUnknownUsbDevices() {
    QVector<Device> out;
    QDir base("/sys/bus/usb/devices");
    if (!base.exists())
        return out;
    for (const QString &entry : base.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (entry.contains(':') || entry.startsWith("usb"))
            continue;
        if (isBluetoothUsbDevice(entry))
            continue;
        QString path = QFileInfo(
            base.absoluteFilePath(entry)).canonicalFilePath();
        if (readSysFile(path + "/bDeviceClass") == "09")
            continue;
        if (!readSysFile(path + "/product").isEmpty())
            continue;
        QString vendorHex = readSysFile(path + "/idVendor").trimmed().toLower();
        QString productHex = readSysFile(path + "/idProduct").trimmed().toLower();
        if (vendorHex.isEmpty())
            continue;
        Device device;
        QString vendorName = usbIds().vendors.value(vendorHex);
        if (vendorName.isEmpty())
            device.name = QString("Unknown device %1:%2").arg(vendorHex, productHex);
        else
            device.name = QString("Unknown %1 device").arg(vendorName);
        if (vendorName.isEmpty())
            device.manufacturer = "Unknown";
        else
            device.manufacturer = vendorName;
        device.status = "Unknown";
        device.rawLocation = entry;
        device.location = friendlyLocation(path, entry);
        out.append(device);
    }
    return out;
}

// Returns true if the first Usage Page declared in a HID report descriptor
// is vendor-specific (0xFF00–0xFFFF). Used to identify MonsGeek raw-HID
// config interfaces that accept battery query commands
static bool startsWithVendorUsagePage(const quint8 *desc, int len) {
    for (int i = 0; i < len; ) {
        quint8 item = desc[i];
        // A long item, which carries its own length and is skipped whole
        if (item == 0xFE) {
            if (i + 2 >= len)
                break;
            i += 3 + desc[i + 1];
            continue;
        }
        int sz = item & 3;
        int btype = (item >> 2) & 3;
        int btag = (item >> 4) & 0xF;
        int nb;
        if (sz == 3)
            nb = 4;
        else
            nb = sz;
        if (i + 1 + nb > len)
            break;
        quint32 v = 0;
        for (int b = 0; b < nb; ++b)
            v |= (quint32)desc[i + 1 + b] << (8 * b);
        i += 1 + nb;
        // First Usage Page
        if (btype == 1 && btag == 0)
            return (v >> 8) == 0xFF;
    }
    return false;
}

struct MonsGeekBattery { int level = -1; bool charging = false; bool hasData = false; };

// Queries battery level from a MonsGeek keyboard via the proprietary
// GET_DONGLE_STATUS (0xF7) or GET_BATTERY (0x83) feature report command.
// Protocol: HIDIOCSFEATURE sends the command; HIDIOCGFEATURE reads the response.
// Response layout (buf[0] = echoed report ID 0x00):
//   Wireless (0xF7): buf[2]=has_response, buf[3]=battery%, buf[5]=charging
//   Wired    (0x83): buf[1]=0x83 echo,   buf[2]=0xAA ok,   buf[3]=battery%
static MonsGeekBattery queryMonsGeekBattery(int fd, bool isDongle) {
    MonsGeekBattery result;
    quint8 cmd = 0x83u;
    if (isDongle)
        cmd = 0xF7u;
    quint8 cksum = (quint8)(255u - cmd);

    quint8 sbuf[65] = {};
    // sbuf[0] = 0x00 (report ID, already zero)
    sbuf[1] = cmd;
    sbuf[7] = cksum;
    if (::ioctl(fd, HIDIOCSFEATURE(sizeof(sbuf)), sbuf) < 0)
        return result;

    quint8 gbuf[65] = {};
    if (::ioctl(fd, HIDIOCGFEATURE(sizeof(gbuf)), gbuf) < 0)
        return result;

    if (isDongle) {
        // Has_response=0: keyboard not seen by dongle yet
        if (gbuf[2] == 0)
            return result;
        result.hasData  = true;
        result.level    = (int)gbuf[3];
        result.charging = (gbuf[5] != 0);
    } else {
        // 0xAA = success marker
        if (gbuf[2] != 0xAA)
            return result;
        result.hasData = true;
        result.level   = (int)gbuf[3];
    }
    return result;
}

// Minimal HID report descriptor parser: returns the set of report IDs that
// contain an "Absolute State of Charge" (usage 0x35 in Battery System page
// 0x85) or "Remaining Capacity" (usage 0x44 in Power Device page 0x84)
// inside a Feature collection
static QSet<int> hidBatteryReportIds(const quint8 *desc, int len) {
    QSet<int> result;
    int usagePage = 0, reportId = 0;
    bool hasBattUsage = false;

    for (int i = 0; i < len; ) {
        quint8 item = desc[i];
        // Long item
        if (item == 0xFE) {
            if (i + 2 >= len)
                break;
            i += 3 + (int)(quint8)desc[i + 1];
            continue;
        }
        int sz    = item & 0x03;
        int btype = (item >> 2) & 0x03;
        int btag  = (item >> 4) & 0x0F;
        // A size of 3 means four bytes
        int nb = sz;
        if (sz == 3)
            nb = 4;
        if (i + 1 + nb > len)
            break;

        quint32 v = 0;
        for (int b = 0; b < nb; ++b)
            v |= (quint32)(quint8)desc[i + 1 + b] << (8 * b);
        i += 1 + nb;

        switch (btype) {
        // Global
        case 1:
            // Usage Page
            if (btag == 0)
                usagePage = (int)v;
            // Report ID
            if (btag == 8)
                reportId  = (int)v;
            break;
        // Local
        case 2:
            if (btag == 0 && ((usagePage == 0x85 && v == 0x35) ||   // Abs SoC
                              // Remaining Cap
                              (usagePage == 0x84 && v == 0x44)))
                hasBattUsage = true;
            break;
        // Main
        case 0:
            // Feature
            if (btag == 0xB && hasBattUsage)
                result.insert(reportId);
            // Local state resets after any Main item
            hasBattUsage = false;
            break;
        }
    }
    return result;
}

// Sends HIDIOCGFEATURE for the given report ID, returns 0-100 on success, -1
// if the report can't be read or doesn't contain a valid percentage
static int readHidFeatureBattery(int fd, int reportId) {
    quint8 buf[65] = {};
    buf[0] = (quint8)reportId;
    int ret = ::ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);
    if (ret < 0)
        return -1;
    // buf[0] is the echoed report ID; data starts at buf[1].
    // The Absolute State of Charge is 0-100%; scan the first 8 data bytes
    for (int b = 1; b < ret && b <= 8; ++b) {
        if (buf[b] <= 100)
            return (int)buf[b];
    }
    return -1;
}

// Scans /sys/class/hidraw for HID devices that advertise a Battery System
// feature report in their report descriptor but have no kernel power_supply
// entry (e.g. some 2.4 GHz wireless keyboards whose battery the kernel doesn't
// surface via the standard power_supply class)
QVector<Device> scanHidrawBatteries() {
    QVector<Device> out;

    // Collect canonical sysfs paths of HID devices already covered by a
    // kernel power_supply entry so we don't create duplicate entries
    QSet<QString> coveredHidPaths;
    {
        QDir ps("/sys/class/power_supply");
        for (const QString &e : ps.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
            QString canon = QFileInfo(ps.absoluteFilePath(e)).canonicalFilePath();
            if (readSysFile(canon + "/type") == QLatin1String("Battery"))
                coveredHidPaths.insert(canon);
        }
    }

    QDir hidrawDir("/sys/class/hidraw");
    if (!hidrawDir.exists())
        return out;

    QSet<QString> seenHidDevs;

    for (const QString &entry : hidrawDir.entryList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        // Resolve the HID device that backs this hidraw node
        QString hidDevPath = QFileInfo(
            hidrawDir.absoluteFilePath(entry) + "/device"
        ).canonicalFilePath();
        if (hidDevPath.isEmpty() || seenHidDevs.contains(hidDevPath))
            continue;

        // Skip if a power_supply entry already covers this HID device
        bool covered = false;
        for (const QString &cp : coveredHidPaths) {
            if (cp.startsWith(hidDevPath + '/')) {
                covered = true;
                break;
            }
        }
        if (covered)
            continue;

        QString devNode = "/dev/" + entry;

        // O_RDWR is required for HIDIOCGFEATURE; fall back to O_RDONLY for
        // descriptor-only detection when the user only has read permission
        int fd = ::open(devNode.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        bool canGetFeature = (fd >= 0);
        if (fd < 0)
            fd = ::open(devNode.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        // Read the HID report descriptor
        int descSize = 0;
        struct hidraw_report_descriptor rptDesc = {};
        bool gotDesc = (::ioctl(fd, HIDIOCGRDESCSIZE, &descSize) >= 0
                        && descSize > 0);
        if (gotDesc) {
            rptDesc.size = (unsigned int)descSize;
            gotDesc = (::ioctl(fd, HIDIOCGRDESC, &rptDesc) >= 0);
        }

        if (!gotDesc) {
            ::close(fd);
            continue;
        }

        const quint8 *descBytes = reinterpret_cast<const quint8 *>(rptDesc.value);
        int           descLen   = (int)rptDesc.size;

        // Resolve a human-readable name from the HID device's sysfs uevent
        QString hidName, hidId;
        for (const QString &line : readSysFile(hidDevPath + "/uevent").split('\n')) {
            if (line.startsWith("HID_NAME="))
                hidName = line.mid(9).trimmed();
            else if (line.startsWith("HID_ID="))
                hidId = line.mid(7).trimmed();
        }
        if (hidName.isEmpty())
            hidName = readSysFile(
                QFileInfo(hidDevPath + "/../product").canonicalFilePath());
        if (hidName.isEmpty())
            hidName = entry;

        // ── MonsGeek proprietary protocol ────────────────────────────────
        // VID 0x3151, vendor usage page (0xFF__) = the raw-HID config
        // interface that accepts GET_DONGLE_STATUS / GET_BATTERY commands
        {
            struct hidraw_devinfo rawInfo = {};
            bool hasMonsGeek = canGetFeature
                && ::ioctl(fd, HIDIOCGRAWINFO, &rawInfo) >= 0
                && (quint16)rawInfo.vendor == 0x3151u
                && startsWithVendorUsagePage(descBytes, descLen);

            if (hasMonsGeek) {
                quint16 pid      = (quint16)rawInfo.product;
                bool    isDongle = (pid == 0x503Au);
                MonsGeekBattery mb = queryMonsGeekBattery(fd, isDongle);
                ::close(fd);
                seenHidDevs.insert(hidDevPath);

                QString deviceStatus;
                if (mb.hasData && mb.level >= 0) {
                    deviceStatus = QString("Battery level: %1%").arg(mb.level);
                    if (mb.charging)
                        deviceStatus += " (charging)";
                } else if (mb.hasData) {
                    deviceStatus = "Battery level: unknown";
                } else if (isDongle) {
                    deviceStatus = "Battery: keyboard not connected to dongle";
                } else {
                    deviceStatus = "Battery level: unknown";
                }

                Device device;
                device.name         = "Battery on " + hidName;
                device.manufacturer = "(Standard)";
                device.status       = deviceStatus;
                device.driver       = "usbhid";
                device.driverVersion = kernelRelease();
                device.rawLocation  = entry;
                device.location     = "on " + hidName;
                out.append(device);
                continue;
            }
        }

        // ── Standard HID Battery System (0x85) ───────────────────────────
        QSet<int> battIds = hidBatteryReportIds(descBytes, descLen);

        if (battIds.isEmpty()) {
            ::close(fd);
            continue;
        }

        // Attempt to read the battery level via feature report
        int level = -1;
        if (canGetFeature) {
            for (int rid : battIds) {
                level = readHidFeatureBattery(fd, rid);
                if (level >= 0)
                    break;
            }
        }
        ::close(fd);

        seenHidDevs.insert(hidDevPath);

        QString deviceStatus;
        if (level >= 0)
            deviceStatus = QString("Battery level: %1%").arg(level);
        else if (!canGetFeature)
            deviceStatus = "Battery detected (no read permission on " + devNode + ")";
        else
            deviceStatus = "Battery level: unknown";

        Device device;
        device.name = "Battery on " + hidName;
        device.manufacturer = "(Standard)";
        device.status = deviceStatus;
        device.driver = "usbhid";
        device.driverVersion = kernelRelease();
        device.rawLocation = entry;
        device.location = "on " + hidName;
        out.append(device);
    }
    return out;
}

} // namespace

QVector<DeviceCategory> scanDevices() {
    QVector<DeviceCategory> cats;

    auto addIfAny = [&](const QString &name, const QString &icon,
                        QVector<Device> devs) {
        if (!devs.isEmpty())
            cats.append({name, icon, devs});
    };

    auto batteries = scanBatteries();
    batteries += scanHidBatteries();
    batteries += scanHidrawBatteries();
    addIfAny("Batteries", "kded5", batteries);
    addIfAny("Bluetooth Radios", "network-bluetooth", scanBluetooth());
    addIfAny("Game controllers", "input-gaming",
             scanGameControllers());
    addIfAny("Disk drives", "drive-harddisk", scanDisks());
    addIfAny("Display adapters", "video-display", scanPciByClass("03"));
    addIfAny("DVD/CD-ROM drives", "media-optical", scanOpticalDrives());
    addIfAny("Human Interface Devices", "input_devices_settings", scanHidGeneric());
    addIfAny("IDE ATA/ATAPI controllers", "drive-harddisk",
             scanIdeControllers());
    addIfAny("Keyboards", "input-keyboard", scanKeyboards());
    addIfAny("Mice and other pointing devices", "input-mouse", scanMice());
    addIfAny("Monitors", "video-display", scanMonitors());
    addIfAny("Network adapters", "network-card", scanNetwork());
    QSet<QString> portableEntries;
    addIfAny("Portable Devices", "smartphone",
             scanPortableDevices(&portableEntries));
    addIfAny("Processors", "cpu", scanCpus());
    addIfAny("Security Devices", "drive-harddisk-encrypted", scanTpm());
    auto soundDevs = scanSoundCards();
    soundDevs += scanBluetoothAudio();
    addIfAny("Sound, video and game controllers", "kmix", soundDevs);
    addIfAny("Storage controllers", "drive-harddisk",
             scanStorageControllers());
    auto sysDevs = scanSystemDevices();
    sysDevs += scanRtc();
    sysDevs += scanPlatformDevices();
    addIfAny("System devices", "computer", sysDevs);
    addIfAny("Universal Serial Bus controllers",
             "drive-removable-media-usb", scanUsbControllers());
    addIfAny("Universal Serial Bus devices",
             "drive-removable-media-usb", scanUsbDevices(portableEntries));
    addIfAny("Unknown devices", "dialog-question", scanUnknownUsbDevices());

    QVector<Device> missing = scanBlacklistedMissing(cats);
    if (!missing.isEmpty())
        cats.append({"__disabled__", "", missing});

    return cats;
}

} // namespace ControlPanel
