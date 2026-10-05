// =============================================================================
//  app/screens/HelpChrome.hpp - les boutons, la barre et la palette de l'aide
// -----------------------------------------------------------------------------
//  POURQUOI DES WIDGETS A PART, ET PAS ui::Button / ui::ToolBar.
//
//  La barre de l'aide doit faire trois choses que ToolBar ne fait pas :
//
//    * CACHER un bouton. Enregistrer et Annuler n'ont de sens que quand il y a
//      quelque chose a enregistrer ; ToolBar remet chaque bouton visible a la
//      mise en page, donc un bouton cache reapparait a l'image suivante.
//    * DISTINGUER l'action principale. " Essayer " est ce qu'on vient faire
//      ici ; perdu entre neuf boutons plats de meme poids, il ne se trouve pas.
//    * GROUPER. Naviguer, essayer, ecrire, sortir : quatre familles de gestes,
//      et la barre doit le dire par sa forme avant qu'on lise un libelle.
//
//  ToolBar sert tout le reste de l'application et a ses tests ; le changer
//  pour une page, c'est risquer les autres. Ces widgets vivent donc ici, a cote
//  de la seule page qui s'en sert, et ne dependent que de ui::Widget.
//
//  LA PALETTE. Ctrl+F ouvre un champ unique qui cherche partout : les blocs,
//  les dossiers, les commandes. C'est le geste des editeurs modernes (la
//  palette de VS Code, le Ctrl+K de Slack) : on tape trois lettres, Entree, on
//  y est. Ctrl+K etait prevu ; ui::Key n'a pas de K, et l'ajouter touche la
//  pompe d'evenements SDL, qu'on ne livre pas d'ici.
// =============================================================================
#pragma once

#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/Widget.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// ---------------------------------------------------------------- PillButton --
class PillButton final : public ui::Widget {
public:
    // Ghost   : plat, un voile au survol - la plupart des boutons.
    // Primary : plein, en accent - L'action de la page, une seule par barre.
    // Subtle  : un fond discret et un trait - un choix secondaire mais visible.
    enum class Kind : std::uint8_t { Ghost, Primary, Subtle };
    // Des formes que ui::Icon n'a pas : les fleches d'un navigateur. Collapse
    // et Expand, qu'on leur substituait, sont un carre moins et un carre plus -
    // personne n'y lisait Precedent et Suivant.
    enum class Glyph : std::uint8_t { None, Back, Forward };

    PillButton(std::string text, ui::Icon icon, Kind kind = Kind::Ghost, std::string id = {});

    void setText(std::string t);
    void setIcon(ui::Icon i)       { icon_ = i; invalidateLayout(); }
    void setGlyph(Glyph g)         { glyph_ = g; invalidateLayout(); }
    void setKind(Kind k)           { kind_ = k; invalidate(); }
    void setIconTone(ui::Tone t)   { iconTone_ = t; invalidate(); }
    // Icone seule, dans un cercle. Le libelle reste dans l'infobulle.
    void setCompact(bool c);
    // Une pastille dans le coin : " il y a des modifications ", " ca tourne ".
    void setDot(ui::Tone t)        { dot_ = t; invalidate(); }

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] bool compact() const noexcept {
        return compact_ && (icon_ != ui::Icon::None || glyph_ != Glyph::None);
    }
    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override;

    const core::SignalPtr<> clicked = core::Signal<>::create();

    // Ce que fait un clic, sans clic.
    void clickForTest() { if (enabled()) clicked->emit(); }

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    std::string text_;
    ui::Icon    icon_{ui::Icon::None};
    Glyph       glyph_{Glyph::None};
    Kind        kind_{Kind::Ghost};
    ui::Tone    iconTone_{ui::Tone::None};
    ui::Tone    dot_{ui::Tone::None};
    bool        compact_{false};
    bool        pressed_{false};
    // Le survol arrive en fondu : l'heure a laquelle il a commence, prise a la
    // premiere image qui le voit - un widget n'a pas d'horloge a lui.
    bool        wasHovered_{false};
    double      hoverSince_{-1.0};
    // La police de la derniere image : sizeHint() n'a pas de theme, et le
    // theme a fort contraste ecrit en 22 px - mesurer en 16 faisait deborder
    // le libelle de son bouton.
    gfx::FontId font_{16};
};

// ------------------------------------------------------------------- HelpBar --
// Une rangee de PillButton en groupes. Les boutons caches (Collapsed) ne
// prennent pas de place, et leurs separateurs non plus quand un groupe entier
// disparait. Quand la largeur manque, les boutons passent en icone seule -
// sauf l'action principale, qui garde son libelle jusqu'au bout.
class HelpBar final : public ui::Widget {
public:
    explicit HelpBar(std::string id = {});

    PillButton& add(std::unique_ptr<PillButton> b);
    void        addSeparator();
    void        addSpacer();           // ce qui suit part a droite

    [[nodiscard]] ui::SizeHint sizeHint() const override;
    [[nodiscard]] bool compactForTest() const noexcept { return compact_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    enum class SlotKind : std::uint8_t { Button, Separator, Spacer };
    struct Slot { SlotKind kind; PillButton* button; float x; };
    std::vector<Slot> slots_;
    bool              compact_{false};
};

// -------------------------------------------------------------- HelpTabStrip --
// Lot macros 1 : LES QUATRE ONGLETS DE L'AIDE, en haut de chacun de ses ecrans.
//
//   Aide              le document general (les formats, les ecrans, F1)
//   Macros            chaque macro de libs/Macros, rangee dans ses dossiers
//   Blocs DFB / DDT   les blocs et les types de libs/, dossier par dossier
//   IHM               les vues, les popups, les objets et leurs exemples
//
// Chaque onglet est un ecran (un MenuId). Passer de l'un a l'autre REMPLACE
// l'ecran au lieu de l'empiler : Fermer ramene toujours d'ou l'on venait,
// quel que soit le nombre d'onglets visites.
class HelpTabStrip final : public ui::Widget {
public:
    enum Tab : int { General = 0, Macros = 1, Blocks = 2, Hmi = 3 };
    static constexpr int kTabCount = 4;

    explicit HelpTabStrip(int current, std::string id = "help.tabs");

    [[nodiscard]] int current() const noexcept { return current_; }
    [[nodiscard]] static std::string_view label(int tab) noexcept;
    [[nodiscard]] static std::string_view menuId(int tab) noexcept;    // "help", "help.macros"...
    [[nodiscard]] static std::string_view tooltipOf(int tab) noexcept;
    // Pour les scripts (bouton "IHM") : l'onglet de ce libelle, -1 sinon.
    [[nodiscard]] static int tabOfLabel(std::string_view text) noexcept;
    [[nodiscard]] gfx::Rect tabRect(int tab) const noexcept;
    [[nodiscard]] gfx::Rect closeRect() const noexcept { return close_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override;

    // Un onglet autre que le courant ; le bouton Fermer.
    const core::SignalPtr<int> chosen         = core::Signal<int>::create();
    const core::SignalPtr<>    closeRequested = core::Signal<>::create();
    void clickForTest(int tab);

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] int partAt(gfx::Point p) const noexcept;    // 0..3 un onglet, 4 Fermer, -1 rien
    int         current_{0};
    int         hover_{-1};
    int         pressed_{-1};
    gfx::Rect   rects_[kTabCount]{};
    gfx::Rect   close_{};
    gfx::FontId font_{16};
};

// ------------------------------------------------------------------ la palette --
struct PaletteItem {
    std::string title;      // "DFB_EQ_MOTOR", "Essayer l'exemple"
    std::string detail;     // "Bloc fonction derive . Equipements"
    std::string target;     // ce que `chosen` rend : "lib:X", "cat:IO", "cmd:5"
    ui::Icon    icon{ui::Icon::None};
    ui::Tone    tone{ui::Tone::None};
    std::string shortcut;   // "F5", affiche a droite
    std::string keywords;   // cherches aussi, jamais affiches
    bool        enabled{true};
};

// La pertinence d'une entree pour ce qui est tape. 0 : ne correspond pas.
// Dans l'ordre : le titre COMMENCE par la requete, un MOT du titre commence par
// elle (apres _ . espace, ou une majuscule), le titre la CONTIENT, le detail ou
// les mots-cles la contiennent, et enfin ses lettres apparaissent DANS L'ORDRE
// ("dfbmot" trouve DFB_EQ_MOTOR). Sans casse, sans accents ASCII.
[[nodiscard]] int paletteScore(std::string_view query, const PaletteItem& item);

// Les indices des entrees retenues, les plus pertinentes d'abord ; a score
// egal, l'ordre d'origine. Requete vide : tout, dans l'ordre d'origine.
[[nodiscard]] std::vector<std::size_t> paletteFilter(std::string_view query,
                                                     const std::vector<PaletteItem>& items,
                                                     std::size_t limit = 60);

// Le texte tel qu'il tient dans `maxWidth`, coupe avec "..." s'il deborde - et
// vide si meme les points ne tiennent pas. La coupe recule au debut d'un
// caractere : un " e " coupe en deux laisse un octet orphelin, que la police
// dessine en carre. "..." et pas U+2026 : la police de secours n'a que Latin-1.
[[nodiscard]] std::string fitText(const gfx::IRenderer& r, const std::string& text, gfx::FontId font,
                                  float maxWidth);

class CommandPalette final : public ui::Widget {
public:
    explicit CommandPalette(std::string id = {});

    void open(std::vector<PaletteItem> items);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return visible(); }

    [[nodiscard]] const std::string& query() const noexcept { return query_; }
    [[nodiscard]] const std::vector<std::size_t>& results() const noexcept { return results_; }
    [[nodiscard]] std::size_t currentForTest() const noexcept { return current_; }
    void typeForTest(std::string text);

    // La cible choisie (Entree, clic). La palette est deja fermee quand le
    // signal part : celui qui l'ecoute peut en rouvrir une.
    const core::SignalPtr<const std::string&> chosen =
        core::Signal<const std::string&>::create();

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void refilter();
    void choose(std::size_t resultIndex);
    [[nodiscard]] int rowAt(gfx::Point p) const noexcept;

    std::vector<PaletteItem> items_;
    std::vector<std::size_t> results_;
    std::string              query_;
    std::size_t              current_{0};
    std::size_t              first_{0};      // premiere ligne affichee
    double                   openedAt_{-1.0};
    int                      hover_{-1};
    // La geometrie de la derniere image, pour les clics.
    gfx::Rect                panel_{};
    float                    listTop_{0.f};
    float                    rowH_{44.f};
    std::size_t              visibleRows_{8};
};

} // namespace app
