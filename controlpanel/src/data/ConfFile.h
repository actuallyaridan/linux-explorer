#pragma once

#include <QFile>
#include <QString>
#include <QStringView>

// Read a single shell-style `KEY=value` field out of a config file, stripping
// surrounding single or double quotes. ufw's config files (/etc/ufw/ufw.conf,
// /etc/default/ufw) are world-readable sourced shell fragments, so this reads
// the same state KDE's firewall module consults, no root required. Shared by
// the Firewall and Action Center pages.
inline QString readConfField(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    const QString text = QString::fromUtf8(f.readAll());
    const QString prefix = key + QLatin1Char('=');
    for (const QStringView &raw : QStringView(text).split(u'\n')) {
        const QStringView line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#')) || !line.startsWith(prefix))
            continue;
        QString val = line.mid(prefix.size()).trimmed().toString();
        if (val.size() >= 2
            && ((val.startsWith('"') && val.endsWith('"'))
                || (val.startsWith('\'') && val.endsWith('\''))))
            val = val.mid(1, val.size() - 2);
        return val;
    }
    return QString();
}
