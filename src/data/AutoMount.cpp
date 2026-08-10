#include "AutoMount.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLatin1String>
#include <QStringList>

namespace AutoMount {

namespace {

// The file the Device Auto-Mount page writes, and its keys. The defaults repeat
// Plasma's kcfg, all false, so an untouched machine reads here as it behaves
// there.
constexpr const char *kFile = "kded_device_automounterrc";
constexpr const char *kGeneral = "General";
constexpr const char *kDevices = "Devices";

constexpr const char *kOnAttach = "AutomountOnPlugin";
constexpr const char *kOnLogin = "AutomountOnLogin";
constexpr const char *kEnabled = "AutomountEnabled";

// The per-device overrides, one subgroup of [Devices] per device UDI.
constexpr const char *kForceOnAttach = "ForceAttachAutomount";
constexpr const char *kForceOnLogin = "ForceLoginAutomount";

// NoGlobals: kdeglobals has no say here, and cascading it in would put a
// desktop-wide file in the write path for one page's setting.
KConfig openConfig()
{
    return KConfig(QLatin1String(kFile), KConfig::NoGlobals);
}

// The master switch the KDED module checks at startup, which the page keeps as
// an OR over everything that could ask for a mount. Computed the same way here.
bool anythingWantsAutomount(KConfig &config)
{
    const KConfigGroup general = config.group(QLatin1String(kGeneral));
    if (general.readEntry(kOnLogin, false) || general.readEntry(kOnAttach, false))
        return true;

    const KConfigGroup devices = config.group(QLatin1String(kDevices));
    const QStringList udis = devices.groupList();
    for (const QString &udi : udis) {
        const KConfigGroup device = devices.group(udi);
        if (device.readEntry(kForceOnAttach, false) || device.readEntry(kForceOnLogin, false))
            return true;
    }
    return false;
}

// The two calls the page makes after saving. A module that started with
// automounting off has unloaded itself, so re-enabling must load it by hand.
void setKdedModuleLoaded(bool loaded)
{
    QDBusConnection dbus = QDBusConnection::sessionBus();
    const QString module = QStringLiteral("device_automounter");

    auto call = [&dbus](const QString &method, const QVariantList &args) {
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kded6"),
                                                          QStringLiteral("/kded"),
                                                          QStringLiteral("org.kde.kded6"),
                                                          method);
        msg.setArguments(args);
        dbus.call(msg, QDBus::NoBlock);
    };

    call(QStringLiteral("setModuleAutoloading"), {module, loaded});
    call(loaded ? QStringLiteral("loadModule") : QStringLiteral("unloadModule"), {module});
}

} // namespace

bool onAttachEnabled()
{
    KConfig config = openConfig();
    return config.group(QLatin1String(kGeneral)).readEntry(kOnAttach, false);
}

void setOnAttachEnabled(bool enabled)
{
    KConfig config = openConfig();
    KConfigGroup general = config.group(QLatin1String(kGeneral));

    general.writeEntry(kOnAttach, enabled);

    // Read back through the group rather than reusing `enabled`: the login box
    // or a per-device override may still want automounting.
    const bool master = anythingWantsAutomount(config);
    general.writeEntry(kEnabled, master);

    config.sync();
    setKdedModuleLoaded(master);
}

bool isConfigurable()
{
    KConfig config = openConfig();
    const KConfigGroup general = config.group(QLatin1String(kGeneral));

    return !general.isImmutable()
        && !general.isEntryImmutable(QLatin1String(kOnAttach))
        && !general.isEntryImmutable(QLatin1String(kEnabled));
}

} // namespace AutoMount
