// =============================================================================
//  ui/widgets/HelpArticleView.hpp — une page d'aide, mise en page
// -----------------------------------------------------------------------------
//  POURQUOI PAS UN MultiLineText EN LECTURE SEULE ?
//
//  Parce qu'une page d'aide n'est pas du code. Un parametre a un nom, un type,
//  une portee, un resume court et une explication longue : cinq choses de
//  natures differentes. Ecrites l'une derriere l'autre en police fixe, elles se
//  lisent comme un listing, c'est-a-dire ligne par ligne, alors qu'on vient y
//  chercher UNE reponse. La hierarchie visuelle - un titre qu'on trouve sans
//  lire, un resume detache, un nom de parametre qui se repere au survol de
//  l'oeil - est ce qui fait la difference entre une documentation qu'on
//  consulte et une documentation qu'on evite.
//
//  CE WIDGET NE SAIT PAS CE QU'EST UN AUTOMATE. Il recoit un document deja
//  compose - des blocs typés - et sait le mettre en page et le peindre. Ce qui
//  transforme un DDT en document vit dans app/, du cote qui a le droit de
//  savoir ce qu'est un DFB. C'est la meme frontiere que partout ailleurs ici.
//
//  LA MISE EN PAGE EST SEPAREE DE LA PEINTURE, et c'est ce qui la rend
//  verifiable : layoutArticle() ne prend qu'une largeur et une fonction qui
//  mesure du texte. Un test lui donne une mesure fausse mais connue - huit
//  pixels par caractere - et verifie que rien ne depasse, que le retour a la
//  ligne tombe entre deux mots, et qu'une page vide ne fait pas zero pixel de
//  haut sans le dire. Aucun ecran n'est necessaire, ce qui compte ici : les
//  machines qui font tourner les tests n'en ont pas.
// =============================================================================
#pragma once

#include "../Icons.hpp"
#include "../Theme.hpp"
#include "../Widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ui {

enum class HelpBlockKind : std::uint8_t {
    Title,        // le nom de l'item
    Subtitle,     // categorie / fichier, en retrait
    Lead,         // le resume : ce qu'on lit en premier
    Heading,      // un intertitre de section
    Paragraph,
    Code,         // un appel ST, en police fixe, sur un fond ; 1.10 : `label` sa notation
                  // (ST, C, C++ : la coloration), `links` les notations de
                  // l'exemple (le selecteur ; la choisie : Tone::Accent)
    Term,         // un parametre : nom + type + portee, puis son aide
    Fault,        // un code de defaut : [n] puis ce qu'il veut dire
    Chips,        // "voir aussi", separes par des points
    Note,         // un avertissement, en ambre
    Meter,        // la couverture de la documentation
    Separator,

    // --- ajoutes a la FIN, pour que les numeros existants ne bougent pas ----
    Hero,         // l'en-tete : nom, badge de genre, version, fil d'Ariane
    Callout,      // un encadre teinte : info, attention, danger
    Bullet,       // un element de liste, avec sa puce ("0", "1", "-")
    Links,        // des puces cliquables, avec leur legende
    Ring,         // la couverture en anneau, et les trous en puces
    Empty,        // un etat vide : un grand pictogramme pale, une phrase, un bouton
    Table,        // un tableau : lignes separees par \n, cellules par \t
};

// "PAS DE COULEUR". gfx::Color{} est un NOIR OPAQUE - sa transparence vaut 255
// par defaut - donc l'absence de couleur se dit avec a == 0, et s'ecrit avec
// cette constante. Un {} passe pour "rien" donnait des puces noires.
inline constexpr gfx::Color kNoColor{0, 0, 0, 0};

// Une puce cliquable. `target` est ce que linkActivated rend : le widget ne sait
// pas ce qu'il designe, et c'est voulu - il ne sait pas ce qu'est un automate.
struct HelpLink {
    std::string text;
    std::string target;
    std::string tip;          // ce que le survol montre
    gfx::Color  color{kNoColor};   // a == 0 : l'accent du theme
    Tone        tone{Tone::None};  // prime sur `color` : resolu par le theme
};

// Une etiquette non cliquable : un type, une portee, une valeur par defaut.
struct HelpPill {
    std::string text;
    gfx::Color  color{kNoColor};   // a == 0 : neutre
    Tone        tone{Tone::None};
    // Une etiquette qui designe quelque chose - un type de la bibliotheque, un
    // mot du glossaire - devient cliquable, et son survol dit ce que c'est.
    std::string target{};
    std::string tip{};
};

struct HelpBlock {
    HelpBlockKind kind{HelpBlockKind::Paragraph};
    std::string   text;     // le corps
    std::string   label;    // nom du parametre, code du defaut, legende du jauge
    std::string   detail;   // type et portee, en retrait
    std::string   extra;    // le resume court venant de la declaration
    float         value{0.f};       // 0..1, pour Meter
    bool          muted{false};     // un parametre encore sans aide

    // --- ajoutes a la FIN : un agregat initialise avec sept champs le reste ---
    gfx::Color            accent{kNoColor};  // la couleur de famille ; a == 0 : aucune
    std::vector<HelpLink> links{};      // Hero (fil d'Ariane), Links, Ring
    std::vector<HelpPill> pills{};      // Term (type, portee, defaut), Hero
    int                   severity{0};  // Callout : 0 info, 1 attention, 2 danger
    Icon                  icon{Icon::None};
    Tone                  tone{Tone::None};  // la famille, dite par son sens
    // 1.10 (chantier P) : un bloc NOUVEAU pour le lecteur ("NOUVEAU \xC2\xB7 1.10") -
    // encadre en orange, l'etiquette sur le cadre. Des blocs qui se suivent avec
    // la meme etiquette partagent un cadre. Vide : rien.
    std::string           novelty{};
};

struct HelpArticle {
    std::vector<HelpBlock> blocks;
    [[nodiscard]] bool empty() const noexcept { return blocks.empty(); }
};

// --- la mise en page, sans renderer -----------------------------------------
enum class HelpRole : std::uint8_t {
    Title, Subtitle, Lead, Heading, Body, Muted, Mono, Accent, Warning, Term, Code,
    // --- ajoutes ---
    Custom,       // la couleur est portee par le run (HelpRun::color / tone)
    Error,        // le texte d'un encadre "danger"
    Novelty,      // 1.10 : le texte blanc de l'etiquette orange des nouveautes
};

// La taille d'un texte, par son ROLE dans l'echelle typographique et non en
// pixels : la mise en page ne connait pas les pixels, c'est le theme qui les
// donne. Un test peut ainsi tout mesurer a huit pixels par caractere.
enum class HelpFont : std::uint8_t { Body, Title, Lead, Heading, Caption, Mono };

struct HelpRun {
    float       x{}, y{};
    std::string text;
    HelpRole    role{HelpRole::Body};
    bool        bold{false};
    bool        mono{false};
    // --- ajoutes ---
    HelpFont    font{HelpFont::Body};
    gfx::Color  color{kNoColor};  // pour HelpRole::Custom
    bool        fauxBold{false};  // trace deux fois, a un pixel : il n'y a pas de gras
    // La classe de jeton pour un run de code colore, 0xFF sinon. La mise en page
    // ne connait pas le theme : elle dit "mot-cle", et c'est la peinture qui
    // dit bleu.
    std::uint8_t syntax{0xFF};
    Tone         tone{Tone::None};
};

enum class HelpDecorKind : std::uint8_t {
    CodePanel, Separator, HeadingRule, Badge, MeterTrack, MeterFill, TermMarker,
    // --- ajoutes ---
    Card,         // une surface surelevee : bordure fine, ombre douce
    Pill,         // une etiquette arrondie, teintee de `color`
    Stripe,       // le liseré de famille
    CalloutBox,   // le fond teinte d'un encadre, et sa barre
    RingTrack, RingFill,
    Gutter,       // la marge des numeros de ligne dans un bloc de code
    Glyph,        // un pictogramme : `value` porte l'Icon
    HeroBand,     // le fond teinte de l'en-tete
    TableStripe,  // une ligne sur deux d'un tableau
    // 1.10 (chantier P) : les nouveautes, en orange.
    NoveltyFrame, // le cadre autour des blocs nouveaux
    NoveltyPill,  // leur etiquette, posee sur le cadre
};

struct HelpDecor {
    HelpDecorKind kind{HelpDecorKind::Separator};
    gfx::Rect     rect{};
    bool          muted{false};
    // --- ajoutes ---
    gfx::Color    color{kNoColor};
    float         value{0.f};
    Tone          tone{Tone::None};
};

// Une zone cliquable, en coordonnees de page.
struct HelpHotspot {
    gfx::Rect   rect{};
    std::string target{};
    std::string tip{};
};

// Un intertitre, pour le sommaire flottant.
struct HelpSection {
    float       y{0.f};
    std::string title;
};

struct HelpLayout {
    std::vector<HelpRun>   runs;
    std::vector<HelpDecor> decor;
    float                  height{0.f};
    // --- ajoutes ---
    std::vector<HelpHotspot> hotspots{};
    std::vector<HelpSection> sections{};
};

// Mesure d'une chaine dans une police donnee. Le booleen dit si c'est la police
// fixe ; la hauteur de ligne est passee a part parce qu'elle ne depend pas du
// texte et qu'un test veut pouvoir la fixer.
//
// `measureFont` et `lineHeightFont` sont FACULTATIFS : sans eux, les tailles de
// l'echelle sont deduites de `measure` et `lineHeight` par proportion. C'est ce
// qui garde valides les tests ecrits avant l'echelle typographique.
struct HelpMetrics {
    std::function<float(std::string_view, bool bold, bool mono)> measure;
    float lineHeight{16.f};
    float monoLineHeight{16.f};
    std::function<float(std::string_view, HelpFont)> measureFont;
    std::function<float(HelpFont)>                   lineHeightFont;
};

[[nodiscard]] HelpLayout layoutArticle(const HelpArticle&, float width, const HelpMetrics&);

// --- le widget ---------------------------------------------------------------
class HelpArticleView : public Widget {
public:
    explicit HelpArticleView(std::string id = {});

    void setArticle(HelpArticle a);
    // 1.10 : le meme article, autrement (une autre notation des exemples) : le
    // defilement reste, pas de fondu.
    void replaceArticle(HelpArticle a);
    [[nodiscard]] const HelpArticle& article() const noexcept { return article_; }

    void scrollToTop();
    [[nodiscard]] float scrollOffset() const noexcept { return scrollY_; }
    [[nodiscard]] float contentHeight() const noexcept { return layout_.height; }

    [[nodiscard]] SizeHint sizeHint() const override;

    // Une puce, un fil d'Ariane, un bouton d'etat vide a ete clique. Le widget
    // rend la cible telle quelle : c'est l'ecran qui sait ce qu'elle designe.
    // Les cibles "copy:<n>" sont traitees ici - le texte du bloc n part dans le
    // presse-papier - puis emises quand meme, pour que l'ecran puisse le dire.
    const core::SignalPtr<const std::string&> linkActivated =
        core::Signal<const std::string&>::create();

    // 1.10 : a l'ecran, le rectangle des zones cliquables dont la cible commence
    // par `prefix` (les premieres qui se suivent et se voient : le selecteur
    // ST | C | C++ d'un exemple, "notation:") ; vide : aucune a l'ecran.
    [[nodiscard]] gfx::Rect hotspotsRect(std::string_view prefix) const;
    // 1.10 : defile jusqu'a la premiere de ces zones ("Me montrer" d'une
    // nouveaute de l'aide). Faux : pas encore mise en page, ou aucune.
    bool revealHotspots(std::string_view prefix);

    // Seams de test : la cible sous un point, et le clic, sans evenement.
    [[nodiscard]] const HelpLayout& layoutForTest() const noexcept { return layout_; }

protected:
    void        onPaint(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;
    void        onLayout() override;

private:
    void rebuild(const PaintContext*);
    [[nodiscard]] float maxScroll() const noexcept;
    [[nodiscard]] float tocWidth(float width) const noexcept;
    [[nodiscard]] int   hotspotAt(gfx::Point p) const noexcept;
    [[nodiscard]] int   tocEntryAt(gfx::Point p) const noexcept;
    void                activate(const std::string& target);
    void                scrollTo(float y);

    HelpArticle article_;
    HelpLayout  layout_;
    float       scrollY_{0.f};
    float       scrollTarget_{0.f};     // le defilement doux court vers lui
    float       builtWidth_{-1.f};
    bool        dirtyLayout_{true};
    bool        estimated_{false};      // mis en page sans renderer : a refaire

    int         hoveredSpot_{-1};       // la zone cliquable sous la souris
    std::string copiedTarget_;          // le bouton Copier qui vient de servir
    double      copiedAt_{-10.0};
    int         hoveredToc_{-1};
    gfx::Point  mouse_{};
    double      now_{0.0};              // le temps de la derniere image
    double      shownAt_{-1.0};         // quand l'article courant est apparu
    std::vector<gfx::Rect> tocRects_;   // les lignes du sommaire, en ecran
};

} // namespace ui
