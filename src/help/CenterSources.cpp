// =============================================================================
//  help/CenterSources.cpp - 1.11 (chantier T2) : le guide et la bibliotheque
//  en sujets du centre d'aide
// =============================================================================
#include "CenterSources.hpp"

#include "../hmi/HmiGuide.hpp"

#include <algorithm>

namespace help::center {

std::vector<SourceTopic> guideSources() {
    std::vector<SourceTopic> out;
    for (const auto& t : hmi::guide::topics()) {
        SourceTopic s;
        s.key = t.key;
        s.title = t.title;
        s.summary = t.summary;
        s.group = t.chapter;
        // "*" : le sujet des parametres communs a tous les objets, pas un objet a poser.
        if (!t.kind.empty() && t.kind != "*") s.kind = "objet";
        s.headings = static_cast<int>(std::count_if(t.blocks.begin(), t.blocks.end(), [](const hmi::guide::Block& b) {
            return b.kind == hmi::guide::BlockKind::Heading;
        }));
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<SourceTopic> librarySources(const std::vector<project::CatalogEntry>& library) {
    std::vector<SourceTopic> out;
    for (const auto& e : library) {
        SourceTopic s;
        switch (e.kind) {
            case project::CatalogKind::Macro:         s.kind = "macro"; break;
            case project::CatalogKind::FunctionBlock: s.kind = "dfb"; break;
            case project::CatalogKind::DerivedType:   s.kind = "ddt"; break;
            case project::CatalogKind::Other:         continue;
        }
        s.key = e.name;
        s.title = e.name;
        s.summary = e.help.summary;
        s.group = e.category;
        out.push_back(std::move(s));
    }
    return out;
}

// ---- 1.11 (tranche 2) : les pages de l'aide generale ----
const std::vector<std::pair<std::string_view, std::string_view>>& apiPageGroups() {
    static const std::vector<std::pair<std::string_view, std::string_view>> k = {
        // Demarrer (7), dans l'ordre de la maquette
        {"glisser", "start"}, {"themes", "start"}, {"dossiers", "start"}, {"explorateur-lot8", "start"},
        {"bandeaux", "start"}, {"filtres", "start"}, {"exports-lot8", "start"},
        // L'automate (API) (14)
        {"arbre-lot8", "plc"}, {"importer", "plc"}, {"reimporter", "plc"}, {"ordre", "plc"}, {"renommer", "plc"},
        {"compiler", "plc"}, {"alarmes", "plc"}, {"ddt-popups", "plc"},
        {"sim-ensemble", "plc"}, {"sim-debogage", "plc"}, {"sim-pause", "plc"}, {"sim-forcages", "plc"},
        {"sim-courbes", "plc"}, {"sim-journal", "plc"},
    };
    return k;
}

std::vector<SourceTopic> apiSources(const ui::HelpDocument& doc) {
    std::vector<SourceTopic> out;
    const auto& blocks = doc.blocks();
    for (const auto& [anchor, group] : apiPageGroups()) {
        const auto toc = std::find_if(doc.toc().begin(), doc.toc().end(),
                                      [&](const ui::TocEntry& e) { return e.level == 2 && e.anchor == anchor; });
        if (toc == doc.toc().end()) continue;   // une page retiree : l'essai le dit (21 attendues)
        SourceTopic s;
        s.key = std::string(anchor);
        s.title = toc->title;
        s.group = std::string(group);
        s.kind = "page";
        s.headings = 0;
        for (std::size_t i = toc->block + 1; i < blocks.size(); ++i) {
            const auto& b = blocks[i];
            if (b.kind == ui::BlockKind::Heading && b.level <= 2) break;
            if (b.kind == ui::BlockKind::Heading) ++s.headings;
            else if (s.summary.empty() && b.kind == ui::BlockKind::Paragraph) s.summary = b.text;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<SourceTopic> apiSources() {
    static const ui::HelpDocument doc = ui::buildHelp();
    return apiSources(doc);
}

// ---- 1.11 (recette R111-4) : une page, seule ----
namespace {

// La fin d'une partie : le titre suivant de son rang ou d'un rang plus haut ; le
// separateur qui la ferme reste dehors.
std::size_t partEnd(const std::vector<ui::Block>& blocks, std::size_t from) {
    std::size_t i = from + 1;
    while (i < blocks.size()
           && !(blocks[i].kind == ui::BlockKind::Heading && blocks[i].level <= blocks[from].level))
        ++i;
    while (i > from + 1 && blocks[i - 1].kind == ui::BlockKind::Separator) --i;
    return i;
}

void copyPart(ui::HelpDocument& out, const std::vector<ui::Block>& blocks, std::size_t from, std::size_t to,
              int minLevel) {
    for (std::size_t i = from; i < to; ++i) {
        const auto& b = blocks[i];
        switch (b.kind) {
            case ui::BlockKind::Heading:   out.heading(std::max(b.level, minLevel), b.text, b.anchor); break;
            case ui::BlockKind::Paragraph: out.paragraph(b.text); break;
            case ui::BlockKind::Bullet:    out.bullet(b.text); break;
            case ui::BlockKind::Code:      out.code(b.text); break;
            case ui::BlockKind::TableRow:  out.tableRow(b.cells, b.header); break;
            case ui::BlockKind::Separator: out.separator(); break;
        }
    }
}

} // namespace

ui::HelpDocument apiPage(const ui::HelpDocument& doc, std::string_view anchor) {
    ui::HelpDocument out;
    const auto& blocks = doc.blocks();
    const std::size_t at = doc.blockOf(anchor);
    if (at >= blocks.size()) return out;
    copyPart(out, blocks, at, partEnd(blocks, at), 1);
    if (anchor == "importer")
        if (const std::size_t f = doc.blockOf("formats"); f < blocks.size())
            copyPart(out, blocks, f, partEnd(blocks, f), 2);
    return out;
}

} // namespace help::center
