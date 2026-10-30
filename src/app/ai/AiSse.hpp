// =============================================================================
//  app/ai/AiSse.hpp - l'Assistant IA : le flux d'evenements du serveur (SSE)
// -----------------------------------------------------------------------------
//  UN LECTEUR PUR, sans reseau ni fil : on lui donne les morceaux tels qu'ils
//  arrivent (coupes n'importe ou : au milieu d'une ligne, entre \r et \n, au
//  milieu d'un caractere UTF-8), il rend les evenements complets. Le format
//  (text/event-stream) :
//
//      event: content_block_delta        le type (defaut : "message")
//      data: {"type": ...}               une ligne ; plusieurs lignes data: se
//      data: ...                         joignent par \n
//      id: 42                            l'identifiant (garde, non utilise)
//      : un commentaire                  ignore
//      <ligne vide>                      l'evenement est complet
//
//  Les fins de ligne : \n, \r\n ou \r seul. Un BOM UTF-8 en tete est saute. Un
//  evenement sans "data" ne compte pas (la regle du format).
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

struct SseEvent {
    std::string event{"message"};
    std::string data;
    std::string id;
};

class SseReader {
public:
    // Les evenements completes par ce morceau, dans l'ordre, ajoutes a `out`.
    void feed(std::string_view chunk, std::vector<SseEvent>& out);
    // La fin du flux : une derniere ligne sans fin de ligne est lue ; un evenement sans sa
    // ligne vide est perdu (la regle du format : le serveur l'aurait termine).
    void finish(std::vector<SseEvent>& out);
    // Il reste un evenement commence (des lignes sans leur ligne vide) : le flux a ete coupe.
    [[nodiscard]] bool pending() const noexcept { return !line_.empty() || hasData_ || !event_.empty(); }
    void reset();

private:
    void body(std::string_view chunk, std::vector<SseEvent>& out);
    void line(std::string_view l, std::vector<SseEvent>& out);

    std::string line_;            // la ligne en cours (sans sa fin)
    bool        lastCr_{false};   // le morceau precedent finissait par \r : un \n en tete est la meme fin
    bool        started_{false};  // le BOM eventuel est passe
    std::string bom_;             // les premiers octets, tant qu'ils peuvent etre un BOM
    std::string event_, data_, id_;
    bool        hasData_{false};
};

} // namespace app::ai
