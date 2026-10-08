#include "ViewModels.hpp"

#include "../project/CodeIconKeys.hpp"

#include "../project/MacroFolders.hpp"
#include "../hmi/HmiTemplates.hpp"
#include "../hmi/HmiTypes.hpp"
#include "../hmi/HmiOperators.hpp"   // 1.10 (chantier O) : la signature d'un operateur dans l'arbre
#include "../hmi/HmiSymbols.hpp"     // 1.10 (chantier O) : le symbole d'une instance (ses operateurs)
#include "../hmi/HmiFolders.hpp"      // lot 21 : les listes rangees en dossiers

#include <optional>
#include "../hmi/HmiCommands.hpp"
#include "../hmi/HmiPublicVars.hpp"
#include "../hmi/HmiAlarmGroups.hpp"   // 1.11.1 (decision 108) : le groupe de l'IHM lie a celui d'un objet
#include "hmi/HmiTreeData.hpp"
#include "hmi/HmiObjectAlarmTree.hpp"   // 1.10 (chantier O) : les donnees seulement (pas de lien avec l'IHM)
#include "../hmi/HmiBuildState.hpp"   // 1.11 (chantier T3, C4) : compilable / generable

#include "../domain/ExecutionOrder.hpp"
#include "../project/IoCheck.hpp"

#include "../ui/Icons.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace app {

namespace {
// 1.8.0 : l'icone au choix d'un element (s'il en a une) remplace celle de
// toujours, dans sa couleur (core/CodeIcons.hpp). -1 : rien ne change.
void codeIconStyle(ui::CellStyle& s, int icon) {
    if (icon < 0) return;
    s.icon = ui::codeIcon(icon);
    s.iconColor = ui::codeIconColor(icon);
    s.iconTone = ui::Tone::None;
}

// 1.11 (chantier T3, C4) : les icones de Compiler et de Generer, lues dans le
// cache (hmi::build) : ✓ / ✕ n / ⊘, et pour un script ⇩ ou ⇩ barre. Toutes
// les deux portent la meme infobulle (la raison, et Generer pour un script).
void buildTrail(ui::CellStyle& s, const hmi::build::State* st) {
    if (!st) return;
    namespace bs = hmi::build;
    const std::string tip = bs::tooltip(*st);
    ui::CellStyle::Trail c;
    c.glyph = std::string(bs::compileGlyph(st->compile));
    if (st->compile == bs::Compile::Error && st->errors > 0) c.glyph += std::to_string(st->errors);   // ✕2
    c.tone = st->compile == bs::Compile::Ok ? ui::Tone::Ok : st->compile == bs::Compile::Error ? ui::Tone::Error : ui::Tone::Muted;
    c.tip = tip;
    s.trail.push_back(std::move(c));
    if (st->generate == bs::Generate::None) return;            // une section : le programme part entier
    ui::CellStyle::Trail g;
    g.glyph = std::string(bs::generateGlyph(st->generate));
    g.struck = st->generate == bs::Generate::No;
    g.tone = g.struck ? ui::Tone::Warning : ui::Tone::Info;
    g.tip = tip;
    s.trail.push_back(std::move(g));
}

// 1.11 (chantier T3, C4, tranche 10) : LE RESUME d'un noeud qui porte des
// sections (une unite de programme, une tache) ou des scripts : ✓ si tout
// compile ; sinon « 2 ✕/⊘ » (comme le titre d'un dossier, Cache::folderSummary),
// rouge s'il y a une erreur, gris si ce n'est que du non compilable. Le client
// ne cherche pas l'icone dans les lignes repliees : le noeud la montre deja.
struct BuildTally {
    int total{0}, notCompiling{0}, errors{0}, notGenerated{0};
    std::vector<std::string> named;           // les premiers qui ne compilent pas
};
void tally(BuildTally& t, std::string_view name, const hmi::build::State* st) {
    if (!st) return;
    ++t.total;
    if (!st->compiles()) {
        ++t.notCompiling;
        const bool err = st->compile == hmi::build::Compile::Error;
        t.errors += err ? 1 : 0;
        if (t.named.size() < 4)
            t.named.push_back(std::string(name) + (err ? " (en erreur)" : " (non compilable)"));
    }
    if (!st->generated()) ++t.notGenerated;
}
void summaryTrail(ui::CellStyle& s, const BuildTally& t, std::string_view what, std::string_view plural = {}) {   // what : "section", "script"
    if (t.total == 0) return;
    const auto noun = [what, plural](int k) {
        return std::to_string(k) + " " + (k > 1 && !plural.empty() ? std::string(plural) : std::string(what) + (k > 1 ? "s" : ""));
    };
    ui::CellStyle::Trail c;
    if (t.notCompiling == 0 && t.notGenerated == 0) {
        c.glyph = "\xE2\x9C\x93";
        c.tone = ui::Tone::Ok;
        c.tip = "Tout compile : " + noun(t.total) + ".";
    } else {
        c.glyph = hmi::build::Cache::folderSummary(t.notCompiling, t.notGenerated);
        c.tone = t.errors > 0 ? ui::Tone::Error : t.notCompiling > 0 ? ui::Tone::Muted : ui::Tone::Warning;
        if (t.notCompiling > 0) {
            c.tip = noun(t.notCompiling) + " sur " + std::to_string(t.total) + " ne compile" + (t.notCompiling > 1 ? "nt" : "") + " pas : ";
            for (std::size_t k = 0; k < t.named.size(); ++k) c.tip += (k ? ", " : "") + t.named[k];
            if (t.notCompiling > static_cast<int>(t.named.size())) c.tip += "\xE2\x80\xA6";
            c.tip += ".";
        }
        if (t.notGenerated > 0)
            c.tip += std::string(c.tip.empty() ? "" : " ") + std::to_string(t.notGenerated)
                   + (t.notGenerated > 1 ? " ne partent pas" : " ne part pas") + " avec G\xC3\xA9n\xC3\xA9rer.";
        c.tip += t.notCompiling > 0 ? " Le filtre \xC2\xAB Ce qui ne compile pas \xC2\xBB les montre."
                                    : " Le filtre \xC2\xAB Ce qui n\xE2\x80\x99" "est pas g\xC3\xA9n\xC3\xA9r\xC3\xA9 \xC2\xBB les montre.";
    }
    s.trail.push_back(std::move(c));
}
// 1.11 (R111, recette T3-11) : un nombre et son nom, accordes : « 1 section »,
// « 2 sections » (0 prend le pluriel, comme partout dans l'appli).
std::string compte(std::size_t n, std::string_view un, std::string_view plusieurs) {
    return std::to_string(n) + " " + std::string(n == 1 ? un : plusieurs);
}
} // namespace


    using namespace domain;

    // ============================================================ variables =====
    VariableTableModel::VariableTableModel(ProjectRef project,
        importer::ProjectAnalyzer::ReferenceIndex refs)
        : project_(std::move(project)), refs_(std::move(refs)) {}

    std::size_t VariableTableModel::rowCount() const { return project_->variables.size(); }

    const Variable& VariableTableModel::variable(ui::RowIndex r) const { return project_->variables[r]; }

    std::uint32_t VariableTableModel::usage(ui::RowIndex r) const {
        const auto it = refs_.find(project_->variables[r].name);
        return it == refs_.end() ? 0u : it->second;
    }

    std::string VariableTableModel::headerText(std::size_t c) const {
        switch (c) {
        case Name:    return "Name";
        case Type:    return "Type";
        case Address: return "Address";
        case Scope:   return "Scope";
        case Comment: return "Comment";
        case Usage:   return "Usage";
        default:      return {};
        }
    }

    std::string VariableTableModel::cellText(ui::RowIndex r, std::size_t c) const {
        const auto& v = project_->variables[r];
        switch (c) {
        case Name:    return std::string(project_->strings.text(v.name));
        case Type:    return std::string(project_->strings.text(v.type.name));
        case Address: return v.address.raw;
        case Scope:   return std::string(toString(v.scope));
        case Comment: return std::string(project_->strings.text(v.comment));
        case Usage: {
            const auto n = usage(r);
            return n == 0 ? std::string("unused") : std::to_string(n);
        }
        default: return {};
        }
    }

    ui::CellStyle VariableTableModel::cellStyle(ui::RowIndex r, std::size_t c) const {
        ui::CellStyle s;
        const auto& v = project_->variables[r];
        if (c == Name) {
            s.icon = v.type.klass == TypeClass::FunctionBlock ? ui::Icon::FunctionBlock
                : v.located ? ui::Icon::LocatedVariable
                : ui::Icon::Variable;
        }
        if (c == Address) {
            s.monospace = true;                      // addresses align only in a mono face
            if (v.located) s.fg = gfx::Color::rgb(0x9CDCFE);
        }
        if (c == Usage && usage(r) == 0) s.fg = gfx::Color::rgb(0xDCA032);   // warning amber
        if (c == Type && v.type.klass == TypeClass::FunctionBlock) s.fg = gfx::Color::rgb(0x4EC9A0);
        if (c == Comment && v.comment == 0) s.fg = gfx::Color::rgb(0x6A6A6A);
        return s;
    }

    bool VariableTableModel::less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const {
        const auto& va = project_->variables[a];
        const auto& vb = project_->variables[b];
        switch (c) {
            // Address sorts by area then numeric offset: %MW9 before %MW10.
        case Address: return va.address < vb.address;
        case Usage:   return usage(a) < usage(b);
        case Scope:   return static_cast<int>(va.scope) < static_cast<int>(vb.scope);
        case Type:    return project_->strings.text(va.type.name) < project_->strings.text(vb.type.name);
        case Comment: return project_->strings.text(va.comment) < project_->strings.text(vb.comment);
        case Name:
        default:      return project_->strings.text(va.name) < project_->strings.text(vb.name);
        }
    }

    // ========================================================= project tree =====
    // NodeId layout: [ 8 bits kind | 28 bits index | 28 bits sub-index ]
    namespace {
        constexpr int kKindShift = 56;
        constexpr int kIndexShift = 28;
        constexpr std::uint64_t kMask28 = (1ull << 28) - 1;

        // UNE VUE IHM EST DESIGNEE PAR SON IDENTIFIANT, pas par son rang : un
        // noeud deplie reste celui de SA vue quand on reordonne, duplique ou
        // supprime (par le rang, le depli passait a la vue qui prenait la place).
        // Les identifiants sont petits (un compteur) : 28 bits suffisent.
        // Lot 16 : les dossiers des variables IHM, comme les filtres de Visual Studio.
        namespace hmivars {
        std::string upperOf(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }
        // Les enfants d'un dossier ("" : Variables IHM) : ses sous-dossiers, puis ses variables.
        std::vector<ui::NodeId> childrenOf(const hmi::Project& p, const std::string& folder) {
            std::vector<ui::NodeId> out;
            const std::string key = upperOf(folder);
            const auto all = hmi::types::allFolders(p);
            for (std::size_t i = 0; i < all.size(); ++i)
                if (upperOf(hmi::types::folderParent(all[i])) == key)
                    out.push_back(ProjectTreeModel::pack(ProjectTreeModel::NodeKind::HmiVarFolder, static_cast<domain::Index>(i)));
            for (const auto& v : p.programs.variables)
                if (upperOf(v.folder) == key)
                    out.push_back(ProjectTreeModel::pack(ProjectTreeModel::NodeKind::HmiVariable, static_cast<domain::Index>(v.id & kMask28)));
            return out;
        }
        } // namespace hmivars

        // Lot 21 : les autres listes de l'IHM rangees en dossiers (hmi::fold).
        namespace hmilists {
        using hmi::fold::List;
        ui::NodeId itemNode(List list, hmi::Id id) {
            using NK = ProjectTreeModel::NodeKind;
            switch (list) {
                case List::Scripts: return ProjectTreeModel::pack(NK::HmiGeneralScript, static_cast<domain::Index>(id & kMask28));
                case List::Types:   return ProjectTreeModel::pack(NK::HmiTypeNode, static_cast<domain::Index>(id & kMask28));
                default:            return ProjectTreeModel::pack(NK::HmiView, static_cast<domain::Index>(id & kMask28));
            }
        }
        // Les enfants d'un niveau ("" : la liste elle-meme) : ses sous-dossiers, puis ses elements.
        std::vector<ui::NodeId> childrenOf(const hmi::Project& p, List list, const std::string& folder) {
            std::vector<ui::NodeId> out;
            const auto all = hmi::fold::allFolders(p, list);
            for (std::size_t i = 0; i < all.size(); ++i)
                if (hmi::fold::sameFolder(hmi::types::folderParent(all[i]), folder))
                    out.push_back(ProjectTreeModel::pack(ProjectTreeModel::NodeKind::HmiListFolder, static_cast<domain::Index>(i),
                                                         static_cast<domain::Index>(list)));
            for (const auto& it : hmi::fold::items(p, list))
                if (hmi::fold::sameFolder(it.folder, folder)) out.push_back(itemNode(list, it.id));
            return out;
        }
        // La liste d'un noeud-liste (Vues, Popups, un modele, Symboles, Scripts, Types IHM).
        std::optional<List> listOfNode(ProjectTreeModel::NodeKind kind, domain::Index i) {
            using NK = ProjectTreeModel::NodeKind;
            switch (kind) {
                case NK::HmiViewFolder:     return i == 1 ? std::optional<List>(List::Views) : i == 2 ? std::optional<List>(List::Popups) : std::nullopt;
                case NK::HmiTemplateFolder: return i == 0 ? List::Templates : i == 1 ? List::Headers : List::Footers;
                case NK::HmiSymbolsFolder:  return List::Symbols;
                case NK::HmiScriptsFolder:  return List::Scripts;
                case NK::HmiTypesFolder:    return List::Types;
                default:                    return std::nullopt;
            }
        }
        } // namespace hmilists

        const hmi::View* hmiViewById(const hmi::Document* doc, std::uint64_t id28) {
            if (!doc) return nullptr;
            for (const auto& v : doc->project.views)
                if ((v.id & kMask28) == id28) return &v;
            return nullptr;
        }

        // LOT 5 : les noeuds deballes. Les objets, alarmes, recettes... sont
        // designes par leur IDENTIFIANT (comme les vues) : un noeud deplie
        // reste le sien quand la liste change. Une surcharge : objet et rang
        // tiennent ensemble dans les 28 bits du sous-index (objet < 2^20).
        constexpr std::uint64_t kItemBits = 8;
        const hmi::Object* hmiObjectById(const hmi::View* v, std::uint64_t id) {
            if (!v) return nullptr;
            for (const auto& o : v->objects) if ((o.id & kMask28) == id) return &o;
            return nullptr;
        }
        template <class T> const T* byId(const std::vector<T>& list, std::uint64_t id) {
            for (const auto& x : list) if ((x.id & kMask28) == id) return &x;
            return nullptr;
        }
        // Le libelle de chaque genre : la table de l'en-tete du modele (en ligne,
        // ce fichier n'appelle pas la bibliotheque de l'IHM).
        std::string_view kindText(hmi::Kind k) { return hmi::kindLabel(k); }
        ui::Icon kindIcon(hmi::Kind k) {
            switch (k) {
                case hmi::Kind::Text:        return ui::Icon::Document;
                case hmi::Kind::Image:
                case hmi::Kind::Video:       return ui::Icon::Image;
                case hmi::Kind::Gauge:
                case hmi::Kind::ProgressBar:
                case hmi::Kind::Trend:       return ui::Icon::Chart;
                case hmi::Kind::Table:
                case hmi::Kind::List:
                case hmi::Kind::History:
                case hmi::Kind::RecipeManager: return ui::Icon::AnimationTable;
                case hmi::Kind::AnimatedImage:
                case hmi::Kind::AnimatedGif:   return ui::Icon::Image;
                case hmi::Kind::Indicator:   return ui::Icon::Ok;
                case hmi::Kind::Group:
                case hmi::Kind::Container:   return ui::Icon::Folder;
                default:                     return ui::Icon::Module;
            }
        }
        std::string_view eventText(std::string_view e) {
            if (e == "Demarrage") return "D\xC3\xA9marrage";
            if (e == "Cyclique") return "Cyclique";
            if (e == "Changement") return "Sur changement";
            if (e == "Appel") return "Appel\xC3\xA9";
            return e;
        }
        std::string countTag(std::size_t n) { return "  [" + std::to_string(n) + "]"; }
        // Lot 8 : le role des vues d'un dossier de Modeles (0 ecrans modeles, 1 en-tetes, 2 pieds).
        std::string_view templateRoleOf(std::uint64_t k) { return k == 0 ? "modele" : k == 1 ? "entete" : "pied"; }
        // L'objet d'une surcharge : les 20 bits hauts du sous-index.
        const hmi::Object* hmiItemObject(const hmi::View* v, std::uint64_t sub) {
            if (!v) return nullptr;
            const std::uint64_t id = sub >> kItemBits;
            constexpr std::uint64_t mask = (1ull << (28 - kItemBits)) - 1;
            for (const auto& o : v->objects) if ((o.id & mask) == id) return &o;
            return nullptr;
        }
        // ---- 1.11.1 (decision 108) : « Variables d'instances », tout ce qu'un objet publie,
        // dans l'ordre du volet : ses variables, les parametres d'une instance, son groupe
        // d'alarmes (un objet du synoptique, une instance), ses alarmes.
        struct InstParts {
            std::size_t members{0}, params{0};
            bool        group{false};
            std::size_t alarms{0};
            [[nodiscard]] std::size_t count() const { return members + params + (group ? 1u : 0u) + (alarms ? 1u : 0u); }
        };
        InstParts instPartsOf(const hmi::Project& p, const hmi::View& v, const hmi::Object& o) {
            InstParts r;
            r.members = hmi::pub::objectMembers(o).size();
            r.params = hmi::pub::instanceParams(p, o).size();
            r.group = hmi::pub::hasAlarmGroup(o);
            r.alarms = r.group ? std::min<std::size_t>(hmi::pub::objectAlarmNames(p, v, o).size(), 256) : 0;
            return r;
        }
        // Le sous-index d'une ligne sous l'objet : (objet << 8) | rang.
        Index instItem(const hmi::Object& o, std::size_t rank) {
            return static_cast<Index>(((o.id & ((1ull << (28 - kItemBits)) - 1)) << kItemBits) | (rank & 0xFF));
        }
        // HmiInstAlarmVar : la vue sous le rang du membre (index = (membre << 24) | vue).
        constexpr std::uint64_t kInstViewMask = (1ull << 24) - 1;
        // Le groupe de l'IHM (IHM > Alarmes > Groupes) auquel le groupe interne de l'objet est lie ("" : aucun).
        std::string instLinkedGroup(const hmi::Project& p, const hmi::View& v, const hmi::Object& o) {
            std::string symbols;
            if (o.kind == hmi::Kind::SymbolInstance)
                if (const auto* sp = o.find("symbol")) symbols = sp->value;
            const auto* link = hmi::alarmGroupLinkOf(p, hmi::objectGroupOf(v, o), symbols);
            return link ? link->group : std::string{};
        }
        // L'alarme d'un noeud HmiInstAlarm ou HmiInstAlarmVar (rien : hors de la liste).
        struct InstAlarm {
            const hmi::View*   view{nullptr};
            const hmi::Object* object{nullptr};
            std::optional<hmi::pub::ObjectAlarmName> alarm;
        };
        InstAlarm instAlarmAt(const hmi::Document* doc, std::uint64_t view28, std::uint64_t sub) {
            InstAlarm r;
            r.view = hmiViewById(doc, view28);
            r.object = hmiItemObject(r.view, sub);
            if (!r.object) return r;
            auto all = hmi::pub::objectAlarmNames(doc->project, *r.view, *r.object);
            if ((sub & 0xFF) < all.size()) r.alarm = std::move(all[sub & 0xFF]);
            return r;
        }
        // La valeur connue dans l'editeur d'un membre d'alarme (celle du volet) : "" sinon.
        std::string instAlarmValue(const hmi::Project& p, const InstAlarm& a, std::string_view member) {
            if (!a.alarm) return {};
            if (member == "Name") return a.alarm->full;
            if (member == "Enabled") return a.alarm->enabled ? "TRUE" : "FALSE";
            if (member == "Priority") return std::to_string(a.alarm->priority);
            if (member == "Message") return a.alarm->message;
            if (member == "Group") {
                const auto link = instLinkedGroup(p, *a.view, *a.object);
                return link.empty() ? a.alarm->group : link;
            }
            return {};
        }
        // 1.10 (chantier O) : la ligne `rank` du noeud Alarmes d'un objet, dans
        // l'ordre de alarmtree::linesOf (ecrit ici : le modele ne lie pas l'IHM).
        // group == &root : le noeud lui-meme ; un autre groupe : celui d'un objet
        // d'une instance ; alarm : une alarme. Rien : hors de la liste.
        struct AlarmLine { const alarmtree::Group* group{nullptr}; const alarmtree::Entry* alarm{nullptr}; };
        AlarmLine alarmLineOf(const alarmtree::Group& root, std::uint64_t rank) {
            if (rank == 0) return {&root, nullptr};
            std::uint64_t k = 1;
            for (const auto& a : root.alarms) { if (k == rank) return {nullptr, &a}; ++k; }
            for (const auto& g : root.groups) {
                if (k == rank) return {&g, nullptr};
                ++k;
                for (const auto& a : g.alarms) { if (k == rank) return {nullptr, &a}; ++k; }
            }
            return {};
        }
        // 1.10 (chantier O, decision 14) : les operateurs du symbole d'une instance (nul : aucun).
        // 1.10.2 (chantier A) : les familles presentes sous un objet (non vides),
        // dans l'ordre de l'arbre (le noeud Alarmes se glisse apres Parametres).
        // 1.10.3 (Q1103) : le meme code que l'explorateur d'objets de la vue (HmiTreeData).
        std::vector<hmitree::Family> objectFamilies(const hmi::Project& p, const hmi::View& v, const hmi::Object& o) {
            return hmitree::objectFamilies(p, v, o);
        }
        const std::vector<hmi::HmiOperator>* objectOperators(const hmi::Document* d, const hmi::Object* o) {
            if (!d || !o) return nullptr;
            const auto* sym = hmi::symbolOf(d->project, *o);
            return sym && !sym->operators.empty() ? &sym->operators : nullptr;
        }
        // 1.11.10 : les fonctions et les popups du symbole d'une instance (vides : aucune).
        const hmi::View* objectSymbol(const hmi::Document* d, const hmi::Object* o) {
            return d && o && o->kind == hmi::Kind::SymbolInstance ? hmi::symbolOf(d->project, *o) : nullptr;
        }
        const std::vector<hmi::HmiFunction>* objectFunctions(const hmi::Document* d, const hmi::Object* o) {
            const auto* sym = objectSymbol(d, o);
            return sym && !sym->functions.empty() ? &sym->functions : nullptr;
        }
        std::vector<const hmi::View*> ownedPopups(const hmi::Document* d, const hmi::View* sym) {
            std::vector<const hmi::View*> out;
            if (d && sym)
                for (const auto& v : d->project.views)
                    if (v.ownerSymbol == sym->id) out.push_back(&v);
            return out;
        }
    }

    ui::NodeId ProjectTreeModel::pack(NodeKind k, Index i, Index sub) {
        return (static_cast<std::uint64_t>(k) << kKindShift)
            | ((static_cast<std::uint64_t>(i) & kMask28) << kIndexShift)
            | (static_cast<std::uint64_t>(sub) & kMask28);
    }
    // La liste derriere un dossier a plat, ou nullptr. Passer par une seule
    // fonction evite d'avoir le meme switch a cinq endroits - et c'est le genre de
    // switch dont on oublie une branche en ajoutant un dossier.
    const std::vector<Index>* ProjectTreeModel::listFor(NodeKind k) const noexcept {
        switch (k) {
        case NodeKind::ElementaryFolder:   return &elementary_;
        case NodeKind::DdtInstanceFolder:  return &ddtInstances_;
        case NodeKind::DfbInstanceFolder:  return &dfbInstances_;
        default:                           return nullptr;
        }
    }

    const std::vector<Index>* ProjectTreeModel::listById(Index id) const noexcept {
        switch (id) {
        case 0:  return &elementary_;
        case 1:  return &ddtInstances_;
        case 2:  return &dfbInstances_;
        default: return nullptr;
        }
    }

    ProjectTreeModel::NodeKind ProjectTreeModel::kindOf(ui::NodeId n) noexcept {
        return static_cast<NodeKind>(n >> kKindShift);
    }
    Index ProjectTreeModel::indexOf(ui::NodeId n) noexcept {
        return static_cast<Index>((n >> kIndexShift) & kMask28);
    }
    Index ProjectTreeModel::subOf(ui::NodeId n) noexcept {
        return static_cast<Index>(n & kMask28);
    }

    ProjectTreeModel::ProjectTreeModel(ProjectRef project) : project_(std::move(project)) {
        rebuildLists();
    }

    // LE CLASSEMENT DES VARIABLES, EN UN SEUL ENDROIT.
    //
    //  Trois listes, et la regle qui les separe est le TYPE, pas le nom : une
    //  instance de DFB est une variable dont le type se resout sur un bloc, une
    //  instance de DDT sur un type derive, le reste est elementaire. Se fier au
    //  prefixe ("ST_", "DFB_") marcherait sur notre bibliotheque et sur aucune
    //  autre.
    //
    //  Un ARRAY compte pour ce que sont ses ELEMENTS : `ARRAY[0..15] OF ST_EQ_Pump`
    //  est un paquet d'instances de DDT, et le ranger avec les BOOL le rendrait
    //  introuvable.
    void ProjectTreeModel::rebuildLists() {
        elementary_.clear();
        ddtInstances_.clear();
        dfbInstances_.clear();
        subroutines_.clear();
        if (!project_) return;

        for (Index i = 0; i < project_->variables.size(); ++i) {
            const auto& v = project_->variables[i];
            // Les parametres et les locales d'un POU sont deja sous leur POU ;
            // les remettre ici ferait lire deux fois la meme chose.
            if (v.scope != VariableScope::Global && v.scope != VariableScope::Constant) continue;

            if (v.type.klass == TypeClass::FunctionBlock || v.type.fbTypeIndex != kNoIndex)
                dfbInstances_.push_back(i);
            else if (v.type.klass == TypeClass::Derived || v.type.derivedIndex != kNoIndex)
                ddtInstances_.push_back(i);
            else
                elementary_.push_back(i);
        }

        // Lot API 7 : une SR lue dans un .XPG porte un POU de genre Section (sa
        // section dit isSubroutine). Elle se voyait sous Unites de programme, qui
        // ne montre plus que les unites : elle est ici, comme dans l'onglet
        // Sous-routines (et toujours sous Taches, par sa tache).
        for (Index i = 0; i < project_->pous.size(); ++i) {
            const auto& pou = project_->pous[i];
            const bool readSr = pou.kind == PouKind::Section && !pou.sections.empty()
                && pou.sections.front() < project_->sections.size() && project_->sections[pou.sections.front()].isSubroutine;
            if (pou.kind == PouKind::SubRoutine || readSr) subroutines_.push_back(i);
        }
    }

    void ProjectTreeModel::refresh() {
        configCounts_.reset();
        rebuildLists();
        // Lot API 7 : les enfants des membres se recalculent (un type a pu
        // changer, une liste a plat a bouge) ; la table des membres reste, et
        // avec elle le NodeId de chacun - ce qui etait deplie le reste.
        memberKids_.clear();
        memberHas_.clear();
    }

    void ProjectTreeModel::setMacros(std::vector<std::string> names) {
        macros_ = std::move(names);
        macroFolderOfMacro_.assign(macros_.size(), std::string{});
        macroFolders_.clear();
    }

    // Lot macros 1 : les dossiers des macros.
    void ProjectTreeModel::setMacroTree(std::vector<std::string> folders,
                                        std::vector<std::pair<std::string, std::string>> macros) {
        macroFolders_ = std::move(folders);
        macros_.clear();
        macroFolderOfMacro_.clear();
        for (auto& [name, folder] : macros) {
            macros_.push_back(std::move(name));
            macroFolderOfMacro_.push_back(std::move(folder));
        }
    }

    std::vector<ui::NodeId> ProjectTreeModel::macroChildren(const std::string& folder) const {
        std::vector<ui::NodeId> out;
        for (std::size_t i = 0; i < macroFolders_.size(); ++i)
            if (project::macro::sameFolder(project::macro::folderParent(macroFolders_[i]), folder))
                out.push_back(pack(NodeKind::MacroSubFolder, static_cast<Index>(i)));
        for (std::size_t i = 0; i < macros_.size(); ++i) {
            const std::string& f = i < macroFolderOfMacro_.size() ? macroFolderOfMacro_[i] : std::string{};
            if (project::macro::sameFolder(f, folder)) out.push_back(pack(NodeKind::Macro, static_cast<Index>(i)));
        }
        return out;
    }

    bool ProjectTreeModel::macroFolderOf(ui::NodeId n, std::string& path) const {
        if (kindOf(n) == NodeKind::MacroFolder) {
            path.clear();
            return true;
        }
        if (kindOf(n) != NodeKind::MacroSubFolder) return false;
        const auto i = indexOf(n);
        if (i >= macroFolders_.size()) return false;
        path = macroFolders_[i];
        return true;
    }

    ui::NodeId ProjectTreeModel::macroFolderNode(const std::string& path) const {
        if (path.empty()) return pack(NodeKind::MacroFolder, 0);
        for (std::size_t i = 0; i < macroFolders_.size(); ++i)
            if (project::macro::sameFolder(macroFolders_[i], path)) return pack(NodeKind::MacroSubFolder, static_cast<Index>(i));
        return ui::kInvalidNode;
    }

    ui::NodeId ProjectTreeModel::macroNode(const std::string& name) const {
        for (std::size_t i = 0; i < macros_.size(); ++i)
            if (macros_[i] == name) return pack(NodeKind::Macro, static_cast<Index>(i));
        return ui::kInvalidNode;
    }

    Index ProjectTreeModel::variableOf(ui::NodeId n) const {
        switch (kindOf(n)) {
        case NodeKind::DerivedField:
        case NodeKind::DfbVariable:
            return indexOf(n);
        default: break;
        }
        // Les feuilles des trois listes portent le RANG dans la liste, pas l'indice
        // de la variable : une liste refaite apres une macro doit designer les
        // nouvelles variables, pas celles qui etaient a la meme place avant.
        if (kindOf(n) != NodeKind::ListVariable) return kNoIndex;
        const auto* list = listById(subOf(n));
        if (list == nullptr) return kNoIndex;
        const auto rank = indexOf(n);
        return rank < list->size() ? (*list)[rank] : kNoIndex;
    }

    Index ProjectTreeModel::subroutineOf(ui::NodeId n) const {
        if (kindOf(n) != NodeKind::Subroutine) return kNoIndex;
        const auto rank = indexOf(n);
        if (rank >= subroutines_.size()) return kNoIndex;
        const auto& pou = project_->pous[subroutines_[rank]];
        // C'est la SECTION qu'on ouvre : une sous-routine n'a pas d'editeur a elle,
        // elle s'edite comme n'importe quelle section.
        return pou.sections.empty() ? kNoIndex : pou.sections.front();
    }

    namespace {
        VariableScope scopeForFolder(ProjectTreeModel::NodeKind k) {
            using NK = ProjectTreeModel::NodeKind;
            switch (k) {
            case NK::DfbInputs:      return VariableScope::Input;
            case NK::DfbOutputs:     return VariableScope::Output;
            case NK::DfbInOut:       return VariableScope::InOut;
            case NK::DfbPublicVars:  return VariableScope::Public;
            default:                 return VariableScope::Local;   // "private" in Control Expert
            }
        }
    } // namespace

    std::vector<ProjectTreeModel::NodeKind> ProjectTreeModel::scopeFolders(Index pouIndex) const {
        using NK = NodeKind;
        std::vector<NK> out;
        if (pouIndex >= project_->pous.size()) return out;

        for (auto k : { NK::DfbInputs, NK::DfbOutputs, NK::DfbInOut,
                       NK::DfbPublicVars, NK::DfbPrivateVars })
            if (!folderMembers(k, pouIndex).empty()) out.push_back(k);
        return out;
    }

    std::vector<ProjectTreeModel::NodeKind> ProjectTreeModel::folderKinds(Index pouIndex) const {
        auto out = scopeFolders(pouIndex);
        if (pouIndex < project_->pous.size() && !project_->pous[pouIndex].sections.empty())
            out.push_back(NodeKind::DfbSectionsFolder);
        return out;
    }

    std::vector<Index> ProjectTreeModel::folderMembers(NodeKind k, Index pouIndex) const {
        std::vector<Index> out;
        if (pouIndex >= project_->pous.size()) return out;
        const auto& pou = project_->pous[pouIndex];
        const auto want = scopeForFolder(k);
        const auto count = project_->variables.size();

        // Parameters and locals live in two lists on the POU; the scope on each
        // variable is what actually distinguishes them.
        for (auto vi : pou.parameters)
            if (vi < count && project_->variables[vi].scope == want) out.push_back(vi);
        for (auto vi : pou.locals)
            if (vi < count && project_->variables[vi].scope == want) out.push_back(vi);
        return out;
    }

    ui::NodeId ProjectTreeModel::root() const { return pack(NodeKind::Root, 0); }

    std::size_t ProjectTreeModel::childCount(ui::NodeId n) const {
        // ---- Lot API 8 : l'arbre du projet (les resultats du filtre, a la fin du dossier de leur domaine) ----
        if (kindOf(n) == NodeKind::FilterHit) return 0;
        if (!filterHits_.empty() && !hitGuard_) {
            const auto extra = hitsUnder(n).size();
            if (extra > 0) {
                hitGuard_ = true;
                const auto base = childCount(n);
                hitGuard_ = false;
                return base + extra;
            }
        }
        // Les outils sortis de l'arbre : IHM = la rangee + 8 dossiers (API garde
        // 11 : la rangee + ses 10 dossiers ; Statistiques est un bouton).
        if (kindOf(n) == NodeKind::ToolRow || kindOf(n) == NodeKind::VersionsMore) return 0;
        if (kindOf(n) == NodeKind::HmiFolder) return hmi_ ? 9 : 0;
        // Les versions en bref : le travail en cours, les 5 dernieres, "Voir les N versions...".
        if (kindOf(n) == NodeKind::VersionsFolder && briefVersions()) return kBriefVersions + 2;
        // Epingles et Recents : en haut de la racine, s'ils ne sont pas vides.
        if (kindOf(n) == NodeKind::PinsFolder) return pins_.size();
        if (kindOf(n) == NodeKind::RecentFolder) return recents_.size();
        if (kindOf(n) == NodeKind::PinItem || kindOf(n) == NodeKind::RecentItem) return 0;
        // 2e partie : la portee du rail - la racine ne montre que ce domaine (Epingles : ses deux dossiers).
        if (kindOf(n) == NodeKind::Root && hmi_ && scope_ != 0 && !rootGuard_)
            return scope_ == 1 ? shortcutFolders() : 1;
        if (kindOf(n) == NodeKind::Root && !rootGuard_ && shortcutFolders() > 0) {
            rootGuard_ = true;
            const auto base = childCount(n);
            rootGuard_ = false;
            return shortcutFolders() + base;
        }
        // ---- fin Lot API 8 : l'arbre du projet ----
        const auto i = indexOf(n);
        switch (kindOf(n)) {
        // Lot 10 : avec l'IHM, deux dossiers au meme niveau (API, IHM).
        // Lot API 8 : API, IHM, Simulation, Versions ; sans IHM : les dix dossiers, puis Simulation.
        case NodeKind::Root:                return hmi_ ? 4 : 11;       // lot 21 : API, IHM, Versions
        case NodeKind::VersionsFolder:      return versions_.size();
        case NodeKind::VersionItem:         return 0;
        // Lot API 7 : + Simulation, Statistiques ; lot API 8 : Simulation part dans son dossier.
        case NodeKind::ApiFolder:           return 11;
        // ---- Lot API 8 : Centre de simulation ----
        case NodeKind::SimFolder:           return hmi_ ? 8 : 6;        // sans IHM : ni IHM ni Equipements
        case NodeKind::SimOverview:
        case NodeKind::SimEquipment:
        case NodeKind::SimDebug:
        case NodeKind::SimForcing:
        case NodeKind::SimTrends:
        case NodeKind::SimJournal:          return 0;
        // ---- fin Lot API 8 ----
        case NodeKind::ApiSimulation:
        case NodeKind::ApiStatistics:       return 0;
        // Lot API 4 : Processeur, Racks et modules, Voies et adresses, Reseau,
        // Plan memoire. Les taches ont leur dossier (Taches) : plus de doublon ici.
        case NodeKind::ConfigurationFolder: return 5;
        case NodeKind::ApiChannels:
        case NodeKind::ApiNetwork:
        case NodeKind::ApiMemory:           return 0;
        case NodeKind::RackFolder:          return project_->hardware.racks.size();
        case NodeKind::Rack:
            return i < project_->hardware.racks.size() ? project_->hardware.racks[i].modules.size() : 0;
        case NodeKind::TaskFolder:          return project_->tasks.size();
        case NodeKind::TypesFolder:         return project_->derivedTypes.size();
        case NodeKind::DerivedType:         return project_->derivedTypes[i].fields.size();
        case NodeKind::DfbFolder: {
            std::size_t count = 0;
            for (const auto& pou : project_->pous) count += (pou.kind == PouKind::FunctionBlockType);
            return count;
        }
        case NodeKind::DfbType:             return folderKinds(i).size();
        // Lot API 7 : comme un DFB, ses dossiers de portee non vides (Entrees,
        // Sorties, Entrees / sorties, publiques, privees), puis ses sections.
        case NodeKind::ProgramUnit:
            return i < project_->pous.size() ? scopeFolders(i).size() + project_->pous[i].sections.size() : 0u;
        // Lot API 7 : une variable ou un membre, sur ses membres (MemberTree).
        case NodeKind::DerivedField:
        case NodeKind::DfbVariable:
        case NodeKind::ListVariable:
        case NodeKind::MemberNode:          return memberChildren(n).size();
        case NodeKind::DfbInputs:
        case NodeKind::DfbOutputs:
        case NodeKind::DfbInOut:
        case NodeKind::DfbPublicVars:
        case NodeKind::DfbPrivateVars:      return folderMembers(kindOf(n), i).size();
        case NodeKind::DfbSectionsFolder:   return project_->pous[i].sections.size();
        case NodeKind::UnitsFolder: {
            // Lot API 7 : les unites de programme SEULES (voir childAt).
            std::size_t count = 0;
            for (const auto& pou : project_->pous) count += (pou.kind == PouKind::ProgramUnit);
            return count;
        }
        case NodeKind::Task:                return project_->tasks[i].sections.size();

            // Avec une seule tache, le dossier porte directement les etapes : un
            // niveau qui n'a jamais qu'un enfant est un clic de plus pour rien.
        case NodeKind::ExecOrderFolder:
            if (project_->tasks.size() > 1) return project_->tasks.size();
            return project_->tasks.empty()
                ? 0u
                : domain::executionOrder(*project_, project_->tasks[0].name).size();
        case NodeKind::ExecOrderTask:
            return i < project_->tasks.size()
                ? domain::executionOrder(*project_, project_->tasks[i].name).size()
                : 0u;

        case NodeKind::TablesFolder:        return project_->animationTables.size();
        case NodeKind::AnimationTable:      return 0;     // lot API 3 : une table s'ouvre dans son onglet

        case NodeKind::VariablesFolder:     return 3;
        case NodeKind::ElementaryFolder:
        case NodeKind::DdtInstanceFolder:
        case NodeKind::DfbInstanceFolder: {
            const auto* list = listFor(kindOf(n));
            return list ? list->size() : 0;
        }
        case NodeKind::SubroutinesFolder:   return subroutines_.size();
        case NodeKind::MacroFolder:         return macroChildren({}).size();
        case NodeKind::MacroSubFolder:
            return i < macroFolders_.size() ? macroChildren(macroFolders_[i]).size() : 0;   // lot macros 1

        // Lot 10 : + Symboles ; lot 12 : + Styles, Rechercher ; lot 13 : + Essais ; lot 15 : + Outil
        // Modbus ; lot API 8 : - Simulation (dans le dossier Simulation).
        case NodeKind::HmiFolder:           return hmi_ ? 13 : 0;
        case NodeKind::HmiSymbolsFolder:
            return hmi_ ? hmilists::childrenOf(hmi_->project, hmi::fold::List::Symbols, {}).size() : 0;   // lot 21 : ses dossiers
        case NodeKind::HmiConfig:           return hmi_ ? 11 : 0;  // lot 13 : + Langues, Unites et formats ; lot 14 : + Communication, Poste, Notifications, Rapports, Acces web
        case NodeKind::HmiViews:            return hmi_ ? 3 : 0;   // lot 8 : Modeles, Vues, Popups
        case NodeKind::HmiViewFolder:
            if (!hmi_) return 0;
            if (i == 0) return 3;                                    // ecrans modeles, en-tetes, pieds
            return hmilists::childrenOf(hmi_->project, i == 1 ? hmi::fold::List::Views : hmi::fold::List::Popups, {}).size();
        case NodeKind::HmiTemplateFolder:
            return hmi_ ? hmilists::childrenOf(hmi_->project, *hmilists::listOfNode(NodeKind::HmiTemplateFolder, i), {}).size() : 0;
        // Lot 21 : un dossier d'une liste - ses sous-dossiers, puis ses elements.
        case NodeKind::HmiListFolder: {
            if (!hmi_) return 0;
            const auto list = static_cast<hmi::fold::List>(subOf(n));
            const auto all = hmi::fold::allFolders(hmi_->project, list);
            return i < all.size() ? hmilists::childrenOf(hmi_->project, list, all[i]).size() : 0;
        }
        case NodeKind::HmiView: {
            // 1.11.10 : un symbole a en plus ses Fonctions et ses Popups.
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v) return 0;
            return v->role == "symbole" ? static_cast<std::size_t>(HmiPart::Count) : static_cast<std::size_t>(HmiPart::Functions);
        }

        // ---- lot 5 : ce qui se deballe ----------------------------------------
        case NodeKind::HmiViewPart: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v) return 0;
            switch (static_cast<HmiPart>(subOf(n))) {
            case HmiPart::Objects:    return hmitree::childrenOf(*v, hmi::kNoId).size();
            case HmiPart::Scripts:    return v->scripts.size();
            case HmiPart::Animations: return hmitree::animationsOf(*v).size();
            case HmiPart::Layers:     return v->layers.size();
            case HmiPart::Groups:
                return static_cast<std::size_t>(std::count_if(v->objects.begin(), v->objects.end(),
                    [](const hmi::Object& o) { return o.kind == hmi::Kind::Group; }));
            case HmiPart::Functions:  return v->functions.size();                 // 1.11.10
            case HmiPart::Popups:     return ownedPopups(hmi_.get(), v).size();
            case HmiPart::Count:      break;
            }
            return 0;
        }
        case NodeKind::HmiObject: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            // 1.10.2 (chantier A) : ses familles (Actions, Liens fx, Parametres, Animations,
            // Elements, Securite), le noeud Alarmes parmi elles, puis Operateurs.
            return o ? objectFamilies(hmi_->project, *v, *o).size()
                           + (objectAlarms(v->id, o->id) ? 1u : 0u)                // 1.10 : son noeud Alarmes
                           + (objectOperators(hmi_.get(), o) ? 1u : 0u)             // 1.10 : puis Operateurs
                           + (objectFunctions(hmi_.get(), o) ? 1u : 0u)             // 1.11.10 : puis Fonctions
                           + (ownedPopups(hmi_.get(), objectSymbol(hmi_.get(), o)).empty() ? 0u : 1u)   //  et Popups
                     : 0;
        }
        case NodeKind::HmiObjectOperators: {
            const auto* ops = objectOperators(hmi_.get(), hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n)));
            return ops ? ops->size() : 0;
        }
        case NodeKind::HmiObjectFunctions: {                 // 1.11.10
            const auto* fns = objectFunctions(hmi_.get(), hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n)));
            return fns ? fns->size() : 0;
        }
        case NodeKind::HmiObjectPopups:
            return ownedPopups(hmi_.get(), objectSymbol(hmi_.get(), hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n)))).size();
        case NodeKind::HmiObjectFunction:
        case NodeKind::HmiSymbolFunction: return 0;
        // ---- 1.10.2 (chantier A) : une famille de l'objet ----
        case NodeKind::HmiObjectFamily: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            const auto f = static_cast<hmitree::Family>(subOf(n) & 0xFF);
            // 1.10.3 (Q1103) : sous les reperes, la ligne d'aide (Ctrl+D).
            if (o && f == hmitree::Family::Markers) return hmitree::familyLines(hmi_->project, *o, f).size();
            return o && f < hmitree::Family::Count ? hmitree::familySize(hmi_->project, *v, *o, f) : 0;
        }
        case NodeKind::HmiObjectParam:
        case NodeKind::HmiObjectMarker: return 0;
        // ---- 1.10 (chantier O) : le noeud Alarmes d'un objet, ses lignes ----
        case NodeKind::HmiObjectAlarms: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            const auto g = o ? objectAlarms(v->id, o->id) : nullptr;
            return g ? g->alarms.size() + g->groups.size() : 0;
        }
        case NodeKind::HmiObjectAlarm: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            const auto g = o ? objectAlarms(v->id, o->id) : nullptr;
            const auto line = g ? alarmLineOf(*g, subOf(n) & 0xFF) : AlarmLine{};
            return line.group && line.group != g.get() ? line.group->alarms.size() : 0;
        }
        // ---- 1.10 (chantier O, decision 15) : sous un type IHM, ses valeurs puis ses operateurs ----
        case NodeKind::HmiTypeNode: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            if (!ty) return 0;
            return (ty->kind == hmi::HmiTypeKind::Enumeration ? 1u : 0u) + (ty->operators.empty() ? 0u : 1u);
        }
        case NodeKind::HmiTypeValues: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            return ty ? ty->values.size() : 0;
        }
        case NodeKind::HmiTypeOperators: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            return ty ? ty->operators.size() : 0;
        }
        case NodeKind::HmiAlarms:     return hmi_ ? hmitree::alarmGroups(hmi_->project).size() : 0;
        case NodeKind::HmiAlarmGroup: {
            if (!hmi_) return 0;
            const auto groups = hmitree::alarmGroups(hmi_->project);
            return i < groups.size() ? hmitree::alarmsOf(hmi_->project, groups[i]).size() : 0;
        }
        case NodeKind::HmiRecipes:    return hmi_ ? hmi_->project.recipes.size() : 0;
        case NodeKind::HmiRecipe: {
            const auto* r = hmi_ ? byId(hmi_->project.recipes, i) : nullptr;
            return r ? r->records.size() : 0;
        }
        case NodeKind::HmiUsers:
            return hmi_ ? hmi_->project.security.groups.size() + hmitree::usersWithoutGroup(hmi_->project).size() + 1 : 0;
        case NodeKind::HmiUserGroup: {
            // Les groupes par defaut ont des identifiants hauts (0xFFFFFE01...) :
            // le noeud n'en garde que 28 bits, on retrouve le groupe par eux.
            const auto* g = hmi_ ? byId(hmi_->project.security.groups, i) : nullptr;
            return g ? hmitree::usersOf(hmi_->project, g->id).size() : 0;
        }
        case NodeKind::HmiRoles:      return hmi_ ? hmi_->project.security.roles.size() : 0;
        case NodeKind::HmiScripts:    return hmi_ ? 7 : 0;   // lot 9 : + variables systeme, d'instances ; lot 16 : + types IHM
        case NodeKind::HmiScriptsFolder:   return hmi_ ? hmilists::childrenOf(hmi_->project, hmi::fold::List::Scripts, {}).size() : 0;
        case NodeKind::HmiFunctionsFolder: return hmi_ ? hmi_->project.programs.functions.size() : 0;
        case NodeKind::HmiTypesFolder:     return hmi_ ? hmilists::childrenOf(hmi_->project, hmi::fold::List::Types, {}).size() : 0;   // lot 16, 21
        case NodeKind::HmiVariablesFolder: return hmi_ ? hmivars::childrenOf(hmi_->project, {}).size() : 0;
        case NodeKind::HmiVarFolder: {
            if (!hmi_) return 0;
            const auto all = hmi::types::allFolders(hmi_->project);
            return i < all.size() ? hmivars::childrenOf(hmi_->project, all[i]).size() : 0;
        }
        case NodeKind::HmiUsedFolder:      return hmi_ ? usedVariables().size() : 0;
        // ---- lot 9
        case NodeKind::HmiSysFolder:  return hmi_ ? hmi::pub::kSysDomainCount : 0;
        case NodeKind::HmiSysDomain:  return hmi_ ? hmi::pub::sysVarsOf(static_cast<int>(i)).size() : 0;
        case NodeKind::HmiInstFolder: return hmi_ ? hmi_->project.views.size() : 0;
        case NodeKind::HmiInstView: {
            const auto* v = hmiViewById(hmi_.get(), i);
            return v ? 1 + v->objects.size() : 0;
        }
        case NodeKind::HmiInstViewInfo: return hmiViewById(hmi_.get(), i) ? std::size(hmi::pub::kViewInfo) : 0;
        case NodeKind::HmiInstObject: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            return o ? instPartsOf(hmi_->project, *v, *o).count() : 0;   // 1.11.1 (decision 108)
        }
        // ---- 1.11.1 (decision 108) : le groupe d'alarmes, les alarmes d'un objet ----
        case NodeKind::HmiInstAlarmGroup: {
            const auto* o = hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n));
            return o && hmi::pub::hasAlarmGroup(*o) ? std::size(hmi::pub::kAlarmInfo) : 0;
        }
        case NodeKind::HmiInstAlarms: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            return o ? std::min<std::size_t>(hmi::pub::objectAlarmNames(hmi_->project, *v, *o).size(), 256) : 0;
        }
        case NodeKind::HmiInstAlarm:
            return instAlarmAt(hmi_.get(), i, subOf(n)).alarm ? std::size(hmi::pub::kAlarmMembers) : 0;

        default:                            return 0;
        }
    }

    ui::NodeId ProjectTreeModel::childAt(ui::NodeId n, std::size_t k) const {
        // ---- Lot API 8 : l'arbre du projet (les resultats du filtre, apres les enfants du dossier) ----
        if (!filterHits_.empty() && !hitGuard_) {
            const auto hits = hitsUnder(n);
            if (!hits.empty()) {
                hitGuard_ = true;
                const auto base = childCount(n);
                hitGuard_ = false;
                if (k >= base) return k - base < hits.size() ? pack(NodeKind::FilterHit, hits[k - base]) : ui::kInvalidNode;
            }
        }
        // Les outils sortis de l'arbre : la rangee de boutons en tete d'API et
        // d'IHM ; API > Statistiques, IHM > Exporter / Importer, Rechercher,
        // Outil Modbus, Generer, Compiler n'y sont plus.
        if (kindOf(n) == NodeKind::PinsFolder) return k < pins_.size() ? pack(NodeKind::PinItem, static_cast<Index>(k)) : ui::kInvalidNode;
        if (kindOf(n) == NodeKind::RecentFolder) return k < recents_.size() ? pack(NodeKind::RecentItem, static_cast<Index>(k)) : ui::kInvalidNode;
        if (kindOf(n) == NodeKind::Root && hmi_ && scope_ > 1 && !rootGuard_) {   // 2e partie : la portee du rail
            static constexpr NodeKind heads[] = {NodeKind::ApiFolder, NodeKind::HmiFolder, NodeKind::SimFolder, NodeKind::VersionsFolder};
            return k == 0 && scope_ <= 5 ? pack(heads[scope_ - 2], 0) : ui::kInvalidNode;
        }
        if (kindOf(n) == NodeKind::Root && !rootGuard_) {          // Epingles, Recents, puis la racine d'avant
            if (!pins_.empty()) {
                if (k == 0) return pinsFolderNode();
                --k;
            }
            if (!recents_.empty()) {
                if (k == 0) return recentsFolderNode();
                --k;
            }
        }
        if (kindOf(n) == NodeKind::ApiFolder) {
            if (k == 0) return pack(NodeKind::ToolRow, 0);
            if (--k >= 10) return ui::kInvalidNode;          // puis les 10 dossiers, comme avant
        } else if (kindOf(n) == NodeKind::HmiFolder) {
            static constexpr NodeKind order[] = {
                NodeKind::ToolRow, NodeKind::HmiConfig, NodeKind::HmiExternalFiles, NodeKind::HmiResources, NodeKind::HmiViews,
                NodeKind::HmiSymbolsFolder, NodeKind::HmiStyles, NodeKind::HmiTests, NodeKind::HmiScripts};
            if (!hmi_ || k >= std::size(order)) return ui::kInvalidNode;
            return pack(order[k], static_cast<Index>(order[k] == NodeKind::ToolRow ? 1u : 0u));
        } else if (kindOf(n) == NodeKind::VersionsFolder && briefVersions()) {
            if (k <= kBriefVersions) return pack(NodeKind::VersionItem, static_cast<Index>(k));
            return k == kBriefVersions + 1 ? pack(NodeKind::VersionsMore, 0) : ui::kInvalidNode;
        }
        // ---- fin Lot API 8 : l'arbre du projet ----
        const auto i = indexOf(n);
        switch (kindOf(n)) {
        case NodeKind::Root:
            // Lot 10 : l'automate et l'IHM, cote a cote. Lot API 8 : la
            // simulation entre l'IHM et les versions.
            if (hmi_) return k == 0 ? pack(NodeKind::ApiFolder, 0) : k == 1 ? pack(NodeKind::HmiFolder, 0)
                           : k == 2 ? pack(NodeKind::SimFolder, 0)
                           : k == 3 ? pack(NodeKind::VersionsFolder, 0) : ui::kInvalidNode;
            // Lot API 8 : sans IHM, le dossier Simulation apres les dix dossiers de l'automate.
            if (k == 10) return pack(NodeKind::SimFolder, 0);
            [[fallthrough]];
        case NodeKind::ApiFolder:
            switch (k) {
            case 0: return pack(NodeKind::ConfigurationFolder, 0);
            case 1: return pack(NodeKind::TypesFolder, 0);
            case 2: return pack(NodeKind::DfbFolder, 0);
            case 3: return pack(NodeKind::UnitsFolder, 0);
            case 4: return pack(NodeKind::ExecOrderFolder, 0);
            case 5: return pack(NodeKind::TaskFolder, 0);
            case 6: return pack(NodeKind::VariablesFolder, 0);
            case 7: return pack(NodeKind::SubroutinesFolder, 0);
            case 8: return pack(NodeKind::MacroFolder, 0);
            case 9: return pack(NodeKind::TablesFolder, 0);
            // Lot API 8 : Simulation est partie dans le dossier Simulation (Automate).
            case 10: return pack(NodeKind::ApiStatistics, 0);
            default: return ui::kInvalidNode;
            }
        case NodeKind::ConfigurationFolder:
            switch (k) {
            case 0: return pack(NodeKind::Cpu, 0);
            case 1: return pack(NodeKind::RackFolder, 0);
            case 2: return pack(NodeKind::ApiChannels, 0);
            case 3: return pack(NodeKind::ApiNetwork, 0);
            case 4: return pack(NodeKind::ApiMemory, 0);
            default: return ui::kInvalidNode;
            }
        case NodeKind::RackFolder: return pack(NodeKind::Rack, static_cast<Index>(k));
        case NodeKind::Rack:       return pack(NodeKind::HwModule, i, static_cast<Index>(k));
        case NodeKind::TaskFolder:   return pack(NodeKind::Task, static_cast<Index>(k));
        case NodeKind::TypesFolder:  return pack(NodeKind::DerivedType, static_cast<Index>(k));
        case NodeKind::DerivedType:
            return pack(NodeKind::DerivedField, project_->derivedTypes[i].fields[k]);
        case NodeKind::DfbFolder: {
            std::size_t seen = 0;
            for (Index p = 0; p < project_->pous.size(); ++p)
                if (project_->pous[p].kind == PouKind::FunctionBlockType && seen++ == k)
                    return pack(NodeKind::DfbType, p);
            return ui::kInvalidNode;
        }
        case NodeKind::UnitsFolder: {
            // Lot API 7 : LES UNITES DE PROGRAMME SEULES. Une section de tache
            // porte un POU a elle (PouKind::Section, pour que l'arbre soit
            // uniforme) : la compter ici faisait lire "Unites de programme (65)"
            // pour trois unites. Elle reste sous Taches > MAST et sous Ordre
            // d'execution, ou elle tourne.
            std::size_t seen = 0;
            for (Index p = 0; p < project_->pous.size(); ++p)
                if (project_->pous[p].kind == PouKind::ProgramUnit && seen++ == k)
                    return pack(NodeKind::ProgramUnit, p);
            return ui::kInvalidNode;
        }
        case NodeKind::DfbType: {
            const auto folders = folderKinds(i);
            return k < folders.size() ? pack(folders[k], i) : ui::kInvalidNode;
        }
        case NodeKind::DfbInputs:
        case NodeKind::DfbOutputs:
        case NodeKind::DfbInOut:
        case NodeKind::DfbPublicVars:
        case NodeKind::DfbPrivateVars: {
            const auto members = folderMembers(kindOf(n), i);
            return k < members.size() ? pack(NodeKind::DfbVariable, members[k]) : ui::kInvalidNode;
        }
        case NodeKind::DfbSectionsFolder:
            return pack(NodeKind::DfbSection, project_->pous[i].sections[k]);
        case NodeKind::ProgramUnit: {
            // Lot API 7 : ses dossiers de portee, comme un DFB (les parametres
            // Entrees / sorties ne se voyaient nulle part), puis ses sections
            // DIRECTEMENT dessous, sans dossier Sections.
            if (i >= project_->pous.size()) return ui::kInvalidNode;
            const auto folders = scopeFolders(i);
            if (k < folders.size()) return pack(folders[k], i);
            const auto s = k - folders.size();
            return s < project_->pous[i].sections.size()
                ? pack(NodeKind::Section, project_->pous[i].sections[s])
                : ui::kInvalidNode;
        }
        // Lot API 7 : une variable ou un membre, sur ses membres.
        case NodeKind::DerivedField:
        case NodeKind::DfbVariable:
        case NodeKind::ListVariable:
        case NodeKind::MemberNode: {
            const auto& kids = memberChildren(n);
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::Task:
            return pack(NodeKind::Section, project_->tasks[i].sections[k]);

        case NodeKind::ExecOrderFolder:
            if (project_->tasks.size() > 1)
                return pack(NodeKind::ExecOrderTask, static_cast<Index>(k));
            return project_->tasks.empty()
                ? ui::kInvalidNode
                : pack(NodeKind::ExecStep, 0, static_cast<Index>(k));
        case NodeKind::ExecOrderTask:
            return pack(NodeKind::ExecStep, i, static_cast<Index>(k));

        case NodeKind::TablesFolder: return pack(NodeKind::AnimationTable, static_cast<Index>(k));

        case NodeKind::VariablesFolder:
            return k == 0 ? pack(NodeKind::ElementaryFolder, 0)
                : k == 1 ? pack(NodeKind::DdtInstanceFolder, 0)
                : pack(NodeKind::DfbInstanceFolder, 0);
            // Le RANG dans la liste, pas l'indice de la variable : c'est ce qui
            // permet a refresh() de changer le contenu sans invalider les noeuds.
        case NodeKind::ElementaryFolder:
            return pack(NodeKind::ListVariable, static_cast<Index>(k), 0);
        case NodeKind::DdtInstanceFolder:
            return pack(NodeKind::ListVariable, static_cast<Index>(k), 1);
        case NodeKind::DfbInstanceFolder:
            return pack(NodeKind::ListVariable, static_cast<Index>(k), 2);
        case NodeKind::SubroutinesFolder:
            return pack(NodeKind::Subroutine, static_cast<Index>(k));
        case NodeKind::MacroFolder: {
            // Lot macros 1 : ses dossiers d'abord, puis les macros de la racine.
            const auto children = macroChildren({});
            return k < children.size() ? children[k] : ui::kInvalidNode;
        }
        case NodeKind::MacroSubFolder: {
            if (i >= macroFolders_.size()) return ui::kInvalidNode;
            const auto children = macroChildren(macroFolders_[i]);
            return k < children.size() ? children[k] : ui::kInvalidNode;
        }
        // Lot 21 : les versions.
        case NodeKind::VersionsFolder:
            return k < versions_.size() ? pack(NodeKind::VersionItem, static_cast<Index>(k)) : ui::kInvalidNode;

        case NodeKind::HmiFolder: {
            // Lot API 8 : HmiSimulation est partie dans le dossier Simulation (IHM).
            static constexpr NodeKind order[] = {
                NodeKind::HmiConfig, NodeKind::HmiExternalFiles, NodeKind::HmiResources,
                NodeKind::HmiExchange, NodeKind::HmiViews, NodeKind::HmiSymbolsFolder, NodeKind::HmiStyles, NodeKind::HmiFind,
                NodeKind::HmiModbusTool,   // lot 15 : l'outil Modbus
                NodeKind::HmiTests, NodeKind::HmiGenerate, NodeKind::HmiCompile, NodeKind::HmiScripts};
            return k < std::size(order) ? pack(order[k], 0) : ui::kInvalidNode;
        }
        // ---- Lot API 8 : Centre de simulation ----
        case NodeKind::SimFolder: {
            static constexpr NodeKind order[] = {
                NodeKind::SimOverview, NodeKind::ApiSimulation, NodeKind::HmiSimulation, NodeKind::SimEquipment,
                NodeKind::SimDebug, NodeKind::SimForcing, NodeKind::SimTrends, NodeKind::SimJournal};
            // Sans IHM : ni IHM ni Equipements.
            static constexpr NodeKind plcOnly[] = {
                NodeKind::SimOverview, NodeKind::ApiSimulation, NodeKind::SimDebug,
                NodeKind::SimForcing, NodeKind::SimTrends, NodeKind::SimJournal};
            if (!hmi_) return k < std::size(plcOnly) ? pack(plcOnly[k], 0) : ui::kInvalidNode;
            return k < std::size(order) ? pack(order[k], 0) : ui::kInvalidNode;
        }
        // ---- fin Lot API 8 ----
        case NodeKind::HmiConfig: {
            static constexpr NodeKind sub[] = {NodeKind::HmiAlarms, NodeKind::HmiRecipes, NodeKind::HmiUsers,
                                               NodeKind::HmiHistory, NodeKind::HmiLanguages,      // lot 13 : Langues,
                                               NodeKind::HmiUnits,                                // Unites et formats
                                               NodeKind::HmiComm,                                 // lot 14 : Communication,
                                               NodeKind::HmiStation,                              // Poste d'exploitation,
                                               NodeKind::HmiNotify, NodeKind::HmiReports,         // Notifications, Rapports,
                                               NodeKind::HmiWeb};                                 // Acces web
            return k < std::size(sub) ? pack(sub[k], 0) : ui::kInvalidNode;
        }
        case NodeKind::HmiViews:
            return hmi_ && k < 3 ? pack(NodeKind::HmiViewFolder, static_cast<Index>(k)) : ui::kInvalidNode;
        case NodeKind::HmiViewFolder: {
            if (!hmi_) return ui::kInvalidNode;
            if (i == 0) return k < 3 ? pack(NodeKind::HmiTemplateFolder, static_cast<Index>(k)) : ui::kInvalidNode;
            const auto kids = hmilists::childrenOf(hmi_->project, i == 1 ? hmi::fold::List::Views : hmi::fold::List::Popups, {});
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::HmiTemplateFolder: {
            if (!hmi_) return ui::kInvalidNode;
            const auto kids = hmilists::childrenOf(hmi_->project, *hmilists::listOfNode(NodeKind::HmiTemplateFolder, i), {});
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::HmiSymbolsFolder: {
            if (!hmi_) return ui::kInvalidNode;
            const auto kids = hmilists::childrenOf(hmi_->project, hmi::fold::List::Symbols, {});
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::HmiListFolder: {                 // lot 21
            if (!hmi_) return ui::kInvalidNode;
            const auto list = static_cast<hmi::fold::List>(subOf(n));
            const auto all = hmi::fold::allFolders(hmi_->project, list);
            if (i >= all.size()) return ui::kInvalidNode;
            const auto kids = hmilists::childrenOf(hmi_->project, list, all[i]);
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::HmiView:
            return k < childCount(n) ? pack(NodeKind::HmiViewPart, i, static_cast<Index>(k)) : ui::kInvalidNode;

        // ---- lot 5 ------------------------------------------------------------
        case NodeKind::HmiViewPart: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v) return ui::kInvalidNode;
            switch (static_cast<HmiPart>(subOf(n))) {
            case HmiPart::Objects: {
                const auto top = hmitree::childrenOf(*v, hmi::kNoId);
                return k < top.size() ? pack(NodeKind::HmiObject, i, static_cast<Index>(top[k]->id & kMask28))
                                      : ui::kInvalidNode;
            }
            case HmiPart::Scripts:
                return k < v->scripts.size() ? pack(NodeKind::HmiViewScript, i, static_cast<Index>(k)) : ui::kInvalidNode;
            case HmiPart::Animations:
                return k < hmitree::animationsOf(*v).size() ? pack(NodeKind::HmiAnimation, i, static_cast<Index>(k))
                                                            : ui::kInvalidNode;
            case HmiPart::Layers:
                return k < v->layers.size() ? pack(NodeKind::HmiLayer, i, static_cast<Index>(k)) : ui::kInvalidNode;
            case HmiPart::Groups: {
                std::size_t seen = 0;
                for (const auto& o : v->objects)
                    if (o.kind == hmi::Kind::Group && seen++ == k)
                        return pack(NodeKind::HmiGroupEntry, i, static_cast<Index>(o.id & kMask28));
                return ui::kInvalidNode;
            }
            case HmiPart::Functions:                                            // 1.11.10
                return k < v->functions.size() ? pack(NodeKind::HmiSymbolFunction, i, static_cast<Index>(k)) : ui::kInvalidNode;
            case HmiPart::Popups: {
                const auto pops = ownedPopups(hmi_.get(), v);
                return k < pops.size() ? pack(NodeKind::HmiView, static_cast<Index>(pops[k]->id & kMask28)) : ui::kInvalidNode;
            }
            case HmiPart::Count: break;
            }
            return ui::kInvalidNode;
        }
        case NodeKind::HmiObject: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            if (!o) return ui::kInvalidNode;
            // 1.10.2 (chantier A) : Actions, Liens fx, Parametres, [Alarmes], Animations,
            // Elements, Securite (les familles non vides), puis [Operateurs].
            const auto fams = objectFamilies(hmi_->project, *v, *o);
            const auto before = static_cast<std::size_t>(std::count_if(fams.begin(), fams.end(),
                [](hmitree::Family f) { return f <= hmitree::Family::Params; }));
            const std::size_t alarms = objectAlarms(v->id, o->id) ? 1u : 0u;
            const auto family = [&](hmitree::Family f) {
                return pack(NodeKind::HmiObjectFamily, i,
                            static_cast<Index>(((o->id & ((1ull << (28 - kItemBits)) - 1)) << kItemBits) | static_cast<unsigned>(f)));
            };
            if (k < before) return family(fams[k]);
            if (k == before && alarms)                                          // 1.10 : le noeud Alarmes
                return pack(NodeKind::HmiObjectAlarms, i, static_cast<Index>(o->id & kMask28));
            if (k - alarms < fams.size()) return family(fams[k - alarms]);
            std::size_t at = k - alarms - fams.size();
            if (objectOperators(hmi_.get(), o)) {                              // 1.10 : puis Operateurs
                if (at == 0) return pack(NodeKind::HmiObjectOperators, i, static_cast<Index>(o->id & kMask28));
                --at;
            }
            if (objectFunctions(hmi_.get(), o)) {                              // 1.11.10 : Fonctions
                if (at == 0) return pack(NodeKind::HmiObjectFunctions, i, static_cast<Index>(o->id & kMask28));
                --at;
            }
            if (at == 0 && !ownedPopups(hmi_.get(), objectSymbol(hmi_.get(), o)).empty())   //  et Popups
                return pack(NodeKind::HmiObjectPopups, i, static_cast<Index>(o->id & kMask28));
            return ui::kInvalidNode;
        }
        // ---- 1.10.2 (chantier A) : les lignes d'une famille ----
        case NodeKind::HmiObjectFamily: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            if (!o) return ui::kInvalidNode;
            const auto f = static_cast<hmitree::Family>(subOf(n) & 0xFF);
            const auto item = [&](std::size_t r) {
                return static_cast<Index>(((o->id & ((1ull << (28 - kItemBits)) - 1)) << kItemBits) | (r & 0xFF));
            };
            if (f == hmitree::Family::Elements) {                              // ses objets, depliables a leur tour
                const auto kids = hmitree::childrenOf(*v, o->id);
                return k < kids.size() ? pack(NodeKind::HmiObject, i, static_cast<Index>(kids[k]->id & kMask28)) : ui::kInvalidNode;
            }
            if (f == hmitree::Family::Params)
                return k < hmi::pub::instanceParams(hmi_->project, *o).size() && k <= 0xFF
                    ? pack(NodeKind::HmiObjectParam, i, item(k)) : ui::kInvalidNode;
            if (f == hmitree::Family::Markers)            // 1.10.3 : les reperes, puis la ligne d'aide
                return k < hmitree::familyLines(hmi_->project, *o, f).size() && k <= 0xFF ? pack(NodeKind::HmiObjectMarker, i, item(k)) : ui::kInvalidNode;
            const auto ranks = hmitree::familyRanks(hmitree::overridesOf(*o), f);
            return k < ranks.size() && ranks[k] <= 0xFF ? pack(NodeKind::HmiObjectItem, i, item(ranks[k])) : ui::kInvalidNode;
        }
        case NodeKind::HmiObjectOperators: {                 // 1.10 : les operateurs du symbole
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            const auto* ops = objectOperators(hmi_.get(), o);
            if (!ops || k >= ops->size() || k > 0xFF) return ui::kInvalidNode;
            return pack(NodeKind::HmiObjectOperator, i, static_cast<Index>(((o->id & ((1ull << (28 - kItemBits)) - 1)) << kItemBits) | k));
        }
        case NodeKind::HmiObjectFunctions: {                 // 1.11.10 : les fonctions du symbole
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            const auto* fns = objectFunctions(hmi_.get(), o);
            if (!fns || k >= fns->size() || k > 0xFF) return ui::kInvalidNode;
            return pack(NodeKind::HmiObjectFunction, i, static_cast<Index>(((o->id & ((1ull << (28 - kItemBits)) - 1)) << kItemBits) | k));
        }
        case NodeKind::HmiObjectPopups: {                    // 1.11.10 : ses popups, des vues
            const auto pops = ownedPopups(hmi_.get(), objectSymbol(hmi_.get(), hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n))));
            return k < pops.size() ? pack(NodeKind::HmiView, static_cast<Index>(pops[k]->id & kMask28)) : ui::kInvalidNode;
        }
        // ---- 1.10 (chantier O) : les lignes du noeud Alarmes ----
        case NodeKind::HmiObjectAlarms:
        case NodeKind::HmiObjectAlarm: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const bool top = kindOf(n) == NodeKind::HmiObjectAlarms;
            const auto* o = top ? hmiObjectById(v, subOf(n)) : hmiItemObject(v, subOf(n));
            const auto g = o ? objectAlarms(v->id, o->id) : nullptr;
            if (!g) return ui::kInvalidNode;
            // Le rang dans la liste a plat (0 : le noeud ; puis ses alarmes, puis
            // chaque groupe d'un objet de l'instance suivi des siennes).
            std::size_t rank = 0;
            if (top) {
                if (k < g->alarms.size()) rank = 1 + k;
                else if (k - g->alarms.size() < g->groups.size()) {
                    rank = 1 + g->alarms.size();
                    for (std::size_t h = 0; h < k - g->alarms.size(); ++h) rank += 1 + g->groups[h].alarms.size();
                } else return ui::kInvalidNode;
            } else {
                const auto line = alarmLineOf(*g, subOf(n) & 0xFF);
                if (!line.group || line.group == g.get() || k >= line.group->alarms.size()) return ui::kInvalidNode;
                rank = (subOf(n) & 0xFF) + 1 + k;
            }
            if (rank > 0xFF) return ui::kInvalidNode;
            return pack(NodeKind::HmiObjectAlarm, i, static_cast<Index>(((o->id & ((1ull << (28 - kItemBits)) - 1)) << kItemBits) | rank));
        }
        case NodeKind::HmiAlarms:
            return pack(NodeKind::HmiAlarmGroup, static_cast<Index>(k));
        case NodeKind::HmiAlarmGroup: {
            if (!hmi_) return ui::kInvalidNode;
            const auto groups = hmitree::alarmGroups(hmi_->project);
            if (i >= groups.size()) return ui::kInvalidNode;
            const auto list = hmitree::alarmsOf(hmi_->project, groups[i]);
            return k < list.size() ? pack(NodeKind::HmiAlarm, static_cast<Index>(list[k]->id & kMask28)) : ui::kInvalidNode;
        }
        case NodeKind::HmiRecipes:
            return hmi_ && k < hmi_->project.recipes.size()
                ? pack(NodeKind::HmiRecipe, static_cast<Index>(hmi_->project.recipes[k].id & kMask28))
                : ui::kInvalidNode;
        case NodeKind::HmiRecipe: {
            const auto* r = hmi_ ? byId(hmi_->project.recipes, i) : nullptr;
            return r && k < r->records.size()
                ? pack(NodeKind::HmiRecord, i, static_cast<Index>(r->records[k].id & kMask28))
                : ui::kInvalidNode;
        }
        case NodeKind::HmiUsers: {
            if (!hmi_) return ui::kInvalidNode;
            const auto& sec = hmi_->project.security;
            if (k < sec.groups.size()) return pack(NodeKind::HmiUserGroup, static_cast<Index>(sec.groups[k].id & kMask28));
            const auto loose = hmitree::usersWithoutGroup(hmi_->project);
            if (k - sec.groups.size() < loose.size())
                return pack(NodeKind::HmiUser, static_cast<Index>(loose[k - sec.groups.size()]->id & kMask28));
            return pack(NodeKind::HmiRoles, 0);
        }
        case NodeKind::HmiUserGroup: {
            const auto* g = hmi_ ? byId(hmi_->project.security.groups, i) : nullptr;
            if (!g) return ui::kInvalidNode;
            const auto list = hmitree::usersOf(hmi_->project, g->id);
            return k < list.size() ? pack(NodeKind::HmiUser, static_cast<Index>(list[k]->id & kMask28)) : ui::kInvalidNode;
        }
        case NodeKind::HmiRoles:
            return pack(NodeKind::HmiRole, static_cast<Index>(k));
        case NodeKind::HmiScripts: {
            // Lot 7 : les fonctions a cote des scripts.
            // Lot 9 : les variables systeme et les variables d'instances apres.
            // Lot 16 : les types IHM avant les variables.
            static constexpr NodeKind folders[] = {NodeKind::HmiScriptsFolder, NodeKind::HmiFunctionsFolder, NodeKind::HmiTypesFolder,
                                                   NodeKind::HmiVariablesFolder, NodeKind::HmiUsedFolder,
                                                   NodeKind::HmiSysFolder, NodeKind::HmiInstFolder};
            return k < std::size(folders) ? pack(folders[k], 0) : ui::kInvalidNode;
        }
        case NodeKind::HmiTypesFolder: {                // lot 21 : ses dossiers, puis ses types
            if (!hmi_) return ui::kInvalidNode;
            const auto kids = hmilists::childrenOf(hmi_->project, hmi::fold::List::Types, {});
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        // ---- 1.10 (chantier O, decision 15) : "Valeurs (n)" puis "Operateurs (n)" ----
        case NodeKind::HmiTypeNode: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            if (!ty) return ui::kInvalidNode;
            const std::size_t values = ty->kind == hmi::HmiTypeKind::Enumeration ? 1u : 0u;
            if (k < values) return pack(NodeKind::HmiTypeValues, i);
            if (k == values && !ty->operators.empty()) return pack(NodeKind::HmiTypeOperators, i);
            return ui::kInvalidNode;
        }
        case NodeKind::HmiTypeValues:
        case NodeKind::HmiTypeOperators: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            const bool values = kindOf(n) == NodeKind::HmiTypeValues;
            const std::size_t count = !ty ? 0u : values ? ty->values.size() : ty->operators.size();
            if (k >= count || k > 0xFFFF) return ui::kInvalidNode;
            return pack(values ? NodeKind::HmiTypeValue : NodeKind::HmiTypeOperator, i, static_cast<Index>(k));
        }
        case NodeKind::HmiVarFolder: {
            if (!hmi_) return ui::kInvalidNode;
            const auto all = hmi::types::allFolders(hmi_->project);
            if (i >= all.size()) return ui::kInvalidNode;
            const auto kids = hmivars::childrenOf(hmi_->project, all[i]);
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::HmiFunctionsFolder:
            return hmi_ && k < hmi_->project.programs.functions.size()
                ? pack(NodeKind::HmiFunction, static_cast<Index>(hmi_->project.programs.functions[k].id & kMask28))
                : ui::kInvalidNode;
        case NodeKind::HmiScriptsFolder: {              // lot 21 : ses dossiers, puis ses scripts
            if (!hmi_) return ui::kInvalidNode;
            const auto kids = hmilists::childrenOf(hmi_->project, hmi::fold::List::Scripts, {});
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::HmiVariablesFolder: {
            // Lot 16 : les dossiers d'abord, puis les variables de la racine.
            if (!hmi_) return ui::kInvalidNode;
            const auto kids = hmivars::childrenOf(hmi_->project, {});
            return k < kids.size() ? kids[k] : ui::kInvalidNode;
        }
        case NodeKind::HmiUsedFolder:
            return pack(NodeKind::HmiUsedVariable, static_cast<Index>(k));
        // ---- lot 9
        case NodeKind::HmiSysFolder:
            return k < hmi::pub::kSysDomainCount ? pack(NodeKind::HmiSysDomain, static_cast<Index>(k)) : ui::kInvalidNode;
        case NodeKind::HmiSysDomain: {
            const auto list = hmi::pub::sysVarsOf(static_cast<int>(i));
            return k < list.size() ? pack(NodeKind::HmiSysVar, static_cast<Index>(list[k] - hmi::pub::kSysVars)) : ui::kInvalidNode;
        }
        case NodeKind::HmiInstFolder:
            return hmi_ && k < hmi_->project.views.size()
                ? pack(NodeKind::HmiInstView, static_cast<Index>(hmi_->project.views[k].id & kMask28))
                : ui::kInvalidNode;
        case NodeKind::HmiInstView: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v) return ui::kInvalidNode;
            if (k == 0) return pack(NodeKind::HmiInstViewInfo, i);
            return k - 1 < v->objects.size()
                ? pack(NodeKind::HmiInstObject, i, static_cast<Index>(v->objects[k - 1].id & kMask28))
                : ui::kInvalidNode;
        }
        case NodeKind::HmiInstViewInfo:
            return k < std::size(hmi::pub::kViewInfo) ? pack(NodeKind::HmiInstViewVar, i, static_cast<Index>(k)) : ui::kInvalidNode;
        case NodeKind::HmiInstObject: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            if (!o) return ui::kInvalidNode;
            // 1.11.1 (decision 108) : ses variables, puis les parametres d'une instance,
            // son groupe d'alarmes et ses alarmes (comme le volet).
            const auto parts = instPartsOf(hmi_->project, *v, *o);
            if (k < parts.members) return pack(NodeKind::HmiInstVar, i, instItem(*o, k));
            k -= parts.members;
            if (k < parts.params) return pack(NodeKind::HmiInstParam, i, instItem(*o, k));
            k -= parts.params;
            if (parts.group) {
                if (k == 0) return pack(NodeKind::HmiInstAlarmGroup, i, static_cast<Index>(o->id & kMask28));
                --k;
            }
            return k == 0 && parts.alarms ? pack(NodeKind::HmiInstAlarms, i, static_cast<Index>(o->id & kMask28)) : ui::kInvalidNode;
        }
        case NodeKind::HmiInstAlarmGroup: {
            const auto* o = hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n));
            return o && k < std::size(hmi::pub::kAlarmInfo) ? pack(NodeKind::HmiInstGroupVar, i, instItem(*o, k)) : ui::kInvalidNode;
        }
        case NodeKind::HmiInstAlarms: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            return o && k < 256 && k < hmi::pub::objectAlarmNames(hmi_->project, *v, *o).size()
                ? pack(NodeKind::HmiInstAlarm, i, instItem(*o, k)) : ui::kInvalidNode;
        }
        case NodeKind::HmiInstAlarm:
            return k < std::size(hmi::pub::kAlarmMembers) && i <= kInstViewMask
                ? pack(NodeKind::HmiInstAlarmVar, static_cast<Index>((k << 24) | i), subOf(n)) : ui::kInvalidNode;

        default: return ui::kInvalidNode;
        }
    }

    // La section derriere un noeud. Le menu contextuel et le glisser-deposer s'en
    // servent pour ne pas avoir a connaitre le paquetage des NodeId.
    Index ProjectTreeModel::sectionOf(ui::NodeId n) const {
        switch (kindOf(n)) {
        case NodeKind::Section:
        case NodeKind::DfbSection:
            return indexOf(n);
        case NodeKind::ExecStep: {
            const auto t = indexOf(n);
            if (t >= project_->tasks.size()) return kNoIndex;
            const auto steps = domain::executionOrder(*project_, project_->tasks[t].name);
            const auto rank = subOf(n);
            return rank < steps.size() ? steps[rank].section : kNoIndex;
        }
        case NodeKind::ProgramUnit: {
            // Une unite d'UNE seule section se comporte comme cette section :
            // c'est la forme que prend chaque section generee par les macros,
            // et obliger a deplier pour attraper l'unique enfant serait une
            // ceremonie pour rien.
            const auto i = indexOf(n);
            if (i < project_->pous.size() && project_->pous[i].sections.size() == 1)
                return project_->pous[i].sections.front();
            return kNoIndex;
        }
        default: return kNoIndex;
        }
    }

    bool ProjectTreeModel::hasChildren(ui::NodeId n) const {
        // Lot API 7 : une variable, un membre - sans fabriquer ses enfants (la
        // fleche de chaque ligne d'une liste de mille variables).
        if (holdsMembers(n)) return memberHasChildren(n);
        return childCount(n) > 0;
    }

    // ---- Lot API 8 : l'arbre du projet ----
    void ProjectTreeModel::setFilterHits(std::vector<FilterHitRow> hits) {
        filterHits_ = std::move(hits);
    }

    // Les rangs des resultats du contenu places sous `n` : IHM sous le dossier
    // IHM ; API sous le dossier API (sans IHM : sous la racine, qui porte les
    // dossiers de l'automate). Vide pour tout autre noeud.
    std::vector<domain::Index> ProjectTreeModel::hitsUnder(ui::NodeId n) const {
        std::vector<domain::Index> out;
        const auto k = kindOf(n);
        const bool apiHome = hmi_ ? k == NodeKind::ApiFolder : k == NodeKind::Root;
        const bool hmiHome = hmi_ && k == NodeKind::HmiFolder;
        if (!apiHome && !hmiHome) return out;
        for (std::size_t r = 0; r < filterHits_.size(); ++r)
            if (filterHits_[r].hmi == hmiHome) out.push_back(static_cast<domain::Index>(r));
        return out;
    }

    // Le noeud d'avant derriere le bouton k de la rangee des outils.
    ui::NodeId ProjectTreeModel::toolNode(bool hmi, std::size_t k) {
        static constexpr NodeKind hmiTools[] = {NodeKind::HmiExchange, NodeKind::HmiFind, NodeKind::HmiModbusTool,
                                                NodeKind::HmiGenerate, NodeKind::HmiCompile};
        if (!hmi) return k == 0 ? pack(NodeKind::ApiStatistics, 0) : ui::kInvalidNode;
        return k < std::size(hmiTools) ? pack(hmiTools[k], 0) : ui::kInvalidNode;
    }

    bool ProjectTreeModel::setHmiExprErrors(std::size_t n, std::size_t inViews) {
        inViews = std::min(inViews, n);
        if (n == hmiExprErrors_ && inViews == hmiExprErrorsInViews_) return false;
        hmiExprErrors_ = n;
        hmiExprErrorsInViews_ = inViews;
        return true;
    }

    bool ProjectTreeModel::setAllVersions(bool all) {
        if (all == allVersions_) return false;
        allVersions_ = all;
        return true;
    }

    void ProjectTreeModel::setShortcuts(std::vector<ShortcutRow> pins, std::vector<ShortcutRow> recents) {
        pins_ = std::move(pins);
        recents_ = std::move(recents);
    }

    ui::NodeId ProjectTreeModel::shortcutTarget(ui::NodeId n) const {
        const auto k = kindOf(n);
        if (k != NodeKind::PinItem && k != NodeKind::RecentItem) return ui::kInvalidNode;
        const auto& list = k == NodeKind::PinItem ? pins_ : recents_;
        if (indexOf(n) >= list.size()) return ui::kInvalidNode;
        const auto t = list[indexOf(n)].target;
        // Jamais un raccourci vers un raccourci (text / style s'appelleraient sans fin).
        const auto tk = kindOf(t);
        return tk == NodeKind::PinItem || tk == NodeKind::RecentItem ? ui::kInvalidNode : t;
    }

    // ---- 2e partie : les compteurs en pastille, le domaine, le point, les mises a jour ----
    namespace {
        // Les dossiers dont le texte finit par un compteur ("Nom  [28]", "Nom (28)") :
        // le nom reste, le nombre part en pastille. Pas les feuilles (un
        // commentaire "(3)" n'est pas un compteur).
        bool treeCounterKind(ProjectTreeModel::NodeKind k) {
            using NK = ProjectTreeModel::NodeKind;
            switch (k) {
            case NK::RackFolder: case NK::ApiChannels: case NK::ApiNetwork: case NK::ApiMemory: case NK::TaskFolder:
            case NK::TypesFolder: case NK::DfbFolder: case NK::UnitsFolder: case NK::ExecOrderFolder:
            case NK::DfbInputs: case NK::DfbOutputs: case NK::DfbInOut: case NK::DfbPublicVars: case NK::DfbPrivateVars:
            case NK::DfbSectionsFolder: case NK::VariablesFolder: case NK::ElementaryFolder: case NK::DdtInstanceFolder:
            case NK::DfbInstanceFolder: case NK::SubroutinesFolder: case NK::MacroFolder: case NK::MacroSubFolder:
            case NK::TablesFolder:
            case NK::HmiExternalFiles: case NK::HmiResources: case NK::HmiViews: case NK::HmiSymbolsFolder:
            case NK::HmiViewFolder: case NK::HmiTemplateFolder: case NK::HmiViewPart: case NK::HmiStyles: case NK::HmiTests:
            case NK::HmiScripts: case NK::HmiAlarms: case NK::HmiRecipes: case NK::HmiUsers: case NK::HmiLanguages:
            case NK::HmiUnits: case NK::HmiComm: case NK::HmiReports: case NK::HmiAlarmGroup: case NK::HmiRoles:
            case NK::HmiScriptsFolder: case NK::HmiFunctionsFolder: case NK::HmiVariablesFolder: case NK::HmiTypesFolder:
            case NK::HmiVarFolder: case NK::HmiUsedFolder: case NK::HmiSysFolder: case NK::HmiSysDomain:
            case NK::HmiInstFolder: case NK::HmiInstViewInfo: case NK::HmiListFolder:
            case NK::HmiInstAlarmGroup: case NK::HmiInstAlarms:            // 1.11.1 (decision 108)
                return true;
            default:
                return false;
            }
        }

        // "Nom  [28]", "Nom (28)" -> "Nom", "28" ; "Ordre d'execution  [21 entrees,
        // 65 sections]" -> "Ordre d'execution", "21", "65 sections". Faux : pas de compteur.
        bool splitTreeCounter(const std::string& full, std::string& name, std::string& count, std::string& extra) {
            if (full.size() < 4) return false;
            const char close = full.back();
            if (close != ']' && close != ')') return false;
            const auto at = full.rfind(close == ']' ? '[' : '(');
            if (at == std::string::npos || at == 0) return false;
            const std::string inside = full.substr(at + 1, full.size() - at - 2);
            std::size_t d = 0;
            while (d < inside.size() && inside[d] >= '0' && inside[d] <= '9') ++d;
            if (d == 0) return false;
            if (d == inside.size()) {
                count = inside;
            } else if (inside.compare(d, 7, " entr\xC3\xA9") == 0) {
                count = inside.substr(0, d);
                const auto comma = inside.find(", ");
                extra = comma == std::string::npos ? std::string{} : inside.substr(comma + 2);
            } else {
                return false;
            }
            name = full.substr(0, at);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            return !name.empty();
        }

        bool treeHmiKind(ProjectTreeModel::NodeKind k) {
            using NK = ProjectTreeModel::NodeKind;
            return (k >= NK::HmiFolder && k <= NK::HmiScripts) || (k >= NK::HmiAlarms && k <= NK::HmiHistory)
                || (k >= NK::HmiObject && k <= NK::HmiUsedVariable) || k == NK::HmiFunctionsFolder || k == NK::HmiFunction
                || k == NK::HmiViewFolder || k == NK::HmiTemplateFolder || (k >= NK::HmiSysFolder && k <= NK::HmiInstViewInfo)
                || k == NK::HmiSymbolsFolder || k == NK::HmiStyles || k == NK::HmiFind || k == NK::HmiTests
                || k == NK::HmiLanguages || k == NK::HmiUnits || k == NK::HmiComm || k == NK::HmiStation
                || k == NK::HmiNotify || k == NK::HmiReports || k == NK::HmiWeb || k == NK::HmiModbusTool
                || k == NK::HmiTypesFolder || k == NK::HmiTypeNode || k == NK::HmiVarFolder || k == NK::HmiListFolder
                || k == NK::HmiObjectAlarms || k == NK::HmiObjectAlarm               // 1.10 (chantier O)
                || k == NK::HmiTypeValues || k == NK::HmiTypeValue || k == NK::HmiTypeOperators || k == NK::HmiTypeOperator
                || k == NK::HmiObjectOperators || k == NK::HmiObjectOperator
                || (k >= NK::HmiObjectFunctions && k <= NK::HmiSymbolFunction)                       // 1.11.10
                || k == NK::HmiObjectFamily || k == NK::HmiObjectParam || k == NK::HmiObjectMarker   // 1.10.2 (chantier A)
                || (k >= NK::HmiInstParam && k <= NK::HmiInstAlarmVar);                              // 1.11.1 (decision 108)
        }
    } // namespace

    std::string ProjectTreeModel::counterOf(ui::NodeId n) const {
        const auto k = kindOf(n);
        if (k == NodeKind::PinItem || k == NodeKind::RecentItem) {
            const auto t = shortcutTarget(n);
            return t == ui::kInvalidNode ? std::string{} : counterOf(t);
        }
        if (!treeCounterKind(k) || counterGuard_) return {};
        counterGuard_ = true;
        const std::string full = text(n);
        counterGuard_ = false;
        std::string name, count, extra;
        return splitTreeCounter(full, name, count, extra) ? count : std::string{};
    }

    void ProjectTreeModel::setChangedKinds(std::vector<NodeKind> kinds, std::string tip) {
        changedKinds_ = std::move(kinds);
        changedTip_ = std::move(tip);
    }

    bool ProjectTreeModel::changedSinceVersion(ui::NodeId n) const {
        const auto k = kindOf(n);
        if (k == NodeKind::PinItem || k == NodeKind::RecentItem) {
            const auto t = shortcutTarget(n);
            return t != ui::kInvalidNode && changedSinceVersion(t);
        }
        return std::find(changedKinds_.begin(), changedKinds_.end(), k) != changedKinds_.end();
    }

    bool ProjectTreeModel::setLibraryUpdates(std::size_t ddt, std::size_t dfb) {
        if (ddt == libDdt_ && dfb == libDfb_) return false;
        libDdt_ = ddt;
        libDfb_ = dfb;
        return true;
    }

    bool ProjectTreeModel::setScope(int scope) {
        scope = scope < 0 || scope > 5 ? 0 : scope;
        if (scope == scope_) return false;
        scope_ = scope;
        return true;
    }

    std::uint8_t ProjectTreeModel::domainOf(ui::NodeId n) const {
        const auto k = kindOf(n);
        switch (k) {
        case NodeKind::Root: case NodeKind::PinsFolder: case NodeKind::RecentFolder: return 0;
        case NodeKind::PinItem: case NodeKind::RecentItem: {
            const auto t = shortcutTarget(n);
            return t == ui::kInvalidNode ? std::uint8_t{0} : domainOf(t);
        }
        case NodeKind::FilterHit:
            return indexOf(n) < filterHits_.size() && filterHits_[indexOf(n)].hmi ? std::uint8_t{2} : std::uint8_t{1};
        case NodeKind::ToolRow: return indexOf(n) == 1 ? std::uint8_t{2} : std::uint8_t{1};
        case NodeKind::VersionsFolder: case NodeKind::VersionItem: case NodeKind::VersionsMore: return 4;
        default: break;
        }
        if (isSimNode(n)) return 3;
        return treeHmiKind(k) ? std::uint8_t{2} : std::uint8_t{1};
    }

    void ProjectTreeModel::decorate(ui::NodeId n, ui::CellStyle& s) const {
        const auto k = kindOf(n);
        s.domain = domainOf(n);
        s.domainHead = k == NodeKind::ApiFolder || k == NodeKind::HmiFolder || k == NodeKind::SimFolder
                    || k == NodeKind::VersionsFolder;
        // Le compteur, en pastille neutre ; ce qu'elle portait deja passe a sa gauche.
        if (s.chips.empty() && (treeCounterKind(k) || k == NodeKind::PinItem || k == NodeKind::RecentItem)) {
            const auto t = k == NodeKind::PinItem || k == NodeKind::RecentItem ? shortcutTarget(n) : n;
            if (t != ui::kInvalidNode && treeCounterKind(kindOf(t))) {
                counterGuard_ = true;
                const std::string full = text(t);
                counterGuard_ = false;
                std::string name, count, extra;
                if (splitTreeCounter(full, name, count, extra)) {
                    if (!s.badge.empty() && s.pill.empty()) {
                        s.pill = s.badge;
                        s.pillTone = s.badgeTone;
                    }
                    s.badge = count;
                    s.badgeTone = ui::Tone::None;
                    if (!extra.empty() && s.hint.empty()) s.hint = extra;     // "65 sections", en gris
                }
            }
        }
        // Les mises a jour de bibliotheque : orange.
        const std::size_t lib = k == NodeKind::TypesFolder ? libDdt_ : k == NodeKind::DfbFolder ? libDfb_ : 0;
        if (lib > 0) {
            s.pill = std::to_string(lib);
            s.pillTone = ui::Tone::Warning;
        }
        if (k == NodeKind::ApiFolder && libDdt_ + libDfb_ > 0) {
            s.pill = std::to_string(libDdt_ + libDfb_) + " \xC3\xA0 mettre \xC3\xA0 jour";
            s.pillTone = ui::Tone::Warning;
        }
        // Ce qui a change depuis la derniere version : le point orange.
        if (!changedKinds_.empty() && changedSinceVersion(n)) {
            s.dotTone = ui::Tone::Warning;
            s.dotTip = changedTip_;
        }
    }
    // ---- fin Lot API 8 : l'arbre du projet ----

    // ---------------------------------------------- lot API 7 : les membres ----
    namespace {
        namespace mt = project::members;

        std::string lowerOf(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        // L'allure d'un membre : son icone dit sa nature (un tableau, une
        // structure, un bloc, une variable), sa couleur son sens (une entree,
        // une sortie...) ; un paquet, une ligne d'un tableau rangent des
        // elements et n'ont pas de valeur a eux : en retrait.
        void memberLook(const Project& p, const mt::Node& node, ui::Icon& icon, ui::Tone& iconTone, ui::Tone& fgTone) {
            if (!node.real) {
                icon = node.what == "ligne" ? ui::Icon::AnimationTable : ui::Icon::Folder;
                iconTone = ui::Tone::Muted;
                fgTone = ui::Tone::Muted;
                return;
            }
            switch (mt::natureOf(p, node.type)) {
            case mt::Nature::Array:     icon = ui::Icon::AnimationTable; break;
            case mt::Nature::Structure: icon = ui::Icon::DerivedType; break;
            case mt::Nature::Block:     icon = ui::Icon::FunctionBlock; break;
            default:                    icon = ui::Icon::Variable; break;
            }
            if (node.what == "entr\xC3\xA9" "e")              iconTone = ui::Tone::Input;
            else if (node.what == "sortie")                     iconTone = ui::Tone::Output;
            else if (node.what == "entr\xC3\xA9" "e-sortie")   iconTone = ui::Tone::InOut;
            else if (node.what == "publique")                   iconTone = ui::Tone::Info;
            else if (node.what == "priv\xC3\xA9" "e")          iconTone = ui::Tone::Muted;
        }
    } // namespace

    bool ProjectTreeModel::holdsMembers(ui::NodeId n) noexcept {
        switch (kindOf(n)) {
        case NodeKind::DerivedField:
        case NodeKind::DfbVariable:
        case NodeKind::ListVariable:
        case NodeKind::MemberNode:   return true;
        default:                     return false;
        }
    }

    bool ProjectTreeModel::memberNodeOf(ui::NodeId n, mt::Node& node, Index& root, Index& decl) const {
        if (kindOf(n) == NodeKind::MemberNode) {
            const auto m = indexOf(n);
            if (m >= members_.size()) return false;          // un noeud d'un autre modele
            node = members_[m].node;
            root = members_[m].root;
            decl = members_[m].decl;
            return true;
        }
        if (!holdsMembers(n)) return false;
        // La racine : la variable elle-meme, qui se declare (ses <instanceElementDesc>
        // disent les elements d'un tableau).
        const auto v = variableOf(n);
        if (v == kNoIndex || v >= project_->variables.size()) return false;
        const auto& var = project_->variables[v];
        node = mt::root(std::string(project_->strings.text(var.name)), std::string(project_->strings.text(var.type.name)));
        root = v;
        decl = v;
        return true;
    }

    ui::NodeId ProjectTreeModel::internMember(ui::NodeId parentId, const mt::Node& parent, mt::Node child, Index root,
                                              Index parentDecl) const {
        MemberEntry e;
        e.root = root;
        e.parent = parentId;
        e.decl = mt::declarationOf(*project_, parent, parentDecl, child);
        memberLook(*project_, child, e.icon, e.iconTone, e.fgTone);
        // Comme les autres variables de l'arbre : "nom : type   // commentaire".
        e.text = child.label + " : " + (child.real ? child.type : mt::groupType(child));
        if (const auto comment = mt::commentOf(*project_, e.decl, parent, child); !comment.empty()) e.text += "   // " + comment;
        // La cle : la variable racine ET le chemin. Deux unites ont chacune leur
        // "Enable", deux DDT leur champ "sorties" - des membres differents.
        std::string key = std::to_string(root) + '|' + lowerOf(child.key);
        e.node = std::move(child);
        if (const auto it = memberIds_.find(key); it != memberIds_.end()) {
            members_[it->second] = std::move(e);           // apres refresh() : le type a pu changer
            return pack(NodeKind::MemberNode, it->second);
        }
        if (members_.size() >= kMask28) return ui::kInvalidNode;   // 268 millions : on ne range plus
        const auto rank = static_cast<Index>(members_.size());
        members_.push_back(std::move(e));
        memberIds_.emplace(std::move(key), rank);
        return pack(NodeKind::MemberNode, rank);
    }

    bool ProjectTreeModel::memberRecurses(ui::NodeId n) const {
        if (kindOf(n) != NodeKind::MemberNode || indexOf(n) >= members_.size()) return false;
        const auto& me = members_[indexOf(n)];
        if (!me.node.real) return false;           // un paquet a le type de son tableau : c'est normal
        const auto type = lowerOf(me.node.type);
        // Chaque ancetre reel, puis la variable racine. Un chemin qui repasse
        // par le meme type EST une recursion : un type ne contient pas le sien.
        for (auto up = me.parent; kindOf(up) == NodeKind::MemberNode && indexOf(up) < members_.size();) {
            const auto& a = members_[indexOf(up)];
            if (a.node.real && lowerOf(a.node.type) == type) return true;
            up = a.parent;
        }
        return me.root < project_->variables.size() && lowerOf(project_->strings.text(project_->variables[me.root].type.name)) == type;
    }

    const std::vector<ui::NodeId>& ProjectTreeModel::memberChildren(ui::NodeId n) const {
        if (const auto it = memberKids_.find(n); it != memberKids_.end()) return it->second;
        // Un noeud d'un autre modele, une variable disparue d'une liste : rien, et
        // rien de retenu - la table ne garde que les noeuds de ce modele.
        static const std::vector<ui::NodeId> kNone;
        mt::Node parent;
        Index root = kNoIndex, decl = kNoIndex;
        if (!memberNodeOf(n, parent, root, decl)) return kNone;
        std::vector<ui::NodeId> out;
        if (!memberRecurses(n)) {
            auto kids = mt::children(*project_, parent);
            out.reserve(kids.size());
            for (auto& c : kids)
                if (const auto id = internMember(n, parent, std::move(c), root, decl); id != ui::kInvalidNode) out.push_back(id);
        }
        // Une reference dans la table : un unordered_map ne deplace pas ses
        // elements quand il grandit (seul refresh() les efface).
        return memberKids_.emplace(n, std::move(out)).first->second;
    }

    bool ProjectTreeModel::memberHasChildren(ui::NodeId n) const {
        if (const auto it = memberHas_.find(n); it != memberHas_.end()) return it->second;
        if (const auto kids = memberKids_.find(n); kids != memberKids_.end()) return !kids->second.empty();
        mt::Node node;
        Index root = kNoIndex, decl = kNoIndex;
        if (!memberNodeOf(n, node, root, decl)) return false;      // pas retenu, comme memberChildren
        const bool has = !memberRecurses(n) && mt::hasChildren(*project_, node);
        memberHas_.emplace(n, has);
        return has;
    }

    std::string ProjectTreeModel::memberPathOf(ui::NodeId n) const {
        if (kindOf(n) != NodeKind::MemberNode || indexOf(n) >= members_.size()) return {};
        const auto& node = members_[indexOf(n)].node;
        return node.real ? node.path : node.path + node.label;
    }

    std::string ProjectTreeModel::memberKeyOf(ui::NodeId n) const {
        if (kindOf(n) != NodeKind::MemberNode || indexOf(n) >= members_.size()) return {};
        return members_[indexOf(n)].node.key;
    }

    bool ProjectTreeModel::memberIsGroup(ui::NodeId n) const {
        return kindOf(n) == NodeKind::MemberNode && indexOf(n) < members_.size() && !members_[indexOf(n)].node.real;
    }

    ui::NodeId ProjectTreeModel::memberParentOf(ui::NodeId n) const {
        if (kindOf(n) != NodeKind::MemberNode || indexOf(n) >= members_.size()) return ui::kInvalidNode;
        return members_[indexOf(n)].parent;
    }

    Index ProjectTreeModel::memberRootOf(ui::NodeId n) const {
        if (kindOf(n) != NodeKind::MemberNode || indexOf(n) >= members_.size()) return kNoIndex;
        return members_[indexOf(n)].root;
    }

    const ProjectTreeModel::ConfigCounts& ProjectTreeModel::configCounts() const {
        if (!configCounts_) {
            ConfigCounts c;
            const auto io = project::io::check(*project_);
            c.channels = io.addresses.size();
            c.faulty = io.faulty;
            c.network = project::io::networkOf(*project_).size();
            // Lot API 5 : les variables situees des trois zones (%M, %MW, %KW).
            c.memory = 0;
            for (const auto& z : project::io::memoryZones(*project_)) c.memory += z.variables.size();
            configCounts_ = c;
        }
        return *configCounts_;
    }

    std::string ProjectTreeModel::text(ui::NodeId n) const {
        // ---- Lot API 8 : l'arbre du projet (2e partie : le compteur part en pastille, counterOf) ----
        if (!counterGuard_ && treeCounterKind(kindOf(n))) {
            counterGuard_ = true;
            const std::string full = text(n);
            counterGuard_ = false;
            std::string name, count, extra;
            return splitTreeCounter(full, name, count, extra) ? name : full;
        }
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : l'arbre du projet ----
        if (kindOf(n) == NodeKind::FilterHit)
            return indexOf(n) < filterHits_.size() ? filterHits_[indexOf(n)].title : std::string{};
        if (kindOf(n) == NodeKind::ToolRow) return {};    // des boutons, pas de texte (style : chips)
        if (kindOf(n) == NodeKind::VersionsMore)
            return "Voir les " + std::to_string(versions_.empty() ? 0 : versions_.size() - 1) + " versions\xE2\x80\xA6";
        if (kindOf(n) == NodeKind::PinsFolder) return "\xC3\x89pingl\xC3\xA9s";
        if (kindOf(n) == NodeKind::RecentFolder) return "R\xC3\xA9" "cents";
        if (kindOf(n) == NodeKind::PinItem || kindOf(n) == NodeKind::RecentItem) {
            const auto t = shortcutTarget(n);
            return t == ui::kInvalidNode ? std::string{} : text(t);
        }
        // ---- fin Lot API 8 : l'arbre du projet ----
        const auto i = indexOf(n);
        const auto& p = *project_;
        switch (kindOf(n)) {
        case NodeKind::Root:
            return p.header.projectName.empty() ? "Project" : p.header.projectName;
        case NodeKind::ApiFolder:  return "API";
        case NodeKind::ApiSimulation:  return "Automate";         // lot API 8 : sous Simulation (etait API > Simulation)
        case NodeKind::ApiStatistics:  return "Statistiques";
        // ---- Lot API 8 : Centre de simulation ----
        case NodeKind::SimFolder:      return "Simulation";
        case NodeKind::SimOverview:    return "Vue d'ensemble";
        case NodeKind::SimEquipment:   return "\xC3\x89quipements";
        case NodeKind::SimDebug:       return "D\xC3\xA9" "bogage";
        case NodeKind::SimForcing:     return "For\xC3\xA7" "ages";
        case NodeKind::SimTrends:      return "Courbes";
        case NodeKind::SimJournal:     return "Journal";
        // ---- fin Lot API 8 ----
        case NodeKind::ConfigurationFolder: return "Configuration";
        // Lot API 2 : l'arbre de l'API en francais.
        case NodeKind::Cpu:
            return "Processeur  " + p.hardware.cpuReference + "  (" + p.hardware.family + ", OS "
                + p.hardware.cpuFirmware + ")";
        case NodeKind::RackFolder:
            return "Racks et modules (" + std::to_string(p.hardware.racks.size()) + ")";
        // Lot API 4 : les trois autres entrees de Configuration.
        case NodeKind::ApiChannels: {
            const auto& c = configCounts();
            return "Voies et adresses  [" + std::to_string(c.channels) + "]";
        }
        case NodeKind::ApiNetwork:  return "R\xC3\xA9seau  [" + std::to_string(configCounts().network) + "]";
        case NodeKind::ApiMemory:   return "Plan m\xC3\xA9moire  [" + std::to_string(configCounts().memory) + "]";
        case NodeKind::Rack: {
            if (i >= p.hardware.racks.size()) return {};
            const auto& rack = p.hardware.racks[i];
            return "Rack " + std::to_string(rack.number) + "  " + rack.reference
                + "  [" + compte(rack.modules.size(), "module", "modules") + "]";
        }
        case NodeKind::HwModule: {
            const auto sub = static_cast<Index>(n & ((1ull << 28) - 1));
            if (i >= p.hardware.racks.size() || sub >= p.hardware.racks[i].modules.size()) return {};
            const auto& m = p.hardware.racks[i].modules[sub];
            std::string label = (m.slot < 0 ? std::string("PS") : std::to_string(m.slot))
                + "  " + m.reference + "  " + std::string(toString(m.kind));
            if (m.points()) label += "  [" + std::to_string(m.inputPoints) + " I / "
                + std::to_string(m.outputPoints) + " Q]";
            return label;
        }
        case NodeKind::TaskFolder:  return "T\xC3\xA2" "ches  [" + std::to_string(p.tasks.size()) + "]";

        // Lot API 2 : les ENTREES (une section, ou une unite en bloc) et les
        // sections qu'elles executent - « [65 sections] » seul comptait les
        // sections des unites comme autant d'entrees.
        case NodeKind::ExecOrderFolder: {
            if (p.tasks.empty()) return "Ordre d'ex\xC3\xA9" "cution";
            if (p.tasks.size() > 1)
                return "Ordre d'ex\xC3\xA9" "cution (" + std::to_string(p.tasks.size()) + " t\xC3\xA2" "ches)";
            const auto steps = domain::executionOrder(p, p.tasks[0].name);
            std::size_t entries = 0;
            std::vector<Index> units;
            for (const auto& st : steps) {
                if (!st.fromProgramUnit) { ++entries; continue; }
                if (std::find(units.begin(), units.end(), st.unit) == units.end()) { units.push_back(st.unit); ++entries; }
            }
            return "Ordre d'ex\xC3\xA9" "cution  [" + compte(entries, "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ", "
                + compte(steps.size(), "section", "sections") + "]";
        }
        case NodeKind::ExecOrderTask: {
            if (i >= p.tasks.size()) return {};
            return std::string(p.strings.text(p.tasks[i].name)) + "  ["
                + compte(domain::executionOrder(p, p.tasks[i].name).size(), "section", "sections") + "]";
        }
        case NodeKind::ExecStep: {
            if (i >= p.tasks.size()) return {};
            const auto steps = domain::executionOrder(p, p.tasks[i].name);
            const auto rank = subOf(n);
            if (rank >= steps.size()) return {};
            const auto& step = steps[rank];
            const auto& sec = p.sections[step.section];

            // Le rang est aligne sur deux chiffres : une liste dont les numeros
            // ne sont pas alignes se lit deux fois plus lentement, et cette
            // liste existe pour etre lue de haut en bas.
            std::string label = (rank + 1 < 10 ? " " : "") + std::to_string(rank + 1)
                + ".  " + std::string(p.strings.text(sec.name));

            // D'ou vient la section : seulement quand ce n'est pas evident.
            // Une section de tache porte le nom de son POU, l'afficher serait
            // le repeter.
            if (step.fromProgramUnit && step.unit < p.pous.size())
                label += "   (" + std::string(p.strings.text(p.pous[step.unit].name)) + ")";

            // Ce que le simulateur ne saura pas executer. Le dire ici evite de
            // chercher pourquoi une section "ne fait rien".
            if (sec.language != PouLanguage::ST)
                label += "   [" + std::string(toString(sec.language)) + ", non simule]";
            else
                label += "   [" + std::to_string(sec.lineCount)
                + (sec.lineCount == 1 ? " ligne]" : " lignes]");
            return label;
        }
        case NodeKind::Task: {
            const auto& t = p.tasks[i];
            const std::string kind = t.type == "cyclic" ? std::string("cyclique")
                                   : t.type == "periodic" ? "p\xC3\xA9riodique " + std::to_string(t.period) + " ms" : t.type;
            return std::string(p.strings.text(t.name)) + "  [" + kind + ", chien de garde "
                + std::to_string(t.watchdog) + " ms]";
        }
        case NodeKind::TypesFolder: return "Types d\xC3\xA9riv\xC3\xA9s (" + std::to_string(p.derivedTypes.size()) + ")";
        case NodeKind::DerivedType: {
            const auto& d = p.derivedTypes[i];
            return std::string(p.strings.text(d.name)) + "  v" + d.version
                + (d.instanceCount ? "  x" + std::to_string(d.instanceCount) : std::string(" (pas utilis\xC3\xA9)"));
        }
        case NodeKind::DerivedField: {
            const auto& v = p.variables[i];
            return std::string(p.strings.text(v.name)) + " : " + std::string(p.strings.text(v.type.name));
        }
        case NodeKind::DfbFolder:   return "Blocs DFB (" + std::to_string(childCount(n)) + ")";
        case NodeKind::DfbInputs:
            return "Entr\xC3\xA9" "es (" + std::to_string(folderMembers(NodeKind::DfbInputs, i).size()) + ")";
        case NodeKind::DfbOutputs:
            return "Sorties (" + std::to_string(folderMembers(NodeKind::DfbOutputs, i).size()) + ")";
        case NodeKind::DfbInOut:
            return "Entr\xC3\xA9" "es / sorties (" + std::to_string(folderMembers(NodeKind::DfbInOut, i).size()) + ")";
        case NodeKind::DfbPublicVars:
            return "Variables publiques (" + std::to_string(folderMembers(NodeKind::DfbPublicVars, i).size()) + ")";
        case NodeKind::DfbPrivateVars:
            return "Variables priv\xC3\xA9" "es (" + std::to_string(folderMembers(NodeKind::DfbPrivateVars, i).size()) + ")";
        case NodeKind::DfbSectionsFolder:
            return "Sections (" + std::to_string(project_->pous[i].sections.size()) + ")";
        case NodeKind::DfbVariable: {
            const auto& v = p.variables[i];
            std::string label = std::string(p.strings.text(v.name)) + " : "
                + std::string(p.strings.text(v.type.name));
            if (v.initValue != 0) label += " := " + std::string(p.strings.text(v.initValue));
            if (v.comment != 0)   label += "   // " + std::string(p.strings.text(v.comment));
            return label;
        }
        case NodeKind::DfbType: {
            const auto& pou = p.pous[i];
            return std::string(p.strings.text(pou.name)) + "  v" + pou.version
                + "  x" + std::to_string(pou.instanceCount);
        }
        case NodeKind::UnitsFolder: return "Unit\xC3\xA9s de programme (" + std::to_string(childCount(n)) + ")";
        case NodeKind::ProgramUnit: return std::string(p.strings.text(p.pous[i].name));
        case NodeKind::DfbSection:
        case NodeKind::Section: {
            const auto& s = p.sections[i];
            std::string label = std::string(p.strings.text(s.name)) + "  ["
                + std::string(toString(s.language)) + ", "
                + std::to_string(s.lineCount) + (s.lineCount > 1 ? " lignes]" : " ligne]");
            if (s.activationCondition != 0)
                label += "  si " + std::string(p.strings.text(s.activationCondition));
            return label;
        }
        case NodeKind::VariablesFolder:
            return "Variables  [" + std::to_string(elementary_.size() + ddtInstances_.size()
                + dfbInstances_.size()) + "]";
        case NodeKind::ElementaryFolder:
            return "Elementaires  [" + std::to_string(elementary_.size()) + "]";
        case NodeKind::DdtInstanceFolder:
            return "Instances de DDT  [" + std::to_string(ddtInstances_.size()) + "]";
        case NodeKind::DfbInstanceFolder:
            return "Instances de DFB  [" + std::to_string(dfbInstances_.size()) + "]";
        case NodeKind::ListVariable: {
            const auto at = variableOf(n);
            if (at == kNoIndex) return {};
            const auto& v = p.variables[at];
            std::string label = std::string(p.strings.text(v.name)) + " : "
                + std::string(p.strings.text(v.type.name));
            // L'adresse quand il y en a une : c'est ce qu'on vient chercher
            // dans une liste d'entrees-sorties, et l'ouvrir pour la lire serait
            // un clic pour un mot.
            if (v.located && !v.address.raw.empty()) label += "   " + v.address.raw;
            return label;
        }
        case NodeKind::SubroutinesFolder:
            return "Sous-routines  [" + std::to_string(subroutines_.size()) + "]";
        case NodeKind::Subroutine: {
            if (i >= subroutines_.size()) return {};
            const auto& pou = p.pous[subroutines_[i]];
            std::string label = std::string(p.strings.text(pou.name));
            if (!pou.sections.empty() && pou.sections.front() < p.sections.size()) {
                const auto& sec = p.sections[pou.sections.front()];
                label += "   [" + std::string(toString(sec.language)) + ", "
                    + compte(sec.lineCount, "ligne", "lignes") + "]";
            }
            return label;
        }
        case NodeKind::MacroFolder:
            return "Macros  [" + std::to_string(macros_.size()) + "]";
            // LE NOM NU, et rien d'autre. L'ecran fait `openMacro(text(n))` : y
            // ajouter un compteur ou une extension ferait chercher un fichier qui
            // n'existe pas.
        case NodeKind::Macro:
            return i < macros_.size() ? macros_[i] : std::string{};
        case NodeKind::MacroSubFolder: {                 // lot macros 1
            if (i >= macroFolders_.size()) return {};
            std::size_t deep = 0;
            for (const auto& f : macroFolderOfMacro_)
                if (project::macro::insideFolder(f, macroFolders_[i])) ++deep;
            return project::macro::folderLeaf(macroFolders_[i]) + "  [" + std::to_string(deep) + "]";
        }

        case NodeKind::HmiFolder:        return "IHM";
        // Lot 21 : les versions.
        case NodeKind::VersionsFolder:   return "Versions";
        case NodeKind::VersionItem:      return i < versions_.size() ? versions_[i].label : std::string{};
        case NodeKind::HmiListFolder:    return hmiListFolderText(n);     // lot 21 : un dossier d'une liste
        case NodeKind::HmiConfig:        return "Configuration";
        case NodeKind::HmiExternalFiles:
            return "Fichiers externes  [" + std::to_string(hmi_ ? hmi_->project.assets.files.size() : 0u) + "]";
        case NodeKind::HmiResources:
            return "Ressources  [" + std::to_string(hmi_ ? hmi_->project.assets.resources.size() : 0u) + "]";
        case NodeKind::HmiExchange:      return "Exporter / Importer";
        case NodeKind::HmiViews: {
            // Lot 10 : sans les symboles (ils ont leur dossier).
            std::size_t count = 0;
            if (hmi_) for (const auto& v : hmi_->project.views) count += hmi::viewFolderOf(v.role) != hmi::ViewFolder::Symbols;
            return "Vues  [" + std::to_string(count) + "]";
        }
        case NodeKind::HmiSymbolsFolder:
            return "Symboles" + countTag(hmi_ ? hmi::viewsInFolder(hmi_->project, hmi::ViewFolder::Symbols).size() : 0u);
        case NodeKind::HmiViewFolder: {
            static const char* names[] = {"Mod\xC3\xA8les", "Vues", "Popups"};
            const auto f = i == 0 ? hmi::ViewFolder::Templates : i == 1 ? hmi::ViewFolder::Views : hmi::ViewFolder::Popups;
            return std::string(names[std::min<std::size_t>(i, 2)])
                 + countTag(hmi_ ? hmi::viewsInFolder(hmi_->project, f).size() : 0u);
        }
        case NodeKind::HmiTemplateFolder: {
            static const char* names[] = {"\xC3\x89" "crans mod\xC3\xA8les", "En-t\xC3\xAAtes", "Pieds de page"};
            return std::string(names[std::min<std::size_t>(i, 2)])
                 + countTag(hmi_ ? hmi::viewsWithRole(hmi_->project, templateRoleOf(i)).size() : 0u);
        }
        case NodeKind::HmiView: {
            const auto* found = hmiViewById(hmi_.get(), i);
            if (!found) return {};
            const auto& v = *found;
            std::string label = v.name;
            // Lot 10 : un symbole dit d'abord combien d'instances le posent.
            if (v.role == "symbole") {
                std::size_t uses = 0;
                // (Les proprietes lues a la main : ce fichier ne lie pas la
                // bibliotheque de l'IHM - execorder_wiring_test s'en passe.)
                for (const auto& other : hmi_->project.views)
                    for (const auto& o : other.objects) {
                        if (o.kind != hmi::Kind::SymbolInstance) continue;
                        for (const auto& prop : o.props) uses += prop.key == "symbol" && prop.value == v.name;
                    }
                label += "   (" + std::to_string(uses) + (uses > 1 ? " instances)" : " instance)");
            }
            label += "   [" + std::to_string(v.width) + " x " + std::to_string(v.height) + ", " + std::to_string(v.objects.size())
                + (v.objects.size() > 1 ? " objets]" : " objet]");
            if (hmi_->project.config.startView == v.id) label += "   (d\xC3\xA9marrage)";
            // Lot 6 : ce qu'elle emprunte (modele, en-tete, pied) ; son role est
            // dans la pastille (style).
            std::vector<std::string> borrowed;
            if (v.templateView != hmi::kNoId)
                for (const auto& t : hmi_->project.views)
                    if (t.id == v.templateView) borrowed.push_back("h\xC3\xA9rite de " + t.name);
            if (v.showHeader && v.role != "entete") borrowed.emplace_back("en-t\xC3\xAAte");
            if (v.showFooter && v.role != "pied") borrowed.emplace_back("pied");
            for (std::size_t k = 0; k < borrowed.size(); ++k) label += (k ? ", " : "   \xE2\x86\x90 ") + borrowed[k];
            return label;
        }
        case NodeKind::HmiViewPart: {
            const auto* found = hmiViewById(hmi_.get(), i);
            if (!found) return {};
            const auto& v = *found;
            std::size_t animations = 0, groups = 0;
            for (const auto& o : v.objects) {
                if (o.kind == hmi::Kind::Group) ++groups;
                for (const auto& prop : o.props) animations += !prop.expr.empty();
            }
            switch (static_cast<HmiPart>(subOf(n))) {
            case HmiPart::Objects:    return "Objets  [" + std::to_string(v.objects.size() - groups) + "]";
            case HmiPart::Scripts:    return "Scripts  [" + std::to_string(v.scripts.size()) + "]";
            case HmiPart::Animations: return "Animations  [" + std::to_string(animations) + "]";
            case HmiPart::Layers:     return "Calques  [" + std::to_string(v.layers.size()) + "]";
            case HmiPart::Groups:     return "Groupes  [" + std::to_string(groups) + "]";
            case HmiPart::Functions:  return "Fonctions  [" + std::to_string(v.functions.size()) + "]";              // 1.11.10
            case HmiPart::Popups:     return "Popups  [" + std::to_string(ownedPopups(hmi_.get(), &v).size()) + "]";
            case HmiPart::Count:      break;
            }
            return {};
        }
        case NodeKind::HmiSimulation:    return "IHM";           // lot API 8 : sous Simulation (etait IHM > Simulation)
        case NodeKind::HmiGenerate:      return "G\xC3\xA9n\xC3\xA9rer";
        case NodeKind::HmiStyles:        return "Styles" + countTag(hmi_ ? hmi_->project.styles.size() : 0u);   // lot 12
        case NodeKind::HmiFind:          return "Rechercher / remplacer";
        case NodeKind::HmiTests:         return "Essais" + countTag(hmi_ ? hmi_->project.scenarios.size() : 0u);   // lot 13
        case NodeKind::HmiCompile:       return "Compiler";
        case NodeKind::HmiScripts:
            return "Programmation g\xC3\xA9n\xC3\xA9rale  [" + std::to_string(hmi_ ? hmi_->project.programs.scripts.size() : 0u) + "]";
        case NodeKind::HmiAlarms:
            return "Alarmes  [" + std::to_string(hmi_ ? hmi_->project.alarms.size() : 0u) + "]";
        case NodeKind::HmiRecipes:
            return "Recettes  [" + std::to_string(hmi_ ? hmi_->project.recipes.size() : 0u) + "]";
        case NodeKind::HmiUsers:
            // La securite active se voit a l'icone (verte), pas au libelle.
            return "Utilisateurs  [" + std::to_string(hmi_ ? hmi_->project.security.users.size() : 0u) + "]";
        case NodeKind::HmiHistory:       return "Historiques";
        case NodeKind::HmiLanguages:     return "Langues  [" + std::to_string(hmi_ ? hmi_->project.languages.list.size() : 1u) + "]";   // lot 13
        case NodeKind::HmiUnits:         return "Unit\xC3\xA9s et formats  [" + std::to_string(hmi_ ? hmi_->project.displays.size() : 0u) + "]";
        case NodeKind::HmiComm:                                                                                      // lot 14 ; lot 15 : les equipements
            return "\xC3\x89quipements  [" + std::to_string(hmi_ ? hmi_->project.equipments.size() + (hmi_->project.comm.modbus() ? 1u : 0u) : 0u) + "]";
        case NodeKind::HmiModbusTool:    return "Outil Modbus";                                                      // lot 15
        case NodeKind::HmiNotify: {                                                                                        // lot 14
            if (!hmi_) return "Notifications";
            const auto people = hmi_->project.notify.recipients.size();
            return "Notifications  [" + std::to_string(people) + (people > 1 ? " destinataires" : " destinataire") + (hmi_->project.notify.enabled ? "]" : ", arr\xC3\xAAt\xC3\xA9" "es]");
        }
        case NodeKind::HmiReports:       return "Rapports  [" + std::to_string(hmi_ ? hmi_->project.reports.size() : 0u) + "]";
        case NodeKind::HmiWeb:           return std::string("Acc\xC3\xA8s web  [") + (hmi_ && hmi_->project.web.enabled ? "port " + std::to_string(hmi_->project.web.port) : std::string("arr\xC3\xAAt\xC3\xA9")) + "]";
        case NodeKind::HmiStation: {                                                                                       // lot 14
            if (!hmi_) return "Poste d'exploitation";
            const auto extra = hmi_->project.station.screens.size();
            return std::string("Poste d'exploitation  [") + std::to_string(1 + extra) + (extra ? " \xC3\xA9" "crans" : " \xC3\xA9" "cran")
                   + (hmi_->project.station.kiosk ? ", kiosque]" : "]");
        }

        // ---- lot 5 ------------------------------------------------------------
        case NodeKind::HmiObject:
        case NodeKind::HmiGroupEntry: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            if (!o) return {};
            std::string label = o->name + "   " + std::string(kindText(o->kind));
            if (kindOf(n) == NodeKind::HmiGroupEntry) {
                label += countTag(hmitree::childrenOf(*v, o->id).size());
            } else if (const auto items = hmitree::overridesOf(*o).size()) {
                label += "   \xC2\xB7 " + std::to_string(items) + (items > 1 ? " surcharges" : " surcharge");
            }
            return label;
        }
        case NodeKind::HmiObjectItem: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            if (!o) return {};
            const auto items = hmitree::overridesOf(*o);
            const auto k = subOf(n) & 0xFF;
            if (k >= items.size()) return {};
            // 1.10.2 (chantier A) : sous la famille Actions, "Clic -> ..." sans "Action : ".
            return hmitree::lineText(items[k]);   // 1.10.3 : le texte de l'explorateur d'objets aussi
        }
        // ---- 1.10.2 (chantier A) : "Actions  [2]", "Pompe : T_POMPE := Pompes[2]" ----
        case NodeKind::HmiObjectFamily: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            const auto f = static_cast<hmitree::Family>(subOf(n) & 0xFF);
            if (!o || f >= hmitree::Family::Count) return {};
            return std::string(hmitree::familyLabel(f)) + countTag(hmitree::familySize(hmi_->project, *v, *o, f));
        }
        case NodeKind::HmiObjectParam: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            if (!o) return {};
            const auto params = hmi::pub::instanceParams(hmi_->project, *o);
            const auto k = subOf(n) & 0xFF;
            return k < params.size() ? hmitree::paramText(params[k]) : std::string{};
        }
        case NodeKind::HmiObjectMarker: {
            const auto* o = hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n));
            // 1.10.3 (Q1103) : "$Vanne$   x 3 - Valeur, Ouverture, Libelle", puis l'aide.
            const auto lines = o ? hmitree::familyLines(hmi_->project, *o, hmitree::Family::Markers) : std::vector<hmitree::Line>{};
            const auto k = subOf(n) & 0xFF;
            return k < lines.size() ? lines[k].text : std::string{};
        }
        // ---- 1.10 (chantier O) : "Alarmes . Vue.Pompe_3 (3)", puis chacune ----
        case NodeKind::HmiObjectAlarms:
        case NodeKind::HmiObjectAlarm: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const bool top = kindOf(n) == NodeKind::HmiObjectAlarms;
            const auto* o = top ? hmiObjectById(v, subOf(n)) : hmiItemObject(v, subOf(n));
            const auto g = o ? objectAlarms(v->id, o->id) : nullptr;
            if (!g) return {};
            const auto line = alarmLineOf(*g, top ? 0 : subOf(n) & 0xFF);
            if (line.group) return line.group->label;
            if (!line.alarm) return {};
            const auto& a = *line.alarm;
            std::string out = a.name + "   " + std::to_string(a.priority) + " - " + std::string(hmitree::priorityText(a.priority));
            if (!a.active) out += "   \xC2\xB7 d\xC3\xA9" "coch\xC3\xA9" "e";
            if (a.overridden) out += "   \xC2\xB7 surcharg\xC3\xA9" "e";
            return out;
        }
        case NodeKind::HmiViewScript: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v || subOf(n) >= v->scripts.size()) return {};
            const auto& sc = v->scripts[subOf(n)];
            const auto lines = sc.body.empty() ? 0 : 1 + std::count(sc.body.begin(), sc.body.end(), '\n') - (sc.body.back() == '\n');
            return sc.event + "   " + sc.name + "   " + compte(static_cast<std::size_t>(lines), "ligne", "lignes");
        }
        case NodeKind::HmiAnimation: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v) return {};
            const auto all = hmitree::animationsOf(*v);
            if (subOf(n) >= all.size()) return {};
            const auto& [o, pr] = all[subOf(n)];
            return o->name + "   " + std::string(hmitree::animatedLabel(pr->key)) + " = " + pr->expr;
        }
        case NodeKind::HmiLayer: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v || subOf(n) >= v->layers.size()) return {};
            const auto& l = v->layers[subOf(n)];
            const auto count = std::count_if(v->objects.begin(), v->objects.end(),
                                             [&](const hmi::Object& o) { return o.layer == l.id; });
            std::string label = l.name + countTag(static_cast<std::size_t>(count));
            if (!l.visible) label += "   masqu\xC3\xA9";
            if (l.locked) label += "   verrouill\xC3\xA9";
            if (v->activeLayer == l.id) label += "   (actif)";
            return label;
        }
        case NodeKind::HmiAlarmGroup: {
            if (!hmi_) return {};
            const auto groups = hmitree::alarmGroups(hmi_->project);
            if (i >= groups.size()) return {};
            return (groups[i].empty() ? std::string("(sans groupe)") : groups[i])
                + countTag(hmitree::alarmsOf(hmi_->project, groups[i]).size());
        }
        case NodeKind::HmiAlarm: {
            const auto* a = hmi_ ? byId(hmi_->project.alarms, i) : nullptr;
            if (!a) return {};
            return a->name + "   " + std::to_string(a->priority) + " - " + std::string(hmitree::priorityText(a->priority))
                + "   " + a->condition;
        }
        case NodeKind::HmiRecipe: {
            const auto* r = hmi_ ? byId(hmi_->project.recipes, i) : nullptr;
            if (!r) return {};
            return r->name + "   " + compte(r->fields.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments") + countTag(r->records.size());
        }
        case NodeKind::HmiRecord: {
            const auto* r = hmi_ ? byId(hmi_->project.recipes, i) : nullptr;
            const auto* rec = r ? byId(r->records, subOf(n)) : nullptr;
            if (!rec) return {};
            return rec->name + (rec->description.empty() ? std::string{} : "   " + rec->description);
        }
        case NodeKind::HmiUserGroup: {
            const auto* g = hmi_ ? byId(hmi_->project.security.groups, i) : nullptr;
            if (!g) return {};
            return g->name + "   niveau " + std::to_string(g->level)
                + countTag(hmitree::usersOf(hmi_->project, g->id).size());
        }
        case NodeKind::HmiUser: {
            const auto* u = hmi_ ? byId(hmi_->project.security.users, i) : nullptr;
            if (!u) return {};
            std::string label = u->login;
            if (!u->fullName.empty()) label += "   " + u->fullName;
            label += "   " + u->protection;
            if (!u->enabled) label += "   (d\xC3\xA9sactiv\xC3\xA9)";
            if (hmi_->project.security.startUser == u->login) label += "   (au lancement)";
            return label;
        }
        case NodeKind::HmiRoles:
            return "R\xC3\xB4les" + countTag(hmi_ ? hmi_->project.security.roles.size() : 0u);
        case NodeKind::HmiRole: {
            if (!hmi_ || i >= hmi_->project.security.roles.size()) return {};
            const auto& r = hmi_->project.security.roles[i];
            return r.name + " : " + (r.permissions.empty() ? std::string("aucune permission") : hmikit::joinList(r.permissions, ", "));
        }
        case NodeKind::HmiScriptsFolder:
            return "Scripts" + countTag(hmi_ ? hmi_->project.programs.scripts.size() : 0u);
        case NodeKind::HmiFunctionsFolder:
            return "Fonctions" + countTag(hmi_ ? hmi_->project.programs.functions.size() : 0u);
        case NodeKind::HmiFunction: {
            const auto* f = hmi_ ? byId(hmi_->project.programs.functions, i) : nullptr;
            if (!f) return {};
            return hmitree::signatureOf(*f) + (f->description.empty() ? std::string{} : "   // " + f->description);
        }
        case NodeKind::HmiVariablesFolder:
            return "Variables IHM" + countTag(hmi_ ? hmi_->project.programs.variables.size() : 0u);
        case NodeKind::HmiTypesFolder:
            return "Types IHM" + countTag(hmi_ ? hmi_->project.programs.types.size() : 0u);
        case NodeKind::HmiTypeNode: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            if (!ty) return {};
            if (ty->kind == hmi::HmiTypeKind::Enumeration)       // 1.10 (decision 15)
                return ty->name + "  (\xC3\xA9num\xC3\xA9ration)";
            const auto w = hmi::types::weightOf(hmi_->project, ty->name);
            return ty->name + "  [" + std::to_string(ty->members.size()) + " membre" + (ty->members.size() > 1 ? "s" : "") + ", "
                 + std::to_string(w.words) + " mot" + (w.words > 1 ? "s" : "") + "]";
        }
        // ---- 1.10 (chantier O, decision 14) : sous un objet, les operateurs de son symbole ----
        case NodeKind::HmiObjectOperators: {
            const auto* ops = objectOperators(hmi_.get(), hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n)));
            return ops ? "Op\xC3\xA9rateurs (" + std::to_string(ops->size()) + ")" : std::string{};
        }
        case NodeKind::HmiObjectOperator: {
            const auto* ops = objectOperators(hmi_.get(), hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n)));
            const auto k = static_cast<std::size_t>(subOf(n) & 0xFF);
            return ops && k < ops->size() ? hmi::operatorSignature((*ops)[k]) : std::string{};
        }
        // ---- 1.11.10 : les fonctions et les popups d'un symbole ----
        case NodeKind::HmiObjectFunctions: {
            const auto* fns = objectFunctions(hmi_.get(), hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n)));
            return fns ? "Fonctions (" + std::to_string(fns->size()) + ")" : std::string{};
        }
        case NodeKind::HmiObjectFunction: {
            const auto* o = hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n));
            const auto* fns = objectFunctions(hmi_.get(), o);
            const auto k = static_cast<std::size_t>(subOf(n) & 0xFF);
            if (!fns || k >= fns->size()) return {};
            const auto& f = (*fns)[k];
            return hmi::functionSignature(f) + (hmi::functionOverride(*o, f.name) && f.isVirtual ? "   \xC2\xB7 red\xC3\xA9" "finie"
                                                : "   \xC2\xB7 du symbole");
        }
        case NodeKind::HmiObjectPopups: {
            const auto pops = ownedPopups(hmi_.get(), objectSymbol(hmi_.get(), hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n))));
            return "Popups (" + std::to_string(pops.size()) + ")";
        }
        case NodeKind::HmiSymbolFunction: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto k = static_cast<std::size_t>(subOf(n));
            if (!v || k >= v->functions.size()) return {};
            return hmi::functionSignature(v->functions[k]) + (v->functions[k].isVirtual ? "   \xC2\xB7 virtuelle" : "");
        }
        // ---- 1.10 (chantier O, decision 15) : "Valeurs (4)", "Arret = 0", "Operateurs (2)", une signature ----
        case NodeKind::HmiTypeValues:
        case NodeKind::HmiTypeOperators: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            if (!ty) return {};
            return kindOf(n) == NodeKind::HmiTypeValues ? "Valeurs (" + std::to_string(ty->values.size()) + ")"
                                                        : "Op\xC3\xA9rateurs (" + std::to_string(ty->operators.size()) + ")";
        }
        case NodeKind::HmiTypeValue: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            const auto k = static_cast<std::size_t>(subOf(n));
            if (!ty || k >= ty->values.size()) return {};
            return ty->values[k].name + " = " + std::to_string(ty->values[k].value);
        }
        case NodeKind::HmiTypeOperator: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            const auto k = static_cast<std::size_t>(subOf(n));
            if (!ty || k >= ty->operators.size()) return {};
            return hmi::operatorSignature(ty->operators[k]);
        }
        case NodeKind::HmiVarFolder: {
            if (!hmi_) return {};
            const auto all = hmi::types::allFolders(hmi_->project);
            if (i >= all.size()) return {};
            std::size_t inside = 0;
            const std::string key = hmivars::upperOf(all[i]);
            for (const auto& v : hmi_->project.programs.variables) {
                const std::string f = hmivars::upperOf(v.folder);
                inside += f == key || f.rfind(key + "/", 0) == 0 ? 1 : 0;
            }
            return hmi::types::folderLeaf(all[i]) + countTag(inside);
        }
        case NodeKind::HmiUsedFolder:
            return "Variables employ\xC3\xA9" "es" + countTag(hmi_ ? usedVariables().size() : 0u);
        case NodeKind::HmiGeneralScript: {
            const auto* sc = hmi_ ? byId(hmi_->project.programs.scripts, i) : nullptr;
            if (!sc) return {};
            std::string label = sc->name + "   " + (sc->lang == hmi::ScriptLang::ST ? "ST" : sc->lang == hmi::ScriptLang::C ? "C" : "C++")
                + "   " + std::string(eventText(sc->event));
            if (sc->event == "Changement" && !sc->watch.empty()) label += " (" + sc->watch + ")";
            return label;
        }
        case NodeKind::HmiVariable: {
            const auto* v = hmi_ ? byId(hmi_->project.programs.variables, i) : nullptr;
            if (!v) return {};
            // Lot 16 : une structure ou un tableau n'a pas de valeur initiale a montrer ;
            // une variable liee dit son equipement.
            std::string label = v->name + " : " + v->type;
            if (!hmi::types::isComposite(v->type)) label += " = " + v->initial;
            if (v->bound()) label += "   \xE2\x86\x94 " + v->equipment + (v->address.empty() ? std::string{} : " " + v->address);
            return label + (v->description.empty() ? std::string{} : "   // " + v->description);
        }
        // ---- lot 9 : les variables systeme et d'instances --------------------------
        case NodeKind::HmiSysFolder:
            return "Variables syst\xC3\xA8me" + countTag(hmi::pub::kSysVarCount);
        case NodeKind::HmiSysDomain:
            return i < hmi::pub::kSysDomainCount ? std::string(hmi::pub::kSysDomains[i]) + countTag(hmi::pub::sysVarsOf(static_cast<int>(i)).size())
                                                 : std::string{};
        case NodeKind::HmiSysVar: {
            if (i >= hmi::pub::kSysVarCount) return {};
            const auto& v = hmi::pub::kSysVars[i];
            return "SYS." + std::string(v.name) + " : " + std::string(v.type);
        }
        case NodeKind::HmiInstFolder:
            return "Variables d'instances" + countTag(hmi_ ? hmi_->project.views.size() : 0u);
        case NodeKind::HmiInstView: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v) return {};
            return v->name + "   " + std::string(hmi::viewRoleLabel(v->role)) + countTag(v->objects.size());
        }
        case NodeKind::HmiInstViewVar: {
            const auto* v = hmiViewById(hmi_.get(), i);
            if (!v || subOf(n) >= std::size(hmi::pub::kViewInfo)) return {};
            const auto& info = hmi::pub::kViewInfo[subOf(n)];
            return std::string(info.name) + " : " + std::string(info.type);
        }
        case NodeKind::HmiInstViewInfo:
            return "Variables de la vue" + countTag(std::size(hmi::pub::kViewInfo));
        case NodeKind::HmiInstObject: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            if (!o) return {};
            return o->name + "   " + std::string(hmi::kindLabel(o->kind)) + countTag(instPartsOf(hmi_->project, *v, *o).count());
        }
        // ---- 1.11.1 (decision 108) : les parametres, le groupe d'alarmes, les alarmes ----
        case NodeKind::HmiInstParam: {
            const auto* o = hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n));
            if (!o) return {};
            const auto params = hmi::pub::instanceParams(hmi_->project, *o);
            const auto k = subOf(n) & 0xFF;
            if (k >= params.size()) return {};
            return params[k].name + " : " + params[k].type + (params[k].argument.empty() ? std::string{} : "  = " + params[k].argument);
        }
        case NodeKind::HmiInstAlarmGroup: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            if (!o) return {};
            const auto link = instLinkedGroup(hmi_->project, *v, *o);
            return "Groupe d'alarmes " + hmi::objectGroupOf(*v, *o) + (link.empty() ? std::string{} : " \xE2\x86\x92 " + link)
                 + countTag(std::size(hmi::pub::kAlarmInfo));
        }
        case NodeKind::HmiInstGroupVar: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            const auto k = subOf(n) & 0xFF;
            if (!o || k >= std::size(hmi::pub::kAlarmInfo)) return {};
            const auto& info = hmi::pub::kAlarmInfo[k];
            std::string value;
            if (hmi::pub::same(info.name, "AlarmGroup")) value = hmi::objectGroupOf(*v, *o);
            else if (hmi::pub::same(info.name, "AlarmLinkedGroup")) value = instLinkedGroup(hmi_->project, *v, *o);
            return std::string(info.name) + " : " + std::string(info.type) + (value.empty() ? std::string{} : "  = " + value);
        }
        case NodeKind::HmiInstAlarms: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiObjectById(v, subOf(n));
            if (!o) return {};
            return std::string(hmi::pub::kAlarmsRoot) + countTag(hmi::pub::objectAlarmNames(hmi_->project, *v, *o).size());
        }
        case NodeKind::HmiInstAlarm: {
            const auto a = instAlarmAt(hmi_.get(), i, subOf(n));
            return a.alarm ? a.alarm->local : std::string{};
        }
        case NodeKind::HmiInstAlarmVar: {
            const auto a = instAlarmAt(hmi_.get(), i & kInstViewMask, subOf(n));
            const auto m = i >> 24;
            if (!a.alarm || m >= std::size(hmi::pub::kAlarmMembers)) return {};
            const auto& mem = hmi::pub::kAlarmMembers[m];
            const auto value = instAlarmValue(hmi_->project, a, mem.name);
            return std::string(mem.name) + " : " + std::string(mem.type) + (value.empty() ? std::string{} : "  = " + value);
        }
        case NodeKind::HmiInstVar: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            if (!o) return {};
            const auto members = hmi::pub::objectMembers(*o, v);
            const auto k = subOf(n) & 0xFF;
            if (k >= members.size()) return {};
            const auto& m = members[k];
            return m.name + " : " + m.type + (m.value.empty() ? std::string{} : "  = " + m.value);
        }
        case NodeKind::HmiUsedVariable: {
            if (!hmi_) return {};
            const auto& all = usedVariables();
            if (i >= all.size()) return {};
            const auto& u = all[i];
            return u.path + (u.hmi ? "   IHM" : "   automate") + "   " + std::to_string(u.uses)
                + (u.uses > 1 ? " emplois" : " emploi");
        }

        case NodeKind::TablesFolder:   return "Tables d'animation (" + std::to_string(p.animationTables.size()) + ")";
        case NodeKind::AnimationTable:
            if (i >= p.animationTables.size()) return {};
            return std::string(p.strings.text(p.animationTables[i].name)) + "  [" + std::to_string(p.animationTables[i].entries.size()) + "]";
        case NodeKind::ProgramUnitVars: return "Variables";
        // Lot API 7 : un membre deplie (".sorties : Q", "[4] : BOOL   // vanne 4",
        // "[0 ... 99] : paquet de 100").
        case NodeKind::MemberNode:      return i < members_.size() ? members_[i].text : std::string{};
        case NodeKind::FilterHit:       break;      // lot API 8 : l'arbre du projet (traite plus haut)
        case NodeKind::ToolRow:         break;
        case NodeKind::VersionsMore:    break;
        case NodeKind::PinsFolder: case NodeKind::PinItem: case NodeKind::RecentFolder: case NodeKind::RecentItem: break;
        }
        return {};
    }

    // 1.11 (chantier T3, C4) : ce que les deux filtres de l'arbre retiennent (lu dans le cache).
    std::uint8_t ProjectTreeModel::buildFlags(ui::NodeId n) const {
        if (!buildState_) return 0;
        const auto i = indexOf(n);
        const hmi::build::State* st = nullptr;
        switch (kindOf(n)) {
        case NodeKind::Section:    st = buildState_->section(i); break;
        case NodeKind::DfbSection: st = buildState_->dfbSection(i); break;
        case NodeKind::DfbType:    st = buildState_->dfb(i); break;
        case NodeKind::HmiGeneralScript:
            if (const auto* sc = hmi_ ? byId(hmi_->project.programs.scripts, i) : nullptr) st = buildState_->script(sc->id);
            break;
        case NodeKind::HmiViewScript:
            if (const auto* v = hmiViewById(hmi_.get(), i); v && subOf(n) < v->scripts.size()) st = buildState_->script(v->scripts[subOf(n)].id);
            break;
        default: break;
        }
        if (!st) return 0;
        return static_cast<std::uint8_t>((st->compiles() ? 0 : BuildNotCompiling) | (st->generated() ? 0 : BuildNotGenerated));
    }

    ui::CellStyle ProjectTreeModel::style(ui::NodeId n) const {
        ui::CellStyle s;
        // ---- Lot API 8 : l'arbre du projet (2e partie : le domaine, le compteur, le point, les mises a jour) ----
        if (!decoGuard_) {
            decoGuard_ = true;
            s = style(n);
            decoGuard_ = false;
            decorate(n, s);
            return s;
        }
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : l'arbre du projet (un resultat du contenu : son icone, ou il est en gris) ----
        if (kindOf(n) == NodeKind::FilterHit) {
            if (indexOf(n) < filterHits_.size()) {
                s.icon = filterHits_[indexOf(n)].icon;
                s.hint = filterHits_[indexOf(n)].hint;
            }
            return s;
        }
        // Les outils sortis de l'arbre : un bouton par outil (l'icone de son
        // ancien noeud, un libelle court), teinte du domaine (API accent, IHM info).
        if (kindOf(n) == NodeKind::ToolRow) {
            const bool hmiRow = indexOf(n) == 1;
            static const char* const apiLabels[] = {"Statistiques"};
            static const char* const hmiLabels[] = {"\xC3\x89" "changes", "Rechercher", "Outil Modbus", "G\xC3\xA9n\xC3\xA9rer", "Compiler"};
            for (std::size_t k = 0; k < toolCount(hmiRow); ++k)
                s.chips.push_back({style(toolNode(hmiRow, k)).icon, hmiRow ? hmiLabels[k] : apiLabels[k]});
            s.iconTone = hmiRow ? ui::Tone::Info : ui::Tone::Accent;
            return s;
        }
        // Un raccourci (Epingles, Recents) : le style du noeud vise, son domaine en gris.
        if (kindOf(n) == NodeKind::PinItem || kindOf(n) == NodeKind::RecentItem) {
            const auto t = shortcutTarget(n);
            if (t != ui::kInvalidNode) s = style(t);
            s.chips.clear();
            const auto& list = kindOf(n) == NodeKind::PinItem ? pins_ : recents_;
            if (indexOf(n) < list.size()) s.hint = list[indexOf(n)].hint;
            return s;
        }
        // La pastille rouge des expressions impossibles (le dernier Generer /
        // Compiler) : sur Vues, le nombre ; sur le titre IHM, "N erreur(s)".
        if (hmiExprErrors_ > 0 && !styleGuard_
            && ((kindOf(n) == NodeKind::HmiViews && hmiExprErrorsInViews_ > 0) || kindOf(n) == NodeKind::HmiFolder)) {
            styleGuard_ = true;
            s = style(n);
            styleGuard_ = false;
            s.badge = kindOf(n) == NodeKind::HmiViews ? std::to_string(hmiExprErrorsInViews_)
                                                      : std::to_string(hmiExprErrors_) + (hmiExprErrors_ > 1 ? " erreurs" : " erreur");
            s.badgeTone = ui::Tone::Error;
            return s;
        }
        // ---- fin Lot API 8 : l'arbre du projet ----
        const auto i = indexOf(n);

        switch (kindOf(n)) {
        case NodeKind::Root:                s.bold = true; s.icon = ui::Icon::Project; break;
        case NodeKind::ApiFolder:           s.bold = true; s.icon = ui::Icon::Cpu; break;
        case NodeKind::ApiSimulation:
            s.icon = ui::Icon::Cpu;             // lot API 8 : Simulation > Automate
            if (!simBadge_.empty()) { s.badge = simBadge_; s.badgeTone = simTone_; }
            break;
        case NodeKind::ApiStatistics:       s.icon = ui::Icon::Chart; break;
        // ---- Lot API 8 : Centre de simulation ----
        case NodeKind::SimFolder:           s.bold = true; s.icon = ui::Icon::Play; break;
        case NodeKind::SimOverview:         s.icon = ui::Icon::Station; break;
        case NodeKind::SimEquipment:        s.icon = ui::Icon::Network; break;
        case NodeKind::SimDebug:            s.icon = ui::Icon::Halt; break;
        case NodeKind::SimForcing:          s.icon = ui::Icon::Force; break;
        case NodeKind::SimTrends:           s.icon = ui::Icon::Chart; break;
        case NodeKind::SimJournal:          s.icon = ui::Icon::Document; break;
        // ---- fin Lot API 8 ----
        case NodeKind::ConfigurationFolder:
        case NodeKind::TypesFolder:
        case NodeKind::DfbFolder:
        case NodeKind::UnitsFolder:
        case NodeKind::TaskFolder:
        case NodeKind::ExecOrderFolder:
        case NodeKind::VariablesFolder:
        case NodeKind::SubroutinesFolder:
        case NodeKind::MacroFolder:
        case NodeKind::TablesFolder:
            s.bold = true; s.icon = ui::Icon::Folder;
            // 1.11 (T3, C4, tranche 10) : Blocs DFB et Unites de programme resument les leurs, replies au
            // depart : le client voit ✓ ou « n ✕/⊘ » sans rien deplier.
            if (buildState_ && (kindOf(n) == NodeKind::DfbFolder || kindOf(n) == NodeKind::UnitsFolder)) {
                const bool dfbs = kindOf(n) == NodeKind::DfbFolder;
                BuildTally t;
                for (Index p = 0; p < project_->pous.size(); ++p) {
                    const auto& pou = project_->pous[p];
                    if (dfbs && pou.kind == PouKind::FunctionBlockType)
                        tally(t, project_->strings.text(pou.name), buildState_->dfb(p));
                    else if (!dfbs && pou.kind == PouKind::ProgramUnit)
                        for (const auto si : pou.sections)
                            if (si < project_->sections.size()) tally(t, project_->strings.text(project_->sections[si].name), buildState_->section(si));
                }
                summaryTrail(s, t, dfbs ? "bloc DFB" : "section", dfbs ? "blocs DFB" : "");
            }
            break;

        case NodeKind::ElementaryFolder:
        case NodeKind::DdtInstanceFolder:
        case NodeKind::DfbInstanceFolder:   s.icon = ui::Icon::Folder; break;

        case NodeKind::ListVariable: {
            const auto at = variableOf(n);
            if (at == kNoIndex) break;
            const auto& v = project_->variables[at];
            s.icon = v.type.klass == TypeClass::FunctionBlock ? ui::Icon::FunctionBlock
                : v.located ? ui::Icon::LocatedVariable
                : ui::Icon::Variable;
            if (v.located) s.iconColor = gfx::Color::rgb(0x9CDCFE);
            break;
        }
        case NodeKind::Subroutine:          s.icon = ui::Icon::Section; break;
        case NodeKind::Macro:               s.icon = ui::Icon::Play; break;
        case NodeKind::MacroSubFolder:      s.bold = true; s.icon = ui::Icon::Folder; s.iconTone = ui::Tone::Warning; break;   // lot macros 1

        case NodeKind::DfbInputs:
        case NodeKind::DfbOutputs:
        case NodeKind::DfbInOut:
        case NodeKind::DfbPublicVars:
        case NodeKind::DfbPrivateVars:
        case NodeKind::DfbSectionsFolder:
        case NodeKind::ProgramUnitVars:     s.icon = ui::Icon::Folder; break;

        case NodeKind::Cpu:                 s.icon = ui::Icon::Cpu; break;
        case NodeKind::RackFolder:          s.icon = ui::Icon::Rack; break;
        case NodeKind::ApiChannels:
            s.icon = ui::Icon::Module;
            if (configCounts().faulty) { s.badge = std::to_string(configCounts().faulty) + " \xE2\x9A\xA0"; s.badgeTone = ui::Tone::Warning; }
            break;
        case NodeKind::ApiNetwork:          s.icon = ui::Icon::Network; break;
        case NodeKind::ApiMemory:           s.icon = ui::Icon::Chart; break;
        case NodeKind::Rack:                s.icon = ui::Icon::Rack; break;
        case NodeKind::HwModule:            s.icon = ui::Icon::Module; break;
        case NodeKind::Task:
        case NodeKind::ExecOrderTask:
            s.icon = ui::Icon::Task;
            if (kindOf(n) == NodeKind::Task && buildState_ && i < project_->tasks.size()) {   // 1.11 (T3, C4, tranche 10)
                BuildTally t;
                for (const auto si : project_->tasks[i].sections)
                    if (si < project_->sections.size()) tally(t, project_->strings.text(project_->sections[si].name), buildState_->section(si));
                summaryTrail(s, t, "section");
            }
            break;

        case NodeKind::ExecStep: {
            s.icon = ui::Icon::Section;
            if (i >= project_->tasks.size()) break;
            const auto steps = domain::executionOrder(*project_, project_->tasks[i].name);
            const auto rank = subOf(n);
            if (rank >= steps.size()) break;
            codeIconStyle(s, project::codeicons::sectionIcon(*project_, steps[rank].section));
            // Une section que le simulateur ne sait pas executer est marquee
            // comme une variable declaree jamais utilisee l'est deja ailleurs :
            // meme couleur pour "c'est la, et ca ne fait rien".
            if (project_->sections[steps[rank].section].language != PouLanguage::ST) {
                s.fg = gfx::Color::rgb(0xDCA032);
                s.iconColor = s.fg;
            }
            break;
        }
        case NodeKind::AnimationTable:      s.icon = ui::Icon::AnimationTable; break;

        case NodeKind::DerivedType:
            s.icon = ui::Icon::DerivedType;
            if (project_->derivedTypes[i].instanceCount == 0) {
                s.fg = gfx::Color::rgb(0xDCA032);        // declared but never used
                s.iconColor = s.fg;
            }
            codeIconStyle(s, project::codeicons::typeIcon(*project_, i));      // 1.8.0 : l'icone au choix
            break;
        case NodeKind::DfbType:
            s.icon = ui::Icon::FunctionBlock;
            if (project_->pous[i].instanceCount == 0) {
                s.fg = gfx::Color::rgb(0xDCA032);
                s.iconColor = s.fg;
            }
            codeIconStyle(s, project::codeicons::pouIcon(*project_, i));
            if (buildState_) buildTrail(s, buildState_->dfb(i));   // 1.11 (chantier T3, C4)
            break;
        case NodeKind::ProgramUnit:
            s.icon = ui::Icon::Program;
            codeIconStyle(s, project::codeicons::pouIcon(*project_, i));
            if (buildState_ && i < project_->pous.size()) {          // 1.11 (T3, C4, tranche 10) : le resume de ses sections
                BuildTally t;
                for (const auto si : project_->pous[i].sections)
                    if (si < project_->sections.size()) tally(t, project_->strings.text(project_->sections[si].name), buildState_->section(si));
                summaryTrail(s, t, "section");
            }
            break;
        case NodeKind::Section:
        case NodeKind::DfbSection:
            s.icon = ui::Icon::Section;
            codeIconStyle(s, project::codeicons::sectionIcon(*project_, i));
            if (buildState_)                                      // 1.11 (chantier T3, C4)
                buildTrail(s, kindOf(n) == NodeKind::DfbSection ? buildState_->dfbSection(i) : buildState_->section(i));
            break;

        case NodeKind::DerivedField:
        case NodeKind::DfbVariable: {
            const auto& v = project_->variables[i];
            s.icon = v.located ? ui::Icon::LocatedVariable : ui::Icon::Variable;
            if (v.located) s.iconColor = gfx::Color::rgb(0x9CDCFE);
            break;
        }
        // Lot API 7 : un membre - sa nature, son sens (calcules en le rangeant).
        case NodeKind::MemberNode:
            if (i < members_.size()) {
                s.icon = members_[i].icon;
                s.iconTone = members_[i].iconTone;
                s.fgTone = members_[i].fgTone;
            }
            break;
        case NodeKind::FilterHit: break;     // lot API 8 : l'arbre du projet (traite plus haut)
        case NodeKind::ToolRow:   break;
        case NodeKind::VersionsMore:         // lot API 8 : l'arbre du projet (les versions en bref)
            s.icon = ui::Icon::History;
            s.fgTone = ui::Tone::Accent;
            break;
        case NodeKind::PinsFolder:   s.bold = true; s.icon = ui::Icon::Star; s.iconTone = ui::Tone::Warning; break;
        case NodeKind::RecentFolder: s.bold = true; s.icon = ui::Icon::History; s.iconTone = ui::Tone::Muted; break;
        case NodeKind::PinItem: case NodeKind::RecentItem: break;   // traites plus haut

        case NodeKind::HmiFolder:        s.bold = true; s.icon = ui::Icon::Screen; break;
        // Lot 21 : les versions - une pastille de couleur par etat.
        case NodeKind::VersionsFolder:
            s.bold = true;
            s.icon = ui::Icon::History;
            if (versions_.size() > 1) s.badge = std::to_string(versions_.size() - 1);
            break;
        case NodeKind::HmiListFolder:    s.bold = true; s.icon = ui::Icon::Folder; s.iconTone = ui::Tone::Warning; break;
        case NodeKind::VersionItem:
            if (i < versions_.size()) {
                s.icon = ui::Icon::History;
                s.iconTone = versions_[i].tone;
                s.badge = versions_[i].badge;
                if (versions_[i].number == 0) s.fgTone = ui::Tone::Warning;
            }
            break;
        case NodeKind::HmiConfig:        s.icon = ui::Icon::Settings; break;
        case NodeKind::HmiExternalFiles: s.icon = ui::Icon::Document; break;
        case NodeKind::HmiResources:     s.icon = ui::Icon::Image; break;
        case NodeKind::HmiExchange:      s.icon = ui::Icon::Export; break;
        case NodeKind::HmiViews:         s.bold = true; s.icon = ui::Icon::Folder; break;
        case NodeKind::HmiSymbolsFolder: s.bold = true; s.icon = ui::Icon::Folder; s.iconTone = ui::Tone::Accent; break;
        case NodeKind::HmiViewFolder:
            s.icon = i == 0 ? ui::Icon::Layers : ui::Icon::Folder;
            if (i == 2) s.iconTone = ui::Tone::Accent;
            break;
        case NodeKind::HmiTemplateFolder: s.icon = ui::Icon::Folder; break;
        case NodeKind::HmiView:
            s.icon = ui::Icon::Screen;
            if (const auto* v = hmiViewById(hmi_.get(), i); v && hmi_->project.config.startView == v->id)
                s.bold = true;                     // la vue de demarrage
            // Lot 6 : un ecran modele (et les bandes d'en-tete / de pied) : des calques.
            if (const auto* v = hmiViewById(hmi_.get(), i); v && v->role != "vue" && !v->role.empty()) {
                s.icon = ui::Icon::Layers;
                s.iconTone = ui::Tone::Accent;
                s.badge = v->role == "modele" ? "mod\xC3\xA8le" : v->role == "entete" ? "en-t\xC3\xAAte"
                        : v->role == "pied" ? "pied" : v->role == "popup" ? "popup" : v->role;
                if (v->role == "popup") s.icon = ui::Icon::Screen;
                if (v->role == "symbole") s.icon = ui::Icon::Module;   // lot 10
            }
            break;
        case NodeKind::HmiViewPart:
            switch (static_cast<HmiPart>(subOf(n))) {
            case HmiPart::Objects:    s.icon = ui::Icon::Module; break;
            case HmiPart::Scripts:    s.icon = ui::Icon::Code; break;
            case HmiPart::Animations: s.icon = ui::Icon::Play; break;
            case HmiPart::Layers:     s.icon = ui::Icon::Layers; break;
            case HmiPart::Groups:     s.icon = ui::Icon::Folder; break;
            case HmiPart::Functions:  s.icon = ui::Icon::FunctionBlock; s.iconTone = ui::Tone::InOut; break;   // 1.11.10 : en violet
            case HmiPart::Popups:     s.icon = ui::Icon::Screen; s.iconTone = ui::Tone::Family2; break;
            case HmiPart::Count:      break;
            }
            break;
        case NodeKind::HmiSimulation:    s.icon = ui::Icon::Screen; break;   // lot API 8 : Simulation > IHM
        case NodeKind::HmiGenerate:      s.icon = ui::Icon::Analyze; break;
        case NodeKind::HmiStyles:        s.icon = ui::Icon::Settings; s.iconTone = ui::Tone::Accent; break;   // lot 12
        case NodeKind::HmiFind:          s.icon = ui::Icon::Search; break;
        case NodeKind::HmiTests:         s.icon = ui::Icon::Ok; s.iconTone = ui::Tone::Accent; break;   // lot 13
        case NodeKind::HmiCompile:       s.icon = ui::Icon::Code; break;
        case NodeKind::HmiScripts:       s.icon = ui::Icon::Code; break;
        case NodeKind::HmiAlarms:
            s.icon = ui::Icon::Warning;
            if (hmi_ && !hmi_->project.alarms.empty()) s.iconTone = ui::Tone::Warning;
            break;
        case NodeKind::HmiRecipes:       s.icon = ui::Icon::AnimationTable; break;
        case NodeKind::HmiUsers:
            s.icon = ui::Icon::User;
            if (hmi_ && hmi_->project.security.enabled) s.iconTone = ui::Tone::Ok;
            break;
        case NodeKind::HmiHistory:       s.icon = ui::Icon::Document; break;
        case NodeKind::HmiLanguages:                                               // lot 13 : un globe, vert a plusieurs langues
            s.icon = ui::Icon::Globe;
            if (hmi_ && hmi_->project.languages.list.size() > 1) s.iconTone = ui::Tone::Ok;
            break;
        case NodeKind::HmiUnits:
            s.icon = ui::Icon::Variable;
            if (hmi_ && !hmi_->project.displays.empty()) s.iconTone = ui::Tone::Ok;
            break;
        case NodeKind::HmiComm:                                                    // lot 14 : vert relie a un automate reel ou a des equipements
            s.icon = ui::Icon::Network;
            if (hmi_ && (hmi_->project.comm.modbus() || !hmi_->project.equipments.empty())) s.iconTone = ui::Tone::Ok;
            break;
        case NodeKind::HmiModbusTool:                                              // lot 15
            s.icon = ui::Icon::Settings;
            break;
        case NodeKind::HmiNotify:                                                  // lot 14 : vert, actives
            s.icon = ui::Icon::Mail;
            if (hmi_ && hmi_->project.notify.enabled) s.iconTone = ui::Tone::Ok;
            break;
        case NodeKind::HmiReports:
            s.icon = ui::Icon::Document;
            if (hmi_ && !hmi_->project.reports.empty()) s.iconTone = ui::Tone::Ok;
            break;
        case NodeKind::HmiWeb:                                                     // lot 14 : vert, le serveur web actif
            s.icon = ui::Icon::Globe;
            if (hmi_ && hmi_->project.web.enabled) s.iconTone = ui::Tone::Ok;
            break;
        case NodeKind::HmiStation:                                                 // lot 14 : orange, un kiosque sans mot de passe
            s.icon = ui::Icon::Station;
            if (hmi_ && hmi_->project.station.kiosk && hmi_->project.station.exitHash.empty()) s.iconTone = ui::Tone::Warning;
            break;

        // ---- lot 5 ------------------------------------------------------------
        case NodeKind::HmiObject:
        case NodeKind::HmiGroupEntry: {
            const auto* o = hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n));
            s.icon = o ? kindIcon(o->kind) : ui::Icon::Module;
            if (o && kindOf(n) == NodeKind::HmiObject && !hmitree::overridesOf(*o).empty()) s.iconTone = ui::Tone::Accent;
            // 1.10 (chantier O) : le nombre de ses alarmes se voit sans deplier.
            if (o && kindOf(n) == NodeKind::HmiObject)
                if (const auto g = objectAlarms(hmiViewOf(n), o->id)) {
                    s.badge = std::to_string(g->count) + (g->count > 1 ? " alarmes" : " alarme");
                    s.badgeTone = ui::Tone::Warning;
                }
            break;
        }
        // ---- 1.10 (chantier O) : le noeud Alarmes, ses groupes, ses alarmes ----
        case NodeKind::HmiObjectAlarms:
            s.icon = ui::Icon::Warning;
            s.iconTone = ui::Tone::Warning;
            s.fgTone = ui::Tone::Warning;
            break;
        case NodeKind::HmiObjectAlarm: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, subOf(n));
            const auto g = o ? objectAlarms(v->id, o->id) : nullptr;
            const auto line = g ? alarmLineOf(*g, subOf(n) & 0xFF) : AlarmLine{};
            s.icon = line.group ? ui::Icon::Folder : ui::Icon::Warning;
            if (line.alarm) {
                const int p = line.alarm->priority;
                s.iconTone = p <= 1 ? ui::Tone::Error : p == 2 ? ui::Tone::Warning : p == 3 ? ui::Tone::Family2 : ui::Tone::Info;
                if (!line.alarm->active) { s.fgTone = ui::Tone::Muted; s.badge = "d\xC3\xA9" "coch\xC3\xA9" "e"; s.badgeTone = ui::Tone::Muted; }
                else if (line.alarm->overridden) { s.badge = "surcharg\xC3\xA9" "e"; s.badgeTone = ui::Tone::Info; }
            }
            break;
        }
        // ---- 1.10.2 (chantier A) : une famille, un parametre d'instance ----
        case NodeKind::HmiObjectFamily: {
            const auto f = static_cast<hmitree::Family>(subOf(n) & 0xFF);
            s.icon = hmitree::familyIcon(f);
            if (f == hmitree::Family::Actions || f == hmitree::Family::Links || f == hmitree::Family::Params) s.iconTone = ui::Tone::Accent;
            break;
        }
        case NodeKind::HmiObjectParam:
            s.icon = ui::Icon::Variable;
            s.fgTone = ui::Tone::Muted;
            break;
        case NodeKind::HmiObjectMarker: {
            // 1.10.3 (Q1103) : la ligne d'aide (Ctrl+D) est discrete.
            const auto* o = hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n));
            const auto lines = o ? hmitree::familyLines(hmi_->project, *o, hmitree::Family::Markers) : std::vector<hmitree::Line>{};
            const auto k = subOf(n) & 0xFF;
            const bool help = k < lines.size() && lines[k].icon == ui::Icon::Info;
            s.icon = help ? ui::Icon::Info : ui::Icon::Star;
            s.iconTone = help ? ui::Tone::Muted : ui::Tone::Warning;
            if (help) s.fgTone = ui::Tone::Muted;
            break;
        }
        case NodeKind::HmiObjectItem: {
            const auto* o = hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n));
            if (!o) break;
            const auto items = hmitree::overridesOf(*o);
            const auto k = subOf(n) & 0xFF;
            if (k < items.size()) s.icon = items[k].icon;
            s.fgTone = ui::Tone::Muted;
            break;
        }
        case NodeKind::HmiViewScript: {
            s.icon = ui::Icon::Code;
            if (const auto* v = hmiViewById(hmi_.get(), i); v && project_ && subOf(n) < v->scripts.size())
                codeIconStyle(s, project::codeicons::iconOf(*project_, core::codeicons::keyOf(core::codeicons::Kind::HmiScript, v->name, v->scripts[subOf(n)].name)));
            if (const auto* v = hmiViewById(hmi_.get(), i); v && buildState_ && subOf(n) < v->scripts.size())   // 1.11 (T3, C4)
                buildTrail(s, buildState_->script(v->scripts[subOf(n)].id));
            break;
        }
        case NodeKind::HmiAnimation:     s.icon = ui::Icon::Play; break;
        case NodeKind::HmiLayer:         s.icon = ui::Icon::Layers; break;
        case NodeKind::HmiAlarmGroup:    s.icon = ui::Icon::Folder; break;
        case NodeKind::HmiAlarm: {
            s.icon = ui::Icon::Warning;
            if (const auto* a = hmi_ ? byId(hmi_->project.alarms, i) : nullptr)
                s.iconTone = a->priority <= 1 ? ui::Tone::Error : a->priority == 2 ? ui::Tone::Warning : ui::Tone::Muted;
            break;
        }
        case NodeKind::HmiRecipe:        s.icon = ui::Icon::AnimationTable; break;
        case NodeKind::HmiRecord:        s.icon = ui::Icon::Document; break;
        case NodeKind::HmiUserGroup:     s.icon = ui::Icon::Folder; break;
        case NodeKind::HmiUser: {
            s.icon = ui::Icon::User;
            if (const auto* u = hmi_ ? byId(hmi_->project.security.users, i) : nullptr; u && !u->enabled)
                s.fgTone = ui::Tone::Muted;
            break;
        }
        case NodeKind::HmiRoles:         s.icon = ui::Icon::Settings; break;
        case NodeKind::HmiRole:          s.icon = ui::Icon::Lock; break;
        case NodeKind::HmiScriptsFolder:
            s.icon = ui::Icon::Code;
            if (hmi_ && buildState_) {                       // 1.11 (T3, C4, tranche 10) : le resume des scripts generaux
                BuildTally t;
                for (const auto& sc : hmi_->project.programs.scripts) tally(t, sc.name, buildState_->script(sc.id));
                summaryTrail(s, t, "script");
            }
            break;
        case NodeKind::HmiFunctionsFolder: s.icon = ui::Icon::FunctionBlock; break;
        case NodeKind::HmiFunction:
            s.icon = ui::Icon::FunctionBlock;
            s.iconTone = ui::Tone::Accent;
            if (const auto* f = hmi_ ? byId(hmi_->project.programs.functions, i) : nullptr; f && project_)
                codeIconStyle(s, project::codeicons::iconOf(*project_, core::codeicons::keyOf(core::codeicons::Kind::HmiFunction, {}, f->name)));
            break;
        case NodeKind::HmiVariablesFolder: s.icon = ui::Icon::Screen; break;
        case NodeKind::HmiUsedFolder:    s.icon = ui::Icon::Variable; break;
        case NodeKind::HmiGeneralScript: {
            s.icon = ui::Icon::Code;
            if (const auto* sc = hmi_ ? byId(hmi_->project.programs.scripts, i) : nullptr; sc && project_)
                codeIconStyle(s, project::codeicons::iconOf(*project_, core::codeicons::keyOf(core::codeicons::Kind::HmiScript, {}, sc->name)));
            if (const auto* sc = hmi_ ? byId(hmi_->project.programs.scripts, i) : nullptr; sc && sc->lang != hmi::ScriptLang::ST)
                s.fgTone = ui::Tone::Muted;          // edite, pas execute
            if (const auto* sc = hmi_ ? byId(hmi_->project.programs.scripts, i) : nullptr; sc && buildState_)   // 1.11 (T3, C4)
                buildTrail(s, buildState_->script(sc->id));
            break;
        }
        case NodeKind::HmiVariable: {
            s.icon = ui::Icon::Screen;
            // Lot 16 : une structure, un tableau, une variable liee.
            if (const auto* v = hmi_ ? byId(hmi_->project.programs.variables, i) : nullptr) {
                hmi::types::Spec spec;
                if (hmi::types::parseSpec(v->type, spec) && spec.array()) { s.icon = ui::Icon::AnimationTable; s.iconTone = ui::Tone::Family2; }
                else if (hmi::types::isComposite(v->type)) { s.icon = ui::Icon::DerivedType; s.iconTone = ui::Tone::Accent; }
                else if (v->bound()) s.icon = ui::Icon::LocatedVariable;
            }
            break;
        }
        case NodeKind::HmiTypesFolder:   s.icon = ui::Icon::DerivedType; break;
        case NodeKind::HmiTypeNode:      s.icon = ui::Icon::DerivedType; s.iconTone = ui::Tone::Accent; break;
        // ---- 1.10 (chantier O, decision 15) : le texte affiche d'une valeur, toString / fromString ----
        case NodeKind::HmiTypeValues:
        case NodeKind::HmiTypeOperators:
        case NodeKind::HmiObjectOperators: s.icon = ui::Icon::Folder; break;
        case NodeKind::HmiObjectOperator:  s.icon = ui::Icon::Code; break;
        // 1.11.10 : les fonctions d'un symbole, en violet ; une redefinition, une pastille.
        case NodeKind::HmiObjectFunctions:
        case NodeKind::HmiSymbolFunction:  s.icon = ui::Icon::FunctionBlock; s.iconTone = ui::Tone::InOut; break;
        case NodeKind::HmiObjectFunction: {
            s.icon = ui::Icon::FunctionBlock;
            s.iconTone = ui::Tone::InOut;
            const auto* o = hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n));
            const auto* fns = objectFunctions(hmi_.get(), o);
            const auto k = static_cast<std::size_t>(subOf(n) & 0xFF);
            if (fns && k < fns->size() && (*fns)[k].isVirtual && hmi::functionOverride(*o, (*fns)[k].name)) {
                s.badge = "red\xC3\xA9" "finie";
                s.badgeTone = ui::Tone::InOut;
            }
            break;
        }
        case NodeKind::HmiObjectPopups:    s.icon = ui::Icon::Screen; s.iconTone = ui::Tone::Family2; break;
        case NodeKind::HmiTypeValue: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            const auto k = static_cast<std::size_t>(subOf(n));
            s.icon = ui::Icon::DerivedType;
            if (ty && k < ty->values.size() && !ty->values[k].text.empty()) {
                s.badge = "'" + ty->values[k].text + "'";
                s.badgeTone = ui::Tone::Muted;
            }
            break;
        }
        case NodeKind::HmiTypeOperator: {
            const auto* ty = hmi_ ? byId(hmi_->project.programs.types, i) : nullptr;
            const auto k = static_cast<std::size_t>(subOf(n));
            s.icon = ui::Icon::Code;
            if (ty && k < ty->operators.size() && ty->kind == hmi::HmiTypeKind::Enumeration) {
                const auto& op = ty->operators[k];
                const bool conv = op.op == "TO" && op.right.empty();
                if (conv && hmivars::upperOf(op.left) == hmivars::upperOf(ty->name) && hmivars::upperOf(op.result) == "STRING") s.badge = "toString";
                else if (conv && hmivars::upperOf(op.left) == "STRING" && hmivars::upperOf(op.result) == hmivars::upperOf(ty->name)) s.badge = "fromString";
                s.badgeTone = ui::Tone::Info;
            }
            break;
        }
        case NodeKind::HmiVarFolder:     s.icon = ui::Icon::Folder; s.iconTone = ui::Tone::Warning; break;
        // ---- lot 9 : la pastille dit R (lecture seule) ou R/W
        case NodeKind::HmiSysFolder:     s.icon = ui::Icon::Settings; break;
        case NodeKind::HmiSysDomain:     s.icon = ui::Icon::Folder; break;
        case NodeKind::HmiSysVar:
            s.icon = ui::Icon::Lock;
            s.iconTone = ui::Tone::Muted;
            s.badge = "R";
            s.badgeTone = ui::Tone::Muted;
            break;
        case NodeKind::HmiInstFolder:    s.icon = ui::Icon::Screen; break;
        case NodeKind::HmiInstView:      s.icon = ui::Icon::Screen; s.iconTone = ui::Tone::Accent; break;
        case NodeKind::HmiInstViewInfo:  s.icon = ui::Icon::Folder; break;
        case NodeKind::HmiInstObject: {
            const auto* o = hmiObjectById(hmiViewById(hmi_.get(), i), subOf(n));
            s.icon = o ? kindIcon(o->kind) : ui::Icon::Module;
            break;
        }
        // ---- 1.11.1 (decision 108) : le groupe d'alarmes et les alarmes d'un objet ----
        case NodeKind::HmiInstAlarmGroup:
        case NodeKind::HmiInstAlarms:
            s.icon = ui::Icon::Warning;
            s.iconTone = ui::Tone::Warning;
            break;
        case NodeKind::HmiInstAlarm: {
            const auto a = instAlarmAt(hmi_.get(), i, subOf(n));
            s.icon = ui::Icon::Warning;
            if (a.alarm) {
                const int p = a.alarm->priority;
                s.iconTone = p <= 1 ? ui::Tone::Error : p == 2 ? ui::Tone::Warning : p == 3 ? ui::Tone::Family2 : ui::Tone::Info;
                if (!a.alarm->enabled) { s.fgTone = ui::Tone::Muted; s.badge = "d\xC3\xA9" "coch\xC3\xA9" "e"; s.badgeTone = ui::Tone::Muted; }
            }
            break;
        }
        case NodeKind::HmiInstViewVar:
        case NodeKind::HmiInstVar:
        case NodeKind::HmiInstParam:            // 1.11.1 : en lecture (on ecrit la variable reliee)
        case NodeKind::HmiInstGroupVar:         // 1.11.1 : en lecture
        case NodeKind::HmiInstAlarmVar: {       // 1.11.1 : Acked et Shelved en R/W
            bool rw = false;
            if (kindOf(n) == NodeKind::HmiInstViewVar) {
                rw = subOf(n) < std::size(hmi::pub::kViewInfo) && hmi::pub::kViewInfo[subOf(n)].access == hmi::pub::Access::ReadWrite;
            } else if (kindOf(n) == NodeKind::HmiInstAlarmVar) {
                const auto m = i >> 24;
                rw = m < std::size(hmi::pub::kAlarmMembers) && hmi::pub::kAlarmMembers[m].access == hmi::pub::Access::ReadWrite;
            } else if (kindOf(n) != NodeKind::HmiInstVar) {
                rw = false;
            } else if (const auto* o = hmiItemObject(hmiViewById(hmi_.get(), i), subOf(n))) {
                const auto members = hmi::pub::objectMembers(*o);
                const auto k = subOf(n) & 0xFF;
                rw = k < members.size() && members[k].access == hmi::pub::Access::ReadWrite;
            }
            s.icon = rw ? ui::Icon::Variable : ui::Icon::Lock;
            s.iconTone = rw ? ui::Tone::Ok : ui::Tone::Muted;
            s.badge = rw ? "R/W" : "R";
            s.badgeTone = rw ? ui::Tone::Ok : ui::Tone::Muted;
            break;
        }
        case NodeKind::HmiUsedVariable: {
            s.icon = ui::Icon::Variable;
            if (hmi_ && i < usedVariables().size() && usedVariables()[i].hmi) s.icon = ui::Icon::Screen;
            break;
        }
        }
        // ---- Lot API 8 : les pastilles du dossier Simulation ----
        if (isSimNode(n))
            for (const auto& b : simBadges_)
                if (b.kind == kindOf(n) && !b.text.empty()) {
                    s.badge = b.text;
                    s.badgeTone = b.tone;
                }
        // ---- fin Lot API 8 ----
        return s;
    }

    // ---- Lot API 8 : Centre de simulation ----
    bool ProjectTreeModel::isSimNode(ui::NodeId n) noexcept {
        const auto k = kindOf(n);
        return (k >= NodeKind::SimFolder && k <= NodeKind::SimJournal) || k == NodeKind::ApiSimulation || k == NodeKind::HmiSimulation;
    }

    bool ProjectTreeModel::setSimBadge(NodeKind kind, std::string text, ui::Tone tone) {
        for (auto& b : simBadges_)
            if (b.kind == kind) {
                if (b.text == text && b.tone == tone) return false;
                b.text = std::move(text);
                b.tone = tone;
                return true;
            }
        if (text.empty()) return false;
        simBadges_.push_back({kind, std::move(text), tone});
        return true;
    }
    // ---- fin Lot API 8 ----

    // ---------------------------------------------------------------- IHM ----
    void ProjectTreeModel::setHmi(std::shared_ptr<const hmi::Document> hmi) {
        hmi_ = std::move(hmi);
        hmiLinks_.clear();
        usedDirty_ = true;
        alarmCache_.clear();                   // 1.10 (chantier O)
        // Le contenu d'une vue change (une expression posee, une action...) :
        // les surcharges et les variables employees sous l'IHM suivent.
        if (hmi_)
            hmiLinks_ += hmi_->changed->connect([this](hmi::Id) {
                usedDirty_ = true;
                alarmCache_.clear();           // 1.10 : les alarmes des objets aussi
                childrenReady->emit(hmiFolderNode());
            });
        modelReset->emit();
    }

    // ---- 1.10 (chantier O) : les alarmes des objets ----
    void ProjectTreeModel::setObjectAlarms(ObjectAlarms fn) {
        objectAlarms_ = std::move(fn);
        alarmCache_.clear();
        if (hmi_) childrenReady->emit(hmiFolderNode());
    }

    std::shared_ptr<const alarmtree::Group> ProjectTreeModel::objectAlarms(std::uint64_t view, std::uint64_t object) const {
        if (!objectAlarms_ || !hmi_ || view == 0 || object == 0) return nullptr;
        const auto key = std::make_pair(view, object);
        if (const auto it = alarmCache_.find(key); it != alarmCache_.end()) return it->second;
        auto g = objectAlarms_(view, object);
        alarmCache_.emplace(key, g);
        return g;
    }

    bool ProjectTreeModel::hmiObjectAlarmOf(ui::NodeId n, std::string& name, std::string& path) const {
        if (kindOf(n) != NodeKind::HmiObjectAlarm || !hmi_) return false;
        const auto* v = hmiViewById(hmi_.get(), indexOf(n));
        const auto* o = hmiItemObject(v, subOf(n));
        const auto g = o ? objectAlarms(v->id, o->id) : nullptr;
        const auto line = g ? alarmLineOf(*g, subOf(n) & 0xFF) : AlarmLine{};
        if (!line.alarm) return false;
        name = line.alarm->localName.empty() ? line.alarm->name : line.alarm->localName;
        path = line.alarm->path;
        return true;
    }

    const std::vector<ProjectTreeModel::UsedVariable>& ProjectTreeModel::usedVariables() const {
        if (usedDirty_) {
            used_.clear();
            if (hmi_)
                for (auto& u : hmitree::usedVariables(hmi_->project)) used_.push_back({std::move(u.path), u.hmi, u.uses});
            usedDirty_ = false;
        }
        return used_;
    }

    std::uint64_t ProjectTreeModel::hmiObjectOf(ui::NodeId n) const {
        const auto* v = hmiViewById(hmi_.get(), indexOf(n));
        switch (kindOf(n)) {
        case NodeKind::HmiObject:
        case NodeKind::HmiGroupEntry:
            if (const auto* o = hmiObjectById(v, subOf(n))) return o->id;
            return 0;
        case NodeKind::HmiObjectItem:
        case NodeKind::HmiObjectAlarm:                 // 1.10 (chantier O)
        case NodeKind::HmiObjectOperator:
        case NodeKind::HmiObjectFamily:                // 1.10.2 (chantier A)
        case NodeKind::HmiObjectParam:
        case NodeKind::HmiObjectMarker:
            if (const auto* o = hmiItemObject(v, subOf(n))) return o->id;
            return 0;
        case NodeKind::HmiObjectOperators:
        case NodeKind::HmiObjectAlarms:                // 1.10 (chantier O)
        case NodeKind::HmiObjectFunctions:             // 1.11.10
        case NodeKind::HmiObjectPopups:
            if (const auto* o = hmiObjectById(v, subOf(n))) return o->id;
            return 0;
        case NodeKind::HmiObjectFunction:              // 1.11.10
            if (const auto* o = hmiItemObject(v, subOf(n))) return o->id;
            return 0;
        case NodeKind::HmiAnimation: {
            if (!v) return 0;
            const auto all = hmitree::animationsOf(*v);
            return subOf(n) < all.size() ? all[subOf(n)].first->id : 0;
        }
        default: return 0;
        }
    }

    int ProjectTreeModel::hmiRankOf(ui::NodeId n) const {
        switch (kindOf(n)) {
        case NodeKind::HmiObjectItem:
        case NodeKind::HmiObjectFamily:                 // 1.10.2 (chantier A) : la famille
        case NodeKind::HmiObjectParam:
        case NodeKind::HmiObjectMarker:
        case NodeKind::HmiObjectFunction: return static_cast<int>(subOf(n) & 0xFF);   // 1.11.10
        case NodeKind::HmiSymbolFunction:                                              // 1.11.10
        case NodeKind::HmiViewScript:
        case NodeKind::HmiAnimation:
        case NodeKind::HmiLayer:        return static_cast<int>(subOf(n));
        case NodeKind::HmiAlarmGroup:
        case NodeKind::HmiRole:
        case NodeKind::HmiUsedVariable: return static_cast<int>(indexOf(n));
        default:                        return -1;
        }
    }

    std::uint64_t ProjectTreeModel::hmiIdOf(ui::NodeId n) const {
        if (!hmi_) return 0;
        const auto i = indexOf(n);
        const auto& p = hmi_->project;
        switch (kindOf(n)) {
        case NodeKind::HmiAlarm:         if (const auto* a = byId(p.alarms, i)) return a->id; return 0;
        case NodeKind::HmiRecipe:
        case NodeKind::HmiRecord:        if (const auto* r = byId(p.recipes, i)) return r->id; return 0;
        case NodeKind::HmiUserGroup:     if (const auto* g = byId(p.security.groups, i)) return g->id; return 0;
        case NodeKind::HmiUser:          if (const auto* u = byId(p.security.users, i)) return u->id; return 0;
        case NodeKind::HmiGeneralScript: if (const auto* sc = byId(p.programs.scripts, i)) return sc->id; return 0;
        case NodeKind::HmiFunction:      if (const auto* f = byId(p.programs.functions, i)) return f->id; return 0;
        case NodeKind::HmiVariable:      if (const auto* v = byId(p.programs.variables, i)) return v->id; return 0;
        case NodeKind::HmiTypeValues: case NodeKind::HmiTypeValue:            // 1.10 (chantier O) : le type
        case NodeKind::HmiTypeOperators: case NodeKind::HmiTypeOperator:
        case NodeKind::HmiTypeNode:      if (const auto* ty = byId(p.programs.types, i)) return ty->id; return 0;   // lot 16
        default:                         return 0;
        }
    }

    std::uint64_t ProjectTreeModel::hmiRecordOf(ui::NodeId n) const {
        if (!hmi_ || kindOf(n) != NodeKind::HmiRecord) return 0;
        const auto* r = byId(hmi_->project.recipes, indexOf(n));
        const auto* rec = r ? byId(r->records, subOf(n)) : nullptr;
        return rec ? rec->id : 0;
    }

    std::string ProjectTreeModel::hmiUsedPathOf(ui::NodeId n) const {
        if (!hmi_ || kindOf(n) != NodeKind::HmiUsedVariable) return {};
        const auto& all = usedVariables();
        return indexOf(n) < all.size() ? all[indexOf(n)].path : std::string{};
    }

    // 1.11.1 (decision 108) : le chemin d'une variable de « Variables d'instances ».
    std::string ProjectTreeModel::hmiInstPathOf(ui::NodeId n) const {
        if (!hmi_) return {};
        const auto i = indexOf(n);
        const auto sub = subOf(n);
        const auto k = sub & 0xFF;
        switch (kindOf(n)) {
        case NodeKind::HmiInstViewVar: {
            const auto* v = hmiViewById(hmi_.get(), i);
            return v && sub < std::size(hmi::pub::kViewInfo) ? v->name + "." + std::string(hmi::pub::kViewInfo[sub].name) : std::string{};
        }
        case NodeKind::HmiInstVar: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, sub);
            if (!o) return {};
            const auto members = hmi::pub::objectMembers(*o);
            return k < members.size() ? hmi::pub::instancePath(*v, *o, members[k].name) : std::string{};
        }
        case NodeKind::HmiInstParam: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, sub);
            if (!o) return {};
            const auto params = hmi::pub::instanceParams(hmi_->project, *o);
            return k < params.size() ? hmi::pub::instancePath(*v, *o, params[k].name) : std::string{};
        }
        case NodeKind::HmiInstGroupVar: {
            const auto* v = hmiViewById(hmi_.get(), i);
            const auto* o = hmiItemObject(v, sub);
            return o && k < std::size(hmi::pub::kAlarmInfo) ? hmi::pub::instancePath(*v, *o, hmi::pub::kAlarmInfo[k].name) : std::string{};
        }
        case NodeKind::HmiInstAlarmVar: {
            const auto a = instAlarmAt(hmi_.get(), i & kInstViewMask, sub);
            const auto m = i >> 24;
            if (!a.alarm || m >= std::size(hmi::pub::kAlarmMembers)) return {};
            return a.view->name + "." + a.object->name + "." + std::string(hmi::pub::kAlarmsRoot) + "." + a.alarm->local + "."
                 + std::string(hmi::pub::kAlarmMembers[m].name);
        }
        default: return {};
        }
    }

    // Le debut commun des chemins sous un noeud de « Variables d'instances » (le filtre du volet).
    std::string ProjectTreeModel::hmiInstPrefixOf(ui::NodeId n) const {
        if (!hmi_) return {};
        const auto kind = kindOf(n);
        const auto i = kind == NodeKind::HmiInstAlarmVar ? indexOf(n) & kInstViewMask : indexOf(n);
        const auto* v = hmiViewById(hmi_.get(), i);
        if (!v) return {};
        const hmi::Object* o = nullptr;
        switch (kind) {
        case NodeKind::HmiInstView: case NodeKind::HmiInstViewInfo: case NodeKind::HmiInstViewVar:
            return v->name + ".";
        case NodeKind::HmiInstObject: case NodeKind::HmiInstAlarmGroup: case NodeKind::HmiInstAlarms:
            o = hmiObjectById(v, subOf(n));
            break;
        case NodeKind::HmiInstVar: case NodeKind::HmiInstParam: case NodeKind::HmiInstGroupVar:
        case NodeKind::HmiInstAlarm: case NodeKind::HmiInstAlarmVar:
            o = hmiItemObject(v, subOf(n));
            break;
        default:
            return {};
        }
        if (!o) return v->name + ".";
        const std::string base = v->name + "." + o->name + ".";
        const std::string alarms = base + std::string(hmi::pub::kAlarmsRoot) + ".";
        if (kind == NodeKind::HmiInstAlarms) return alarms;
        if (kind == NodeKind::HmiInstAlarm || kind == NodeKind::HmiInstAlarmVar) {
            const auto a = instAlarmAt(hmi_.get(), i, subOf(n));
            return a.alarm ? alarms + a.alarm->local + "." : alarms;
        }
        return base;
    }

    std::string ProjectTreeModel::hmiFolderOf(ui::NodeId n) const {
        if (!hmi_ || kindOf(n) != NodeKind::HmiVarFolder) return {};
        const auto all = hmi::types::allFolders(hmi_->project);
        return indexOf(n) < all.size() ? all[indexOf(n)] : std::string{};
    }

    void ProjectTreeModel::hmiChanged() {
        if (hmi_) childrenReady->emit(hmiFolderNode());
    }

    void ProjectTreeModel::setVersions(std::vector<VersionRow> rows) {
        versions_ = std::move(rows);
        if (hmi_) childrenReady->emit(versionsFolderNode());
    }

    int ProjectTreeModel::versionOf(ui::NodeId n) const {
        if (kindOf(n) != NodeKind::VersionItem) return -1;
        const auto i = indexOf(n);
        return i < versions_.size() ? versions_[i].number : -1;
    }

    // 1.11.12 : un switch sans "default" - le compilateur dit la partie oubliee.
    const char* ProjectTreeModel::hmiPartHint(HmiPart part) noexcept {
        switch (part) {
            case HmiPart::Objects:
                return "Objets : l'explorateur d'objets, \xC3\xA0 gauche (recherche, filtre par type).";
            case HmiPart::Scripts:
                return "Scripts de la vue (OnOpen, OnCycle, OnClose) : leur onglet \xC2\xAB Scripts \xC2\xB7 vue \xC2\xBB.";
            case HmiPart::Animations:
                return "Animations : les propri\xC3\xA9t\xC3\xA9s pilot\xC3\xA9" "es par une expression (=expression) ; "
                       "les actions : onglet Actions de l'inspecteur.";
            case HmiPart::Layers:
                return "Calques : le panneau Calques, en bas \xC3\xA0 gauche.";
            case HmiPart::Groups:
                return "Groupes : dans l'explorateur d'objets ; double-clic sur un groupe pour l'\xC3\xA9" "diter de l'int\xC3\xA9rieur.";
            case HmiPart::Functions:
                return "Fonctions du symbole : son sous-onglet \xC2\xAB Fonctions \xC2\xBB (virtuelles en violet, "
                       "red\xC3\xA9" "finissables par instance).";
            case HmiPart::Popups:
                return "Popups du symbole : son sous-onglet \xC2\xAB Popups \xC2\xBB (chaque instance ouvre la sienne).";
            case HmiPart::Count:
                break;
        }
        return nullptr;
    }

    std::uint64_t ProjectTreeModel::hmiViewOf(ui::NodeId n) const {
        const auto k = kindOf(n);
        const bool inView = k == NodeKind::HmiView || k == NodeKind::HmiViewPart || k == NodeKind::HmiObject
            || k == NodeKind::HmiObjectItem || k == NodeKind::HmiViewScript || k == NodeKind::HmiAnimation
            || k == NodeKind::HmiLayer || k == NodeKind::HmiGroupEntry
            || k == NodeKind::HmiObjectAlarms || k == NodeKind::HmiObjectAlarm      // 1.10 (chantier O)
            || k == NodeKind::HmiObjectOperators || k == NodeKind::HmiObjectOperator
            || (k >= NodeKind::HmiObjectFunctions && k <= NodeKind::HmiSymbolFunction)   // 1.11.10
            || k == NodeKind::HmiObjectFamily || k == NodeKind::HmiObjectParam
            || k == NodeKind::HmiObjectMarker;                                       // 1.10.2 (chantier A)
        if (!hmi_ || !inView) return 0;
        const auto* v = hmiViewById(hmi_.get(), indexOf(n));
        return v ? v->id : 0;
    }

    std::string ProjectTreeModel::hmiListFolderText(ui::NodeId n) const {
        if (!hmi_ || kindOf(n) != NodeKind::HmiListFolder) return {};
        const auto list = static_cast<hmi::fold::List>(subOf(n));
        const auto all = hmi::fold::allFolders(hmi_->project, list);
        const auto i = indexOf(n);
        if (i >= all.size()) return {};
        const std::size_t inside = hmi::fold::countIn(hmi_->project, list, all[i], true);
        return hmi::types::folderLeaf(all[i]) + "  [" + std::to_string(inside) + "]";
    }

    bool ProjectTreeModel::hmiListFolderOf(ui::NodeId n, int& list, std::string& folder) const {
        if (!hmi_) return false;
        const auto kind = kindOf(n);
        if (kind == NodeKind::HmiListFolder) {
            const auto l = static_cast<hmi::fold::List>(subOf(n));
            const auto all = hmi::fold::allFolders(hmi_->project, l);
            if (indexOf(n) >= all.size()) return false;
            list = static_cast<int>(l);
            folder = all[indexOf(n)];
            return true;
        }
        if (const auto l = hmilists::listOfNode(kind, indexOf(n))) {
            list = static_cast<int>(*l);
            folder.clear();
            return true;
        }
        return false;
    }

    int ProjectTreeModel::hmiListOf(ui::NodeId n) const {
        if (!hmi_) return -1;
        switch (kindOf(n)) {
            case NodeKind::HmiView: {
                const auto* v = hmiViewById(hmi_.get(), indexOf(n));
                return v ? static_cast<int>(hmi::fold::listOfView(*v)) : -1;
            }
            case NodeKind::HmiGeneralScript: return static_cast<int>(hmi::fold::List::Scripts);
            case NodeKind::HmiTypeNode:      return static_cast<int>(hmi::fold::List::Types);
            default:                         return -1;
        }
    }

    ui::NodeId ProjectTreeModel::hmiListFolderNode(int list, const std::string& folder) const {
        if (!hmi_ || list < 0 || list >= static_cast<int>(hmi::fold::List::Count)) return ui::kInvalidNode;
        const auto l = static_cast<hmi::fold::List>(list);
        if (folder.empty()) {
            switch (l) {
                case hmi::fold::List::Views:     return pack(NodeKind::HmiViewFolder, 1);
                case hmi::fold::List::Popups:    return pack(NodeKind::HmiViewFolder, 2);
                case hmi::fold::List::Templates: return pack(NodeKind::HmiTemplateFolder, 0);
                case hmi::fold::List::Headers:   return pack(NodeKind::HmiTemplateFolder, 1);
                case hmi::fold::List::Footers:   return pack(NodeKind::HmiTemplateFolder, 2);
                case hmi::fold::List::Symbols:   return pack(NodeKind::HmiSymbolsFolder, 0);
                case hmi::fold::List::Scripts:   return pack(NodeKind::HmiScriptsFolder, 0);
                case hmi::fold::List::Types:     return pack(NodeKind::HmiTypesFolder, 0);
                default:                         return ui::kInvalidNode;
            }
        }
        const auto all = hmi::fold::allFolders(hmi_->project, l);
        for (std::size_t i = 0; i < all.size(); ++i)
            if (hmi::fold::sameFolder(all[i], folder))
                return pack(NodeKind::HmiListFolder, static_cast<Index>(i), static_cast<Index>(l));
        return ui::kInvalidNode;
    }

    ui::NodeId ProjectTreeModel::hmiViewNode(std::uint64_t viewId) const {
        const auto* v = hmiViewById(hmi_.get(), viewId & kMask28);
        return v && v->id == viewId ? pack(NodeKind::HmiView, static_cast<Index>(viewId & kMask28))
                                    : ui::kInvalidNode;
    }

    // ============================================================ sections ======
    SectionTableModel::SectionTableModel(ProjectRef project) : project_(std::move(project)) {}

    std::size_t SectionTableModel::rowCount() const { return project_->sections.size(); }

    std::string SectionTableModel::headerText(std::size_t c) const {
        switch (c) {
        case Name:       return "Name";
        case Task:       return "Task";
        case Language:   return "Language";
        case Lines:      return "Lines";
        case Activation: return "Activation condition";
        default:         return {};
        }
    }

    std::string SectionTableModel::cellText(ui::RowIndex r, std::size_t c) const {
        const auto& s = project_->sections[r];
        switch (c) {
        case Name:       return std::string(project_->strings.text(s.name));
        case Task: {
            const auto task = project_->strings.text(s.task);
            return task.empty() ? std::string("-") : std::string(task);   // a POU body
        }
        case Language:   return std::string(toString(s.language));
        case Lines:      return std::to_string(s.lineCount);
        case Activation: return std::string(project_->strings.text(s.activationCondition));
        default:         return {};
        }
    }

    ui::CellStyle SectionTableModel::cellStyle(ui::RowIndex r, std::size_t c) const {
        ui::CellStyle st;
        if (c == Name) st.icon = ui::Icon::Section;
        if (c == Lines && project_->sections[r].statementCount == 0)
            st.fg = gfx::Color::rgb(0xDCA032);      // a section with no statement
        return st;
    }

    bool SectionTableModel::less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const {
        if (c == Lines) return project_->sections[a].lineCount < project_->sections[b].lineCount;
        return cellText(a, c) < cellText(b, c);
    }

    // ======================================================= configuration ======
    std::vector<ui::PropertyGrid::Category> buildConfigurationProperties(const Project& p) {
        using Category = ui::PropertyGrid::Category;
        using VT = ui::PropertyGrid::ValueType;

        std::vector<Category> cats;

        Category project{ "Projet", {}, {}, true };
        project.properties.push_back({ "Nom", p.header.projectName, VT::ReadOnly, "contentHeader/@name", {}, nullptr });
        project.properties.push_back({ "Version", p.header.projectVersion, VT::ReadOnly, {}, {}, nullptr });
        project.properties.push_back({ "Export\xC3\xA9 par", p.header.product, VT::ReadOnly, {}, {}, nullptr });
        project.properties.push_back({ "Export\xC3\xA9 le", p.header.exportedAt, VT::ReadOnly, {}, {}, nullptr });
        project.properties.push_back({ "Version de la DTD", p.header.dtdVersion, VT::ReadOnly, {}, {}, nullptr });
        project.properties.push_back({ "Fichier source", p.header.sourceFile, VT::ReadOnly, {}, {}, nullptr });
        cats.push_back(std::move(project));

        Category controller{ "Processeur", {}, {}, true };
        controller.properties.push_back({ "Famille", p.hardware.family, VT::ReadOnly, {}, {}, nullptr });
        controller.properties.push_back({ "R\xC3\xA9" "f\xC3\xA9rence", p.hardware.cpuReference, VT::ReadOnly, {}, {}, nullptr });
        controller.properties.push_back({ "Version du syst\xC3\xA8me", p.hardware.cpuFirmware, VT::ReadOnly, {}, {}, nullptr });
        controller.properties.push_back({ "Ressource", p.hardware.resourceName, VT::ReadOnly, {}, {}, nullptr });
        if (!p.hardware.busName.empty())
            controller.properties.push_back({ "Bus", p.hardware.busName, VT::ReadOnly, {}, {}, nullptr });
        if (!p.hardware.powerSupply.empty())
            controller.properties.push_back({ "Alimentation", p.hardware.powerSupply, VT::ReadOnly, {}, {}, nullptr });
        cats.push_back(std::move(controller));

        if (p.hardware.memory.declared) {
            const auto& mem = p.hardware.memory;
            Category memory{ "M\xC3\xA9moire des donn\xC3\xA9" "es", {}, {}, true };
            memory.properties.push_back({ "Bits internes (%M)", std::to_string(mem.internalBits),
                                         VT::ReadOnly, {}, {}, nullptr });
            memory.properties.push_back({ "Mots internes (%MW)", std::to_string(mem.internalWords),
                                         VT::ReadOnly, {}, {}, nullptr });
            memory.properties.push_back({ "Mots constants (%KW)", std::to_string(mem.constantWords),
                                         VT::ReadOnly, {}, {}, nullptr });
            memory.properties.push_back({ "%MW remis \xC3\xA0 z\xC3\xA9ro au d\xC3\xA9marrage \xC3\xA0 froid", mem.initialiseWords ? "oui" : "non",
                                         VT::ReadOnly, {}, {}, nullptr });
            memory.properties.push_back({ "D\xC3\xA9marrage automatique (RUN)", mem.autoRun ? "oui" : "non", VT::ReadOnly, {}, {}, nullptr });
            memory.properties.push_back({ "Modification de la configuration en marche",
                                         mem.changeConfigOnTheFly ? "permise" : "interdite",
                                         VT::ReadOnly, {}, {}, nullptr });
            cats.push_back(std::move(memory));
        }

        if (!p.hardware.racks.empty()) {
            Category racks{ "Racks et modules", {}, {}, true };
            for (const auto& rack : p.hardware.racks) {
                Category one{ "Rack " + std::to_string(rack.number) + "  " + rack.reference, {}, {}, false };
                one.properties.push_back({ "Embase", rack.description.empty() ? rack.reference
                                                                                : rack.description,
                                          VT::ReadOnly, {}, {}, nullptr });
                one.properties.push_back({ "Emplacements", std::to_string(rack.slotCount), VT::ReadOnly, {}, {}, nullptr });
                for (const auto& m : rack.modules) {
                    std::string value = m.reference;
                    if (m.points())
                        value += "   " + std::to_string(m.inputPoints) + " I / "
                        + std::to_string(m.outputPoints) + " Q"
                        + (m.pointsFromCatalog ? "  (du catalogue)" : "");
                    one.properties.push_back({
                        (m.slot < 0 ? std::string("Alimentation") : "Emplacement " + std::to_string(m.slot)),
                        value, VT::ReadOnly, m.description, {}, nullptr });
                }
                racks.children.push_back(std::move(one));
            }
            cats.push_back(std::move(racks));
        }

        Category tasks{ "T\xC3\xA2" "ches", {}, {}, true };
        for (const auto& t : p.tasks) {
            Category one{ std::string(p.strings.text(t.name)), {}, {}, true };
            one.properties.push_back({ "Type", t.type == "cyclic" ? std::string("cyclique") : t.type == "periodic" ? "p\xC3\xA9riodique, " + std::to_string(t.period) + " ms" : t.type, VT::ReadOnly, {}, {}, nullptr });
            one.properties.push_back({ "Chien de garde", std::to_string(t.watchdog) + " ms", VT::ReadOnly, {}, {}, nullptr });
            one.properties.push_back({ "Sections", std::to_string(t.sections.size()), VT::ReadOnly, {}, {}, nullptr });
            tasks.children.push_back(std::move(one));
        }
        cats.push_back(std::move(tasks));

        if (p.hardware.inferred) {
            Category notice{ "Mat\xC3\xA9riel", {}, {}, true };   // no .XHW imported yet
            notice.properties.push_back(
                { "Racks et modules", "pas dans cet export", VT::ReadOnly,
                 "Un .XPG porte le processeur, pas le rack, ses emplacements ni ses modules. "
                 "Importer le .XHW (ou le .XEF) de Control Expert remplit cette partie.",
                 {}, nullptr });
            cats.push_back(std::move(notice));
        }
        return cats;
    }

    std::vector<ui::PropertyGrid::Category> buildModuleProperties(const Project& p, const Module& m) {
        using Category = ui::PropertyGrid::Category;
        using VT = ui::PropertyGrid::ValueType;
        (void)p;

        auto row = [](std::string name, std::string value, std::string help = {}) {
            return ui::PropertyGrid::Property{ std::move(name), std::move(value), VT::ReadOnly,
                                              std::move(help), {}, nullptr };
            };

        std::vector<Category> cats;

        Category id{ "Module", {}, {}, true };
        id.properties.push_back(row("R\xC3\xA9" "f\xC3\xA9rence", m.reference));
        id.properties.push_back(row("Description",
            m.description.empty() ? "pas dans le catalogue" : m.description,
            m.knownReference ? "" : "Ajouter cette r\xC3\xA9" "f\xC3\xA9rence \xC3\xA0 resources/plc_catalog.txt "
            "lui donne une description"));
        id.properties.push_back(row("Famille", m.family));
        id.properties.push_back(row("Genre", std::string(toString(m.kind))));
        id.properties.push_back(row("Micrologiciel", m.firmware));
        id.properties.push_back(row("Rack", std::to_string(m.rack)));
        id.properties.push_back(row("Emplacement", m.slot < 0 ? "alimentation" : std::to_string(m.slot)));
        id.properties.push_back(row("Adresse topologique", m.topologicalAddress));
        if (m.points())
            id.properties.push_back(row("Voies",
                compte(m.inputPoints, "entr\xC3\xA9" "e", "entr\xC3\xA9" "es") + ", "
                + compte(m.outputPoints, "sortie", "sorties"),
                m.pointsFromCatalog
                ? "Ce module d\xC3\xA9" "clare un groupe de voies par sens : l'export ne donne "
                "pas de taille \xC3\xA0 mesurer. Le nombre vient du catalogue, pas du fichier."
                : "D\xC3\xA9" "duit des groupes de voies d\xC3\xA9" "clar\xC3\xA9s dans l'export"));
        cats.push_back(std::move(id));

        if (!m.channels.empty()) {
            Category ch{ "Voies (" + std::to_string(m.channels.size()) + ")", {}, {}, true };
            for (const auto& c : m.channels) {
                std::string value = std::string(toString(c.direction)) + "  " + c.role;
                ch.properties.push_back(row("Voie " + std::to_string(c.number), value,
                    "T\xC3\xA2" "che " + c.task + ", code fonction "
                    + std::to_string(c.functionCode) + " v"
                    + std::to_string(c.functionVersion)
                    + (c.iobFile.empty() ? "" : ", " + c.iobFile)));
            }
            cats.push_back(std::move(ch));
        }
        return cats;
    }

    std::vector<ui::PropertyGrid::Category> buildAnalysisSummary(const importer::AnalysisReport& r) {
        using Category = ui::PropertyGrid::Category;
        using VT = ui::PropertyGrid::ValueType;

        auto row = [](std::string name, std::string value) {
            return ui::PropertyGrid::Property{ std::move(name), std::move(value), VT::ReadOnly, {}, {}, nullptr };
            };

        std::vector<Category> cats;

        Category counts{ "Counts", {}, {}, true };
        counts.properties.push_back(row("Variables", std::to_string(r.totalVariables)));
        counts.properties.push_back(row("Global", std::to_string(r.globalVariables)));
        counts.properties.push_back(row("Local and parameters", std::to_string(r.localVariables)));
        counts.properties.push_back(row("Located I/O", std::to_string(r.locatedVariables)));
        counts.properties.push_back(row("Derived types", std::to_string(r.derivedTypes)));
        counts.properties.push_back(row("DFB types", std::to_string(r.functionBlockTypes)));
        counts.properties.push_back(row("DFB instances", std::to_string(r.dfbInstances)));
        counts.properties.push_back(row("Sections", std::to_string(r.sections)));
        cats.push_back(std::move(counts));

        Category code{ "Code", {}, {}, true };
        code.properties.push_back(row("Lines", std::to_string(r.linesOfCode)));
        code.properties.push_back(row("Statements", std::to_string(r.statements)));
        for (const auto& l : r.byLanguage)
            code.properties.push_back(row(std::string(toString(l.language)),
                compte(l.sections, "section", "sections") + ", "
                + compte(l.lines, "line", "lines")));
        cats.push_back(std::move(code));

        Category quality{ "Quality", {}, {}, true };
        quality.properties.push_back(row("Documented variables",
            std::to_string(static_cast<int>(r.documentationCoverage() * 100.f)) + " %"));
        quality.properties.push_back(row("Unused globals",
            std::to_string(r.count(importer::FindingKind::UnusedGlobalVariable))));
        quality.properties.push_back(row("Unused locals",
            std::to_string(r.count(importer::FindingKind::UnusedLocalVariable))));
        quality.properties.push_back(row("Unused DFB types",
            std::to_string(r.count(importer::FindingKind::UnusedFunctionBlockType))));
        quality.properties.push_back(row("Duplicate addresses",
            std::to_string(r.count(importer::FindingKind::DuplicateAddress))));
        quality.properties.push_back(row("Analysis time",
            std::to_string(static_cast<int>(r.analysisMilliseconds)) + " ms"));
        cats.push_back(std::move(quality));
        return cats;
    }

    std::vector<ui::PropertyGrid::Category> buildProjectStatus(const Project& p,
        const importer::AnalysisReport& r) {
        using Category = ui::PropertyGrid::Category;
        using VT = ui::PropertyGrid::ValueType;

        auto row = [](std::string name, std::string value) {
            return ui::PropertyGrid::Property{ std::move(name), std::move(value), VT::ReadOnly, {}, {}, nullptr };
            };

        std::size_t errors = 0, warnings = 0;
        for (const auto& f : r.findings) {
            if (f.severity == importer::Finding::Severity::Error)   ++errors;
            if (f.severity == importer::Finding::Severity::Warning) ++warnings;
        }

        std::vector<Category> cats;
        Category status{ "Status", {}, {}, true };
        status.properties.push_back(row("Overall", errors ? "Errors present" : warnings ? "Warnings" : "OK"));
        status.properties.push_back(row("Errors", std::to_string(errors)));
        status.properties.push_back(row("Warnings", std::to_string(warnings)));
        status.properties.push_back(row("Findings", std::to_string(r.findings.size())));
        cats.push_back(std::move(status));

        Category target{ "Target", {}, {}, true };
        target.properties.push_back(row("Family", p.hardware.family));
        target.properties.push_back(row("CPU", p.hardware.cpuReference));
        target.properties.push_back(row("OS version", p.hardware.cpuFirmware));
        target.properties.push_back(row("Source", p.header.sourceFile));
        cats.push_back(std::move(target));

        if (!p.partialDataNotices.empty()) {
            Category missing{ "Incomplete", {}, {}, true };
            for (std::size_t i = 0; i < p.partialDataNotices.size(); ++i)
                missing.properties.push_back(row("Notice " + std::to_string(i + 1),
                    p.partialDataNotices[i]));
            cats.push_back(std::move(missing));
        }
        return cats;
    }

    // ======================================================== diagnostics =======
    DiagnosticsTableModel::DiagnosticsTableModel(ProjectRef project, importer::AnalysisReport report)
        : project_(std::move(project)), report_(std::move(report)) {}

    std::string DiagnosticsTableModel::headerText(std::size_t c) const {
        switch (c) {
        case Severity: return "";
        case Kind:     return "Type";
        case Subject:  return "Subject";
        case Detail:   return "Message";
        default:       return {};
        }
    }

    std::string DiagnosticsTableModel::cellText(ui::RowIndex r, std::size_t c) const {
        const auto& f = report_.findings[r];
        switch (c) {
        case Severity:
            switch (f.severity) {
            case importer::Finding::Severity::Error:   return "Error";
            case importer::Finding::Severity::Warning: return "Warning";
            default:                                   return "Info";
            }
        case Kind:    return std::string(toString(f.kind));
        case Subject: return f.subject;
        case Detail:  return f.detail;
        default:      return {};
        }
    }

    ui::CellStyle DiagnosticsTableModel::cellStyle(ui::RowIndex r, std::size_t c) const {
        ui::CellStyle s;
        if (c != Severity) return s;
        switch (report_.findings[r].severity) {
        case importer::Finding::Severity::Error:
            s.fg = gfx::Color::rgb(0xD1585B); s.icon = ui::Icon::Error; break;
        case importer::Finding::Severity::Warning:
            s.fg = gfx::Color::rgb(0xDCA032); s.icon = ui::Icon::Warning; break;
        default:
            s.fg = gfx::Color::rgb(0x4A9BD8); s.icon = ui::Icon::Info; break;
        }
        s.iconColor = s.fg;
        return s;
    }

    bool DiagnosticsTableModel::less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const {
        const auto& fa = report_.findings[a];
        const auto& fb = report_.findings[b];
        switch (c) {
        case Severity: return static_cast<int>(fa.severity) > static_cast<int>(fb.severity);
        case Kind:     return toString(fa.kind) < toString(fb.kind);
        case Detail:   return fa.detail < fb.detail;
        case Subject:
        default:       return fa.subject < fb.subject;
        }
    }

    // ========================================================== libraries =======
    LibraryTreeModel::LibraryTreeModel(ProjectRef project) : project_(std::move(project)) {
        families_ = { LibraryKind::Standard, LibraryKind::Motion, LibraryKind::Process,
                     LibraryKind::Communication, LibraryKind::Safety, LibraryKind::User,
                     LibraryKind::Custom };
        byFamily_.resize(families_.size());
        for (Index i = 0; i < project_->libraries.size(); ++i) {
            const auto k = project_->libraries[i].kind;
            for (std::size_t f = 0; f < families_.size(); ++f)
                if (families_[f] == k) { byFamily_[f].push_back(i); break; }
        }
    }

    ui::NodeId LibraryTreeModel::root() const { return 1; }

    std::size_t LibraryTreeModel::childCount(ui::NodeId n) const {
        if (n == 1) return families_.size();
        if (n > 1 && n <= 1 + families_.size()) return byFamily_[n - 2].size();
        return 0;
    }

    ui::NodeId LibraryTreeModel::childAt(ui::NodeId n, std::size_t k) const {
        if (n == 1) return 2 + k;
        if (n > 1 && n <= 1 + families_.size())
            return 1000 + byFamily_[n - 2][k];
        return ui::kInvalidNode;
    }

    bool LibraryTreeModel::hasChildren(ui::NodeId n) const { return childCount(n) > 0; }

    ui::CellStyle LibraryTreeModel::style(ui::NodeId n) const {
        ui::CellStyle s;
        if (n == 1) { s.bold = true; s.icon = ui::Icon::Library; }
        else if (n <= 1 + families_.size()) { s.icon = ui::Icon::Folder; }
        else {
            s.icon = ui::Icon::FunctionBlock;
            if (project_->libraries[n - 1000].usageCount == 0) {
                s.fg = gfx::Color::rgb(0xDCA032);
                s.iconColor = s.fg;
            }
        }
        return s;
    }

    std::string LibraryTreeModel::text(ui::NodeId n) const {
        static constexpr const char* kNames[] = { "Standard", "Motion", "Process",
                                                 "Communication", "Safety", "User", "Custom" };
        if (n == 1) return "Libraries";
        if (n > 1 && n <= 1 + families_.size()) {
            const auto f = n - 2;
            return std::string(kNames[f]) + " (" + std::to_string(byFamily_[f].size()) + ")";
        }
        const auto& lib = project_->libraries[n - 1000];
        return std::string(project_->strings.text(lib.name)) + "  v" + lib.version
            + "  x" + std::to_string(lib.usageCount);
    }

} // namespace app