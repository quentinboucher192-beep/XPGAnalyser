// =============================================================================
//  ui/widgets/DatePicker.hpp - 1.42.0 : LE MINI CALENDRIER
// -----------------------------------------------------------------------------
//  Un champ de date (<< mar. 10/11/2026 >>, l'icone du calendrier a gauche) : un
//  clic ouvre un mois sous lui - les jours de la semaine (L M M J V S D), les
//  fleches pour le mois d'avant et d'apres, aujourd'hui entoure, le jour choisi
//  plein. Un clic sur un jour le choisit et ferme. Des RACCOURCIS en bas (<< 1
//  semaine >>, << 1 mois >>, << Fin du mois >>) se comptent depuis le DEBUT de la
//  periode (setRangeStart : le champ << Du >> d'une periode ; sans lui :
//  aujourd'hui). La periode, du debut au jour choisi, est surlignee. << Effacer >>
//  (setClearable) remet << aucune date >>. Au clavier : les fleches deplacent le
//  jour, Page haut / bas le mois, Entree choisit, Echap ferme.
//
//  Fini la date tapee AAAA-MM-JJ (une faute de frappe, un 31 avril : le bug des
//  reductions). Les dates sont des JOURS (00:00 UTC, en secondes depuis 1970 ;
//  0 : aucune) : a l'appelant de dire si une fin est comprise (une periode << du
//  10 au 16 inclus >> finit le 17 a 00:00).
//
//  Comme une liste deroulante, le mois est dessine au-dessus de tout
//  (requestsOverlayPass) et recoit les clics de toute la surface pendant qu'il
//  est ouvert (un clic a cote le ferme).
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../Widget.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

class DatePicker : public Widget {
public:
    using Seconds = std::int64_t;
    static constexpr Seconds kDay = 86400;

    // Un raccourci : depuis le debut (setRangeStart, sinon aujourd'hui), `days` jours de plus, ou `months` mois (moins un
    // jour : << 1 mois >> du 10 finit le 9 du mois suivant, compris), ou la fin de son mois.
    struct Shortcut {
        std::string label;
        int         days{0};
        int         months{0};
        bool        endOfMonth{false};
    };

    explicit DatePicker(std::string id = {});

    // Le jour choisi (00:00 UTC ; 0 : aucun). Un instant quelconque est ramene a son jour.
    void setDate(Seconds day);
    [[nodiscard]] Seconds date() const noexcept { return day_; }
    // Aujourd'hui (00:00 UTC) : entoure dans le mois ; 0 : l'horloge du systeme.
    void setToday(Seconds day) { today_ = day ? dayOf(day) : 0; }
    [[nodiscard]] Seconds today() const;
    // Le debut de la periode (le champ << Du >>) : la periode jusqu'au jour choisi est surlignee ; les raccourcis s'y
    // comptent. 0 : pas de periode.
    void setRangeStart(Seconds day) { rangeStart_ = day ? dayOf(day) : 0; invalidate(); }
    // Les jours avant celui-ci sont grises (0 : aucun).
    void setMinDate(Seconds day) { min_ = day ? dayOf(day) : 0; }
    void setShortcuts(std::vector<Shortcut> s) { shortcuts_ = std::move(s); }
    void setClearable(bool on) { clearable_ = on; }
    void setPlaceholder(std::string text) { placeholder_ = std::move(text); }

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    // Le mois montre (annee, mois de 1 a 12) ; changer de mois (-1, +1).
    [[nodiscard]] int shownYear() const noexcept { return year_; }
    [[nodiscard]] int shownMonth() const noexcept { return month_; }
    void shiftMonth(int delta);

    // Ce que montre le champ (<< mar. 10/11/2026 >>, ou le texte d'attente).
    [[nodiscard]] std::string fieldText() const;
    // << Novembre 2026 >>.
    [[nodiscard]] std::string monthTitle() const;
    // Les essais et les scripts : ou sont le mois, un jour (vide s'il n'est pas montre), les fleches, un raccourci, Effacer.
    [[nodiscard]] gfx::Rect popupRect() const;
    [[nodiscard]] gfx::Rect dayRect(Seconds day) const;
    [[nodiscard]] gfx::Rect prevRect() const;
    [[nodiscard]] gfx::Rect nextRect() const;
    [[nodiscard]] gfx::Rect shortcutRect(std::size_t i) const;
    [[nodiscard]] gfx::Rect clearRect() const;
    // Choisir comme d'un clic (le mois se ferme) : un jour, un raccourci.
    void pick(Seconds day);
    void pickShortcut(std::size_t i);
    [[nodiscard]] Seconds shortcutDay(std::size_t i) const;

    // Des aides de calendrier (UTC).
    [[nodiscard]] static Seconds dayOf(Seconds t) noexcept;                       // ramene a 00:00
    [[nodiscard]] static Seconds dayFromCivil(int y, int m, int d) noexcept;     // le jour (secondes)
    static void civilFromDay(Seconds day, int& y, int& m, int& d) noexcept;
    [[nodiscard]] static int weekdayMonday0(Seconds day) noexcept;              // 0 lundi ... 6 dimanche
    [[nodiscard]] static int daysInMonth(int y, int m) noexcept;
    [[nodiscard]] static std::string frenchDay(Seconds day);                    // << mar. 10/11/2026 >>

    [[nodiscard]] SizeHint  sizeHint() const override;
    [[nodiscard]] bool      requestsOverlayPass() const override { return open_; }
    [[nodiscard]] gfx::Rect eventBounds() const override;

    // Un jour choisi (ou efface : 0).
    const core::SignalPtr<Seconds> changed = core::Signal<Seconds>::create();

protected:
    void        onPaint(const PaintContext&) override;
    void        onPaintOverlay(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    [[nodiscard]] Seconds firstShown() const;          // le lundi de la premiere ligne
    [[nodiscard]] gfx::Rect gridRect() const;
    [[nodiscard]] Seconds dayAt(gfx::Point p) const;   // 0 : aucun
    [[nodiscard]] float footerHeight() const;
    void choose(Seconds day);
    void showMonthOf(Seconds day);

    Seconds               day_{0}, today_{0}, rangeStart_{0}, min_{0};
    Seconds               hot_{0};               // le jour sous la souris ou le clavier
    int                   year_{1970}, month_{1};
    std::vector<Shortcut> shortcuts_;
    bool                  clearable_{false}, open_{false};
    std::string           placeholder_{"Choisir une date"};
    mutable bool          flipped_{false};
};

} // namespace ui
