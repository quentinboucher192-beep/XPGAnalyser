// =============================================================================
//  hmi/HmiTrash.cpp - 1.26.1 : la corbeille du projet IHM (HmiTrash.hpp)
// =============================================================================
#include "HmiTrash.hpp"

#include "HmiStore.hpp"       // la taille d'un contenu : ce que l'enregistrement en ecrirait
#include "HmiTemplates.hpp"   // le dossier d'une vue (Vues, Popups, Symboles, Modeles)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <map>
#include <unordered_set>
#include <utility>

namespace hmi {

bool TrashItem::operator==(const TrashItem& o) const {
    if (id != o.id || kind != o.kind || name != o.name || origin != o.origin || deletedAt != o.deletedAt || bytes != o.bytes
        || note != o.note)
        return false;
    if (content == o.content) return true;
    return content && o.content && *content == *o.content;
}

namespace bin {
namespace {

constexpr std::string_view kKeys[] = {"vue", "ressource", "externe", "script", "fonction", "variable", "type", "alarme", "recette", "style"};
static_assert(std::size(kKeys) == static_cast<std::size_t>(Kind::Count), "une cle par genre");

constexpr std::string_view kSep = " \xE2\x80\xBA ";   // la fleche des chemins de l'arbre

// "IHM > Vues > Pompes" : la liste d'ou il vient, et son dossier.
std::string originOf(std::string_view list, const std::string& folder) {
    std::string s = "IHM" + std::string(kSep) + std::string(list);
    if (!folder.empty()) s += std::string(kSep) + folder;
    return s;
}

std::string_view viewList(const View& v) {
    switch (viewFolderOf(v.role)) {
        case ViewFolder::Templates: return "Mod\xC3\xA8les";
        case ViewFolder::Popups: return "Popups";
        case ViewFolder::Symbols: return "Symboles";
        case ViewFolder::Views: break;
    }
    return "Vues";
}

// Les elements de `before` absents de `after` (par identifiant), dans l'ordre de `before`.
template <class T>
std::vector<const T*> goneFrom(const std::vector<T>& before, const std::vector<T>& after) {
    std::unordered_set<Id> kept;
    kept.reserve(after.size());
    for (const auto& x : after) kept.insert(x.id);
    std::vector<const T*> out;
    for (const auto& x : before)
        if (!kept.count(x.id)) out.push_back(&x);
    return out;
}

bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[i]);
        if ((ca < 128 ? std::tolower(ca) : ca) != (cb < 128 ? std::tolower(cb) : cb)) return false;
    }
    return true;
}

// Un nom libre dans un espace de noms : `unique` recoit le nom complet (Gaz.Horloge) ; rend le nom court.
template <class Unique>
std::string freeIn(const std::string& ns, const std::string& name, Unique&& unique) {
    const std::string full = qualifiedName(ns, name);
    const std::string got = unique(full);
    if (!ns.empty() && got.size() > ns.size() + 1 && got.compare(0, ns.size() + 1, ns + ".") == 0) return got.substr(ns.size() + 1);
    return got;
}

// Les types des parametres d'une fonction (deux surcharges du meme nom : des parametres differents).
std::vector<std::string> paramTypes(const HmiFunction& f) {
    std::vector<std::string> out;
    for (const auto& d : f.decls)
        if (d.kind == DeclKind::Parameter) out.push_back(d.type);
    return out;
}

} // namespace

std::string_view kindKey(Kind k) noexcept {
    const auto i = static_cast<std::size_t>(k);
    return i < std::size(kKeys) ? kKeys[i] : std::string_view{};
}

std::optional<Kind> kindOf(std::string_view key) noexcept {
    for (std::size_t i = 0; i < std::size(kKeys); ++i)
        if (kKeys[i] == key) return static_cast<Kind>(i);
    return std::nullopt;
}

std::int64_t now() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::uint64_t sizeOf(const Project& content) {
    // L'index d'un projet vide (sa configuration, sa securite par defaut...) ne compte pas : ce qui reste est l'element.
    static const std::uint64_t empty = [] {
        std::uint64_t n = 0;
        for (const auto& f : serializeProject(Project{}))
            if (f.data && f.path == "ihm.txt") n = f.data->size();
        return n;
    }();
    std::uint64_t n = 0;
    for (const auto& f : serializeProject(content)) {
        if (!f.data) continue;
        if (f.path == "ihm.txt") n += f.data->size() > empty ? f.data->size() - empty : 0;
        else n += f.data->size();
    }
    return n;
}

std::size_t capture(const Project& before, Project& after, std::int64_t at) {
    if (at <= 0) at = now();
    after.nextId = std::max(after.nextId, before.nextId);   // un projet remplace par une copie plus ancienne : jamais en arriere
    std::size_t made = 0;
    const auto add = [&](Kind k, std::string name, std::string origin, Project&& content) {
        TrashItem t;
        t.id = after.allocate();
        t.kind = std::string(kindKey(k));
        t.name = std::move(name);
        t.origin = std::move(origin);
        t.deletedAt = at;
        // Son compteur : celui du projet - relu, il garde le projet au-dessus de tout identifiant de la corbeille.
        content.nextId = after.nextId;
        t.bytes = sizeOf(content);
        t.content = std::make_shared<const Project>(std::move(content));
        after.trash.push_back(std::move(t));
        ++made;
    };
    // Les vues : un symbole avec les popups qui partent avec lui (View::ownerSymbol).
    const auto views = goneFrom(before.views, after.views);
    std::unordered_set<Id> goneViews;
    for (const auto* v : views) goneViews.insert(v->id);
    for (const auto* v : views) {
        if (v->ownerSymbol != kNoId && v->ownerSymbol != v->id && goneViews.count(v->ownerSymbol)) continue;   // avec son symbole
        Project c;
        c.views.push_back(*v);
        for (const auto* w : views)
            if (w != v && w->ownerSymbol == v->id) c.views.push_back(*w);
        add(Kind::View, v->name, originOf(viewList(*v), v->folder), std::move(c));
    }
    for (const auto* r : goneFrom(before.assets.resources, after.assets.resources)) {
        Project c;
        c.assets.resources.push_back(*r);
        add(Kind::Resource, r->name, originOf("Ressources", r->folder), std::move(c));
    }
    for (const auto* f : goneFrom(before.assets.files, after.assets.files)) {
        Project c;
        c.assets.files.push_back(*f);
        add(Kind::External, f->name, originOf("Fichiers externes", {}), std::move(c));
    }
    for (const auto* s : goneFrom(before.programs.scripts, after.programs.scripts)) {
        Project c;
        c.programs.scripts.push_back(*s);
        add(Kind::Script, fullName(*s), originOf("Scripts", s->folder), std::move(c));
    }
    for (const auto* f : goneFrom(before.programs.functions, after.programs.functions)) {
        Project c;
        c.programs.functions.push_back(*f);
        add(Kind::Function, fullName(*f), originOf("Fonctions", {}), std::move(c));
    }
    for (const auto* v : goneFrom(before.programs.variables, after.programs.variables)) {
        Project c;
        c.programs.variables.push_back(*v);
        add(Kind::Variable, v->name, originOf("Variables IHM", v->folder), std::move(c));
    }
    for (const auto* t : goneFrom(before.programs.types, after.programs.types)) {
        Project c;
        c.programs.types.push_back(*t);
        add(Kind::Type, fullName(*t), originOf("Types IHM", t->folder), std::move(c));
    }
    for (const auto* a : goneFrom(before.alarms, after.alarms)) {
        Project c;
        c.alarms.push_back(*a);
        add(Kind::Alarm, a->name, originOf("Alarmes", {}), std::move(c));
    }
    for (const auto* r : goneFrom(before.recipes, after.recipes)) {
        Project c;
        c.recipes.push_back(*r);
        add(Kind::Recipe, r->name, originOf("Recettes", {}), std::move(c));
    }
    for (const auto* s : goneFrom(before.styles, after.styles)) {
        if (s->builtin) continue;   // un style fourni n'est pas au projet
        Project c;
        c.styles.push_back(*s);
        add(Kind::Style, s->name, originOf("Styles", s->folder), std::move(c));
    }
    return made;
}

const TrashItem* find(const Project& p, Id item) noexcept {
    for (const auto& t : p.trash)
        if (t.id == item) return &t;
    return nullptr;
}

bool restore(Project& p, Id item, std::string* shown, std::string* why) {
    const auto it = std::find_if(p.trash.begin(), p.trash.end(), [&](const TrashItem& t) { return t.id == item; });
    if (it == p.trash.end()) {
        if (why) *why = "il n'est plus dans la corbeille";
        return false;
    }
    if (!it->content) {
        if (why) *why = it->note.empty() ? std::string("illisible") : it->note;
        return false;
    }
    const std::shared_ptr<const Project> keep = it->content;   // l'element quitte la liste : son contenu reste lisible
    const Project& c = *keep;
    const std::string name = it->name;
    std::string renamed;   // le nouveau nom du premier element renomme
    const auto renameTo = [&](std::string& n, std::string fresh) {
        if (fresh == n) return;
        if (renamed.empty()) renamed = fresh;
        n = std::move(fresh);
    };
    // Les vues (un symbole et ses popups) : un identifiant pris (un projet edite a la main) en recoit un neuf.
    std::map<Id, Id> viewIds;
    for (View v : c.views) {
        const Id old = v.id;
        if (v.id == kNoId || p.view(v.id)) v.id = p.allocate();
        viewIds[old] = v.id;
        if (p.viewByName(v.name)) renameTo(v.name, uniqueViewName(p, v.name));
        p.views.push_back(std::move(v));
    }
    Id firstView = kNoId;
    for (const auto& [old, id] : viewIds) {
        View* v = p.view(id);
        if (!v) continue;
        if (v->role == "vue" && firstView == kNoId) firstView = id;
        if (v->ownerSymbol == kNoId) continue;
        // La popup suit son symbole (rendu avec elle, ou toujours la) ; sans lui, une popup ordinaire.
        if (const auto f = viewIds.find(v->ownerSymbol); f != viewIds.end()) {
            v->ownerSymbol = f->second;
        } else {
            const View* owner = p.view(v->ownerSymbol);
            if (!owner || owner->role != "symbole") v->ownerSymbol = kNoId;
        }
    }
    if (p.config.startView == kNoId && firstView != kNoId) p.config.startView = firstView;
    for (Resource r : c.assets.resources) {
        if (r.id == kNoId || p.resource(r.id)) r.id = p.allocate();
        if (p.resourceByName(r.name)) renameTo(r.name, uniqueResourceName(p, r.name));
        p.assets.resources.push_back(std::move(r));
    }
    for (ExternalFile f : c.assets.files) {
        if (f.id == kNoId || p.externalFile(f.id)) f.id = p.allocate();
        if (p.externalByName(f.name)) renameTo(f.name, uniqueExternalName(p, f.name));
        p.assets.files.push_back(std::move(f));
    }
    for (Script s : c.programs.scripts) {
        if (s.id == kNoId || p.script(s.id)) s.id = p.allocate();
        if (p.generalScript(fullName(s)))
            renameTo(s.name, freeIn(s.ns, s.name, [&](const std::string& full) { return uniqueScriptName(p, full); }));
        p.programs.scripts.push_back(std::move(s));
    }
    for (HmiFunction f : c.programs.functions) {
        if (f.id == kNoId || p.function(f.id)) f.id = p.allocate();
        // Une surcharge (meme nom, autres parametres) reste une surcharge ; la meme signature : un nom libre.
        const auto params = paramTypes(f);
        const bool clash = std::any_of(p.programs.functions.begin(), p.programs.functions.end(), [&](const HmiFunction& o) {
            return sameText(fullName(o), fullName(f)) && paramTypes(o) == params;
        });
        if (clash || p.variable(f.name))
            renameTo(f.name, freeIn(f.ns, f.name, [&](const std::string& full) { return uniqueFunctionName(p, full); }));
        p.programs.functions.push_back(std::move(f));
    }
    for (Variable v : c.programs.variables) {
        if (v.id == kNoId || p.variableById(v.id)) v.id = p.allocate();
        if (p.variable(v.name)) renameTo(v.name, uniqueVariableName(p, v.name));
        p.programs.variables.push_back(std::move(v));
    }
    for (HmiType t : c.programs.types) {
        if (t.id == kNoId || p.hmiType(t.id)) t.id = p.allocate();
        if (p.hmiTypeByName(fullName(t)))
            renameTo(t.name, freeIn(t.ns, t.name, [&](const std::string& full) {
                       for (int i = 2; i < 100000; ++i)
                           if (std::string n = full + "_" + std::to_string(i); !p.hmiTypeByName(n)) return n;
                       return full;
                   }));
        p.programs.types.push_back(std::move(t));
    }
    for (AlarmDef a : c.alarms) {
        if (a.id == kNoId || p.alarm(a.id)) a.id = p.allocate();
        if (p.alarmByName(a.name)) renameTo(a.name, uniqueAlarmName(p, a.name));
        p.alarms.push_back(std::move(a));
    }
    for (Recipe r : c.recipes) {
        if (r.id == kNoId || p.recipe(r.id)) r.id = p.allocate();
        if (p.recipeByName(r.name)) renameTo(r.name, uniqueRecipeName(p, r.name));
        p.recipes.push_back(std::move(r));
    }
    for (Style s : c.styles) {
        if (s.id == kNoId || p.style(s.id)) s.id = p.allocate();
        if (p.styleByName(s.name)) renameTo(s.name, uniqueStyleName(p, s.name));
        p.styles.push_back(std::move(s));
    }
    p.trash.erase(std::find_if(p.trash.begin(), p.trash.end(), [&](const TrashItem& t) { return t.id == item; }));
    if (shown) *shown = renamed.empty() ? name : name + " \xE2\x86\x92 " + renamed;
    return true;
}

bool forget(Project& p, Id item) {
    const auto before = p.trash.size();
    std::erase_if(p.trash, [&](const TrashItem& t) { return t.id == item; });
    return p.trash.size() != before;
}

std::vector<Id> older(const Project& p, int days, std::int64_t at) {
    if (at <= 0) at = now();
    const std::int64_t limit = at - static_cast<std::int64_t>(days) * 86400;
    std::vector<Id> out;
    for (const auto& t : p.trash)
        if (t.deletedAt > 0 && t.deletedAt < limit) out.push_back(t.id);
    return out;
}

} // namespace bin
} // namespace hmi
