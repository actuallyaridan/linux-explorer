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

} // namespace DriveLabel
