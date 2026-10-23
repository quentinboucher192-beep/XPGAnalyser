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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

} // namespace hmi
