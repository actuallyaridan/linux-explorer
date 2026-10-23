#pragma once

namespace ControlPanel {

// The Control Panel was a program of its own, linux-control, with its own
// settings file and benchmark results. This copies them into Explorer's once,
// so nobody who used it loses their branding, clocks or experience index, and
// it does nothing on every run after the first
void importOldSettings();

} // namespace ControlPanel
