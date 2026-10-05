// =============================================================================
//  ui/widgets/FilterMemory.hpp - lot API 8 : les filtres retenus d'une seance
//  a l'autre
// -----------------------------------------------------------------------------
//  CE QUI EST RETENU : les filtres des colonnes de chaque tableau, la recherche
//  tapee et la pastille choisie de son volet, les champs de recherche de l'IHM -
//  par widget (son id, stable) et par projet. ui/ ne connait ni les reglages ni
//  le projet : App branche le crochet (Store : lire, ecrire une cle) et range
//  sous "filtres.<projet>.<id>" dans ses reglages.
//
//    TableView     ses filtres de colonnes, chacun avec le titre de sa colonne :
//                  relus quand l'entonnoir s'allume et que les colonnes sont la
//                  (avant le premier dessin : rien ne clignote), ecrits a chaque
//                  changement ; "Tout effacer" les efface et les oublie. Un
//                  filtre dont la colonne n'existe plus (renommee) est ignore,
//                  sans message.
//    SearchMemory  la recherche et la pastille d'un volet (app::ApiFilterBar,
//                  ui::SearchField, un champ de recherche) : relues une fois le
//                  volet branche, ecrites a chaque changement.
//
//  UN AUTRE PROJET S'OUVRE : App appelle contextChanged(true) - chaque widget
//  inscrit relit ce que CE projet retient (rien : ses filtres s'effacent, sans
//  rien ecrire). Le meme projet enregistre ailleurs : contextChanged(false),
//  chacun reecrit ce qu'il montre, sous la nouvelle cle.
//
//  LE TEXTE RETENU tient sur une ligne de reglage (Settings : cle = valeur) :
//    "c1;Type,1,egal,BOOL,,;Commentaire,6,contient,pompe,,"   des colonnes
//    "s1;TON_D;Situees"                                        une recherche, une pastille
//  Chaque champ est echappe (%, ;, la virgule, |, =, les blancs des bords, les
//  caracteres de controle : %XX). Un texte vide : rien de retenu (oublie).
// =============================================================================
#pragma once

#include "DataViews.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

class InputText;

class FilterMemory {
public:
    // ---- le crochet, que l'application branche ------------------------------
    struct Store {
        std::function<std::string(const std::string& key)>                    read;     // "" : rien de retenu
        std::function<void(const std::string& key, const std::string& value)> write;    // "" : oublier la cle
        std::function<void(bool allProjects)>                                  forget;   // facultatif : tout oublier
    };
    static void install(Store store);            // Store{} : plus de memoire (rien n'est lu ni ecrit)
    [[nodiscard]] static bool installed() noexcept;
    [[nodiscard]] static std::string read(const std::string& key);
    static void write(const std::string& key, const std::string& value);
    static void forget(bool allProjects);

    // ---- les widgets qui retiennent quelque chose ---------------------------
    //  Inscrits tant que vit le jeton rendu par enroll (le widget le garde).
    struct Client {
        std::function<void()> reload;    // un autre projet : relire (et poser) ce qu'il retient
        std::function<void()> resave;    // le meme projet, ailleurs : reecrire ce qui est montre
    };
    [[nodiscard]] static std::shared_ptr<void> enroll(Client client);
    static void contextChanged(bool reload);
    [[nodiscard]] static std::size_t enrolled() noexcept;     // (tests)

    // ---- le texte retenu -----------------------------------------------------
    [[nodiscard]] static std::string escape(std::string_view text);
    [[nodiscard]] static std::string unescape(std::string_view text);
    // Des champs echappes, separes par `sep` ; split les rend desechappes.
    [[nodiscard]] static std::string join(const std::vector<std::string>& fields, char sep);
    [[nodiscard]] static std::vector<std::string> split(std::string_view text, char sep);

    // Les filtres d'une table : chacun avec le titre de sa colonne (celui qui
    // compte a la relecture) ; filter.column : son rang quand il a ete ecrit
    // (pour deux colonnes de meme titre).
    struct SavedFilter {
        std::string  title;
        ColumnFilter filter;
    };
    [[nodiscard]] static std::string encodeColumns(const std::vector<SavedFilter>& filters);
    [[nodiscard]] static std::vector<SavedFilter> decodeColumns(std::string_view stored);
    // La colonne d'un filtre relu : celle de ce titre (sans casse ni accents) -
    // au rang retenu s'il y en a plusieurs ; -1 : plus de colonne de ce titre.
    [[nodiscard]] static int columnFor(const std::vector<std::string>& titles, std::string_view title, std::size_t rank);

    // La recherche d'un volet et sa pastille (son libelle).
    [[nodiscard]] static std::string encodeSearch(const std::string& search, const std::string& chip);
    [[nodiscard]] static bool decodeSearch(std::string_view stored, std::string& search, std::string& chip);

    // Ce qu'un texte retenu veut dire, en clair (le journal des scripts) :
    // "Type = BOOL ; Commentaire contient pompe", "recherche << TON_D >>".
    [[nodiscard]] static std::string describe(std::string_view stored);
};

// La recherche (et la pastille) d'un volet, retenue d'une seance a l'autre.
class SearchMemory {
public:
    struct State {
        std::string search, chip;
    };
    SearchMemory() = default;
    SearchMemory(const SearchMemory&) = delete;
    SearchMemory& operator=(const SearchMemory&) = delete;

    // `key` : l'id du widget. get / set : l'etat du volet. Inscrit ; `recallNow` :
    // relit et pose tout de suite (le volet a deja branche ses signaux).
    void bind(std::string key, std::function<State()> get, std::function<void(const State&)> set, bool recallNow);
    // Un champ de recherche seul (son texte ; pas de pastille) : chaque
    // changement est ecrit, et ce qui est retenu est relu tout de suite -
    // textChanged part : le volet doit l'avoir deja branche. `key` vide : l'id du champ.
    void bindField(InputText& field, std::string key = {});
    void recall();              // relire, et poser l'etat retenu (rien : vide)
    void save();                // ecrire l'etat du moment (sauf pendant recall)
    [[nodiscard]] bool recalling() const noexcept { return recalling_; }
    [[nodiscard]] bool bound() const noexcept { return static_cast<bool>(get_); }
    [[nodiscard]] const std::string& key() const noexcept { return key_; }

private:
    std::string                       key_;
    std::function<State()>            get_;
    std::function<void(const State&)> set_;
    bool                              recalling_{false};
    std::shared_ptr<void>             token_;
    core::ConnectionScope             links_;
};

// Les termes d'une recherche, surlignes derriere un texte dessine hors d'une
// table (une liste peinte, des barres) - la meme marque que dans les tableaux.
void drawSearchMarks(gfx::IRenderer& r, const SearchQuery& query, std::string_view text, float x, float y, float h,
                     float maxWidth, gfx::FontId font);

} // namespace ui
