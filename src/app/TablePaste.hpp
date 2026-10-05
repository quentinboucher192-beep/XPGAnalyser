// =============================================================================
//  app/TablePaste.hpp - coller un tableau d'Excel dans un tableau du projet (lot 20)
// -----------------------------------------------------------------------------
//  CE QU'EXCEL MET DANS LE PRESSE-PAPIERS : du texte, une ligne par ligne, les
//  cases separees par des tabulations ; une case qui contient une tabulation,
//  un retour ou un guillemet est entre guillemets ("" pour un guillemet).
//  parseGrid le relit ; ui::TableView::copyText l'ecrit (Ctrl+C).
//
//  LES COLONNES SONT RECONNUES PAR LEUR TITRE. Si la premiere ligne collee
//  porte des titres de la table (ou des noms voisins : Name, Libelle...),
//  chaque colonne va dans la sienne, dans n'importe quel ordre ; une colonne
//  inconnue est ignoree, et le bandeau le dit. Sans titres, les cases se
//  collent a partir de la case choisie, comme dans Excel : la colonne du
//  dernier clic, puis celles a sa droite.
//
//  LA COLONNE CLE (le nom) DIT QUELLE LIGNE. Un nom qui existe : la ligne est
//  mise a jour ; un nom nouveau : elle est creee. "Coller en nouvelles
//  lignes" : toujours creee (Nom_2 si le nom est pris). Sans la colonne cle
//  (une colonne de types collee sur des lignes choisies), les lignes collees
//  vont dans celles de la table a partir de la ligne choisie, dans l'ordre ou
//  la table les montre.
//
//  UNE CASE REFUSEE N'ARRETE PAS LE RESTE : sa raison est notee (ligne,
//  colonne), la case est marquee dans la table, les autres sont collees. UNE
//  CASE VIDE NE CHANGE RIEN (pour delier une variable, on ecrit "(aucun)").
//
//  UN SEUL CTRL+Z. Le volet ouvre un groupe (core::CommandGroupScope) le temps
//  du collage : les actions ordinaires (et leurs controles) y empilent leurs
//  commandes, et l'historique n'en montre qu'une ligne.
// =============================================================================
#pragma once

#include "../ui/widgets/DataViews.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace app::paste {

using Grid = std::vector<std::vector<std::string>>;

// Le texte d'un tableur en cases (les lignes entierement vides sont retirees).
[[nodiscard]] Grid parseGrid(std::string_view text);
// "Equipement" (accentue), "EQUIPEMENT", "equipement " -> "equipement" : de quoi comparer
// deux titres (casse, accents, espaces, _ - . et parentheses ignores).
[[nodiscard]] std::string normalizedTitle(std::string_view title);

// Une colonne que la cible connait.
struct Column {
    std::string              title;              // comme dans la table ("Nom", "Type"...)
    std::vector<std::string> aliases;            // d'autres titres acceptes ("Name", "Variable"...)
    int                      tableColumn{-1};    // sa colonne dans la table ; -1 : reconnue par son titre seulement
    bool                     key{false};         // celle qui dit quelle ligne
    // Ecrire la case de la ligne `key` (qui existe) ; faux et `why` : refusee.
    // Nulle : une colonne calculee (Place, Qualite), jamais ecrite.
    std::function<bool(const std::string& key, const std::string& value, std::string* why)> set;
};

// Une colonne en une ligne (les cibles en font une dizaine).
[[nodiscard]] Column column(std::string title, std::vector<std::string> aliases, int tableColumn,
                            std::function<bool(const std::string&, const std::string&, std::string*)> set, bool key = false);

// Une case refusee pendant la creation d'une ligne : (titre de la colonne, raison).
using Notes = std::vector<std::pair<std::string, std::string>>;

struct Target {
    std::string noun{"ligne"}, nouns{"lignes"};  // "variable", "variables"
    bool        feminine{true};                  // creee(s) / cree(s)
    std::vector<Column> columns;                 // les colonnes de la table dans leur ordre, puis les autres
    std::function<bool(const std::string& key)> exists;
    std::string unknown{"introuvable"};          // la raison d'un nom inconnu, quand la cible ne cree pas
    // Creer la ligne `key` ; `cells` : les cases de la ligne collee, par titre
    // normalise. Rend le nom retenu (vide : refusee, `why`). `notes` : les cases
    // refusees en chemin (la ligne est creee quand meme) ; `used` : les titres
    // (normalises) que la creation a deja pris en compte.
    std::function<std::string(const std::string& key, const std::map<std::string, std::string>& cells,
                              Notes& notes, std::vector<std::string>& used, std::string* why)> create;
    // Coller en nouvelles lignes : un nom libre a partir de `key`.
    std::function<std::string(const std::string& key)> freeKey;
    // Les cles des lignes de la table dans l'ordre de la vue, a partir de la
    // ligne choisie (un collage sans la colonne cle va dans celles-ci).
    std::vector<std::string> keysFromAnchor;
};

struct Refusal {
    std::size_t line{0};         // la ligne du collage (1 : la premiere, titres compris)
    std::string key, column, value, why;
    int         tableColumn{-1};
};

struct Report {
    std::size_t lines{0}, created{0}, updated{0}, skipped{0};
    bool        titles{false};                   // la premiere ligne etait des titres
    std::vector<std::string> recognized, ignored, computed;   // titres : reconnus, inconnus, calcules
    std::string              startColumn;        // sans titres : la colonne de depart
    std::vector<Refusal>     refused;
    std::vector<std::string> createdKeys, updatedKeys;
    std::string              error;              // rien n'a ete colle, et pourquoi
    // "9 variables creees, 3 mises a jour, 1 case refusee"
    [[nodiscard]] std::string counts(const Target&) const;
    // Le bandeau au-dessus de la table.
    [[nodiscard]] std::string banner(const Target&) const;
    // La barre d'etat : "Colle : 9 variable(s) creee(s)... - Ctrl+Z pour tout annuler".
    [[nodiscard]] std::string status(const Target&) const;
    // Les marques de la table (lignes creees, mises a jour, cases refusees).
    [[nodiscard]] std::vector<ui::TableView::Mark> marks() const;
};

// Les cles des lignes montrees (le texte de la colonne `keyColumn`), dans
// l'ordre de la vue, a partir de la ligne de VUE `anchorViewRow`.
[[nodiscard]] std::vector<std::string> keysFrom(const ui::TableView&, std::size_t anchorViewRow, std::size_t keyColumn);

// Colle `text` dans la cible. `anchorColumn` : la colonne de la table d'ou
// coller sans titres (-1 : la premiere). Les actions de la cible font le
// travail ; l'appelant ouvre le groupe (un seul Ctrl+Z).
[[nodiscard]] Report run(const Target&, std::string_view text, int anchorColumn, bool asNewRows);

// ---- relier une table ------------------------------------------------------
//  Le geste complet : la cible (faite a la demande, avec les cles vues depuis
//  la ligne choisie), un groupe dans la pile de l'application, le collage, le
//  rafraichissement du volet, les marques et le bandeau ; puis `done` (le
//  message du volet). Le bandeau s'efface a la modification suivante (le
//  volet appelle forget()).
struct Binding {
    ui::TableView*                                                   table{nullptr};
    std::function<Target(const ui::TableView::PasteRequest&)>        target;
    std::function<void()>                                            refresh;
    std::function<void(const Report&, const Target&)>                done;
    int                                                              keyColumn{0};
    bool                                                             pasting{false};
};
// Le collage d'une table ; `binding` doit vivre aussi longtemps que la table.
void bind(Binding& binding);
// Une modification hors collage : le bandeau et les marques n'ont plus lieu d'etre.
void forget(Binding& binding);

// Un seul endroit sait montrer un message dans la barre d'etat de l'ecran :
// l'ecran l'enregistre ici (vide : rien).
void setNotifier(std::function<void(const std::string&)> notify);
void notify(const std::string& text);

} // namespace app::paste
