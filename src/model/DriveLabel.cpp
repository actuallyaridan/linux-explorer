#include "DriveLabel.h"

#include <KFilePlacesModel>

#include <Solid/Block>
#include <Solid/Device>

#include <QModelIndex>

namespace DriveLabel {

QString deviceNode(const KFilePlacesModel *places, const QModelIndex &index)
{
    if (!places || !index.isValid())
        return {};

    // deviceForIndex hands back an invalid device for any row that is not one,
    // and an invalid device yields a null interface, so no group-type check is
    // needed here.
    const Solid::Device device = places->deviceForIndex(index);
    const Solid::Block *block = device.as<Solid::Block>();
    if (!block)
        return {};

    // Solid reports the full path ("/dev/nvme0n1p1"), but Windows shows the name
    // rather than the object it resolves to, so only the last segment belongs
    // in the brackets.
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

    // A label that is already the node, or already carries it, is left alone:
    // the places model falls back to the node for an unlabelled volume.
    if (label == node
        || label.contains(QLatin1Char('(') + node + QLatin1Char(')'))) {
        return label;
    }

    return QStringLiteral("%1 (%2)").arg(label, node);
}

} // namespace DriveLabel
