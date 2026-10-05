// tests/themecatalog_test.cpp - lot API 6 : les neuf themes ; lot API 8 : les
// quarante-trois, ranges par famille.
//
//   themecatalog_test
//
//  Chaque theme du catalogue : le texte a 7:1 sur les fonds, le texte
//  secondaire et la selection a 4,5:1, les couleurs d'etat lisibles sur les
//  panneaux (7:1 pour la famille Contraste eleve), l'accent, le bouton
//  principal, l'infobulle, le code ; la coque desaturee et les etats francs
//  (la regle de l'application) ; les regles de ThemeFile (celles que la
//  galerie montre) toutes tenues. Puis les noms : la cle (ce que retiennent les
//  preferences et les scripts), le libelle, uniques, et un nom tape sans souci
//  de la casse, des accents ni des espaces ; les familles.
#include "../src/ui/Theme.hpp"
#include "../src/ui/ThemeFile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

double channel(std::uint8_t v) {
    const double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double luminance(gfx::Color c) { return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); }
double contrast(gfx::Color a, gfx::Color b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
int chroma(gfx::Color c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }

std::string ratio(double r) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f:1", r);
    return buf;
}

} // namespace

int main() {
    const auto& all = ui::Theme::catalog();
    check(all.size() >= 40, "au moins quarante themes au catalogue (" + std::to_string(all.size()) + ")");
    check(std::string(all[0].key) == "Dark" && std::string(all[1].key) == "Light" && std::string(all[2].key) == "High contrast",
          "les trois d'avant d'abord, avec leurs cles d'avant");
    {
        static const char* const kLot6[] = {"Dark", "Light", "High contrast", "Night", "Graphite", "Solarized", "Paper", "Slate", "High contrast light"};
        bool same = all.size() >= 9;
        for (std::size_t i = 0; same && i < 9; ++i) same = std::string(all[i].key) == kLot6[i];
        check(same, "les neuf du lot API 6 gardent leur place et leur cle");
    }

    const auto& families = ui::Theme::familyLabels();
    check(families.size() == 5, "cinq familles");
    std::map<std::string, int> perFamily;
    std::set<std::string> keys, labels;
    int hcCount = 0;

    for (const auto& info : all) {
        const auto t = ui::Theme::byName(info.key);
        const std::string n = info.label;
        const bool hc = t.isHighContrast();
        hcCount += hc;
        const auto& c = t.color;
        check(t.name == info.key, n + " : byName(cle) rend ce theme");
        check(std::find(families.begin(), families.end(), std::string(info.family)) != families.end() && t.family == info.family,
              n + " : dans une des cinq familles (" + info.family + ")");
        ++perFamily[info.family];
        check(std::string(info.description).size() > 10 && t.description == info.description, n + " : une description");
        check(keys.insert(ui::Theme::fold(info.key)).second, n + " : cle unique (" + info.key + ")");
        check(labels.insert(ui::Theme::fold(info.label)).second, n + " : libelle unique");
        check(ui::Theme::byName(info.label).name == info.key, n + " : byName(libelle) rend ce theme");

        const double textWin = contrast(c.text, c.windowBg), textPanel = contrast(c.text, c.panelBg), textInput = contrast(c.text, c.inputBg);
        check(textWin >= 7.0 && textPanel >= 7.0 && textInput >= 7.0,
              n + " : le texte a 7:1 (fenetre " + ratio(textWin) + ", panneau " + ratio(textPanel) + ", champ " + ratio(textInput) + ")");
        const double header = contrast(c.text, c.headerBg), card = contrast(c.text, t.brand.card);
        check(header >= (hc ? 7.0 : 4.5) && card >= (hc ? 7.0 : 4.5), n + " : le texte sur les en-tetes " + ratio(header) + " et les cartes " + ratio(card));
        const double muted = contrast(c.textMuted, c.panelBg), mutedCard = contrast(c.textMuted, t.brand.card);
        check(muted >= (hc ? 7.0 : 4.5) && mutedCard >= (hc ? 7.0 : 4.5), n + " : le texte secondaire " + ratio(muted) + " (carte " + ratio(mutedCard) + ")");
        const double sel = contrast(c.selectionText, c.selectionBg);
        check(sel >= (hc ? 7.0 : 4.5), n + " : la selection " + ratio(sel));
        const double okR = contrast(c.ok, c.panelBg), warnR = contrast(c.warning, c.panelBg), errR = contrast(c.error, c.panelBg),
                     infoR = contrast(c.info, c.panelBg);
        const double need = hc ? 7.0 : 4.5;
        check(okR >= need && warnR >= need && errR >= need && infoR >= need,
              n + " : les etats sur les panneaux (ok " + ratio(okR) + ", alerte " + ratio(warnR) + ", erreur " + ratio(errR) + ", info " + ratio(infoR) + ")");
        const double accent = contrast(c.accent, c.panelBg);
        check(accent >= 4.5, n + " : l'accent (liens, bouton principal) sur les panneaux " + ratio(accent));
        const double button = contrast(c.textInverted, c.accent);
        check(button >= 4.5, n + " : le texte du bouton principal " + ratio(button));
        const double tip = contrast(t.brand.tooltipText, t.brand.tooltipBg);
        check(tip >= 7.0, n + " : l'infobulle " + ratio(tip));
        const double code = contrast(c.text, t.brand.codeBg);
        check(code >= 7.0, n + " : le code sur son fond " + ratio(code));
        double worstSyntax = 21.0;
        for (const auto s : {c.syntaxKeyword, c.syntaxType, c.syntaxComment, c.syntaxString, c.syntaxNumber, c.syntaxPreprocessor,
                             c.syntaxFunction, c.syntaxConstant, c.syntaxOperator})
            worstSyntax = std::min(worstSyntax, contrast(s, t.brand.codeBg));
        check(worstSyntax >= (hc ? 4.5 : 3.0), n + " : la coloration du code lisible (la pire " + ratio(worstSyntax) + ")");
        check(contrast(t.brand.led, t.brand.card) >= 3.0, n + " : une LED allumee se voit sur une carte");
        double worstFamily = 21.0;
        for (const auto f : t.brand.family) worstFamily = std::min(worstFamily, contrast(t.onSurface(f), c.panelBg));
        for (const auto f : {t.brand.scopeIn, t.brand.scopeOut, t.brand.scopeInOut}) worstFamily = std::min(worstFamily, contrast(t.onSurface(f), c.panelBg));
        // onSurface() s'arrete des 4,5 (calcule en float) : un rien de marge ici.
        check(worstFamily >= 4.45, n + " : les familles et les portees, ramenees par onSurface() " + ratio(worstFamily));
        check(t.isDark() == (luminance(c.windowBg) < 0.2), n + " : isDark() dit vrai");

        // LA REGLE DE L'APPLICATION : la coque desaturee, les etats francs.
        const gfx::Color shell[] = {c.windowBg, c.panelBg, c.headerBg, c.railBg, c.inputBg, c.rowAltBg, c.border, c.borderStrong, c.gridLine,
                                    c.text, c.textMuted, c.textDisabled, c.textInverted, c.scrollbar, c.scrollbarHover,
                                    t.brand.card, t.brand.cardBorder, t.brand.codeBg, t.brand.codeGutter, t.brand.tooltipBg,
                                    t.brand.tooltipText, t.brand.ledOff};
        int shellMax = 0;
        for (const auto s : shell) shellMax = std::max(shellMax, chroma(s));
        const int stateMin = std::min({chroma(c.ok), chroma(c.warning), chroma(c.error)});
        check(shellMax <= 80 && stateMin > shellMax,
              n + " : seuls les etats sont satures (coque " + std::to_string(shellMax) + ", etats " + std::to_string(stateMin) + ")");

        // Les regles de ThemeFile, celles que la galerie et l'editeur montrent.
        const auto fails = ui::contrastFailures(t);
        std::string which;
        for (const auto& f : fails) which += " [" + f.label + " " + ratio(f.ratio) + "]";
        check(fails.empty(), n + " : les " + std::to_string(ui::checkContrasts(t).size()) + " regles de la galerie tenues" + which);
        check(ui::saturationProblems(t).empty(), n + " : saturationProblems() ne dit rien");
    }

    // ---- les familles -----------------------------------------------------------
    for (const auto& f : families) check(perFamily[f] >= 4, "la famille " + f + " : " + std::to_string(perFamily[f]) + " themes");
    check(hcCount >= 4, "au moins quatre themes a contraste eleve (" + std::to_string(hcCount) + ")");
    check(ui::Theme::byName("Dark").family == "Sombres" && ui::Theme::byName("Light").family == "Clairs"
              && ui::Theme::byName("Solarized").family == "Color\xC3\xA9s" && ui::Theme::byName("High contrast").family == "Contraste \xC3\xA9lev\xC3\xA9"
              && ui::Theme::byName("Control Expert").family == "Industriels",
          "Sombre, Clair, Solarise, Contraste eleve, Control Expert : leurs familles");
    check(ui::Theme::familyOf("clairs") == "Clairs" && ui::Theme::familyOf("colores") == "Color\xC3\xA9s"
              && ui::Theme::familyOf("CONTRASTE ELEVE") == "Contraste \xC3\xA9lev\xC3\xA9" && ui::Theme::familyOf("industriel") == "Industriels"
              && ui::Theme::familyOf("zzz").empty(),
          "familyOf : sans casse ni accents, au singulier aussi, ou rien");
    check(ui::Theme::all().size() == all.size() && !ui::Theme::all().front().user, "all() : les integres (pas encore de theme de l'utilisateur)");

    // ---- les noms -----------------------------------------------------------
    check(ui::Theme::byName("nuit").name == "Night", "\"nuit\" : Nuit");
    check(ui::Theme::byName("Solarise").name == "Solarized" && ui::Theme::byName("solaris\xC3\xA9").name == "Solarized", "\"Solarise\", accent ou pas");
    check(ui::Theme::byName("high contrast").name == "High contrast" && ui::Theme::byName("HIGHCONTRAST").name == "High contrast",
          "\"high contrast\" : la casse et les espaces ne comptent pas");
    check(ui::Theme::byName("Contraste \xC3\xA9lev\xC3\xA9 clair").name == "High contrast light", "par le libelle : Contraste eleve clair");
    check(ui::Theme::byName("Dark").name == "Dark" && ui::Theme::byName("Sombre").name == "Dark", "Dark, ou Sombre");
    check(ui::Theme::byName("un theme de 2030").name == "Light", "un nom inconnu : le clair, le defaut");
    check(ui::Theme::keyOf("Ardoise") == "Slate" && ui::Theme::keyOf("papier") == "Paper" && ui::Theme::keyOf("zzz").empty(), "keyOf : la cle, ou rien");
    check(ui::Theme::labelOf("Paper") == "Papier" && ui::Theme::labelOf("Night") == "Nuit" && ui::Theme::labelOf("zzz") == "zzz", "labelOf : le libelle, ou le nom tel quel");
    check(ui::Theme::byName("tokyo nuit").name == "Tokyo Night" && ui::Theme::byName("Ros\xC3\xA9 Pine").name == "Rose Pine"
              && ui::Theme::byName("salle de controle").name == "Control room" && ui::Theme::byName("Plein soleil").name == "Full sun"
              && ui::Theme::byName("GITHUB SOMBRE").name == "GitHub dark",
          "lot API 8 : Tokyo nuit, Rose Pine, Salle de controle, Plein soleil, GitHub sombre - par le libelle, sans casse ni accents");
    check(ui::Theme::labelOf("Color blind") == "Daltonisme" && ui::Theme::labelOf("Oceanic") == "Oc\xC3\xA9" "anique", "lot API 8 : les libelles en francais");
    check(ui::Theme::byName("Nord").base == "Nord" && !ui::Theme::byName("Nord").user, "un theme integre est sa propre base, et pas un theme de l'utilisateur");

    std::printf(failures ? "ECHEC : %d echec(s)\n" : "themecatalog_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
