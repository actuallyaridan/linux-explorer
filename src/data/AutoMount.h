#pragma once

// Plasma's "Device Auto-Mount" setting: the All Devices row, On Attach column.
//
// Not one of ours. It lives in kded_device_automounterrc, written by System
// Settings' Device Auto-Mount page and acted on by the device_automounter KDED
// module. Nothing here mounts anything; it only flips the desktop's own switch,
// so the mount dialog's checkbox and that page are two views of one setting.
//
// Three things in that file matter, and only one of them is ours:
//
//   AutomountOnPlugin        the All Devices / On Attach box. This one.
//   AutomountOnLogin         the All Devices / On Login box, left alone.
//   AutomountUnknownDevices  the "media that have never been mounted before"
//                            box under the list, its own control on the page
//                            and not part of what On Attach means.
//
// AutomountEnabled is a fourth key but not a fourth checkbox: the page derives
// it on save from whether anything asks for auto-mounting at all. It has to be
// kept in step, the KDED module reading it once at startup and unloading itself
// when it is false.
namespace AutoMount {

// AutomountOnPlugin. A device may still be force-mounted by its own row in the
// page's list; those overrides are read but never written here.
bool onAttachEnabled();

// Writes the setting, then does what the page does after a save: recompute
// AutomountEnabled and tell KDED to load or unload the module. Without that
// second half, switching this back on would wait for the next login, an
// unloaded module not noticing the file change.
void setOnAttachEnabled(bool enabled);

// False when the config is locked down (a kiosk profile, a read-only file), so
// the checkbox can be disabled rather than silently doing nothing.
bool isConfigurable();

} // namespace AutoMount
