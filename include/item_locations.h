#pragma once
#include <string>
#include <vector>

// Helper umano: dove si trova lo strumento richiesto per lo scambio.
namespace ItemLocations {

struct Where {
    std::string gameTag; // es. "O/A/C", "R/Z/S/E", "D/P/PT/HG/SS"
    std::string how;     // es. "Selvatico Magnemite 2%", "Torre Lotta 48 BP"
};

std::string itemName(uint16_t modernId);
std::vector<Where> locations(uint16_t modernId);

// Formatta in una riga per footer picker, es.
// "Metal Coat — O/A/C: Selvatico Magnemite 2% | R/Z/S/E: ... "
std::string footerLine(uint16_t modernId);

} // namespace ItemLocations
