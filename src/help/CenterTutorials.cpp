// =============================================================================
//  help/CenterTutorials.cpp - 1.11 (chantier T2, tranche 8) : les sujets du
//  centre pour le registre des tutoriels de T1 (voir CenterTutorials.hpp)
// =============================================================================
#include "CenterTutorials.hpp"

#include "../hmi/HmiGuide.hpp"
#include "../hmi/HmiModel.hpp"   // tranche 10 : kPlaceableKinds, kindKey, kindLabel (inline : rien a lier)

namespace help::center {

std::vector<TopicFacts> topicFacts(const Index& ix) {
    std::vector<TopicFacts> out;
    out.reserve(ix.topics().size());
    for (const auto& t : ix.topics()) {
        TopicFacts f;
        f.key = t.key;
        f.title = t.title;
        switch (t.source) {
        case Source::Guide:
            if (const auto* g = hmi::guide::topic(t.ref)) {
                f.places = g->places;
                for (const auto& b : g->blocks)
                    if (b.kind == hmi::guide::BlockKind::Heading) f.headings.push_back(b.text);
                // Un @objet du guide qui a sa tuile dans la bibliotheque. Tranche 10 (decision
                // du chef : help::TopicInfo de T1 fait foi) : `name` est le nom francais de la
                // tuile ("Vanne", hmi::kindLabel), `objectType` le genre interne du guide
                // ("Valve"). Les autres (SymbolInstance ; "*" : les parametres communs) sont
                // des pages du guide, comme chez T3 (hmi::tutotopics::fromGuide).
                if (!g->kind.empty() && g->kind != "*") {
                    for (const auto k : hmi::kPlaceableKinds)
                        if (hmi::kindKey(k) == g->kind) {
                            f.name = std::string(hmi::kindLabel(k));
                            f.objectType = g->kind;
                        }
                    if (!f.name.empty()) {
                        f.kind = TopicKindT1::Object;
                        break;
                    }
                }
            }
            f.kind = TopicKindT1::Guide;
            break;
        case Source::ApiPage:
            f.kind = TopicKindT1::Guide;   // une page de l'aide generale : deduite comme une page du guide
            break;
        case Source::Macro:
            f.kind = TopicKindT1::Macro;
            f.name = t.ref;
            break;
        case Source::Block:
            f.kind = t.kind == "ddt" ? TopicKindT1::DataType : TopicKindT1::Block;
            f.name = t.ref;
            break;
        case Source::Expression:
            f.kind = TopicKindT1::Expression;
            f.name = t.ref;
            break;
        case Source::ShortcutsPage:
        case Source::NotesPage:
        case Source::ReportPage:
            f.kind = TopicKindT1::Special;
            f.name = t.ref;
            break;
        }
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace help::center
