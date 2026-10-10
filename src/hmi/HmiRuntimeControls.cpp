// =============================================================================
//  hmi/HmiRuntimeControls.cpp - le moteur des commandes et des afficheurs (lot 9)
// -----------------------------------------------------------------------------
//  Les commandes ecrivent LEUR variable ("variable"), comme une action Affecter :
//  le niveau d'acces de l'objet, puis la permission Piloter sous securite, les
//  parametres de la vue (une popup ouverte pour Pompes[3] ecrit Pompes[3]), le
//  journal. Rien n'est ecrit sans variable, et le refus est dit.
//
//    Bouton a impulsion   vrai a l'appui, faux au relachement (inverse : l'envers)
//    Interrupteur, case   un clic bascule la variable
//    Bouton lumineux      basculer, impulsion, mettre a 1, mettre a 0, ou rien
//    Selecteur, radio     la valeur du choix clique (ou son rang)
//    Liste deroulante     ouvrir, faire defiler, choisir
//    Curseur, potentiom.  la valeur suit la souris ; ecrite au relachement
//    Date et heure        les fleches reglent, Valider ecrit
//    Programmateur        une case, un jour, une heure s'allument ; sa sortie
//                         est vraie pendant une plage
//
//  Et les afficheurs qui ont une memoire : le compteur horaire (il compte tant
//  que sa condition est vraie) et la fleche de tendance (la valeur d'il y a N
//  secondes contre celle de maintenant).
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiControls.hpp"
#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"   // 1.11 (REP) : les reperes $...$
#include "HmiEnums.hpp"     // 1.12.2 : une enumeration, source d'elements
#include "HmiTypes.hpp"     // 1.12.2 : un tableau IHM, source d'elements ou de lignes

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace hmi {

namespace {

std::string trimmedText(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Le compteur horaire dans sa variable : TIME en millisecondes, REAL en heures,
// un entier en secondes.
double counterSeconds(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::Time: return static_cast<double>(v.asInteger()) / 1000.0;
        case sim::Type::Real: return v.asReal() * 3600.0;
        case sim::Type::String: case sim::Type::Unknown: return 0;
        default: return static_cast<double>(v.asInteger());
    }
}
sim::Value counterValue(sim::Type t, double seconds) {
    switch (t) {
        case sim::Type::Time: return sim::Value::time(static_cast<std::int64_t>(std::llround(seconds * 1000.0)));
        case sim::Type::Real: return sim::Value::real(seconds / 3600.0);
        default: return sim::Value::integer(t == sim::Type::Unknown ? sim::Type::DInt : t, static_cast<std::int64_t>(std::floor(seconds)));
    }
}

int indexAfter(std::string_view part, std::string_view prefix) {
    if (part.substr(0, prefix.size()) != prefix) return -1;
    const std::string rest(part.substr(prefix.size()));
    if (rest.empty() || !std::isdigit(static_cast<unsigned char>(rest[0]))) return -1;
    return std::atoi(rest.c_str());
}

} // namespace

// ================================================================= ecrire ===
bool Runtime::numberProp(const Object& o, std::string_view key, double& out) {
    const auto* p = o.find(key);
    if (!p) return false;
    if (!p->expr.empty()) {
        bool ok = false;
        const auto text = evalText(p->expr, &ok);
        return ok && parseNumber(text, out);
    }
    return parseNumber(p->value, out);
}

bool Runtime::writeCommand(const View& v, const Object& o, const sim::Value& value, double now, const std::string& shown,
                           bool journal) {
    const auto aliases = viewAliases(v.id);
    const std::string source = where(v, &o);
    std::string why;
    if (!objectAllowed(o, &why)) {
        event("Acc\xC3\xA8s refus\xC3\xA9", source, why);
        formMessage(o.id, why, true, now);
        return false;
    }
    if (project_ && project_->security.enabled && !permitted("Piloter")) {
        const std::string refused = std::string(kindLabel(o.kind)) + " : permission \xC2\xAB Piloter \xC2\xBB requise ("
                                  + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", source, refused);
        formMessage(o.id, "permission Piloter requise", true, now);
        return false;
    }
    const std::string var = trimmedText(markers::strip(o.text("variable")));
    if (var.empty()) {
        log("Erreur", source, std::string(kindLabel(o.kind)) + " sans variable : rien n'est \xC3\xA9" "crit");
        formMessage(o.id, "aucune variable reli\xC3\xA9" "e", true, now);
        return false;
    }
    if (!write(var, value, source)) {
        formMessage(o.id, "variable inconnue : " + var, true, now);
        return false;
    }
    if (journal) {
        sim::Value after;
        const std::string text = environment().read(var, after) ? formatValue(after) : formatValue(value);
        log("Action", source, var + " = " + text + (shown.empty() || shown == text ? std::string{} : " (" + shown + ")"));
    }
    auto& f = forms_[o.id];
    if (f.error) {
        f.message.clear();
        f.error = false;
    }
    return true;
}

std::optional<sim::Value> Runtime::choiceValue(const Object& o, const Choice& c) {
    const std::string var = trimmedText(markers::strip(o.text("variable")));
    sim::Value cur;
    const bool known = !var.empty() && environment().read(var, cur);
    if (c.index) {
        if (known && cur.type() == sim::Type::String) return sim::Value::text(c.label);
        return sim::Value::integer(sim::Type::DInt, std::atoll(c.value.c_str()));
    }
    auto e = Expression::compile(c.value);
    if (e.valid())
        if (auto val = e.evaluate(environment())) return *val;
    // Un mot sans apostrophes (Azote) : un texte.
    std::string t = trimmedText(c.value);
    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') t = t.substr(1, t.size() - 2);
    return sim::Value::text(t);
}

// ============================================================ appui, clic ===
void Runtime::controlPress(const View& v, const Object& o, double now) {
    auto& st = controls_[o.id];
    st.holdFired = false;
    if (o.kind == Kind::PushButton) {
        (void)writeCommand(v, o, sim::Value::boolean(!o.flag("inverted", false)), now);
    } else if (o.kind == Kind::IlluminatedButton && o.text("operation") == "impulsion") {
        (void)writeCommand(v, o, sim::Value::boolean(true), now);
    }
}

void Runtime::controlRelease(const View& v, const Object& o, double now, bool click) {
    const auto toggle = [&] {
        const auto aliases = viewAliases(v.id);
        sim::Value cur;
        const std::string var = trimmedText(markers::strip(o.text("variable")));
        const bool on = !var.empty() && environment().read(var, cur) && cur.isTruthy();
        (void)writeCommand(v, o, sim::Value::boolean(!on), now);
    };
    switch (o.kind) {
        case Kind::PushButton:
            (void)writeCommand(v, o, sim::Value::boolean(o.flag("inverted", false)), now);
            break;
        case Kind::IlluminatedButton: {
            const std::string op = o.text("operation", "basculer");
            if (op == "impulsion") (void)writeCommand(v, o, sim::Value::boolean(false), now);
            else if (!click) break;
            else if (op == "basculer") toggle();
            else if (op == "mettre \xC3\xA0 1") (void)writeCommand(v, o, sim::Value::boolean(true), now);
            else if (op == "mettre \xC3\xA0 0") (void)writeCommand(v, o, sim::Value::boolean(false), now);
            break;
        }
        case Kind::Switch:
        case Kind::CheckBox:
            if (click) toggle();
            break;
        default:
            break;
    }
}

bool Runtime::buttonConfirmHolds(const Object& o, double now) {
    const std::string mode = o.text("confirmMode", "aucune");
    if (mode == "second clic") {
        auto& st = controls_[o.id];
        if (now > st.confirmUntil) {
            st.confirmUntil = now + 3.0;
            return true;
        }
        st.confirmUntil = 0;
        return false;
    }
    return mode == "maintien";      // c'est l'appui maintenu qui agit (holdTick)
}

void Runtime::holdTick(double now) {
    if (pressed_ == kNoId || longFired_) return;
    const View* v = shownViewOf(pressed_);
    const Object* o = v ? v->object(pressed_) : nullptr;
    if (!o || o->kind != Kind::Button || o->text("confirmMode") != "maintien") return;
    auto& st = controls_[o->id];
    if (st.holdFired) return;
    const double hold = std::clamp(o->number("holdMs", 2000), 200.0, 20000.0) / 1000.0;
    if (now - pressStart_ < hold) return;
    st.holdFired = true;
    longFired_ = true;           // le relachement ne sera pas un clic
    const View snapshot = *v;
    GestureGuard gesture(*this, where(snapshot, snapshot.object(pressed_)));      // lot 13 : l'audit
    // Lot 13 : l'appui maintenu a confirme ; la signature vient ensuite.
    if (signatureNeeded(snapshot, *snapshot.object(pressed_))) {
        requestSignature(snapshot, *snapshot.object(pressed_), SignatureRequest::Gesture::Click, now);
        return;
    }
    runActions(snapshot, snapshot.object(pressed_), Trigger::Click, now);
}

bool Runtime::confirmPending(Id object, double now) const {
    const auto it = controls_.find(object);
    return it != controls_.end() && it->second.confirmUntil > now;
}

double Runtime::holdProgress(Id object, double now) const {
    if (pressed_ != object || object == kNoId) return 0;
    const View* v = shownViewOf(object);
    const Object* o = v ? v->object(object) : nullptr;
    if (!o || o->kind != Kind::Button || o->text("confirmMode") != "maintien") return 0;
    const auto it = controls_.find(object);
    if (it != controls_.end() && it->second.holdFired) return 1;
    const double hold = std::clamp(o->number("holdMs", 2000), 200.0, 20000.0) / 1000.0;
    return std::clamp((now - pressStart_) / hold, 0.0, 1.0);
}

// ================================================================= parties ===
void Runtime::controlPart(const View& v, const Object& o, std::string_view part, double now) {
    auto& st = controls_[o.id];
    const auto shownValue = [&]() -> std::string {
        const auto aliases = viewAliases(v.id);
        const std::string state = trimmedText(o.text("state"));
        const std::string src = state.empty() ? trimmedText(markers::strip(o.text("variable"))) : state;
        if (src.empty()) return {};
        bool ok = false;
        const std::string text = evalText(src, &ok);
        return ok ? text : std::string{};
    };
    const auto choose = [&](const std::vector<Choice>& choices, int i) {
        if (i < 0 || static_cast<std::size_t>(i) >= choices.size()) return;
        const auto aliases = viewAliases(v.id);
        if (auto value = choiceValue(o, choices[static_cast<std::size_t>(i)]))
            (void)writeCommand(v, o, *value, now, choices[static_cast<std::size_t>(i)].label);
    };
    switch (o.kind) {
        case Kind::Selector: {
            const auto choices = choicesOf(o);
            if (choices.empty()) return;
            int i = indexAfter(part, "position:");
            if (part == "suivant") {
                const int cur = choiceIndexOf(choices, shownValue());
                i = (cur + 1) % static_cast<int>(choices.size());
            }
            choose(choices, i);
            return;
        }
        case Kind::RadioGroup:
            choose(choicesOf(o), indexAfter(part, "option:"));
            return;
        case Kind::List: {
            // 1.12.2 : une vraie liste - un clic choisit (la variable ecrite), les bandes la font defiler.
            // Sans variable, elle se lit seulement (un clic ne dit rien).
            const auto choices = choicesOf(o);
            if (part == "defiler:-1" || part == "defiler:1") {
                const auto box = o.box();
                const std::size_t shown = listLayout(o, box.w, box.h, choices.size(), 0).rows.size();
                const long step = part == "defiler:1" ? 1 : -1;
                const long last = static_cast<long>(choices.size() > shown ? choices.size() - shown : 0);
                st.comboFirst = static_cast<std::size_t>(std::clamp(static_cast<long>(st.comboFirst) + step, 0L, last));
                return;
            }
            if (trimmedText(markers::strip(o.text("variable"))).empty()) return;
            if (const int i = indexAfter(part, "choix:"); i >= 0) choose(choices, i);
            return;
        }
        case Kind::Table: {
            // 1.12.2 : un tableau dynamique (rowsFrom) defile - "defiler:-N" / "defiler:N" (la molette,
            // un clic sur sa barre), borne a ses lignes.
            if (part.rfind("defiler:", 0) != 0) return;
            const long step = std::atol(std::string(part.substr(8)).c_str());
            std::vector<std::string> headers;
            std::vector<std::vector<std::string>> rows;
            if (step == 0 || !tableRows(o, headers, rows)) return;
            const auto box = o.box();
            const std::size_t fit = tableLayout(o, box.w, box.h, rows.size(), 0).fit;
            const long last = static_cast<long>(rows.size() > fit ? rows.size() - fit : 0);
            st.comboFirst = static_cast<std::size_t>(std::clamp(static_cast<long>(st.comboFirst) + step, 0L, last));
            return;
        }
        case Kind::ComboBox: {
            const auto choices = choicesOf(o);
            const auto maxVisible = static_cast<std::size_t>(std::clamp(o.number("maxVisible", 6), 1.0, 40.0));
            if (part == "ouvrir") {
                st.comboOpen = !st.comboOpen;
                if (st.comboOpen) {
                    // La ligne choisie en vue.
                    const int cur = choiceIndexOf(choices, shownValue());
                    st.comboFirst = cur >= 0 && static_cast<std::size_t>(cur) >= maxVisible ? static_cast<std::size_t>(cur) - maxVisible + 1 : 0;
                }
                return;
            }
            if (part == "fermer") { st.comboOpen = false; return; }
            if (part == "defiler:-1" || part == "defiler:1") {
                const long step = part == "defiler:1" ? 1 : -1;
                const long last = static_cast<long>(choices.size() > maxVisible ? choices.size() - maxVisible : 0);
                st.comboFirst = static_cast<std::size_t>(std::clamp(static_cast<long>(st.comboFirst) + step, 0L, last));
                return;
            }
            if (const int i = indexAfter(part, "choix:"); i >= 0) {
                st.comboOpen = false;
                choose(choices, i);
            }
            return;
        }
        case Kind::DateTimePicker: {
            if (part.rfind("plus:", 0) == 0 || part.rfind("moins:", 0) == 0) {
                if (!st.picker) st.picker = pickerValue(o.id);
                const bool plus = part.rfind("plus:", 0) == 0;
                st.picker = stepDateTime(*st.picker, part.substr(plus ? 5 : 6), plus ? 1 : -1);
                return;
            }
            if (part == "maintenant") {
                st.picker = dateTimeFromEpoch(epochOf(now));
                return;
            }
            if (part != "valider") return;
            const DateTime d = st.picker ? *st.picker : pickerValue(o.id);
            const std::string fields = o.text("fields", "date et heure");
            const bool seconds = o.flag("seconds", false);
            const auto aliases = viewAliases(v.id);
            const std::string var = trimmedText(markers::strip(o.text("variable")));
            sim::Value cur;
            const bool known = !var.empty() && environment().read(var, cur);
            sim::Value next;
            if (!known || cur.type() == sim::Type::String) {
                next = sim::Value::text(isoDateTime(d, fields, seconds));
            } else if (cur.type() == sim::Type::Time) {
                next = sim::Value::time((static_cast<std::int64_t>(d.hour) * 3600 + d.minute * 60 + (seconds ? d.second : 0)) * 1000);
            } else if (cur.type() == sim::Type::Real) {
                next = sim::Value::real(epochFromDateTime(d));
            } else {
                next = sim::Value::integer(cur.type(), static_cast<std::int64_t>(epochFromDateTime(d)));
            }
            if (writeCommand(v, o, next, now, frenchDateTime(d, fields, seconds))) {
                st.picker.reset();
                st.current = d;
            }
            return;
        }
        case Kind::WeeklySchedule: {
            WeekSchedule ws = scheduleOf(o, st);
            const int slots = ws.slots();
            bool changed = false;
            if (part.rfind("case:", 0) == 0) {
                const std::string rest(part.substr(5));
                const auto comma = rest.find(',');
                if (comma == std::string::npos) return;
                const int d = std::atoi(rest.substr(0, comma).c_str()), s = std::atoi(rest.substr(comma + 1).c_str());
                if (d < 0 || d > 6 || s < 0 || s >= slots) return;
                auto&& cell = ws.days[static_cast<std::size_t>(d)][static_cast<std::size_t>(s)];
                cell = !cell;
                changed = true;
            } else if (const int d = indexAfter(part, "jour:"); d >= 0 && d <= 6) {
                auto& day = ws.days[static_cast<std::size_t>(d)];
                const bool any = std::find(day.begin(), day.end(), true) != day.end();
                std::fill(day.begin(), day.end(), !any);
                changed = true;
            } else if (const int s = indexAfter(part, "heure:"); s >= 0 && s < slots) {
                bool all = true;
                for (const auto& day : ws.days) all = all && day[static_cast<std::size_t>(s)];
                for (auto& day : ws.days) day[static_cast<std::size_t>(s)] = !all;
                changed = true;
            }
            if (!changed) return;
            const std::string text = formatSchedule(ws);
            const std::string var = trimmedText(markers::strip(o.text("variable")));
            if (!var.empty()) {
                if (!writeCommand(v, o, sim::Value::text(text), now, {}, false)) return;
                st.scheduleText = text;
            } else {
                // Sans variable : pas de journal d'ecriture, mais la securite compte.
                std::string why;
                if (!objectAllowed(o, &why)) { event("Acc\xC3\xA8s refus\xC3\xA9", where(v, &o), why); return; }
                if (project_ && project_->security.enabled && !permitted("Piloter")) {
                    event("Acc\xC3\xA8s refus\xC3\xA9", where(v, &o), "programmateur : permission \xC2\xAB Piloter \xC2\xBB requise");
                    formMessage(o.id, "permission Piloter requise", true, now);
                    return;
                }
            }
            st.schedule = ws;
            st.output = -1;          // la sortie sera recalculee au prochain cycle
            log("Action", where(v, &o), "plages : " + (text.empty() ? std::string("aucune") : text));
            return;
        }
        default:
            return;
    }
}

// ================================================================ glisser ===
void Runtime::dragValue(Id object, double fraction, bool commit, double now) {
    now_ = std::max(now_, now);
    lastActivity_ = now;
    if (!running_) return;
    const View* v = shownViewOf(object);
    const Object* o = v ? v->object(object) : nullptr;
    if (!o || (o->kind != Kind::Slider && o->kind != Kind::Knob)) return;
    const View snapshot = *v;
    const Object& obj = *snapshot.object(object);
    GestureGuard gesture(*this, where(snapshot, &obj));         // lot 13 : l'audit
    auto& st = controls_[object];
    double mn = 0, mx = 100, step = 1;
    {
        const auto aliases = viewAliases(snapshot.id);
        if (!numberProp(obj, "min", mn)) mn = 0;
        if (!numberProp(obj, "max", mx)) mx = 100;
        if (!numberProp(obj, "step", step)) step = 1;
    }
    const double value = snapToStep(mn + std::clamp(fraction, 0.0, 1.0) * (mx - mn), mn, mx, step);
    if (!commit) {
        // Refuse d'emblee sans les droits : la poignee ne suit pas.
        std::string why;
        if (!objectAllowed(obj, &why) || (project_ && project_->security.enabled && !permitted("Piloter"))) {
            if (!st.drag) (void)writeCommand(snapshot, obj, sim::Value::real(value), now);   // dit le refus, une fois
            st.drag = std::nullopt;
            return;
        }
        st.drag = value;
        // Lot 13 : a signature, rien n'est ecrit en glissant - au relachement, signe.
        if (obj.flag("continuous", false) && !signatureNeeded(snapshot, obj))
            (void)writeCommand(snapshot, obj, sim::Value::real(value), now, {}, false);
        return;
    }
    if (signatureNeeded(snapshot, obj)) {
        st.drag = value;             // la poignee reste la ou on l'a lachee, le temps de signer
        requestSignature(snapshot, obj, SignatureRequest::Gesture::Drag, now, {}, fraction);
        return;
    }
    st.drag.reset();
    (void)writeCommand(snapshot, obj, sim::Value::real(value), now, formatNumber(value));
}

std::optional<double> Runtime::dragPreview(Id object) const {
    const auto it = controls_.find(object);
    return it == controls_.end() ? std::nullopt : it->second.drag;
}

// ============================================================== lectures ===
std::size_t Runtime::listFirst(Id object) const {
    const auto it = controls_.find(object);
    return it == controls_.end() ? 0 : it->second.comboFirst;
}

namespace {
// Une valeur simple, telle qu'on la lit : un texte sans apostrophes, un nombre, TRUE.
std::string shownOf(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }
std::string shownOf(const sim::Obj& o) {
    if (o.type && o.type->kind == sim::TypeDesc::Kind::Scalar) return shownOf(o.value);
    return sim::display(o);
}
std::string literalOf(const sim::Obj& o) {
    std::string t;
    return sim::literalText(o, t) ? t : sim::display(o);
}
std::string quotedText(const std::string& t) {
    std::string out = "'";
    for (const char c : t) out += c == '\'' ? std::string("$'") : std::string(1, c);
    return out + "'";
}
} // namespace

bool Runtime::resolveChoices(std::string_view source, std::vector<Choice>& out) {
    if (!project_) return false;
    const std::string src = trimmedText(source);
    if (src.empty()) return false;
    // Une enumeration du projet : ses textes ; la valeur ecrite, son nombre.
    if (const auto* e = findEnumeration(*project_, src)) {
        for (const auto& v : e->values) out.push_back({v.text.empty() ? v.name : v.text, std::to_string(v.value), false});
        return true;
    }
    // Une variable objet : une liste, un vecteur, un tableau (ses elements), une MAP (ses cles).
    if (auto o = richVariable(src); o && o->type) {
        if (o->type->kind == sim::TypeDesc::Kind::Map) {
            for (const auto& [k, v] : o->textKeys) out.push_back({k, quotedText(k), false});
            for (const auto& [k, v] : o->intKeys) out.push_back({std::to_string(k), std::to_string(k), false});
            return true;
        }
        for (const auto& item : o->items)
            if (item) out.push_back({shownOf(*item), literalOf(*item), false});
        return true;
    }
    // Un tableau IHM (deplie en cases) : ses cases, dans l'ordre.
    if (const auto* a = aggregate(src); a && a->array && types::isElementary(a->spec.element)) {
        const auto& sp = a->spec;
        const auto add = [&](const std::string& path) {
            if (const auto* v = variable(path))
                out.push_back({shownOf(*v), v->type() == sim::Type::String ? quotedText(v->asString()) : v->display(), false});
        };
        for (long long i = sp.low[0]; i <= sp.high[0]; ++i) {
            if (sp.dims == 1) add(src + "[" + std::to_string(i) + "]");
            else
                for (long long j = sp.low[1]; j <= sp.high[1]; ++j) add(src + "[" + std::to_string(i) + "," + std::to_string(j) + "]");
        }
        return true;
    }
    // Une expression qui rend un texte a;b;c (une STRING de l'IHM, JOIN(L, ';')...) : ses morceaux, leur rang.
    bool ok = false;
    const std::string text = evalText(src, &ok);
    if (!ok) return false;
    std::size_t k = 0;
    for (auto& item : listItems(text)) out.push_back({std::move(item), std::to_string(k++), true});
    return true;
}

bool Runtime::tableRows(const Object& o, std::vector<std::string>& headers, std::vector<std::vector<std::string>>& rows) const {
    std::string src = trimmedText(markers::strip(o.text("rowsFrom")));
    if (src.empty() || !project_) return false;
    // Les cellules d'un element : une valeur seule, ou les membres d'un tuple, d'une structure.
    const auto cellsOf = [&](const sim::Obj& item, bool first) {
        std::vector<std::string> row;
        const bool composite = item.type && (item.type->kind == sim::TypeDesc::Kind::Tuple || item.type->kind == sim::TypeDesc::Kind::Struct);
        if (composite) {
            for (std::size_t m = 0; m < item.items.size(); ++m) {
                row.push_back(item.items[m] ? shownOf(*item.items[m]) : std::string{});
                if (first && m < item.type->members.size()) headers.push_back(item.type->members[m].first);
            }
        } else {
            row.push_back(shownOf(item));
            if (first) headers.push_back("Valeur");
        }
        return row;
    };
    if (auto obj = richVariable(src); obj && obj->type) {
        if (obj->type->kind == sim::TypeDesc::Kind::Map) {
            headers = {"Cl\xC3\xA9", "Valeur"};
            for (const auto& [k, v] : obj->textKeys) rows.push_back({k, v ? shownOf(*v) : std::string{}});
            for (const auto& [k, v] : obj->intKeys) rows.push_back({std::to_string(k), v ? shownOf(*v) : std::string{}});
            return true;
        }
        bool first = true;
        for (const auto& item : obj->items) {
            if (!item) continue;
            rows.push_back(cellsOf(*item, first));
            first = false;
        }
        if (headers.empty()) headers.push_back("Valeur");
        return true;
    }
    // Un tableau IHM (deplie en cases) : une ligne par case ; une structure : une colonne par membre.
    const auto* a = aggregate(src);
    if (!a || !a->array) return false;
    const auto& sp = a->spec;
    const auto members = types::isElementary(sp.element) ? std::vector<TypeMember>{} : types::membersOf(*project_, sp.element);
    if (members.empty()) headers.push_back("Valeur");
    for (const auto& m : members) headers.push_back(m.name);
    for (long long i = sp.low[0]; i <= sp.high[0]; ++i) {
        const std::string cell = src + "[" + std::to_string(i) + "]";
        std::vector<std::string> row;
        if (members.empty()) {
            const auto* v = variable(cell);
            row.push_back(v ? shownOf(*v) : std::string{});
        } else {
            for (const auto& m : members) {
                const auto* v = variable(cell + "." + m.name);
                row.push_back(v ? shownOf(*v) : std::string{});
            }
        }
        rows.push_back(std::move(row));
    }
    return true;
}

bool Runtime::comboOpen(Id object, std::size_t* first) const {
    const auto it = controls_.find(object);
    if (it == controls_.end() || !it->second.comboOpen) return false;
    if (first) *first = it->second.comboFirst;
    return true;
}

const WeekSchedule* Runtime::schedule(Id object) const {
    const auto it = controls_.find(object);
    return it == controls_.end() || !it->second.schedule ? nullptr : &*it->second.schedule;
}

DateTime Runtime::pickerValue(Id object, bool* pending) const {
    const auto it = controls_.find(object);
    if (pending) *pending = it != controls_.end() && it->second.picker.has_value();
    if (it != controls_.end()) {
        if (it->second.picker) return *it->second.picker;
        if (it->second.current) return *it->second.current;
    }
    DateTime d = dateTimeFromEpoch(epochOf(now_));
    d.second = 0;
    return d;
}

double Runtime::hourMeterSeconds(Id object) const {
    const auto it = controls_.find(object);
    return it == controls_.end() ? 0.0 : it->second.runSeconds;
}

bool Runtime::hourMeterRunning(Id object) const {
    const auto it = controls_.find(object);
    return it != controls_.end() && it->second.running;
}

int Runtime::trendArrow(Id object) const {
    const auto it = controls_.find(object);
    return it == controls_.end() ? 0 : it->second.trend;
}

const WeekSchedule& Runtime::scheduleOf(const Object& o, ControlState& st) {
    const int res = resolutionMinutes(o.text("resolution", "30 min"));
    const std::string var = trimmedText(markers::strip(o.text("variable")));
    std::string text;
    bool fromVariable = false;
    if (!var.empty()) {
        sim::Value cur;
        if (environment().read(var, cur) && cur.type() == sim::Type::String) {
            text = cur.asString();
            fromVariable = !trimmedText(text).empty();
        }
    }
    if (!st.schedule || st.schedule->slotMinutes != res || (fromVariable && text != st.scheduleText)) {
        WeekSchedule ws;
        if (!parseSchedule(fromVariable ? text : o.text("schedule"), res, ws)) ws = emptySchedule(res);
        st.schedule = std::move(ws);
        st.scheduleText = text;
    }
    return *st.schedule;
}

// ================================================================== cycle ===
void Runtime::controlsCycle(double now) {
    if (!project_) return;
    const double epoch = epochOf(now);
    const DateTime clock = dateTimeFromEpoch(epoch);
    const int weekday = weekdayFromEpoch(epoch);
    for (const auto& view : project_->views) {
        for (const auto& o : view.objects) {
            switch (o.kind) {
                case Kind::HourMeter: {
                    auto& st = controls_[o.id];
                    const double dt = st.lastTick < 0 ? 0.0 : std::max(0.0, now - st.lastTick);
                    st.lastTick = now;
                    const auto aliases = viewAliases(view.id);
                    const std::string cond = trimmedText(o.text("condition"));
                    const bool on = cond.empty() || evalBool(cond, false);
                    st.running = on;
                    const std::string var = trimmedText(markers::strip(o.text("variable")));
                    sim::Value cur;
                    if (!var.empty() && environment().read(var, cur)) {
                        // Remise a zero (ou autre ecriture) par un script : on repart de la variable.
                        const double inVar = counterSeconds(cur);
                        if (st.lastWritten < 0 || std::fabs(inVar - st.lastWritten) > 1.0) st.runSeconds = inVar;
                        if (on && dt > 0) {
                            st.runSeconds += dt;
                            const sim::Value next = counterValue(cur.type(), st.runSeconds);
                            if (write(var, next, where(view, &o))) st.lastWritten = counterSeconds(next);
                        } else {
                            st.lastWritten = inVar;
                        }
                    } else if (on) {
                        st.runSeconds += dt;
                    }
                    break;
                }
                case Kind::TrendArrow: {
                    auto& st = controls_[o.id];
                    const auto aliases = viewAliases(view.id);
                    double value = 0;
                    bool ok = false;
                    const auto* p = o.find("value");
                    const std::string var = trimmedText(markers::strip(o.text("variable")));
                    if (p && !p->expr.empty()) ok = numberProp(o, "value", value);
                    else if (!var.empty()) ok = parseNumber(evalText(var, &ok), value) && ok;
                    else ok = numberProp(o, "value", value);
                    if (!ok) break;
                    st.samples.emplace_back(now, value);
                    const double window = std::clamp(o.number("window", 10), 0.5, 3600.0);
                    while (!st.samples.empty() && st.samples.front().first < now - window) st.samples.pop_front();
                    const double dead = std::max(0.0, o.number("deadband", 0.5));
                    const double diff = st.samples.size() < 2 ? 0.0 : st.samples.back().second - st.samples.front().second;
                    st.trend = diff > dead ? 1 : diff < -dead ? -1 : 0;
                    break;
                }
                case Kind::WeeklySchedule: {
                    auto& st = controls_[o.id];
                    const auto& ws = scheduleOf(o, st);
                    const std::string out = trimmedText(o.text("output"));
                    if (out.empty()) break;
                    const bool on = scheduleOn(ws, weekday, clock.hour * 60 + clock.minute);
                    if (st.output == (on ? 1 : 0)) break;
                    st.output = on ? 1 : 0;
                    if (write(out, sim::Value::boolean(on), where(view, &o)))
                        log("Action", where(view, &o), out + " = " + (on ? "TRUE" : "FALSE") + " (programmateur)");
                    break;
                }
                case Kind::DateTimePicker: {
                    auto& st = controls_[o.id];
                    const auto aliases = viewAliases(view.id);
                    const std::string var = trimmedText(markers::strip(o.text("variable")));
                    sim::Value cur;
                    if (var.empty() || !environment().read(var, cur)) break;
                    DateTime d = st.current.value_or(clock);
                    if (cur.type() == sim::Type::String) {
                        if (!parseDateTime(cur.asString(), d)) break;
                    } else if (cur.type() == sim::Type::Time) {
                        const auto s = cur.asInteger() / 1000;
                        d.hour = static_cast<int>((s / 3600) % 24);
                        d.minute = static_cast<int>((s / 60) % 60);
                        d.second = static_cast<int>(s % 60);
                    } else if (cur.type() == sim::Type::Real) {
                        d = dateTimeFromEpoch(cur.asReal());
                    } else {
                        if (cur.asInteger() <= 0) break;
                        d = dateTimeFromEpoch(static_cast<double>(cur.asInteger()));
                    }
                    st.current = d;
                    break;
                }
                default:
                    break;
            }
        }
    }
}

} // namespace hmi
