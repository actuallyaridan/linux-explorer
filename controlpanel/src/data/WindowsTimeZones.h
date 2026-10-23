#pragma once

#include <QString>
#include <QStringList>
#include <QList>

// A Windows-style time-zone entry: the grouped display name Windows shows (with
// its "(UTC±HH:MM)" prefix) paired with the real IANA zone id used to actually
// set the zone via `timedatectl`. `aliases` lists the other IANA ids Windows
// folds into the same group, so the current system zone can be matched back to
// its Windows label.
struct WinTimeZone {
    QString     display;
    QString     iana;
    QStringList aliases;
};

// The Windows 7 time-zone list, in the same top-to-bottom (by offset) order the
// Change-time-zone dropdown uses.
const QList<WinTimeZone> &windowsTimeZones();

// The Windows display label for an IANA zone id, or an empty string if the id
// isn't one of the grouped zones (caller then synthesizes a "(UTC±HH:MM) id"
// label from the live offset).
QString windowsDisplayForIana(const QString &iana);
