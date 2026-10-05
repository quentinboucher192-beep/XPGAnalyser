#include "Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {
using gfx::Color;

Theme Theme::dark() {
    // LA COQUE RESTE DESATUREE, ET C'EST LA REGLE. Les seuls pixels satures de
    // l'ecran sont un ETAT : voie saine, avertissement, defaut, selection. Dans
    // une salle de controle cette regle compte plus que le gout - une couleur
    // doit vouloir dire quelque chose.
    //
    // CE QUI A CHANGE. L'ancienne palette etait celle de VS Code a l'octet pres:
    // 1E1E1E, 252526, 2D2D30, 333337. Quatre gris dans un intervalle de 21
    // valeurs, donc des surfaces qui ne se distinguent presque pas, et aucune
    // identite. Les paliers font maintenant 40 valeurs, et le graphite est
    // legerement bleute plutot que neutre : un gris pur a cote d'un accent bleu
    // parait sale.
    Theme t;
    t.name = "Dark";
    t.color = Palette{
        /* windowBg      */ Color::rgb(0x14161A),
        /* panelBg       */ Color::rgb(0x1C1F24),
        /* headerBg      */ Color::rgb(0x252931),
        /* railBg        */ Color::rgb(0x2E333C),
        /* inputBg       */ Color::rgb(0x101215),
        /* rowAltBg      */ Color::rgb(0x20242A),
        /* border        */ Color::rgb(0x363C47),
        /* borderStrong  */ Color::rgb(0x4A515E),
        /* gridLine      */ Color::rgb(0x272C34),

        // Un blanc pur sur un fond sombre eblouit et bave. E6E9EF est le blanc
        // que les interfaces sombres utilisent depuis qu'on a cesse de croire
        // que plus de contraste valait mieux.
        /* text          */ Color::rgb(0xE6E9EF),
        /* textMuted     */ Color::rgb(0x9AA2B1),
        /* textDisabled  */ Color::rgb(0x5C6472),
        /* textInverted  */ Color::rgb(0x0B0D10),

        // L'accent est franc. L'ancien, 2F6FB2, etait le bleu Visual Studio
        // delave : il se fondait dans le gris au lieu de designer.
        /* accent        */ Color::rgb(0x4C8DFF),
        /* accentHover   */ Color::rgb(0x6BA1FF),
        /* accentPressed */ Color::rgb(0x2E6FD9),
        /* selectionBg   */ Color::rgb(0x1E3A63),
        /* selectionText */ Color::rgb(0xF2F5FA),

        /* ok            */ Color::rgb(0x3FBF7F),
        // L'AMBRE TIRE FRANCHEMENT VERS LE JAUNE. A E3A23C il n'etait qu'a 95
        // de distance du rouge : c'est la paire qu'il ne faut surtout pas
        // confondre, puisque l'une dit "regarde quand tu pourras" et l'autre
        // "la machine est arretee".
        /* warning       */ Color::rgb(0xF0B429),
        /* error         */ Color::rgb(0xE4574F),
        /* info          */ Color::rgb(0x4CB8E8),

        /* scrollbar     */ Color::rgb(0x3A414D),
        /* scrollbarHover*/ Color::rgb(0x525B6B),

        // La coloration du code ne bouge pas. Bleu pour les mots-cles, vert pour
        // les commentaires, orange pour les chaines : c'est la correspondance
        // que Visual Studio, VS Code et Qt Creator partagent tous. Un
        // automaticien lit plus vite dans une palette que ses yeux connaissent
        // deja, donc ce n'est pas l'endroit ou etre original.
        /* syntaxKeyword */ Color::rgb(0x6BA9E8),
        /* syntaxType    */ Color::rgb(0x56C7B0),
        /* syntaxComment */ Color::rgb(0x6E9E5C),
        /* syntaxString  */ Color::rgb(0xD69A78),
        /* syntaxNumber  */ Color::rgb(0xB8D4A6),
        /* syntaxPreproc */ Color::rgb(0xC98BC4),
        /* syntaxFunction*/ Color::rgb(0xE0DCA8),
        /* syntaxConstant*/ Color::rgb(0x9CD3F5),
        /* syntaxOperator*/ Color::rgb(0xC9CEDA),
    };

    // Les familles, a mi-saturation : assez franches pour se distinguer dans
    // l'arbre, assez douces pour ne pas passer pour un etat.
    t.brand.family[0] = Color::rgb(0xF0766B);   // Alarmes     corail
    t.brand.family[1] = Color::rgb(0xA78BFA);   // Controle    violet
    t.brand.family[2] = Color::rgb(0x2DD4BF);   // Equipement  sarcelle
    t.brand.family[3] = Color::rgb(0xF5B75A);   // E/S         abricot
    t.brand.family[4] = Color::rgb(0x60A5FA);   // Macros      bleu ciel
    t.brand.family[5] = Color::rgb(0xB4BCC8);   // autre       gris
    t.brand.card       = Color::rgb(0x22262D);
    t.brand.cardBorder = Color::rgb(0x323843);
    t.brand.cardShadow = Color::rgb(0x000000).withAlpha(90);
    t.brand.hover      = Color::rgb(0xFFFFFF).withAlpha(14);
    t.brand.focusRing  = Color::rgb(0x4C8DFF).withAlpha(170);
    t.brand.codeBg     = Color::rgb(0x111318);
    t.brand.codeGutter = Color::rgb(0x5C6472);
    t.brand.tooltipBg  = Color::rgb(0x0B0D10);
    t.brand.tooltipText= Color::rgb(0xE6E9EF);
    t.brand.led        = Color::rgb(0x3FBF7F);
    t.brand.ledOff     = Color::rgb(0x3A414D);
    t.brand.scopeIn    = Color::rgb(0x60A5FA);
    t.brand.scopeOut   = Color::rgb(0x4ADE80);
    t.brand.scopeInOut = Color::rgb(0xC084FC);
    return t;
}

Theme Theme::light() {
    // REPRIS EN ENTIER, ET PAS PAR DIFFERENCE. La version precedente redefinissait
    // dix-neuf champs sur une trentaine : borderStrong, rowAltBg, les couleurs
    // d'etat et les ascenseurs restaient ceux du theme sombre. Sur fond blanc, un
    // vert 4EC9A0 et un ambre DCA032 ne passent pas - ils ont ete choisis pour
    // ressortir sur du noir, ce qui est exactement le contraire du probleme.
    Theme t = dark();
    t.name = "Light";
    t.color = Palette{
        /* windowBg      */ Color::rgb(0xEEF0F3),
        /* panelBg       */ Color::rgb(0xFFFFFF),
        /* headerBg      */ Color::rgb(0xE3E7EC),
        /* railBg        */ Color::rgb(0xD8DDE4),
        /* inputBg       */ Color::rgb(0xFFFFFF),
        /* rowAltBg      */ Color::rgb(0xF6F8FA),
        /* border        */ Color::rgb(0xC4CBD4),
        /* borderStrong  */ Color::rgb(0x98A1AE),
        /* gridLine      */ Color::rgb(0xE4E8ED),

        /* text          */ Color::rgb(0x1B1F26),
        /* textMuted     */ Color::rgb(0x5A6472),
        /* textDisabled  */ Color::rgb(0x9AA2AE),
        /* textInverted  */ Color::rgb(0xFFFFFF),

        /* accent        */ Color::rgb(0x1D6FE0),
        /* accentHover   */ Color::rgb(0x3A86F0),
        /* accentPressed */ Color::rgb(0x1557B0),
        /* selectionBg   */ Color::rgb(0xCFE2FA),
        /* selectionText */ Color::rgb(0x0E1218),

        // Les etats, choisis pour du BLANC. Un vert clair sur blanc est illisible
        // a deux metres, ce qui est precisement la distance d'un poste d'atelier.
        /* ok            */ Color::rgb(0x107C41),
        /* warning       */ Color::rgb(0x9A6700),
        /* error         */ Color::rgb(0xC42B1C),
        /* info          */ Color::rgb(0x0B6E99),

        /* scrollbar     */ Color::rgb(0xBFC6CF),
        /* scrollbarHover*/ Color::rgb(0x98A1AE),

        /* syntaxKeyword */ Color::rgb(0x0F3FBF),
        /* syntaxType    */ Color::rgb(0x117A87),
        /* syntaxComment */ Color::rgb(0x1E7A2E),
        /* syntaxString  */ Color::rgb(0xA31515),
        /* syntaxNumber  */ Color::rgb(0x0A6640),
        /* syntaxPreproc */ Color::rgb(0x7A3E8C),
        /* syntaxFunction*/ Color::rgb(0x795E26),
        /* syntaxConstant*/ Color::rgb(0x0058A8),
        /* syntaxOperator*/ Color::rgb(0x1B1F26),
    };

    // Les memes teintes que le sombre, plus denses : sur du blanc, une couleur
    // de mi-saturation se delave. onSurface() les assombrit encore pour le
    // texte ; ici ce sont les teintes des puces et des icones.
    t.brand.family[0] = Color::rgb(0xD9534A);
    t.brand.family[1] = Color::rgb(0x7C5CD6);
    t.brand.family[2] = Color::rgb(0x0E9F8E);
    t.brand.family[3] = Color::rgb(0xC98A1B);
    t.brand.family[4] = Color::rgb(0x2F7AE0);
    t.brand.family[5] = Color::rgb(0x6B7483);
    t.brand.card       = Color::rgb(0xFFFFFF);
    t.brand.cardBorder = Color::rgb(0xDDE2E8);
    t.brand.cardShadow = Color::rgb(0x1B1F26).withAlpha(26);
    t.brand.hover      = Color::rgb(0x1B1F26).withAlpha(10);
    t.brand.focusRing  = Color::rgb(0x1D6FE0).withAlpha(150);
    t.brand.codeBg     = Color::rgb(0xF6F8FA);
    t.brand.codeGutter = Color::rgb(0x9AA2AE);
    t.brand.tooltipBg  = Color::rgb(0x1B1F26);
    t.brand.tooltipText= Color::rgb(0xF2F5FA);
    t.brand.led        = Color::rgb(0x107C41);
    t.brand.ledOff     = Color::rgb(0xC4CBD4);
    t.brand.scopeIn    = Color::rgb(0x2563EB);
    t.brand.scopeOut   = Color::rgb(0x15803D);
    t.brand.scopeInOut = Color::rgb(0x7C3AED);
    return t;
}

Theme Theme::highContrast() {
    // FINI, PAS EBAUCHE. Il redefinissait cinq champs : tout le reste - les
    // bordures secondaires, les lignes de grille, les ascenseurs, la coloration
    // du code - restait celui du theme sombre, avec des contrastes de 3:1 la ou
    // ce theme existe precisement pour en garantir 7:1.
    //
    // Les couleurs sont pures et peu nombreuses. Un theme a fort contraste qui
    // essaie d'etre joli n'est plus un theme a fort contraste.
    Theme t = dark();
    t.name = "High contrast";
    t.font = Fonts{gfx::FontId{22}, gfx::FontId{22}, gfx::FontId{22}, gfx::FontId{16},
                   gfx::FontId{32}, gfx::FontId{24}, gfx::FontId{22}, gfx::FontId{18}};
    t.metric.rowHeight    = 36.f;
    t.metric.headerHeight = 40.f;
    t.metric.tabHeight    = 40.f;
    t.metric.toolbarHeight = 48.f;
    t.metric.statusBarHeight = 32.f;
    t.metric.radius       = 0.f;     // un coin arrondi mange du contraste
    t.color = Palette{
        /* windowBg      */ Color::rgb(0x000000),
        /* panelBg       */ Color::rgb(0x000000),
        /* headerBg      */ Color::rgb(0x1A1A1A),
        /* railBg        */ Color::rgb(0x1A1A1A),
        /* inputBg       */ Color::rgb(0x000000),
        /* rowAltBg      */ Color::rgb(0x121212),
        /* border        */ Color::rgb(0xFFFFFF),
        /* borderStrong  */ Color::rgb(0xFFFFFF),
        /* gridLine      */ Color::rgb(0x6E6E6E),

        /* text          */ Color::rgb(0xFFFFFF),
        /* textMuted     */ Color::rgb(0xD0D0D0),
        /* textDisabled  */ Color::rgb(0x8A8A8A),
        /* textInverted  */ Color::rgb(0x000000),

        /* accent        */ Color::rgb(0x1AEBFF),
        /* accentHover   */ Color::rgb(0x7DF4FF),
        /* accentPressed */ Color::rgb(0x00B8CC),
        /* selectionBg   */ Color::rgb(0x1AEBFF),
        /* selectionText */ Color::rgb(0x000000),

        /* ok            */ Color::rgb(0x3FF23F),
        /* warning       */ Color::rgb(0xFFD400),
        // FF4040 ne faisait que 6,06:1 sur noir - sous le 7:1 que ce theme
        // promet. C'est le test de contraste qui l'a dit ; FF6B6B est encore
        // franchement rouge et passe a 7,6:1.
        /* error         */ Color::rgb(0xFF6B6B),
        /* info          */ Color::rgb(0x1AEBFF),

        /* scrollbar     */ Color::rgb(0x8A8A8A),
        /* scrollbarHover*/ Color::rgb(0xFFFFFF),

        /* syntaxKeyword */ Color::rgb(0x7DC4FF),
        /* syntaxType    */ Color::rgb(0x3FF2C8),
        /* syntaxComment */ Color::rgb(0x8AE88A),
        /* syntaxString  */ Color::rgb(0xFFB066),
        /* syntaxNumber  */ Color::rgb(0xD6F5B0),
        /* syntaxPreproc */ Color::rgb(0xF2A0E8),
        /* syntaxFunction*/ Color::rgb(0xFFF08A),
        /* syntaxConstant*/ Color::rgb(0xBEE8FF),
        /* syntaxOperator*/ Color::rgb(0xFFFFFF),
    };
    // Ici les familles se taisent : ce theme existe pour le contraste, et six
    // teintes de plus sont six occasions de le perdre. Tout passe en blanc,
    // et le liseré de famille reste un trait - la forme dit ce que la couleur
    // ne dit plus.
    for (auto& f : t.brand.family) f = Color::rgb(0xFFFFFF);
    t.brand.card       = Color::rgb(0x000000);
    t.brand.cardBorder = Color::rgb(0xFFFFFF);
    t.brand.cardShadow = Color::rgb(0x000000).withAlpha(0);
    t.brand.hover      = Color::rgb(0xFFFFFF).withAlpha(40);
    t.brand.focusRing  = Color::rgb(0x1AEBFF);
    t.brand.codeBg     = Color::rgb(0x000000);
    t.brand.codeGutter = Color::rgb(0xD0D0D0);
    t.brand.tooltipBg  = Color::rgb(0x000000);
    t.brand.tooltipText= Color::rgb(0xFFFFFF);
    t.brand.led        = Color::rgb(0x3FF23F);
    t.brand.ledOff     = Color::rgb(0x6E6E6E);
    // La portee garde ses trois teintes ici : c'est une information, pas une
    // decoration, et les trois passent 7:1 sur du noir.
    t.brand.scopeIn    = Color::rgb(0x7DC4FF);
    t.brand.scopeOut   = Color::rgb(0x3FF23F);
    t.brand.scopeInOut = Color::rgb(0xF2A0E8);
    return t;
}

// ------------------------------------------------------- lot API 6 : six de plus --
namespace {

// Les 33 couleurs d'une palette, dans l'ordre de Palette : fonds (6), traits
// (3), textes (4), accent et selection (5), etats (4), ascenseurs (2), code (9).
Palette paletteOf(const std::uint32_t (&c)[33]) {
    return Palette{Color::rgb(c[0]),  Color::rgb(c[1]),  Color::rgb(c[2]),  Color::rgb(c[3]),  Color::rgb(c[4]),
                   Color::rgb(c[5]),  Color::rgb(c[6]),  Color::rgb(c[7]),  Color::rgb(c[8]),  Color::rgb(c[9]),
                   Color::rgb(c[10]), Color::rgb(c[11]), Color::rgb(c[12]), Color::rgb(c[13]), Color::rgb(c[14]),
                   Color::rgb(c[15]), Color::rgb(c[16]), Color::rgb(c[17]), Color::rgb(c[18]), Color::rgb(c[19]),
                   Color::rgb(c[20]), Color::rgb(c[21]), Color::rgb(c[22]), Color::rgb(c[23]), Color::rgb(c[24]),
                   Color::rgb(c[25]), Color::rgb(c[26]), Color::rgb(c[27]), Color::rgb(c[28]), Color::rgb(c[29]),
                   Color::rgb(c[30]), Color::rgb(c[31]), Color::rgb(c[32])};
}

// Les surfaces surelevees, le code, l'infobulle et le banc d'essai.
void surfaces(Theme& t, std::uint32_t card, std::uint32_t cardBorder, std::uint32_t code, std::uint32_t gutter,
              std::uint32_t tipBg, std::uint32_t tipText, std::uint32_t ledOff) {
    t.brand.card        = Color::rgb(card);
    t.brand.cardBorder  = Color::rgb(cardBorder);
    t.brand.codeBg      = Color::rgb(code);
    t.brand.codeGutter  = Color::rgb(gutter);
    t.brand.tooltipBg   = Color::rgb(tipBg);
    t.brand.tooltipText = Color::rgb(tipText);
    t.brand.led         = t.color.ok;
    t.brand.ledOff      = Color::rgb(ledOff);
    t.brand.focusRing   = t.color.accent.withAlpha(170);
}

} // namespace

Theme Theme::night() {
    // Un bleu nuit profond et un accent cyan : la salle de controle eteinte.
    // Plus sombre que Sombre, et franchement bleu la ou Sombre n'est que bleute.
    Theme t = dark();
    t.name = "Night";
    static constexpr std::uint32_t c[33] = {
        0x0B1220, 0x111A2E, 0x17233D, 0x1D2A48, 0x0A1020, 0x142038, 0x233255, 0x30446E, 0x18243F,
        0xE3EAF7, 0x8FA0C0, 0x4D5D80, 0x06101F,
        0x38BDF8, 0x5ECBFA, 0x0EA5E9, 0x0E3A5C, 0xF0F7FF,
        0x34D399, 0xFBBF24, 0xF87171, 0x60A5FA,
        0x2A3A5E, 0x3D5282,
        0x7AA2F7, 0x2AC3DE, 0x6A86B0, 0xE0AF68, 0x9ECE6A, 0xBB9AF7, 0xE6D38A, 0x7DCFFF, 0xC0CAF5};
    t.color = paletteOf(c);
    surfaces(t, 0x15203A, 0x26375C, 0x0A1020, 0x4D5D80, 0x06101F, 0xE3EAF7, 0x2A3A5E);
    return t;
}

Theme Theme::graphite() {
    // Sombre et CHAUD : des gris qui tirent vers le brun, sans aucun reflet
    // bleu - pour ceux que le bleute de Sombre fatigue. L'accent reste un bleu
    // pervenche : un accent chaud se confondrait avec l'ambre des avertissements.
    Theme t = dark();
    t.name = "Graphite";
    static constexpr std::uint32_t c[33] = {
        0x161514, 0x1E1D1B, 0x282624, 0x312F2C, 0x121110, 0x222120, 0x3A3733, 0x524E48, 0x2A2826,
        0xECE8E1, 0xA8A198, 0x67615A, 0x121110,
        0x7C9CFF, 0x98B1FF, 0x5F7FE8, 0x2F3550, 0xF4F2EE,
        0x4CC38A, 0xF2B84B, 0xE86A5F, 0x6CB6E6,
        0x45413C, 0x5E5952,
        0x82AAFF, 0x7FCFB8, 0x7F9A6A, 0xE0A77D, 0xC3D69B, 0xD39BCB, 0xE8D9A0, 0xA6D0F0, 0xD6D0C6};
    t.color = paletteOf(c);
    surfaces(t, 0x242220, 0x3A3733, 0x141312, 0x67615A, 0x0E0D0C, 0xECE8E1, 0x45413C);
    return t;
}

Theme Theme::solarized() {
    // LA PALETTE SOLARIZED (Ethan Schoonover), en sombre : base03 pour les
    // panneaux, et sa coloration du code telle quelle - les mots-cles en vert,
    // les chaines en cyan - puisque c'est pour elle qu'on la choisit. Deux
    // ecarts, pour la lisibilite : le texte est plus clair que base0 (9:1 au
    // lieu de 4,8:1), le rouge est eclairci (4,8:1 au lieu de 3,2:1) et le bleu
    // de l'accent aussi (4,8:1 au lieu de 4,1:1).
    Theme t = dark();
    t.name = "Solarized";
    static constexpr std::uint32_t c[33] = {
        0x002029, 0x002B36, 0x073642, 0x0B4150, 0x00212B, 0x04313D, 0x0F4A58, 0x2F6470, 0x063440,
        0xC4CDCB, 0x8FA1A3, 0x586E75, 0x002B36,
        0x3A98DC, 0x55ABE4, 0x2A7FBF, 0x12506A, 0xFDF6E3,
        0x859900, 0xB58900, 0xF0645F, 0x2AA198,
        0x0F4A58, 0x2F6470,
        0x859900, 0xB58900, 0x657B83, 0x2AA198, 0xD33682, 0xCB4B16, 0x268BD2, 0x6C71C4, 0x93A1A1};
    t.color = paletteOf(c);
    surfaces(t, 0x07343F, 0x15505E, 0x00252F, 0x586E75, 0x001A21, 0xEEE8D5, 0x0F4A58);
    return t;
}

Theme Theme::paper() {
    // Clair et CHAUD : un papier creme au lieu du blanc, le texte en brun tres
    // sombre. Moins eblouissant sur un ecran pousse a fond, pour les longues
    // relectures. Les etats sont assombris pour tenir 4,5:1 sur le creme.
    Theme t = light();
    t.name = "Paper";
    static constexpr std::uint32_t c[33] = {
        0xF4EFE6, 0xFBF7F0, 0xEDE5D8, 0xE4DACB, 0xFFFDF9, 0xF7F2E9, 0xD9CDBA, 0xB5A48B, 0xEAE1D3,
        0x2E2A24, 0x6F665A, 0xA99F90, 0xFFFDF9,
        0x2F6F9F, 0x3C82B6, 0x245A82, 0xD8E6F1, 0x1E2A33,
        0x2F7A40, 0x9A5F0C, 0xB83A2E, 0x276E94,
        0xD2C5B1, 0xB5A48B,
        0x1F4E99, 0x1D7A73, 0x5E7F3A, 0xA0461B, 0x2E6B3F, 0x7A3E7A, 0x7A5A1E, 0x1F5E8C, 0x2E2A24};
    t.color = paletteOf(c);
    surfaces(t, 0xFFFDF9, 0xE2D8C8, 0xF7F1E6, 0xA99F90, 0x2E2A24, 0xFBF7F0, 0xD9CDBA);
    t.brand.cardShadow = Color::rgb(0x2E2A24).withAlpha(24);
    t.brand.hover      = Color::rgb(0x2E2A24).withAlpha(12);
    return t;
}

Theme Theme::slate() {
    // Clair et FROID : des gris ardoise, un accent indigo. Le pendant clair de
    // Nuit, pour qui trouve le Clair trop blanc.
    Theme t = light();
    t.name = "Slate";
    static constexpr std::uint32_t c[33] = {
        0xE9EDF2, 0xF4F6F9, 0xDDE3EA, 0xD2DAE4, 0xFFFFFF, 0xEEF2F6, 0xC3CCD8, 0x97A3B4, 0xE1E6EC,
        0x1C2430, 0x5B6778, 0x9AA4B2, 0xFFFFFF,
        0x5B5BD6, 0x7070E0, 0x4747B8, 0xD9DBFA, 0x1C2040,
        0x237A4A, 0x94600A, 0xC53B3B, 0x2B6FA6,
        0xC3CCD8, 0x97A3B4,
        0x3F3FB0, 0x0E7C86, 0x4F7A3A, 0xA23A2A, 0x256B4A, 0x8A3F8F, 0x6B5A1A, 0x2A5DA8, 0x1C2430};
    t.color = paletteOf(c);
    surfaces(t, 0xFFFFFF, 0xD5DCE5, 0xF7F9FB, 0x9AA4B2, 0x1C2430, 0xF4F6F9, 0xC3CCD8);
    return t;
}

Theme Theme::highContrastLight() {
    // LE FORT CONTRASTE, EN CLAIR : noir sur blanc, pour l'atelier en plein jour
    // ou l'ecran d'un poste pres d'une fenetre. Memes tailles et memes coins
    // carres que Contraste eleve ; chaque etat passe 7:1 sur le blanc.
    Theme t = highContrast();
    t.name = "High contrast light";
    static constexpr std::uint32_t c[33] = {
        0xFFFFFF, 0xFFFFFF, 0xF0F0F0, 0xF0F0F0, 0xFFFFFF, 0xF5F5F5, 0x000000, 0x000000, 0x8A8A8A,
        0x000000, 0x2B2B2B, 0x6E6E6E, 0xFFFFFF,
        0x0000CC, 0x0000FF, 0x000099, 0x0000CC, 0xFFFFFF,
        0x005A1A, 0x7A4E00, 0xB00000, 0x004E9A,
        0x6E6E6E, 0x000000,
        0x0000B0, 0x005F5F, 0x1F5F1F, 0x8B1A00, 0x004D26, 0x6B006B, 0x5C4300, 0x003F8C, 0x000000};
    t.color = paletteOf(c);
    for (auto& f : t.brand.family) f = Color::rgb(0x000000);
    surfaces(t, 0xFFFFFF, 0x000000, 0xFFFFFF, 0x2B2B2B, 0x000000, 0xFFFFFF, 0x8A8A8A);
    t.brand.cardShadow = Color::rgb(0x000000).withAlpha(0);
    t.brand.hover      = Color::rgb(0x000000).withAlpha(30);
    t.brand.focusRing  = Color::rgb(0x0000CC);
    t.brand.scopeIn    = Color::rgb(0x0000CC);
    t.brand.scopeOut   = Color::rgb(0x005A1A);
    t.brand.scopeInOut = Color::rgb(0x6B006B);
    return t;
}

// ------------------------------------------- lot API 8 : trente-quatre de plus --
//
// QUARANTE-TROIS THEMES, EN CINQ FAMILLES. Les palettes connues (Nord,
// Dracula, Gruvbox, Solarized...) gardent leur esprit - leurs fonds, leur
// coloration du code - mais pas forcement leurs octets : chaque couleur qui ne
// tenait pas les contrastes de l'application a ete assombrie ou eclaircie, en
// gardant sa teinte (c'est ce que fait "Corriger les contrastes", ThemeFile).
// Trois ecarts de fond, voulus :
//
//   * l'ACCENT n'est jamais un orange ni un jaune (Ayu, par exemple, a le sien
//     en or) : il se confondrait avec l'ambre des avertissements - c'est le bleu
//     de la palette qui le remplace ;
//   * la COQUE reste desaturee, meme dans les Colores et les Industriels (le
//     vert du Terminal vert est pale, l'ambre de l'Ambre aussi) : seuls les
//     etats sont francs ;
//   * DALTONISME dit ok en bleu, alerte en jaune et defaut en orange : les
//     paires que la deuteranopie et la protanopie ne confondent pas.
//
// themecatalog_test verifie chacun : les contrastes, la regle des etats, les noms.
namespace {

constexpr const char* kDarkFamily         = "Sombres";
constexpr const char* kLightFamily        = "Clairs";
constexpr const char* kColorFamily        = "Color\xC3\xA9s";
constexpr const char* kHighContrastFamily = "Contraste \xC3\xA9lev\xC3\xA9";
constexpr const char* kIndustrialFamily   = "Industriels";

// Un theme du lot API 8 : sa palette dans l'ordre de Palette (paletteOf) et
// ses surfaces (carte, bord de carte, fond du code, marge du code, infobulle,
// texte de l'infobulle, LED eteinte). Les tailles, les familles de blocs et les
// portees sont celles de sa base, le sombre ou le clair.
struct More {
    const char*   key;
    const char*   label;
    const char*   description;
    const char*   family;
    bool          dark;
    std::uint32_t c[33];
    std::uint32_t s[7];
};

constexpr More kMore[] = {
    {"One Dark", "One Dark",
     "Le gris ardoise de l'\xC3\xA9" "diteur Atom : doux, bien contrast\xC3\xA9.",
     kDarkFamily, true,
     {0x21252B, 0x282C34, 0x2C313A, 0x30353F, 0x1D2025, 0x2B3038, 0x3A404B, 0x4B5263, 0x2F343C,
      0xD7DAE0, 0x9DA5B4, 0x5C6370, 0x1D2025, 0x61AFEF, 0x81BFF2, 0x3C9CEB, 0x2C4A6E, 0xEDF0F5,
      0x98C379, 0xE5C07B, 0xE8838B, 0x56B6C2, 0x3E4451, 0x4F5666,
      0xC678DD, 0xE5C07B, 0x8A909B, 0x98C379, 0xD19A66, 0xE88A8F, 0x61AFEF, 0x56B6C2, 0xC0C6D0},
     {0x2C313A, 0x3A404B, 0x23272E, 0x5C6370, 0x1D2025, 0xD7DAE0, 0x3E4451}},
    {"GitHub dark", "GitHub sombre",
     "Le sombre des pages de code en ligne : noir bleut\xC3\xA9, net, accent bleu.",
     kDarkFamily, true,
     {0x010409, 0x0D1117, 0x161B22, 0x161B22, 0x0A0D12, 0x11161D, 0x30363D, 0x484F58, 0x21262D,
      0xE6EDF3, 0x8D96A0, 0x484F58, 0x0D1117, 0x58A6FF, 0x79B8FF, 0x388BFD, 0x1C3A5E, 0xF0F6FC,
      0x3FB950, 0xD29922, 0xF85149, 0x6CB6FF, 0x30363D, 0x484F58,
      0xFF7B72, 0xFFA657, 0x8B949E, 0xA5D6FF, 0x79C0FF, 0xFFA198, 0xD2A8FF, 0x79C0FF, 0xC9D1D9},
     {0x161B22, 0x30363D, 0x0A0D12, 0x484F58, 0x010409, 0xE6EDF3, 0x30363D}},
    {"Visual Studio dark", "Visual Studio sombre",
     "Le gris de l'environnement de d\xC3\xA9veloppement, et sa coloration du code.",
     kDarkFamily, true,
     {0x1B1B1C, 0x252526, 0x2D2D30, 0x333337, 0x1E1E1E, 0x2A2A2C, 0x3F3F46, 0x555558, 0x2D2D30,
      0xF1F1F1, 0xA0A0A0, 0x656565, 0x1E1E1E, 0x3794FF, 0x58A6FF, 0x1C7CD6, 0x264F78, 0xFFFFFF,
      0x89D185, 0xCCA700, 0xF48771, 0x75BEFF, 0x3E3E42, 0x686868,
      0x569CD6, 0x4EC9B0, 0x57A64A, 0xD69D85, 0xB5CEA8, 0x9B9B9B, 0xDCDCAA, 0xB8D7A3, 0xB4B4B4},
     {0x2D2D30, 0x3F3F46, 0x1E1E1E, 0x656565, 0x111111, 0xF1F1F1, 0x3E3E42}},
    {"Material", "Material",
     "Le gris bleu profond de Material, accent sarcelle.",
     kDarkFamily, true,
     {0x1E272C, 0x263238, 0x2E3C43, 0x314549, 0x1F292E, 0x2A373D, 0x37474F, 0x4B5F69, 0x2E3C43,
      0xEEFFFF, 0xB0BEC5, 0x607D8B, 0x1E272C, 0x80CBC4, 0x99D5D0, 0x63BFB7, 0x2F4C57, 0xFFFFFF,
      0xC3E88D, 0xFFCB6B, 0xFF7086, 0x82AAFF, 0x37474F, 0x4B5F69,
      0xC792EA, 0xFFCB6B, 0x8499A3, 0xC3E88D, 0xF78C6C, 0xFF9CAC, 0x82AAFF, 0xF78C6C, 0x89DDFF},
     {0x2B3940, 0x37474F, 0x212C31, 0x607D8B, 0x1E272C, 0xEEFFFF, 0x37474F}},
    {"Ayu dark", "Ayu sombre",
     "Presque noir, les couleurs d'Ayu dans le code ; l'accent passe au bleu.",
     kDarkFamily, true,
     {0x07090D, 0x0D1017, 0x131721, 0x161B25, 0x0B0E14, 0x10141C, 0x232834, 0x3A4150, 0x161B24,
      0xD9D7CE, 0x8A9199, 0x565B66, 0x0B0E14, 0x59C2FF, 0x7DCFFF, 0x30B3FF, 0x1B3A55, 0xF2F0E8,
      0xAAD94C, 0xFFB454, 0xF07178, 0x95E6CB, 0x232834, 0x3A4150,
      0xFF8F40, 0x59C2FF, 0x7A838E, 0xAAD94C, 0xD2A6FF, 0xE6B673, 0xFFB454, 0xD2A6FF, 0xF29668},
     {0x131721, 0x232834, 0x0B0E14, 0x565B66, 0x0B0E14, 0xD9D7CE, 0x232834}},
    {"Ink", "Encre",
     "Des gris neutres, sans aucun reflet : le noir et blanc, et les \xC3\xA9tats.",
     kDarkFamily, true,
     {0x121212, 0x1A1A1A, 0x232323, 0x2A2A2A, 0x0F0F0F, 0x1F1F1F, 0x333333, 0x474747, 0x262626,
      0xE8E8E8, 0xA3A3A3, 0x5E5E5E, 0x0F0F0F, 0x8AB4F8, 0xACCAFA, 0x649BF6, 0x2B3F5C, 0xF5F8FC,
      0x5CC98A, 0xF2BC3C, 0xF2685F, 0x6EC3F0, 0x333333, 0x474747,
      0x8AB4F8, 0x6FD1C0, 0x7DA36E, 0xE0A37F, 0xBFD8AE, 0xD29BCB, 0xE3DFAF, 0xA8D6F5, 0xCFCFCF},
     {0x202020, 0x333333, 0x141414, 0x5E5E5E, 0x0F0F0F, 0xE8E8E8, 0x333333}},
    {"Solarized light", "Solaris\xC3\xA9 clair",
     "La palette Solarized en clair, le texte assombri pour se lire.",
     kLightFamily, false,
     {0xEAE3CF, 0xFDF6E3, 0xEEE8D5, 0xE6DFCA, 0xFFFBF0, 0xF7F0DC, 0xD6CEB5, 0xB3AA90, 0xEAE3CF,
      0x073642, 0x52676E, 0x93A1A1, 0xFDF6E3, 0x1A6DAA, 0x1F81C9, 0x155687, 0xD7E4EC, 0x073642,
      0x5C6B00, 0x8A6800, 0xC0211E, 0x1B7A73, 0xD6CEB5, 0xB3AA90,
      0x697800, 0x8F6C00, 0x829292, 0x217E77, 0xCF2D7C, 0xC44915, 0x2076B3, 0x6369C1, 0x60747C},
     {0xFFFBF0, 0xD6CEB5, 0xFDF6E3, 0x93A1A1, 0x073642, 0xFDF6E3, 0xD6CEB5}},
    {"GitHub light", "GitHub clair",
     "Le blanc des pages de code en ligne : gris l\xC3\xA9gers, accent bleu.",
     kLightFamily, false,
     {0xF6F8FA, 0xFFFFFF, 0xEFF2F5, 0xEAEEF2, 0xFFFFFF, 0xF6F8FA, 0xD0D7DE, 0xAFB8C1, 0xEAEEF2,
      0x1F2328, 0x59636E, 0x8C959F, 0xFFFFFF, 0x0969DA, 0x218BFF, 0x0550AE, 0xDDF4FF, 0x0A3069,
      0x1A7F37, 0x9A6700, 0xCF222E, 0x0550AE, 0xD0D7DE, 0xAFB8C1,
      0xCF222E, 0x953800, 0x6E7781, 0x0A3069, 0x0550AE, 0xA40E26, 0x8250DF, 0x0550AE, 0x1F2328},
     {0xFFFFFF, 0xD0D7DE, 0xF6F8FA, 0x8C959F, 0x24292F, 0xFFFFFF, 0xD0D7DE}},
    {"Visual Studio light", "Visual Studio clair",
     "Le clair de l'environnement de d\xC3\xA9veloppement : gris doux, code sur blanc.",
     kLightFamily, false,
     {0xEEEEF2, 0xF5F5F5, 0xE7E8EC, 0xE0E0E6, 0xFFFFFF, 0xEFEFF3, 0xCCCEDB, 0xA0A3B8, 0xE5E5EA,
      0x1E1E1E, 0x5C5C66, 0xA2A4A5, 0xFFFFFF, 0x0065A9, 0x007ACC, 0x005A9E, 0xCCE4F7, 0x1E1E1E,
      0x107C10, 0x8A6A00, 0xC50F1F, 0x1C6EA4, 0xCCCEDB, 0xA0A3B8,
      0x0000FF, 0x26809B, 0x008000, 0xA31515, 0x098658, 0x767676, 0x795E26, 0x0070C1, 0x1E1E1E},
     {0xFFFFFF, 0xCCCEDB, 0xFFFFFF, 0xA2A4A5, 0x1E1E1E, 0xF5F5F5, 0xCCCEDB}},
    {"Visual Studio blue", "Visual Studio bleu",
     "Le bleu lavande des anciennes versions : cadre bleut\xC3\xA9, documents blancs.",
     kLightFamily, false,
     {0xCFD8EA, 0xFFFFFF, 0xDDE4F2, 0xC5D0E6, 0xFFFFFF, 0xF2F5FB, 0xA9B8D6, 0x6F85B3, 0xE3E8F2,
      0x1E1E1E, 0x4A5670, 0x8E9AB3, 0xFFFFFF, 0x1E5AA8, 0x236AC6, 0x184785, 0xC5D7F2, 0x0E1E3A,
      0x107C10, 0x8A6A00, 0xC50F1F, 0x1C6EA4, 0xA9B8D6, 0x6F85B3,
      0x0000FF, 0x26809B, 0x008000, 0xA31515, 0x098658, 0x767676, 0x795E26, 0x0070C1, 0x1E1E1E},
     {0xFFFFFF, 0xB9C6DF, 0xFFFFFF, 0x8E9AB3, 0x293955, 0xFFFFFF, 0xA9B8D6}},
    {"Ayu light", "Ayu clair",
     "Blanc lumineux, les couleurs d'Ayu assombries pour se lire.",
     kLightFamily, false,
     {0xF3F4F5, 0xFCFCFC, 0xEEEFF1, 0xE8E9EB, 0xFFFFFF, 0xF6F7F8, 0xD8DADD, 0xB3B7BD, 0xECEDEF,
      0x3D4247, 0x696D75, 0xABB0B6, 0xFFFFFF, 0x1879BF, 0x59AEEA, 0x1C8BDB, 0xD6E8F7, 0x252A2F,
      0x5F7F00, 0xA5670C, 0xE02929, 0x277E9B, 0xD8DADD, 0xB3B7BD,
      0xBF5305, 0x1878BD, 0x8C929B, 0x5F7E00, 0x8F5CC1, 0xC25017, 0xA3660C, 0x8F5CC1, 0xC25017},
     {0xFFFFFF, 0xD8DADD, 0xFAFAFA, 0xABB0B6, 0x3D4247, 0xFCFCFC, 0xD8DADD}},
    {"Nord", "Nord",
     "Bleu polaire, froid et doux : les gris de l'Arctique et le givre.",
     kColorFamily, true,
     {0x242933, 0x2E3440, 0x3B4252, 0x3B4252, 0x272C36, 0x323846, 0x434C5E, 0x4C566A, 0x3B4252,
      0xECEFF4, 0xA6AEBB, 0x616E88, 0x242933, 0x88C0D0, 0xA2CEDA, 0x6BB1C4, 0x3E5068, 0xECEFF4,
      0xA3BE8C, 0xEBCB8B, 0xE0848C, 0x8FB5D6, 0x434C5E, 0x4C566A,
      0x81A1C1, 0x8FBCBB, 0x929DB3, 0xA3BE8C, 0xB48EAD, 0x7A9CC6, 0x88C0D0, 0xB48EAD, 0x81A1C1},
     {0x353C4A, 0x434C5E, 0x2A2F3A, 0x616E88, 0x1D2129, 0xECEFF4, 0x434C5E}},
    {"Dracula", "Dracula",
     "Le violet nocturne et ses couleurs vives dans le code.",
     kColorFamily, true,
     {0x21222C, 0x282A36, 0x303241, 0x343746, 0x21222C, 0x2D2F3D, 0x44475A, 0x5A5E77, 0x343746,
      0xF8F8F2, 0xA9ADC8, 0x6272A4, 0x21222C, 0xBD93F9, 0xD2B5FB, 0xA56CF7, 0x4B4570, 0xF8F8F2,
      0x50FA7B, 0xFFB86C, 0xFF6E6E, 0x8BE9FD, 0x44475A, 0x5A5E77,
      0xFF79C6, 0x8BE9FD, 0x7C8ABB, 0xF1FA8C, 0xBD93F9, 0xFFB86C, 0x50FA7B, 0xBD93F9, 0xFF79C6},
     {0x2F3140, 0x44475A, 0x21222C, 0x6272A4, 0x191A21, 0xF8F8F2, 0x44475A}},
    {"Monokai", "Monokai",
     "Le fond olive sombre et les couleurs franches de Monokai.",
     kColorFamily, true,
     {0x1E1F1C, 0x272822, 0x31322C, 0x3E3D32, 0x1E1F1C, 0x2D2E27, 0x3E3D32, 0x57574A, 0x34352E,
      0xF8F8F2, 0xB0AE9E, 0x75715E, 0x1E1F1C, 0xAE81FF, 0xC5A5FF, 0x9458FF, 0x49483E, 0xF8F8F2,
      0xA6E22E, 0xFD971F, 0xFF4A86, 0x66D9EF, 0x3E3D32, 0x57574A,
      0xF93078, 0x66D9EF, 0x8F8B75, 0xE6DB74, 0xAE81FF, 0xFD971F, 0xA6E22E, 0xAE81FF, 0xF93078},
     {0x2F302A, 0x3E3D32, 0x1E1F1C, 0x75715E, 0x161714, 0xF8F8F2, 0x3E3D32}},
    {"Gruvbox dark", "Gruvbox sombre",
     "Brun chaud et couleurs r\xC3\xA9tro, contrast\xC3\xA9 sans \xC3\xA9" "blouir.",
     kColorFamily, true,
     {0x1D2021, 0x282828, 0x32302F, 0x3C3836, 0x1D2021, 0x2E2C2B, 0x504945, 0x665C54, 0x32302F,
      0xEBDBB2, 0xA89984, 0x7C6F64, 0x1D2021, 0x83A598, 0x98B4A9, 0x6B9484, 0x3F4A45, 0xFBF1C7,
      0xB8BB26, 0xFABD2F, 0xFC6450, 0x83A598, 0x504945, 0x665C54,
      0xFB4934, 0xFABD2F, 0xA08F7F, 0xB8BB26, 0xD3869B, 0xFE8019, 0x8EC07C, 0xD3869B, 0xEBDBB2},
     {0x32302F, 0x504945, 0x1D2021, 0x7C6F64, 0x141617, 0xEBDBB2, 0x504945}},
    {"Gruvbox light", "Gruvbox clair",
     "Le cr\xC3\xA8me de Gruvbox et ses couleurs r\xC3\xA9tro, assombries.",
     kColorFamily, false,
     {0xF2E5BC, 0xFBF1C7, 0xEBDBB2, 0xE5D4A8, 0xF9F5D7, 0xF5EBC0, 0xD5C4A1, 0xBDAE93, 0xEBDBB2,
      0x3C3836, 0x665C54, 0xA89984, 0xFBF1C7, 0x076678, 0x09839A, 0x054551, 0xCFE0DA, 0x282828,
      0x75700E, 0x976311, 0x9D0006, 0x076678, 0xD5C4A1, 0xBDAE93,
      0x9D0006, 0x9A6411, 0x928374, 0x77720E, 0x8F3F71, 0x427B58, 0x427B58, 0x8F3F71, 0xAF3A03},
     {0xF9F5D7, 0xD5C4A1, 0xF9F5D7, 0xA89984, 0x3C3836, 0xFBF1C7, 0xD5C4A1}},
    {"Tokyo Night", "Tokyo nuit",
     "Le bleu violac\xC3\xA9 des n\xC3\xA9ons dans la nuit, doux pour les yeux.",
     kColorFamily, true,
     {0x16161E, 0x1A1B26, 0x1F2335, 0x24283B, 0x16161E, 0x1E202E, 0x292E42, 0x414868, 0x202330,
      0xC0CAF5, 0x8890B8, 0x565F89, 0x16161E, 0x7AA2F7, 0x9CBAF9, 0x5487F5, 0x2E3C64, 0xDDE3FB,
      0x9ECE6A, 0xE0AF68, 0xF7768E, 0x7DCFFF, 0x292E42, 0x414868,
      0xBB9AF7, 0x2AC3DE, 0x7A83AF, 0x9ECE6A, 0xFF9E64, 0xF7768E, 0x7AA2F7, 0xFF9E64, 0x89DDFF},
     {0x1F2335, 0x292E42, 0x16161E, 0x565F89, 0x101014, 0xC0CAF5, 0x292E42}},
    {"Catppuccin Mocha", "Catppuccin Mocha",
     "Des pastels sur un fond moka, le mauve en accent.",
     kColorFamily, true,
     {0x181825, 0x1E1E2E, 0x252536, 0x313244, 0x181825, 0x232334, 0x313244, 0x45475A, 0x28283A,
      0xCDD6F4, 0xA6ADC8, 0x6C7086, 0x11111B, 0xCBA6F7, 0xDEC7FA, 0xB581F4, 0x3B3552, 0xE4E9FB,
      0xA6E3A1, 0xF9E2AF, 0xF38BA8, 0x89B4FA, 0x313244, 0x45475A,
      0xCBA6F7, 0xF9E2AF, 0x9399B2, 0xA6E3A1, 0xFAB387, 0xF5C2E7, 0x89B4FA, 0xFAB387, 0x89DCEB},
     {0x252536, 0x313244, 0x181825, 0x6C7086, 0x11111B, 0xCDD6F4, 0x313244}},
    {"Catppuccin Latte", "Catppuccin Latte",
     "Les pastels de Catppuccin sur un fond lait, assombris pour se lire.",
     kColorFamily, false,
     {0xE6E9EF, 0xEFF1F5, 0xDCE0E8, 0xDCE0E8, 0xF7F8FA, 0xE9ECF1, 0xCCD0DA, 0xACB0BE, 0xE1E4EA,
      0x3C3F58, 0x5C5F77, 0x9CA0B0, 0xEFF1F5, 0x8839EF, 0x9C5AF2, 0x7113EC, 0xDCD2F5, 0x2E3048,
      0x327D22, 0x996214, 0xD20F39, 0x1962F5, 0xCCD0DA, 0xACB0BE,
      0x8839EF, 0x9F6615, 0x7C7F93, 0x348223, 0xC74901, 0xCF20A1, 0x1E66F5, 0xC74901, 0x037AAA},
     {0xF7F8FA, 0xCCD0DA, 0xF7F8FA, 0x9CA0B0, 0x4C4F69, 0xEFF1F5, 0xCCD0DA}},
    {"Everforest", "Everforest",
     "Le vert-gris de la for\xC3\xAAt, chaud et apaisant.",
     kColorFamily, true,
     {0x232A2E, 0x2D353B, 0x343F44, 0x3D484D, 0x272E33, 0x313B40, 0x475258, 0x56635F, 0x343F44,
      0xE0D6BD, 0x9FAAA1, 0x7A8478, 0x232A2E, 0x7FBBB3, 0x96C7C1, 0x64ADA3, 0x3A5250, 0xF2EBD9,
      0xA7C080, 0xDBBC7F, 0xEC8A8C, 0x7FBBB3, 0x475258, 0x56635F,
      0xE67E80, 0xDBBC7F, 0x929F95, 0xA7C080, 0xD699B6, 0xE69875, 0x83C092, 0xD699B6, 0xE69875},
     {0x343F44, 0x475258, 0x272E33, 0x7A8478, 0x1E2326, 0xE0D6BD, 0x475258}},
    {"Rose Pine", "Ros\xC3\xA9 Pine",
     "Le violet sourd et les roses fan\xC3\xA9s de Ros\xC3\xA9 Pine.",
     kColorFamily, true,
     {0x191724, 0x1F1D2E, 0x26233A, 0x2A273F, 0x191724, 0x232136, 0x403D52, 0x524F67, 0x26233A,
      0xE0DEF4, 0x908CAA, 0x6E6A86, 0x191724, 0xC4A7E7, 0xD7C3EF, 0xAF87DE, 0x403D52, 0xE0DEF4,
      0x95D5A6, 0xF6C177, 0xEB6F92, 0x9CCFD8, 0x403D52, 0x524F67,
      0x4A92AE, 0x9CCFD8, 0x8581A0, 0xF6C177, 0xC4A7E7, 0xEB6F92, 0xEBBCBA, 0xEBBCBA, 0x908CAA},
     {0x26233A, 0x403D52, 0x191724, 0x6E6A86, 0x13111C, 0xE0DEF4, 0x403D52}},
    {"Kanagawa", "Kanagawa",
     "L'encre et le bleu de la vague d'Hokusai.",
     kColorFamily, true,
     {0x16161D, 0x1F1F28, 0x2A2A37, 0x2A2A37, 0x16161D, 0x242430, 0x363646, 0x54546D, 0x2A2A37,
      0xDCD7BA, 0xA09C8F, 0x54546D, 0x16161D, 0x7E9CD8, 0x99B1E0, 0x5F84CF, 0x223249, 0xDCD7BA,
      0x98BB6C, 0xFF9E3B, 0xFF5D62, 0x7FB4CA, 0x363646, 0x54546D,
      0x957FB8, 0x7AA89F, 0x8A897F, 0x98BB6C, 0xD27E99, 0xE46876, 0x7E9CD8, 0xFFA066, 0xC0A36E},
     {0x2A2A37, 0x363646, 0x1A1A22, 0x54546D, 0x101014, 0xDCD7BA, 0x363646}},
    {"Ayu mirage", "Ayu mirage",
     "Le bleu ardoise d'Ayu mirage, entre le sombre et le clair.",
     kColorFamily, true,
     {0x1A1F29, 0x242936, 0x282E3B, 0x2D3342, 0x1F2430, 0x282D3A, 0x343B4A, 0x4A5366, 0x2A303D,
      0xCCCAC2, 0x959CA5, 0x5C6773, 0x1A1F29, 0x73D0FF, 0x97DCFF, 0x4AC2FF, 0x33415E, 0xE6E4DC,
      0xBAE67E, 0xFFCC66, 0xFF6666, 0x5CCFE6, 0x343B4A, 0x4A5366,
      0xFFAD66, 0x73D0FF, 0x858F9E, 0xD5FF80, 0xDFBFFF, 0xF28779, 0xFFD173, 0xDFBFFF, 0xF29E74},
     {0x2A303D, 0x343B4A, 0x1F2430, 0x5C6773, 0x151920, 0xCCCAC2, 0x343B4A}},
    {"Oceanic", "Oc\xC3\xA9" "anique",
     "Le bleu-vert du large, et les couleurs d'Oceanic Next.",
     kColorFamily, true,
     {0x16232A, 0x1B2B34, 0x22333D, 0x2A3A44, 0x17252D, 0x1F3039, 0x343D46, 0x4F5B66, 0x243540,
      0xD8DEE9, 0xA7ADBA, 0x65737E, 0x16232A, 0x5FB3B3, 0x77BEBE, 0x4B9E9E, 0x2B4555, 0xE6EBF2,
      0x99C794, 0xFAC863, 0xF2757C, 0x6699CC, 0x343D46, 0x4F5B66,
      0xC594C5, 0xFAC863, 0x8391A0, 0x99C794, 0xF99157, 0xF2757C, 0x6699CC, 0xF99157, 0x5FB3B3},
     {0x22333D, 0x343D46, 0x17252D, 0x65737E, 0x111B21, 0xD8DEE9, 0x343D46}},
    {"Full sun", "Plein soleil",
     "Tr\xC3\xA8s clair et tr\xC3\xA8s contrast\xC3\xA9 : l'\xC3\xA9" "cran pr\xC3\xA8s d'une fen\xC3\xAAtre, en plein \xC3\xA9t\xC3\xA9.",
     kHighContrastFamily, false,
     {0xF7F7F2, 0xFFFFFF, 0xEDEDE6, 0xE4E4DC, 0xFFFFFF, 0xF7F7F2, 0x6B6B66, 0x1A1A1A, 0xC9C9C2,
      0x000000, 0x2E2E2E, 0x76766F, 0xFFFFFF, 0x003FAA, 0x0050D0, 0x002F80, 0xBFD7FF, 0x000000,
      0x0B5D1E, 0x6B4A00, 0xA10000, 0x00478F, 0x8C8C85, 0x1A1A1A,
      0x0030A0, 0x005F5F, 0x2D5F2D, 0x8B1A00, 0x004D26, 0x6B006B, 0x5C4300, 0x003F8C, 0x000000},
     {0xFFFFFF, 0x6B6B66, 0xFFFFFF, 0x4A4A45, 0x000000, 0xFFFFFF, 0xA5A59E}},
    {"Color blind", "Daltonisme",
     "Des \xC3\xA9tats s\xC3\xBBrs pour les daltoniens (deut\xC3\xA9ranopie, protanopie) : ok en bleu, alerte en jaune, d\xC3\xA9" "faut en orange.",
     kHighContrastFamily, true,
     {0x101114, 0x17181C, 0x1F2025, 0x26272D, 0x0C0D10, 0x1B1C21, 0x3A3B42, 0x5A5C66, 0x26272C,
      0xF2F2F2, 0xC4C6CC, 0x6E7078, 0x0C0D10, 0xB9A6FF, 0xD5CAFF, 0x997DFF, 0x3D3566, 0xFFFFFF,
      0x56B4E9, 0xF0E442, 0xFF8C42, 0xE6A1CF, 0x3A3B42, 0x5A5C66,
      0x7FB8FF, 0x56D6C0, 0xA3B8A3, 0xF2B880, 0xE6E0A0, 0xE6A1CF, 0xF0E6A8, 0xB5DDF7, 0xE0E0E0},
     {0x1F2025, 0x3A3B42, 0x0C0D10, 0x9A9CA3, 0x000000, 0xF2F2F2, 0x3A3B42}},
    {"Color blind light", "Daltonisme clair",
     "La m\xC3\xAAme id\xC3\xA9" "e en clair : ok en bleu, alerte en ocre, d\xC3\xA9" "faut en brun-rouge fonc\xC3\xA9.",
     kHighContrastFamily, false,
     {0xF2F2F0, 0xFFFFFF, 0xE8E8E5, 0xDEDEDA, 0xFFFFFF, 0xF7F7F5, 0xA8A8A2, 0x5E5E58, 0xE0E0DC,
      0x111111, 0x3A3A3A, 0x8E8E88, 0xFFFFFF, 0x4B2FB5, 0x5A3BCC, 0x3E2795, 0xDCD5F7, 0x111111,
      0x00558C, 0x6B5200, 0x8A2500, 0x6A2A7F, 0xA8A8A2, 0x5E5E58,
      0x1C3FA8, 0x005F5F, 0x3F6B3F, 0x8B3A00, 0x5C4A00, 0x6A2A7F, 0x5C4300, 0x003F8C, 0x111111},
     {0xFFFFFF, 0xA8A8A2, 0xFFFFFF, 0x5E5E58, 0x111111, 0xFFFFFF, 0xA8A8A2}},
    {"Control Expert", "Control Expert",
     "Le gris bleut\xC3\xA9 des logiciels d'automatisme, la s\xC3\xA9lection bleu franc.",
     kIndustrialFamily, false,
     {0xD9DFE8, 0xF2F4F7, 0xC9D2DE, 0xBCC7D6, 0xFFFFFF, 0xE9EDF2, 0xA8B3C4, 0x7A889E, 0xDDE2E9,
      0x1B2533, 0x4B5A70, 0x8D99AB, 0xFFFFFF, 0x1C5DA8, 0x216EC7, 0x164A85, 0x2F65B8, 0xFFFFFF,
      0x127A2E, 0x8A5A00, 0xB8141F, 0x145F99, 0xA8B3C4, 0x7A889E,
      0x0000E0, 0x1F7391, 0x007A00, 0xA31515, 0x8B4513, 0x6E6E6E, 0x795E26, 0x0070C1, 0x1B2533},
     {0xFFFFFF, 0xB5BFCE, 0xFFFFFF, 0x8D99AB, 0xFFFFE1, 0x1B2533, 0xA8B3C4}},
    {"Workshop", "Atelier",
     "Le beige gris des machines-outils (RAL 7032) : l'atelier.",
     kIndustrialFamily, false,
     {0xD8D5C3, 0xECEAE0, 0xCFCBB6, 0xC4BFA6, 0xF7F6F0, 0xE4E1D5, 0xB5B097, 0x8C8770, 0xDEDBCC,
      0x23231C, 0x55533F, 0x9C987F, 0xF7F6F0, 0x2F5D8C, 0x386FA7, 0x25496D, 0xC9D8E6, 0x1A2530,
      0x2D6E2F, 0x8A5500, 0xA8322A, 0x255D86, 0xB5B097, 0x8C8770,
      0x1F4E99, 0x1D6E68, 0x4F6B35, 0x9A3F17, 0x2E6B3F, 0x6E3A6E, 0x6E531B, 0x1F5E8C, 0x23231C},
     {0xF7F6F0, 0xB5B097, 0xF4F2EA, 0x9C987F, 0x23231C, 0xF7F6F0, 0xB5B097}},
    {"Cabinet", "Armoire",
     "Le gris clair des armoires \xC3\xA9lectriques (RAL 7035), sobre et net.",
     kIndustrialFamily, false,
     {0xCBD0CC, 0xE9ECEA, 0xD7DBD8, 0xC3C9C4, 0xFAFBFA, 0xE1E5E2, 0xB4BAB5, 0x858C87, 0xDADEDB,
      0x1A1D1B, 0x4A514C, 0x99A09B, 0xFFFFFF, 0x1F5F99, 0x2571B7, 0x184A77, 0xC3D6E8, 0x0F1A24,
      0x1C7A3A, 0x8A5C00, 0xB3261E, 0x1F6A9A, 0xB4BAB5, 0x858C87,
      0x1B4FA0, 0x11707A, 0x3F7033, 0x9E2B1B, 0x1D6B45, 0x76407F, 0x6B5320, 0x1C5A94, 0x1A1D1B},
     {0xFAFBFA, 0xB4BAB5, 0xF4F6F4, 0x99A09B, 0x1A1D1B, 0xF4F6F4, 0xB4BAB5}},
    {"Operator panel", "Pupitre",
     "Le gris neutre des \xC3\xA9" "crans de supervision (ISA-101) : la couleur ne dit que les \xC3\xA9tats.",
     kIndustrialFamily, false,
     {0xD4D4D4, 0xE6E6E6, 0xCBCBCB, 0xC0C0C0, 0xF5F5F5, 0xDEDEDE, 0xA6A6A6, 0x7A7A7A, 0xD0D0D0,
      0x1C1C1C, 0x444444, 0x8C8C8C, 0xFFFFFF, 0x2B5F9E, 0x3370BA, 0x224C7E, 0xB9CCE4, 0x101820,
      0x0E772E, 0x7D5A00, 0xB00020, 0x1E5AA0, 0xA6A6A6, 0x7A7A7A,
      0x1B4FA0, 0x11707A, 0x3F7033, 0x9E2B1B, 0x1D6B45, 0x6B6B6B, 0x6B5320, 0x1C5A94, 0x1C1C1C},
     {0xF0F0F0, 0xB0B0B0, 0xF5F5F5, 0x8C8C8C, 0x1C1C1C, 0xF5F5F5, 0xA6A6A6}},
    {"Control room", "Salle de contr\xC3\xB4le",
     "Tr\xC3\xA8s sombre et sans reflet, pour la supervision de nuit : seuls les \xC3\xA9tats ressortent.",
     kIndustrialFamily, true,
     {0x0B0C0D, 0x111315, 0x16191C, 0x1B1E22, 0x090A0B, 0x141619, 0x262A2F, 0x3A4047, 0x1A1D21,
      0xD0D4D9, 0x8C939C, 0x4E555E, 0x090A0B, 0x6E9ED6, 0x8AB1DE, 0x4E89CD, 0x1E2F45, 0xE4E8EE,
      0x3DDC84, 0xFFC21A, 0xFF4D4D, 0x4DB8FF, 0x262A2F, 0x3A4047,
      0x6E9ED6, 0x5DB8A6, 0x6F8F63, 0xC79A7E, 0xA9C29B, 0xB58DB0, 0xC9C59B, 0x93BEDD, 0xB4B9C0},
     {0x16191C, 0x262A2F, 0x0D0E10, 0x4E555E, 0x000000, 0xD0D4D9, 0x262A2F}},
    {"Green terminal", "Terminal vert",
     "Le vert phosphore des anciens terminaux, sur du noir.",
     kIndustrialFamily, true,
     {0x050A06, 0x08100A, 0x0C1810, 0x102014, 0x030604, 0x0B150D, 0x1A3321, 0x2A5234, 0x0F1D12,
      0xB6F2C4, 0x7FBF8E, 0x3F6B4A, 0x030604, 0x45D6F0, 0x66DDF3, 0x1FCEED, 0x12402A, 0xD9FFE3,
      0x4CFF7E, 0xFFC940, 0xFF6060, 0x8FA8FF, 0x1A3321, 0x2A5234,
      0x6EE7FF, 0x4CFFA5, 0x5E9E6E, 0xE3F58A, 0xB4FF9E, 0x9EFFC8, 0xD0FFB0, 0x7AFFD1, 0xB6F2C4},
     {0x0C1810, 0x1A3321, 0x030604, 0x3F6B4A, 0x000000, 0xB6F2C4, 0x1A3321}},
    {"Amber", "Ambre",
     "L'ambre des terminaux monochromes, chaud et reposant.",
     kIndustrialFamily, true,
     {0x0A0703, 0x110C05, 0x1A1308, 0x22190A, 0x070502, 0x150F06, 0x3A2A12, 0x5C4420, 0x1C150A,
      0xF5DDB8, 0xC4A57A, 0x6E5A3C, 0x070502, 0x4FD1C5, 0x6BD8CE, 0x33C4B7, 0x4A3514, 0xFFF1DC,
      0x7FE08A, 0xFFC44D, 0xFF6B5B, 0x6FC3FF, 0x3A2A12, 0x5C4420,
      0xFFB347, 0xFFD27F, 0xA08660, 0xE8C98F, 0xFFCF70, 0xFF9F45, 0xFFE0A8, 0xF7B976, 0xF5DDB8},
     {0x1A1308, 0x3A2A12, 0x070502, 0x6E5A3C, 0x000000, 0xF5DDB8, 0x3A2A12}},
};

const More* moreOf(std::string_view key) {
    for (const auto& m : kMore)
        if (key == m.key) return &m;
    return nullptr;
}

Theme build(const More& m) {
    Theme t = m.dark ? Theme::dark() : Theme::light();
    t.name = m.key;
    t.color = paletteOf(m.c);
    surfaces(t, m.s[0], m.s[1], m.s[2], m.s[3], m.s[4], m.s[5], m.s[6]);
    if (!m.dark) {
        // L'ombre et le survol d'un clair : SON texte, un peu, pas un noir pris ailleurs.
        t.brand.cardShadow = t.color.text.withAlpha(26);
        t.brand.hover      = t.color.text.withAlpha(10);
        t.brand.focusRing  = t.color.accent.withAlpha(150);
    }
    return t;
}

std::vector<Theme>& userStore() {
    static std::vector<Theme> s;
    return s;
}

void sortUsers() {
    auto& s = userStore();
    std::stable_sort(s.begin(), s.end(), [](const Theme& a, const Theme& b) { return Theme::fold(a.name) < Theme::fold(b.name); });
}

const Theme* userOf(std::string_view name) {
    const auto want = Theme::fold(name);
    if (want.empty()) return nullptr;
    for (const auto& u : userStore())
        if (Theme::fold(u.name) == want) return &u;
    return nullptr;
}

} // namespace

const std::vector<Theme::Info>& Theme::catalog() {
    static const std::vector<Info> k = [] {
        std::vector<Info> v{
            {"Dark", "Sombre", "Le graphite bleut\xC3\xA9, sombre.", kDarkFamily},
            {"Light", "Clair", "Le clair, le d\xC3\xA9" "faut.", kLightFamily},
            {"High contrast", "Contraste \xC3\xA9lev\xC3\xA9", "Blanc sur noir, 7:1 partout, le texte plus grand.", kHighContrastFamily},
            {"Night", "Nuit", "Bleu nuit profond, accent cyan : les salles sombres.", kDarkFamily},
            {"Graphite", "Graphite", "Sombre et chaud, sans reflet bleu.", kDarkFamily},
            {"Solarized", "Solaris\xC3\xA9", "La palette Solarized, en sombre.", kColorFamily},
            {"Paper", "Papier", "Clair et chaud, moins \xC3\xA9" "blouissant.", kLightFamily},
            {"Slate", "Ardoise", "Clair et froid, accent indigo.", kLightFamily},
            {"High contrast light", "Contraste \xC3\xA9lev\xC3\xA9 clair", "Noir sur blanc : l'atelier en plein jour.", kHighContrastFamily},
        };
        for (const auto& m : kMore) v.push_back({m.key, m.label, m.description, m.family});
        return v;
    }();
    return k;
}

const std::vector<std::string>& Theme::familyLabels() {
    static const std::vector<std::string> k{kDarkFamily, kLightFamily, kColorFamily, kHighContrastFamily, kIndustrialFamily};
    return k;
}

std::string Theme::familyOf(std::string_view name) {
    const auto want = fold(name);
    for (const auto& f : familyLabels())
        if (fold(f) == want) return f;
    // Au singulier, et les mots d'un fichier ecrit a la main.
    if (want == "sombre" || want == "dark") return kDarkFamily;
    if (want == "clair" || want == "light") return kLightFamily;
    if (want == "colore" || want == "couleur" || want == "couleurs") return kColorFamily;
    if (want == "contraste" || want == "highcontrast") return kHighContrastFamily;
    if (want == "industriel" || want == "industrie") return kIndustrialFamily;
    return {};
}

bool Theme::isHighContrast() const noexcept {
    return family == kHighContrastFamily || name.rfind("High contrast", 0) == 0;
}

std::string Theme::fold(std::string_view s) {
    // "Contraste eleve" (accentue) -> "contrasteeleve" : pour comparer un nom tape.
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            ++i;
            if (d >= 0xA8 && d <= 0xAB) out += 'e';          // e accentues
            else if (d >= 0x88 && d <= 0x8B) out += 'e';
            else if (d >= 0xA0 && d <= 0xA5) out += 'a';
            else if (d >= 0x80 && d <= 0x85) out += 'a';
            else if (d == 0xA7 || d == 0x87) out += 'c';
            else if (d >= 0xB2 && d <= 0xB6) out += 'o';
            else if (d >= 0x92 && d <= 0x96) out += 'o';
            else if (d >= 0xB9 && d <= 0xBC) out += 'u';
            else if (d >= 0x99 && d <= 0x9C) out += 'u';
            else if (d >= 0xAC && d <= 0xAF) out += 'i';
            else if (d >= 0x8C && d <= 0x8F) out += 'i';
            continue;
        }
        if (c == ' ' || c == '_' || c == '-' || c == '\t') continue;
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

std::string Theme::keyOf(std::string_view name) {
    const auto want = fold(name);
    if (want.empty()) return {};
    for (const auto& i : catalog())
        if (fold(i.key) == want || fold(i.label) == want) return i.key;
    if (const auto* u = userOf(name)) return u->name;       // lot API 8
    return {};
}

std::string Theme::labelOf(std::string_view name) {
    const auto key = keyOf(name);
    for (const auto& i : catalog())
        if (key == i.key) return i.label;
    if (!key.empty()) return key;                            // un theme de l'utilisateur : son nom
    return std::string(name);
}

bool Theme::isBuiltIn(std::string_view name) {
    const auto want = fold(name);
    if (want.empty()) return false;
    for (const auto& i : catalog())
        if (fold(i.key) == want || fold(i.label) == want) return true;
    return false;
}

Theme Theme::byName(std::string_view name) {
    const auto key = keyOf(name);
    if (!key.empty() && !isBuiltIn(key))
        if (const auto* u = userOf(key)) return *u;          // lot API 8 : tel qu'il a ete lu
    Theme t;
    if (key == "Dark") t = dark();
    else if (key == "High contrast") t = highContrast();
    else if (key == "Night") t = night();
    else if (key == "Graphite") t = graphite();
    else if (key == "Solarized") t = solarized();
    else if (key == "Paper") t = paper();
    else if (key == "Slate") t = slate();
    else if (key == "High contrast light") t = highContrastLight();
    else if (const auto* m = moreOf(key)) t = build(*m);
    else t = light();
    // Ce que la galerie en dit, et sa base : lui-meme (un theme cree a partir
    // de lui partira de lui).
    for (const auto& i : catalog())
        if (t.name == i.key) {
            t.family = i.family;
            t.description = i.description;
            break;
        }
    t.base = t.name;
    t.user = false;
    return t;
}

std::vector<Theme::Entry> Theme::all() {
    std::vector<Entry> out;
    out.reserve(catalog().size() + userStore().size());
    for (const auto& i : catalog()) out.push_back({i.key, i.label, i.description, i.family, false});
    for (const auto& u : userStore()) out.push_back({u.name, u.name, u.description, u.family, true});
    return out;
}

const std::vector<Theme>& Theme::userThemes() { return userStore(); }

void Theme::setUserThemes(std::vector<Theme> themes) {
    auto& s = userStore();
    s.clear();
    for (auto& t : themes) {
        if (fold(t.name).empty() || isBuiltIn(t.name) || userOf(t.name)) continue;   // pas deux fois le meme nom
        t.user = true;
        s.push_back(std::move(t));
    }
    sortUsers();
}

void Theme::putUserTheme(Theme theme) {
    if (fold(theme.name).empty() || isBuiltIn(theme.name)) return;
    theme.user = true;
    auto& s = userStore();
    const auto want = fold(theme.name);
    for (auto& u : s)
        if (fold(u.name) == want) {
            u = std::move(theme);
            sortUsers();
            return;
        }
    s.push_back(std::move(theme));
    sortUsers();
}

bool Theme::removeUserTheme(std::string_view name) {
    auto& s = userStore();
    const auto want = fold(name);
    const auto it = std::find_if(s.begin(), s.end(), [&](const Theme& u) { return fold(u.name) == want; });
    if (it == s.end()) return false;
    s.erase(it);
    return true;
}

// ------------------------------------------------------------------ outils --
namespace {
float channel(std::uint8_t v) {
    const float c = static_cast<float>(v) / 255.f;
    return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
float luminance(gfx::Color c) {
    return 0.2126f * channel(c.r) + 0.7152f * channel(c.g) + 0.0722f * channel(c.b);
}
std::uint8_t mix(std::uint8_t a, std::uint8_t b, float t) {
    return static_cast<std::uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - a) * t);
}
} // namespace

bool Theme::isDark() const noexcept { return luminance(color.panelBg) < 0.4f; }

gfx::Color Theme::tone(Tone t, gfx::Color fallback) const noexcept {
    switch (t) {
        case Tone::None:    return fallback;
        case Tone::Accent:  return color.accent;
        case Tone::Info:    return color.info;
        case Tone::Ok:      return color.ok;
        case Tone::Warning: return color.warning;
        case Tone::Error:   return color.error;
        case Tone::Muted:   return color.textMuted;
        case Tone::Family0: case Tone::Family1: case Tone::Family2:
        case Tone::Family3: case Tone::Family4: case Tone::Family5:
            return brand.family[static_cast<int>(t) - static_cast<int>(Tone::Family0)];
        case Tone::Input:   return brand.scopeIn;
        case Tone::Output:  return brand.scopeOut;
        case Tone::InOut:   return brand.scopeInOut;
    }
    return fallback;
}

gfx::Color Theme::coverage(float fraction) const noexcept {
    return tone(coverageTone(fraction), color.ok);
}

gfx::Color Theme::onSurface(gfx::Color c) const noexcept {
    // Vers le texte du theme, jusqu'a obtenir 4,5:1 sur le fond des panneaux -
    // le seuil WCAG AA du texte courant. Par pas de 10 % : assez fin pour ne
    // pas trop s'eloigner de la teinte, assez gros pour converger en dix pas.
    const auto contrast = [&](gfx::Color x) {
        const float a = luminance(x) + 0.05f, b = luminance(color.panelBg) + 0.05f;
        return a > b ? a / b : b / a;
    };
    gfx::Color out = c;
    for (int step = 0; step < 10 && contrast(out) < 4.5f; ++step) {
        const float t = 0.1f * static_cast<float>(step + 1);
        out = {mix(c.r, color.text.r, t), mix(c.g, color.text.g, t),
               mix(c.b, color.text.b, t), c.a};
    }
    return out;
}

} // namespace ui
