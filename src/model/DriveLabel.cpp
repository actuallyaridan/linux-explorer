#include "DriveLabel.h"

#include <KFilePlacesModel>

#include <Solid/Block>
#include <Solid/Device>
#include <Solid/StorageVolume>

#include <QHash>
#include <QModelIndex>
#include <QObject>

namespace DriveLabel {

// Solid reports the kernel's lowercase name, so the common ones are mapped to
// how they are usually written
static QString formatFsType(const QString &fsType)
{
    static const QHash<QString, QString> known = {
        {QStringLiteral("ntfs"), QStringLiteral("NTFS")},
        {QStringLiteral("vfat"), QStringLiteral("FAT32")},
        {QStringLiteral("msdos"), QStringLiteral("FAT16")},
        {QStringLiteral("exfat"), QStringLiteral("exFAT")},
        {QStringLiteral("ext2"), QStringLiteral("ext2")},
        {QStringLiteral("ext3"), QStringLiteral("ext3")},
        {QStringLiteral("ext4"), QStringLiteral("ext4")},
        {QStringLiteral("btrfs"), QStringLiteral("Btrfs")},
        {QStringLiteral("xfs"), QStringLiteral("XFS")},
        {QStringLiteral("f2fs"), QStringLiteral("F2FS")},
        {QStringLiteral("iso9660"), QStringLiteral("CDFS")},
        {QStringLiteral("udf"), QStringLiteral("UDF")},
        {QStringLiteral("swap"), QStringLiteral("Swap")},
        {QStringLiteral("linux_raid_member"), QStringLiteral("RAID")},
        {QStringLiteral("crypto_luks"), QStringLiteral("LUKS")},
    };

    const auto it = known.constFind(fsType.toLower());
    if (it != known.constEnd())
        return it.value();

    // An empty or otherwise unrecognised type is what Explorer calls RAW: a
    // partition with no filesystem the OS can make sense of
    return fsType.isEmpty() ? QObject::tr("RAW") : fsType.toUpper();
}

QString deviceNode(const KFilePlacesModel *places, const QModelIndex &index)
{
    if (!places || !index.isValid())
        return {};

    // An invalid device yields a null interface, so no type check is needed
    const Solid::Device device = places->deviceForIndex(index);
    const Solid::Block *block = device.as<Solid::Block>();
    if (!block)
        return {};

    // Solid reports the full path, and only the last segment belongs in brackets
    return block->device().section(QLatin1Char('/'), -1);
}

QString forPlace(const KFilePlacesModel *places, const QModelIndex &index)
{
    if (!places || !index.isValid())
        return {};

    const QString label = places->text(index);
    const QString node = deviceNode(places, index);
    if (node.isEmpty() || label.isEmpty())
        return label;

    // The places model falls back to the node for an unlabelled volume, so a
    // label that already is or carries the node is left alone
    if (label == node
        || label.contains(QLatin1Char('(') + node + QLatin1Char(')'))) {
        return label;
    }

    return QStringLiteral("%1 (%2)").arg(label, node);
}

QString fileSystemType(const KFilePlacesModel *places, const QModelIndex &index)
{
    if (!places || !index.isValid())
        return {};

    const Solid::Device device = places->deviceForIndex(index);
    const Solid::StorageVolume *volume = device.as<Solid::StorageVolume>();
    if (!volume)
        return QObject::tr("RAW");

    // Unused/unformatted media has no fsType either, and reads the same as
    // one the kernel failed to recognise
    if (volume->usage() == Solid::StorageVolume::Unused)
        return QObject::tr("RAW");

    return formatFsType(volume->fsType());
}

} // namespace DriveLabel
