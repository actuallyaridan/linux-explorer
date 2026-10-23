#pragma once

#include "Settings.h"

// The two start menu entries, File Explorer and Control Panel, and which of
// them the mode leaves out
//
// An entry is hidden by a copy of its desktop file in the user's own
// applications folder, which the desktop reads in place of the installed one,
// and Hidden=true in that copy makes it as good as uninstalled, so with the
// Control Panel alone folders also stop opening here and go to whichever file
// manager is next in line
namespace MenuEntries {

// Hides whichever entry the mode leaves out and brings the other back, and
// asks the desktop to read its entries again only when something changed
void apply(Settings::AppMode mode);

} // namespace MenuEntries
