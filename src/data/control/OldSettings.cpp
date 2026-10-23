#include "data/control/OldSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace ControlPanel {

namespace {

// Set once the import has run, whether or not there was anything to import
const QString kImportedKey = QStringLiteral("ControlPanel/importedOldSettings");

// The old program's own name for both its organisation and application, which
// is what QSettings and QStandardPaths built its paths from
const QString kOldName = QStringLiteral("controlpanel");

// The one setting the two programs now share, Explorer's
const QString kWindowsNamesKey = QStringLiteral("Branding/useWindowsNames");

// Branding moved under the Control Panel's group, Explorer having a Branding
// group of its own, and everything else keeps its old key
QString newKeyFor(const QString &oldKey)
{
    const QString branding = QStringLiteral("Branding/");
    if (oldKey.startsWith(branding))
        return QStringLiteral("ControlPanel/") + oldKey;
    return oldKey;
}

// Either program having said Windows is enough, since both mean the same
void addWindowsNames(QSettings &current, const QVariant &value)
{
    if (value.toBool())
        current.setValue(kWindowsNamesKey, true);
}

void importSettingsFile()
{
    const QString folder =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString path = folder + QLatin1Char('/') + kOldName + QLatin1Char('/')
                       + kOldName + QStringLiteral(".conf");
    if (!QFile::exists(path))
        return;

    const QSettings old(path, QSettings::IniFormat);
    QSettings current;

    // Never over a value already set here, which is newer than the old file
    const QStringList keys = old.allKeys();
    for (const QString &oldKey : keys) {
        if (oldKey == kWindowsNamesKey) {
            addWindowsNames(current, old.value(oldKey));
            continue;
        }

        const QString newKey = newKeyFor(oldKey);
        if (!current.contains(newKey))
            current.setValue(newKey, old.value(oldKey));
    }
}

void importBenchmarkResults()
{
    // AppDataLocation, which is where the benchmark keeps them, put together
    // from the old names rather than Explorer's
    const QString data =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString fileName = QStringLiteral("winsat-datastore.ini");
    const QString oldPath = data + QLatin1Char('/') + kOldName + QLatin1Char('/')
                          + kOldName + QLatin1Char('/') + fileName;

    const QString newFolder =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString newPath = newFolder + QLatin1Char('/') + fileName;

    if (!QFile::exists(oldPath) || QFile::exists(newPath))
        return;

    QDir().mkpath(newFolder);
    QFile::copy(oldPath, newPath);
}

} // namespace

void importOldSettings()
{
    QSettings settings;

    // Left by a build from before the two settings were merged
    const QString separateKey = QStringLiteral("ControlPanel/Branding/useWindowsNames");
    if (settings.contains(separateKey)) {
        addWindowsNames(settings, settings.value(separateKey));
        settings.remove(separateKey);
    }

    if (settings.value(kImportedKey, false).toBool())
        return;

    importSettingsFile();
    importBenchmarkResults();

    // Copied rather than moved, so the old files are still there for anyone
    // going back to the separate program
    settings.setValue(kImportedKey, true);
}

} // namespace ControlPanel
