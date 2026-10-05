// =============================================================================
//  app/TopBar.hpp - lot API 2 : la barre du haut, repensee
// -----------------------------------------------------------------------------
//  AVANT : 28 icones sans libelle, la moitie en anglais, et des boutons qui
//  creaient une section ou un module au milieu des commandes du projet.
//
//  APRES : sept groupes nommes, comme les barres des volets de l'IHM.
//    1. le projet - son nom, son etat (NEW, DEV, FINISH, LOCK), le point orange
//       s'il reste des modifications ; un clic ouvre le menu Projet (ouvrir,
//       enregistrer, importer le .XHW, reimporter, etat, deverrouiller, Vers
//       Control Expert, menu principal) ;
//    2. Annuler, Retablir, Historique - pour tout le projet ;
//    3. + Nouveau - tout ce qui se cree dans l'API (et Macros) ;
//    4. Aller a... (Ctrl+K), plus large ;
//    5. la simulation en un bloc : Simuler / Pause, Arreter, Un cycle, l'etat
//       en clair et le numero de cycle ;
//    6. Vers Control Expert - le livrable, seul bouton en couleur ; Release ;
//    7. Affichage (tableau de bord, Macros, panneaux, theme) et Aide, en menus.
//
//  RIEN NE DISPARAIT. Chaque bouton de l'ancienne barre garde son action - le
//  meme identifiant, donc le meme raccourci - il change de place, pas de
//  comportement. legacyButtons() les enumere et api2_test verifie que chacun
//  est joignable ; actionForLabel() les retrouve par leur ancien libelle pour
//  que les sessions de capture d'avant le lot 2 se rejouent sans retouche.
//  Lot API 7 : trois d'entre eux (Simulate, Variables, Statistics) ouvrent des
//  onglets de l'API, que l'arbre atteint ; ils ont quitte le menu Affichage,
//  leur ancien libelle retrouve toujours l'action.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../core/Signal.hpp"
#include "../ui/Icons.hpp"
#include "../ui/Widget.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui { class PopupMenu; }

namespace app {

class TopBar final : public ui::Widget {
public:
    enum class Sim : std::uint8_t { Off, Stopped, Running, Paused, Halted };
    enum class Menu : std::uint8_t { None, Project, New, View, Help, Version,
                                     UndoList, Notices, Tasks,     // Lot API 8 : bandeau haut
                                     Both };                       // 1.10 : "Les deux" (l'API et l'IHM)
    struct Entry {
        std::string label;
        std::string shortcut;
        ui::Icon    icon{ui::Icon::None};
        std::string action;
        bool        separator{false};
        bool        heading{false};     // lot API 6 : une ligne de texte en tete du menu
        bool        enabled{true};
        std::string disabledReason;
        [[nodiscard]] bool operator==(const Entry&) const = default;
    };
    // Lot API 6 : LA VERSION QUE L'ON MODIFIE, a cote du nom du projet - "V5 en
    // cours" sur "depuis V4 . validee" ; en FINISH "V4 validee", en LOCK
    // "V4 livree", en NEW "V1 a venir". Un clic ouvre son menu : ce que l'on
    // modifie et depuis quand (en tete), puis Terminer, Livrer et verrouiller,
    // Version intermediaire, les versions.
    struct VersionChip {
        std::string        title, subtitle, tip;
        std::string        tone;          // "new", "dev", "finish", "lock"
        std::vector<Entry> menu;
        [[nodiscard]] bool operator==(const VersionChip&) const = default;
    };

    explicit TopBar(std::string id = {});
    ~TopBar() override;

    // Ou vont les actions : les memes identifiants que l'ancienne barre.
    void setActionSink(std::function<void(core::ActionId)> sink) { sink_ = std::move(sink); }
    // Une entree de menu grisee quand l'action ne peut pas servir (Deverrouiller
    // sur un projet qui n'est pas LOCK).
    void setEnabledProvider(std::function<bool(core::ActionId)> f) { enabled_ = std::move(f); }
    // Le menu deroulant : un PopupMenu pose dans l'OverlayHost de l'ecran (il
    // dessine et se clique par-dessus tout), a la barre seule.
    void setPopup(ui::PopupMenu* popup);
    // Les deux widgets d'avant, poses dans la barre : Aller a... et Release / Debug.
    void setGoTo(ui::WidgetPtr w);
    void setConfiguration(ui::WidgetPtr w);

    void setProject(std::string name, std::string state, bool modified);
    void setVersion(VersionChip chip);
    [[nodiscard]] const VersionChip& version() const noexcept { return version_; }
    // Lot API 6 : l'icone du projet a la place du logo - RGBA, 32 x 32 ; vide :
    // le logo de l'application.
    void setLogo(std::vector<std::uint8_t> rgba32);
    [[nodiscard]] bool hasLogo() const noexcept { return !logo_.empty(); }
    void setHistory(bool canUndo, std::string undoTip, bool canRedo, std::string redoTip);
    void setSimulation(Sim state, std::uint64_t scans);
    // L'infobulle de l'etat de la simulation : pourquoi elle s'est arretee sur
    // un defaut (le message du simulateur), sinon ce que fait chaque bouton.
    void setSimulationNote(std::string note) { simNote_ = std::move(note); }
    // 1.10 : L'IHM A COTE DE L'API - sa pastille, toujours visible quand le projet
    // a une IHM (None : pas d'IHM, rien ne s'affiche). Un clic : "hmi.toggle" (la
    // demarrer ou l'arreter, sans toucher a l'API). La partie "ihm" ; le bloc de la
    // simulation dit "API" devant son etat. A cote, comme la maquette : "ihm-demarrer"
    // (F8), "ihm-arreter" (Maj+F8) et le menu "les-deux" (Menu::Both : Demarrer les
    // deux, Tout arreter).
    enum class Hmi : std::uint8_t { None, Stopped, Running };
    void setHmi(Hmi state);
    [[nodiscard]] Hmi hmi() const noexcept { return hmi_; }

    [[nodiscard]] ui::SizeHint sizeHint() const override;

    // ---- pour les scripts, les tests et le didacticiel ------------------------
    // Les parties : "projet", "annuler", "retablir", "historique", "nouveau",
    // "aller", "simuler", "arreter", "cycle", "etat", "control-expert",
    // "configuration", "affichage", "aide".
    [[nodiscard]] gfx::Rect partRect(std::string_view part) const;
    [[nodiscard]] const std::vector<Entry>& menuEntries(Menu m) const;
    void openMenu(Menu m);
    [[nodiscard]] Menu openedMenu() const noexcept { return opened_; }
    // Une action par son libelle : une partie de la barre ("Historique",
    // "Simuler"), une entree d'un menu ("Enregistrer sous..."), ou un libelle de
    // l'ANCIENNE barre ("Save", "Macros", "Refresh"). Vide si inconnu.
    [[nodiscard]] std::string actionForLabel(std::string_view label) const;
    // L'ancienne barre : { libelle, action }, dans son ordre.
    [[nodiscard]] static const std::vector<std::pair<std::string, std::string>>& legacyButtons();
    // Une action est-elle joignable depuis la nouvelle barre (une partie ou une
    // entree de menu) ?
    [[nodiscard]] bool reaches(std::string_view action) const;
    void trigger(std::string_view action);

    // Lot 7 : l'infobulle de la partie sous la souris, recalculee tant qu'elle
    // est ouverte - l'etat de la simulation et son cycle, ce qu'annule
    // Annuler, la version : ce qu'ils sont maintenant, pas a l'arrivee de la souris.
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

    // ---- Lot API 8 : bandeau haut ----
    //  Trois zones sur 44 px. A gauche : la puce projet (logo, nom, DEV, point
    //  "modifie", Enregistrer integre qui devient "Enregistre"), la puce version
    //  ("V48 . 3 modifications"), Annuler / Retablir et la liste des 10
    //  dernieres actions (le chevron entre les deux). Au centre : la palette
    //  Aller a / Faire... (Ctrl+K), large. A droite : la simulation (etat en
    //  couleur, cycle, mini-courbe du temps de cycle), la cloche des
    //  notifications, les taches de fond, la sortie (Vers Control Expert +
    //  Release), Deposer un fichier, Affichage et Aide en icones.
    struct UndoItem {
        std::string label, when;
        [[nodiscard]] bool operator==(const UndoItem&) const = default;
    };
    struct Notice {
        std::string group;                  // "Projet", "Simulation"...
        std::string title, detail;
        std::string button, action;         // le bouton qui regle la ligne, son action
        std::string tone;                   // "error", "warning", "ok", "info"
        std::string key;                    // ce qui la rend lue (le titre si vide)
        [[nodiscard]] bool operator==(const Notice&) const = default;
    };
    struct Task {
        std::string label;
        float       progress{-1.f};         // 0..1 ; negatif : on ne sait pas
        std::string cancelAction;
        [[nodiscard]] bool operator==(const Task&) const = default;
    };
    static constexpr float kBarHeight = 44.f;
    void setRecentProjects(std::vector<std::string> paths);
    // Les 10 dernieres actions, la plus recente d'abord ; choisir la n-ieme
    // envoie "edit.undoTo:n" (annuler n actions).
    void setUndoList(std::vector<UndoItem> items);
    [[nodiscard]] const std::vector<UndoItem>& undoList() const noexcept { return undoList_; }
    void setNotices(std::vector<Notice> notices);
    [[nodiscard]] const std::vector<Notice>& notices() const noexcept { return notices_; }
    [[nodiscard]] int  unreadNotices() const;
    void               markNoticesRead();
    void setTasks(std::vector<Task> tasks);
    [[nodiscard]] const std::vector<Task>& tasks() const noexcept { return tasks_; }
    // Un echantillon de la mini-courbe (le temps du dernier cycle, la periode).
    void setCycleTime(float ms, float periodMs);
    [[nodiscard]] const std::vector<float>& cycleTimes() const noexcept { return cycleMs_; }
    // Une partie qui n'est plus dans le bandeau (Historique) : son action.
    [[nodiscard]] std::string hiddenPartAction(std::string_view part) const;
    // ---- fin Lot API 8 : bandeau haut ----

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Part {
        std::string key, label, tip, action;
        ui::Icon    icon{ui::Icon::None};
        Menu        menu{Menu::None};
        bool        primary{false};
        bool        iconOnly{false};
        mutable gfx::Rect rect{};
    };
    [[nodiscard]] int  partAt(gfx::Point p) const;
    [[nodiscard]] std::string tipAt(gfx::Point p) const;   // lot 7 : l'infobulle a cet endroit
    void               layoutParts() const;
    [[nodiscard]] bool partEnabled(const Part&) const;
    void               paintProject(const ui::PaintContext&, const Part&, bool hot) const;
    void               paintSim(const ui::PaintContext&) const;
    void               paintHmi(const ui::PaintContext&, const Part&, bool hot) const;   // 1.10
    void               paintVersion(const ui::PaintContext&, const Part&, bool hot) const;
    void               paintLogo(const ui::PaintContext&, const gfx::Rect&) const;

    std::vector<Part>                    parts_;
    std::vector<Entry>                   project_, create_, view_, help_;
    VersionChip                          version_;
    std::vector<std::uint8_t>            logo_;
    mutable gfx::TextureId               logoTex_{};
    mutable bool                         logoDirty_{false};
    mutable gfx::IRenderer*              logoOwner_{nullptr};
    std::function<void(core::ActionId)>  sink_;
    std::function<bool(core::ActionId)>  enabled_;
    ui::Widget*                          goTo_{nullptr};
    ui::Widget*                          config_{nullptr};
    ui::PopupMenu*                       popup_{nullptr};
    Menu                                 opened_{Menu::None};
    std::vector<std::string>             popupActions_;
    core::ConnectionScope                links_;

    std::string   name_{"Projet"}, state_;
    bool          modified_{false};
    bool          canUndo_{false}, canRedo_{false};
    Sim           sim_{Sim::Off};
    Hmi           hmi_{Hmi::None};   // 1.10
    std::uint64_t scans_{0};
    std::string   simNote_;
    int           hover_{-1}, pressed_{-1};
    mutable gfx::Rect simRect_{}, goToRect_{}, configRect_{};
    gfx::Size         surface_{1920.f, 1080.f};

    // ---- Lot API 8 : bandeau haut ----
    void layoutLot8() const;
    void openLot8Menu(Menu m);
    void paintLot8Groups(const ui::PaintContext&) const;
    void paintSave(const ui::PaintContext&, const Part&, bool hot) const;
    void paintBell(const ui::PaintContext&, const Part&, bool hot) const;
    void paintTasks(const ui::PaintContext&, const Part&, bool hot) const;
    void paintSparkline(const ui::PaintContext&, const gfx::Rect&, gfx::Color) const;
    std::vector<std::string> recents_;
    std::vector<UndoItem>    undoList_;
    std::vector<Notice>      notices_;
    std::vector<std::string> readNotices_;
    std::vector<Task>        tasks_;
    std::vector<float>       cycleMs_;
    float                    periodMs_{0.f};
    mutable gfx::Rect        stateRect_{}, undoBox_{}, exportBox_{};
    double                   pressedAt_{0.0};      // le clic long sur Annuler (secondes)
    // ---- fin Lot API 8 : bandeau haut ----
};

} // namespace app
