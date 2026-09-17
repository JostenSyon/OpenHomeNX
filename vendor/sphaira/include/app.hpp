// SHIM OpenHomeNX (non Sphaira): solo App::Notify usato dal percorso
// forwarder di owo.cpp. Niente App Sphaira trascinata.
#pragma once

#include <string>

namespace sphaira {

struct App {
    static void Notify(const std::string& msg);
};

} // namespace sphaira
