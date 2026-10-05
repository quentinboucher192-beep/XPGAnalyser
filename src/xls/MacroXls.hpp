// =============================================================================
//  xls/MacroXls.hpp - le pont entre le classeur et le runtime des macros
// -----------------------------------------------------------------------------
//  `fonction inconnue : OpenWorkbook` ne parle pas du fichier : la macro est
//  refusee avant d'avoir regarde le chemin, parce que le nom n'est dans aucune
//  table. Ce fichier fournit ce qu'il y a derriere ce nom ; il reste a l'ajouter
//  a la table, et c'est une dizaine de lignes chez toi (voir
//  BRANCHER-OPENWORKBOOK.md).
//
//  POURQUOI UN OBJET ET PAS QUATRE FONCTIONS LIBRES
//
//  `OpenSheet` doit rendre un identifiant utilisable par `RowCount`, `Cell` et
//  `HasColumn` - celles que tu as deja, et qui servent aux CSV. Plutot que de
//  toucher ton registre de tables, ce pont distribue ses identifiants dans une
//  plage qui lui est propre, a partir de `kFirstHandle`. Tes quatre fonctions
//  existantes gagnent alors une ligne chacune :
//
//      if (xls::MacroXls::isSheetHandle(t)) return xls::MacroXls::instance()....
//
//  et le chemin CSV n'est pas touche. C'est ce qui rend l'ajout petit, et
//  surtout reversible.
// =============================================================================
#pragma once

#include "XlsmSource.hpp"

#include <string>
#include <vector>

namespace xls {

class MacroXls {
public:
    // Une seule instance : le classeur ouvert appartient a l'application, pas a
    // une macro. Deux macros enchainees par `ImporterClasseur` lisent le meme.
    static MacroXls& instance();

    // Le chemin que le bouton "Classeur..." a charge. `OpenWorkbook('')` ouvre
    // celui-la, ce qui est le cas normal : l'utilisateur a deja designe son
    // fichier, la macro ne le redemande pas.
    void setLoadedWorkbook(std::string path);
    [[nodiscard]] const std::string& loadedWorkbook() const noexcept { return loaded_; }

    // Rend 0 ou plus en cas de succes, -1 sinon. JAMAIS une valeur positive sur
    // un echec : les macros testent `< 0` et rien d'autre.
    // `error` recoit de quoi ecrire un message utile ; il reste vide en cas de
    // succes.
    int openWorkbook(const std::string& path, std::string& error);

    // Rend un identifiant du meme genre que `OpenTable`, ou -1. Le nom est
    // compare EXACTEMENT, espaces compris : `Cartes API` a une espace.
    int openSheet(const std::string& name);

    [[nodiscard]] static bool isSheetHandle(int handle) noexcept { return handle >= kFirstHandle; }

    [[nodiscard]] int         rowCount(int handle) const;
    [[nodiscard]] std::string cell(int handle, int row, const std::string& column) const;
    [[nodiscard]] bool        hasColumn(int handle, const std::string& column) const;

    // Une ligne d'un onglet n'est jamais a sauter : la recherche d'en-tete a
    // deja ecarte le bandeau, la ligne d'exemples et ce qui traine sous le
    // tableau. La fonction existe pour que `SkipRow` reponde la meme chose,
    // quelle que soit l'origine de la table.
    [[nodiscard]] bool skipRow(int handle, int row) const;

    // L'onglet `Config` est un formulaire, pas un tableau. Cle absente = chaine
    // vide : toutes les macros ont un repli.
    [[nodiscard]] std::string setting(const std::string& key) const;

    // Pour un message d'erreur qui aide : "onglet introuvable ; disponibles : ..."
    [[nodiscard]] std::vector<std::string> sheetNames() const;
    [[nodiscard]] bool                     isOpen() const noexcept { return open_; }

    // A appeler quand une macro se termine, pour ne pas garder un megaoctet de
    // classeur en memoire ni un fichier verrouille par megarde. Ne PAS appeler
    // entre deux macros enchainees par `ImporterClasseur`.
    void close();

    static constexpr int kFirstHandle = 1000;

private:
    MacroXls() = default;

    [[nodiscard]] const Sheet* resolve(int handle) const;

    Workbook                  wb_;
    bool                      open_{false};
    std::string               loaded_;
    std::string               openedFrom_;
    std::string               openedStamp_;     // taille@date du fichier lu (lot macros 1)
    std::vector<const Sheet*> handles_;
};

} // namespace xls
