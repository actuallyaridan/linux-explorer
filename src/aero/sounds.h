#pragma once

// The Windows 7 sound theme

#include <QString>

namespace Aero {

// Plays a named sound, and a missing file is silence
void playSound(const QString &name);

// Where the sound files live
void setSoundThemePath(const QString &directory);

} // namespace Aero
