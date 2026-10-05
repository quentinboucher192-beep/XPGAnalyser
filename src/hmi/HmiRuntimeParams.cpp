// =============================================================================
//  hmi/HmiRuntimeParams.cpp - 1.9 : les copies des parametres des popups
// -----------------------------------------------------------------------------
//  A L'OUVERTURE d'une popup, chaque parametre en mode Copie ou Les deux est
//  CAPTURE : sa valeur (un scalaire) ou tous ses membres (une structure IHM,
//  un DDT, un tableau), a plat, dans la popup ouverte (PopupSlot::copies). Le
//  nom du parametre designe alors un nom interne ("$COPIE3") : les lectures
//  et les ecritures de la popup (expressions, textes, champs, Affecter,
//  scripts) passent par copyRead / copyWrite et ne touchent jamais l'original
//  ni l'automate. APPLIQUER COPIE SUR REFERENCE (mode Les deux) ecrit dans la
//  variable de l'appelant les seuls membres modifies depuis la capture : ce
//  que l'automate a change entre-temps sur les autres membres reste. Fermer
//  la popup oublie la copie.
// =============================================================================
#include "HmiRuntime.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>

namespace hmi {

namespace {

std::string upperText(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trimText(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
constexpr std::size_t kMaxCopyLeaves = 20000;   // une copie raisonnable (un tableau de structures)

// "$COPIE3.Marche" -> ("$COPIE3", ".MARCHE")
std::pair<std::string, std::string> splitCopyPath(std::string_view path) {
    const auto cut = path.find_first_of(".[");
    if (cut == std::string_view::npos) return {upperText(path), {}};
    return {upperText(path.substr(0, cut)), upperText(path.substr(cut))};
}

} // namespace

void Runtime::captureCopies(PopupSlot& slot, const View& v, std::string_view /*arguments*/) {
    slot.copies.clear();
    const bool any = std::any_of(v.params.begin(), v.params.end(), [](const ViewParam& p) { return p.mode != ParamMode::Reference; });
    if (!any) return;
    // La portee de la popup : les parametres Reference tels quels, ceux en
    // Copie / Les deux designent leur copie.
    auto scope = std::make_shared<Scope>();
    if (slot.scope)
        for (const auto& n : slot.scope->names()) {
            const auto* prm = v.param(n);
            if (prm && prm->mode != ParamMode::Reference) continue;
            if (const auto* a = slot.scope->alias(n)) scope->setAlias(n, *a);
            else if (const auto* val = slot.scope->value(n)) scope->setValue(n, *val);
        }
    for (const auto& prm : v.params) {
        if (prm.mode == ParamMode::Reference) continue;
        ParamCopy c;
        c.name = prm.name;
        c.mode = prm.mode;
        c.root = "$COPIE" + std::to_string(++copySeq_);
        const std::string* alias = slot.scope ? slot.scope->alias(prm.name) : nullptr;
        const sim::Value* value = slot.scope ? slot.scope->value(prm.name) : nullptr;
        if (alias) {
            c.source = *alias;
            std::string type = trimText(prm.type);
            if (type.empty() || upperText(type) == "ANY")
                type = project_ ? params::expressionType(*project_, nullptr, c.source, plcTypes_) : std::string{};
            std::size_t leaves = 0;
            // Chaque feuille : lue maintenant, gardee sous son chemin relatif.
            const auto collect = [&](auto&& self, const std::string& t, const std::string& rel, int depth) -> void {
                if (leaves >= kMaxCopyLeaves || depth > 16) return;
                if (project_ && !t.empty()) {
                    const auto members = params::typeMembers(*project_, t, plcTypes_);
                    if (!members.empty()) {
                        for (const auto& [name, mt] : members) self(self, mt, rel + "." + name, depth + 1);
                        return;
                    }
                    types::Spec spec;
                    if (types::parseSpec(t, spec) && spec.array()) {
                        const std::string element = spec.element;
                        if (spec.dims == 1) {
                            for (long long i = spec.low[0]; i <= spec.high[0]; ++i)
                                self(self, element, rel + "[" + std::to_string(i) + "]", depth + 1);
                        } else {
                            for (long long i = spec.low[0]; i <= spec.high[0]; ++i)
                                for (long long j = spec.low[1]; j <= spec.high[1]; ++j)
                                    self(self, element, rel + "[" + std::to_string(i) + "," + std::to_string(j) + "]", depth + 1);
                        }
                        return;
                    }
                }
                sim::Value out;
                if (readDirect(c.source + rel, out)) {
                    c.values[upperText(rel)] = out;
                    c.paths[upperText(rel)] = rel;
                    ++leaves;
                }
            };
            collect(collect, type, std::string{}, 0);
            if (c.values.empty())
                log("Erreur", source_, "param\xC3\xA8tre " + prm.name + " : copie de " + c.source
                                           + " impossible (variable ou type inconnu)");
        } else if (value) {
            c.values[std::string{}] = *value;   // une valeur donnee ('P3', 3) : la copie la garde
        }
        c.captured = c.values;
        scope->setAlias(prm.name, c.root);
        slot.copies.push_back(std::move(c));
    }
    slot.scope = scope;
}

bool Runtime::copyRead(std::string_view path, sim::Value& out) const {
    const auto [root, key] = splitCopyPath(path);
    const auto look = [&](const std::vector<ParamCopy>& copies) -> int {
        for (const auto& c : copies) {
            if (c.root != root) continue;
            const auto it = c.values.find(key);
            if (it == c.values.end()) return 0;
            out = it->second;
            return 1;
        }
        return -1;
    };
    for (const auto& s : slots_) {
        if (const int r = look(s.copies); r >= 0) return r == 1;
        for (const auto& b : s.history)
            if (const int r = look(b.copies); r >= 0) return r == 1;
    }
    return false;
}

int Runtime::copyWrite(std::string_view path, const sim::Value& v, std::string* why) {
    const auto [root, key] = splitCopyPath(path);
    for (auto& s : slots_)
        for (auto& c : s.copies) {
            if (c.root != root) continue;
            const auto it = c.values.find(key);
            if (it == c.values.end()) {
                if (why) *why = "param\xC3\xA8tre " + c.name + " : pas de membre " + (key.empty() ? std::string("(valeur)") : key);
                return 0;
            }
            if (it->second.type() == sim::Type::Unknown) it->second = v;
            else it->second.assignFrom(v);
            return 1;
        }
    if (why) *why = "la copie du param\xC3\xA8tre n'existe plus (la popup est ferm\xC3\xA9" "e)";
    return -1;
}

const ParamCopy* Runtime::paramCopy(Id view, std::string_view name) const {
    for (const auto& s : slots_) {
        if (view != kNoId && s.view != view) continue;
        for (const auto& c : s.copies)
            if (upperText(c.name) == upperText(name)) return &c;
    }
    return nullptr;
}

int Runtime::applyCopy(std::string_view name, Id view, double now, std::string* why) {
    now_ = std::max(now_, now);
    PopupSlot* slot = nullptr;
    if (view == kNoId) {
        if (!slots_.empty()) slot = &slots_.back();
    } else {
        for (auto& s : slots_) if (s.view == view) slot = &s;
    }
    const auto fail = [&](std::string message) {
        if (why) *why = std::move(message);
        return -1;
    };
    const std::string label = "appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence";
    if (!slot) return fail(label + " : seulement dans une popup ouverte");
    const View* v = viewOf(slot->view);
    const std::string popupName = v ? v->name : std::string{};
    const std::string wanted = trimText(name);
    const bool all = wanted.empty() || wanted == "*";
    if (!all && v && !v->param(wanted)) return fail(label + " : " + popupName + " n'a pas de param\xC3\xA8tre " + wanted);
    int written = 0;
    bool found = false;
    for (auto& c : slot->copies) {
        if (!all && upperText(c.name) != upperText(wanted)) continue;
        if (c.mode != ParamMode::Both) {
            if (!all) return fail(label + " : le param\xC3\xA8tre " + c.name + " n'est pas en mode Les deux");
            continue;
        }
        found = true;
        if (c.source.empty()) {
            if (!all) return fail(label + " : " + c.name + " a re\xC3\xA7u une valeur, pas une variable");
            continue;
        }
        int members = 0;
        for (auto& [key, value] : c.values) {
            auto cap = c.captured.find(key);
            if (cap != c.captured.end() && cap->second.equals(value)) continue;   // pas modifie : on n'ecrase pas
            const auto declared = c.paths.find(key);
            if (!writeDirect(c.source + (declared != c.paths.end() ? declared->second : key), value, popupName)) continue;
            if (cap != c.captured.end()) cap->second = value;
            else c.captured[key] = value;
            ++members;
        }
        written += members;
        log("Action", popupName,
            "Appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence : " + popupName + "." + c.name + " \xE2\x86\x92 " + c.source + " ("
                + std::to_string(members) + " membre" + (members > 1 ? "s" : "") + ")");
    }
    if (!found) {
        if (all) return fail(label + " : " + popupName + " n'a aucun param\xC3\xA8tre en mode Les deux");
        return fail(label + " : le param\xC3\xA8tre " + wanted + " n'a pas de copie");
    }
    return written;
}

} // namespace hmi
