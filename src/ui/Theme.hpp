// =============================================================================
//  ui/Theme.hpp — design tokens
// -----------------------------------------------------------------------------
//  Palette is taken from the supplied mockup rather than invented: a desaturated
//  graphite shell so that the only saturated pixels on screen are *state*
//  (module OK green, diagnostic amber, error red, selection blue). In a control
//  room that rule matters more than taste: colour must mean something.
//
//  Metrics are expressed in logical pixels and multiplied by the display scale
//  at layout time, so the same theme serves a 96 dpi panel PC and a 4K laptop.
// =============================================================================
#pragma once

#include "../platform/Geometry.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ui {

struct Palette {
    gfx::Color windowBg, panelBg, headerBg, railBg, inputBg, rowAltBg;
    gfx::Color border, borderStrong, gridLine;
    gfx::Color text, textMuted, textDisabled, textInverted;
    gfx::Color accent, accentHover, accentPressed, selectionBg, selectionText;
    gfx::Color ok, warning, error, info;
    gfx::Color scrollbar, scrollbarHover;

    // Source highlighting. The mapping (blue keywords, teal types, green
    // comments, orange strings, pale-green numbers, mauve preprocessor) is the
    // one Visual Studio, VS Code and Qt Creator all share. Engineers read code
    // faster in a palette their eyes already know, so this is not the place to
    // be original.
    gfx::Color syntaxKeyword, syntaxType, syntaxComment, syntaxString;
    gfx::Color syntaxNumber, syntaxPreprocessor, syntaxFunction, syntaxConstant, syntaxOperator;
};

struct Metrics {
    // UN RYTHME DE QUATRE PIXELS, ET TOUT EN DERIVE.
    //
    // Les hauteurs etaient 24, 28, 38, 30, 26, 84 : aucun rapport entre elles,
    // donc des alignements verticaux qu'il fallait corriger un par un et qui se
    // decalaient des qu'une taille changeait. Toutes sont maintenant des
    // multiples de quatre, et les rapports se lisent : un onglet vaut deux
    // lignes moins huit, une barre d'outils en vaut deux moins huit aussi.
    static constexpr float kUnit = 4.f;

    float rowHeight         = 24.f;   // 6 unites - une ligne de table ou d'arbre
    float headerHeight      = 28.f;   // 7
    float tabHeight         = 32.f;   // 8
    float toolbarHeight     = 40.f;   // 10
    float statusBarHeight   = 24.f;   // 6, comme une ligne
    float railWidth         = 84.f;   // 21
    float indentPerLevel    = 20.f;   // 5
    float expanderSize      = 12.f;   // 3
    float scrollbarWidth    = 12.f;   // 3
    float splitterThickness = 4.f;    // 1
    float radius            = 4.f;    // 1

    // L'elevation, dessinee a la main. Le renderer ne sait pas flouter, donc
    // pas d'ombre portee - mais un trait clair d'un pixel en haut d'une surface
    // et un trait sombre en bas suffisent a la decoller du fond. C'est ce que
    // font macOS et Fluent aux petites tailles de toute facon.
    float elevationHighlight = 0.06f;  // combien eclaircir le trait du haut
    float elevationShadow    = 0.18f;  // combien assombrir celui du bas

    gfx::Edges cellPadding  {4.f, 8.f, 4.f, 8.f};
};

// LE TEXTE PASSE PAR FontAtlas DEPUIS QU'IL EXISTE, et la regle qui etait
// ecrite ici ne vaut plus qu'a moitie.
//
// Elle disait : chaines ASCII, tailles multiples de 8, parce que la police de
// secours de SDL fait 8 x 8 et 190 glyphes. C'est toujours vrai QUAND AUCUNE
// POLICE TrueType N'EST TROUVEE - le renderer retombe alors sur elle. D'ou le
// compromis :
//
//   * les ACCENTS du Latin-1 (e a c o u, « ») sont permis partout : ils sont
//     dans la plage de la police de secours, donc ils s'affichent dans les
//     deux cas ;
//   * au-dela du Latin-1 - le tiret cadratin, les points de suspension,
//     l'etoile - on s'abstient dans les CHAINES, et on DESSINE plutot (une
//     etoile avec Icon::Star, un point avec un rectangle arrondi) ;
//   * les tailles sont libres : FontAtlas rasterise a la taille demandee.
//
// `smallUi` passe de 8 a 13 pour cette raison precise : 8 pixels etaient une
// taille pensee pour la police 8 x 8 agrandie deux fois, et en TrueType ils
// rendent la barre d'etat illisible - c'etait visible sur chaque capture.
struct Fonts {
    gfx::FontId ui{16};        // body text
    gfx::FontId uiBold{16};
    gfx::FontId mono{16};      // addresses, ST source, hex
    gfx::FontId smallUi{13};   // status bar, column sub-labels
                               // NOT `small`: <rpcndr.h> (via windows.h) does
                               // `#define small char`, which breaks the member.

    // L'ECHELLE TYPOGRAPHIQUE. Il n'y a pas de face grasse : `uiBold` porte le
    // meme identifiant que `ui`, donc le renderer ne peut pas les distinguer.
    // La hierarchie passe donc par la TAILLE et la couleur, et les widgets qui
    // veulent du gras le simulent (le texte trace deux fois, a un pixel).
    gfx::FontId title{26};     // le nom d'une page d'aide
    gfx::FontId lead{18};      // son resume, ce qu'on lit en premier
    gfx::FontId heading{16};   // un intertitre
    gfx::FontId caption{13};   // etiquettes, puces, legendes
};

// LES COULEURS QUI NE SONT PAS UN ETAT.
//
// La regle du theme - seuls les ETATS sont satures - reste vraie pour la coque.
// Mais une page d'aide range soixante blocs en cinq familles, et une couleur par
// famille est ce qui permet de savoir ou l'on est sans lire : l'icone de
// l'arbre, le liseré de la page et les puces portent la meme teinte. Ces teintes
// sont a mi-saturation, choisies pour rester lisibles sur les deux fonds, et
// aucune n'est un rouge, un ambre ou un vert d'etat.
struct Brand {
    // Alarmes, Controle, Equipement, E/S, Macros, autre - l'ordre des
    // dossiers de libs/ tries par nom, dont on se sert comme cle.
    gfx::Color family[6]{};

    gfx::Color card{}, cardBorder{}, cardShadow{};   // les surfaces surelevees
    gfx::Color hover{};                               // la ligne sous la souris
    gfx::Color focusRing{};
    gfx::Color codeBg{}, codeGutter{};
    gfx::Color tooltipBg{}, tooltipText{};
    gfx::Color led{}, ledOff{};                       // le banc d'essai

    // La portee d'un parametre : entree bleue, sortie verte, entree-sortie
    // violette. La convention des editeurs de blocs, qu'on lit sans legende.
    gfx::Color scopeIn{}, scopeOut{}, scopeInOut{};
};

// UNE COULEUR DITE PAR SON SENS. Un modele d'arbre ou un compositeur d'article
// ne connait pas le theme - il est construit avant la premiere image, et le
// theme peut changer sous lui. Il dit donc « famille 2 » ou « sortie », et
// c'est la peinture qui dit sarcelle ou vert, dans le theme du moment.
enum class Tone : std::uint8_t {
    None,                        // pas de sens : la couleur portee, ou le neutre
    Accent, Info, Ok, Warning, Error, Muted,
    Family0, Family1, Family2, Family3, Family4, Family5,
    Input, Output, InOut,
};

[[nodiscard]] constexpr Tone familyTone(int index) noexcept {
    return static_cast<Tone>(static_cast<int>(Tone::Family0)
                             + (index < 0 || index > 5 ? 5 : index));
}

// Les trois paliers d'une couverture de documentation, LES MEMES partout -
// l'arbre, l'anneau de la page, la barre : complet (vert), a moitie ou plus
// (ambre), moins (rouge). Une couleur qui changerait de sens d'un endroit a
// l'autre ne voudrait plus rien dire.
[[nodiscard]] constexpr Tone coverageTone(float fraction) noexcept {
    return fraction >= 0.999f ? Tone::Ok : fraction >= 0.5f ? Tone::Warning : Tone::Error;
}

// LE MOUVEMENT, en millisecondes. Court expres : une animation d'interface
// qu'on remarque est une animation trop longue.
struct Motion {
    float hoverMs{120.f};
    float pageFadeMs{140.f};
    float flashMs{450.f};
    float scrollEase{0.28f};     // fraction de l'ecart parcourue par image
};

struct Theme {
    std::string name;
    Palette     color;
    Metrics     metric;
    Fonts       font;
    Brand       brand;
    Motion      motion;

    // ---- Lot API 8 : themes ----
    // Ce que la galerie dit d'un theme, et d'ou il vient. Les integres les
    // recoivent de byName() ; un theme de l'utilisateur, de son fichier
    // (.xpgtheme, ui/ThemeFile.hpp).
    std::string family;        // "Sombres", "Clairs"... (familyLabels())
    std::string description;
    std::string author;        // qui l'a fait (le fichier : "auteur = ..."), vide pour les integres
    std::string base;          // la cle du theme integre de depart ("Dark", "Nord"...)
    bool        user{false};   // un theme de l'utilisateur (un fichier, modifiable)
    // Un theme a contraste eleve (sa famille) : 7:1 partout, pas 4,5.
    [[nodiscard]] bool isHighContrast() const noexcept;
    // ---- fin lot API 8 ----

    // Une couleur de famille, rendue LISIBLE sur ce fond : assombrie sur un
    // theme clair, eclaircie sur un sombre. Sans ca, l'ambre des E/S passe sur
    // du noir et disparait sur du blanc - le test de contraste le verifie.
    [[nodiscard]] gfx::Color onSurface(gfx::Color c) const noexcept;
    [[nodiscard]] bool isDark() const noexcept;

    // La couleur d'une couverture de documentation : coverageTone() resolu.
    [[nodiscard]] gfx::Color coverage(float fraction) const noexcept;

    // La couleur d'un Tone dans ce theme ; `fallback` pour Tone::None.
    [[nodiscard]] gfx::Color tone(Tone t, gfx::Color fallback) const noexcept;

    static Theme dark();     // the mockup
    static Theme light();
    static Theme highContrast();

    // LOT API 6 : SIX DE PLUS. Deux sombres de plus (Nuit, Graphite), la
    // palette Solarized, deux clairs moins eblouissants (Papier, Ardoise) et le
    // fort contraste en clair (l'atelier en plein jour). Chacun garde la regle
    // de la coque - seuls les etats sont satures - et passe 7:1 pour le texte,
    // 4,5:1 pour le texte secondaire et la selection : theme_test le verifie.
    static Theme night();
    static Theme graphite();
    static Theme solarized();
    static Theme paper();
    static Theme slate();
    static Theme highContrastLight();

    // LES THEMES INTEGRES, dans l'ordre de la galerie : les neuf du lot API 6
    // d'abord, puis les trente-quatre du lot API 8. La CLE est ce que
    // retiennent les preferences et les scripts ("Dark", "High contrast" : les
    // noms d'avant ne changent pas) ; le LIBELLE est ce qu'on lit a l'ecran ;
    // la FAMILLE range la galerie (Sombres, Clairs, Colores, Contraste eleve,
    // Industriels).
    struct Info {
        const char* key;
        const char* label;
        const char* description;
        const char* family;      // lot API 8
    };
    [[nodiscard]] static const std::vector<Info>& catalog();
    // Par la cle ou le libelle, sans souci de la casse, des accents ni des
    // espaces ("nuit", "Solarise", "high contrast"). Inconnu : le clair, le
    // defaut - un fichier de preferences d'une version future ne casse rien.
    // Lot API 8 : un theme de l'utilisateur se trouve aussi, par son nom.
    [[nodiscard]] static Theme       byName(std::string_view name);
    [[nodiscard]] static std::string keyOf(std::string_view name);     // "" : inconnu
    [[nodiscard]] static std::string labelOf(std::string_view name);   // le libelle, ou le nom tel quel

    // ---- Lot API 8 : themes ----
    // Les familles de la galerie, dans l'ordre (UTF-8).
    [[nodiscard]] static const std::vector<std::string>& familyLabels();
    // "clairs", "Colores", "contraste eleve" -> le libelle de la famille ("" : aucune).
    [[nodiscard]] static std::string familyOf(std::string_view name);
    // Un nom tape, replie : sans casse, accents ni espaces ("Contraste eleve" ->
    // "contrasteeleve"). Deux noms qui se replient pareil sont le meme theme.
    [[nodiscard]] static std::string fold(std::string_view name);

    // TOUS LES THEMES : les integres (catalog()), puis ceux de l'utilisateur,
    // par nom. Ce que listent la galerie, l'accueil et Reglages.
    struct Entry {
        std::string key, label, description, family;
        bool        user{false};
    };
    [[nodiscard]] static std::vector<Entry> all();
    [[nodiscard]] static bool isBuiltIn(std::string_view name);   // une cle ou un libelle integre

    // LES THEMES DE L'UTILISATEUR, en memoire (le disque : ui/ThemeFile.hpp).
    // Leur `name` est a la fois la cle et le libelle ; il ne se confond avec
    // aucun theme integre (ThemeFile les renomme a l'import).
    [[nodiscard]] static const std::vector<Theme>& userThemes();
    static void setUserThemes(std::vector<Theme> themes);
    static void putUserTheme(Theme theme);                  // ajoute, ou remplace le meme nom
    static bool removeUserTheme(std::string_view name);
    // ---- fin lot API 8 ----
};

} // namespace ui
