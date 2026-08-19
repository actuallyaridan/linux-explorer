#pragma once

// Plasma's device auto mount setting, which lives in the desktop's own config
// and is acted on by a KDED module, so nothing here mounts anything
namespace AutoMount {

// Per device overrides are read but never written here
bool onAttachEnabled();

// Also tells KDED to load or unload the module, since an unloaded one would
// not notice the file change until the next login
void setOnAttachEnabled(bool enabled);

// False when the config is locked down, so the checkbox can be disabled
bool isConfigurable();

} // namespace AutoMount
