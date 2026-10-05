// =============================================================================
//  app/StatusStrip.hpp - Lot API 8 : le bandeau bas (la barre d'etat)
// -----------------------------------------------------------------------------
//  Le JOURNAL DES MESSAGES : les 50 derniers messages de la barre d'etat (le
//  point de passage unique : ui::StatusBar::setMessageHook), avec leur heure,
//  leur gravite et l'onglet ou ils ont ete ecrits (pour "Aller a").
//  Les MORCEAUX CLIQUABLES du bandeau : StatusChip (un point ou une icone, un
//  texte, une touche) et StatusStrip (les pastilles de gauche a droite ; celles
//  qui ne tiennent pas s'effacent, les moins utiles d'abord).
//  L'ecran d'analyse les compose : screens/StatusStripWorkspace.cpp.
// =============================================================================
#pragma once

#include "../ui/Widget.hpp"
#include "../ui/Icons.hpp"
#include "../ui/Theme.hpp"
#include "../ui/widgets/Containers.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace app {

struct JournalEntry {
    std::string             time;       // "16:04:05"
    std::string             text;
    ui::StatusBar::Severity severity{ui::StatusBar::Severity::None};
    std::string             target;     // l'onglet ouvert quand il a ete ecrit ("" : pas d'Aller a)
};

class MessageJournal {
public:
    static constexpr std::size_t kMax = 50;
    //  Garde un message. Un message durable sans gravite qui en remplace un
    //  autre du meme genre en moins de 1,5 s (la position de la souris, la
    //  ligne survolee) prend sa place au lieu de noyer le journal ; le meme
    //  message redit ne fait que rafraichir son heure.
    void push(std::string text, ui::StatusBar::Severity severity, bool transient, std::string target,
              double nowSeconds, std::string clock);
    [[nodiscard]] const std::deque<JournalEntry>& entries() const noexcept { return entries_; }   // les plus recents en tete
    void clear();
    //  Le journal en texte (Copier) : une ligne par message, les plus recents en tete.
    [[nodiscard]] std::string toText() const;
    [[nodiscard]] std::size_t revision() const noexcept { return revision_; }
private:
    std::deque<JournalEntry> entries_;
    double      lastPlainAt_{-1.0e9};
    bool        lastPlain_{false};
    std::size_t revision_{0};
};

[[nodiscard]] std::string wallClock(std::int64_t wallMs);        // "HH:MM:SS", l'heure locale
[[nodiscard]] std::string groupedCount(std::uint64_t n);         // "12 480" (espace insecable)
[[nodiscard]] std::string_view severityWord(ui::StatusBar::Severity s) noexcept;   // "info", "ok", "attention", "erreur", ""

// ------------------------------------------------------------- une pastille ---
class StatusChip final : public ui::Widget {
public:
    explicit StatusChip(std::string id);
    void setText(std::string text);
    void setIcon(ui::Icon icon);
    void setTone(ui::Tone tone);            // le texte et l'icone ; None : le texte du theme
    void setDot(bool on, ui::Tone tone);    // le point de couleur a gauche (la simulation)
    void setKey(std::string key);           // une touche dans un cadre, a droite ("Ctrl+S")
    void setMuted(bool muted);              // les raccourcis : gris, pas cliquable
    void setCaret(bool caret);              // le petit chevron vers le haut (le journal)
    //  Le voeu du proprietaire : faux, la pastille n'est pas montree (StatusStrip).
    void setShown(bool shown);
    [[nodiscard]] bool shown() const noexcept { return shown_; }
    //  Elastique (la selection) : elle prend la place qui reste, 140 px au
    //  moins, et son texte se coupe (...) plutot que de disparaitre.
    void setElastic(bool elastic) { elastic_ = elastic; }
    //  Muette : pas de surbrillance au survol (une session rejouee y gare la souris).
    void setQuiet(bool quiet) { quiet_ = quiet; invalidate(); }
    [[nodiscard]] bool elastic() const noexcept { return elastic_; }
    void setOnClick(std::function<void()> f) { onClick_ = std::move(f); }
    void click() { if (onClick_) onClick_(); }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override;
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    std::string text_, key_;
    ui::Icon    icon_{ui::Icon::None};
    ui::Tone    tone_{ui::Tone::None}, dotTone_{ui::Tone::None};
    bool        dot_{false}, muted_{false}, caret_{false}, pressed_{false}, shown_{true}, elastic_{false}, quiet_{false};
    std::function<void()> onClick_;
    core::ConnectionScope links_;       // le survol perdu annule l'appui
};

// ------------------------------------------------------ la rangee du bandeau ---
class StatusStrip final : public ui::Widget {
public:
    explicit StatusStrip(std::string id);
    //  `priority` : plus grand, gardee plus longtemps quand la place manque.
    StatusChip& add(std::unique_ptr<StatusChip> chip, int priority);
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    //  Les pastilles montrees, dans l'ordre (les scripts, les tests).
    [[nodiscard]] std::vector<const StatusChip*> visibleChips() const;
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    struct Slot { StatusChip* chip; int priority; };
    [[nodiscard]] float room() const;                        // la place des pastilles
    [[nodiscard]] float needed(const StatusChip& chip) const; // sa largeur, au moins
    //  Les pastilles gardees, et la largeur de chacune (0 : effacee).
    [[nodiscard]] std::vector<float> widths() const;
    std::vector<Slot> slots_;
};

} // namespace app
