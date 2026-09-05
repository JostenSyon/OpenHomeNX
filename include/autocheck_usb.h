#pragma once
#include <string>

// Global toggle (Import Settings screen): when enabled, a USB hotplug also
// probes "<device>:/roms/saves/" and "<device>:/roms/" on every mounted
// drive for Gen3 GBA saves, with no need to configure a "usb:" import path
// by hand. Off by default — existing installs shouldn't start scanning USB
// drives without the user opting in.
bool loadAutoCheckUsb(const std::string& basePath);
void saveAutoCheckUsb(const std::string& basePath, bool enabled);
