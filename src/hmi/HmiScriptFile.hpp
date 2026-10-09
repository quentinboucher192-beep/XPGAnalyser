// =============================================================================
//  hmi/HmiScriptFile.hpp - 1.11.3 : exporter et importer les scripts d'une vue
//                          et les operateurs d'un symbole ou d'un type IHM
// -----------------------------------------------------------------------------
//  LES SCRIPTS D'UNE VUE (OnOpen, OnCycle, OnClose) - d'une vue, d'une popup,
//  d'un symbole, d'un ecran modele, d'un modele d'en-tete ou de pied de page -
//  ET LES OPERATEURS (le script de chacun) PARTENT DANS UN FICHIER TEXTE (.xpgst),
//  lisible et modifiable dans n'importe quel editeur : du ST, avec un bloc de
//  commentaire avant chaque script, qui dit ce qu'il est.
//
//      (* XPGAnalyser 1.11.3 - scripts de la vue Popup_vanne (popup) ... *)
//      (*# xpgst format=1 genre=scripts-vue source="Popup_vanne" role=popup *)
//      (*# script evenement=OnOpen langage=ST *)
//      Titre := 'Vanne';
//      (*# script evenement=OnCycle langage=ST *)
//      ...
//      (*# fin *)
//
//  Un operateur :
//      (*# operateur op="+" gauche=T_VECTEUR droite=REAL resultat=T_VECTEUR description="..." *)
//
//  IMPORTER, C'EST COMPARER AVANT D'AGIR : chaque script du fichier est neuf
//  (l'evenement n'a pas de script dans la vue), identique, ou different - on
//  remplace celui de la vue, ou on ajoute le sien a la suite. Un operateur :
//  neuf (pas de meme signature op/gauche/droite), identique ou different
//  (remplace). Tout se fait en UNE modification (un seul Ctrl+Z).
//
//  UN FICHIER .st OU .txt SANS BLOC (*# ... *) EST UN SEUL SCRIPT : il va dans
//  l'evenement choisi.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi::scriptfile {

inline constexpr std::string_view kExtension = ".xpgst";
// 2 : 1.11.18 (refonte des scripts, lot 3) - les declarations du modele d'un script ou d'un
// operateur, un bloc (*# declaration ... *) chacune, avant son code (decision D4) ; un
// fichier sans declaration s'ecrit encore au format 1 (la 1.11.17 le lit).
inline constexpr int              kFormat = 2;

enum class Genre : std::uint8_t { ViewScripts, Operators };

struct Entry {
    // un script de vue
    std::string event;                 // OnOpen, OnCycle, OnClose
    ScriptLang  lang{ScriptLang::ST};
    // un operateur
    HmiOperator op{};
    std::string body;
    std::vector<Declaration> decls{};  // 1.11.18 (lot 3) : ses declarations du modele (sans identifiant a la lecture)
};

struct File {
    Genre              genre{Genre::ViewScripts};
    int                format{kFormat};
    std::string        writer;          // la version qui l'a ecrit
    std::string        source;          // la vue, le symbole ou le type d'origine
    std::string        role;            // vue, popup, symbole, modele, entete, pied ; un type : "type"
    bool               plain{false};    // un fichier sans bloc : un seul script, sans evenement
    std::vector<Entry> entries;
};

// ---- ecrire ---------------------------------------------------------------------------
// Les scripts non vides de la vue (dans l'ordre OnOpen, OnCycle, OnClose), ou seulement
// celui de `onlyEvent` s'il est donne.
[[nodiscard]] File fromView(const View&, std::string_view onlyEvent = {});
// Les operateurs d'un symbole (ou d'un type IHM : `owner`, role "type").
[[nodiscard]] File fromOperators(const std::vector<HmiOperator>&, std::string_view owner, std::string_view role);
[[nodiscard]] std::string write(const File&);

// ---- lire -----------------------------------------------------------------------------
// Faux (et `why`) : un format plus recent, un bloc illisible, un evenement inconnu.
bool read(std::string_view text, File& out, std::string* why = nullptr);

// Le fichier sur le disque (chemin UTF-8). Faux (et `why`) : illisible, ou pas un fichier de scripts.
bool save(const File&, const std::string& path, std::string* why = nullptr);
bool load(const std::string& path, File& out, std::string* why = nullptr);

// ---- importer -------------------------------------------------------------------------
enum class State : std::uint8_t { New, Same, Different };
[[nodiscard]] std::string_view stateLabel(State) noexcept;   // "nouveau", "identique", "diff\xC3\xA9rent"
// L'etat de chaque entree du fichier face a la vue (ses scripts) ou aux operateurs.
[[nodiscard]] std::vector<State> compareView(const File&, const View&);
[[nodiscard]] std::vector<State> compareOperators(const File&, const std::vector<HmiOperator>&);
// La ligne d'une entree pour la fenetre d'import : "OnCycle - 12 lignes - remplace l'actuel (8 lignes)".
[[nodiscard]] std::string describe(const File&, std::size_t index, State, const View* view = nullptr);

enum class Mode : std::uint8_t { Replace, Append };
// Les entrees choisies (`chosen`, une par entree ; vide : toutes) dans la vue : un script
// neuf est cree (son identifiant pris dans `p`) ; un different est remplace, ou recoit le
// code du fichier a la suite (Append) ; un identique ne bouge pas. Un fichier sans bloc va
// dans `plainEvent`. Rend le nombre de scripts changes.
std::size_t applyToView(Project& p, View& v, const File&, const std::vector<bool>& chosen, Mode,
                        std::string_view plainEvent = "OnOpen");
// Les operateurs choisis : un neuf est ajoute, un different remplace (meme signature).
std::size_t applyToOperators(Project& p, std::vector<HmiOperator>& ops, const File&, const std::vector<bool>& chosen);

} // namespace hmi::scriptfile
