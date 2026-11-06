// =============================================================================
//  deploy/Journal.hpp - 1.28.0 : le journal de l'agent
// -----------------------------------------------------------------------------
//  <dossier>/journal/AAAA-MM-JJ.log (la date locale), une ligne par operation :
//      HH:MM:SS op-1a2b3c4d STATUS ok (PC-DEV 192.168.1.20)
//  "-" a la place de l'identifiant pour ce que l'agent fait de lui-meme
//  (demarrage, arret). Sur entre fils ; GET_LOGS rend les dernieres lignes.
// =============================================================================
#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace deploy {

class Journal {
public:
    explicit Journal(std::string agentDir);
    // Ajoute une ligne (les retours a la ligne du detail deviennent des espaces).
    void write(std::string_view opId, std::string_view op, std::string_view detail);
    // Les `count` dernieres lignes (le fichier du jour, puis les precedents s'il en manque).
    [[nodiscard]] std::vector<std::string> tail(std::size_t count) const;
    // Le dossier des journaux et le fichier du jour.
    [[nodiscard]] std::string directory() const;
    [[nodiscard]] std::string todayPath() const;

private:
    std::string dir_;
    mutable std::mutex mutex_;
};

} // namespace deploy
