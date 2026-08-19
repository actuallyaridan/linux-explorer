#pragma once

#include <QString>

class KFilePlacesModel;
class QModelIndex;

// Win7 names a drive by its label and then its drive letter, and with no drive
// letters here the kernel's device node stands in
//
// Rename dialogs prefill the bare label, or committing the node back would make
// the suffix permanent and append a second one next time
namespace DriveLabel {

// The plain places model label when there is no device to bracket
QString forPlace(const KFilePlacesModel *places, const QModelIndex &index);

QString deviceNode(const KFilePlacesModel *places, const QModelIndex &index);

} // namespace DriveLabel
