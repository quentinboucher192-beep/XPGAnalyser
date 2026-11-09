// =============================================================================
//  app/ai/AiNotificationsView.hpp - 1.21.2 : L'ONGLET << NOTIFICATIONS >> DU PANNEAU
// -----------------------------------------------------------------------------
//  Dans le panneau de l'Assistant IA (AiPanel), a la place de la conversation
//  quand l'onglet Notifications est choisi : le journal (AiNotifications.hpp),
//  la plus recente d'abord, range par jour (Aujourd'hui, Hier, la date).
//
//    la barre    Toutes | Non lues (leurs nombres) ; Tout marquer comme lu,
//                Effacer les lues (grises quand il n'y a rien a faire) ;
//    une ligne   le ton (sa couleur, son icone ; un filet a gauche), l'heure,
//                la categorie, << x3 >> si elle s'est repetee, le titre (en
//                gras tant qu'elle n'est pas lue), le detail (deux lignes), et
//                son BOUTON qui mene a la chose (la conversation et le message,
//                la carte, la piece jointe, la fenetre Souvenirs, le compte
//                rendu du scan, l'incident) - le panneau le suit (Callbacks).
//
//  Choisie (un clic, les fleches), une ligne devient LUE. Dans << Non lues >>,
//  une ligne lue pendant qu'on la regarde reste montree (grisee) jusqu'au
//  prochain choix du filtre : elle ne s'enfuit pas sous la souris.
//  Le clic droit : Ouvrir, Marquer comme lue / non lue, Copier, Supprimer. Le
//  clavier : Haut, Bas, Debut, Fin, Page precedente / suivante, Entree (le
//  bouton), Espace (lue / non lue), Suppr, Ctrl+C, la touche Menu.
//
//  Il ne connait pas l'Assistant : il lit le journal, et rend le bouton choisi
//  au panneau (Callbacks::follow). Rien de lourd au dessin : les lignes se
//  mettent en page quand le journal, le filtre ou la largeur change.
// =============================================================================
#pragma once

#include "AiNotifications.hpp"
#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app::ai {

class AiNotificationsView final : public ui::Widget {
public:
    struct Callbacks {
        // Le bouton d'une notification : mener a la chose. Faux : rien n'a pu s'ouvrir (le panneau a dit pourquoi).
        std::function<bool(const std::string& action)> follow;
        std::function<void(const std::string& text)>   status;   // la barre d'etat
    };
    // 1.44.0 : WithFiles - << Avec fichiers >> (les notifications generales du Studio : un message avec des fichiers, une
    // mise a jour) ; le filtre n'est la que si la source sait le dire (Source::files).
    enum class Filter : std::uint8_t { All, Unread, WithFiles };
    // 1.38.0 : le carnet lu - celui de l'Assistant (par defaut), ou les notifications generales du Studio (app/Notices.hpp).
    struct Source {
        std::uint64_t (*revision)();
        std::vector<notifs::Item> (*list)();
        bool (*markRead)(std::uint64_t, bool);
        bool (*remove)(std::uint64_t);
        std::size_t (*markAllRead)();
        std::size_t (*clearRead)();
        // 1.44.0 : "1 fichier \xC2\xB7 6,5 Mo" pour une notification qui porte des fichiers (vide : aucun) ; nul : la source
        // n'a pas de fichiers (l'Assistant) - ni le filtre << Avec fichiers >>, ni la ligne.
        std::string (*files)(const notifs::Item&){nullptr};
    };
    [[nodiscard]] static Source assistant();

    explicit AiNotificationsView(std::string id, Callbacks cb = {});
    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }
    void setSource(Source s) {
        src_ = s;
        refresh();
    }

    // Relit le journal si sa revision a change (le panneau, a chaque image) ; vrai : il a change.
    bool poll();
    void refresh();                                   // relit tout de suite

    void   setFilter(Filter f);
    [[nodiscard]] Filter filter() const noexcept { return filter_; }

    // ---- les lignes montrees (apres le filtre), la plus recente d'abord ----
    [[nodiscard]] std::size_t rowCount() const noexcept { return shown_.size(); }
    [[nodiscard]] const notifs::Item* rowItem(std::size_t row) const noexcept { return row < shown_.size() ? &shown_[row] : nullptr; }
    // "Aujourd'hui | 14:05 | Proposition | titre | detail | x3 | Voir la carte | non lue" (les essais, les scripts).
    [[nodiscard]] std::string rowText(std::size_t row) const;
    // Ce que la liste dit, dans l'ordre : les jours ("== Aujourd'hui") et les lignes (rowText).
    [[nodiscard]] std::vector<std::string> lines() const;
    [[nodiscard]] std::size_t allCount() const noexcept { return all_.size(); }
    [[nodiscard]] const std::vector<notifs::Item>& items() const noexcept { return all_; }   // tout le journal relu
    [[nodiscard]] std::size_t unreadCount() const noexcept { return unread_; }
    [[nodiscard]] int  selectedRow() const noexcept { return selected_; }
    bool select(int row);                             // choisie : lue (et en vue)
    bool follow(int row);                             // son bouton : lue, puis le panneau mene a la chose
    bool setRead(int row, bool read);
    bool removeRow(int row);
    bool copyRow(int row);                            // le titre, l'heure, le detail, dans le presse-papiers
    std::size_t markAllRead();
    std::size_t clearRead();

    // ---- la geometrie (apres une mise en page) ----
    [[nodiscard]] gfx::Rect rowRect(std::size_t row) const;      // vide : hors de la vue
    [[nodiscard]] gfx::Rect buttonRect(std::size_t row) const;   // le bouton de la ligne (vide : aucun, ou cache)
    [[nodiscard]] gfx::Rect filterRect(Filter f) const noexcept { return f == Filter::All ? chipAll_ : f == Filter::Unread ? chipUnread_ : chipFiles_; }
    [[nodiscard]] ui::Button& markAllButton() noexcept { return *markAll_; }
    [[nodiscard]] ui::Button& clearReadButton() noexcept { return *clearRead_; }
    [[nodiscard]] ui::PopupMenu* menu() const noexcept { return menu_; }
    void openRowMenu(int row, gfx::Point at);
    enum MenuId : int { kMenuFollow = 1, kMenuRead, kMenuUnread, kMenuCopy, kMenuDelete };
    void menuChosen(int id);

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

    // La cloche (l'onglet du panneau, la liste vide) : dessinee avec des formes, a la couleur donnee.
    static void drawBell(gfx::IRenderer& r, const gfx::Rect& box, gfx::Color c);
    // "Ouvrir", ou le libelle du bouton sans sa fleche.
    [[nodiscard]] static std::string buttonLabel(const notifs::Item& n);

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Line {                         // une ligne de la liste mise en page (relative au haut du contenu)
        bool                     day{false};
        std::string              text;    // le jour
        int                      row{-1}; // la notification montree
        float                    y{0.f}, h{0.f};
        std::string              meta;    // "14:05 . Proposition"
        std::vector<std::string> detail;  // deux lignes au plus (la seconde finit par ... si le reste ne tient pas)
        float                    buttonW{0.f};
        // 1.39.0 : le petit tableau sous le detail (Item::data, << tableau >>) : ses colonnes, ses lignes et leur ton,
        // combien d'autres lignes ne sont pas montrees ; sa hauteur.
        std::vector<std::string>              columns;
        std::vector<std::vector<std::string>> cells;
        std::vector<std::string>              tones;
        int                                   more{0};
        float                                 tableH{0.f};
        std::string                           files;   // 1.44.0 : "1 fichier \xC2\xB7 6,5 Mo" (la puce sous le detail)
    };
    void   rebuild();                      // les lignes montrees : le filtre, les jours
    void   layoutLines();                  // a la largeur
    void   ensureVisible(int row);
    void   updateButtons();
    void   say(const std::string& text) const;
    [[nodiscard]] int       rowAt(gfx::Point p) const;
    [[nodiscard]] gfx::Rect rowArea(const Line& l) const;   // a l'ecran (le defilement compris)
    [[nodiscard]] gfx::Rect buttonArea(const Line& l) const;
    [[nodiscard]] float     viewH() const noexcept { return list_.h; }

    Callbacks                 cb_;
    Source                    src_{assistant()};
    std::vector<notifs::Item> all_, shown_;
    std::set<std::uint64_t>   kept_;       // Non lues : lues pendant qu'on les regardait (gardees jusqu'au prochain choix)
    std::vector<Line>         lines_;
    std::size_t               unread_{0}, read_{0};
    std::uint64_t             seen_{0};
    Filter                    filter_{Filter::All};
    int                       selected_{-1};
    std::uint64_t             selectedId_{0};
    int                       hover_{-1};
    bool                      hoverButton_{false};
    int                       menuRow_{-1};
    float                     scroll_{0.f}, content_{0.f}, laidW_{-1.f};
    std::int64_t              now_{0};     // le jour de reference des titres de jour (au dernier rebuild)
    gfx::Rect                 bar_{}, list_{}, chipAll_{}, chipUnread_{}, chipFiles_{};   // 1.44.0 : chipFiles_ (Avec fichiers)
    std::size_t               withFiles_{0};
    ui::Button*               markAll_{nullptr};
    ui::Button*               clearRead_{nullptr};
    ui::PopupMenu*            menu_{nullptr};
    ui::EdgeScrollBar         listBar_;
    core::ConnectionScope     links_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;
};

} // namespace app::ai
