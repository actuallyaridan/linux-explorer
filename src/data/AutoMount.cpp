#include "AutoMount.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLatin1String>
#include <QStringList>

namespace AutoMount {

namespace {

// The file the desktop's auto mount page writes, with its own defaults
constexpr const char *kFile = "kded_device_automounterrc";
constexpr const char *kGeneral = "General";
constexpr const char *kDevices = "Devices";

constexpr const char *kOnAttach = "AutomountOnPlugin";
constexpr const char *kOnLogin = "AutomountOnLogin";
constexpr const char *kEnabled = "AutomountEnabled";

// One subgroup per device
constexpr const char *kForceOnAttach = "ForceAttachAutomount";
constexpr const char *kForceOnLogin = "ForceLoginAutomount";

// The desktop wide config has no say here
KConfig openConfig()
{
    return KConfig(QLatin1String(kFile), KConfig::NoGlobals);
}

// The master switch KDED checks at startup, true if anything at all could ask
// for a mount
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

// A module that started with automounting off has unloaded itself, so
// switching this back on has to load it by hand
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

    // Read back, since the login box or an override may still want automounting
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
