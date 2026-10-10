// =============================================================================
//  hmi/HmiRuntimeLot13.cpp - la securite renforcee en marche (lot 13)
// -----------------------------------------------------------------------------
//  LE JOURNAL D'AUDIT : les gestes de l'operateur (press, release, objectPart,
//  typeKey, dragValue, les menus natifs) ouvrent une portee (GestureGuard) ;
//  chaque variable ecrite pendant ce geste - par la commande, une action, un
//  script appele par l'action - donne une ligne "Ecriture" (avant -> apres).
//  Hors d'un geste (un script cyclique, un timer, l'automate), rien : ce n'est
//  pas l'operateur. Une recette donne UNE ligne (toutes ses valeurs). Les
//  actes (connexion, refus, verrouillage, acquittement, mise de cote, mot de
//  passe, signature) donnent chacun la leur. Chaque ligne est chainee a la
//  precedente par son empreinte (HmiHistory.hpp : appendAudit, verifyAudit).
//
//  LE VERROUILLAGE : les echecs de suite par compte (History::accounts, gardes
//  dans ihm/historique/comptes.csv : un redemarrage ne les efface pas).
//
//  LA SIGNATURE (HmiSignature.hpp) : le geste attend dans `signature_` ; signe,
//  resumeSigned() le rejoue, la signature contournee (signatureBypass_), et les
//  lignes d'audit du geste portent le signataire et le motif.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiControls.hpp"
#include "HmiCrypto.hpp"
#include "HmiDisplay.hpp"
#include "HmiLanguages.hpp"
#include "HmiPolicy.hpp"
#include "HmiSystemMenu.hpp"
#include "HmiMarkers.hpp"   // 1.11 (REP) : les reperes $...$

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hmi {

namespace {

constexpr std::size_t kRenewMin = kMenuPasswordMin;   // comme l'onglet Mon compte
constexpr std::size_t kMaxTyped = 64;

std::string upperCopy(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool sameText(std::string_view a, std::string_view b) { return upperCopy(a) == upperCopy(b); }

std::size_t prevCodepoint(const std::string& s, std::size_t at) {
    if (at == 0) return 0;
    std::size_t p = at - 1;
    while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) --p;
    return p;
}
std::size_t nextCodepoint(const std::string& s, std::size_t at) {
    if (at >= s.size()) return s.size();
    std::size_t p = at + 1;
    while (p < s.size() && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) ++p;
    return p;
}
std::size_t codepointCount(const std::string& s) {
    std::size_t n = 0;
    for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}
std::string trimmedCopy(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Un compte peut-il signer (un mot de passe ou un code a retaper) ?
bool canSign(const User& u) {
    return u.enabled && ((u.protection == "classique" && !u.passwordHash.empty()) || (u.protection == "dynamique" && !u.secret.empty()));
}

} // namespace

// ================================================================= l'audit ===
const std::vector<AuditEntry>& Runtime::auditTrail() const noexcept { return history_ ? history_->audit : auditLocal_; }
bool Runtime::auditOn() const noexcept { return project_ && project_->history.audit; }
std::vector<AuditEntry>& Runtime::auditList() { return history_ ? history_->audit : auditLocal_; }
std::vector<AccountState>& Runtime::accountList() { return history_ ? history_->accounts : accountsLocal_; }
const std::vector<AccountState>& Runtime::accountList() const { return history_ ? history_->accounts : accountsLocal_; }

void Runtime::audit(std::string kind, std::string source, std::string target, std::string before, std::string after, std::string reason) {
    if (!auditOn()) return;
    AuditEntry e;
    e.stamp = dateStampOf(now_);
    e.user = user_;
    e.kind = std::move(kind);
    e.source = !source.empty() ? std::move(source) : !gestureWhere_.empty() ? gestureWhere_ : std::string("IHM");
    e.target = std::move(target);
    e.before = std::move(before);
    e.after = std::move(after);
    e.reason = !reason.empty() ? std::move(reason) : signedReason_;
    e.signature = signedBy_;
    auto& list = auditList();
    const AuditEntry added = appendAudit(list, std::move(e));
    // Au-dela de la limite, les plus anciennes tombent (par paquets : pas a chaque ligne).
    if (list.size() > kAuditMax + 512) list.erase(list.begin(), list.begin() + static_cast<long>(list.size() - kAuditMax));
    if (hooks_.audited) hooks_.audited(added);
}

void Runtime::recordAudit(std::string kind, std::string source, std::string target, std::string before, std::string after,
                          std::string reason) {
    audit(std::move(kind), std::move(source), std::move(target), std::move(before), std::move(after), std::move(reason));
}

void Runtime::beginGesture(std::string where) {
    if (gesture_++ == 0) gestureWhere_ = std::move(where);
}

void Runtime::endGesture() {
    if (--gesture_ <= 0) {
        gesture_ = 0;
        gestureWhere_.clear();
    }
}

// 1.12.3 : hors d'un geste, un script ou une fonction qui tourne seul (demarrage,
// cycle, sur changement, evenement de vue) - si les Historiques le demandent.
bool Runtime::auditingScriptWrites() const noexcept {
    return gesture_ == 0 && !origin_.name.empty() && project_ && project_->history.auditScripts;
}

bool Runtime::auditingWrites() const noexcept {
    return (gesture_ > 0 || auditingScriptWrites()) && readOnly_ == 0 && auditMute_ == 0 && auditOn();
}

void Runtime::auditWrite(const std::string& name, const sim::Value& before, const sim::Value& after) {
    const std::string b = before.type() == sim::Type::Unknown ? std::string{} : formatValue(before);
    const std::string a = formatValue(after);
    if (b == a) return;                  // rien n'a change : rien a dire
    // 1.12.3 : la source d'une ecriture de script est le script ("script Horloge",
    // "Vue_A.OnOpen", "fonction Moyenne").
    std::string source;
    if (auditingScriptWrites()) source = !source_.empty() ? source_ : origin_.name;
    audit("\xC3\x89" "criture", std::move(source), name, b, a);
}

// ========================================================== le verrouillage ===
const AccountState* Runtime::accountState(std::string_view login) const {
    for (const auto& a : accountList()) if (sameText(a.login, login)) return &a;
    return nullptr;
}

bool Runtime::accountLocked(std::string_view login) const {
    const auto* st = accountState(login);
    return st && st->locked(epochOf(now_));
}

std::string Runtime::loginFailed(const User& u, double now) {
    if (!project_) return {};
    const auto& sec = project_->security;
    auto& list = accountList();
    AccountState* st = nullptr;
    for (auto& a : list) if (sameText(a.login, u.login)) st = &a;
    if (!st) {
        list.push_back(AccountState{u.login, 0, {}, 0});
        st = &list.back();
    }
    // Un verrou echu : on repart de zero.
    if (!st->lockedAt.empty() && !st->locked(epochOf(now))) {
        st->lockedAt.clear();
        st->lockedUntil = 0;
        st->failures = 0;
    }
    ++st->failures;
    if (sec.lockAttempts <= 0) return {};
    if (st->failures < sec.lockAttempts) {
        const int left = sec.lockAttempts - st->failures;
        return " (encore " + std::to_string(left) + (left > 1 ? " essais" : " essai") + " avant le verrouillage)";
    }
    st->lockedAt = dateStampOf(now);
    st->lockedUntil = sec.lockMinutes > 0 ? epochOf(now) + sec.lockMinutes * 60.0 : 0.0;
    const std::string until = sec.lockMinutes > 0 ? "jusqu'\xC3\xA0 " + dateStampOf(now + sec.lockMinutes * 60.0).substr(11, 5)
                                                  : std::string("jusqu'au d\xC3\xA9verrouillage par un administrateur");
    const std::string login = u.login;
    const int failures = st->failures;
    event("Compte verrouill\xC3\xA9", login, std::to_string(failures) + " \xC3\xA9" "checs de suite : verrouill\xC3\xA9 " + until);
    audit("Verrouillage", {}, login, "actif", "verrouill\xC3\xA9 " + until, std::to_string(failures) + " \xC3\xA9" "checs de suite");
    return " \xE2\x80\x94 compte verrouill\xC3\xA9 " + until;
}

void Runtime::loginSucceeded(const User& u) {
    auto& list = accountList();
    for (auto& a : list)
        if (sameText(a.login, u.login)) {
            a.failures = 0;
            a.lockedAt.clear();
            a.lockedUntil = 0;
        }
}

bool Runtime::unlockAccount(std::string_view login, double now, std::string* why, const std::string& source) {
    now_ = std::max(now_, now);
    const auto fail = [&](std::string reason) {
        if (why) *why = std::move(reason);
        return false;
    };
    if (project_ && project_->security.enabled && !permitted("Administrer")) {
        const std::string msg = "D\xC3\xA9verrouiller : permission \xC2\xAB Administrer \xC2\xBB requise ("
                              + (user_.empty() ? std::string("personne n'est connect\xC3\xA9") : "utilisateur " + user_) + ")";
        event("Acc\xC3\xA8s refus\xC3\xA9", source.empty() ? std::string("d\xC3\xA9verrouillage") : source, msg);
        return fail(msg);
    }
    AccountState* st = nullptr;
    for (auto& a : accountList()) if (sameText(a.login, login)) st = &a;
    if (!st || st->lockedAt.empty()) return fail(std::string(login) + " n'est pas verrouill\xC3\xA9");
    const std::string name = st->login;
    st->failures = 0;
    st->lockedAt.clear();
    st->lockedUntil = 0;
    event("Compte d\xC3\xA9verrouill\xC3\xA9", name, "par " + (user_.empty() ? std::string("personne") : user_));
    audit("D\xC3\xA9verrouillage", source, name, "verrouill\xC3\xA9", "actif");
    return true;
}

// ========================================================= le renouvellement ===
void Runtime::renewalSubmit(double now) {
    auto& f = loginForm_;
    if (!renewal_ || !project_) return;
    const User* u = project_->userByLogin(renewal_->login);
    if (!u || !u->enabled) {
        renewal_.reset();
        loginSay("compte introuvable ou d\xC3\xA9sactiv\xC3\xA9", true, now);
        return;
    }
    const std::string pwd = f.text["nouveau"], confirm = f.text["confirmation"];
    std::string problem = passwordProblem(project_->security, u, pwd, kRenewMin);
    if (problem.empty() && passwordMatches(u->salt, u->passwordHash, pwd)) problem = "le nouveau doit \xC3\xAAtre diff\xC3\xA9rent de l'ancien";
    if (problem.empty() && pwd != confirm) problem = "la confirmation ne correspond pas";
    if (!problem.empty()) {
        f.text["confirmation"].clear();
        f.caret["confirmation"] = 0;
        f.focus = "nouveau";
        f.caret["nouveau"] = f.text["nouveau"].size();
        loginSay(problem, true, now);
        return;
    }
    if (!hooks_.userRequest) {
        loginSay("l'\xC3\xA9" "cran ne sait pas enregistrer le mot de passe ici", true, now);
        return;
    }
    UserRequest rq;
    rq.op = "renouveler";
    rq.user = u->id;
    rq.login = u->login;
    rq.salt = randomHex(16);
    rq.hash = passwordHash(rq.salt, pwd);
    rq.source = "Menu de connexion";
    const std::string who = u->login;
    const Id id = u->id;
    for (auto& [name, text] : f.text) text.clear();
    for (auto& [name, at] : f.caret) at = 0;
    hooks_.userRequest(rq);                  // le projet change : `u` ne vaut plus
    event("Mot de passe renouvel\xC3\xA9", who, "par " + who + " (menu de connexion)");
    audit("Mot de passe", "Menu de connexion", who, {}, "renouvel\xC3\xA9 par " + who);
    renewal_.reset();
    const User* fresh = project_->user(id);
    if (!fresh) {
        loginSay("compte introuvable", true, now);
        return;
    }
    completeLogin(*fresh, now, "Menu de connexion (renouvellement)");
    f.focus.clear();
    f.chosen.clear();
    loginKeyboard_ = false;
    if (loginWanted_) {
        const auto tabs = loginTabs();
        if (std::find(tabs.begin(), tabs.end(), *loginWanted_) != tabs.end()) loginTab_ = *loginWanted_;
        loginWanted_.reset();
    }
    const User* me = user();
    loginSay("Mot de passe renouvel\xC3\xA9. Bienvenue, " + (me && !me->fullName.empty() ? me->fullName : who), false, now);
}

// =============================================================== la signature ===
bool Runtime::signatureNeeded(const View& v, const Object& o) {
    (void)v;
    if (signatureBypass_ || !project_ || !kindSignable(o.kind) || signatureMode(o) == "aucune") return false;
    if (o.kind == Kind::IlluminatedButton && o.text("operation", "basculer") == "impulsion") return false;
    // Un geste qui sera refuse de toute facon (niveau, permission) : pas de
    // signature a demander, le chemin ordinaire dit pourquoi.
    if (!objectAllowed(o)) return false;
    if (project_->security.enabled && (kindWritesVariable(o.kind) || o.kind == Kind::InputField) && !permitted("Piloter")) return false;
    return true;
}

void Runtime::requestSignature(const View& v, const Object& o, SignatureRequest::Gesture gesture, double now, std::string part,
                               double fraction, std::string text) {
    const auto aliases = viewAliases(v.id);
    SignatureRequest rq;
    rq.gesture = gesture;
    rq.view = v.id;
    rq.object = o.id;
    rq.part = std::move(part);
    rq.fraction = fraction;
    rq.text = std::move(text);
    rq.source = where(v, &o);
    rq.twoSigners = signatureMode(o) == "double";
    rq.reasons = signatureReasons(o);
    rq.visaLevel = static_cast<int>(std::clamp(o.number("signatureLevel", 3), 0.0, 99.0));
    rq.since = now;
    // Ce qui est signe, dit en clair : la variable et sa valeur a venir.
    const std::string var = trimmedCopy(markers::strip(o.text("variable")));
    sim::Value cur;
    const bool known = !var.empty() && environment().read(var, cur);
    const std::string label = trimmedCopy(o.text("text"));
    std::string what;
    switch (gesture) {
        case SignatureRequest::Gesture::Click:
            if (o.kind == Kind::Button) {
                what = (label.empty() ? o.name : label) + " (ses actions au clic)";
            } else if (o.kind == Kind::IlluminatedButton && o.text("operation", "basculer") != "basculer") {
                what = var + " \xE2\x86\x92 " + (o.text("operation") == "mettre \xC3\xA0 1" ? "VRAI" : "FAUX");
            } else {
                const bool on = known && cur.isTruthy();
                what = var + " : " + (on ? "VRAI" : "FAUX") + " \xE2\x86\x92 " + (on ? "FAUX" : "VRAI");
            }
            break;
        case SignatureRequest::Gesture::Part: {
            std::string shown = rq.part;
            const auto choices = choicesOf(o);
            const auto indexAfter = [&](std::string_view prefix) -> int {
                if (rq.part.rfind(prefix, 0) != 0) return -1;
                return std::atoi(rq.part.c_str() + prefix.size());
            };
            int i = -1;
            if (o.kind == Kind::Selector) {
                i = indexAfter("position:");
                if (rq.part == "suivant" && !choices.empty()) {
                    bool ok = false;
                    const std::string now2 = evalText(trimmedCopy(o.text("state")).empty() ? var : trimmedCopy(o.text("state")), &ok);
                    i = (choiceIndexOf(choices, ok ? now2 : std::string{}) + 1) % static_cast<int>(choices.size());
                }
            } else if (o.kind == Kind::RadioGroup) {
                i = indexAfter("option:");
            } else if (o.kind == Kind::ComboBox) {
                i = indexAfter("choix:");
            }
            if (i >= 0 && static_cast<std::size_t>(i) < choices.size()) shown = choices[static_cast<std::size_t>(i)].label;
            if (o.kind == Kind::DateTimePicker) {
                const auto it = controls_.find(o.id);
                const DateTime d = it != controls_.end() && it->second.picker ? *it->second.picker : pickerValue(o.id);
                shown = frenchDateTime(d, o.text("fields", "date et heure"), o.flag("seconds", false));
            }
            if (o.kind == Kind::WeeklySchedule) shown = "plages (" + rq.part + ")";
            what = (var.empty() ? o.name : var) + (known ? " : " + formatValue(cur) : std::string{}) + " \xE2\x86\x92 " + shown;
            break;
        }
        case SignatureRequest::Gesture::Drag: {
            double mn = 0, mx = 100, step = 1;
            if (!numberProp(o, "min", mn)) mn = 0;
            if (!numberProp(o, "max", mx)) mx = 100;
            if (!numberProp(o, "step", step)) step = 1;
            const double value = snapToStep(mn + std::clamp(fraction, 0.0, 1.0) * (mx - mn), mn, mx, step);
            what = var + (known ? " : " + formatValue(cur) : std::string{}) + " \xE2\x86\x92 " + formatNumber(value);
            break;
        }
        case SignatureRequest::Gesture::Input:
            what = var + (known ? " : " + formatValue(cur) : std::string{}) + " \xE2\x86\x92 " + rq.text;
            break;
    }
    rq.what = what;
    // Le signataire : l'utilisateur connecte ; personne : le premier compte qui peut signer.
    const auto candidates = [&](const std::string& except) {
        std::vector<const User*> out;
        for (const auto& u : project_->security.users)
            if (canSign(u) && !sameText(u.login, except)) out.push_back(&u);
        return out;
    };
    if (user_.empty())
        if (const auto list = candidates({}); !list.empty()) rq.signer = list.front()->login;
    if (rq.twoSigners) {
        const std::string first = user_.empty() ? rq.signer : user_;
        // Le visa : le premier compte d'un niveau suffisant, sinon le premier autre.
        for (const User* u : candidates(first))
            if (userLevel(*project_, *u) >= rq.visaLevel) { rq.visa = u->login; break; }
        if (rq.visa.empty())
            if (const auto list = candidates(first); !list.empty()) rq.visa = list.front()->login;
    }
    const std::string mode = rq.twoSigners ? "double" : "simple";
    const std::string source = rq.source;
    log("Signature", source, "signature " + mode + " demand\xC3\xA9" "e : " + what);
    signature_ = std::move(rq);
    signForm_ = FormState{};
    signForm_.focus = "motdepasse";       // un clavier physique tape tout de suite
    signKeyboard_ = false;
    now_ = std::max(now_, now);
}

const User* Runtime::signatureSigner() const {
    if (!signature_ || !project_) return nullptr;
    if (!user_.empty()) return user();
    return signature_->signer.empty() ? nullptr : project_->userByLogin(signature_->signer);
}

const User* Runtime::signatureVisa() const {
    if (!signature_ || !project_ || signature_->visa.empty()) return nullptr;
    return project_->userByLogin(signature_->visa);
}

void Runtime::signatureCancel(double now, const std::string& why) {
    if (!signature_) return;
    const SignatureRequest rq = *signature_;
    signature_.reset();
    signForm_ = FormState{};
    signKeyboard_ = false;
    now_ = std::max(now_, now);
    if (rq.gesture == SignatureRequest::Gesture::Drag)
        if (const auto it = controls_.find(rq.object); it != controls_.end()) it->second.drag.reset();
    event("Signature annul\xC3\xA9" "e", rq.source, rq.what + " : " + why);
    audit("Signature annul\xC3\xA9" "e", rq.source, rq.what, {}, {}, why);
}

void Runtime::signaturePart(std::string_view part, double now) {
    now_ = std::max(now_, now);
    lastActivity_ = std::max(lastActivity_, now);
    if (!signature_ || !project_) return;
    auto& rq = *signature_;
    auto& f = signForm_;
    const auto say = [&](std::string text, bool error) {
        f.message = std::move(text);
        f.error = error;
        f.messageAt = now;
    };
    if (part == "fermer" || part == "bouton:annuler") {
        signatureCancel(now, "annul\xC3\xA9" "e par l'op\xC3\xA9rateur");
        return;
    }
    if (part.empty() || part == "rien" || part == "dehors") {
        f.focus.clear();
        signKeyboard_ = false;
        return;
    }
    const auto cycle = [&](std::string& login, int step, const std::string& except) {
        std::vector<const User*> list;
        for (const auto& u : project_->security.users)
            if (canSign(u) && !sameText(u.login, except)) list.push_back(&u);
        if (list.empty()) return false;
        const std::size_t n = list.size();
        std::size_t at = 0;
        for (std::size_t k = 0; k < n; ++k) if (sameText(list[k]->login, login)) at = k;
        at = (at + n + static_cast<std::size_t>(step + static_cast<int>(n))) % n;
        login = list[at]->login;
        return true;
    };
    if (part == "signataire:precedent" || part == "signataire:suivant") {
        if (!user_.empty()) {
            say("le signataire est l'utilisateur connect\xC3\xA9 (" + user_ + ")", true);
            return;
        }
        if (cycle(rq.signer, part == "signataire:suivant" ? 1 : -1, {})) {
            if (rq.twoSigners && sameText(rq.visa, rq.signer)) (void)cycle(rq.visa, 1, rq.signer);
            f.text["motdepasse"].clear();
            f.caret["motdepasse"] = 0;
            f.focus = "motdepasse";
            if (f.error) { f.message.clear(); f.error = false; }
        }
        return;
    }
    if (part == "visa:precedent" || part == "visa:suivant") {
        if (!rq.twoSigners) return;
        const User* s = signatureSigner();
        if (cycle(rq.visa, part == "visa:suivant" ? 1 : -1, s ? s->login : std::string{})) {
            f.text["visa"].clear();
            f.caret["visa"] = 0;
            f.focus = "visa";
            if (f.error) { f.message.clear(); f.error = false; }
        }
        return;
    }
    if (part == "motif:precedent" || part == "motif:suivant") {
        if (rq.reasons.empty()) return;
        const std::size_t n = rq.reasons.size();
        rq.reason = (rq.reason + (part == "motif:suivant" ? 1 : n - 1)) % n;
        return;
    }
    if (part.rfind("champ:", 0) == 0) {
        const std::string field(part.substr(6));
        if (field == "motif" && !rq.reasons.empty()) return;
        if (field == "visa" && !rq.twoSigners) return;
        if (field != "motdepasse" && field != "motif" && field != "visa") return;
        f.focus = field;
        f.caret[field] = f.text[field].size();
        signKeyboard_ = true;
        return;
    }
    if (part == "bouton:signer") signatureSubmit(now);
}

void Runtime::signatureSubmit(double now) {
    if (!signature_ || !project_) return;
    auto& rq = *signature_;
    auto& f = signForm_;
    const auto fail = [&](const std::string& msg, const char* field) {
        f.message = msg;
        f.error = true;
        f.messageAt = now;
        if (field) {
            f.focus = field;
            f.caret[field] = f.text[field].size();
        }
    };
    // Le mot de passe (ou le code) d'un compte ; faux : un echec de connexion.
    const auto verify = [&](const User& u, const std::string& secret, std::string& reason) {
        if (u.protection == "dynamique") {
            reason = "code incorrect ou expir\xC3\xA9";
            return dynamicCodeMatches(u.secret, secret, wallEpoch(), project_->security.dynamicPeriodS, project_->security.dynamicDigits);
        }
        if (u.protection == "classique" && !u.passwordHash.empty()) {
            reason = "mot de passe incorrect";
            return passwordMatches(u.salt, u.passwordHash, secret);
        }
        reason = "ce compte ne peut pas signer (pas de mot de passe)";
        return false;
    };
    const User* s = signatureSigner();
    if (!s) { fail("aucun compte pour signer", nullptr); return; }
    if (!s->enabled) { fail(s->login + " : compte d\xC3\xA9sactiv\xC3\xA9", nullptr); return; }
    if (accountLocked(s->login)) { fail(s->login + " : compte verrouill\xC3\xA9", nullptr); return; }
    std::string reason;
    const std::string secret = f.text["motdepasse"];
    f.text["motdepasse"].clear();
    f.caret["motdepasse"] = 0;
    const std::string signerLogin = s->login;
    if (!verify(*s, secret, reason)) {
        const std::string extra = canSign(*s) ? loginFailed(*s, now) : std::string{};
        event("Signature refus\xC3\xA9" "e", rq.source, signerLogin + " : " + reason);
        audit("Signature refus\xC3\xA9" "e", rq.source, rq.what, {}, {}, signerLogin + " : " + reason);
        fail(reason + extra, "motdepasse");
        return;
    }
    const std::string motive = !rq.reasons.empty() ? rq.reasons[std::min(rq.reason, rq.reasons.size() - 1)] : trimmedCopy(f.text["motif"]);
    if (motive.empty()) { fail("le motif est obligatoire", "motif"); return; }
    std::string signers = signerLogin;
    if (rq.twoSigners) {
        const User* vz = signatureVisa();
        if (!vz) { fail("le visa demande un second compte (choisis-le aux fl\xC3\xA8" "ches)", nullptr); return; }
        if (sameText(vz->login, signerLogin)) { fail("le visa demande un autre compte que " + signerLogin, nullptr); return; }
        if (const int lvl = userLevel(*project_, *vz); lvl < rq.visaLevel) {
            fail("le visa demande le niveau " + std::to_string(rq.visaLevel) + " (" + vz->login + " a le niveau " + std::to_string(lvl) + ")", nullptr);
            return;
        }
        if (accountLocked(vz->login)) { fail(vz->login + " : compte verrouill\xC3\xA9", nullptr); return; }
        const std::string visaSecret = f.text["visa"];
        f.text["visa"].clear();
        f.caret["visa"] = 0;
        const std::string visaLogin = vz->login;
        if (!verify(*vz, visaSecret, reason)) {
            const std::string extra = canSign(*vz) ? loginFailed(*vz, now) : std::string{};
            event("Signature refus\xC3\xA9" "e", rq.source, "visa de " + visaLogin + " : " + reason);
            audit("Signature refus\xC3\xA9" "e", rq.source, rq.what, {}, {}, "visa de " + visaLogin + " : " + reason);
            fail("visa de " + visaLogin + " : " + reason + extra, "visa");
            return;
        }
        loginSucceeded(*vz);
        signers += " + visa " + visaLogin;
    }
    loginSucceeded(*s);
    // Signe : le geste agit, et le journal d'audit dit qui l'a signe.
    const SignatureRequest done = rq;
    signature_.reset();
    signForm_ = FormState{};
    signKeyboard_ = false;
    lastSignature_ = signers + " (" + motive + ") " + dateStampOf(now).substr(0, 16);
    event("Signature", done.source, signers + " : " + done.what + " (motif : " + motive + ")");
    signedBy_ = signers;
    signedReason_ = motive;
    audit("Signature", done.source, done.what);
    resumeSigned(done, now);
    signedBy_.clear();
    signedReason_.clear();
}

void Runtime::resumeSigned(const SignatureRequest& rq, double now) {
    composed_.clear();
    const View* v = shownViewOf(rq.object);
    const Object* o = v ? v->object(rq.object) : nullptr;
    if (!o) {
        log("Erreur", rq.source, "sign\xC3\xA9, mais l'objet n'est plus affich\xC3\xA9 : rien n'agit");
        return;
    }
    const View snapshot = *v;
    signatureBypass_ = true;
    {
        GestureGuard gesture(*this, rq.source);
        switch (rq.gesture) {
            case SignatureRequest::Gesture::Click:
                if (o->kind != Kind::Button) controlRelease(snapshot, *snapshot.object(rq.object), now, true);
                runActions(snapshot, snapshot.object(rq.object), Trigger::Click, now);
                break;
            case SignatureRequest::Gesture::Part:
                objectPart(rq.object, rq.part, now);
                break;
            case SignatureRequest::Gesture::Drag:
                dragValue(rq.object, rq.fraction, true, now);
                break;
            case SignatureRequest::Gesture::Input:
                forms_[rq.object].text["valeur"] = rq.text;
                submitForm(snapshot, *snapshot.object(rq.object), now);
                break;
        }
    }
    signatureBypass_ = false;
}

std::string Runtime::signatureKeyboardMode() const {
    if (!signature_ || signForm_.focus.empty() || settings_.keyboard == "jamais") return {};
    if (settings_.keyboard != "toujours" && !signKeyboard_) return {};
    const User* who = signForm_.focus == "visa" ? signatureVisa() : signForm_.focus == "motdepasse" ? signatureSigner() : nullptr;
    return who && who->protection == "dynamique" ? "numerique" : "complet";
}

void Runtime::signatureTypeText(std::string_view text, double now) {
    auto& f = signForm_;
    if (f.focus.empty()) return;
    auto& buffer = f.text[f.focus];
    auto& caret = f.caret[f.focus];
    caret = std::min(caret, buffer.size());
    const User* who = f.focus == "visa" ? signatureVisa() : f.focus == "motdepasse" ? signatureSigner() : nullptr;
    const bool digitsOnly = who && who->protection == "dynamique";
    std::string accepted;
    for (const char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') continue;
        if (digitsOnly && !(c >= '0' && c <= '9')) continue;
        accepted += c;
    }
    if (accepted.empty()) return;
    if (codepointCount(buffer) + codepointCount(accepted) > kMaxTyped) {
        f.message = std::to_string(kMaxTyped) + " caract\xC3\xA8res au plus";
        f.error = true;
        f.messageAt = now;
        return;
    }
    buffer.insert(caret, accepted);
    caret += accepted.size();
    if (f.error) { f.message.clear(); f.error = false; }
}

void Runtime::signatureTypeKey(EditKey k, double now) {
    if (!signature_) return;
    auto& f = signForm_;
    std::vector<std::string> fields{"motdepasse"};
    if (signature_->reasons.empty()) fields.push_back("motif");
    if (signature_->twoSigners) fields.push_back("visa");
    if (k == EditKey::Escape) {
        signatureCancel(now, "annul\xC3\xA9" "e (\xC3\x89" "chap)");
        return;
    }
    if (f.focus.empty()) {
        if (k == EditKey::Enter) signatureSubmit(now);
        else if (k == EditKey::Tab) { f.focus = fields.front(); f.caret[f.focus] = f.text[f.focus].size(); }
        return;
    }
    auto& buffer = f.text[f.focus];
    auto& caret = f.caret[f.focus];
    caret = std::min(caret, buffer.size());
    const auto moveField = [&](int step) {
        std::size_t at = 0;
        for (std::size_t i = 0; i < fields.size(); ++i) if (fields[i] == f.focus) at = i;
        const std::size_t n = fields.size();
        f.focus = fields[(at + n + static_cast<std::size_t>(step + static_cast<int>(n))) % n];
        f.caret[f.focus] = f.text[f.focus].size();
    };
    switch (k) {
        case EditKey::Backspace:
            if (caret > 0) { const auto p = prevCodepoint(buffer, caret); buffer.erase(p, caret - p); caret = p; }
            break;
        case EditKey::Delete:
            if (caret < buffer.size()) buffer.erase(caret, nextCodepoint(buffer, caret) - caret);
            break;
        case EditKey::Left: caret = prevCodepoint(buffer, caret); break;
        case EditKey::Right: caret = nextCodepoint(buffer, caret); break;
        case EditKey::Home: caret = 0; break;
        case EditKey::End: caret = buffer.size(); break;
        case EditKey::Tab: moveField(1); break;
        case EditKey::BackTab: moveField(-1); break;
        case EditKey::Enter:
            if (f.focus != fields.back()) { moveField(1); break; }
            signatureSubmit(now);
            break;
        case EditKey::Escape: break;
    }
}

// ============================================ l'avertissement de deconnexion ===
bool Runtime::logoutWarning(double now) const {
    if (!project_ || !project_->security.enabled || user_.empty() || project_->security.logoutWarnS <= 0) return false;
    const double left = autoLogoutRemaining(now);
    return left >= 0 && left <= project_->security.logoutWarnS;
}

void Runtime::stayConnected(double now) {
    now_ = std::max(now_, now);
    if (warned_ && !user_.empty())
        log("S\xC3\xA9" "curit\xC3\xA9", "d\xC3\xA9" "connexion automatique", user_ + " reste connect\xC3\xA9");
    warned_ = false;
    lastActivity_ = now;
}

// ================================================================== le badge ===
bool Runtime::badgeListening() const { return running_ && project_ && project_->security.badgeLogin; }

void Runtime::badgeTyped(std::string_view text, double now) {
    if (!badgeListening()) return;
    // Un lecteur tape vite : un trou de plus d'une seconde, c'est une main - on repart.
    if (badgeLast_ >= 0 && now - badgeLast_ > 1.0) badgeBuffer_.clear();
    for (const char c : text)
        if (std::isalnum(static_cast<unsigned char>(c))) badgeBuffer_ += c;
    if (badgeBuffer_.size() > kMaxTyped) badgeBuffer_.erase(0, badgeBuffer_.size() - kMaxTyped);
    badgeLast_ = now;
}

bool Runtime::badgeEnter(double now) {
    const std::string number = badgeBuffer_;
    badgeBuffer_.clear();
    badgeLast_ = -1;
    if (!badgeListening() || number.size() < 4) return false;
    std::string why;
    return loginBadge(number, now, &why);
}

bool Runtime::loginBadge(std::string_view number, double now, std::string* why) {
    now_ = std::max(now_, now);
    if (!project_) return false;
    const auto refuse = [&](const std::string& target, const std::string& reason) {
        if (why) *why = reason;
        event("Connexion refus\xC3\xA9" "e", target, reason + " (badge)");
        audit("Connexion refus\xC3\xA9" "e", "badge", target, {}, {}, reason);
        return false;
    };
    const User* found = nullptr;
    for (const auto& u : project_->security.users)
        if (!u.badge.empty() && badgeMatches(u.badge, number)) { found = &u; break; }
    if (!found) return refuse("(badge)", "badge inconnu");
    const std::string who = found->login;
    if (!found->enabled) return refuse(who, "compte d\xC3\xA9sactiv\xC3\xA9");
    if (accountLocked(who)) return refuse(who, "compte verrouill\xC3\xA9");
    if (!user_.empty() && sameText(user_, who)) {
        lastActivity_ = now;
        return true;                          // deja connecte : le badge le garde
    }
    if (!user_.empty()) logout(now, "badge (" + who + ")");
    found = project_->userByLogin(who);
    if (!found) return false;
    loginSucceeded(*found);
    completeLogin(*found, now, "badge");
    return true;
}

// ======================================================== les performances ===
void Runtime::resetPerf() {
    perf_ = PerfStats{};
    perf_.since = now_;
    if (project_) perf_.periodMs = std::max(10, project_->config.cycleMs);
}

void Runtime::notePerf(double evaluateMs, double paintMs, std::size_t expressions, std::size_t objects) {
    if (evaluateMs >= 0) perf_.evaluate.add(evaluateMs);
    if (paintMs >= 0) perf_.paint.add(paintMs);
    perf_.expressions = expressions;
    perf_.objects = objects;
}

std::string perfMs(double ms) {
    char b[48];
    std::snprintf(b, sizeof b, ms < 10 ? "%.2f ms" : "%.1f ms", ms);
    std::string s = b;
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}

ExportTable perfTable(const PerfStats& p, double elapsedS, std::size_t topScripts) {
    ExportTable t;
    t.title = "Performances de l'IHM";
    t.headers = {"Mesure", "Derni\xC3\xA8re", "Moyenne", "Maximum", "Nombre"};
    const auto series = [&](const char* name, const PerfSeries& s) {
        t.rows.push_back({name, perfMs(s.last), perfMs(s.mean()), perfMs(s.max), std::to_string(s.count)});
    };
    series("Cycle IHM (travail)", p.cycle);
    t.rows.push_back({"P\xC3\xA9riode du cycle", perfMs(p.periodMs), "", "", ""});
    const double share = p.cycle.count ? 100.0 * static_cast<double>(p.overruns) / static_cast<double>(p.cycle.count) : 0.0;
    char pct[32];
    std::snprintf(pct, sizeof pct, "%.1f %%", share);
    std::string pcts = pct;
    for (auto& c : pcts) if (c == '.') c = ',';
    t.rows.push_back({"D\xC3\xA9passements (cycle plus long que sa p\xC3\xA9riode)", std::to_string(p.overruns), pcts, "", std::to_string(p.cycle.count)});
    series("\xC3\x89valuation des expressions", p.evaluate);
    series("Dessin de la vue", p.paint);
    t.rows.push_back({"Expressions \xC3\xA9valu\xC3\xA9" "es (dernier rafra\xC3\xAE" "chissement)", std::to_string(p.expressions), "", "", ""});
    t.rows.push_back({"Objets dessin\xC3\xA9s (dernier rafra\xC3\xAE" "chissement)", std::to_string(p.objects), "", "", ""});
    char rate[48];
    std::snprintf(rate, sizeof rate, "%.1f /s", elapsedS > 0 ? static_cast<double>(p.writes) / elapsedS : 0.0);
    std::string rates = rate;
    for (auto& c : rates) if (c == '.') c = ',';
    t.rows.push_back({"\xC3\x89" "critures de variables", std::to_string(p.writes), rates, "", ""});
    // Les scripts les plus longs (par temps total).
    std::vector<const ScriptPerf*> byTotal;
    for (const auto& [id, sp] : p.scripts) byTotal.push_back(&sp);
    std::stable_sort(byTotal.begin(), byTotal.end(), [](const ScriptPerf* a, const ScriptPerf* b) { return a->time.total > b->time.total; });
    for (std::size_t i = 0; i < byTotal.size() && i < topScripts; ++i)
        t.rows.push_back({"Script : " + byTotal[i]->name, perfMs(byTotal[i]->time.last), perfMs(byTotal[i]->time.mean()),
                          perfMs(byTotal[i]->time.max), std::to_string(byTotal[i]->time.count)});
    return t;
}

// ================================================================ la langue ==
//  SYS.Language : la langue que lit l'operateur. Le moteur ne traduit rien de ce
//  qu'il garde (les valeurs des choix, les textes lus par Vue.Objet.Text) : le
//  dessin traduit la vue montree ; une alarme prend son message et sa consigne
//  dans la langue du moment ou elle apparait.
void Runtime::resetDisplay() {
    language_.clear();
    languageChanges_ = 0;
    if (!project_) return;
    language_ = projectDisplay(*project_).language;
    applyProjectDisplay();
}

void Runtime::applyProjectDisplay() {
    if (!project_) return;
    const auto d = projectDisplay(*project_);
    // L'affichage repart de celui du projet (les autres reglages du poste restent).
    settings_.textScale = std::clamp(d.textScale, 50, 300);
    settings_.colorMode = d.colorMode == "daltonien" ? "daltonien" : "normal";
    settings_.symbols = d.symbols;
    settings_.theme = d.theme == "jour" ? "jour" : "nuit";
}

DisplayOptions Runtime::displayOptions() const {
    DisplayOptions o;
    o.language = language_;
    o.textScale = settings_.textScale;
    o.colorMode = settings_.colorMode;
    o.symbols = settings_.symbols;
    o.theme = settings_.theme;
    return o;
}

bool Runtime::setDisplay(std::string_view key, std::string_view raw, double now, const std::string& source, std::string* why) {
    now_ = std::max(now_, now);
    std::size_t a = 0, b = raw.size();
    while (a < b && std::isspace(static_cast<unsigned char>(raw[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(raw[b - 1]))) --b;
    std::string value(raw.substr(a, b - a));
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') value = value.substr(1, value.size() - 2);
    std::string low = value;
    for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto fail = [&](std::string reason) {
        log("Erreur", source, reason);
        if (why) *why = std::move(reason);
        return false;
    };
    SystemSettings next = settings_;
    if (key == "texte") {
        if (low == "plus" || low == "moins") (void)stepSetting(next, "texte", low);
        else {
            double n = 0;
            if (!parseNumber(value, n) || n < 50 || n > 300) return fail("taille du texte \xC2\xAB " + value + " \xC2\xBB : de 50 \xC3\xA0 300 (%)");
            next.textScale = static_cast<int>(std::lround(n));
        }
    } else if (key == "couleurs") {
        if (low == "daltonien" || low == "normal" || low == "normales") next.colorMode = low == "daltonien" ? "daltonien" : "normal";
        else if (low == "bascule") next.colorMode = next.colorMode == "daltonien" ? "normal" : "daltonien";
        else return fail("couleurs \xC2\xAB " + value + " \xC2\xBB : normal ou daltonien");
    } else if (key == "symboles") {
        if (low == "bascule") next.symbols = !next.symbols;
        else next.symbols = parseBool(value, next.symbols);
    } else if (key == "theme") {
        if (low == "jour" || low == "clair" || low == "day") next.theme = "jour";
        else if (low == "nuit" || low == "sombre" || low == "night") next.theme = "nuit";
        else if (low.empty() || low == "bascule" || low == "suivant") next.theme = next.theme == "jour" ? "nuit" : "jour";
        else return fail("th\xC3\xA8me \xC2\xAB " + value + " \xC2\xBB : jour, nuit ou bascule");
    } else {
        return fail("r\xC3\xA9glage d'affichage inconnu : " + std::string(key));
    }
    if (next == settings_) return true;
    settings_ = next;
    const auto* spec = settingSpec(key);
    log("Action", source, std::string(spec ? spec->label : key) + " : " + settingText(settings_, key));
    return true;
}

bool Runtime::setLanguage(std::string_view code, double now, const std::string& source, std::string* why) {
    now_ = std::max(now_, now);
    const auto fail = [&](std::string reason) {
        log("Erreur", source, "changer de langue : " + reason);
        if (why) *why = std::move(reason);
        return false;
    };
    if (!project_) return fail("pas de projet");
    const auto& l = project_->languages;
    std::size_t a = 0, b = code.size();
    while (a < b && std::isspace(static_cast<unsigned char>(code[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(code[b - 1]))) --b;
    std::string wanted(code.substr(a, b - a));
    if (wanted.size() >= 2 && (wanted.front() == '\'' || wanted.front() == '"') && wanted.back() == wanted.front())
        wanted = wanted.substr(1, wanted.size() - 2);
    const Language* lang = nullptr;
    if (wanted.empty() || sameText(wanted, "suivante") || sameText(wanted, "next")) {
        if (l.list.empty()) return fail("aucune langue dans le projet");
        std::size_t at = 0;
        for (std::size_t i = 0; i < l.list.size(); ++i)
            if (sameText(l.list[i].code, language_)) at = i;
        lang = &l.list[(at + 1) % l.list.size()];
    } else {
        lang = l.find(wanted);
        if (!lang)
            for (const auto& x : l.list)
                if (sameText(x.name, wanted)) lang = &x;
    }
    if (!lang) {
        std::string codes;
        for (const auto& x : l.list) codes += (codes.empty() ? "" : ", ") + x.code;
        return fail("langue \xC2\xAB " + wanted + " \xC2\xBB inconnue (Configuration > Langues : " + codes + ")");
    }
    if (sameText(lang->code, language_)) return true;
    const std::string before = language_;
    language_ = lang->code;
    ++languageChanges_;
    event("Langue", source, (lang->name.empty() ? lang->code : lang->name) + " (" + before + " \xE2\x86\x92 " + lang->code + ")");
    return true;
}

void Runtime::languagePart(const View& v, const Object& o, std::string_view part, double now) {
    if (!project_ || part.rfind("langue:", 0) != 0) return;
    const auto choices = languageChoices(o, project_->languages);
    if (choices.empty()) return;
    const std::string source = where(v, &o);
    const std::string_view rest = part.substr(7);
    std::string code;
    if (rest == "suivante") {
        std::size_t at = choices.size() - 1;
        for (std::size_t i = 0; i < choices.size(); ++i)
            if (sameText(choices[i].code, language_)) at = i;
        code = choices[(at + 1) % choices.size()].code;
    } else {
        const int k = std::atoi(std::string(rest).c_str());
        if (k < 0 || static_cast<std::size_t>(k) >= choices.size()) return;
        code = choices[static_cast<std::size_t>(k)].code;
    }
    (void)setLanguage(code, now, source);
}

void Runtime::resetLot13() {
    signature_.reset();
    signForm_ = FormState{};
    signKeyboard_ = false;
    renewal_.reset();
    loginVia_.clear();
    warned_ = false;
    badgeBuffer_.clear();
    badgeLast_ = -1;
    gesture_ = 0;
    gestureWhere_.clear();
    auditMute_ = 0;
    signedBy_.clear();
    signedReason_.clear();
    signatureBypass_ = false;
    lastSignature_.clear();
    resetPerf();
    resetDisplay();
}

} // namespace hmi
