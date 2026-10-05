// =============================================================================
//  ui/ThemeFile.hpp - un theme dans un fichier, ses contrastes, ceux de
//  l'utilisateur (lot API 8)
// -----------------------------------------------------------------------------
//  LE FICHIER .xpgtheme. Du texte, qu'un automaticien ouvre dans le Bloc-notes,
//  corrige et envoie a un collegue :
//
//      # commentaire
//      format = 1
//      nom = Usine bleue
//      famille = Sombres                 Sombres, Clairs, Colores, Contraste
//                                        eleve ou Industriels
//      base = sombre                     sombre, claire, ou un theme integre
//                                        ("Nord", "Contraste eleve")
//      description = ...
//      fond.panneau = #1C1F24            une ligne par couleur ; #RRGGBBAA pour
//      texte = #E6E9EF                   les trois qui en ont (focus, survol,
//      ...                               ombre.carte)
//
//  Une couleur absente : celle de la base. Une cle inconnue : ignoree, et dite
//  (le fichier d'une version plus recente se lit quand meme). Une ligne
//  abimee - pas de "=", une couleur mal ecrite, pas de nom, une base inconnue -
//  refuse tout le fichier, en disant la ligne.
//
//  LES CONTRASTES. Les regles de l'application, les memes que themecatalog_test :
//  le texte a 7:1 sur les fonds, le secondaire, la selection et les etats a
//  4,5:1 (7:1 pour un theme a contraste eleve), le bouton principal lisible...
//  Un theme qui ne les tient pas est accepte ; la galerie dit lesquels, et
//  fixContrasts() les corrige en ne touchant que la LUMINOSITE des couleurs
//  fautives : leur teinte et leur saturation restent.
//
//  LES THEMES DE L'UTILISATEUR. Un fichier par theme, dans le dossier de ses
//  reglages (App : a cote de settings.txt, "themes/") ; relus au demarrage et
//  mis dans Theme::userThemes() - la galerie, l'accueil, Reglages et la
//  commande de script `theme "Nom"` les voient comme les autres.
// =============================================================================
#pragma once

#include "Theme.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

// ------------------------------------------------------------ les couleurs ---
// Les 53 couleurs d'un theme, dans l'ordre de l'editeur : par groupe (Fonds,
// Textes, Accent et selection, Etats, Bordures, Code, Familles et portees).
struct ThemeColorKey {
    const char* key;      // "fond.panneau" : ce qu'ecrit le fichier (ASCII)
    const char* label;    // "Panneaux" : ce que montre l'editeur (UTF-8)
    const char* group;    // "Fonds"
    const char* hint;     // ou elle sert, en une phrase (l'infobulle)
    bool        alpha;    // #RRGGBBAA permis (focus, survol, ombre.carte)
};
[[nodiscard]] const std::vector<ThemeColorKey>& themeColorKeys();
[[nodiscard]] const std::vector<std::string>&   themeColorGroups();
// Par la cle du fichier ("fond.panneau"), sans souci de la casse ni des
// accents, ou par le nom du champ C++ ("panelBg"). nullptr : inconnue.
[[nodiscard]] gfx::Color*       themeColor(Theme& t, std::string_view key);
[[nodiscard]] const gfx::Color* themeColor(const Theme& t, std::string_view key);
[[nodiscard]] const ThemeColorKey* themeColorKey(std::string_view key);

// "#RRGGBB" (ou "RRGGBB", ou "#RRGGBBAA" si `alpha`) -> la couleur.
[[nodiscard]] bool        parseThemeColor(std::string_view text, gfx::Color& out, bool alpha = false);
[[nodiscard]] std::string themeColorText(gfx::Color c, bool alpha = false);   // "#1C1F24"

// Teinte, saturation, luminosite (HSL) : ce que regle l'editeur, et ce que
// garde la correction des contrastes (elle ne bouge que `l`).
struct Hsl { double h{0}, s{0}, l{0}; };      // h en degres [0, 360[, s et l dans [0, 1]
[[nodiscard]] Hsl        toHsl(gfx::Color c);
[[nodiscard]] gfx::Color fromHsl(const Hsl& hsl, std::uint8_t alpha = 255);

// ----------------------------------------------------------- les contrastes ---
[[nodiscard]] double      contrastRatio(gfx::Color a, gfx::Color b);   // WCAG 2.1, 1 a 21
[[nodiscard]] std::string contrastText(double ratio);                  // "4,5:1" (le seuil : "4,5")

struct ContrastCheck {
    std::string label;      // "texte secondaire"
    std::string fg, bg;     // les cles des deux couleurs ("texte.secondaire", "fond.panneau")
    double      ratio{0};
    double      need{0};
    bool        pass{false};
};
// Chaque regle, sur ce theme (sa famille dit si c'est 4,5 ou 7).
[[nodiscard]] std::vector<ContrastCheck> checkContrasts(const Theme& t);
[[nodiscard]] std::vector<ContrastCheck> contrastFailures(const Theme& t);
// LA COQUE DESATUREE, LES ETATS FRANCS : aucun fond, trait ou texte ne depasse
// une chroma de 80 (sur 255), et ok, alerte et erreur sont plus satures que
// toute la coque. Vide : la regle tient ; sinon, ce qui la casse.
[[nodiscard]] std::vector<std::string> saturationProblems(const Theme& t);
// Corrige ce qui ne passe pas : la couleur du premier plan d'abord (sa
// luminosite seule), le fond si le premier plan n'y suffit pas. Rend les cles
// changees ; les regles qui ne passent toujours pas restent dans
// contrastFailures().
std::vector<std::string> fixContrasts(Theme& t);

// DERIVER DE L'ACCENT. Une couleur, et le reste suit : des fonds et des traits
// neutres a peine teintes de l'accent, le texte, la selection ; les etats et le
// code de la base (le sombre ou le clair), puis fixContrasts(). Le resultat
// tient toutes les regles ; son accent garde sa teinte. Les tailles, les
// familles de blocs et les portees restent celles de `from`.
[[nodiscard]] Theme deriveFromAccent(const Theme& from, gfx::Color accent, bool dark);

// ------------------------------------------------------------- le fichier ---
struct ThemeRead {
    bool                     ok{false};
    Theme                    theme;       // ok : le theme lu (user = true)
    int                      line{0};     // !ok : la ligne fautive (0 : le fichier entier)
    std::string              error;       // !ok : pourquoi, en une phrase
    std::vector<std::string> notes;       // ok : les lignes ignorees, une par ligne
    int                      missing{0};  // ok : les couleurs prises a la base
};
[[nodiscard]] std::string writeThemeText(const Theme& t);
[[nodiscard]] ThemeRead   readThemeText(std::string_view text);
[[nodiscard]] ThemeRead   readThemeFile(const std::string& path);         // chemin UTF-8
bool writeThemeFile(const Theme& t, const std::string& path, std::string* error = nullptr);

// Un nom qu'aucun theme ne porte encore (integre ou de l'utilisateur) : le nom
// lui-meme, sinon "Nom (2)", "Nom (3)"... `except` : un theme qui peut garder
// son nom (on le renomme). Vide : "Mon theme".
[[nodiscard]] std::string freeThemeName(std::string_view wanted, std::string_view except = {});
// Un nom de theme acceptable : non vide, 60 caracteres au plus, sans retour a
// la ligne. "" : bon ; sinon, pourquoi.
[[nodiscard]] std::string themeNameProblem(std::string_view name);

// ------------------------------------------------- les themes de l'utilisateur ---
class UserThemes {
public:
    // Le dossier (UTF-8), cree au besoin. Vide : rien n'est lu ni ecrit.
    static void               setFolder(std::string folder);
    [[nodiscard]] static const std::string& folder();
    // Relit le dossier et remplace Theme::userThemes(). Rend ce qui n'a pas pu
    // etre lu ("bleu.xpgtheme, ligne 4 : ..."), un par fichier.
    static std::vector<std::string> load();
    // Ecrit le theme (son fichier s'il en a un, sinon un nouveau) et le met
    // dans Theme::userThemes(). Faux : l'erreur dit pourquoi.
    static bool save(const Theme& t, std::string* error = nullptr);
    static bool remove(std::string_view name, std::string* error = nullptr);
    static bool rename(std::string_view from, const std::string& to, std::string* error = nullptr);
    [[nodiscard]] static std::string pathOf(std::string_view name);     // "" : pas de fichier

    // IMPORTER : lire un fichier, le renommer si son nom est pris, l'enregistrer
    // dans le dossier. `name` : le nom qu'il porte ; `renamedFrom` : celui du
    // fichier, s'il a fallu en changer.
    struct Imported {
        bool                     ok{false};
        std::string              name, renamedFrom;
        int                      line{0};
        std::string              error;
        std::vector<std::string> notes;
        int                      missing{0};
    };
    static Imported importFile(const std::string& path);
    // EXPORTER : n'importe quel theme (integre ou non), par son nom.
    static bool exportTheme(std::string_view name, const std::string& path, std::string* error = nullptr);
};

} // namespace ui
