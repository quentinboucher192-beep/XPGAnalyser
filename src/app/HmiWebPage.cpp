// =============================================================================
//  app/HmiWebPage.cpp - 1.12.30 : voir HmiWebPage.hpp
// =============================================================================
#include "HmiWebPage.hpp"

#include "SvgRenderer.hpp"
#include "hmi/HmiImages.hpp"
#include "hmi/HmiPainter.hpp"
#include "../export/HtmlPage.hpp"
#include "../hmi/HmiExpr.hpp"
#include "../hmi/HmiMarkers.hpp"
#include "../hmi/HmiNavigation.hpp"
#include "../hmi/HmiSymbols.hpp"
#include "../hmi/HmiTemplates.hpp"
#include "../hmi/HmiWidgets.hpp"
#include "../project/ProjectArchive.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace app {

namespace {

namespace h = exporter::html;

std::string viewId(hmi::Id id) { return "v" + std::to_string(id); }

std::string thousands(std::size_t n) {
    std::string s = std::to_string(n), out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) out += ' ';
        out += s[i];
    }
    return out;
}

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string s = b;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
    return s;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        std::string l = text.substr(from, (nl == std::string::npos ? text.size() : nl) - from);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        for (std::size_t t; (t = l.find('\t')) != std::string::npos;) l.replace(t, 1, "    ");
        out.push_back(std::move(l));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

bool shownView(const hmi::View& v) { return (v.role == "vue" || v.role == "popup") && v.width > 0 && v.height > 0; }

// LA VUE AU REPOS, COMME EN MARCHE SANS VALEUR : un texte a trous montre un tiret a la
// place de chaque valeur ("Pression : - bar" ; l'editeur montrerait {PT1:0.0}), ses
// reperes retires ($Pompe$ : Pompe), {{ et }} rendus en accolades. Le reste : la valeur
// statique, comme l'editeur.
std::string atRest(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '{') {
            if (i + 1 < s.size() && s[i + 1] == '{') { out += '{'; i += 2; continue; }
            const std::size_t end = hmi::templateHoleEnd(s, i + 1);
            if (end == std::string_view::npos) { out += s.substr(i); break; }
            out += "\xE2\x80\x94";
            i = end + 1;
            continue;
        }
        if (s[i] == '}' && i + 1 < s.size() && s[i + 1] == '}') { out += '}'; i += 2; continue; }
        out += s[i++];
    }
    return hmi::markers::strip(out, hmi::markers::Mode::Text);
}

class RestingSource final : public HmiPropertySource {
public:
    [[nodiscard]] std::string text(const hmi::Object& o, std::string_view key, std::string_view fallback = {}) const override {
        std::string t = o.text(key, fallback);
        if (!hmi::displayedTextKey(key) || (t.find('{') == std::string::npos && t.find('$') == std::string::npos)) return t;
        return atRest(t);
    }
};

// La vue telle qu'elle tourne : son modele, son en-tete et son pied, ses instances de
// symboles developpees (leurs objets et leurs actions), ses structures rangees (les
// autres pages des onglets, le contenu d'un panneau replie : caches).
hmi::View prepared(const hmi::Project& p, const hmi::View& v) {
    hmi::View shown = hmi::inherits(p, v) ? hmi::compose(p, v) : v;
    shown = hmi::expandInstances(p, shown);
    hmi::layoutStructures(shown);
    // Les cases pilotees d'un tableau ("=expression", un texte a trous) : au repos aussi.
    for (auto& o : shown.objects) {
        if (o.kind != hmi::Kind::Table) continue;
        auto* cells = o.find("cells");
        if (!cells || cells->value.empty()) continue;
        auto rows = hmi::parseCells(cells->value);
        for (auto& row : rows)
            for (auto& cell : row) {
                if (hmi::cellIsExpression(cell)) cell = "\xE2\x80\x94";
                else if (hmi::cellIsTemplate(cell)) cell = atRest(cell);
            }
        cells->value = hmi::formatCells(rows);
    }
    return shown;
}

// Une zone cliquable : son cadre (dans la vue), sa rotation, ce qu'elle fait.
struct Hot {
    hmi::Box    box;
    double      rotation{0};
    std::string attribute;     // data-go="v12", data-popup="v40", data-back, data-close
    std::string title;         // l'infobulle
    hmi::Id     target{hmi::kNoId};
};

bool clickTrigger(hmi::Trigger t) {
    return t == hmi::Trigger::Click || t == hmi::Trigger::DoubleClick || t == hmi::Trigger::LongPress;
}

// Ce que fait un objet, quand il ouvre une vue ou une popup (la premiere action qui le fait).
bool hotOf(const hmi::Project& p, const hmi::Object& o, Hot& hot) {
    for (const auto& a : o.actions) {
        if (!clickTrigger(a.trigger)) continue;
        using O = hmi::Operation;
        if (a.operation == O::Navigate || a.operation == O::Popup || a.operation == O::ChangePopup) {
            const auto* v = p.viewByName(a.target);
            if (!v || !shownView(*v)) continue;
            const bool popup = a.operation != O::Navigate;
            hot.attribute = std::string(popup ? "data-popup" : "data-go") + "=\"" + viewId(v->id) + "\"";
            hot.title = (popup ? "Ouvrir la popup " : "Aller \xC3\xA0 ") + v->name;
            hot.target = v->id;
            return true;
        }
        if (a.operation == O::NavigateHome) {
            const auto* v = p.view(p.config.startView);
            if (!v || !shownView(*v)) continue;
            hot.attribute = "data-go=\"" + viewId(v->id) + "\"";
            hot.title = "Accueil : " + v->name;
            hot.target = v->id;
            return true;
        }
        if (a.operation == O::NavigateBack) {
            hot.attribute = "data-back";
            hot.title = "Vue pr\xC3\xA9" "c\xC3\xA9" "dente";
            return true;
        }
        if (a.operation == O::ClosePopup || a.operation == O::CloseAllPopups || a.operation == O::PreviousPopup) {
            hot.attribute = "data-close";
            hot.title = "Fermer la popup";
            return true;
        }
    }
    return false;
}

std::vector<Hot> hotsOf(const hmi::Project& p, const hmi::View& shown) {
    std::vector<Hot> out;
    for (const auto* o : shown.paintOrder()) {
        if (!o || !o->flag("visible", true)) continue;      // comme le dessin en marche (un calque cache ne compte que dans l'editeur)
        const hmi::Box b = o->box();
        if (b.w <= 0 || b.h <= 0) continue;
        // Une barre de navigation : un bouton par vue.
        if (o->kind == hmi::Kind::NavBar) {
            const auto items = hmi::navItems(&p, *o);
            const auto layout = hmi::navBarLayout(*o, b.w, b.h, items.size());
            for (std::size_t i = 0; i < items.size() && i < layout.items.size(); ++i) {
                const auto* v = p.viewByName(items[i].view);
                if (!v || !shownView(*v)) continue;
                Hot hot;
                hot.box = {b.x + layout.items[i].x, b.y + layout.items[i].y, layout.items[i].w, layout.items[i].h};
                hot.rotation = o->rotation();
                hot.attribute = "data-go=\"" + viewId(v->id) + "\"";
                hot.title = "Aller \xC3\xA0 " + v->name;
                hot.target = v->id;
                out.push_back(std::move(hot));
            }
            continue;
        }
        Hot hot;
        hot.box = b;
        hot.rotation = o->rotation();
        if (hotOf(p, *o, hot)) out.push_back(std::move(hot));
    }
    return out;
}

std::string hotSvg(const Hot& hot) {
    std::string out = "<g class=\"hot\" " + hot.attribute + " tabindex=\"0\" role=\"link\" aria-label=\"" + h::escape(hot.title)
                    + "\"><title>" + h::escape(hot.title) + "</title><rect x=\"" + num(hot.box.x) + "\" y=\"" + num(hot.box.y)
                    + "\" width=\"" + num(hot.box.w) + "\" height=\"" + num(hot.box.h) + "\" rx=\"3\"";
    if (std::fabs(hot.rotation) > 0.01)
        out += " transform=\"rotate(" + num(hot.rotation) + " " + num(hot.box.cx()) + " " + num(hot.box.cy()) + ")\"";
    out += "/></g>";
    return out;
}

std::string scriptBlock(const hmi::Script& s) {
    std::string meta = std::string(hmi::eventLabel(s.event));
    if (s.event == "Cyclique") meta += ", toutes les " + std::to_string(s.periodMs) + " ms";
    if (s.event == "Changement" && !s.watch.empty()) meta += " de " + s.watch;
    meta += " \xC2\xB7 " + std::string(s.lang == hmi::ScriptLang::ST ? "ST" : s.lang == hmi::ScriptLang::C ? "C" : "C++");
    if (s.detached) meta += " \xC2\xB7 d\xC3\xA9tach\xC3\xA9";
    std::string out = "<h3 class=\"sub\">" + h::escape(s.name) + " <small>" + h::escape(meta) + "</small></h3>\n";
    if (!s.description.empty()) out += "<p class=\"desc\">" + h::escape(s.description) + "</p>\n";
    out += h::code(lines(s.body));
    return out;
}

} // namespace

// ============================================================ la page en deux temps ==
struct HmiWebPageBuilder::Impl {
    hmi::Project             project;
    ui::Theme                theme;
    std::vector<hmi::Id>     order;          // la vue de demarrage, puis les vues et les popups
    std::size_t              next{0};
    HmiImageCache            cache;          // ses images sont celles de ce renderer
    SvgImages                images;
    SvgRenderer              renderer{images};
    struct Drawn {
        hmi::Id              id{hmi::kNoId};
        int                  width{0}, height{0};
        std::string          svg;
        std::vector<Hot>     hots;
    };
    std::vector<Drawn>       drawn;
    std::size_t              elements{0};
    Impl(hmi::Project p, ui::Theme t) : project(std::move(p)), theme(std::move(t)) {
        const hmi::View* start = project.view(project.config.startView);
        if (start && shownView(*start) && start->role == "vue") order.push_back(start->id);
        for (const auto& v : project.views)
            if (shownView(v) && &v != start) order.push_back(v.id);
    }
};

HmiWebPageBuilder::HmiWebPageBuilder(hmi::Project project, ui::Theme theme)
    : impl_(std::make_unique<Impl>(std::move(project), std::move(theme))) {}
HmiWebPageBuilder::~HmiWebPageBuilder() = default;

float HmiWebPageBuilder::progress() const noexcept {
    return impl_->order.empty() ? 1.f : static_cast<float>(impl_->next) / static_cast<float>(impl_->order.size());
}

bool HmiWebPageBuilder::draw(double budgetMs) {
    auto& m = *impl_;
    HmiImageCache::Scope scope(m.cache);
    const auto t0 = std::chrono::steady_clock::now();
    while (m.next < m.order.size()) {
        const auto* v = m.project.view(m.order[m.next++]);
        if (!v) continue;
        const hmi::View shown = prepared(m.project, *v);
        const float w = static_cast<float>(shown.width), hgt = static_cast<float>(shown.height);
        auto& r = m.renderer;
        r.begin(w, hgt, viewId(v->id) + "-");
        HmiPaintOptions opt;
        opt.editor = false;                    // la vue en marche, au repos (sans moteur, sans valeur)
        opt.project = &m.project;
        opt.assets = &m.project.assets;
        r.pushClip({0.f, 0.f, w, hgt});
        paintHmiView(r, shown, HmiViewport{0.f, 0.f, 1.f}, RestingSource{}, m.theme, opt);
        r.popClip();
        m.elements += r.elements();
        Impl::Drawn d;
        d.id = v->id;
        d.width = shown.width;
        d.height = shown.height;
        d.hots = hotsOf(m.project, shown);
        d.svg = "<svg viewBox=\"0 0 " + std::to_string(shown.width) + " " + std::to_string(shown.height) + "\" width=\""
              + std::to_string(shown.width) + "\" height=\"" + std::to_string(shown.height) + "\" font-family=\""
              + h::escape(r.fontFamily()) + "\" role=\"img\" aria-label=\"" + h::escape(v->name) + "\">" + r.body();
        for (const auto& hot : d.hots) d.svg += hotSvg(hot);
        d.svg += "</svg>";
        m.drawn.push_back(std::move(d));
        const double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (spent >= budgetMs) break;
    }
    return m.next >= m.order.size();
}

std::string HmiWebPageBuilder::page(const WebPageInfo& info, WebPageStats* stats) const {
    const auto& m = *impl_;
    const hmi::Project& p = m.project;
    WebPageStats local;
    auto& st = stats ? *stats : local;
    st = WebPageStats{};
    st.elements = m.elements;

    h::Page page;
    page.kind = project::archive::Kind::Ihm;
    page.application = info.application;
    page.title = p.config.name.empty() ? info.manifest.name : p.config.name;
    std::string sub;
    if (!p.config.version.empty()) sub += "version " + p.config.version;
    if (!p.config.author.empty()) sub += (sub.empty() ? "" : " \xC2\xB7 ") + p.config.author;
    sub += (sub.empty() ? std::string("export\xC3\xA9" "e le ") : std::string(" \xC2\xB7 export\xC3\xA9" "e le ")) + info.exportedAt;
    page.subtitle = sub;
    page.archive = info.archive;
    page.archiveName = info.archiveName;

    // ---- les vues et les popups, deja dessinees ---------------------------------------
    h::NavGroup viewsNav{"Vues", {}}, popupsNav{"Popups", {}};
    const hmi::View* start = p.view(p.config.startView);
    for (const auto& d : m.drawn) {
        const auto* v = p.view(d.id);
        if (!v) continue;
        st.svgBytes += d.svg.size();
        st.hotspots += d.hots.size();
        h::Section s;
        s.id = viewId(v->id);
        s.title = v->name;
        const bool popup = v->role == "popup";
        s.css = popup ? "popup" : "view";
        s.subtitle = std::to_string(d.width) + " \xC3\x97 " + std::to_string(d.height) + " px"
                   + (popup ? " \xC2\xB7 popup" : v == start ? " \xC2\xB7 vue de d\xC3\xA9marrage" : "");
        s.attributes = "data-width=\"" + std::to_string(d.width) + "\"";
        s.html = "<div class=\"screen\" style=\"max-width:" + std::to_string(d.width + 14) + "px\">" + d.svg + "</div>\n";
        if (!v->description.empty()) s.html += "<p class=\"desc\">" + h::escape(v->description) + "</p>\n";
        // Ou elle mene (les zones cliquables), une fois chacune.
        std::set<hmi::Id> seen;
        std::string links;
        for (const auto& hot : d.hots) {
            if (hot.target == hmi::kNoId || !seen.insert(hot.target).second) continue;
            const auto* t = p.view(hot.target);
            if (!t) continue;
            const bool toPopup = hot.attribute.rfind("data-popup", 0) == 0;
            links += "<a href=\"#" + viewId(t->id) + "\" " + (toPopup ? "data-popup=\"" : "data-go=\"") + viewId(t->id) + "\">" + h::escape(t->name)
                   + "</a>";
        }
        if (!links.empty()) s.html += "<div class=\"links\">M\xC3\xA8ne \xC3\xA0 : " + links + "</div>\n";
        if (!v->scripts.empty()) {
            s.html += "<details><summary>Ses scripts (" + std::to_string(v->scripts.size()) + ")</summary>\n";
            for (const auto& sc : v->scripts) s.html += scriptBlock(sc);
            s.html += "</details>\n";
        }
        (popup ? popupsNav : viewsNav).links.push_back({s.id, v->name, v == start ? std::string("d\xC3\xA9marrage") : std::string{}});
        (popup ? st.popups : st.views) += 1;
        page.sections.push_back(std::move(s));
    }
    st.images = m.images.count();
    page.defs = m.images.defs();
    page.first = m.drawn.empty() ? std::string("fiche") : viewId(m.drawn.front().id);

    // ---- les alarmes ----------------------------------------------------------------------
    {
        h::Section s;
        s.id = "alarmes";
        s.title = "Alarmes";
        s.subtitle = thousands(p.alarms.size());
        std::vector<std::vector<h::Cell>> rows;
        for (const auto& a : p.alarms)
            rows.push_back({{a.name, "mono", {}}, {std::to_string(a.priority), "num", {}}, {a.category, {}, {}}, {a.group, {}, {}},
                            {a.condition, "mono", {}}, {a.message, {}, {}}, {a.instruction, {}, {}}});
        s.html = h::table({"Alarme", "Priorit\xC3\xA9", "Cat\xC3\xA9gorie", "Groupe", "Condition", "Message", "Consigne"}, rows, true,
                          "Aucune alarme.");
        page.sections.push_back(std::move(s));
    }
    // ---- les variables IHM -------------------------------------------------------------------
    {
        h::Section s;
        s.id = "variables";
        s.title = "Variables IHM";
        s.subtitle = thousands(p.programs.variables.size());
        std::vector<std::vector<h::Cell>> rows;
        for (const auto& v : p.programs.variables) {
            std::string where = v.equipment.empty() ? std::string{} : v.equipment + (v.address.empty() ? std::string{} : " \xC2\xB7 " + v.address);
            rows.push_back({{v.name, "mono", {}}, {v.type, "mono", {}}, {v.initial, "mono", {}}, {where, "mono", {}}, {v.folder, {}, {}},
                            {v.description, {}, {}}});
        }
        s.html = h::table({"Variable", "Type", "Valeur initiale", "\xC3\x89quipement \xC2\xB7 adresse", "Dossier", "Description"}, rows, true,
                          "Aucune variable IHM.");
        page.sections.push_back(std::move(s));
    }
    // ---- les scripts et les fonctions ----------------------------------------------------------
    {
        h::Section s;
        s.id = "scripts";
        s.title = "Scripts et fonctions";
        s.subtitle = std::to_string(p.programs.scripts.size()) + " script(s), " + std::to_string(p.programs.functions.size()) + " fonction(s)";
        for (const auto& sc : p.programs.scripts) s.html += scriptBlock(sc);
        for (const auto& f : p.programs.functions) {
            s.html += "<h3 class=\"sub\">" + h::escape(f.name) + " <small>fonction"
                    + (f.returnType.empty() ? std::string(", sans retour") : " : " + h::escape(f.returnType)) + "</small></h3>\n";
            if (!f.description.empty()) s.html += "<p class=\"desc\">" + h::escape(f.description) + "</p>\n";
            s.html += h::code(lines(f.body));
        }
        if (s.html.empty()) s.html = "<p class=\"desc\">Aucun script g\xC3\xA9n\xC3\xA9ral, aucune fonction IHM.</p>\n";
        page.sections.push_back(std::move(s));
    }
    // ---- la fiche du projet ------------------------------------------------------------------
    {
        const auto stats2 = p.statistics();
        h::Section s;
        s.id = "fiche";
        s.title = "Fiche du projet";
        s.html = h::tiles({{std::to_string(st.views), "vues"}, {std::to_string(st.popups), "popups"}, {thousands(stats2.objects), "objets"},
                           {thousands(p.alarms.size()), "alarmes"}, {thousands(p.programs.variables.size()), "variables IHM"},
                           {thousands(stats2.scripts), "scripts"}, {thousands(p.programs.functions.size()), "fonctions"},
                           {thousands(p.assets.resources.size()), "ressources"}});
        std::vector<std::pair<std::string, std::string>> rows = {
            {"Projet", p.config.name}, {"Description", p.config.description}, {"Version", p.config.version}, {"Auteur", p.config.author},
            {"Cr\xC3\xA9\xC3\xA9", p.config.created}, {"Modifi\xC3\xA9", p.config.modified},
            {"R\xC3\xA9solution", std::to_string(p.config.width) + " \xC3\x97 " + std::to_string(p.config.height) + " (" + p.config.orientation + ")"},
            {"Vue de d\xC3\xA9marrage", start ? start->name : std::string{}}, {"Cycle IHM", std::to_string(p.config.cycleMs) + " ms"},
            {"\xC3\x89quipements", p.equipments.empty() ? std::string{} : std::to_string(p.equipments.size())},
            {"Recettes", p.recipes.empty() ? std::string{} : std::to_string(p.recipes.size())},
            {"Utilisateurs", p.security.users.empty() ? std::string{} : std::to_string(p.security.users.size())}};
        if (info.hasManifest) {
            rows.push_back({"\xC3\x89tat", std::string(project::toString(info.manifest.state))});
            rows.push_back({"Commentaire", info.manifest.comment});
        }
        if (info.archive) rows.push_back({"Archive dans la page", info.archiveName + " (" + project::archive::sizeText(info.archive->size()) + ")"});
        rows.push_back({"Export\xC3\xA9" "e le", info.exportedAt});
        rows.push_back({"Par", info.application});
        s.html += h::card(rows);
        page.sections.push_back(std::move(s));
    }
    page.nav.push_back(std::move(viewsNav));
    page.nav.push_back(std::move(popupsNav));
    page.nav.push_back({"Le projet", {{"alarmes", "Alarmes", thousands(p.alarms.size())},
                                       {"variables", "Variables IHM", thousands(p.programs.variables.size())},
                                       {"scripts", "Scripts et fonctions", std::to_string(p.programs.scripts.size() + p.programs.functions.size())},
                                       {"fiche", "Fiche du projet", {}}}});
    std::string html = h::render(page);
    st.pageBytes = html.size();
    return html;
}


std::string hmiWebPage(const hmi::Project& p, const ui::Theme& theme, const WebPageInfo& info, WebPageStats* stats) {
    HmiWebPageBuilder builder(p, theme);
    while (!builder.draw(1e12)) {}
    return builder.page(info, stats);
}

} // namespace app
