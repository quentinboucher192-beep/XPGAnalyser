// =============================================================================
//  hmi/HmiRuntimeLot12.cpp - la navigation en marche (lot 12)
// -----------------------------------------------------------------------------
//  L'historique (Precedent, Suivant), la vue d'accueil du groupe, le glisser
//  entre les vues ; les clics des objets de navigation et de structure : un
//  bouton de la barre de navigation, une etape du fil d'Ariane, un onglet, la
//  barre d'un panneau defilant (et la molette), le bandeau d'un panneau
//  repliable, une zone d'un plan.
//
//  L'etat des structures (la page montree, le panneau replie, le defilement)
//  est une propriete de l'objet ECRITE EN MARCHE, comme une variable
//  d'instance (Vue.Onglets.Page := 2) : il se lit et s'ecrit pareil, et repart
//  de l'editeur au redemarrage. Un objet relie a une variable ("variable")
//  ecrit la variable a la place.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiNavigation.hpp"
#include "HmiWidgets.hpp"
#include "HmiMarkers.hpp"   // 1.11 (REP) : les reperes $...$

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hmi {

namespace {

// "vue:3" -> 3 ; -1 sinon.
int indexAfter(std::string_view part, std::string_view prefix) {
    if (part.substr(0, prefix.size()) != prefix) return -1;
    const std::string rest(part.substr(prefix.size()));
    if (rest.empty()) return -1;
    char* end = nullptr;
    const long v = std::strtol(rest.c_str(), &end, 10);
    return end && *end == '\0' && v >= 0 ? static_cast<int>(v) : -1;
}

double fractionAfter(std::string_view part, std::string_view prefix) {
    if (part.substr(0, prefix.size()) != prefix) return -1;
    double f = 0;
    if (!parseNumber(std::string(part.substr(prefix.size())), f)) return -1;
    return std::clamp(f, 0.0, 1.0);
}

std::string trimmedText(std::string s) {
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// La transition d'une barre de navigation : son libelle ("Glissement"), 300 ms ;
// un glissement vient du cote de la vue choisie.
Transition barTransition(const Object& bar, int direction) {
    Transition t;
    t.kind = transitionFromLabel(bar.text("transition", "Instantan\xC3\xA9" "e")).value_or(TransitionKind::Instant);
    t.durationMs = 300;
    if (t.kind == TransitionKind::Slide) t.direction = direction >= 0 ? "droite" : "gauche";
    return t;
}

} // namespace

void Runtime::resetLot12() {
    back_.clear();
    forward_.clear();
    currentArgs_.clear();
    historyMove_ = false;
    viewZoom_ = 100;
}

// ========================================================== l'historique ====
std::vector<std::string> Runtime::historyNames() const {
    std::vector<std::string> out;
    for (const auto& e : back_)
        if (const View* v = project_ ? project_->view(e.view) : nullptr) out.push_back(v->name);
    return out;
}

bool Runtime::goBack(const Transition& t, double now, int steps, std::string* why) {
    now_ = std::max(now_, now);
    if (!running_ || steps < 1) return false;
    if (static_cast<int>(back_.size()) < steps) {
        if (why) *why = "aucune vue pr\xC3\xA9" "c\xC3\xA9" "dente";
        return false;
    }
    // Les vues sautees et la vue quittee passent dans Suivant, la plus proche en haut.
    std::vector<NavEntry> skipped(back_.end() - steps, back_.end());
    back_.erase(back_.end() - steps, back_.end());
    const NavEntry target = skipped.front();
    forward_.push_back({current_, currentArgs_});
    for (std::size_t k = skipped.size(); k-- > 1;) forward_.push_back(skipped[k]);
    if (!viewOf(target.view)) {
        if (why) *why = "la vue pr\xC3\xA9" "c\xC3\xA9" "dente n'existe plus";
        return false;
    }
    historyMove_ = true;
    Transition tt = t;
    if (tt.kind == TransitionKind::Slide && tt.direction.empty()) tt.direction = "gauche";
    const bool done = navigate(target.view, tt, now, target.arguments);
    historyMove_ = false;
    return done;
}

bool Runtime::goForward(const Transition& t, double now, std::string* why) {
    now_ = std::max(now_, now);
    if (!running_) return false;
    if (forward_.empty()) {
        if (why) *why = "aucune vue suivante";
        return false;
    }
    const NavEntry target = forward_.back();
    forward_.pop_back();
    if (!viewOf(target.view)) {
        if (why) *why = "la vue suivante n'existe plus";
        return false;
    }
    back_.push_back({current_, currentArgs_});
    historyMove_ = true;
    const bool done = navigate(target.view, t, now, target.arguments);
    historyMove_ = false;
    return done;
}

Id Runtime::homeView() const {
    if (!project_) return kNoId;
    if (const User* u = user())
        if (const UserGroup* g = project_->group(u->group); g && g->startView != kNoId && project_->view(g->startView))
            return g->startView;
    return project_->config.startView;
}

bool Runtime::goHome(const Transition& t, double now, std::string* why) {
    const Id home = homeView();
    if (home == kNoId || !viewOf(home)) {
        if (why) *why = "aucune vue d'accueil";
        return false;
    }
    if (home == current_ && popups_.empty()) {
        if (why) *why = "d\xC3\xA9j\xC3\xA0 sur la vue d'accueil";
        return false;
    }
    return navigate(home, t, now, {});
}

std::string Runtime::navigationPath() const {
    if (!project_) return {};
    const View* v = project_->view(current_);
    std::vector<std::string> names;
    for (int guard = 0; v && guard < 32; ++guard) {
        names.push_back(viewCaption(v->name));
        v = v->upView != kNoId && v->upView != v->id ? project_->view(v->upView) : nullptr;
    }
    std::string s;
    for (auto it = names.rbegin(); it != names.rend(); ++it) s += (s.empty() ? "" : " > ") + *it;
    return s;
}

bool Runtime::navigateByUser(const View& v, const Object* o, Operation op, const std::string& target, Transition t, double now) {
    Action a;
    a.trigger = Trigger::Click;
    a.operation = op;
    a.target = target;
    a.transition = std::move(t);
    const Id before = current_;
    const auto backs = back_.size();
    fire(v, o, a, now, true);
    return current_ != before || back_.size() != backs;
}

bool Runtime::swipe(int direction, double now, std::string* why) {
    // 1.13.0 : EN PAUSE (le debogueur), l'IHM est figee : le glissement ne change pas de vue (ni OnClose ni OnOpen), et
    // rien ne se vide (une fonction de vue peut etre arretee au milieu).
    if (paused_) {
        if (why) *why = "l'IHM est en pause (d\xC3\xA9" "bogage) : Continuer (F5) d'abord";
        return false;
    }
    if (unitWanted()) {
        auto done = std::make_shared<bool>(false);
        auto said = std::make_shared<std::string>();
        (void)debugUnit("Glisser", [this, direction, now, done, said] { *done = swipe(direction, now, said.get()); });
        if (why) *why = *said;
        return *done;
    }
    now_ = std::max(now_, now);
    lastActivity_ = now;
    composed_.clear(); boundCalls_.clear();
    if (!running_ || !project_ || direction == 0) return false;
    if (!project_->config.swipeNavigation) {
        if (why) *why = "le changement de vue en glissant n'est pas activ\xC3\xA9 (Configuration)";
        return false;
    }
    if (!popups_.empty()) {
        if (why) *why = "une popup est ouverte";
        return false;
    }
    const View* cur = viewOf(current_);
    if (!cur) return false;
    // L'ordre : la barre de navigation de la vue qui porte la vue courante, sinon
    // les vues ordinaires du projet.
    std::vector<std::string> order;
    for (const auto& so : cur->objects) {
        if (so.kind != Kind::NavBar) continue;
        Object o = so;
        evaluateProps(cur->id, o);      // 1.12.6 (lot B) : ses vues telles que l'ecran les montre
        std::vector<std::string> names;
        for (const auto& it : navItems(project_, o)) names.push_back(it.view);
        for (const auto& n : names)
            if (n == cur->name) {
                order = names;
                break;
            }
        if (!order.empty()) break;
    }
    if (order.empty())
        for (const auto& v : project_->views)
            if (v.role == "vue") order.push_back(v.name);
    const auto it = std::find(order.begin(), order.end(), cur->name);
    if (it == order.end()) {
        if (why) *why = "la vue courante n'est pas dans l'ordre des vues";
        return false;
    }
    const long at = static_cast<long>(it - order.begin()) + (direction > 0 ? 1 : -1);
    if (at < 0 || at >= static_cast<long>(order.size())) {
        if (why) *why = direction > 0 ? "d\xC3\xA9j\xC3\xA0 sur la derni\xC3\xA8re vue" : "d\xC3\xA9j\xC3\xA0 sur la premi\xC3\xA8re vue";
        return false;
    }
    Transition t;
    t.kind = TransitionKind::Slide;
    t.durationMs = 350;
    t.direction = direction > 0 ? "droite" : "gauche";
    const std::string previous = source_;
    source_ = "glisser";
    const bool done = navigateByUser(*cur, nullptr, Operation::Navigate, order[static_cast<std::size_t>(at)], t, now);
    source_ = previous;
    return done;
}

// ================================================================== clics ====
void Runtime::setOverride(const View& v, const Object& o, const std::string& key, const std::string& value) {
    overrides_[{v.id, o.id}][key] = value;
    // 1.12.6 (lot B, § 2.7) : une propriete pilotee par une formule - le geste vaut a cote d'elle, tant
    // qu'elle garde sa valeur d'ici (releaseGestures, a chaque appel du moteur).
    if (const Prop* p = o.find(key); p && !p->expr.empty()) {
        const Prop formula{key, {}, p->expr};
        gestureFormulas_[{v.id, o.id, key}] = GestureFormula{o.kind, p->expr, evaluatedProp(v.id, o.kind, formula).value_or(std::string{})};
    } else {
        gestureFormulas_.erase({v.id, o.id, key});
    }
    composed_.clear(); boundCalls_.clear();
}

void Runtime::releaseGestures() {
    for (auto it = gestureFormulas_.begin(); it != gestureFormulas_.end();) {
        const auto& [view, object, key] = it->first;
        const Prop formula{key, {}, it->second.expr};
        const std::string now = evaluatedProp(view, it->second.kind, formula).value_or(std::string{});
        if (now == it->second.seen) { ++it; continue; }
        // La formule a change : elle reprend la main, le geste tombe.
        if (auto ov = overrides_.find({view, object}); ov != overrides_.end()) {
            ov->second.erase(key);
            if (ov->second.empty()) overrides_.erase(ov);
        }
        it = gestureFormulas_.erase(it);
        composed_.clear(); boundCalls_.clear();
    }
}

// 1.12.3 : l'etat des structures que l'operateur a change (le poste le garde).
std::vector<Runtime::UiState> Runtime::uiState() const {
    std::vector<UiState> out;
    for (const auto& [at, props] : overrides_) {
        const View* v = viewOf(at.first);
        const Object* o = v ? v->object(at.second) : nullptr;
        if (!o) continue;
        for (const auto& [key, value] : props) {
            const bool kept = (o->kind == Kind::TabContainer && key == "page") || (o->kind == Kind::CollapsiblePanel && key == "collapsed")
                           || (o->kind == Kind::ScrollPanel && (key == "scrollX" || key == "scrollY"))
                           || (o->kind == Kind::DocumentViewer && key == "page");   // 1.12.30 : la page lue (sans variable)
            if (kept) out.push_back({at.first, at.second, key, value});
        }
    }
    return out;
}

void Runtime::lot12Part(const View& v, const Object& o, std::string_view part, double now) {
    const std::string source = where(v, &o);
    switch (o.kind) {
        case Kind::NavBar: {
            if (part == "precedent" || part == "suivant") {
                (void)navigateByUser(v, &o, part == "precedent" ? Operation::NavigateBack : Operation::NavigateForward, {},
                                     barTransition(o, part == "precedent" ? -1 : 1), now);
                return;
            }
            const int k = indexAfter(part, "vue:");
            const auto items = navItems(project_, o);
            if (k < 0 || static_cast<std::size_t>(k) >= items.size()) return;
            const View* cur = viewOf(current_);
            int from = -1;
            for (std::size_t i = 0; i < items.size(); ++i)
                if (cur && items[i].view == cur->name) from = static_cast<int>(i);
            if (cur && items[static_cast<std::size_t>(k)].view == cur->name && popups_.empty()) return;   // deja la
            (void)navigateByUser(v, &o, Operation::Navigate, items[static_cast<std::size_t>(k)].view,
                                 barTransition(o, k >= from ? 1 : -1), now);
            return;
        }
        case Kind::Breadcrumb: {
            const int k = indexAfter(part, "etape:");
            const View* cur = viewOf(current_);
            const View* home = project_->view(homeView());
            const auto trail = breadcrumbTrail(*project_, o, cur ? cur->name : std::string{}, historyNames(),
                                               home ? home->name : std::string{});
            if (k < 0 || static_cast<std::size_t>(k) >= trail.size()) return;
            const auto& c = trail[static_cast<std::size_t>(k)];
            if (c.back == 0 || (cur && c.view == cur->name && c.back < 0)) return;    // la vue courante
            if (c.back > 0) {
                if (project_->security.enabled && !permitted("Naviguer")) {
                    event("Acc\xC3\xA8s refus\xC3\xA9", source, "fil d'Ariane : permission \xC2\xAB Naviguer \xC2\xBB requise");
                    return;
                }
                std::string why;
                const std::string previous = source_;
                source_ = source;
                if (!goBack(Transition{}, now, c.back, &why)) log("Action", source, "fil d'Ariane : " + why);
                source_ = previous;
                return;
            }
            (void)navigateByUser(v, &o, Operation::Navigate, c.view, Transition{}, now);
            return;
        }
        case Kind::TabContainer: {
            const int k = indexAfter(part, "onglet:");
            const auto labels = tabLabels(o);
            if (k < 1 || static_cast<std::size_t>(k) > labels.size()) return;
            const std::string var = trimmedText(markers::strip(o.text("variable")));
            if (!var.empty()) {
                // 1.12.5 (lot A) : ce qu'on ecrit suit le type de la variable - une enumeration sa
                // k-ieme valeur (avant : k, decale d'un cran), un BOOL l'onglet 2 = VRAI, une STRING
                // le nom de l'onglet ; un nombre, le rang (1, 2...).
                sim::Value cur;
                const bool known = environment().read(var, cur);
                sim::Value next = sim::Value::integer(sim::Type::Int, k);
                const auto values = splitSemicolons(o.text("pageValues"));
                if (!values.empty()) {
                    if (static_cast<std::size_t>(k) > values.size()) return;
                    double n = 0;
                    (void)parseNumber(values[static_cast<std::size_t>(k - 1)], n);
                    next = sim::Value::integer(sim::Type::DInt, static_cast<std::int64_t>(std::llround(n)));
                } else if (known && cur.type() == sim::Type::Bool) {
                    if (k > 2) { log("Action", source, "onglet " + std::to_string(k) + " : la variable est un BOOL (onglets 1 et 2 seulement)"); return; }
                    next = sim::Value::boolean(k == 2);
                } else if (known && cur.type() == sim::Type::String) {
                    next = sim::Value::text(labels[static_cast<std::size_t>(k - 1)]);
                }
                (void)writeConverted(var, next, source, &o);   // 1.12.6 (lot B) : vers son type
            } else {
                setOverride(v, o, "page", std::to_string(k));
            }
            log("Action", source, "onglet " + std::to_string(k) + " : " + labels[static_cast<std::size_t>(k - 1)]);
            return;
        }
        case Kind::ScrollPanel: {
            const auto l = scrollLayout(v, o);
            double sx = l.scrollX, sy = l.scrollY;
            if (const double f = fractionAfter(part, "vbarre:"); f >= 0) sy = f * l.maxY;
            else if (const double g = fractionAfter(part, "hbarre:"); g >= 0) sx = g * l.maxX;
            else if (part.substr(0, 8) == "defiler:") {
                double d = 0;
                if (!parseNumber(std::string(part.substr(8)), d)) return;
                if (l.maxY > 0) sy = std::clamp(sy + d, 0.0, l.maxY);
                else sx = std::clamp(sx + d, 0.0, l.maxX);
            } else if (part.substr(0, 9) == "defilerx:") {
                double d = 0;
                if (!parseNumber(std::string(part.substr(9)), d)) return;
                sx = std::clamp(sx + d, 0.0, l.maxX);
            } else {
                return;
            }
            if (std::fabs(sx - l.scrollX) > 1e-6) setOverride(v, o, "scrollX", formatNumber(std::round(sx)));
            if (std::fabs(sy - l.scrollY) > 1e-6) setOverride(v, o, "scrollY", formatNumber(std::round(sy)));
            return;
        }
        case Kind::CollapsiblePanel: {
            if (part != "entete") return;
            const std::string var = trimmedText(markers::strip(o.text("variable")));
            bool collapsed = o.flag("collapsed");
            sim::Value cur;
            // 1.12.5 (lot A) : comme le dessin - un nombre different de 0 (2 aussi), TRUE, VRAI, une STRING
            // 'TRUE' (avant : 2 dessine deplie mais lu replie ; une STRING ne se depliait plus).
            if (!var.empty() && environment().read(var, cur)) collapsed = cur.type() == sim::Type::String ? valueOn(cur.asString()) : cur.isTruthy();
            else if (!o.expr("collapsed").empty()) collapsed = evalBool(o.expr("collapsed"), collapsed);
            const bool next = !collapsed;
            if (!var.empty()) (void)writeConverted(var, sim::Value::boolean(next), source, &o);   // 1.12.6 : une STRING : 'TRUE'
            else setOverride(v, o, "collapsed", next ? "TRUE" : "FALSE");
            log("Action", source, next ? std::string("panneau repli\xC3\xA9") : std::string("panneau d\xC3\xA9pli\xC3\xA9"));
            return;
        }
        case Kind::ZoneMap: {
            const int k = indexAfter(part, "zone:");
            const auto zones = parseMapZones(o.text("mapZones"));
            if (k < 0 || static_cast<std::size_t>(k) >= zones.size()) return;
            const auto& z = zones[static_cast<std::size_t>(k)];
            selectedZone_ = z.alarmGroup();
            const std::string var = trimmedText(markers::strip(o.text("variable")));
            // 1.12.12 (lot H) : la variable recoit le nom de la zone (un STRING) ou son numero (un entier, 1 pour la
            // premiere) ; avant : le groupe d'alarmes en texte, 0 dans un entier. Ce qui est ecrit est retenu : le
            // cycle ne le prend pas pour un choix du programme.
            if (!var.empty()) {
                sim::Value cur;
                const bool number = environment().read(var, cur) && cur.type() != sim::Type::String && cur.type() != sim::Type::Unknown;
                const sim::Value chosen = number ? sim::Value::integer(sim::Type::DInt, k + 1) : sim::Value::text(markers::strip(z.name, markers::Mode::Text));
                if (writeConverted(var, chosen, source, &o) && environment().read(var, cur))
                    zoneMapSeen_[o.id] = cur.type() == sim::Type::String ? cur.asString() : std::to_string(cur.asInteger());
            }
            log("Action", source, "zone choisie : " + selectedZone_);
            if (!z.view.empty()) {
                Transition t;
                t.kind = TransitionKind::Zoom;
                t.durationMs = 300;
                t.direction = "avant";
                (void)navigateByUser(v, &o, Operation::Navigate, z.view, t, now);
            }
            return;
        }
        default:
            return;
    }
}

// 1.12.12 (lot H, § 3.9) : LA VARIABLE D'UN PLAN A ZONES, RELUE. Un programme qui l'ecrit choisit la zone, comme
// un clic sans la navigation : un STRING, la zone de ce nom (ou de ce groupe d'alarmes) ; un entier, la zone de ce
// numero (1 pour la premiere ; 0 : aucune). Seul un changement compte (ce que le clic a ecrit est deja vu) : un
// choix fait ailleurs (le resume par zone) n'est pas defait a chaque cycle.
void Runtime::zoneMapsCycle() {
    if (!project_) return;
    for (const auto& view : project_->views)
        for (const auto& o : view.objects) {
            if (o.kind != Kind::ZoneMap) continue;
            const std::string var = trimmedText(markers::strip(o.text("variable")));
            sim::Value cur;
            if (var.empty() || !environment().read(var, cur)) continue;
            const bool text = cur.type() == sim::Type::String;
            const std::string seen = text ? cur.asString() : std::to_string(cur.asInteger());
            auto& last = zoneMapSeen_[o.id];
            if (seen == last) continue;
            last = seen;
            const auto zones = parseMapZones(o.text("mapZones"));
            const MapZone* chosen = nullptr;
            if (text) {
                const std::string wanted = trimmedText(seen);
                for (const auto& z : zones)
                    if (!wanted.empty() && (markers::strip(z.name, markers::Mode::Text) == wanted || z.alarmGroup() == wanted)) {
                        chosen = &z;
                        break;
                    }
            } else if (const long long n = cur.asInteger(); n >= 1 && n <= static_cast<long long>(zones.size())) {
                chosen = &zones[static_cast<std::size_t>(n - 1)];
            }
            if (chosen) {
                selectedZone_ = chosen->alarmGroup();
                log("Action", where(view, &o), "zone choisie par le programme : " + selectedZone_);
            } else if (!selectedZone_.empty()) {
                for (const auto& z : zones)
                    if (z.alarmGroup() == selectedZone_) {
                        selectedZone_.clear();   // aucune zone de ce plan (0, un nom inconnu) : le choix part
                        break;
                    }
            }
        }
}

} // namespace hmi
