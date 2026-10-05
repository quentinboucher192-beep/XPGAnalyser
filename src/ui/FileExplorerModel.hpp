// =============================================================================
//  ui/FileExplorerModel.hpp - lot API 8 : l'explorateur de fichiers, sans ecran
// -----------------------------------------------------------------------------
//  TOUT CE QUE L'EXPLORATEUR DE L'APPLI (ui/widgets/FileExplorer) SAIT FAIRE
//  SANS DESSINER, pour que tests/fileexplorer_test.cpp le verifie :
//    - lire un dossier en UNE lecture (le genre de chaque fichier d'apres son
//      extension : ui/FileKinds) ; les fichiers caches du systeme restent
//      caches ; les erreurs dites en francais (acces refuse, dossier disparu,
//      lecteur retire) ;
//    - trier (les dossiers d'abord ; les noms "naturellement" : Vue2 < Vue10),
//      filtrer par les motifs de la demande (et compter ce que le filtre
//      cache), chercher (ui::SearchQuery : ni casse ni accents) ;
//    - le fil d'Ariane d'un chemin, Windows (D:\, \\serveur\partage) et Linux,
//      calcule sur le TEXTE (le meme resultat sur les deux systemes) ;
//    - le nom a enregistrer : l'extension ajoutee, les noms interdits de
//      Windows, "existe deja" ;
//    - la memoire : les dossiers recents par genre de demande, les epingles, la
//      vue et le tri - en lignes de texte (Settings::setList les garde) ;
//    - les emplacements (Bureau, Documents, Telechargements, les lecteurs).
// =============================================================================
#pragma once

#include "FileKinds.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui::files {

    // Ce que demande l'appelant (ui::FilePick) : ouvrir, enregistrer, un dossier.
    enum class PickMode : std::uint8_t { Open, Save, Folder };

    // ------------------------------------------------------------- un element ---
    struct Entry {
        std::string    name;             // UTF-8
        std::string    path;             // UTF-8, complet
        std::string    ext;              // minuscules, sans point
        Family         family{Family::Other};
        bool           folder{false};
        std::uintmax_t size{0};          // octets (0 pour un dossier)
        std::int64_t   modified{0};      // secondes depuis 1970 (UTC) ; 0 : inconnu
    };

    enum class ListError : std::uint8_t { None, NotFound, AccessDenied, NotAFolder, DriveGone, Other };

    struct Listing {
        std::string        folder;
        std::vector<Entry> entries;      // les caches du systeme en sont retires
        std::size_t        systemHidden{0};
        ListError          error{ListError::None};
        std::string        message;      // l'erreur, en francais ("" : aucune)
    };

    // Le dossier en une lecture ; jamais d'exception.
    [[nodiscard]] Listing     listFolder(const std::string& folder);
    [[nodiscard]] std::string errorMessage(ListError e, std::string_view folder);
    // Un nom cache du systeme ("." en tete sous Linux ; l'attribut cache ou
    // systeme sous Windows - vu par listFolder).
    [[nodiscard]] bool        dotHidden(std::string_view name) noexcept;

    // -------------------------------------------------------------------- tri ---
    enum class SortKey : std::uint8_t { Name, Modified, Type, Size };
    // Les noms compares comme on les lit : sans casse ni accents, les nombres
    // par leur valeur ("Vue2" < "Vue10"). <0, 0, >0.
    [[nodiscard]] int compareNames(std::string_view a, std::string_view b);
    // Les dossiers d'abord, toujours ; puis la cle ; a egalite, le nom.
    void sortEntries(std::vector<Entry>& entries, SortKey key, bool ascending);
    [[nodiscard]] std::string sortKeyName(SortKey k);          // "nom", "modifie", "type", "taille"
    [[nodiscard]] bool        sortKeyFrom(std::string_view s, SortKey& out);

    // ----------------------------------------------------------------- filtre ---
    struct FilterGroup {
        std::string              name;         // "Exports Control Expert"
        std::vector<std::string> extensions;   // minuscules, sans point ; vide : tout
        [[nodiscard]] bool all() const noexcept { return extensions.empty(); }
        // "Exports Control Expert (*.xpg; *.xhw)" ; "Tous les fichiers (*.*)".
        [[nodiscard]] std::string label() const;
        // Un dossier passe toujours ; un fichier, si son extension est la.
        [[nodiscard]] bool accepts(const Entry& e) const;
    };
    // Les filtres d'une demande (ui::FilePick::filters : "nom", "xpg;xhw" ou
    // "*") ; "Tous les fichiers" toujours a la fin, une seule fois.
    [[nodiscard]] std::vector<FilterGroup> filterGroups(const std::vector<std::pair<std::string, std::string>>& pickFilters);
    // Le filtre dont le nom commence par `prefix` (sans casse ni accents) ; -1.
    [[nodiscard]] int findFilter(const std::vector<FilterGroup>& groups, std::string_view prefix);

    // Ce que montre la liste : le filtre, la recherche, le tri.
    struct Shown {
        std::vector<Entry> rows;
        std::size_t        hiddenByFilter{0};   // "7 autres fichiers caches par le filtre"
    };
    [[nodiscard]] Shown shownEntries(const std::vector<Entry>& all, const FilterGroup* filter, std::string_view search,
                                     SortKey key, bool ascending, bool showFiltered = false);
    // La ligne de la liste qui commence par ce qu'on tape (sans casse ni
    // accents), a partir de `from` (compris), en revenant au debut ; -1.
    [[nodiscard]] int typeAhead(const std::vector<Entry>& rows, std::string_view typed, int from);

    // ---------------------------------------------------------- fil d'Ariane ---
    struct Crumb {
        std::string label;
        std::string path;        // "" : Ce PC (les lecteurs, les emplacements)
    };
    // "D:\Affaires\Gaz" -> Ce PC | D: | Affaires | Gaz ;
    // "\\srv-usine\automatisme\x" -> Ce PC | \\srv-usine\automatisme | x ;
    // "/home/q/x" -> Ce PC | / | home | q | x. Le separateur du chemin est garde.
    [[nodiscard]] std::vector<Crumb> breadcrumb(std::string_view path);
    // Le dossier parent ; "" a la racine (on remonte a Ce PC).
    [[nodiscard]] std::string parentOf(std::string_view path);
    // dossier + nom, avec le separateur du dossier.
    [[nodiscard]] std::string joinPath(std::string_view folder, std::string_view name);
    // Le meme chemin ? Sans casse sous Windows ; / et \ ; sans separateur final.
    [[nodiscard]] bool samePath(std::string_view a, std::string_view b);
    // Un chemin tape : les sous-dossiers de son parent qui commencent par le
    // dernier morceau (sans casse), complets ; `max` au plus.
    [[nodiscard]] std::vector<std::string> completeFolder(std::string_view typed, std::size_t max = 8);

    // ---------------------------------------------------- le nom a enregistrer ---
    // "" : le nom est bon ; sinon l'erreur, en francais (caractere interdit,
    // nom reserve de Windows, fin par un point ou un espace, trop long, vide).
    [[nodiscard]] std::string nameError(std::string_view name);
    // Le nom, avec l'extension du filtre s'il n'en a pas (ou `fallbackExt`).
    [[nodiscard]] std::string withExtension(std::string name, const FilterGroup* filter, std::string_view fallbackExt = {});
    struct SaveCheck {
        std::string name;        // le nom final (l'extension ajoutee)
        std::string path;        // dossier + nom
        std::string error;       // non vide : le bouton est grise
        bool        exists{false};
        std::string warning;     // "rapport.csv existe deja (modifie hier 17:42) : il sera remplace"
    };
    [[nodiscard]] SaveCheck checkSaveName(std::string_view folder, std::string_view typed, const FilterGroup* filter,
                                          std::int64_t now, std::string_view fallbackExt = {});

    // ------------------------------------------------------------ les libelles ---
    [[nodiscard]] std::int64_t nowSeconds();
    // "\xC3\xA0 l'instant", "il y a 3 min", "aujourd'hui 09:12", "hier 17:42", "12/09/2026".
    [[nodiscard]] std::string relativeTime(std::int64_t t, std::int64_t now);
    // "0 octet", "532 octets", "12 Ko", "3,4 Mo", "1,2 Go".
    [[nodiscard]] std::string sizeLabel(std::uintmax_t bytes);

    // --------------------------------------------------------------- memoire ---
    //  Par GENRE de demande (requestKind : "ouvrir:xpg", "enregistrer:csv",
    //  "dossier") : les 5 derniers dossiers choisis (et le dernier fichier), la
    //  vue, le tri, les largeurs des colonnes. Les epingles valent pour tous.
    class Memory {
    public:
        static constexpr std::size_t kRecents = 5;
        struct Recent {
            std::string folder;
            std::string file;          // le dernier fichier choisi dans ce dossier ("" : aucun)
        };
        struct ViewState {
            bool               thumbnails{false};
            SortKey            sort{SortKey::Name};
            bool               ascending{true};
            std::vector<float> widths;   // les colonnes Nom, Modifie, Type, Taille
        };

        // Un choix : un fichier (son dossier en tete des recents) ou un dossier.
        void remember(const std::string& kind, const std::string& chosen, bool isFolder);
        // Un dossier ouvert en tapant son chemin (un partage reseau) : dans les recents.
        void rememberFolder(const std::string& kind, const std::string& folder);
        [[nodiscard]] std::vector<Recent> recents(const std::string& kind) const;
        [[nodiscard]] std::string lastFolder(const std::string& kind) const;   // "" : jamais
        [[nodiscard]] bool isRecentFile(const std::string& path) const;       // la marque "recent"

        bool pin(const std::string& folder);       // faux : deja la
        bool unpin(const std::string& folder);     // faux : pas la
        [[nodiscard]] bool pinned(const std::string& folder) const;
        [[nodiscard]] const std::vector<std::string>& pins() const noexcept { return pins_; }

        [[nodiscard]] ViewState view(const std::string& kind) const;
        void setView(const std::string& kind, ViewState v);

        // En lignes de texte ("recent|kind|dossier|fichier", "epingle|dossier",
        // "vue|kind|details|nom|1|220,120,180,80") ; load ignore ce qu'il ne lit pas.
        [[nodiscard]] std::vector<std::string> save() const;
        void load(const std::vector<std::string>& lines);
        [[nodiscard]] bool empty() const noexcept { return recents_.empty() && pins_.empty() && views_.empty(); }

    private:
        std::map<std::string, std::vector<Recent>> recents_;
        std::vector<std::string>                   pins_;
        std::map<std::string, ViewState>           views_;
    };
    // Le genre d'une demande : "dossier", "ouvrir:xpg", "enregistrer:csv"
    // (l'extension du premier filtre ; "*" : tous les fichiers).
    [[nodiscard]] std::string requestKind(PickMode mode, const std::vector<FilterGroup>& groups);

    // ----------------------------------------------------------- emplacements ---
    struct Place {
        enum class Group : std::uint8_t { Project, Recent, Pinned, Computer };
        Group         group{Group::Computer};
        std::string   label;          // "Documents", "D:", "exports"
        std::string   path;
        std::string   detail;         // le nom du volume ("Cl\xC3\xA9 USB"), le dernier fichier...
        Family        family{Family::Folder};
        std::uint64_t total{0};       // octets (0 : inconnu) - la barre de place libre
        std::uint64_t free{0};
        bool          removable{false};
        bool          network{false};
    };
    // CE PC : Bureau, Documents, Telechargements, puis les lecteurs (Windows :
    // GetLogicalDrives...) ; ailleurs le dossier personnel, les dossiers XDG,
    // /media et /mnt. Seulement ce qui existe.
    [[nodiscard]] std::vector<Place> computerPlaces();
    // CE PROJET : son dossier (son nom), exports, simulation, ressources (ceux
    // qui existent), et le dossier du .XPG d'origine s'il est ailleurs.
    [[nodiscard]] std::vector<Place> projectPlaces(const std::string& projectDir, const std::string& sourceXpg);
    // La place libre d'un dossier (son lecteur) ; faux : inconnue.
    bool spaceOf(const std::string& folder, std::uint64_t& total, std::uint64_t& free);

} // namespace ui::files
