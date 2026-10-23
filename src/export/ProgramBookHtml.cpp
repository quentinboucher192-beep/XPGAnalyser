// =============================================================================
//  export/ProgramBookHtml.cpp - 1.12.30 : le programme en page web (voir ProgramBook.hpp)
// -----------------------------------------------------------------------------
//  Le meme modele que le PDF et le classeur (Book) : la fiche du projet, la
//  configuration materielle, les taches et l'ordre d'execution, chaque section
//  (son rang, sa condition, ce qu'elle ecrit, lit, appelle, son code colore et
//  numerote ; un ladder, un FBD, un GRAFCET : leur lecture en texte), les unites
//  et leurs parametres, les blocs DFB et leur code, les types derives, les
//  variables globales. Une page a la fois, le menu a gauche ; l'archive .xapi du
//  projet dedans (export/HtmlPage.hpp).
// =============================================================================
#include "ProgramBook.hpp"

#include "HtmlPage.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/GraphListing.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace exporter::book {

namespace {

namespace h = exporter::html;

std::string thousands(std::size_t n) {
    std::string s = std::to_string(n), out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) out += ' ';
        out += s[i];
    }
    return out;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        std::string line = text.substr(from, (nl == std::string::npos ? text.size() : nl) - from);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

std::string joined(const std::vector<std::string>& items, std::size_t max = 40) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i == max) { out += ", \xE2\x80\xA6 (" + std::to_string(items.size() - max) + " autres)"; break; }
        out += (i ? ", " : "") + items[i];
    }
    return out;
}

std::string paramTable(const std::vector<Param>& params) {
    std::vector<std::vector<h::Cell>> rows;
    for (const auto& p : params)
        rows.push_back({{p.direction, {}, {}}, {p.name, "mono", {}}, {p.type, "mono", {}}, {p.receives, "mono", {}}, {p.comment, {}, {}}});
    return h::table({"Sens", "Nom", "Type", "Re\xC3\xA7oit", "Commentaire"}, rows, true, "Aucun param\xC3\xA8tre.");
}

std::string sectionId(const Section& s) { return "s" + std::to_string(s.rank) + "-" + h::anchor(s.name); }

} // namespace

std::string toHtml(const Book& b, const Options& o, const domain::Project& p, const HtmlExtras& x) {
    (void)o;
    h::Page page;
    page.kind = project::archive::Kind::Api;
    page.application = b.app;
    page.title = b.project;
    page.subtitle = (b.version.empty() ? std::string{} : "version " + b.version + " \xC2\xB7 ") + (b.cpu.empty() ? std::string{} : b.cpu + " \xC2\xB7 ")
                  + "export\xC3\xA9" "e le " + b.exportedAt;
    page.archive = x.archive;
    page.archiveName = x.archiveName;
    page.first = "fiche";

    // ---- la fiche -------------------------------------------------------------
    {
        std::size_t modules = 0;
        for (const auto& r : p.hardware.racks) modules += r.modules.size();
        h::Section s;
        s.id = "fiche";
        s.title = "Fiche du projet";
        s.html = h::tiles({{thousands(b.totalSections), "sections"}, {thousands(b.totalLines), "lignes de code"},
                           {thousands(b.programUnits), "unit\xC3\xA9s de programme"}, {thousands(b.dfbs.size()), "blocs DFB"},
                           {thousands(b.derivedTypes), "types d\xC3\xA9riv\xC3\xA9s"}, {thousands(b.variables.size()), "variables globales"},
                           {thousands(modules), "modules"}});
        std::vector<std::pair<std::string, std::string>> rows = {{"Projet", b.project}, {"Version", b.version}, {"Automate", b.cpu},
                                                                 {"Famille", p.hardware.family}, {"Produit", b.product}, {"Source", b.source}};
        for (const auto& r : x.card) rows.push_back(r);
        rows.push_back({"Export\xC3\xA9" "e le", b.exportedAt});
        rows.push_back({"Par", b.app});
        s.html += h::card(rows);
        page.sections.push_back(std::move(s));
    }
    // ---- la configuration -------------------------------------------------------
    {
        h::Section s;
        s.id = "configuration";
        s.title = "Configuration mat\xC3\xA9rielle";
        s.subtitle = p.hardware.inferred ? "d\xC3\xA9" "duite de l'automate (pas de .XHW)" : std::string{};
        std::vector<std::vector<h::Cell>> rows;
        for (const auto& r : p.hardware.racks)
            for (const auto& m : r.modules)
                rows.push_back({{std::to_string(r.number), "num", {}}, {std::to_string(m.slot), "num", {}}, {m.reference, "mono", {}},
                                {m.description, {}, {}}, {std::string(domain::toString(m.kind)), {}, {}}, {m.firmware, "mono", {}},
                                {m.inputPoints ? std::to_string(m.inputPoints) : std::string{}, "num", {}},
                                {m.outputPoints ? std::to_string(m.outputPoints) : std::string{}, "num", {}}});
        s.html = h::card({{"Automate", b.cpu}, {"Famille", p.hardware.family}, {"Ressource", p.hardware.resourceName},
                          {"Bus", p.hardware.busName}, {"Alimentation", p.hardware.powerSupply}});
        s.html += "<h3 class=\"sub\">Racks et modules</h3>\n";
        s.html += h::table({"Rack", "Emplacement", "R\xC3\xA9" "f\xC3\xA9rence", "Description", "Genre", "Version", "Entr\xC3\xA9" "es", "Sorties"}, rows,
                           true, "Aucun rack d\xC3\xA9" "clar\xC3\xA9 (la configuration vient du .XHW).");
        page.sections.push_back(std::move(s));
    }
    // ---- les taches et l'ordre d'execution ------------------------------------
    {
        h::Section s;
        s.id = "taches";
        s.title = "T\xC3\xA2" "ches et ordre d'ex\xC3\xA9" "cution";
        std::vector<std::vector<h::Cell>> tasks;
        for (const auto& t : b.tasks)
            tasks.push_back({{t.name, "mono", {}}, {t.kind, {}, {}}, {t.period ? std::to_string(t.period) + " ms" : std::string{}, "num", {}},
                             {t.watchdog ? std::to_string(t.watchdog) + " ms" : std::string{}, "num", {}},
                             {std::to_string(t.sections), "num", {}}, {thousands(t.lines), "num", {}}});
        s.html = h::table({"T\xC3\xA2" "che", "Genre", "P\xC3\xA9riode", "Chien de garde", "Sections", "Lignes"}, tasks, false);
        s.html += "<h3 class=\"sub\">Dans l'ordre o\xC3\xB9 l'automate les ex\xC3\xA9" "cute</h3>\n";
        std::vector<std::vector<h::Cell>> order;
        for (const auto& sec : b.sections)
            order.push_back({{std::to_string(sec.rank), "num", {}},
                             {{}, {}, "<a href=\"#" + h::escape(sectionId(sec)) + "\">" + h::escape(sec.name) + "</a>"},
                             {sec.inUnit ? sec.owner : std::string{}, {}, {}}, {sec.task, {}, {}},
                             {sec.condition.empty() ? std::string("chaque cycle") : sec.condition, "mono", {}},
                             {std::to_string(sec.lineCount), "num", {}}, {sec.comment, {}, {}}});
        s.html += h::table({"Rang", "Section", "Unit\xC3\xA9", "T\xC3\xA2" "che", "Condition", "Lignes", "Ce qu'elle fait"}, order);
        page.sections.push_back(std::move(s));
    }
    // ---- les sections -------------------------------------------------------------
    h::NavGroup secNav{"Sections (" + std::to_string(b.sections.size()) + ")", {}};
    for (const auto& sec : b.sections) {
        h::Section s;
        s.id = sectionId(sec);
        s.title = sec.name;
        s.subtitle = "rang " + std::to_string(sec.rank) + " / " + std::to_string(b.totalSections) + " \xC2\xB7 " + sec.task
                   + (sec.inUnit ? " \xC2\xB7 unit\xC3\xA9 " + sec.owner : std::string{});
        std::vector<std::pair<std::string, std::string>> rows = {
            {"Condition", sec.condition.empty() ? std::string("chaque cycle") : sec.condition}, {"Ce qu'elle fait", sec.comment},
            {"\xC3\x89" "crit", joined(sec.writes)}, {"Lit", joined(sec.reads)}, {"Appelle", joined(sec.calls)},
            {"R\xC3\xB4le", roleName(sec.icon)}};
        s.html = h::card(rows);
        std::vector<std::string> lines = sec.lines;
        if (sec.index < p.sections.size() && domain::keepsSourceXml(p.sections[sec.index])) {
            lines = splitLines(project::graphListingText(p.sections[sec.index]));
            s.html += "<p class=\"desc\">Un sch\xC3\xA9ma (LD, FBD ou SFC) gard\xC3\xA9 tel que Control Expert l'a \xC3\xA9" "crit : sa lecture en texte.</p>\n";
        }
        s.html += "<h3 class=\"sub\">Code (" + std::to_string(lines.size()) + " lignes)</h3>\n" + h::code(lines);
        secNav.links.push_back({s.id, sec.name, std::to_string(sec.lineCount)});
        page.sections.push_back(std::move(s));
    }
    // ---- les unites -----------------------------------------------------------------
    h::NavGroup unitNav{"Unit\xC3\xA9s de programme", {}};
    for (const auto& u : b.units) {
        h::Section s;
        s.id = "u-" + h::anchor(u.name);
        s.title = u.name;
        s.subtitle = "unit\xC3\xA9 de programme \xC2\xB7 " + u.task;
        s.html = h::card({{"T\xC3\xA2" "che", u.task}, {"Rang dans la t\xC3\xA2" "che", std::to_string(u.rank)},
                          {"Sections", std::to_string(u.sections)}, {"Lignes", thousands(u.lines)}, {"Variables locales", std::to_string(u.locals)}});
        std::string links;
        for (const auto& sec : b.sections)
            if (sec.inUnit && sec.owner == u.name) links += "<a href=\"#" + h::escape(sectionId(sec)) + "\">" + h::escape(sec.name) + "</a>";
        if (!links.empty()) s.html += "<div class=\"links\">Ses sections : " + links + "</div>\n";
        s.html += "<h3 class=\"sub\">Param\xC3\xA8tres</h3>\n" + paramTable(u.params);
        unitNav.links.push_back({s.id, u.name, {}});
        page.sections.push_back(std::move(s));
    }
    // ---- les DFB ---------------------------------------------------------------------
    h::NavGroup dfbNav{"Blocs DFB", {}};
    for (const auto& d : b.dfbs) {
        h::Section s;
        s.id = "dfb-" + h::anchor(d.name);
        s.title = d.name;
        s.subtitle = "bloc DFB" + (d.version.empty() ? std::string{} : " \xC2\xB7 version " + d.version);
        s.html = h::card({{"Entr\xC3\xA9" "es", std::to_string(d.inputs)}, {"Sorties", std::to_string(d.outputs)},
                          {"Entr\xC3\xA9" "es / sorties", std::to_string(d.inOuts)}, {"Lignes", thousands(d.lines)}});
        s.html += "<h3 class=\"sub\">Param\xC3\xA8tres</h3>\n" + paramTable(d.params);
        for (const auto& body : d.bodies) s.html += "<h3 class=\"sub\">" + h::escape(body.name) + "</h3>\n" + h::code(body.lines);
        dfbNav.links.push_back({s.id, d.name, {}});
        page.sections.push_back(std::move(s));
    }
    // ---- les types derives -----------------------------------------------------------
    {
        h::Section s;
        s.id = "ddt";
        s.title = "Types d\xC3\xA9riv\xC3\xA9s (DDT)";
        s.subtitle = std::to_string(p.derivedTypes.size());
        for (const auto& t : p.derivedTypes) {
            const std::string name(p.strings.text(t.name));
            s.html += "<h3 class=\"sub\" id=\"ddt-" + h::escape(h::anchor(name)) + "\">" + h::escape(name)
                    + (t.version.empty() ? std::string{} : " <small>version " + h::escape(t.version) + "</small>") + "</h3>\n";
            if (const std::string c(p.strings.text(t.comment)); !c.empty()) s.html += "<p class=\"desc\">" + h::escape(c) + "</p>\n";
            std::vector<std::vector<h::Cell>> rows;
            for (const auto f : t.fields) {
                if (f >= p.variables.size()) continue;
                const auto& v = p.variables[f];
                rows.push_back({{std::string(p.strings.text(v.name)), "mono", {}}, {std::string(p.strings.text(v.type.name)), "mono", {}},
                                {std::string(p.strings.text(v.initValue)), "mono", {}}, {std::string(p.strings.text(v.comment)), {}, {}}});
            }
            s.html += h::table({"Membre", "Type", "Valeur initiale", "Commentaire"}, rows, false, "Aucun membre.");
        }
        if (p.derivedTypes.empty()) s.html = "<p class=\"desc\">Aucun type d\xC3\xA9riv\xC3\xA9.</p>\n";
        page.sections.push_back(std::move(s));
    }
    // ---- les variables globales ------------------------------------------------------
    {
        h::Section s;
        s.id = "variables";
        s.title = "Variables globales";
        s.subtitle = thousands(b.variables.size());
        std::vector<std::vector<h::Cell>> rows;
        for (const auto& v : b.variables)
            rows.push_back({{v.name, "mono", {}}, {v.type, "mono", {}}, {v.address, "mono", {}}, {v.initial, "mono", {}}, {v.comment, {}, {}}});
        s.html = h::table({"Nom", "Type", "Adresse", "Valeur initiale", "Commentaire"}, rows);
        page.sections.push_back(std::move(s));
    }

    page.nav.push_back({"Le projet", {{"fiche", "Fiche du projet", {}}, {"configuration", "Configuration", {}},
                                       {"taches", "T\xC3\xA2" "ches et ordre", std::to_string(b.tasks.size())},
                                       {"ddt", "Types d\xC3\xA9riv\xC3\xA9s", std::to_string(p.derivedTypes.size())},
                                       {"variables", "Variables globales", thousands(b.variables.size())}}});
    page.nav.push_back(std::move(secNav));
    page.nav.push_back(std::move(unitNav));
    page.nav.push_back(std::move(dfbNav));
    return h::render(page);
}

} // namespace exporter::book
