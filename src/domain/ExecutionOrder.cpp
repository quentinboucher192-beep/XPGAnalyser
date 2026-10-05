// =============================================================================
//  domain/ExecutionOrder.cpp
// =============================================================================
#include "ExecutionOrder.hpp"

#include <algorithm>

namespace domain {
namespace {

// L'unite qui porte cette section, pour l'affichage et pour savoir d'ou elle
// vient. Une section de tache nue en a une aussi : l'application lui cree un
// POU de genre Section pour que l'explorateur reste uniforme.
Index unitOf(const Project& p, Index section) {
    if (section < p.sections.size() && p.sections[section].owner != kNoIndex)
        return p.sections[section].owner;
    for (Index i = 0; i < p.pous.size(); ++i)
        if (std::find(p.pous[i].sections.begin(), p.pous[i].sections.end(), section)
            != p.pous[i].sections.end())
            return i;
    return kNoIndex;
}

} // namespace

SymbolId taskOf(const Project& p, Index section) {
    if (section >= p.sections.size()) return 0;
    if (p.sections[section].task != 0) return p.sections[section].task;

    const auto unit = unitOf(p, section);
    if (unit == kNoIndex || unit >= p.pous.size()) return 0;
    const auto& pou = p.pous[unit];
    // Seule une unite de programme est ordonnancee par une tache. Le corps d'un
    // DFB tourne quand une instance est appelee, pas au rythme d'un cycle.
    if (pou.kind != PouKind::ProgramUnit && pou.kind != PouKind::Section) return 0;
    return pou.task;
}

// Lot API 4 : UNE UNITE DE PROGRAMME TOURNE EN BLOC, A SON RANG DANS LA TACHE.
//
//  C'est ce que dit l'export : <programUnitDesc name="Logigrammes_A"
//  SectionOrder="15"> parmi les <sectionDesc SectionOrder="1..21"> de MAST - et
//  les sections de l'unite, numerotees 1..17 chez elle, tournent l'une apres
//  l'autre a ce rang-la. Les trier avec celles de la tache sur leur propre
//  numero les faisait tourner EN TETE de MAST (1, 1, 1, 2, 2, 2...), avant Init :
//  l'ordre montre et simule n'etait pas celui de l'automate.
//
//  Une unite sans rang (order 0 : un projet construit a la main, les essais
//  d'avant ce lot) garde l'ancienne regle : chacune de ses sections a son rang
//  dans la tache, sur l'echelle des sections de la tache.
namespace {

bool blockUnit(const Project& p, Index unit) {
    return unit != kNoIndex && unit < p.pous.size() && p.pous[unit].kind == PouKind::ProgramUnit
        && p.pous[unit].order > 0;
}

} // namespace

std::vector<ExecutionStep> executionOrder(const Project& p, SymbolId task) {
    std::vector<ExecutionStep> steps;
    if (task == 0) return steps;

    struct Keyed { ExecutionStep step; std::uint32_t k1, k2; };
    std::vector<Keyed> keyed;
    for (Index i = 0; i < p.sections.size(); ++i) {
        if (taskOf(p, i) != task) continue;
        const auto unit = unitOf(p, i);
        const bool fromUnit = unit != kNoIndex && unit < p.pous.size()
                              && p.pous[unit].kind == PouKind::ProgramUnit;
        const ExecutionStep step{i, unit, p.sections[i].order, fromUnit};
        if (fromUnit && blockUnit(p, unit))
            keyed.push_back({step, p.pous[unit].order, 1u + p.sections[i].order});
        else
            keyed.push_back({step, p.sections[i].order, 0u});
    }

    // STABLE : les ex aequo d'un import gardent l'ordre du fichier, et deux
    // appels de suite rendent la meme liste. Une simulation qui change d'ordre
    // d'une execution a l'autre n'est pas une simulation.
    std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b) {
        return a.k1 != b.k1 ? a.k1 < b.k1 : a.k2 < b.k2;
    });
    steps.reserve(keyed.size());
    for (const auto& k : keyed) steps.push_back(k.step);
    return steps;
}

std::vector<ExecutionStep> executionOrder(const Project& p, std::string_view task) {
    // `intern` ecrit dans la table, d'ou le const_cast - le projet n'est pas
    // modifie pour autant : interner un nom deja present rend le meme jeton.
    const auto id = const_cast<Project&>(p).strings.intern(task);
    return executionOrder(p, id);
}

std::vector<ExecutionEntry> executionEntries(const Project& p, SymbolId task) {
    std::vector<ExecutionEntry> out;
    for (const auto& step : executionOrder(p, task)) {
        if (step.fromProgramUnit && blockUnit(p, step.unit) && !out.empty() && out.back().unit
            && out.back().pou == step.unit) {
            out.back().sections.push_back(step.section);
            continue;
        }
        ExecutionEntry e;
        e.unit = step.fromProgramUnit && blockUnit(p, step.unit);
        e.pou = step.unit;
        e.section = step.section;
        e.sections.push_back(step.section);
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<ExecutionEntry> executionEntries(const Project& p, std::string_view task) {
    const auto id = const_cast<Project&>(p).strings.intern(task);
    return executionEntries(p, id);
}

namespace {

// Les rangs des entrees 1, 2, 3... : une section de la tache (ou d'une unite
// sans rang) prend le sien, une unite en bloc le sien ; les sections d'une
// unite, 1, 2, 3... chez elle.
void assignRanks(Project& p, const std::vector<ExecutionEntry>& entries) {
    std::uint32_t rank = 0;
    for (const auto& e : entries) {
        ++rank;
        if (e.unit && e.pou < p.pous.size()) {
            p.pous[e.pou].order = rank;
            std::uint32_t inner = 0;
            for (const auto s : e.sections)
                if (s < p.sections.size()) p.sections[s].order = ++inner;
        } else if (e.section < p.sections.size()) {
            p.sections[e.section].order = rank;
        }
    }
}

void resortTaskCache(Project& p, SymbolId task) {
    // `Task::sections` n'est qu'un cache de l'ordre. Le laisser derriere est ce qui
    // faisait diverger l'arbre du simulateur.
    for (auto& t : p.tasks) {
        if (t.name != task) continue;
        std::stable_sort(t.sections.begin(), t.sections.end(), [&](Index a, Index b) {
            const auto oa = a < p.sections.size() ? p.sections[a].order : 0u;
            const auto ob = b < p.sections.size() ? p.sections[b].order : 0u;
            return oa < ob;
        });
    }
}

// L'entree qui contient cette section ; entries.size() : aucune.
std::size_t entryOf(const std::vector<ExecutionEntry>& entries, Index section) {
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (std::find(entries[i].sections.begin(), entries[i].sections.end(), section) != entries[i].sections.end())
            return i;
    return entries.size();
}

} // namespace

void renumber(Project& p, SymbolId task) {
    assignRanks(p, executionEntries(p, task));
    resortTaskCache(p, task);
}

bool moveSection(Project& p, Index section, int delta) {
    if (section >= p.sections.size() || delta == 0) return false;
    const auto task = taskOf(p, section);
    if (task == 0) return false;

    renumber(p, task);                       // plus d'ex aequo : un cran = un cran
    auto entries = executionEntries(p, task);
    const auto from = entryOf(entries, section);
    if (from >= entries.size()) return false;
    // Une section d'une unite en bloc deplace l'unite entiere : ses sections ne
    // tournent qu'ensemble, a son rang.
    const auto to = static_cast<long long>(from) + delta;
    if (to < 0 || to >= static_cast<long long>(entries.size())) return false;

    // Un deplacement d'un cran est un echange ; de plusieurs, une rotation.
    // Ecrire l'echange pour les deux donnerait un resultat faux des que delta
    // depasse 1, et "descendre de 3" est ce que fait un glisser-deposer.
    auto moved = entries[from];
    entries.erase(entries.begin() + static_cast<long long>(from));
    entries.insert(entries.begin() + to, std::move(moved));
    assignRanks(p, entries);
    resortTaskCache(p, task);
    return true;
}

bool moveSectionBefore(Project& p, Index section, Index before) {
    if (section >= p.sections.size() || section == before) return false;
    const auto task = taskOf(p, section);
    if (task == 0) return false;
    if (before != kNoIndex && taskOf(p, before) != task) return false;

    renumber(p, task);
    auto entries = executionEntries(p, task);
    const auto from = entryOf(entries, section);
    if (from >= entries.size()) return false;

    // Deux sections de la MEME unite en bloc : on range l'unite en dedans.
    if (before != kNoIndex && entries[from].unit) {
        auto& inner = entries[from].sections;
        if (std::find(inner.begin(), inner.end(), before) != inner.end()) {
            inner.erase(std::find(inner.begin(), inner.end(), section));
            inner.insert(std::find(inner.begin(), inner.end(), before), section);
            assignRanks(p, entries);
            resortTaskCache(p, task);
            return true;
        }
    }

    long long to = static_cast<long long>(entries.size());
    if (before != kNoIndex) {
        const auto target = entryOf(entries, before);
        if (target >= entries.size()) return false;
        if (target == from) return false;           // deja a cette place
        to = static_cast<long long>(target);
    }

    // La cible est exprimee AVANT le retrait. Retirer un element place plus
    // haut decale tout ce qui suit d'un cran : sans cette correction, deposer
    // une section juste apres sa voisine la laisse ou elle etait, et
    // l'utilisateur recommence trois fois avant de conclure que c'est casse.
    auto moved = entries[from];
    entries.erase(entries.begin() + static_cast<long long>(from));
    if (to > static_cast<long long>(from)) --to;
    if (to < 0) to = 0;
    if (to > static_cast<long long>(entries.size())) to = static_cast<long long>(entries.size());
    if (to == static_cast<long long>(from)) {
        // rendu a sa place : rien n'a bouge
        entries.insert(entries.begin() + to, std::move(moved));
        return false;
    }
    entries.insert(entries.begin() + to, std::move(moved));
    assignRanks(p, entries);
    resortTaskCache(p, task);
    return true;
}

} // namespace domain
