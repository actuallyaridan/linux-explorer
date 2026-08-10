#pragma once

#include <QString>

class KFilePlacesModel;
class QModelIndex;

// Windows 7 names a drive "Data (D:)": the volume's own label, then the
// identifier the system knows it by. Linux has no drive letters, so the
// kernel's device node stands in and the drive reads "Data (nvme0n1p1)".
//
// This does not contradict Branding.h on leaving volume labels alone: the label
// is still exactly what its owner set, with the node appended where Windows
// puts the letter. It is also the more useful half here, two partitions often
// being labelled the same.
//
// Renaming is the exception: the dialogs prefill the raw label, since
// committing the node back would make the suffix permanent and then append a
// second one next time.
namespace DriveLabel {

// "Data (nvme0n1p1)", or the plain places-model label for an entry with no
// block device behind it to put in the brackets.
QString forPlace(const KFilePlacesModel *places, const QModelIndex &index);

// The bare node name ("nvme0n1p1"), or an empty string when there is none.
QString deviceNode(const KFilePlacesModel *places, const QModelIndex &index);

} // namespace DriveLabel
