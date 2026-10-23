#include "Branding.h"

#include <QSettings>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QDate>
#include <QDir>

namespace Branding {

// All settings live under [Branding] in the app's default QSettings store.
namespace {

QString key(const char *name)
{
    return QStringLiteral("Branding/") + QLatin1String(name);
}

} // namespace

Style style()
{
    QSettings s;
    const int v = s.value(key("style"), int(Style::Distro)).toInt();
    return v == int(Style::Windows7) ? Style::Windows7 : Style::Distro;
}

void setStyle(Style s)
{
    QSettings st;
    st.setValue(key("style"), int(s));
}

bool useWindowsNames()
{
    QSettings s;
    return s.value(key("useWindowsNames"), false).toBool();
}

void setUseWindowsNames(bool on)
{
    QSettings s;
    s.setValue(key("useWindowsNames"), on);
}

// ---- Word substitution ----------------------------------------------------

QString os()
{
    return useWindowsNames() ? QStringLiteral("Windows") : QStringLiteral("Linux");
}

QString brand(const QString &canonical)
{
    if (!useWindowsNames())
        return canonical;
    QString out = canonical;
    out.replace(QRegularExpression(QStringLiteral("\\bLinux\\b")),
                QStringLiteral("Windows"));
    return out;
}

QString debrand(const QString &shown)
{
    if (!useWindowsNames())
        return shown;
    QString out = shown;
    out.replace(QRegularExpression(QStringLiteral("\\bWindows\\b")),
                QStringLiteral("Linux"));
    return out;
}

// ---- Edition badge --------------------------------------------------------

bool fakeBadgeActive()
{
    return style() == Style::Windows7;
}

QString editionName(const QString &realPretty)
{
    if (fakeBadgeActive())
        return QStringLiteral("Windows 7 Ultimate");
    return realPretty;
}

QString copyrightHolder(const QString &distroVendor)
{
    if (fakeBadgeActive())
        return QStringLiteral("Microsoft Corporation");
    // The real distro's org name is a proper noun, so it isn't run through the
    // Linux->Windows rename; fall back to a generic phrase if none was resolved.
    return distroVendor.isEmpty() ? QStringLiteral("The Linux community")
                                  : distroVendor;
}

int copyrightYear()
{
    // Windows 7 was released in 2009; the fake badge reads best with that year.
    if (fakeBadgeActive())
        return 2009;

    // For the real distro, use the year the running kernel was built, the last
    // 4-digit year in /proc/version's trailing build date (e.g. "... 2026 ...").
    QFile f(QStringLiteral("/proc/version"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(f.readAll());
        static const QRegularExpression re(QStringLiteral("\\b((?:19|20)\\d\\d)\\b"));
        int year = 0;
        auto it = re.globalMatch(text);
        while (it.hasNext())
            year = it.next().captured(1).toInt();   // keep the last (the date)
        if (year > 0)
            return year;
    }
    return QDate::currentDate().year();
}

// The Windows 7 orb. On AeroThemePlasma the active icon theme ships this as
// "distributor-logo" (places context, up to 256px), which is exactly what KDE's
// own "About this System" shows. We ask the theme for it and let Qt pick the
// 256px raster and downscale for smaller requests; a couple of fallbacks keep
// something sensible on themes without it.
QIcon windowsLogo()
{
    for (const QString &name : { QStringLiteral("distributor-logo"),
                                 QStringLiteral("start-here"),
                                 QStringLiteral("windows"),
                                 QStringLiteral("computer") }) {
        QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            return icon;
    }
    return QIcon();
}

namespace {

// Mirror KDE's kcm_about-distro ("About this System"): the AeroThemePlasma
// branding ships a high-resolution glossy Windows 7 orb whose path is recorded
// as LogoPath in kcm-about-distrorc. Finding that PNG lets our badge match KDE
// exactly (the icon theme only carries the orb up to 48px). Returns "" if the
// branding is not installed.
QString brandingOrbPath()
{
    QStringList rcCandidates;
    const auto cfgDirs = QStandardPaths::standardLocations(
        QStandardPaths::GenericConfigLocation);
    for (const QString &d : cfgDirs) {
        rcCandidates << d + QStringLiteral("/kcm-about-distrorc")
                     << d + QStringLiteral("/aerothemeplasma/kcm-about-distrorc");
    }
    rcCandidates << QStringLiteral("/etc/xdg/kcm-about-distrorc")
                 << QStringLiteral("/etc/xdg/aerothemeplasma/kcm-about-distrorc");

    for (const QString &rc : rcCandidates) {
        if (!QFileInfo::exists(rc))
            continue;
        QSettings cfg(rc, QSettings::IniFormat);
        QString logo = cfg.value(QStringLiteral("General/LogoPath")).toString();
        if (logo.isEmpty())
            continue;
        if (QDir::isRelativePath(logo))   // resolve against the branding data dir
            logo = QStringLiteral("/usr/share/aerothemeplasma/branding/") + logo;
        if (QFileInfo::exists(logo))
            return logo;
    }
    // Well-known install path as a last resort.
    const QString known =
        QStringLiteral("/usr/share/aerothemeplasma/branding/kcminfo.png");
    return QFileInfo::exists(known) ? known : QString();
}

} // namespace

QPixmap windowsOrb(int px)
{
    // Preferred: the high-res branding orb KDE's About page uses.
    const QString path = brandingOrbPath();
    if (!path.isEmpty()) {
        QPixmap orb(path);
        if (!orb.isNull())
            return orb.scaled(px, px, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation);
    }

    // Fallback: the icon theme only draws the orb at <=48px (its 256px raster is
    // the bare flag), so take the 48px orb and scale it up.
    const QIcon icon = windowsLogo();
    QPixmap orb = icon.pixmap(48, 48);
    if (!orb.isNull() && px != orb.width())
        orb = orb.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return orb;
}

} // namespace Branding
