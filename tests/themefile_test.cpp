// tests/themefile_test.cpp - lot API 8 : un theme dans un fichier (.xpgtheme),
// ses contrastes, les themes de l'utilisateur.
//
//   themefile_test
//
//  Ecrire puis relire a l'identique (un theme de l'utilisateur, et chacun des
//  themes integres) ; un fichier ecrit a la main (BOM, CRLF, commentaires, cles
//  sans accents ou en C++) ; une couleur absente (celle de la base) ; un
//  fichier abime, refuse avec sa ligne ; une cle inconnue, ignoree et dite ; un
//  theme qui ne passe pas un contraste, puis "Corriger les contrastes" (la
//  teinte gardee, le fond eloigne quand le texte n'y suffit pas) ; deriver de
//  l'accent ; le dossier des themes de l'utilisateur (enregistrer, relire,
//  renommer, importer - deux fois -, exporter, supprimer).
#include "../src/ui/Theme.hpp"
#include "../src/ui/ThemeFile.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool same(gfx::Color a, gfx::Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// Toutes les couleurs, une a une ; "" : identiques, sinon la premiere qui differe.
std::string diff(const ui::Theme& a, const ui::Theme& b) {
    for (const auto& k : ui::themeColorKeys())
        if (!same(*ui::themeColor(a, k.key), *ui::themeColor(b, k.key)))
            return std::string(k.key) + " " + ui::themeColorText(*ui::themeColor(a, k.key), true) + " / "
                 + ui::themeColorText(*ui::themeColor(b, k.key), true);
    return {};
}

double hueGap(double a, double b) {
    const double d = std::fabs(a - b);
    return std::min(d, 360.0 - d);
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// Un chemin UTF-8 (ce que rend UserThemes::pathOf).
std::filesystem::path p8(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}

} // namespace

int main() {
    // ---- les cles ----------------------------------------------------------------
    const auto& keys = ui::themeColorKeys();
    {
        std::set<std::string> seen;
        bool resolved = true;
        ui::Theme t = ui::Theme::byName("Dark");
        for (const auto& k : keys) {
            seen.insert(ui::Theme::fold(k.key));
            resolved = resolved && ui::themeColor(t, k.key) != nullptr;
        }
        check(keys.size() == 53 && seen.size() == keys.size() && resolved, "53 couleurs, des cles uniques, chacune trouvee");
        check(ui::themeColorGroups().size() == 7, "sept groupes (Fonds, Textes, Accent et selection, Etats, Bordures, Code, Familles et portees)");
        check(ui::themeColor(t, "panelBg") == &t.color.panelBg && ui::themeColor(t, "Fond.Panneau") == &t.color.panelBg
                  && ui::themeColor(t, "code.mot-cle") == &t.color.syntaxKeyword && ui::themeColor(t, "famille.es") == &t.brand.family[3]
                  && ui::themeColor(t, "zzz") == nullptr,
              "une cle du fichier, sans casse, ou le nom du champ C++ ; une inconnue : rien");
        gfx::Color c{};
        check(ui::parseThemeColor("#1c1F24", c) && same(c, gfx::Color{0x1C, 0x1F, 0x24, 255}) && ui::parseThemeColor("A0B1C2", c)
                  && !ui::parseThemeColor("#12345", c) && !ui::parseThemeColor("#1122334", c) && !ui::parseThemeColor("#11223344", c)
                  && ui::parseThemeColor("#11223344", c, true) && c.a == 0x44 && !ui::parseThemeColor("#GG0000", c),
              "#RRGGBB (le # facultatif), #RRGGBBAA seulement quand la transparence est permise");
        check(ui::themeColorText({0x1C, 0x1F, 0x24, 255}) == "#1C1F24" && ui::themeColorText({1, 2, 3, 90}, true) == "#0102035A",
              "themeColorText : #RRGGBB, #RRGGBBAA quand il y a de la transparence");
        bool hsl = true;
        for (int r = 0; r < 256; r += 17)
            for (int g = 0; g < 256; g += 15)
                for (int b = 0; b < 256; b += 51) {
                    const gfx::Color x{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255};
                    hsl = hsl && same(ui::fromHsl(ui::toHsl(x)), x);
                }
        check(hsl, "toHsl puis fromHsl rendent la couleur, a l'octet pres");
        check(ui::contrastText(4.5) == "4,5:1" && ui::contrastText(12.44) == "12,4:1", "contrastText : 4,5:1 (la virgule)");
    }

    // ---- ecrire puis relire a l'identique -----------------------------------------
    {
        ui::Theme t = ui::Theme::byName("Nord");
        t.name = "Usine bleue";
        t.family = "Industriels";
        t.description = "Le bleu de l'atelier B, pour l'\xC3\xA9quipe de nuit.";
        t.user = true;
        t.color.accent = gfx::Color{0x12, 0x34, 0x56, 255};
        t.brand.hover = gfx::Color{255, 255, 255, 33};
        const auto text = ui::writeThemeText(t);
        check(contains(text, "nom = Usine bleue\n") && contains(text, "famille = Industriels\n") && contains(text, "base = Nord\n")
                  && contains(text, "accent = #123456\n") && contains(text, "survol = #FFFFFF21\n") && contains(text, "\n# Fonds\n"),
              "le fichier : nom, famille, base, une ligne par couleur, les groupes en commentaires");
        const auto back = ui::readThemeText(text);
        check(back.ok, "relu : " + back.error);
        check(back.ok && diff(t, back.theme).empty(), "relu a l'identique, couleur par couleur " + diff(t, back.theme));
        check(back.theme.name == t.name && back.theme.family == t.family && back.theme.base == "Nord" && back.theme.description == t.description
                  && back.theme.user && back.missing == 0 && back.notes.empty(),
              "le nom, la famille, la base, la description ; aucune couleur absente, aucune remarque");
        check(ui::writeThemeText(back.theme) == text, "reecrit : le meme texte, a l'octet pres");

        // L'auteur (tranche 2 : l'editeur le demande) : ecrit s'il y en a un, relu.
        ui::Theme signedTheme = t;
        signedTheme.author = "Karim, maintenance ligne 2";
        const auto signedText = ui::writeThemeText(signedTheme);
        const auto signedBack = ui::readThemeText(signedText);
        check(contains(signedText, "auteur = Karim, maintenance ligne 2\n") && !contains(text, "auteur =") && signedBack.ok
                  && signedBack.theme.author == signedTheme.author && signedBack.notes.empty(),
              "l'auteur : une ligne \"auteur = ...\" quand il y en a un, relue sans remarque");

        bool allSame = true;
        std::string which;
        for (const auto& info : ui::Theme::catalog()) {
            ui::Theme b = ui::Theme::byName(info.key);
            b.name = std::string("Copie de ") + info.label;
            const auto r = ui::readThemeText(ui::writeThemeText(b));
            const bool ok = r.ok && diff(b, r.theme).empty() && r.theme.font.ui.v == b.font.ui.v
                         && r.theme.metric.rowHeight == b.metric.rowHeight && r.theme.family == b.family;
            if (!ok && which.empty()) which = std::string(info.key) + " " + r.error + diff(b, r.theme);
            allSame = allSame && ok;
        }
        check(allSame, "chacun des " + std::to_string(ui::Theme::catalog().size()) + " themes integres : ecrit, relu a l'identique (couleurs, tailles, famille) " + which);
        const auto hc = ui::readThemeText(ui::writeThemeText(ui::Theme::byName("High contrast")));
        check(hc.ok && hc.theme.font.ui.v == 22 && hc.theme.metric.radius == 0.f && hc.theme.isHighContrast(),
              "Contraste eleve : ses grandes polices et ses coins carres suivent (base = Contraste eleve)");
    }

    // ---- ecrit a la main ----------------------------------------------------------
    {
        const std::string text =
            "\xEF\xBB\xBF# un theme ecrit dans le Bloc-notes\r\n"
            "\r\n"
            "Nom =   Salle B  \r\n"
            "BASE = Sombre\r\n"
            "  fond.PANNEAU = #202020\r\n"
            "Fond.Fen\xC3\xAAtre=#101010\r\n"
            "textMuted = A0A0A0\r\n"
            "; pas un commentaire, mais une cle inconnue = 1\r\n"
            "couleur.du.futur = #123456";
        const auto r = ui::readThemeText(text);
        check(r.ok && r.theme.name == "Salle B", "BOM, CRLF, commentaires, espaces, casse : lu (" + r.error + ")");
        const auto dark = ui::Theme::byName("Dark");
        check(r.ok && same(r.theme.color.panelBg, gfx::Color{0x20, 0x20, 0x20, 255}) && same(r.theme.color.windowBg, gfx::Color{0x10, 0x10, 0x10, 255})
                  && same(r.theme.color.textMuted, gfx::Color{0xA0, 0xA0, 0xA0, 255}),
              "les couleurs donnees (cle accentuee, cle C++, sans #)");
        check(r.ok && same(r.theme.color.text, dark.color.text) && same(r.theme.color.syntaxKeyword, dark.color.syntaxKeyword)
                  && same(r.theme.brand.card, dark.brand.card) && r.missing == 50,
              "une couleur absente : celle de la base (50 prises au Sombre)");
        check(r.ok && r.notes.size() == 2 && contains(r.notes[0], "ligne 8") && contains(r.notes[1], "ligne 9") && contains(r.notes[1], "couleur.du.futur"),
              "deux cles inconnues : ignorees, et dites avec leur ligne");
        check(r.ok && r.theme.family == "Sombres" && r.theme.base == "Dark" && r.theme.user, "sans famille : celle de la base ; un theme de l'utilisateur");
        const auto guess = ui::readThemeText("nom = Devine\nfond.panneau = #FAFAFA\n");
        check(guess.ok && guess.theme.base == "Light" && !guess.theme.isDark() && guess.notes.size() == 1,
              "sans base : devinee du fond des panneaux (clair), et dite");
        const auto nordBase = ui::readThemeText("nom = Presque Nord\nbase = nord\ntexte = #FFFFFF\n");
        check(nordBase.ok && same(nordBase.theme.color.panelBg, ui::Theme::byName("Nord").color.panelBg) && nordBase.theme.family == "Color\xC3\xA9s",
              "base = un theme integre (Nord) : ses couleurs pour les absentes, sa famille");
    }

    // ---- un fichier abime -----------------------------------------------------------
    {
        const auto noEq = ui::readThemeText("nom = X\nbase = sombre\nfond.panneau #202020\n");
        check(!noEq.ok && noEq.line == 3 && contains(noEq.error, "ligne 3") && contains(noEq.error, "fond.panneau #202020"),
              "une ligne sans = : refuse, ligne 3 (" + noEq.error + ")");
        const auto badColor = ui::readThemeText("nom = X\n\n# commentaire\ntexte = #12345\n");
        check(!badColor.ok && badColor.line == 4 && contains(badColor.error, "#RRGGBB"), "une couleur mal ecrite : refuse, ligne 4 (" + badColor.error + ")");
        const auto alpha = ui::readThemeText("nom = X\ntexte = #11223344\nfocus = #11223344\n");
        check(!alpha.ok && alpha.line == 2, "de la transparence sur le texte : refuse, ligne 2 ; permise sur le focus");
        const auto noName = ui::readThemeText("base = sombre\ntexte = #FFFFFF\n");
        check(!noName.ok && noName.line == 0 && contains(noName.error, "nom"), "pas de nom : refuse (" + noName.error + ")");
        const auto badBase = ui::readThemeText("nom = X\nbase = violette\n");
        check(!badBase.ok && badBase.line == 2 && contains(badBase.error, "base"), "une base inconnue : refuse, ligne 2");
        const auto future = ui::readThemeText("format = 2\nnom = X\n");
        check(!future.ok && future.line == 1, "un format plus recent : refuse, ligne 1");
        const auto longName = ui::readThemeText("nom = " + std::string(61, 'a') + "\n");
        check(!longName.ok && longName.line == 1, "un nom de plus de 60 caracteres : refuse");
        const auto unknownFamily = ui::readThemeText("nom = X\nfamille = Pastels\n");
        check(unknownFamily.ok && unknownFamily.theme.family == "Clairs" && unknownFamily.notes.size() == 2,
              "une famille inconnue : pas un refus, rangee avec sa base, et dite");
        check(!ui::readThemeFile("/chemin/qui/n/existe/pas.xpgtheme").ok, "un fichier introuvable : refuse");
    }

    // ---- les contrastes, puis "Corriger les contrastes" ----------------------------------
    {
        ui::Theme t = ui::Theme::byName("Dark");
        t.name = "Trop sombre";
        t.user = true;
        t.family = "Sombres";
        const auto mutedBefore = gfx::Color{0x50, 0x55, 0x60, 255};
        const auto warnBefore = gfx::Color{0x80, 0x60, 0x00, 255};
        t.color.textMuted = mutedBefore;
        t.color.warning = warnBefore;
        const auto panel = t.color.panelBg;
        const auto fails = ui::contrastFailures(t);
        std::set<std::string> labels;
        for (const auto& f : fails) labels.insert(f.label);
        check(labels.count("texte secondaire") && labels.count("alerte") && labels.count("secondaire sur les cartes"),
              "un secondaire et une alerte trop sombres : les regles qui ne passent pas le disent (" + std::to_string(fails.size()) + ")");
        check(!fails.empty() && fails.front().ratio < fails.front().need && fails.front().need == 4.5, "avec leur contraste et le seuil (4,5)");
        const auto changed = ui::fixContrasts(t);
        check(ui::contrastFailures(t).empty(), "corriges : toutes les regles passent");
        check(std::find(changed.begin(), changed.end(), "texte.secondaire") != changed.end()
                  && std::find(changed.begin(), changed.end(), "etat.alerte") != changed.end() && changed.size() == 2,
              "seules les deux couleurs fautives ont change");
        check(same(t.color.panelBg, panel) && same(t.color.text, ui::Theme::byName("Dark").color.text), "le fond et le texte n'ont pas bouge");
        check(hueGap(ui::toHsl(t.color.warning).h, ui::toHsl(warnBefore).h) < 2.0 && hueGap(ui::toHsl(t.color.textMuted).h, ui::toHsl(mutedBefore).h) < 3.0
                  && ui::toHsl(t.color.warning).l > ui::toHsl(warnBefore).l,
              "leur teinte est gardee ; l'alerte est seulement plus claire (" + ui::themeColorText(t.color.warning) + ")");

        // Un fond gris moyen : aucun texte n'y tient 7:1 - c'est le fond qui s'eloigne.
        ui::Theme grey = ui::Theme::byName("Light");
        grey.color.panelBg = gfx::Color{0x80, 0x80, 0x80, 255};
        check(!ui::contrastFailures(grey).empty(), "un panneau gris moyen (#808080) : le texte n'y tient pas 7:1");
        const auto changed2 = ui::fixContrasts(grey);
        check(ui::contrastFailures(grey).empty() && std::find(changed2.begin(), changed2.end(), "fond.panneau") != changed2.end()
                  && ui::toHsl(grey.color.panelBg).l > 0.5,
              "corrige : le panneau s'eclaircit jusqu'a ce que le texte y suffise (" + ui::themeColorText(grey.color.panelBg) + ")");

        // Contraste eleve : 7:1 partout.
        ui::Theme strict = ui::Theme::byName("Dark");
        strict.family = "Contraste \xC3\xA9lev\xC3\xA9";
        bool seven = false;
        for (const auto& c : ui::checkContrasts(strict))
            if (c.label == "texte secondaire") seven = c.need == 7.0;
        check(strict.isHighContrast() && seven, "un theme de la famille Contraste eleve : le secondaire doit tenir 7:1");
        (void)ui::fixContrasts(strict);
        check(ui::contrastFailures(strict).empty(), "et corrige, il les tient");
        check(ui::saturationProblems(ui::Theme::byName("Dark")).empty(), "saturationProblems : rien a dire du Sombre");
        ui::Theme loud = ui::Theme::byName("Dark");
        loud.color.panelBg = gfx::Color{0x00, 0x20, 0xC0, 255};
        check(!ui::saturationProblems(loud).empty(), "un panneau bleu vif : la regle de la coque desaturee le dit");
    }

    // ---- deriver de l'accent ------------------------------------------------------------
    {
        const gfx::Color accents[] = {{0xE0, 0x45, 0x7B, 255}, {0x00, 0xA8, 0x6B, 255}, {0x7A, 0x5C, 0xFA, 255},
                                      {0xFF, 0xB0, 0x00, 255}, {0x20, 0x60, 0xA0, 255}, {0x80, 0x80, 0x80, 255}};
        bool allPass = true, huesKept = true, sides = true, shells = true;
        std::string which;
        for (const auto a : accents)
            for (const bool dark : {true, false}) {
                ui::Theme from = ui::Theme::byName(dark ? "Dark" : "Light");
                from.name = "Derive";
                const auto t = ui::deriveFromAccent(from, a, dark);
                const auto fails = ui::contrastFailures(t);
                if (!fails.empty() && which.empty()) which = ui::themeColorText(a) + (dark ? " sombre : " : " clair : ") + fails.front().label;
                allPass = allPass && fails.empty();
                if (ui::toHsl(a).s > 0.2) huesKept = huesKept && hueGap(ui::toHsl(t.color.accent).h, ui::toHsl(a).h) < 4.0;
                sides = sides && t.isDark() == dark && t.name == "Derive";
                shells = shells && ui::saturationProblems(t).empty();
            }
        check(allPass, "deriver de six accents, en sombre et en clair : toutes les regles passent " + which);
        check(huesKept, "l'accent garde sa teinte");
        check(sides && shells, "sombre quand on le demande, clair sinon ; la coque reste desaturee ; le nom reste");
    }

    // ---- les themes de l'utilisateur, sur le disque -----------------------------------------
    {
        namespace fs = std::filesystem;
        const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path dir = fs::temp_directory_path() / ("xpg_themefile_test_" + stamp);
        fs::create_directories(dir);
        ui::UserThemes::setFolder((dir / "themes").string());
        check(ui::UserThemes::load().empty() && ui::Theme::userThemes().empty(), "un dossier vide : aucun theme de l'utilisateur");

        ui::Theme mine = ui::Theme::byName("Graphite");
        mine.name = "Usine: bleue?";
        mine.family = "Industriels";
        mine.color.accent = gfx::Color{0x33, 0x66, 0x99, 255};
        std::string why;
        const bool saved = ui::UserThemes::save(mine, &why);
        check(saved, "enregistrer \"Usine: bleue?\" " + why);
        const std::string path = ui::UserThemes::pathOf("usine: BLEUE?");
        check(!path.empty() && fs::exists(p8(path)) && p8(path).filename().string() == "Usine_ bleue_.xpgtheme",
              "un fichier par theme, au nom sur partout (" + p8(path).filename().string() + ")");
        check(ui::Theme::byName("usine: bleue?").user && same(ui::Theme::byName("Usine: bleue?").color.accent, mine.color.accent)
                  && ui::Theme::keyOf("USINE: BLEUE?") == "Usine: bleue?" && ui::Theme::labelOf("usine: bleue?") == "Usine: bleue?",
              "byName, keyOf, labelOf le trouvent, sans souci de la casse");
        const auto all = ui::Theme::all();
        check(all.size() == ui::Theme::catalog().size() + 1 && all.back().user && all.back().family == "Industriels",
              "all() : les integres, puis celui de l'utilisateur");
        why.clear();
        const bool builtInName = ui::UserThemes::save([] { auto t = ui::Theme::byName("Dark"); t.name = "Nuit"; return t; }(), &why);
        check(!builtInName && !why.empty(), "un theme ne prend pas le nom d'un theme integre (" + why + ")");

        // Relu au demarrage.
        ui::Theme::setUserThemes({});
        const auto problems = ui::UserThemes::load();
        check(problems.empty() && ui::Theme::userThemes().size() == 1 && ui::Theme::userThemes().front().name == "Usine: bleue?"
                  && diff(ui::Theme::userThemes().front(), ui::Theme::byName("Usine: bleue?")).empty(),
              "relu du dossier au demarrage");
        {
            std::ofstream bad(dir / "themes" / "abime.xpgtheme", std::ios::binary);
            bad << "nom = Abime\ntexte = rouge\n";
        }
        const auto problems2 = ui::UserThemes::load();
        check(problems2.size() == 1 && contains(problems2.front(), "abime.xpgtheme") && contains(problems2.front(), "ligne 2")
                  && ui::Theme::userThemes().size() == 1,
              "un fichier abime dans le dossier : dit, avec sa ligne, et les autres chargent quand meme");
        fs::remove(dir / "themes" / "abime.xpgtheme");

        // Renommer.
        why.clear();
        const bool renamed = ui::UserThemes::rename("Usine: bleue?", "Atelier B", &why);
        check(renamed, "renommer en \"Atelier B\" " + why);
        check(!fs::exists(p8(path)) && fs::exists(dir / "themes" / "Atelier B.xpgtheme") && ui::Theme::keyOf("atelier b") == "Atelier B"
                  && ui::Theme::keyOf("Usine: bleue?").empty(),
              "le fichier suit le nom ; l'ancien nom n'existe plus");
        check(!ui::UserThemes::rename("Nord", "Mon Nord", &why), "un theme integre ne se renomme pas");

        // Importer, deux fois ; un nom deja pris.
        const fs::path exported = dir / "Nuit export.xpgtheme";
        check(ui::UserThemes::exportTheme("nuit", exported.string(), &why) && contains(slurp(exported), "nom = Nuit\n"),
              "exporter Nuit (un theme integre) : sous son libelle");
        const auto imp = ui::UserThemes::importFile(exported.string());
        check(imp.ok && imp.name == "Nuit (2)" && imp.renamedFrom == "Nuit" && ui::Theme::byName("Nuit (2)").user
                  && diff(ui::Theme::byName("Nuit (2)"), ui::Theme::byName("Night")).empty(),
              "l'importer : \"Nuit\" est pris, il devient \"Nuit (2)\", aux couleurs de Nuit");
        const auto again = ui::UserThemes::importFile(exported.string());
        check(again.ok && again.name == "Nuit (2)" && again.renamedFrom.empty() && ui::Theme::userThemes().size() == 2,
              "l'importer une seconde fois : rien de plus, c'est le meme");
        {
            std::ofstream other(dir / "autre.xpgtheme", std::ios::binary);
            other << "nom = Nuit (2)\nbase = claire\n";
        }
        const auto third = ui::UserThemes::importFile((dir / "autre.xpgtheme").string());
        check(third.ok && third.name == "Nuit (3)" && third.missing == 53, "un autre theme du meme nom : \"Nuit (3)\", pas \"Nuit (2) (2)\"");
        const auto broken = ui::UserThemes::importFile((dir / "absent.xpgtheme").string());
        check(!broken.ok && !broken.error.empty(), "importer un fichier qui n'existe pas : refuse, et dit");
        check(ui::freeThemeName("Sombre") == "Sombre (2)" && ui::freeThemeName("Atelier B", "atelier b") == "Atelier B" && ui::freeThemeName("  ") == "Mon th\xC3\xA8me",
              "freeThemeName : un nom libre, le sien quand on se renomme, \"Mon theme\" s'il est vide");

        // Supprimer.
        const std::string gone = ui::UserThemes::pathOf("Nuit (3)");
        check(ui::UserThemes::remove("Nuit (3)", &why) && !fs::exists(p8(gone)) && ui::Theme::keyOf("Nuit (3)").empty()
                  && ui::Theme::byName("Nuit (3)").name == "Light",
              "supprimer : le fichier retire, le nom oublie");
        check(!ui::UserThemes::remove("Nord", &why), "un theme integre ne se supprime pas");
        ui::Theme::setUserThemes({});
        ui::UserThemes::setFolder({});
        std::error_code ec;
        fs::remove_all(dir, ec);   // le dossier de ce test, et rien d'autre
    }

    std::printf(failures ? "ECHEC : %d echec(s)\n" : "themefile_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
