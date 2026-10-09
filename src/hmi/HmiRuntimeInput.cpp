// =============================================================================
//  hmi/HmiRuntimeInput.cpp - la souris, le clavier et les raccourcis des vues  1.11.23
// -----------------------------------------------------------------------------
//  Le canevas qui montre l'IHM en marche (la simulation de l'editeur, le poste
//  d'exploitation) dit au moteur ou est la souris et ce que fait le clavier :
//  SYS.Mouse*, SYS.Key*, SYS.Key.<touche> les lisent. Une touche enfoncee cherche
//  son raccourci (une action de la vue a declencheur Touche ...) : les popups du
//  dessus vers le dessous, puis la vue ; la premiere qui en a un pour cette touche
//  (memes Ctrl, Maj, Alt) la prend. Voir HmiRuntime.hpp (la souris et le clavier).
// =============================================================================
#include "HmiRuntime.hpp"
#include "HmiPublicVars.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

namespace hmi {

namespace {

bool keyAction(const Action& a, const keys::Chord& c) { return triggerIsKey(a.trigger) && keys::matches(a.key, c); }

bool hasKeyAction(const View& v, const keys::Chord& c) {
    return std::any_of(v.actions.begin(), v.actions.end(), [&c](const Action& a) { return keyAction(a, c); });
}

double seconds(int ms, int fallback) { return std::max(10, ms > 0 ? ms : fallback) / 1000.0; }

} // namespace

void Runtime::resetInput() {
    keysDown_.clear();
    pointer_ = Pointer{};
    keyCtrl_ = keyShift_ = keyAlt_ = false;
    keyLast_.clear();
    keyLastKey_.clear();
    shortcutLast_.clear();
    keyLastSince_ = -1;
    keyPresses_ = shortcutCount_ = 0;
    keyScopeView_ = kNoId;
    keyScope_.reset();
}

// ------------------------------------------------------------------ la souris ----
void Runtime::pointerMoved(Id view, double x, double y, Id object, bool inside) {
    const View* v = inside && view != kNoId ? viewOf(view) : nullptr;
    pointer_.view = v ? view : kNoId;
    pointer_.viewName = v ? v->name : std::string{};
    const Object* o = v && object != kNoId ? v->object(object) : nullptr;
    pointer_.objectName = o ? v->name + "." + o->name : std::string{};
    pointer_.x = x;
    pointer_.y = y;
    pointer_.inside = inside;
    if (!inside) pointer_.buttons = 0;      // un bouton relache dehors ne revient jamais
}

void Runtime::pointerButton(int button, bool down, double now) {
    const std::uint8_t bit = button == 0 ? 1 : button == 1 ? 2 : button == 2 ? 4 : 0;
    if (!bit) return;
    if (down) {
        pointer_.buttons = static_cast<std::uint8_t>(pointer_.buttons | bit);
        lastActivity_ = std::max(lastActivity_, now);
    } else {
        pointer_.buttons = static_cast<std::uint8_t>(pointer_.buttons & ~bit);
    }
}

void Runtime::pointerWheel(double notches) {
    pointer_.wheel += static_cast<std::int64_t>(std::llround(notches));
}

// ------------------------------------------------------------------ le clavier ---
void Runtime::keyModifiers(bool ctrl, bool shift, bool alt) {
    keyCtrl_ = ctrl;
    keyShift_ = shift;
    keyAlt_ = alt;
}

bool Runtime::keyHeld(std::string_view key) const {
    const auto token = keys::tokenOf(key);
    return !token.empty() && keysDown_.find(token) != keysDown_.end();
}

Id Runtime::keyOwner(const keys::Chord& c) const {
    if (!running_ || c.key.empty()) return kNoId;
    const bool plainEscape = c.key == "Escape" && !c.ctrl && !c.shift && !c.alt;
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it) {
        const View* v = viewOf(*it);
        if (!v) continue;
        if (hasKeyAction(*v, c)) return *it;
        // Une popup modale sans ce raccourci arrete la recherche (le dessous ne repond plus) ;
        // Echap seul, une popup ouverte : la sienne, sinon elle se ferme (comme avant).
        if (popupSettingsOf(*v).modal || plainEscape) return kNoId;
    }
    const View* v = viewOf(current_);
    return v && hasKeyAction(*v, c) ? current_ : kNoId;
}

bool Runtime::keyDown(const keys::Chord& chord, double now, bool repeat, bool typing) {
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    keyModifiers(chord.ctrl, chord.shift, chord.alt);
    if (chord.key.empty()) return false;                    // Ctrl, Maj ou Alt seuls
    if (const auto it = keysDown_.find(chord.key); it != keysDown_.end())
        return it->second.owner != kNoId;                   // tenue : la repetition du systeme ne compte pas
    const bool wasAsleep = running_ && asleep(now);         // la touche reveille l'ecran, sans rien de plus
    HeldKey h;
    h.chord = chord;
    h.since = now;
    ++keyPresses_;
    keyLast_ = keys::label(chord);
    keyLastKey_ = chord.key;
    keyLastSince_ = now;
    lastActivity_ = std::max(lastActivity_, now);
    // Suspendus : l'IHM arretee, un champ de saisie qui a le clavier (sauf F1..F12), un menu
    // natif, la signature, le clavier d'une action, le lecteur de badge (ses chiffres, Entree).
    const bool bare = !chord.ctrl && !chord.alt;
    const bool badgeKey = bare && (chord.key == "Enter" || chord.key.rfind("Digit", 0) == 0);
    const bool suspended = !running_ || repeat || wasAsleep
                        || ((typing || focused_ != kNoId) && !keys::isFunctionKey(chord.key))
                        || loginShown() || systemShown() || signatureShown() || promptShown()
                        || (badgeKey && badgeListening());
    h.owner = suspended ? kNoId : keyOwner(chord);
    if (h.owner != kNoId) {
        if (const View* v = viewOf(h.owner)) {
            h.snapshot = std::make_shared<const View>(*v);
            h.scope = scopePtr(h.owner);
            for (std::size_t i = 0; i < v->actions.size(); ++i) {
                const auto& a = v->actions[i];
                if (a.trigger == Trigger::KeyRepeat && keys::matches(a.key, chord))
                    h.next[i] = now + seconds(a.delayMs, keys::kRepeatDefaultMs);
            }
        } else {
            h.owner = kNoId;
        }
    }
    const Id owner = h.owner;
    const auto snapshot = h.snapshot;
    keysDown_[chord.key] = std::move(h);
    if (owner == kNoId) return wasAsleep;                   // personne : l'hote garde ses raccourcis
    runKeyActions(*snapshot, Trigger::KeyPress, chord, now);
    return true;
}

bool Runtime::keyUp(std::string_view key, double now) {
    composed_.clear(); boundCalls_.clear();
    now_ = std::max(now_, now);
    const auto it = keysDown_.find(key);
    if (it == keysDown_.end()) return false;
    HeldKey h = std::move(it->second);
    keysDown_.erase(it);
    if (h.owner == kNoId || !h.snapshot) return false;
    if (!running_) return true;
    // Le front descendant part dans la vue qui a pris la touche, telle qu'elle est si elle
    // est encore montree, sinon telle qu'a l'appui - avec ses parametres d'alors (une
    // popup fermee entre-temps : un « Mettre a 0 » au relachement n'est jamais perdu).
    const View* live = viewOpen(h.owner) ? viewOf(h.owner) : nullptr;
    const View snapshot = live ? *live : *h.snapshot;
    keyScopeView_ = h.owner;
    keyScope_ = h.scope;
    runKeyActions(snapshot, Trigger::KeyRelease, h.chord, now);
    keyScopeView_ = kNoId;
    keyScope_.reset();
    return true;
}

void Runtime::releaseAllKeys(double now) {
    std::vector<std::string> held;
    held.reserve(keysDown_.size());
    for (const auto& [k, h] : keysDown_) held.push_back(k);
    for (const auto& k : held) (void)keyUp(k, now);
    keyModifiers(false, false, false);
}

void Runtime::keysTick(double now) {
    if (keysDown_.empty() || !running_) return;
    std::vector<std::string> owned;
    for (const auto& [k, h] : keysDown_)
        if (h.owner != kNoId) owned.push_back(k);
    for (const auto& k : owned) {
        const auto it = keysDown_.find(k);
        if (it == keysDown_.end() || !viewOpen(it->second.owner)) continue;   // maintenue, repetee : sa vue montree
        const View* v = viewOf(it->second.owner);
        if (!v) continue;
        const View snapshot = *v;
        const keys::Chord chord = it->second.chord;
        std::vector<std::size_t> holds, repeats;
        for (std::size_t i = 0; i < snapshot.actions.size(); ++i) {
            const auto& a = snapshot.actions[i];
            if (!keys::matches(a.key, chord)) continue;
            if (a.trigger == Trigger::KeyHold && !it->second.held.count(i)
                && now - it->second.since >= seconds(a.delayMs, keys::kHoldDefaultMs) - 1e-9) {
                it->second.held.insert(i);
                holds.push_back(i);
            }
            if (a.trigger == Trigger::KeyRepeat) {
                const double period = seconds(a.delayMs, keys::kRepeatDefaultMs);
                auto [at, fresh] = it->second.next.try_emplace(i, it->second.since + period);
                (void)fresh;
                int n = 0;
                while (at->second <= now + 1e-9 && n < 10) {
                    ++n;
                    at->second += period;
                    repeats.push_back(i);
                }
                if (at->second <= now) at->second = now + period;   // le retard au-dela de dix : oublie
            }
        }
        for (const auto i : holds)
            runKeyActions(snapshot, Trigger::KeyHold, chord, now, [i](std::size_t j) { return j == i; });
        for (const auto i : repeats)
            runKeyActions(snapshot, Trigger::KeyRepeat, chord, now, [i](std::size_t j) { return j == i; });
    }
}

void Runtime::runKeyActions(const View& v, Trigger t, const keys::Chord& chord, double now,
                            const std::function<bool(std::size_t)>& pick) {
    // Une copie : une action peut changer de vue (Naviguer, fermer la popup).
    const std::vector<Action> copy = v.actions;
    const auto origins = actionOriginNames(v);
    std::optional<GestureGuard> gesture;                    // lot 13 : l'audit, un geste de l'operateur
    for (std::size_t i = 0; i < copy.size(); ++i) {
        const auto& a = copy[i];
        if (a.trigger != t || !keys::matches(a.key, chord) || (pick && !pick(i))) continue;
        if (!gesture) {
            gesture.emplace(*this, where(v, nullptr));
            ++shortcutCount_;
            shortcutLast_ = v.name + " \xC2\xB7 " + keys::label(chord);
            log("Raccourci", where(v, nullptr), keys::label(chord) + " (" + std::string(triggerLabel(t)) + ")");
        }
        actionOrigin_ = i < origins.size() ? origins[i] : std::string{};
        fire(v, nullptr, a, now, /*byUser*/ true);
    }
    actionOrigin_.clear();
}

// ------------------------------------------------------- les variables systeme ---
bool Runtime::inputSysValue(std::string_view n, sim::Value& out) const {
    const auto text = [&](std::string s) { out = sim::Value::text(std::move(s)); return true; };
    const auto flag = [&](bool b) { out = sim::Value::boolean(b); return true; };
    const auto integer = [&](long long v) { out = sim::Value::integer(sim::Type::Int, v); return true; };
    const auto dint = [&](long long v) { out = sim::Value::integer(sim::Type::DInt, v); return true; };
    const auto real = [&](double v) { out = sim::Value::real(v); return true; };
    const auto duration = [&](double s) {
        out = sim::Value::time(static_cast<std::int64_t>(std::llround(std::max(0.0, s) * 1000.0)));
        return true;
    };
    using pub::same;
    if (same(n, "MouseX")) return real(pointer_.x);
    if (same(n, "MouseY")) return real(pointer_.y);
    if (same(n, "MouseView")) return text(pointer_.viewName);
    if (same(n, "MouseObject")) return text(pointer_.objectName);
    if (same(n, "MouseInside")) return flag(pointer_.inside);
    if (same(n, "MouseLeft")) return flag((pointer_.buttons & 1) != 0);
    if (same(n, "MouseRight")) return flag((pointer_.buttons & 2) != 0);
    if (same(n, "MouseMiddle")) return flag((pointer_.buttons & 4) != 0);
    if (same(n, "MouseButtons")) return integer(pointer_.buttons);
    if (same(n, "MouseWheel")) return dint(pointer_.wheel);
    if (same(n, "KeyLast")) return text(keyLast_);
    if (same(n, "KeysDown")) {
        std::string all;
        for (const auto& [k, h] : keysDown_) {
            const auto* name = keys::keyOf(k);
            all += (all.empty() ? "" : ";") + (name ? std::string(name->label) : k);
        }
        return text(all);
    }
    if (same(n, "KeyDownCount")) return integer(static_cast<long long>(keysDown_.size()));
    if (same(n, "KeyAnyDown")) return flag(!keysDown_.empty());
    if (same(n, "KeyCtrl")) return flag(keyCtrl_);
    if (same(n, "KeyShift")) return flag(keyShift_);
    if (same(n, "KeyAlt")) return flag(keyAlt_);
    if (same(n, "KeyHoldTime")) {
        const auto it = keysDown_.find(keyLastKey_);
        return duration(it == keysDown_.end() ? 0.0 : now_ - it->second.since);
    }
    if (same(n, "KeyPresses")) return dint(keyPresses_);
    if (same(n, "ShortcutLast")) return text(shortcutLast_);
    if (same(n, "ShortcutCount")) return dint(shortcutCount_);
    return false;
}

bool Runtime::keySysValue(std::string_view key, sim::Value& out) const {
    if (key.empty() || std::isdigit(static_cast<unsigned char>(key.front()))) return false;   // SYS.Key.1 : un bit
    const auto token = keys::tokenOf(key);
    if (token.empty()) return false;
    out = sim::Value::boolean(keysDown_.find(token) != keysDown_.end());
    return true;
}

} // namespace hmi
