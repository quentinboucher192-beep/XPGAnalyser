// =============================================================================
//  sim/Json.hpp - 1.13.0 : lire et ecrire du JSON (dialecte IHM)
// -----------------------------------------------------------------------------
//  Un analyseur et un serialiseur JSON ecrits a la main, sans dependance
//  (MSVC / MinGW / Linux / WASM : du calcul pur, pas d'I/O, pas d'exception qui
//  s'echappe). Le JSON est porte par les valeurs riches du dialecte :
//    - un objet { } -> MAP[STRING] OF ANY (les valeurs : des boites ANY) ;
//    - un tableau [ ] -> LIST OF ANY (les cases : des boites ANY) ;
//    - une chaine -> STRING (UTF-8, \n \t \uXXXX decodes) dans une boite ANY ;
//    - un nombre -> LINT s'il tient entier sans point ni exposant, sinon LREAL ;
//    - true / false -> BOOL ; null -> une boite ANY vide (JSON null).
//  Reserve au dialecte IHM (JSON_LIRE / JSON_ECRIRE), jamais au ST de l'automate.
// =============================================================================
#pragma once

#include "Rich.hpp"

#include <string>
#include <string_view>

namespace sim {

// Le resultat d'une lecture ou d'une ecriture : ok, sinon `message` (la position citee pour une lecture,
// le genre refuse pour une ecriture). Le message contient « JSON » (code E_JSON au CATCH).
struct JsonStatus {
    bool        ok{true};
    std::string message;
};

// Analyse un texte JSON. Rend l'objet (MAP[STRING] OF ANY), le tableau (LIST OF ANY), ou une boite ANY
// (scalaire / null) ; nullptr + status.message (« JSON mal forme (position 12) : ... ») si le texte est
// mal forme, trop imbrique ou trop long.
[[nodiscard]] ObjRef parseJson(std::string_view text, JsonStatus& status);

// Serialise une valeur en JSON (INDENT : deux espaces par niveau ; sinon compact). Faux + status.message
// si la valeur ne se met pas en JSON (un REAL infini ou NAN, une reference, un pointeur, un rappel, un
// iterateur). Une boite ANY, une MAP / LIST / SET / ARRAY / TUPLE, une structure (ses membres), un
// scalaire sont acceptes.
[[nodiscard]] bool writeJson(const Obj& value, bool indent, std::string& out, JsonStatus& status);

} // namespace sim
