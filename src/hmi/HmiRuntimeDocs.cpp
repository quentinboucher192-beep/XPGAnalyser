// =============================================================================
//  hmi/HmiRuntimeDocs.cpp - 1.12.30 : le moteur de l'IHM, la visionneuse de documents.
//  Voir HmiRuntime.hpp (DocViewer) et HmiDocView.hpp (la barre, les gestes).
// -----------------------------------------------------------------------------
//  LA PAGE : elle vient de la Variable de la page (INT) si l'objet en a une, sinon de sa propriete Page (un
//  nombre, une formule). Quand ce qu'on lit change, la visionneuse y va ; quand l'operateur change de page,
//  elle l'ecrit (la variable, sinon la propriete en marche). Son propre echo (la page qu'elle vient d'ecrire,
//  relue au cycle suivant) ne la fait pas sauter en haut de la page.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiMarkers.hpp"
#include "HmiPdf.hpp"
#include "HmiRoles.hpp"     // 1.14.1 (lot N5) : roles::nameOf (la source de l'explorateur)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace hmi {

namespace {

std::string trimmedOf(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// "doc:at:12.5:40" -> (12.5, 40).
bool pointOf(std::string_view part, double& x, double& y) {
    constexpr std::string_view prefix = "doc:at:";
    if (part.substr(0, prefix.size()) != prefix) return false;
    const std::string_view rest = part.substr(prefix.size());
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos) return false;
    return parseNumber(rest.substr(0, colon), x) && parseNumber(rest.substr(colon + 1), y) && std::isfinite(x) && std::isfinite(y);
}

bool isNavigation(std::string_view part) {
    return part == "doc:first" || part == "doc:prev" || part == "doc:next" || part == "doc:last" || part == "doc:page"
        || part == "doc:thumbs" || part.rfind("doc:thumb:", 0) == 0 || part == "doc:key:PageUp" || part == "doc:key:PageDown"
        || part == "doc:key:Home" || part == "doc:key:End";
}

} // namespace

// ================================================================== ce que l'ecran lit ===
const Runtime::DocViewer* Runtime::docViewer(Id object) const {
    const auto it = docs_.find(object);
    return it == docs_.end() ? nullptr : &it->second;
}

std::string Runtime::docPartAt(Id object, double lx, double ly) const {
    const auto* d = docViewer(object);
    if (!d) return {};
    return docview::hitPart(d->layout, d->state, d->sizes, lx, ly);
}

bool Runtime::docFocused() const {
    if (focused_ == kNoId) return false;
    const View* v = shownViewOf(focused_);
    const Object* o = v ? v->object(focused_) : nullptr;
    return o && o->kind == Kind::DocumentViewer;
}

bool Runtime::docTyping() const {
    if (!docFocused()) return false;
    const auto* d = docViewer(focused_);
    return d && (d->state.editingPage || d->state.editingSearch);
}

std::string Runtime::docKeyboardMode() const {
    const auto* d = docViewer(focused_);
    if (!d || settings_.keyboard != "toujours") return {};
    if (d->state.editingPage) return "numerique";
    if (d->state.editingSearch) return "complet";
    return {};
}

bool Runtime::docMemberValue(const Object& o, std::string_view info, sim::Value& out) const {
    if (o.kind != Kind::DocumentViewer) return false;
    const auto* d = docViewer(o.id);
    const bool loaded = d && d->ready && d->document && d->document->state() == doc::Document::State::Ready;
    if (info == "PageCount") {
        out = sim::Value::integer(sim::Type::Int, loaded ? static_cast<std::int64_t>(d->sizes.size()) : 0);
        return true;
    }
    if (info == "CurrentPage") {
        out = sim::Value::integer(sim::Type::Int, loaded ? d->state.page : 0);
        return true;
    }
    if (info == "Zoom") {
        double zoom = 0;
        if (loaded) zoom = docview::contentOf(d->layout, d->state, d->sizes).zoom * 100.0;
        out = sim::Value::real(std::round(zoom * 10.0) / 10.0);
        return true;
    }
    if (info == "Loaded") {
        out = sim::Value::boolean(loaded);
        return true;
    }
    if (info == "DocumentError") {
        std::string why;
        if (d) {
            why = d->why;
            if (why.empty() && d->document && d->document->state() == doc::Document::State::Failed) why = d->document->error();
        }
        out = sim::Value::text(why);
        return true;
    }
    return false;
}

// ================================================================== le cycle ===
void Runtime::docsReset() { docs_.clear(); }

int Runtime::docPageRead(const Object& shown) {
    const std::string var = trimmedOf(markers::strip(shown.text("variable"), markers::Mode::Expression));
    if (!var.empty()) {
        sim::Value cur;
        if (environment().read(var, cur)) {
            double n = 0;
            if (cur.type() == sim::Type::String) {
                if (!parseNumber(trimmedOf(cur.asString()), n)) return 0;
            } else {
                n = cur.asReal();
            }
            return std::isfinite(n) ? static_cast<int>(std::lround(n)) : 0;
        }
        return 0;
    }
    return static_cast<int>(std::lround(shown.number("page", 1)));
}

void Runtime::docsCycle(double now) {
    if (!project_) return;
    std::set<Id> seen;
    const auto visit = [&](Id viewId) {
        const View* v = viewOf(viewId);
        if (!v) return;
        const auto aliases = viewAliases(viewId);
        for (const auto& so : v->objects) {
            if (so.kind != Kind::DocumentViewer) continue;
            seen.insert(so.id);
            Object o = so;
            evaluateProps(viewId, o);      // tel que l'ecran le montre
            docRefresh(*v, so, o, now);
        }
    };
    visit(current_);
    for (const Id p : popups_) visit(p);
    // Les visionneuses qui ne se montrent plus : leur document se ferme (sa page reste dans sa variable).
    for (auto it = docs_.begin(); it != docs_.end();) {
        if (seen.count(it->first)) {
            ++it;
            continue;
        }
        if (focused_ == it->first) focused_ = kNoId;
        it = docs_.erase(it);
    }
}

void Runtime::docRefresh(const View& v, const Object& raw, const Object& o, double now) {
    auto& d = docs_[raw.id];
    d.settings = docview::settingsOf(o);
    d.navigation = d.settings.navigation && objectAllowed(o);
    // ---- le document : un autre nom, ou (un fichier, ou ce qui ne s'est pas ouvert) toutes les deux secondes
    const std::string spec = trimmedOf(roles::nameOf(o.text("document")));
    const bool file = d.document && d.document->key().rfind("fichier:", 0) == 0;
    if (spec != d.spec || ((file || !d.document) && now - d.checkedAt >= 2.0)) {
        d.checkedAt = now;
        std::string why;
        const std::string folder = hooks_.projectFolder ? hooks_.projectFolder() : std::string{};
        std::shared_ptr<doc::Document> next = spec.empty() ? nullptr : doc::open(project_->assets, spec, folder, &why);
        if (spec.empty()) why = "aucun document";
        if (next != d.document || spec != d.spec) {
            if (spec != d.spec && !why.empty() && !spec.empty()) logAt(LogLevel::Warning, "Visionneuse", where(v, &raw), why);
            const bool keepThumbs = d.state.thumbs;
            d.document = std::move(next);
            d.why = why;
            d.sizes.clear();
            d.ready = false;
            d.state = docview::initialState(d.settings);
            if (!d.spec.empty()) d.state.thumbs = keepThumbs && d.navigation;   // un autre document : la colonne reste
            d.lastRead = 0;
            d.written = 0;
        } else {
            d.why = why;
        }
        d.spec = spec;
    }
    // ---- Vignettes affichees change (sa formule) : la colonne suit ; l'operateur la change ensuite a son gre
    if (d.settings.thumbnails != d.thumbsSetting) {
        d.thumbsSetting = d.settings.thumbnails;
        d.state.thumbs = d.settings.thumbnails && d.navigation;
    }
    if (!d.navigation) d.state.thumbs = false;
    if (!d.settings.toolbar) {
        d.state.searchOpen = false;
        d.state.zoomMenu = false;
        d.state.editingPage = false;
        d.state.editingSearch = false;
    }
    const docview::Layout previous = d.layout;
    d.layout = docview::layoutOf(o.number("w"), o.number("h"), d.settings, d.state);
    if (!d.document || d.document->state() != doc::Document::State::Ready) return;
    // L'objet a change de taille (une formule), la barre est apparue : ce qu'on lisait reste en haut.
    if (d.ready && (previous.area.w != d.layout.area.w || previous.area.h != d.layout.area.h || previous.area.y != d.layout.area.y)
        && previous.area.w > 0)
        docview::relayout(d.state, previous, d.layout, d.sizes);
    if (!d.ready) {
        d.sizes = d.document->pageSizes();
        d.ready = true;
        // Un texte ne se tourne pas (ses lignes s'ecrivent droites).
        if (d.document->type() == doc::Type::Text) d.state.quarter = 0;
    }
    // ---- la page lue : la seule montree (navigation interdite), ou celle ou aller quand elle change
    const int count = static_cast<int>(d.sizes.size());
    const int read = docPageRead(o);
    if (!d.navigation) {
        d.state.single = std::clamp(read >= 1 ? read : 1, 1, std::max(1, count));
        d.state.editingPage = false;
        d.lastRead = read;
        d.written = 0;
    } else {
        if (d.state.single > 0) {
            // La navigation revient (un niveau d'acces suffisant) : toutes les pages, a celle qu'on montrait.
            const int was = d.state.single;
            d.state.single = 0;
            (void)docview::goToPage(d.state, d.layout, d.sizes, was);
        }
        if (read != d.lastRead) {
            if (read != d.written && read >= 1) (void)docview::goToPage(d.state, d.layout, d.sizes, std::min(read, count));
            d.lastRead = read;
            d.written = 0;
        }
    }
    // ---- la recherche finie : sa premiere occurrence se montre (sa page s'ecrit comme un geste)
    if (d.state.searchOpen && d.state.hit < 0 && !trimmedOf(d.state.query).empty()) {
        const auto s = d.document->searchState();
        if (!s.running && !s.hits.empty() && s.query == trimmedOf(d.state.query)) {
            std::vector<doc::Hit> hits = s.hits;
            if (d.state.single > 0) std::erase_if(hits, [&](const doc::Hit& h) { return h.page != d.state.single - 1; });
            const int before = d.state.page;
            if (!hits.empty() && docview::showHit(d.state, d.layout, d.sizes, hits, 0) && d.state.page != before) {
                docview::Effect fx;
                fx.pageChanged = true;
                docApply(v, raw, d, fx, now);
            }
        }
    }
    docview::settle(d.state, d.layout, d.sizes);
}

// ================================================================== les gestes ===
void Runtime::docApply(const View& v, const Object& o, DocViewer& d, const docview::Effect& fx, double now) {
    (void)now;
    const std::string source = where(v, &o);
    if (fx.focus) {
        if (focused_ != kNoId && focused_ != o.id) unfocus(now);
        focused_ = o.id;
    }
    if (fx.startSearch && d.document) {
        // Une autre question : ses occurrences repartent de la premiere (montree quand la recherche finit).
        if (d.document->searchState().query != trimmedOf(d.state.query)) d.state.hit = -1;
        d.document->search(d.state.query);
    }
    if (!fx.pageChanged) return;
    const int page = d.state.page;
    d.written = page;
    const std::string var = trimmedOf(markers::strip(o.text("variable"), markers::Mode::Expression));
    if (!var.empty()) {
        if (!writeConverted(var, sim::Value::integer(sim::Type::Int, page), source, &o))
            logAt(LogLevel::Warning, "Visionneuse", source, "page " + std::to_string(page) + " : la variable " + var + " ne s'\xC3\xA9" "crit pas");
    } else {
        setOverride(v, o, "page", std::to_string(page));
    }
}

void Runtime::docObjectPart(const View& v, const Object& raw, std::string_view partIn, double now) {
    if (!running_) return;
    Object o = raw;
    evaluateProps(v.id, o);
    const auto aliases = viewAliases(v.id);
    docRefresh(v, raw, o, now);
    auto& d = docs_[raw.id];
    std::string part(partIn);
    double x = 0, y = 0;
    if (pointOf(part, x, y)) part = docview::hitPart(d.layout, d.state, d.sizes, x, y);
    if (part.empty()) return;
    const std::string source = where(v, &raw);
    GestureGuard gesture(*this, source, "Clic sur");
    // Un texte ne se tourne pas.
    if (part == "doc:rotate" && d.document && d.document->type() == doc::Type::Text) return;
    // Navigation interdite : le dire (le niveau d'acces, ou la case decochee).
    if (!d.navigation && isNavigation(part)) {
        std::string why;
        if (!objectAllowed(o, &why)) event("Acc\xC3\xA8s refus\xC3\xA9", source, why);
        else log("Visionneuse", source, "navigation interdite (Navigation autoris\xC3\xA9" "e d\xC3\xA9" "coch\xC3\xA9" "e) : la page suit sa variable");
        return;
    }
    std::vector<doc::Hit> hits;
    if (d.document) hits = d.document->searchState().hits;
    if (d.state.single > 0)
        std::erase_if(hits, [&](const doc::Hit& h) { return h.page != d.state.single - 1; });
    const auto fx = docview::gesture(d.state, d.layout, d.sizes, part, d.navigation, hits);
    // La disposition suit l'etat (la barre de recherche, la colonne des vignettes) ; ce qu'on lisait reste en haut.
    const docview::Layout previous = d.layout;
    d.layout = docview::layoutOf(o.number("w"), o.number("h"), d.settings, d.state);
    if (d.ready && (previous.area.w != d.layout.area.w || previous.area.h != d.layout.area.h || previous.area.y != d.layout.area.y))
        docview::relayout(d.state, previous, d.layout, d.sizes);
    docview::settle(d.state, d.layout, d.sizes);
    docApply(v, raw, d, fx, now);
}

void Runtime::docTypeText(const View& v, const Object& raw, std::string_view text, double now) {
    auto it = docs_.find(raw.id);
    if (it == docs_.end()) return;
    auto& d = it->second;
    if (!d.state.editingPage && !d.state.editingSearch) {
        // Sans champ en saisie : + et - zooment.
        if (text == "+" || text == "=") docObjectPart(v, raw, "doc:key:Plus", now);
        else if (text == "-") docObjectPart(v, raw, "doc:key:Minus", now);
        return;
    }
    (void)docview::typeText(d.state, text);
}

void Runtime::docTypeKey(const View& v, const Object& raw, EditKey k, double now) {
    auto it = docs_.find(raw.id);
    if (it == docs_.end()) return;
    auto& d = it->second;
    if (!d.state.editingPage && !d.state.editingSearch) {
        switch (k) {
            case EditKey::Home:  docObjectPart(v, raw, "doc:key:Home", now); break;
            case EditKey::End:   docObjectPart(v, raw, "doc:key:End", now); break;
            case EditKey::Left:  docObjectPart(v, raw, "doc:key:Left", now); break;
            case EditKey::Right: docObjectPart(v, raw, "doc:key:Right", now); break;
            case EditKey::Enter:
                if (d.state.searchOpen) docObjectPart(v, raw, "doc:hitnext", now);
                break;
            case EditKey::Escape:
                if (d.state.searchOpen) docObjectPart(v, raw, "doc:searchclose", now);
                else if (focused_ == raw.id) focused_ = kNoId;
                break;
            default: break;
        }
        return;
    }
    const char* key = k == EditKey::Backspace ? "Backspace" : k == EditKey::Escape ? "Escape" : k == EditKey::Enter ? "Enter" : nullptr;
    if (!key) return;
    const auto aliases = viewAliases(v.id);   // la variable de la page peut passer par un parametre de la vue
    std::vector<doc::Hit> hits;
    if (d.document) hits = d.document->searchState().hits;
    if (d.state.single > 0) std::erase_if(hits, [&](const doc::Hit& h) { return h.page != d.state.single - 1; });
    const bool searching = d.state.editingSearch;
    const std::string query = trimmedOf(d.state.query);
    auto fx = docview::typeKey(d.state, d.layout, d.sizes, key);
    // Entree dans la recherche : la meme question une seconde fois, l'occurrence suivante.
    if (searching && k == EditKey::Enter && d.document) {
        const auto s = d.document->searchState();
        if (s.query == query && !s.running && !hits.empty()) {
            fx.startSearch = false;
            const auto more = docview::gesture(d.state, d.layout, d.sizes, "doc:hitnext", d.navigation, hits);
            fx.pageChanged = fx.pageChanged || more.pageChanged;
            d.state.editingSearch = true;   // la saisie continue (Entree, Entree...)
        }
    }
    docApply(v, raw, d, fx, now);
}

// ================================================ 1.14.1 (lot N5) : l'explorateur de fichiers ===
namespace {

std::filesystem::path explorerPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string explorerText(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

} // namespace

const Runtime::FileExplorer* Runtime::fileExplorer(Id object) const {
    const auto it = explorers_.find(object);
    return it == explorers_.end() ? nullptr : &it->second;
}

std::vector<std::string> Runtime::explorerCrumbs(Id object) const {
    const auto* e = fileExplorer(object);
    if (!e) return {};
    std::vector<std::string> out{e->label};
    out.insert(out.end(), e->loc.at.begin(), e->loc.at.end());
    return out;
}

explorer::Tip Runtime::explorerTip(Id object, std::size_t row) const {
    const auto* e = fileExplorer(object);
    if (!e || row >= e->shown.size()) return {};
    const auto& entry = e->shown[row];
    if (const auto it = e->tips.find(entry.path); it != e->tips.end()) return it->second;
    explorer::Tip t;
    if (e->resources) {
        // Les ressources du projet, en memoire (le projet n'est pas encore enregistre) : ce qu'on en sait.
        t.title = entry.name;
        t.body = "Ressource du projet (le projet n'est pas encore enregistr\xC3\xA9 sur le disque)\n" + explorer::genreLabel(entry.genre, entry.name)
               + "\nTaille : " + explorer::sizeText(entry.size);
        if (project_)
            for (const auto& r : project_->assets.resources)
                if (r.name == entry.name) {
                    if (r.width > 0 && r.height > 0) t.body += "\nImage : " + std::to_string(r.width) + " \xC3\x97 " + std::to_string(r.height) + " pixels";
                    if (!r.added.empty()) t.body += "\nImport\xC3\xA9" "e le " + r.added;
                }
    } else {
        t = explorer::tipOf(e->loc, entry);
    }
    e->tips.emplace(entry.path, t);
    return t;
}

void Runtime::explorersCycle(double now) {
    if (!project_) return;
    std::set<Id> seen;
    const auto visit = [&](Id viewId) {
        const View* v = viewOf(viewId);
        if (!v) return;
        const auto aliases = viewAliases(viewId);
        for (const auto& so : v->objects) {
            if (so.kind != Kind::FileExplorer) continue;
            seen.insert(so.id);
            Object o = so;
            evaluateProps(viewId, o);      // tel que l'ecran le montre (sa source peut etre une formule)
            explorerRefresh(*v, so, o, now);
        }
    };
    visit(current_);
    for (const Id p : popups_) visit(p);
    // Ceux qui ne se montrent plus : oublies ; rouverts, ils relisent leur source (l'ouverture de la vue).
    for (auto it = explorers_.begin(); it != explorers_.end();) {
        if (seen.count(it->first)) {
            ++it;
            continue;
        }
        if (focused_ == it->first) focused_ = kNoId;
        forms_.erase(it->first);
        it = explorers_.erase(it);
    }
}

void Runtime::explorerRecompute(Id object) {
    const View* v = shownViewOf(object);
    const Object* raw = v ? v->object(object) : nullptr;
    if (!raw || raw->kind != Kind::FileExplorer || !project_) return;
    const auto aliases = viewAliases(v->id);
    Object o = *raw;
    evaluateProps(v->id, o);
    explorerRefresh(*v, *raw, o, now_);
}

// A chaque cycle : les reglages relus ; la source n'est relue que si elle change (ou son filtre) - jamais a chaque cycle.
void Runtime::explorerRefresh(const View& v, const Object& raw, const Object& o, double) {
    const bool fresh = !explorers_.count(raw.id);
    auto& e = explorers_[raw.id];
    e.allowRefresh = o.flag("allowRefresh", true);
    e.allowExtract = o.flag("allowExtract", false);
    e.showSearch = o.flag("showSearch", true);
    e.showSize = o.flag("showSize", true);
    e.showDate = o.flag("showDate", true);
    e.pilotable = o.flag("pilotable", true);
    e.maxItems = static_cast<std::size_t>(std::clamp(o.number("maxItems", 2000), 1.0, 100000.0));
    const std::string sortProp = trimmedOf(o.text("sortBy", "nom")) + "|" + trimmedOf(o.text("sortOrder", "croissant"));
    bool resort = false;
    if (fresh || sortProp != e.sortProp) {
        e.sortProp = sortProp;
        e.sortBy = explorer::sortKeyFrom(o.text("sortBy", "nom"));
        e.descending = trimmedOf(o.text("sortOrder", "croissant")).rfind("d", 0) == 0;
        resort = true;
    }
    const std::string spec = trimmedOf(roles::nameOf(o.text("folder")));
    const std::string filter = trimmedOf(o.text("filter"));
    if (fresh || spec != e.spec || filter != e.filter) {
        if (fresh || spec != e.spec) e.loc.at.clear();   // une autre source : on repart de sa racine
        e.spec = spec;
        e.filter = filter;
        explorerLoad(e, v, raw);
        return;
    }
    if (resort) explorerShow(e);
}

void Runtime::explorerLoad(FileExplorer& e, const View& v, const Object& o) {
    const std::string folder = hooks_.projectFolder ? hooks_.projectFolder() : std::string{};
    e.resources = false;
    e.listing = explorer::Listing{};
    e.tips.clear();
    e.first = 0;
    ++e.loads;
    std::error_code ec;
    if (e.spec.empty()) {
        // Vide : le dossier ressources du projet ; pas encore enregistre, ses ressources en memoire.
        const auto res = explorerPath(folder) / "ihm" / "ressources";
        if (!folder.empty() && std::filesystem::is_directory(res, ec)) {
            e.loc.root = explorerText(res);
            e.label = "Ressources";
        } else {
            e.resources = true;
            e.loc = explorer::Location{};
            e.label = "Ressources du projet";
        }
    } else {
        auto p = explorerPath(e.spec);
        e.label = explorerText(p.filename());
        if (e.label.empty()) e.label = e.spec;
        if (p.is_relative()) {
            if (folder.empty()) {
                e.loc.root.clear();
                e.listing.error = "le projet n'est pas encore enregistr\xC3\xA9 : un chemin relatif part du dossier du projet";
                explorerShow(e);
                return;
            }
            p = explorerPath(folder) / p;
        }
        e.loc.root = explorerText(p.lexically_normal());
    }
    explorer::Options opt;
    opt.extensions = parseExtensionFilter(e.filter);
    opt.maxItems = e.maxItems;
    if (e.resources) {
        if (project_)
            for (const auto& r : project_->assets.resources) {
                explorer::Entry en;
                en.name = r.name;
                en.path = r.name;
                en.genre = explorer::genreOf(r.name, false);
                en.size = r.data ? r.data->size() : r.bytes;
                ++e.listing.total;
                if (!opt.extensions.empty() && en.genre != explorer::Genre::Zip && !extensionAccepted(opt.extensions, r.name)) continue;
                if (e.listing.entries.size() >= opt.maxItems) {
                    e.listing.truncated = true;
                    continue;
                }
                e.listing.entries.push_back(std::move(en));
            }
    } else {
        e.listing = explorer::list(e.loc, opt);
        if (!e.listing.error.empty()) logAt(LogLevel::Warning, "Explorateur", where(v, &o), e.listing.error);
        if (e.listing.suspicious > 0)
            logAt(LogLevel::Warning, "Explorateur", where(v, &o),
                  plural(e.listing.suspicious, "nom ramen\xC3\xA9", "noms ramen\xC3\xA9s") + " dans le zip (il disait ../ ou un chemin absolu)");
    }
    explorerShow(e);
}

void Runtime::explorerShow(FileExplorer& e) {
    e.shown.clear();
    for (const auto& en : e.listing.entries)
        if (e.search.empty() || explorer::nameContains(en.name, e.search)) e.shown.push_back(en);
    explorer::sort(e.shown, e.sortBy, e.descending);
    e.selected = -1;
    for (std::size_t i = 0; i < e.shown.size(); ++i)
        if (!e.selectedPath.empty() && e.shown[i].path == e.selectedPath) e.selected = static_cast<int>(i);
    if (e.first >= e.shown.size()) e.first = 0;
}

void Runtime::explorerPick(FileExplorer& e, const View& v, const Object& o, double now) {
    const std::string var = trimmedOf(markers::strip(o.text("pickTo"), markers::Mode::Expression));
    if (var.empty() || e.selected < 0) return;
    const std::string source = where(v, &o);
    if (project_ && project_->security.enabled && !permitted("Piloter")) {
        event("Acc\xC3\xA8s refus\xC3\xA9", source, std::string(kindLabel(o.kind)) + " : permission \xC2\xAB Piloter \xC2\xBB requise pour Choix vers");
        e.message = "permission Piloter requise (Choix vers)";
        e.error = true;
        return;
    }
    const auto aliases = viewAliases(v.id);
    if (!writeConverted(var, sim::Value::text(e.shown[static_cast<std::size_t>(e.selected)].path), source, &o)) {
        e.message = "Choix vers : \xC3\xA9" "criture refus\xC3\xA9" "e (" + var + ", voir la Console)";
        e.error = true;
    }
    (void)now;
}

void Runtime::explorerTyped(Id object) {
    auto it = explorers_.find(object);
    const auto f = forms_.find(object);
    if (it == explorers_.end() || f == forms_.end()) return;
    const auto t = f->second.text.find("recherche");
    it->second.search = t == f->second.text.end() ? std::string{} : t->second;
    it->second.first = 0;
    explorerShow(it->second);
}

void Runtime::explorerPart(const View& v, const Object& o, std::string_view part, double now) {
    if (!explorers_.count(o.id)) explorerRecompute(o.id);   // avant le premier cycle
    auto& e = explorers_[o.id];
    if (!e.pilotable) return;                                 // il montre, il ne reagit pas
    const std::string source = where(v, &o);
    e.message.clear();
    e.error = false;
    const auto say = [&](std::string m, bool err) {
        e.message = std::move(m);
        e.error = err;
    };
    const auto index = [&](std::size_t prefix) -> long {
        const std::string n(part.substr(prefix));
        char* end = nullptr;
        const unsigned long i = std::strtoul(n.c_str(), &end, 10);
        return end && *end == '\0' && !n.empty() && i < e.shown.size() ? static_cast<long>(i) : -1;
    };
    const auto reload = [&] {
        explorerLoad(e, v, o);
        if (!e.listing.error.empty()) say(e.listing.error, true);
    };
    // Aller ailleurs (un dossier, un zip, plus haut) : refuse, on reste ou l'on etait.
    const auto goTo = [&](std::vector<std::string> at) {
        const auto keep = e.loc.at;
        e.loc.at = std::move(at);
        e.selectedPath.clear();
        explorerLoad(e, v, o);
        if (!e.listing.error.empty()) {
            const std::string why = e.listing.error;
            e.loc.at = keep;
            explorerLoad(e, v, o);
            say(why, true);
            log("Erreur", source, "explorateur : " + why);
        }
    };
    if (focused_ == o.id && part != "recherche") {      // un autre geste : la case de recherche rend le clavier
        auto& f = forms_[o.id];
        f.focus.clear();
        focused_ = kNoId;
    }
    if (part == "rafraichir") {
        if (!e.allowRefresh) {
            say("Rafra\xC3\xAE" "chir n'est pas permis ici", true);
            return;
        }
        reload();
        if (!e.error) say("Rafra\xC3\xAE" "chi : " + plural(e.listing.total, "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments"), false);
    } else if (part == "monter" || part == "parent") {
        if (e.loc.at.empty()) {
            say("D\xC3\xA9j\xC3\xA0 \xC3\xA0 la source : on ne remonte pas au-dessus", true);
            return;
        }
        auto at = e.loc.at;
        at.pop_back();
        goTo(std::move(at));
    } else if (part.rfind("ariane:", 0) == 0) {
        const std::string n(part.substr(7));
        char* end = nullptr;
        const unsigned long i = std::strtoul(n.c_str(), &end, 10);
        if (end && *end == '\0' && i <= e.loc.at.size()) goTo(std::vector<std::string>(e.loc.at.begin(), e.loc.at.begin() + static_cast<long>(i)));
    } else if (part.rfind("aller:", 0) == 0) {
        std::vector<std::string> steps;
        if (!explorer::splitSteps(part.substr(6), steps)) {
            say("chemin refus\xC3\xA9 : on ne remonte pas au-dessus de la source", true);
            log("Erreur", source, "explorateur : chemin refus\xC3\xA9 \xC2\xAB " + std::string(part.substr(6)) + " \xC2\xBB");
            return;
        }
        goTo(std::move(steps));
    } else if (part.rfind("colonne:", 0) == 0) {
        const auto key = explorer::sortKeyFrom(part.substr(8));
        if (key == e.sortBy) e.descending = !e.descending;
        else {
            e.sortBy = key;
            e.descending = false;
        }
        explorerShow(e);
    } else if (part.rfind("ligne:", 0) == 0) {
        const long i = index(6);
        if (i < 0) return;
        e.selected = static_cast<int>(i);
        e.selectedPath = e.shown[static_cast<std::size_t>(i)].path;
        explorerPick(e, v, o, now);
    } else if (part.rfind("ouvrir:", 0) == 0) {
        const long i = index(7);
        if (i < 0) return;
        const auto entry = e.shown[static_cast<std::size_t>(i)];
        if (entry.folder || entry.genre == explorer::Genre::Zip) {
            if (e.resources) {
                say("une ressource zip s'ouvre une fois le projet enregistr\xC3\xA9", true);
                return;
            }
            auto at = e.loc.at;
            at.push_back(entry.name);
            goTo(std::move(at));
            return;
        }
        // Un fichier : choisi (Choix vers), puis les actions Double clic de l'objet (ouvrir une popup, la visionneuse...).
        e.selected = static_cast<int>(i);
        e.selectedPath = entry.path;
        explorerPick(e, v, o, now);
        if (recordOn_) recordClick(rec::EventKind::DoubleClick, o.id);
        runActions(v, &o, Trigger::DoubleClick, now);
    } else if (part == "recherche") {
        if (!e.showSearch) return;
        auto& f = forms_[o.id];
        f.text["recherche"] = e.search;
        f.focus = "recherche";
        f.caret["recherche"] = e.search.size();
        f.fresh = false;
        focused_ = o.id;
    } else if (part.rfind("recherche:", 0) == 0) {
        e.search = std::string(part.substr(10));
        e.first = 0;
        explorerShow(e);
    } else if (part == "extraire") {
        if (!e.allowExtract) {
            say("Extraire n'est pas permis (la propri\xC3\xA9t\xC3\xA9 Extraire est d\xC3\xA9" "coch\xC3\xA9" "e)", true);
            log("Erreur", source, "explorateur : extraire n'est pas permis");
            return;
        }
        if (e.selected < 0 || !e.shown[static_cast<std::size_t>(e.selected)].inZip || e.shown[static_cast<std::size_t>(e.selected)].folder) {
            say("choisis d'abord un fichier d'un zip", true);
            return;
        }
        if (project_ && project_->security.enabled && !permitted("Piloter")) {
            event("Acc\xC3\xA8s refus\xC3\xA9", source, std::string(kindLabel(o.kind)) + " : permission \xC2\xAB Piloter \xC2\xBB requise pour extraire");
            say("permission Piloter requise", true);
            return;
        }
        const std::string folder = hooks_.projectFolder ? hooks_.projectFolder() : std::string{};
        std::string dest = trimmedOf(roles::nameOf(o.text("extractTo", "donnees/extraits")));
        if (dest.empty()) dest = "donnees/extraits";
        auto d = explorerPath(dest);
        if (d.is_relative()) {
            if (folder.empty()) {
                say("le projet n'est pas encore enregistr\xC3\xA9 : le dossier d'extraction part du dossier du projet", true);
                return;
            }
            d = explorerPath(folder) / d;
        }
        // Jamais dans le projet lui-meme (son dossier ihm/) : un fichier n'y entre que par l'import des ressources.
        if (!folder.empty()) {
            const auto ihm = (explorerPath(folder) / "ihm").lexically_normal();
            const auto dn = d.lexically_normal();
            auto a = ihm.begin();
            auto b = dn.begin();
            for (; a != ihm.end() && b != dn.end() && *a == *b; ++a, ++b) {}
            if (a == ihm.end()) {
                say("le dossier d'extraction est dans le projet (ihm/) : refus\xC3\xA9", true);
                return;
            }
        }
        std::string written, why;
        const auto entry = e.shown[static_cast<std::size_t>(e.selected)];
        if (explorer::extract(e.loc, entry, explorerText(d.lexically_normal()), written, why)) {
            say("Extrait : " + written, false);
            log("Action", source, "extrait " + entry.path + " vers " + written);
        } else {
            say(why, true);
            log("Erreur", source, "extraire : " + why);
        }
    } else if (part == "liste:haut" || part == "liste:bas") {
        const auto l = explorer::layoutOf(o.number("w", 520), o.number("h", 320), o.number("fontSize", 13), explorerCrumbs(o.id), e.showSearch,
                                          e.allowRefresh, e.allowExtract, !e.loc.at.empty(), e.showSize, e.showDate, e.shown.size(), e.first);
        const std::size_t step = std::max<std::size_t>(1, l.rows.size());
        e.first = part == "liste:haut" ? (l.first > step ? l.first - step : 0) : std::min(l.first + step, e.shown.empty() ? 0 : e.shown.size() - 1);
    }
}

} // namespace hmi
