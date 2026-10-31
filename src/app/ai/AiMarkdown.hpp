// =============================================================================
//  app/ai/AiMarkdown.hpp - 1.20.2 : les blocs de code des reponses de l'IA
// -----------------------------------------------------------------------------
//  Une reponse de l'IA peut contenir des blocs de code entre balises :
//
//      ```st
//      IF Niveau > 80.0 THEN Alarme := TRUE; END_IF;
//      ```
//
//  Le panneau les montre a part (police a chasse fixe, fond, la langue) avec un
//  bouton Copier qui copie le code EXACT, sans les balises. Pendant que la
//  reponse arrive, un bloc pas encore ferme est deja un bloc (closed = false).
//  Rien d'autre n'est interprete ici : le reste est du texte.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace app::ai::markdown {

struct Segment {
    enum class Kind { Text, Code };
    Kind        kind{Kind::Text};
    std::string text;          // Text : le texte ; Code : le code, sans les balises ni la derniere fin de ligne
    std::string lang;          // Code : la langue donnee apres la balise (st, cpp, json...), peut etre vide
    bool        closed{true};  // Code : faux tant que la balise de fin n'est pas arrivee
};

// Coupe `text` en textes et blocs de code. Une balise : une ligne qui commence (au plus 3 espaces avant) par ``` ou ~~~ ;
// la fin : la meme suite (au moins aussi longue), seule sur sa ligne. Sans bloc : un seul segment Text (vide : aucun).
[[nodiscard]] std::vector<Segment> split(std::string_view text);

// Vrai si `text` contient au moins un bloc de code.
[[nodiscard]] bool hasCode(std::string_view text);

// Le nom affiche de la langue d'un bloc (<< st >> -> << ST >>, << cpp >> -> << C++ >>, vide -> << Code >>).
[[nodiscard]] std::string languageLabel(std::string_view lang);

} // namespace app::ai::markdown
