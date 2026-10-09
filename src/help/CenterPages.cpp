// =============================================================================
//  help/CenterPages.cpp - 1.11 (chantier T2, tranche 3) : les pages Raccourcis
//  et Notes de version, la partie pure (voir CenterPages.hpp)
// =============================================================================
#include "CenterPages.hpp"

#include <algorithm>

namespace help::center {

// ---- la page Raccourcis --------------------------------------------------------

KeysPage keysPage(std::string_view term, std::optional<keys::Context> only) {
    KeysPage page;
    page.total = keys::search({}).size();   // 1.12.0 : ceux de l'application
    const auto found = keys::search(term);   // un terme vide : tout, dans l'ordre de la table
    for (const auto c : keys::contexts()) {
        if (only && *only != c) continue;
        KeyGroup g;
        g.context = c;
        g.label = std::string(keys::contextLabel(c));
        for (const auto* s : found) {
            if (s->context != c) continue;
            KeyRow r;
            for (const auto& alt : keys::alternatives(s->keys)) r.caps.push_back(keys::keyCaps(alt));
            r.text = std::string(s->text);
            r.since = std::string(s->since);
            r.row = s;
            g.rows.push_back(std::move(r));
        }
        page.shown += g.rows.size();
        if (!g.rows.empty()) page.groups.push_back(std::move(g));
    }
    const bool filtered = !keys::fold(term).empty() || only.has_value();
    if (page.shown == 0)
        page.status = "Aucun raccourci pour \xC2\xAB " + std::string(term) + " \xC2\xBB";
    else if (!filtered)
        page.status = std::to_string(page.total) + " raccourcis";
    else
        page.status = std::to_string(page.shown) + (page.shown > 1 ? " raccourcis sur " : " raccourci sur ")
                      + std::to_string(page.total);
    return page;
}

// ---- la page Notes de version ----------------------------------------------------

std::string notesKey(std::string_view version) {
    const auto* r = notes::release(version);
    return "notes-" + std::string(r != nullptr ? r->version : version);
}

ShowAction showAction(const notes::Note& n) {
    ShowAction a;
    a.topic = std::string(n.topic);
    if (!n.tutorialStep.empty()) {
        std::size_t k = 0;
        for (const char c : n.tutorialStep) {
            if (c < '0' || c > '9') break;
            k = k * 10 + static_cast<std::size_t>(c - '0');
        }
        if (k > 0) {
            a.kind = ShowKind::TutorialStep;
            a.step = k - 1;   // "3" : la 3e etape, l'index 2 (T1 : 0 = la premiere)
            return a;
        }
    }
    if (!n.go.empty()) {
        a.kind = ShowKind::Place;
        a.go = std::string(n.go);
        a.widget = std::string(n.widget);
        return a;
    }
    a.kind = ShowKind::Topic;
    return a;
}

TutorialRequest showRequest(const notes::Note& n) {
    const auto a = showAction(n);
    TutorialRequest r;
    r.topic = a.topic;
    r.step = a.kind == ShowKind::TutorialStep ? a.step : 0;
    r.paused = a.kind == ShowKind::TutorialStep;
    r.returnTopic = notesKey(n.version);
    return r;
}

TutorialRequest tutorialRequest(const notes::Note& n) {
    TutorialRequest r;
    r.topic = std::string(n.topic);
    r.returnTopic = notesKey(n.version);
    return r;
}

NotesPage notesPage(std::string_view version, std::string_view domain) {
    NotesPage page;
    for (const auto& r : notes::releases()) page.versions.emplace_back(r.version);
    const notes::Release* rel = version.empty() ? nullptr : notes::release(version);
    if (rel == nullptr && !notes::releases().empty()) rel = &notes::releases().front();
    if (rel == nullptr) return page;
    page.version = std::string(rel->version);
    page.date = std::string(rel->date);
    page.state = std::string(rel->note);
    page.summary = notes::summary(rel->version);
    for (const auto d : notes::domainsOf(rel->version)) page.domains.emplace_back(d);
    const auto lines = notes::of(rel->version);
    for (const auto& d : page.domains) {
        if (!domain.empty() && d != domain) continue;
        NoteSection s;
        s.domain = d;
        for (const auto* n : lines) {
            if (n->domain != d) continue;
            NoteRow row;
            row.letter = std::string(notes::kindLetter(n->kind));
            row.label = std::string(notes::kindLabel(n->kind));
            row.title = std::string(n->title);
            row.text = std::string(n->text);
            row.news = n->news;
            row.show = showAction(*n);
            row.tutorialTopic = std::string(n->topic);
            row.note = n;
            s.rows.push_back(std::move(row));
        }
        page.rows += s.rows.size();
        if (!s.rows.empty()) page.sections.push_back(std::move(s));
    }
    return page;
}

// ---- la fiche A4 des raccourcis (tranche 8) ---------------------------------------

namespace {
std::string escapeHtml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}
} // namespace

std::string keysSheetHtml(std::string_view version, std::string_view term, std::optional<keys::Context> only) {
    const auto page = keysPage(term, only);
    const std::string v = escapeHtml(version);
    std::string h;
    h += "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\">\n";
    h += "<title>XPGAnalyser " + v + " \xE2\x80\x94 raccourcis clavier</title>\n<style>\n";
    h += "@page { size: A4; margin: 12mm; }\n";
    h += "body { font: 9.5pt/1.35 system-ui, sans-serif; color: #111; margin: 0; }\n";
    h += "h1 { font-size: 14pt; margin: 0 0 2mm; } p.etat { color: #555; margin: 0 0 4mm; }\n";
    h += ".groupes { columns: 2; column-gap: 8mm; }\n";
    h += "section { break-inside: avoid; margin: 0 0 4mm; }\n";
    h += "h2 { font-size: 10.5pt; margin: 0 0 1.5mm; border-bottom: 1px solid #999; }\n";
    h += "table { border-collapse: collapse; width: 100%; } td { padding: .6mm 1mm; vertical-align: top; }\n";
    h += "td.k { white-space: nowrap; } .ou, .plus { color: #777; }\n";
    h += "kbd { font: 8.5pt ui-monospace, monospace; border: 1px solid #888; border-bottom-width: 2px;"
         " border-radius: 3px; padding: 0 3px; background: #f4f4f4; }\n";
    h += ".rep { font-size: 7.5pt; color: #fff; background: #3a6ea5; border-radius: 6px; padding: 0 4px; }\n";
    h += "</style></head><body>\n";
    h += "<h1>XPGAnalyser " + v + " \xE2\x80\x94 raccourcis clavier</h1>\n";
    h += "<p class=\"etat\">" + escapeHtml(page.status) + "</p>\n<div class=\"groupes\">\n";
    for (const auto& g : page.groups) {
        h += "<section><h2>" + escapeHtml(g.label) + "</h2><table>\n";
        for (const auto& r : g.rows) {
            h += "<tr><td class=\"k\">";
            for (std::size_t a = 0; a < r.caps.size(); ++a) {
                if (a) h += " <span class=\"ou\">ou</span> ";
                for (std::size_t i = 0; i < r.caps[a].size(); ++i) {
                    if (i) h += "<span class=\"plus\">+</span>";
                    h += "<kbd>" + escapeHtml(r.caps[a][i]) + "</kbd>";
                }
            }
            h += "</td><td>" + escapeHtml(r.text);
            if (!r.since.empty()) h += " <span class=\"rep\">" + escapeHtml(r.since) + "</span>";
            h += "</td></tr>\n";
        }
        h += "</table></section>\n";
    }
    h += "</div>\n</body></html>\n";
    return h;
}

} // namespace help::center
