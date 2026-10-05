// =============================================================================
//  hmi/HmiVersions.hpp - les versions du projet (lot 21)
// -----------------------------------------------------------------------------
//  UNE VERSION EST UNE PHOTO DU PROJET : l'API (le manifeste, les sections, les
//  unites, les DFB, les DDT, les variables, la configuration, les tables),
//  l'IHM (ihm/ : les vues, les scripts, les ressources... sauf l'historique de
//  marche et la corbeille) et les donnees (donnees/). Pas les exports.
//
//  SUR LE DISQUE, DANS LE DOSSIER DU PROJET :
//    versions/index.txt          les versions (nom, etat, date, auteur...) et le reglage
//    versions/V0005.txt          les fichiers de la version 5 : empreinte, taille, chemin
//    versions/objets/3f/9a...    le contenu, UNE FOIS par empreinte (SHA-256)
//  Un fichier inchange d'une version a l'autre n'est garde qu'une fois : dix
//  versions d'un projet de 5 Mo pesent quelques Mo, pas 50.
//
//  COMPARER dit ce qui change PAR ELEMENT, pas par fichier : une section, une
//  unite, les variables globales ; une vue (ses objets), un script, les
//  variables IHM, une alarme, une ressource... Un enregistrement qui ne change
//  que les dates (le manifeste, ihm.txt) ne compte pas comme un changement.
//
//  RESTAURER une version cree d'abord la version "Avant restauration" : rien
//  ne se perd. L'ecran relit ensuite le projet.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::ver {

// ---- une version ------------------------------------------------------------
enum class State : int { Draft = 0, Validated, Delivered, Auto, BeforeRestore };
[[nodiscard]] std::string_view stateLabel(State) noexcept;     // "Brouillon", "Validee", "Livree", "auto", "avant restauration"
[[nodiscard]] std::string_view stateKey(State) noexcept;       // "brouillon", "validee", "livree", "auto", "avant_restauration"
[[nodiscard]] State            stateFromKey(std::string_view) noexcept;

// La version automatique : jamais, a chaque enregistrement, au plus une par heure.
enum class AutoMode : int { Never = 0, EverySave, Hourly };
[[nodiscard]] std::string_view autoLabel(AutoMode) noexcept;
[[nodiscard]] std::string_view autoKey(AutoMode) noexcept;
[[nodiscard]] AutoMode         autoFromKey(std::string_view) noexcept;

struct FileEntry {
    std::string   path;       // relatif au dossier du projet, avec des '/'
    std::uint64_t size{0};
    std::string   hash;       // SHA-256, en hexadecimal
    bool operator==(const FileEntry&) const = default;
};

struct Version {
    int           number{0};
    std::string   name, comment, author, date;   // date : "2026-09-26 18:40"
    State         state{State::Draft};
    int           base{0};              // la version qui la precedait (0 : aucune)
    std::size_t   fileCount{0};
    std::uint64_t bytes{0};             // la taille de ses fichiers
    std::uint64_t newBytes{0};          // ce qu'elle a ajoute a versions/objets
    std::string   fingerprint;          // l'empreinte de son manifeste (12 caracteres)
    std::string   since;                // ce qui a change depuis la precedente, en bref
    [[nodiscard]] std::string label() const;   // "V5 . Livree au client"
};

struct Store {
    std::string          folder;        // le dossier du projet
    std::vector<Version> versions;      // de la plus ancienne a la plus recente
    AutoMode             autoMode{AutoMode::Never};
    int                  keepAuto{10};  // les versions automatiques gardees (les plus recentes)

    [[nodiscard]] const Version* find(int number) const noexcept;
    [[nodiscard]] Version*       find(int number) noexcept;
    [[nodiscard]] const Version* last() const noexcept;
    [[nodiscard]] int            nextNumber() const noexcept;
    [[nodiscard]] std::uint64_t  storedBytes() const;   // ce que pese versions/ sur le disque
    [[nodiscard]] std::uint64_t  copyBytes() const noexcept;   // ce que peseraient des copies
};

// ---- ce qui entre dans une version -----------------------------------------
[[nodiscard]] bool                   included(std::string_view relativePath);
// Le projet enregistre sur le disque : ses fichiers, tries, avec leur empreinte.
[[nodiscard]] std::vector<FileEntry> scan(const std::string& projectFolder);

// ---- le magasin -------------------------------------------------------------
[[nodiscard]] core::Result<Store> open(const std::string& projectFolder);   // vide si versions/ n'existe pas
[[nodiscard]] core::Status        saveIndex(const Store&);
[[nodiscard]] core::Result<std::vector<FileEntry>> filesOf(const Store&, int number);   // 0 : le disque
[[nodiscard]] core::Result<std::string> contentOf(const Store&, int number, const std::string& path);

// `now` vide : l'heure du poste.
[[nodiscard]] core::Result<Version> create(Store&, const std::string& name, State, const std::string& comment,
                                           const std::string& author, const std::string& now = {});
[[nodiscard]] core::Status update(Store&, int number, const std::string& name, State, const std::string& comment);
// Retire la version, puis les objets que plus aucune version ne cite.
[[nodiscard]] core::Status remove(Store&, int number);
// Le disque differe-t-il de la derniere version (hors dates d'enregistrement) ?
[[nodiscard]] bool         changedSinceLast(const Store&);
// Remet le projet (sur le disque) dans l'etat de la version ; rend la version
// "Avant restauration" creee d'abord (rien si le disque etait deja une version).
[[nodiscard]] core::Result<std::optional<Version>> restore(Store&, int number, const std::string& author,
                                                           const std::string& now = {});
// Un dossier de projet complet (a ouvrir a cote), ou une archive .zip.
[[nodiscard]] core::Status extract(const Store&, int number, const std::string& toFolder);
[[nodiscard]] core::Status exportZip(const Store&, int number, const std::string& zipPath);
// Apres un enregistrement : la version automatique, selon le reglage (rien si
// rien n'a change, ou si la derniere a moins d'une heure en mode Hourly).
[[nodiscard]] core::Result<std::optional<Version>> afterSave(Store&, const std::string& author, const std::string& now = {});

// ---- comparer ---------------------------------------------------------------
enum class Side : int { Api = 0, Ihm, Data };
enum class Change : int { Added = 0, Removed, Modified };
struct Element {
    Side        side{Side::Api};
    std::string category;    // "Sections", "Variables globales", "Vues", "Variables IHM", "Alarmes"...
    std::string name;        // "Matrice (Logigrammes_A)", "Vue_Equipements"
    std::string where;       // "API > Unites > Logigrammes_A > Matrice" (avec des ?)
    std::string detail;      // "+8 -4 lignes", "3 objets modifies, 1 ajoute", "Seuil_Bas, Seuil_Haut"
    Change      change{Change::Modified};
    std::string kind;        // "texte", "vue", "liste", "fichier"
    std::string key;         // de quoi le retrouver : "fichier:sections/Matrice.st", "vue:12", "script:3"...
    std::string fileA, fileB;   // un texte : son chemin de chaque cote (vide : absent)
    int         added{0}, removed{0};   // des lignes (un texte), des articles (une liste), des objets (une vue)
    int         modified{0};            // une liste : les articles modifies
};
struct Comparison {
    int                  a{0}, b{0};    // 0 : le travail en cours
    std::vector<Element> elements;
    [[nodiscard]] std::size_t count(Side) const noexcept;
    // "+2 vues . ? 14 variables . ? Matrice" : ce qu'affiche la colonne "Depuis la precedente".
    [[nodiscard]] std::string summary(std::size_t maxParts = 3) const;
};
// a, b : des numeros de version ; 0 : le travail en cours (enregistre).
[[nodiscard]] core::Result<Comparison> compare(const Store&, int a, int b);
// L'IHM d'une version (0 : le disque), relue.
[[nodiscard]] core::Result<Project> hmiOf(const Store&, int number);

// La difference ligne a ligne (plus longue sous-suite commune) : chaque ligne
// d'un cote, de l'autre, ou des deux ; une ligne retiree suivie d'une ajoutee
// au meme endroit est "modifiee".
struct DiffLine {
    enum Kind : int { Same = 0, Removed, Added, Changed };
    Kind kind{Same};
    int  left{-1}, right{-1};    // le numero de ligne (0...) de chaque cote ; -1 : absente
};
[[nodiscard]] std::vector<std::string> splitLines(std::string_view text);
[[nodiscard]] std::vector<DiffLine>    diffLines(const std::vector<std::string>& a, const std::vector<std::string>& b);

// Une vue d'une version a l'autre : les objets ajoutes, retires, modifies, et quoi.
struct ObjectChange {
    Change                   change{Change::Modified};
    Id                       id{kNoId};
    std::string              name;
    std::vector<std::string> props;    // "Couleur de fond #2D3440 -> #4A1F22"
};
[[nodiscard]] std::vector<ObjectChange> compareViews(const View& a, const View& b);

// L'auteur d'une version : l'utilisateur du poste et le nom du poste,
// "Quentin (PC-ATELIER)".
[[nodiscard]] std::string defaultAuthor();

// "412 Ko", "5,3 Mo" : une taille lisible.
[[nodiscard]] std::string sizeText(std::uint64_t bytes);

} // namespace hmi::ver
