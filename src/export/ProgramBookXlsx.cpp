// =============================================================================
//  export/ProgramBookXlsx.cpp - 1.8.0 : le classeur Excel de l'export lisible
// -----------------------------------------------------------------------------
//  Lisez-moi ; Sommaire (une ligne par section, un lien vers son code) ; un
//  onglet par groupe dans l'ordre d'execution (les sections d'une tache qui se
//  suivent, ou une unite de programme) ; DFB ; Variables. Le code : une ligne
//  par cellule, en Consolas, colore (des morceaux de texte riche). Volets
//  figes, filtre sur le sommaire, impression A4 paysage sur une page de large,
//  les titres repetes. Des chaines en ligne (inlineStr) : pas de table commune
//  a tenir, Excel la refait a l'enregistrement.
// =============================================================================
#include "ProgramBook.hpp"

#include "../core/CodeIcons.hpp"
#include "DocKit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace exporter::book {

namespace {

using doc::columnName;
using doc::xmlEscape;

// Les styles (cellXfs) : voir stylesXml().
enum Style : int {
    SDefault = 0, STitle, SSubtitle, SHeader, SBand, SUnitBand, STaskBand, SInfo, SLineNo, SCode, SLink, SCondition, SNumber,
    SHeaderNumber, SBandNumber, SRoleFirst  // + 18
};

struct Sheet {
    std::string         name;
    std::vector<double> widths;          // en caracteres
    int                 frozen{0};       // lignes figees en haut
    int                 printTitle{0};   // la ligne des titres a repeter (0 : aucune)
    std::string         autoFilter;      // "A3:J70"
    std::string         rows;
    std::vector<std::string> links;
    int                 row{0};          // la derniere ecrite
    std::string         cells;           // la ligne en cours
    int                 cellCount{0};

    void begin() {
        ++row;
        cells.clear();
        cellCount = 0;
    }
    void end(double height = 0) {
        rows += "<row r=\"" + std::to_string(row) + "\"";
        if (height > 0) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.1f", height);
            rows += std::string(" ht=\"") + buf + "\" customHeight=\"1\"";
        }
        rows += ">" + cells + "</row>";
    }
    [[nodiscard]] std::string ref(int col) const { return columnName(static_cast<std::size_t>(col)) + std::to_string(row); }
    void str(int col, std::string_view text, int style = SDefault) {
        cells += "<c r=\"" + ref(col) + "\" s=\"" + std::to_string(style) + "\"";
        if (text.empty()) {
            cells += "/>";
            return;
        }
        cells += " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + xmlEscape(text) + "</t></is></c>";
    }
    void num(int col, double value, int style = SNumber) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.15g", value);
        cells += "<c r=\"" + ref(col) + "\" s=\"" + std::to_string(style) + "\"><v>" + buf + "</v></c>";
    }
    void rich(int col, const std::vector<Run>& runs, int style) {
        if (runs.empty()) {
            str(col, {}, style);
            return;
        }
        cells += "<c r=\"" + ref(col) + "\" s=\"" + std::to_string(style) + "\" t=\"inlineStr\"><is>";
        for (const auto& r : runs) {
            const char* color = "FF1F1F1F";
            bool bold = false, italic = false;
            switch (r.kind) {
                case Run::Keyword: color = "FF0000C8"; bold = true; break;
                case Run::Type: color = "FF7B3F00"; break;
                case Run::Comment: color = "FF008000"; italic = true; break;
                case Run::String: color = "FFA31515"; break;
                case Run::Number: color = "FF098658"; break;
                case Run::Address: color = "FF795E26"; break;
                case Run::Function: color = "FF6F42C1"; break;
                case Run::Plain: break;
            }
            cells += "<r><rPr><rFont val=\"Consolas\"/><family val=\"3\"/>";
            if (bold) cells += "<b/>";
            if (italic) cells += "<i/>";
            cells += std::string("<color rgb=\"") + color + "\"/><sz val=\"10\"/></rPr><t xml:space=\"preserve\">" + xmlEscape(r.text) + "</t></r>";
        }
        cells += "</is></c>";
    }
    // Une ligne de bandeau : le style sur toutes les colonnes (le fond plein).
    void band(std::string_view text, int style, int firstCol = 0, std::string_view number = {}) {
        for (int c = 0; c < static_cast<int>(widths.size()); ++c) {
            if (!number.empty() && c == 0) str(c, number, SBandNumber);
            else if (c == firstCol) str(c, text, style);
            else str(c, {}, style);
        }
    }
    void link(int col, std::string_view text, const std::string& target) {
        str(col, text, SLink);
        links.push_back("<hyperlink ref=\"" + ref(col) + "\" location=\"" + xmlEscape(target) + "\" display=\"" + xmlEscape(text) + "\"/>");
    }
};

std::string sheetXml(const Sheet& s, bool selected, const std::string& header, const std::string& footer) {
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                    "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
    x += "<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>";
    x += "<sheetViews><sheetView workbookViewId=\"0\"";
    if (selected) x += " tabSelected=\"1\"";
    x += ">";
    if (s.frozen > 0) {
        const std::string top = "A" + std::to_string(s.frozen + 1);
        x += "<pane ySplit=\"" + std::to_string(s.frozen) + "\" topLeftCell=\"" + top + "\" activePane=\"bottomLeft\" state=\"frozen\"/>"
             "<selection pane=\"bottomLeft\" activeCell=\"" + top + "\" sqref=\"" + top + "\"/>";
    }
    x += "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"15\"/><cols>";
    for (std::size_t i = 0; i < s.widths.size(); ++i) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "<col min=\"%zu\" max=\"%zu\" width=\"%.1f\" customWidth=\"1\"/>", i + 1, i + 1, s.widths[i]);
        x += buf;
    }
    x += "</cols><sheetData>" + s.rows + "</sheetData>";
    if (!s.autoFilter.empty()) x += "<autoFilter ref=\"" + s.autoFilter + "\"/>";
    if (!s.links.empty()) {
        x += "<hyperlinks>";
        for (const auto& l : s.links) x += l;
        x += "</hyperlinks>";
    }
    x += "<pageMargins left=\"0.4\" right=\"0.4\" top=\"0.7\" bottom=\"0.7\" header=\"0.3\" footer=\"0.3\"/>"
         "<pageSetup paperSize=\"9\" orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>"
         "<headerFooter><oddHeader>" + xmlEscape(header) + "</oddHeader><oddFooter>" + xmlEscape(footer) + "</oddFooter></headerFooter>";
    x += "</worksheet>";
    return x;
}

// Une couleur de l'icone, eclaircie pour un fond de case (75 % de blanc).
std::string lightFill(std::uint32_t rgb) {
    const auto mix = [](std::uint32_t c) { return static_cast<unsigned>(c + (255 - c) * 3 / 4); };
    char buf[16];
    std::snprintf(buf, sizeof buf, "FF%02X%02X%02X", mix((rgb >> 16) & 0xFF), mix((rgb >> 8) & 0xFF), mix(rgb & 0xFF));
    return buf;
}

std::string stylesXml() {
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                    "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
    x += "<fonts count=\"10\">"
         "<font><sz val=\"11\"/><color rgb=\"FF1F1F1F\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"                    // 0
         "<font><b/><sz val=\"16\"/><color rgb=\"FF1D3B5C\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"               // 1 titre
         "<font><i/><sz val=\"10\"/><color rgb=\"FF666666\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"               // 2 sous-titre, infos
         "<font><b/><sz val=\"11\"/><color rgb=\"FF1D3B5C\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"               // 3 en-tete
         "<font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"               // 4 bandeaux
         "<font><sz val=\"10\"/><color rgb=\"FF9A9A9A\"/><name val=\"Consolas\"/><family val=\"3\"/></font>"                  // 5 numeros de ligne
         "<font><sz val=\"10\"/><color rgb=\"FF1F1F1F\"/><name val=\"Consolas\"/><family val=\"3\"/></font>"                  // 6 code
         "<font><u/><sz val=\"11\"/><color rgb=\"FF0563C1\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"               // 7 lien
         "<font><sz val=\"11\"/><color rgb=\"FF8A5A00\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"                   // 8 condition
         "<font><sz val=\"10\"/><color rgb=\"FF1F1F1F\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"                   // 9 role
         "</fonts>";
    std::string fills = "<fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
                        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFDFE6EE\"/><bgColor indexed=\"64\"/></patternFill></fill>"   // 2 en-tete
                        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1D3B5C\"/><bgColor indexed=\"64\"/></patternFill></fill>"   // 3 section
                        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF0F6E6E\"/><bgColor indexed=\"64\"/></patternFill></fill>"   // 4 unite
                        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF5A3E8C\"/><bgColor indexed=\"64\"/></patternFill></fill>";  // 5 tache
    for (std::size_t i = 0; i < core::codeicons::kCount; ++i)
        fills += "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"" + lightFill(core::codeicons::info(i).rgb)
               + "\"/><bgColor indexed=\"64\"/></patternFill></fill>";
    x += "<fills count=\"" + std::to_string(6 + core::codeicons::kCount) + "\">" + fills + "</fills>";
    x += "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
         "<border><left/><right/><top/><bottom style=\"thin\"><color rgb=\"FF9FB3C8\"/></bottom><diagonal/></border></borders>";
    x += "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>";
    std::string xfs;
    const auto xf = [&xfs](int font, int fill, int border, const char* align) {
        xfs += "<xf numFmtId=\"0\" fontId=\"" + std::to_string(font) + "\" fillId=\"" + std::to_string(fill) + "\" borderId=\"" + std::to_string(border)
             + "\" xfId=\"0\"" + (font ? " applyFont=\"1\"" : "") + (fill ? " applyFill=\"1\"" : "") + (border ? " applyBorder=\"1\"" : "");
        if (align) xfs += std::string(" applyAlignment=\"1\"><alignment ") + align + "/></xf>";
        else xfs += "/>";
    };
    xf(0, 0, 0, nullptr);                              // 0 SDefault
    xf(1, 0, 0, "vertical=\"center\"");                // 1 STitle
    xf(2, 0, 0, nullptr);                              // 2 SSubtitle
    xf(3, 2, 1, "vertical=\"center\"");                // 3 SHeader
    xf(4, 3, 0, "vertical=\"center\"");                // 4 SBand
    xf(4, 4, 0, "vertical=\"center\"");                // 5 SUnitBand
    xf(4, 5, 0, "vertical=\"center\"");                // 6 STaskBand
    xf(2, 0, 0, nullptr);                              // 7 SInfo
    xf(5, 0, 0, "horizontal=\"right\"");               // 8 SLineNo
    xf(6, 0, 0, nullptr);                              // 9 SCode
    xf(7, 0, 0, nullptr);                              // 10 SLink
    xf(8, 0, 0, nullptr);                              // 11 SCondition
    xf(0, 0, 0, "horizontal=\"right\"");               // 12 SNumber
    xf(3, 2, 1, "horizontal=\"right\" vertical=\"center\"");   // 13 SHeaderNumber
    xf(4, 3, 0, "horizontal=\"center\" vertical=\"center\""); // 14 SBandNumber
    for (std::size_t i = 0; i < core::codeicons::kCount; ++i) xf(9, static_cast<int>(6 + i), 0, nullptr);   // 15.. SRoleFirst
    x += "<cellXfs count=\"" + std::to_string(15 + core::codeicons::kCount) + "\">" + xfs + "</cellXfs>";
    x += "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles><dxfs count=\"0\"/><tableStyles count=\"0\"/>";
    x += "</styleSheet>";
    return x;
}

// Un nom d'onglet permis (31 caracteres, sans []:*?/\), unique.
std::string sheetName(std::string wanted, std::set<std::string>& taken) {
    for (auto& c : wanted)
        if (c == '[' || c == ']' || c == ':' || c == '*' || c == '?' || c == '/' || c == '\\') c = '_';
    const auto cut = [](std::string s, std::size_t n) {
        // Pas au milieu d'un caractere UTF-8.
        if (s.size() <= n) return s;
        std::size_t k = n;
        while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
        return s.substr(0, k);
    };
    wanted = cut(wanted, 31);
    std::string name = wanted;
    for (int i = 2; taken.count(name); ++i) name = cut(wanted, 26) + " (" + std::to_string(i) + ")";
    taken.insert(name);
    return name;
}

std::string quoteSheet(const std::string& name) {
    std::string q = "'";
    for (const char c : name) {
        if (c == '\'') q += "''";
        else q += c;
    }
    return q + "'";
}

std::string joinFirst(const std::vector<std::string>& v, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < v.size() && i < n; ++i) out += (i ? ", " : "") + v[i];
    if (v.size() > n) out += ", ... (" + std::to_string(v.size() - n) + " de plus)";
    return out;
}

std::string sectionBanner(const Section& s, bool roles) {
    std::string t = s.name + "   \xC2\xB7   " + s.owner + "   \xC2\xB7   " + doc::thousands(s.lineCount) + " lignes";
    if (roles && s.icon >= 0) t += "   \xC2\xB7   " + roleName(s.icon);
    t += s.condition.empty() ? std::string("   \xC2\xB7   \xC3\xA0 chaque cycle") : "   \xC2\xB7   si " + s.condition;
    return t;
}

} // namespace

std::vector<std::uint8_t> toXlsx(const Book& b, const Options& o) {
    const std::string header = "&L&8" + b.project + " \xC2\xB7 programme de l'automate&R&8&A";
    const std::string footer = "&L&8Export\xC3\xA9 le " + b.exportedAt + " par " + b.app + "&R&8Page &P / &N";
    std::set<std::string> taken;
    std::vector<Sheet> sheets;

    // ---- les groupes : les sections d'une tache qui se suivent, ou une unite ----
    struct Group { std::string title, name; const Unit* unit{nullptr}; std::vector<std::size_t> sections; };
    std::vector<Group> groups;
    for (const auto& it : b.order) {
        if (it.kind == Item::Task) {
            groups.push_back({});
            groups.back().title = "T\xC3\xA2" "che " + b.tasks[it.index].name;
            groups.back().name = b.tasks[it.index].name;
            continue;
        }
        if (it.kind == Item::Unit) {
            Group g;
            g.unit = &b.units[it.index];
            g.title = "Unit\xC3\xA9 de programme " + g.unit->name;
            g.name = g.unit->name;
            groups.push_back(std::move(g));
            continue;
        }
        const auto& s = b.sections[it.index];
        const bool sameGroup = !groups.empty()
                            && ((groups.back().unit && s.inUnit && groups.back().unit->name == s.owner)
                                || (!groups.back().unit && !s.inUnit && (groups.back().sections.empty() || b.sections[groups.back().sections.back()].owner == s.owner)));
        if (!sameGroup) {
            Group g;
            g.title = s.inUnit ? "Unit\xC3\xA9 de programme " + s.owner : "T\xC3\xA2" "che " + s.owner;
            g.name = s.owner;
            groups.push_back(std::move(g));
        }
        groups.back().sections.push_back(it.index);
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& g) { return g.sections.empty(); }), groups.end());

    std::map<std::size_t, std::pair<std::string, int>> where;    // section -> (onglet, ligne du bandeau)
    std::vector<Sheet> groupSheets;
    for (const auto& g : groups) {
        Sheet sh;
        const auto first = b.sections[g.sections.front()].rank, last = b.sections[g.sections.back()].rank;
        char prefix[32];
        if (first == last) std::snprintf(prefix, sizeof prefix, "%02zu ", first);
        else std::snprintf(prefix, sizeof prefix, "%02zu-%02zu ", first, last);
        sh.name = sheetName(std::string(prefix) + g.name, taken);
        sh.widths = {6, 7, 150};
        sh.frozen = 3;
        sh.printTitle = 3;
        sh.begin();
        sh.str(0, g.title + (g.unit ? std::string{} : first == last ? " : section " + std::to_string(first)
                                                                   : " : sections " + std::to_string(first) + " \xC3\xA0 " + std::to_string(last)),
               STitle);
        sh.end(24);
        sh.begin();
        if (g.unit)
            sh.str(0, "Rang " + std::to_string(g.unit->rank) + " dans " + g.unit->task + " \xC2\xB7 " + std::to_string(g.unit->sections) + " sections \xC2\xB7 "
                          + doc::thousands(g.unit->lines) + " lignes \xC2\xB7 " + std::to_string(g.unit->params.size()) + " param\xC3\xA8tres",
                   SSubtitle);
        else
            sh.str(0, "Dans l'ordre d'ex\xC3\xA9" "cution. Le sommaire (onglet Sommaire) renvoie ici.", SSubtitle);
        sh.end();
        sh.begin();
        sh.str(0, "N\xC2\xB0", SHeaderNumber);
        sh.str(1, "Ligne", SHeaderNumber);
        sh.str(2, "Code", SHeader);
        sh.end(18);
        if (g.unit && o.unitParams && !g.unit->params.empty()) {
            sh.begin();
            sh.str(2, "Param\xC3\xA8tres de l'unit\xC3\xA9 : sens, nom, type, ce qu'il re\xC3\xA7oit (la variable du projet)", SInfo);
            sh.end();
            for (const auto& p : g.unit->params) {
                sh.begin();
                std::string line = p.direction;
                line.resize(std::max<std::size_t>(line.size(), 8), ' ');
                std::string nm = p.name;
                nm.resize(std::max<std::size_t>(nm.size(), 24), ' ');
                std::string ty = p.type;
                ty.resize(std::max<std::size_t>(ty.size(), 26), ' ');
                sh.str(2, line + " " + nm + " " + ty + (p.receives.empty() ? std::string{} : " -> " + p.receives), SCode);
                sh.end();
            }
            sh.begin();
            sh.str(2, "Variables locales : " + std::to_string(g.unit->locals), SInfo);
            sh.end();
            sh.begin();
            sh.end();
        }
        for (const auto si : g.sections) {
            const auto& s = b.sections[si];
            sh.begin();
            where[si] = {sh.name, sh.row};
            sh.band(sectionBanner(s, o.roles), SBand, 2, std::to_string(s.rank));
            sh.end(18);
            if (!s.comment.empty()) {
                sh.begin();
                sh.str(2, "Commentaire : " + s.comment, SInfo);
                sh.end();
            }
            if (o.access) {
                sh.begin();
                sh.str(2, "\xC3\x89" "crit (" + std::to_string(s.writes.size()) + ") : " + (s.writes.empty() ? std::string("\xE2\x80\x94") : joinFirst(s.writes, 40)), SInfo);
                sh.end();
                sh.begin();
                sh.str(2, "Lit (" + std::to_string(s.reads.size()) + ") : " + (s.reads.empty() ? std::string("\xE2\x80\x94") : joinFirst(s.reads, 40)), SInfo);
                sh.end();
                if (!s.calls.empty()) {
                    sh.begin();
                    sh.str(2, "Appelle : " + joinFirst(s.calls, 20), SInfo);
                    sh.end();
                }
            }
            bool inComment = false;
            for (std::size_t i = 0; i < s.lines.size(); ++i) {
                sh.begin();
                if (o.lineNumbers) sh.num(1, static_cast<double>(i + 1), SLineNo);
                sh.rich(2, colorize(s.lines[i], inComment), SCode);
                sh.end();
            }
            sh.begin();
            sh.end();
        }
        groupSheets.push_back(std::move(sh));
    }

    // ---- Lisez-moi ----
    Sheet readme;
    readme.name = sheetName("Lisez-moi", taken);
    readme.widths = {3, 130};
    const auto line = [&readme](std::string_view t, int style = SDefault) {
        readme.begin();
        readme.str(1, t, style);
        readme.end(style == STitle ? 26 : 0);
    };
    line("Programme de l'automate : " + b.project, STitle);
    line(b.cpu + (b.product.empty() ? std::string{} : " \xC2\xB7 " + b.product) + (b.version.empty() ? std::string{} : " \xC2\xB7 version " + b.version), SSubtitle);
    line("Export\xC3\xA9 le " + b.exportedAt + " par " + b.app + (b.source.empty() ? std::string{} : ", depuis " + b.source)
             + (o.scope == Options::Scope::All ? std::string{} : " \xC2\xB7 extrait : " + b.scopeLabel),
         SSubtitle);
    line({});
    const std::string mainTask = b.tasks.empty() ? std::string("MAST") : b.tasks.front().name;
    if (o.guide) {
        line("Comment lire ce classeur", SHeader);
        line("L'automate ex\xC3\xA9" "cute la t\xC3\xA2" "che " + mainTask + " en boucle. \xC3\x80 chaque cycle, il ex\xC3\xA9" "cute les sections dans l'ordre du Sommaire, de 1 \xC3\xA0 "
             + std::to_string(b.totalSections) + ", puis recommence.");
        line("\xC2\xAB si X \xC2\xBB : la section ne s'ex\xC3\xA9" "cute que pendant les cycles o\xC3\xB9 X est vrai.");
        line("Une unit\xC3\xA9 de programme regroupe des sections qui s'ex\xC3\xA9" "cutent en bloc, \xC3\xA0 son rang ; ses param\xC3\xA8tres relient ses noms \xC3\xA0 ceux du projet.");
        line("Le code est du texte structur\xC3\xA9 (ST, CEI 61131-3) : := affecte une valeur, (* ... *) est un commentaire, IF ... THEN ... END_IF une condition.");
        if (o.access) line("\xC3\x89" "crit / Lit : les variables du projet que la section modifie et celles qu'elle consulte. Appelle : les blocs (DFB, temporisations...) qu'elle fait tourner.");
        if (o.roles) line("R\xC3\xB4le : l'ic\xC3\xB4ne choisie pour la section dans " + o.appName + ".");
        line({});
    }
    line("Les onglets", SHeader);
    line("Sommaire : les sections dans l'ordre d'ex\xC3\xA9" "cution ; la colonne Code ouvre la section.");
    for (const auto& g : groupSheets) line(g.name);
    if (o.dfbAppendix && !b.dfbs.empty()) line("DFB : les " + std::to_string(b.dfbs.size()) + " blocs, leurs param\xC3\xA8tres et leur code.");
    if (o.variablesAppendix && !b.variables.empty()) line("Variables : les " + std::to_string(b.variables.size()) + " variables globales (type, adresse, valeur initiale, commentaire).");
    line({});
    line("Imprimer : chaque onglet est r\xC3\xA9gl\xC3\xA9 en A4 paysage, une page de large, les titres r\xC3\xA9p\xC3\xA9t\xC3\xA9s en haut de chaque page.", SInfo);

    // ---- Sommaire ----
    Sheet sum;
    sum.name = sheetName("Sommaire", taken);
    sum.widths = {6, 24, 32, 20, 34, 8, 52, 7, 7, 30};
    sum.frozen = 3;
    sum.printTitle = 3;
    sum.begin();
    sum.str(0, "Programme de l'automate : " + b.project, STitle);
    sum.end(26);
    sum.begin();
    sum.str(0, (o.scope == Options::Scope::All ? "Ordre d'ex\xC3\xA9" "cution de la t\xC3\xA2" "che " + mainTask : b.scopeLabel + ", dans l'ordre d'ex\xC3\xA9" "cution")
                   + " \xC2\xB7 " + b.cpu + (b.product.empty() ? std::string{} : " \xC2\xB7 " + b.product) + " \xC2\xB7 export\xC3\xA9 le " + b.exportedAt,
            SSubtitle);
    sum.end();
    sum.begin();
    const char* heads[] = {"N\xC2\xB0", "R\xC3\xB4le", "Section", "Unit\xC3\xA9 / t\xC3\xA2" "che", "Condition d'activation", "Lignes", "Commentaire",
                           "\xC3\x89" "crit", "Lit", "Code"};
    for (int c = 0; c < 10; ++c) sum.str(c, heads[c], c == 0 || c == 5 || c == 7 || c == 8 ? SHeaderNumber : SHeader);
    sum.end(18);
    const int firstData = sum.row + 1;
    for (const auto& it : b.order) {
        if (it.kind == Item::Task) {
            const auto& t = b.tasks[it.index];
            sum.begin();
            sum.band("T\xC3\xA2" "che " + t.name + " \xC2\xB7 " + t.kind + " \xC2\xB7 " + std::to_string(t.sections) + " sections \xC2\xB7 " + doc::thousands(t.lines) + " lignes",
                     STaskBand);
            sum.end();
            continue;
        }
        if (it.kind == Item::Unit) {
            const auto& u = b.units[it.index];
            sum.begin();
            sum.band("Unit\xC3\xA9 de programme " + u.name + " \xC2\xB7 rang " + std::to_string(u.rank) + " \xC2\xB7 " + std::to_string(u.sections) + " sections \xC2\xB7 "
                         + doc::thousands(u.lines) + " lignes \xC2\xB7 " + std::to_string(u.params.size()) + " param\xC3\xA8tres",
                     SUnitBand);
            sum.end();
            continue;
        }
        const auto& s = b.sections[it.index];
        sum.begin();
        sum.num(0, static_cast<double>(s.rank));
        if (o.roles && s.icon >= 0) sum.str(1, roleName(s.icon), SRoleFirst + s.icon);
        else sum.str(1, {});
        sum.str(2, s.name);
        sum.str(3, s.owner);
        sum.str(4, s.condition, SCondition);
        sum.num(5, static_cast<double>(s.lineCount));
        sum.str(6, s.comment);
        if (o.access) {
            sum.num(7, static_cast<double>(s.writes.size()));
            sum.num(8, static_cast<double>(s.reads.size()));
        }
        if (const auto w = where.find(it.index); w != where.end())
            sum.link(9, "-> voir le code", quoteSheet(w->second.first) + "!A" + std::to_string(w->second.second));
        sum.end();
    }
    sum.autoFilter = "A3:J" + std::to_string(std::max(sum.row, firstData));
    sum.begin();
    sum.end();
    sum.begin();
    sum.str(2, "Total : " + std::to_string(b.sections.size()) + " sections \xC2\xB7 " + doc::thousands(b.scopeLines) + " lignes", SInfo);
    sum.end();

    // ---- DFB ----
    Sheet dfb;
    const bool withDfb = o.dfbAppendix && !b.dfbs.empty();
    if (withDfb) {
        dfb.name = sheetName("DFB", taken);
        dfb.widths = {6, 30, 10, 9, 9, 9, 9, 26, 120};
        dfb.frozen = 3;
        dfb.printTitle = 3;
        dfb.begin();
        dfb.str(0, "Les blocs DFB du projet", STitle);
        dfb.end(26);
        dfb.begin();
        dfb.str(0, "Appel\xC3\xA9s par les sections (colonne \xC2\xAB Appelle \xC2\xBB) ; leur code suit, bloc par bloc.", SSubtitle);
        dfb.end();
        dfb.begin();
        const char* dh[] = {"N\xC2\xB0", "Bloc", "Version", "Entr\xC3\xA9" "es", "Sorties", "E/S", "Lignes", "R\xC3\xB4le", ""};
        for (int c = 0; c < 9; ++c) dfb.str(c, dh[c], c == 0 || (c >= 3 && c <= 6) ? SHeaderNumber : SHeader);
        dfb.end(18);
        for (std::size_t i = 0; i < b.dfbs.size(); ++i) {
            const auto& d = b.dfbs[i];
            dfb.begin();
            dfb.num(0, static_cast<double>(i + 1));
            dfb.str(1, d.name);
            dfb.str(2, d.version);
            dfb.num(3, static_cast<double>(d.inputs));
            dfb.num(4, static_cast<double>(d.outputs));
            dfb.num(5, static_cast<double>(d.inOuts));
            dfb.num(6, static_cast<double>(d.lines));
            if (o.roles && d.icon >= 0) dfb.str(7, roleName(d.icon), SRoleFirst + d.icon);
            dfb.end();
        }
        for (const auto& d : b.dfbs) {
            dfb.begin();
            dfb.end();
            dfb.begin();
            dfb.band(d.name + (d.version.empty() ? std::string{} : "   \xC2\xB7   version " + d.version) + "   \xC2\xB7   " + doc::thousands(d.lines) + " lignes", SBand, 1);
            dfb.end(18);
            for (const auto& p : d.params) {
                dfb.begin();
                dfb.str(1, p.direction + " " + p.name, SInfo);
                dfb.str(8, p.type + (p.comment.empty() ? std::string{} : "   (" + p.comment + ")"), SInfo);
                dfb.end();
            }
            for (const auto& body : d.bodies) {
                dfb.begin();
                dfb.str(1, "Section " + body.name, SHeader);
                dfb.end();
                bool inComment = false;
                for (std::size_t i = 0; i < body.lines.size(); ++i) {
                    dfb.begin();
                    if (o.lineNumbers) dfb.num(0, static_cast<double>(i + 1), SLineNo);
                    dfb.rich(1, colorize(body.lines[i], inComment), SCode);
                    dfb.end();
                }
            }
        }
    }

    // ---- Variables ----
    Sheet vars;
    const bool withVars = o.variablesAppendix && !b.variables.empty();
    if (withVars) {
        vars.name = sheetName("Variables", taken);
        vars.widths = {34, 30, 12, 16, 60};
        vars.frozen = 2;
        vars.printTitle = 2;
        vars.begin();
        vars.str(0, "Les variables globales (" + std::to_string(b.variables.size()) + ")", STitle);
        vars.end(26);
        vars.begin();
        const char* vh[] = {"Nom", "Type", "Adresse", "Valeur initiale", "Commentaire"};
        for (int c = 0; c < 5; ++c) vars.str(c, vh[c], SHeader);
        vars.end(18);
        for (const auto& v : b.variables) {
            vars.begin();
            vars.str(0, v.name);
            vars.str(1, v.type);
            vars.str(2, v.address, SCondition);
            vars.str(3, v.initial);
            vars.str(4, v.comment);
            vars.end();
        }
        vars.autoFilter = "A2:E" + std::to_string(vars.row);
    }

    // ---- le classeur ----
    sheets.push_back(std::move(readme));
    sheets.push_back(std::move(sum));
    for (auto& g : groupSheets) sheets.push_back(std::move(g));
    if (withDfb) sheets.push_back(std::move(dfb));
    if (withVars) sheets.push_back(std::move(vars));

    std::vector<std::pair<std::string, std::string>> files;
    std::string types = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
                        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>";
    std::string wbRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    std::string wbSheets, defined;
    for (std::size_t i = 0; i < sheets.size(); ++i) {
        const auto n = std::to_string(i + 1);
        types += "<Override PartName=\"/xl/worksheets/sheet" + n + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
        wbRels += "<Relationship Id=\"rId" + n + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet" + n + ".xml\"/>";
        wbSheets += "<sheet name=\"" + xmlEscape(sheets[i].name) + "\" sheetId=\"" + n + "\" r:id=\"rId" + n + "\"/>";
        if (!sheets[i].autoFilter.empty()) {
            std::string ref = sheets[i].autoFilter;
            // A3:J70 -> $A$3:$J$70
            const auto colon = ref.find(':');
            const auto abs = [](const std::string& cell) {
                std::size_t k = 0;
                while (k < cell.size() && std::isalpha(static_cast<unsigned char>(cell[k]))) ++k;
                return "$" + cell.substr(0, k) + "$" + cell.substr(k);
            };
            defined += "<definedName name=\"_xlnm._FilterDatabase\" localSheetId=\"" + std::to_string(i) + "\" hidden=\"1\">"
                     + xmlEscape(quoteSheet(sheets[i].name)) + "!" + abs(ref.substr(0, colon)) + ":" + abs(ref.substr(colon + 1)) + "</definedName>";
        }
        if (sheets[i].printTitle > 0) {
            const auto r = std::to_string(sheets[i].printTitle);
            defined += "<definedName name=\"_xlnm.Print_Titles\" localSheetId=\"" + std::to_string(i) + "\">" + xmlEscape(quoteSheet(sheets[i].name)) + "!$" + r + ":$"
                     + r + "</definedName>";
        }
        files.emplace_back("xl/worksheets/sheet" + n + ".xml", sheetXml(sheets[i], i == 1, header, footer));
    }
    types += "</Types>";
    const auto stylesId = std::to_string(sheets.size() + 1);
    wbRels += "<Relationship Id=\"rId" + stylesId + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
              "</Relationships>";
    std::string workbook = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                           "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                           "<bookViews><workbookView activeTab=\"1\"/></bookViews><sheets>" + wbSheets + "</sheets>";
    if (!defined.empty()) workbook += "<definedNames>" + defined + "</definedNames>";
    workbook += "</workbook>";
    const std::string rootRels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                 "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
                                 "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
                                 "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
                                 "</Relationships>";
    const std::string core = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                             "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                             "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                             "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                             "<dc:title>" + xmlEscape("Programme de l'automate : " + b.project) + "</dc:title>"
                             "<dc:creator>" + xmlEscape(b.app) + "</dc:creator></cp:coreProperties>";
    const std::string app = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                            "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>"
                            + xmlEscape(b.app) + "</Application></Properties>";
    files.insert(files.begin(), {"[Content_Types].xml", types});
    files.insert(files.begin() + 1, {"_rels/.rels", rootRels});
    files.emplace_back("xl/workbook.xml", workbook);
    files.emplace_back("xl/_rels/workbook.xml.rels", wbRels);
    files.emplace_back("xl/styles.xml", stylesXml());
    files.emplace_back("docProps/core.xml", core);
    files.emplace_back("docProps/app.xml", app);
    return doc::zip(files, true);
}

} // namespace exporter::book
