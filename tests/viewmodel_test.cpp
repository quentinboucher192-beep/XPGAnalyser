// Verifies the MVC seam and measures the sort/filter strategy at target scale.
//
// Lot API 7 : l'arbre de l'API - "Unites de programme" ne compte que les unites
// (une section de tache reste sous Taches), une unite montre ses dossiers de
// portee comme un DFB puis ses sections, et une variable dont le type a des
// membres se deplie sans limite (MemberNode).
#include "../src/app/ViewModels.hpp"
#include "../src/import/ProjectImporter.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>

using namespace app;
using namespace importer;
using Clock = std::chrono::steady_clock;

template <class F>
static double timeMs(F&& f) {
    const auto t0 = Clock::now();
    f();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Les verifications du lot API 7 comptent leurs echecs (un assert disparait
// dans une construction Release).
static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

// L'enfant de `parent` dont le texte commence par `prefix` ; kInvalidNode sinon.
static ui::NodeId childStartingWith(const ProjectTreeModel& tree, ui::NodeId parent, const std::string& prefix) {
    for (std::size_t k = 0; k < tree.childCount(parent); ++k) {
        const auto c = tree.childAt(parent, k);
        if (startsWith(tree.text(c), prefix)) return c;
    }
    return ui::kInvalidNode;
}

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "/mnt/project/MAST.XPG";

    core::EventBus bus;
    ProjectImporter imp(bus);
    auto res = imp.importFile(path);
    if (!res) { std::fprintf(stderr, "%s\n", res.error().message().c_str()); return 1; }
    bus.drain();

    ProjectRef project = res->project;
    auto refs = ProjectAnalyzer::buildReferenceIndex(*project);
    VariableTableModel vars(project, std::move(refs));

    std::printf("rows: %zu   columns: %zu\n", vars.rowCount(), vars.columnCount());
    assert(vars.columnCount() == VariableTableModel::ColumnCount);
    assert(vars.headerText(VariableTableModel::Address) == "Address");

    // ---- sort: only the index vector moves -------------------------------
    std::vector<ui::RowIndex> view(vars.rowCount());
    for (ui::RowIndex i = 0; i < view.size(); ++i) view[i] = i;

    const double byName = timeMs([&] {
        std::stable_sort(view.begin(), view.end(), [&](ui::RowIndex a, ui::RowIndex b) {
            return vars.less(a, b, VariableTableModel::Name);
        });
    });
    assert(!vars.less(view[1], view[0], VariableTableModel::Name) || view.size() < 2);

    const double byAddress = timeMs([&] {
        std::stable_sort(view.begin(), view.end(), [&](ui::RowIndex a, ui::RowIndex b) {
            return vars.less(a, b, VariableTableModel::Address);
        });
    });
    std::printf("sort by name   : %7.2f ms\n", byName);
    std::printf("sort by address: %7.2f ms\n", byAddress);

    // Numeric address ordering, not lexicographic.
    {
        std::vector<ui::RowIndex> located;
        for (ui::RowIndex r : view)
            if (vars.variable(r).located) located.push_back(r);
        for (std::size_t i = 1; i < located.size(); ++i)
            assert(!(vars.variable(located[i]).address < vars.variable(located[i - 1]).address));
        std::printf("located rows   : %zu (ordered)\n", located.size());
    }

    // ---- filter -----------------------------------------------------------
    ui::FilterChain chain;
    chain.setGlobalTerm("gvar1");
    std::vector<ui::RowIndex> filtered;
    const double filterMs = timeMs([&] { filtered = chain.apply(vars); });
    std::printf("filter 'gvar1' : %7.2f ms -> %zu rows\n", filterMs, filtered.size());

    ui::FilterChain unusedOnly;
    unusedOnly.addPredicate("unused", [&](ui::RowIndex r) { return vars.usage(r) == 0; });
    std::vector<ui::RowIndex> unused;
    const double predMs = timeMs([&] { unused = unusedOnly.apply(vars); });
    std::printf("filter unused  : %7.2f ms -> %zu rows\n", predMs, unused.size());
    for (auto r : unused) assert(vars.usage(r) == 0);

    ui::FilterChain none;
    assert(none.apply(vars).size() == vars.rowCount() && "an empty chain must keep every row");

    // ---- tree -------------------------------------------------------------
    ProjectTreeModel tree(project);
    const auto root = tree.root();
    // Lot API 8 : les dix dossiers de l automate (sans IHM), puis le dossier Simulation.
    assert(tree.hasChildren(root) && tree.childCount(root) == 11);
    assert(tree.childAt(root, 10) == ProjectTreeModel::simFolderNode() && tree.childCount(tree.childAt(root, 10)) == 6);
    std::printf("\nproject tree:\n  %s\n", tree.text(root).c_str());
    for (std::size_t i = 0; i < tree.childCount(root); ++i) {
        const auto folder = tree.childAt(root, i);
        std::printf("    %-34s %zu children\n", tree.text(folder).c_str(), tree.childCount(folder));
        if (tree.childCount(folder) > 0 && i < 3) {
            const auto first = tree.childAt(folder, 0);
            std::printf("        first: %s\n", tree.text(first).c_str());
        }
    }

    // ---- lot API 7 : les unites de programme ------------------------------
    using NK = ProjectTreeModel::NodeKind;
    const auto& p = *project;
    std::printf("\nlot API 7 : les unites de programme\n");
    {
        std::size_t units = 0, bare = 0;
        for (const auto& pou : p.pous) {
            units += pou.kind == domain::PouKind::ProgramUnit;
            bare += pou.kind == domain::PouKind::Section;
        }
        const auto folder = ProjectTreeModel::pack(NK::UnitsFolder, 0);
        check(tree.childAt(root, 3) == folder, "le dossier est toujours le quatrieme");
        check(tree.childCount(folder) == units,
              "il compte les unites de programme seules : " + std::to_string(units) + " (et pas les " + std::to_string(bare) + " sections de tache)");
        check(tree.text(folder) == "Unit\xC3\xA9s de programme" && tree.counterOf(folder) == std::to_string(units), "son libelle : " + tree.text(folder));
        bool onlyUnits = true;
        for (std::size_t k = 0; k < tree.childCount(folder); ++k) {
            const auto n = tree.childAt(folder, k);
            onlyUnits = onlyUnits && ProjectTreeModel::kindOf(n) == NK::ProgramUnit && ProjectTreeModel::indexOf(n) < p.pous.size()
                     && p.pous[ProjectTreeModel::indexOf(n)].kind == domain::PouKind::ProgramUnit;
        }
        check(onlyUnits, "chacun de ses enfants est une unite de programme");
        check(tree.childAt(folder, units) == ui::kInvalidNode, "rien au-dela");

        // Rien ne se perd : une section de tache (un POU Section) est sous Taches.
        std::vector<domain::Index> underTasks;
        const auto tasks = ProjectTreeModel::pack(NK::TaskFolder, 0);
        for (std::size_t t = 0; t < tree.childCount(tasks); ++t) {
            const auto task = tree.childAt(tasks, t);
            for (std::size_t s = 0; s < tree.childCount(task); ++s) underTasks.push_back(tree.sectionOf(tree.childAt(task, s)));
        }
        bool reachable = true;
        for (const auto& pou : p.pous)
            if (pou.kind == domain::PouKind::Section)
                for (const auto s : pou.sections)
                    reachable = reachable && std::find(underTasks.begin(), underTasks.end(), s) != underTasks.end();
        check(reachable, "chaque section de tache se trouve sous Taches (" + std::to_string(bare) + " POU de section)");

        // Une unite : ses dossiers de portee non vides, comme un DFB, puis ses
        // sections DIRECTEMENT dessous.
        const NK scopes[] = {NK::DfbInputs, NK::DfbOutputs, NK::DfbInOut, NK::DfbPublicVars, NK::DfbPrivateVars};
        bool shape = true, counted = true, direct = true;
        for (std::size_t k = 0; k < tree.childCount(folder); ++k) {
            const auto unit = tree.childAt(folder, k);
            const auto& pou = p.pous[ProjectTreeModel::indexOf(unit)];
            std::size_t c = 0, last = 0, inFolders = 0;
            for (; c < tree.childCount(unit); ++c) {
                const auto kind = ProjectTreeModel::kindOf(tree.childAt(unit, c));
                const auto at = std::find(std::begin(scopes), std::end(scopes), kind);
                if (at == std::end(scopes)) break;
                const auto rank = static_cast<std::size_t>(at - std::begin(scopes)) + 1;
                shape = shape && rank > last && tree.childCount(tree.childAt(unit, c)) > 0;     // dans l'ordre, jamais vide
                last = rank;
                inFolders += tree.childCount(tree.childAt(unit, c));
            }
            counted = counted && inFolders == pou.parameters.size() + pou.locals.size();
            direct = direct && tree.childCount(unit) - c == pou.sections.size();
            for (std::size_t s = c; s < tree.childCount(unit); ++s)
                direct = direct && ProjectTreeModel::kindOf(tree.childAt(unit, s)) == NK::Section
                      && tree.sectionOf(tree.childAt(unit, s)) == pou.sections[s - c];
            std::printf("       %s : %zu dossiers, %zu variables, %zu sections\n", tree.text(unit).c_str(), c, inFolders, pou.sections.size());
        }
        check(shape, "d'abord ses dossiers (Entrees, Sorties, E/S, publiques, privees), non vides, dans l'ordre");
        check(counted, "ses dossiers tiennent tous ses parametres et ses variables");
        check(direct, "puis ses sections, directement (pas de dossier Sections)");

        // Le projet d'essai : Logigrammes_A et ses 35 entrees / sorties, et le
        // chemin des sessions "API/Program units/Logigrammes_A/Matrice".
        const auto unitA = childStartingWith(tree, folder, "Logigrammes_A");
        check(unitA != ui::kInvalidNode, "Logigrammes_A est sous Unites de programme");
        if (unitA != ui::kInvalidNode) {
            check(ProjectTreeModel::kindOf(tree.childAt(unitA, 0)) == NK::DfbInOut
                      && tree.text(tree.childAt(unitA, 0)) == "Entr\xC3\xA9" "es / sorties" && tree.counterOf(tree.childAt(unitA, 0)) == "35",
                  "son premier enfant : " + tree.text(tree.childAt(unitA, 0)));
            // Lot API 8 : le nom seul, le nombre dans la pastille (counterOf).
            const auto privates = childStartingWith(tree, unitA, "Variables priv\xC3\xA9" "es");
            check(privates != ui::kInvalidNode && !tree.counterOf(privates).empty(), "... puis Variables privees");
            check(childStartingWith(tree, unitA, "Matrice") != ui::kInvalidNode, "Matrice est un enfant direct (les sessions la trouvent)");
        }
    }

    // ---- lot API 7 : deplier sans limite -----------------------------------
    std::printf("\nlot API 7 : les membres\n");
    {
        const auto named = [&](const char* name) {
            for (domain::Index i = 0; i < p.variables.size(); ++i)
                if (p.strings.text(p.variables[i].name) == name) return i;
            return domain::kNoIndex;
        };
        // Un champ de DDT de type DDT : armoire.sorties (Q, 18 champs).
        domain::Index sorties = domain::kNoIndex;
        for (const auto& dt : p.derivedTypes)
            if (p.strings.text(dt.name) == "armoire")
                for (const auto f : dt.fields)
                    if (p.strings.text(p.variables[f].name) == "sorties") sorties = f;
        check(sorties != domain::kNoIndex, "le DDT armoire a son champ sorties");
        if (sorties != domain::kNoIndex) {
            const auto field = ProjectTreeModel::pack(NK::DerivedField, sorties);
            check(tree.hasChildren(field) && tree.childCount(field) == 18, "armoire.sorties (Q) se deplie sur ses 18 champs");
            check(ProjectTreeModel::holdsMembers(field), "un champ porte des membres (Tout deplier s'y arrete)");
            const auto v3 = childStartingWith(tree, field, ".V3 : sortie_tor");
            check(v3 != ui::kInvalidNode && ProjectTreeModel::kindOf(v3) == NK::MemberNode, ".V3 : sortie_tor, un MemberNode");
            check(tree.memberPathOf(v3) == "sorties.V3" && tree.memberRootOf(v3) == sorties, "son chemin : sorties.V3, sa racine : le champ");
            const auto once = tree.childAt(field, 0), twice = tree.childAt(field, 0);
            check(once == twice && tree.childAt(field, 5) != tree.childAt(field, 6), "des identifiants stables, un par membre");
            const auto mat = v3 != ui::kInvalidNode ? childStartingWith(tree, v3, ".Mat : ") : ui::kInvalidNode;
            check(mat != ui::kInvalidNode && tree.hasChildren(mat) && tree.childCount(mat) == 16, ".V3.Mat : un tableau de 16, qui se deplie");
            const auto bit = mat != ui::kInvalidNode ? tree.childAt(mat, 4) : ui::kInvalidNode;
            check(startsWith(tree.text(bit), "[4] : BOOL") && tree.memberPathOf(bit) == "sorties.V3.Mat[4]" && !tree.hasChildren(bit),
                  "[4] : BOOL, sorties.V3.Mat[4], une feuille : " + tree.text(bit));
            // refresh() recalcule les enfants, sans changer les identifiants.
            const auto before = tree.childAt(field, 3);
            tree.refresh();
            check(tree.childAt(field, 3) == before && tree.childCount(field) == 18, "refresh() garde les identifiants des membres");
        }
        // Le commentaire d'un element (<instanceElementDesc name="[2]">) sur la
        // declaration du tableau : config_gaz.purge[2], "helium".
        domain::Index purge = domain::kNoIndex;
        for (const auto& dt : p.derivedTypes)
            if (p.strings.text(dt.name) == "config_gaz")
                for (const auto f : dt.fields)
                    if (p.strings.text(p.variables[f].name) == "purge") purge = f;
        if (purge != domain::kNoIndex) {
            const auto field = ProjectTreeModel::pack(NK::DerivedField, purge);
            const auto helium = tree.childCount(field) == 3 ? tree.childAt(field, 2) : ui::kInvalidNode;
            check(tree.text(helium) == "[2] : config_purge   // helium", "un element dit son commentaire : " + tree.text(helium));
        } else {
            check(false, "le DDT config_gaz a son champ purge");
        }
        // Une globale de la liste Instances de DDT : Armoires, ARRAY[0..1] OF armoire.
        const auto ddts = ProjectTreeModel::pack(NK::DdtInstanceFolder, 0);
        const auto armoires = childStartingWith(tree, ddts, "Armoires : ");
        check(armoires != ui::kInvalidNode && tree.hasChildren(armoires) && tree.childCount(armoires) == 2, "Armoires se deplie : [0] et [1]");
        if (armoires != ui::kInvalidNode && tree.childCount(armoires) == 2) {
            const auto first = tree.childAt(armoires, 0);
            check(tree.memberPathOf(first) == "Armoires[0]" && startsWith(tree.text(first), "[0] : armoire"), "[0] : armoire, Armoires[0]");
            check(tree.childCount(first) >= 30 && childStartingWith(tree, first, ".sorties : Q") != ui::kInvalidNode, "puis ses champs, dont .sorties : Q");
            // La meme cle sous une autre racine est un autre membre : l'unite
            // Gestion_armoires a aussi son "armoires" (une entree / sortie).
            bool distinct = true;
            for (std::size_t k = 0; k < tree.childCount(ProjectTreeModel::pack(NK::UnitsFolder, 0)); ++k) {
                const auto unit = tree.childAt(ProjectTreeModel::pack(NK::UnitsFolder, 0), k);
                const auto inout = childStartingWith(tree, unit, "Entr\xC3\xA9" "es / sorties");
                const auto local = inout != ui::kInvalidNode ? childStartingWith(tree, inout, "armoires : ") : ui::kInvalidNode;
                if (local != ui::kInvalidNode && tree.childCount(local) == 2)
                    distinct = distinct && tree.childAt(local, 0) != first && tree.memberRootOf(tree.childAt(local, 0)) != tree.memberRootOf(first);
            }
            check(distinct, "armoires[0] d'une unite n'est pas Armoires[0] de la liste (deux racines)");
            check(tree.memberKeyOf(first) == "Armoires[0]" && !tree.memberIsGroup(first), "sa cle : Armoires[0], un membre reel");
        }
        // Un grand tableau, en paquets : tempon, ARRAY[0..149] OF INT (Elementaires).
        const auto tempon = childStartingWith(tree, ProjectTreeModel::pack(NK::ElementaryFolder, 0), "tempon : ");
        check(tempon != ui::kInvalidNode && tree.childCount(tempon) == 2, "tempon se deplie en deux paquets");
        if (tempon != ui::kInvalidNode && tree.childCount(tempon) == 2) {
            const auto pack = tree.childAt(tempon, 1);
            check(tree.memberIsGroup(pack) && tree.memberKeyOf(pack) == "tempon[100..149]" && tree.memberPathOf(pack) == "tempon[100 \xE2\x80\xA6 149]"
                      && tree.text(pack) == "[100 \xE2\x80\xA6 149] : paquet de 50" && tree.childCount(pack) == 50,
                  "le paquet [100 ... 149] : sa cle, son chemin montre, ses 50 elements");
            const auto cell = tree.childCount(pack) == 50 ? tree.childAt(pack, 4) : ui::kInvalidNode;
            check(!tree.memberIsGroup(cell) && tree.memberPathOf(cell) == "tempon[104]" && startsWith(tree.text(cell), "[104] : INT"),
                  "puis tempon[104] : INT");
            check(tree.memberParentOf(cell) == pack && tree.memberParentOf(pack) == tempon && tree.memberParentOf(tempon) == ui::kInvalidNode,
                  "chacun connait son parent : le paquet, puis la variable");
        }
        // Une variable d'unite, instance de DFB : ses broches, ses publiques, ses privees.
        const auto gc = named("Gc_ChangementA");
        check(gc != domain::kNoIndex, "Gc_ChangementA existe");
        if (gc != domain::kNoIndex) {
            const auto node = ProjectTreeModel::pack(NK::DfbVariable, gc);
            check(tree.hasChildren(node) && tree.childCount(node) > 10, "une instance de DFB d'une unite se deplie sur ses broches ("
                                                                         + std::to_string(tree.childCount(node)) + ")");
        }
        // Dans un DFB : Blocs DFB > DFB_GRAFCETENGINE > Entrees / sorties >
        // Steps (ARRAY[0..27] OF ST_GC_Step) > [3] > .Active, une feuille.
        const auto dfbs = ProjectTreeModel::pack(NK::DfbFolder, 0);
        const auto engine = childStartingWith(tree, dfbs, "DFB_GRAFCETENGINE");
        const auto engineIo = engine != ui::kInvalidNode ? childStartingWith(tree, engine, "Entr\xC3\xA9" "es / sorties") : ui::kInvalidNode;
        const auto steps = engineIo != ui::kInvalidNode ? childStartingWith(tree, engineIo, "Steps : ") : ui::kInvalidNode;
        check(steps != ui::kInvalidNode && ProjectTreeModel::kindOf(steps) == NK::DfbVariable && tree.childCount(steps) == 28,
              "DFB_GRAFCETENGINE.Steps se deplie sur ses 28 etapes");
        if (steps != ui::kInvalidNode && tree.childCount(steps) == 28) {
            const auto third = tree.childAt(steps, 3);
            check(startsWith(tree.text(third), "[3] : ST_GC_Step") && tree.memberPathOf(third) == "Steps[3]" && tree.childCount(third) == 10,
                  "[3] : ST_GC_Step, Steps[3], ses 10 champs");
            const auto active = childStartingWith(tree, third, ".Active : BOOL");
            check(active != ui::kInvalidNode && !tree.hasChildren(active) && tree.memberPathOf(active) == "Steps[3].Active",
                  ".Active : BOOL, Steps[3].Active, une feuille");
        }
        // Une variable elementaire : pas de fleche.
        const auto elementary = ProjectTreeModel::pack(NK::ElementaryFolder, 0);
        bool leaf = false;
        for (std::size_t k = 0; k < tree.childCount(elementary) && !leaf; ++k) {
            const auto n = tree.childAt(elementary, k);
            const auto v = tree.variableOf(n);
            if (v < p.variables.size() && p.strings.text(p.variables[v].type.name) == "BOOL")
                leaf = !tree.hasChildren(n) && tree.childCount(n) == 0;
        }
        check(leaf, "un BOOL ne se deplie pas");
        // Un identifiant d'un autre modele (ou perime) : ni enfant, ni texte.
        const auto stale = ProjectTreeModel::pack(NK::MemberNode, 0x0FFFFFF0u);
        check(tree.childCount(stale) == 0 && !tree.hasChildren(stale) && tree.text(stale).empty() && tree.memberPathOf(stale).empty()
                  && tree.childAt(stale, 0) == ui::kInvalidNode,
              "un MemberNode inconnu : ni enfant ni texte (pas de plantage)");
        check(tree.memberPathOf(root).empty() && tree.memberRootOf(root) == domain::kNoIndex && !ProjectTreeModel::holdsMembers(root),
              "un autre noeud n'est pas un membre");
    }

    // ---- lot API 7 : un type qui se contient (un projet abime) ----------------
    //  "Tout deplier" (expandToDepth(99)) le deplierait sans fin - 2^99 noeuds
    //  pour deux champs. Un membre du type d'un de ses ancetres ne se deplie pas.
    std::printf("\nlot API 7 : un type qui se contient\n");
    {
        auto q = std::make_shared<domain::Project>();
        domain::DerivedType loop;
        loop.name = q->strings.intern("Boucle");
        for (const char* name : {"x", "y"}) {
            domain::Variable f;
            f.name = q->strings.intern(name);
            f.type.name = q->strings.intern("Boucle");
            f.scope = domain::VariableScope::DerivedMember;
            q->variables.push_back(f);
            loop.fields.push_back(static_cast<domain::Index>(q->variables.size() - 1));
        }
        q->derivedTypes.push_back(loop);
        domain::Variable g;
        g.name = q->strings.intern("g");
        g.type.name = q->strings.intern("Boucle");
        g.type.klass = domain::TypeClass::Derived;
        g.type.derivedIndex = 0;
        g.scope = domain::VariableScope::Global;
        q->variables.push_back(g);
        q->buildIndices();
        ProjectTreeModel small(q);
        std::size_t nodes = 0;
        const auto walk = [&](auto&& self, ui::NodeId n, int depth) -> void {
            ++nodes;
            if (depth >= 99 || nodes > 100000 || !small.hasChildren(n)) return;
            for (std::size_t k = 0; k < small.childCount(n); ++k) self(self, small.childAt(n, k), depth + 1);
        };
        walk(walk, small.root(), 0);
        check(nodes < 100, "tout deplier s'arrete : " + std::to_string(nodes) + " noeuds");
        const auto list = ProjectTreeModel::pack(NK::DdtInstanceFolder, 0);
        const auto top = small.childCount(list) == 1 ? small.childAt(list, 0) : ui::kInvalidNode;
        check(top != ui::kInvalidNode && small.childCount(top) == 2 && !small.hasChildren(small.childAt(top, 0)),
              "g se deplie sur .x et .y, qui ne se deplient pas");
    }

    // ---- lot API 7 : une sous-routine lue d'un .XPG ---------------------------
    //  Elle porte un POU de genre Section (sa section dit isSubroutine) : elle
    //  n'est plus sous Unites de programme, elle est sous Sous-routines, et
    //  toujours sous Taches > MAST.
    std::printf("\nlot API 7 : une sous-routine lue d'un .XPG\n");
    {
        auto q = std::make_shared<domain::Project>();
        domain::Task mast;
        mast.name = q->strings.intern("MAST");
        mast.type = "cyclic";
        domain::Section sr;
        sr.name = q->strings.intern("SR_Calcul");
        sr.task = mast.name;
        sr.language = domain::PouLanguage::ST;
        sr.isSubroutine = true;
        sr.owner = 0;
        q->sections.push_back(sr);
        domain::Pou own;
        own.name = sr.name;
        own.kind = domain::PouKind::Section;
        own.sections.push_back(0);
        q->pous.push_back(own);
        mast.sections.push_back(0);
        q->tasks.push_back(mast);
        q->buildIndices();
        ProjectTreeModel small(q);
        const auto units = ProjectTreeModel::pack(NK::UnitsFolder, 0);
        const auto srs = ProjectTreeModel::pack(NK::SubroutinesFolder, 0);
        const auto task = ProjectTreeModel::pack(NK::Task, 0);
        check(small.childCount(units) == 0, "pas sous Unites de programme");
        check(small.childCount(srs) == 1 && small.subroutineOf(small.childAt(srs, 0)) == 0 && startsWith(small.text(small.childAt(srs, 0)), "SR_Calcul"),
              "sous Sous-routines : " + (small.childCount(srs) ? small.text(small.childAt(srs, 0)) : std::string("rien")));
        check(small.childCount(task) == 1 && small.sectionOf(small.childAt(task, 0)) == 0, "et sous Taches > MAST");
    }

    // ---- property grid ----------------------------------------------------
    const auto cats = buildConfigurationProperties(*project);
    std::printf("\nPLC configuration categories: %zu\n", cats.size());
    for (const auto& c : cats)
        std::printf("  %-14s %zu properties, %zu subcategories\n",
                    c.name.c_str(), c.properties.size(), c.children.size());
    assert(cats.size() >= 3);

    DiagnosticsTableModel diags(project, res->report);
    std::printf("\ndiagnostics rows: %zu\n", diags.rowCount());

    if (failures) {
        std::printf("\nviewmodel_test: %d echec(s)\n", failures);
        return 1;
    }
    std::printf("\nviewmodel_test: all assertions passed\n");
    return 0;
}
