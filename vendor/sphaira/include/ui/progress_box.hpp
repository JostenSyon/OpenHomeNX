// SHIM OpenHomeNX (non Sphaira): dichiara solo i metodi di ui::ProgressBox
// usati dal percorso forwarder di owo.cpp. Implementati in shim.cpp con
// DebugLog (niente UI Sphaira trascinata). Se Sphaira cambia queste firme,
// il build rompe ed è il segnale di riallineare.
#pragma once

#include <switch.h>
#include <span>
#include <string>
#include <vector>

namespace sphaira::ui {

struct ProgressBox {
    ProgressBox& SetTitle(const std::string& title);
    ProgressBox& SetImageDataConst(std::span<const u8> data);
    ProgressBox& NewTransfer(const std::string& transfer);
    ProgressBox& UpdateTransfer(s64 offset, s64 size);
};

} // namespace sphaira::ui
