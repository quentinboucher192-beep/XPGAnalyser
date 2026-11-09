// =============================================================================
//  licensing/Gateway.cpp - les formats des demandes en ligne et de l'etat d'un
//  poste (voir Gateway.hpp)
// =============================================================================
#include "Gateway.hpp"
#include "Service.hpp"

#include "../core/Json.hpp"

#include <utility>

namespace licensing::gateway {

namespace json = core::json;

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// Une ligne de texte : UTF-8 valide, aucun caractere de controle (ni fin de ligne, ni echappement de terminal).
bool plainLine(std::string_view s) noexcept {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7F) return false;
        if (c < 0x80) {
            ++i;
            continue;
        }
        const std::size_t n = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        if (n == 0 || c < 0xC2 || c > 0xF4 || i + n >= s.size()) return false;
        for (std::size_t k = 1; k <= n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        // les caracteres de controle C1 (U+0080 a U+009F : C2 80 a C2 9F), dont l'echappement CSI
        if (c == 0xC2 && static_cast<unsigned char>(s[i + 1]) < 0xA0) return false;
        i += n + 1;
    }
    return true;
}

// Le base64 en lignes de 76 (un sceau lisible dans un journal ou un fichier).
std::string wrap76(const std::string& b64) {
    std::string out;
    for (std::size_t i = 0; i < b64.size(); i += 76) {
        out.append(b64, i, 76);
        out += '\n';
    }
    return out;
}

// Un objet JSON strict : pas d'autre cle que `known`, "version" = 1.
bool readObject(std::string_view text, json::Value& v, std::initializer_list<std::string_view> known, std::string_view what, std::string* why) {
    std::string err;
    if (!json::parse(text, v, &err) || !v.isObject()) return fail(why, std::string(what) + " n'est pas un JSON lisible" + (err.empty() ? "" : " (" + err + ")"));
    const json::Value extra = json::unknownMembers(v, known);
    if (!extra.members().empty()) return fail(why, std::string(what) + " porte un champ inconnu (" + extra.members().front().first + ")");
    const json::Value* ver = v.find("version");
    if (!ver || !ver->isNumber() || ver->asInt() != 1) return fail(why, std::string(what) + " n'est pas de la version 1 (un logiciel plus r\xC3\xA9" "cent l'a \xC3\xA9" "crit ?)");
    return true;
}

bool readString(const json::Value& v, std::string_view key, std::string& out, bool required, std::size_t maxLen, std::string_view what,
                std::string* why) {
    const json::Value* f = v.find(key);
    if (!f) {
        if (required) return fail(why, std::string(what) + " : le champ " + std::string(key) + " manque");
        out.clear();
        return true;
    }
    if (!f->isString()) return fail(why, std::string(what) + " : le champ " + std::string(key) + " n'est pas un texte");
    if (f->asString().size() > maxLen) return fail(why, std::string(what) + " : le champ " + std::string(key) + " est trop long");
    out = f->asString();
    return true;
}

bool readCount(const json::Value& v, std::string_view key, std::uint64_t& out, std::string_view what, std::string* why) {
    const json::Value* f = v.find(key);
    if (!f || !f->isNumber() || f->asNumber() < 1 || f->asNumber() > 9007199254740991.0 || f->asNumber() != static_cast<double>(f->asInt()))
        return fail(why, std::string(what) + " : le champ " + std::string(key) + " doit \xC3\xAAtre un entier positif");
    out = static_cast<std::uint64_t>(f->asInt());
    return true;
}

bool readTime(const json::Value& v, std::string_view key, Seconds& out, std::string_view what, std::string* why) {
    std::string t;
    if (!readString(v, key, t, true, 40, what, why)) return false;
    if (!parseIsoTime(t, out)) return fail(why, std::string(what) + " : la date " + std::string(key) + " est illisible");
    return true;
}

} // namespace

// ============================================================ sceller ==
std::string sealFor(const crypto::PublicKey& key, std::string_view plain) {
    crypto::Bytes box;
    if (!crypto::seal(key, plain, box)) return {};
    return wrap76(crypto::base64(box.data(), box.size()));
}

bool openWith(const SigningKey& key, std::string_view sealedBase64, std::string& plain, std::string* why) {
    crypto::Bytes box;
    if (!crypto::unbase64(sealedBase64, box)) return fail(why, "le contenu scell\xC3\xA9 n'est pas du base64");
    if (!key.openSeal(box, plain)) return fail(why, "le contenu scell\xC3\xA9 ne s'ouvre pas avec " + std::string(key.test() ? "cette cl\xC3\xA9 d'essai" : "cette cl\xC3\xA9") + " (scell\xC3\xA9 pour une autre, ou modifi\xC3\xA9)");
    return true;
}

// ============================================================ l'etat d'un PC ==
const char* stateKey(State s) noexcept {
    switch (s) {
        case State::Received: return "recue";
        case State::Accepted: return "acceptee";
        case State::Refused: return "refusee";
        case State::Cancelled: return "annulee";
        case State::Updated: return "mise_a_jour";
        case State::Revoked: return "revoquee";
        case State::Removed: return "retiree";
    }
    return "?";
}

bool parseStateKey(std::string_view key, State& out) noexcept {
    for (State s : {State::Received, State::Accepted, State::Refused, State::Cancelled, State::Updated, State::Revoked, State::Removed})
        if (key == stateKey(s)) {
            out = s;
            return true;
        }
    return false;
}

std::string stateLabel(State s) {
    switch (s) {
        case State::Received: return "Re\xC3\xA7ue par l'\xC3\xA9" "diteur";
        case State::Accepted: return "Accept\xC3\xA9" "e";
        case State::Refused: return "Refus\xC3\xA9" "e";
        case State::Cancelled: return "Annul\xC3\xA9" "e";
        case State::Updated: return "Licence mise \xC3\xA0 jour";
        case State::Revoked: return "Licence r\xC3\xA9voqu\xC3\xA9" "e";
        case State::Removed: return "PC retir\xC3\xA9 de la licence";
    }
    return "?";
}

std::string writeDeviceStatus(const DeviceStatus& s) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("poste", s.device);
    v.set("numero", s.number);
    v.set("emis", isoTime(s.issuedAt));
    v.set("etat", stateKey(s.state));
    if (!s.requestNonce.empty()) v.set("demande", s.requestNonce);
    if (!s.licenseId.empty()) v.set("licence", s.licenseId);
    if (!s.sealed.empty()) v.set("scelle", s.sealed);
    return json::write(v) + "\n";
}

bool parseDeviceStatus(std::string_view text, DeviceStatus& out, std::string* why) {
    constexpr std::string_view what = "l'\xC3\xA9tat du PC";
    json::Value v;
    if (!readObject(text, v, {"version", "poste", "numero", "emis", "etat", "demande", "licence", "scelle"}, what, why)) return false;
    DeviceStatus s;
    std::string state;
    if (!readString(v, "poste", s.device, true, 32, what, why) || !readCount(v, "numero", s.number, what, why) ||
        !readTime(v, "emis", s.issuedAt, what, why) || !readString(v, "etat", state, true, 20, what, why) ||
        !readString(v, "demande", s.requestNonce, false, 128, what, why) || !readString(v, "licence", s.licenseId, false, 64, what, why) ||
        !readString(v, "scelle", s.sealed, false, 700u * 1024u, what, why))
        return false;
    if (!service::isDeviceId(s.device)) return fail(why, std::string(what) + " : l'identifiant du poste est mal \xC3\xA9" "crit");
    if (!parseStateKey(state, s.state)) return fail(why, std::string(what) + " : l'\xC3\xA9tat \xC2\xAB " + state + " \xC2\xBB est inconnu");
    out = std::move(s);
    return true;
}

std::string writeStatusContent(const StatusContent& c) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    if (!c.message.empty()) v.set("message", c.message);
    if (!c.license.empty()) v.set("licence", c.license);
    if (!c.revocations.empty()) v.set("revocations", c.revocations);
    if (!c.code.empty()) v.set("code", c.code);   // 1.37.0
    if (!c.onlineLicense.empty()) v.set("licence_en_ligne", c.onlineLicense);
    if (!c.mail.empty()) v.set("mail", c.mail);
    if (!c.mailTo.empty()) v.set("mail_a", c.mailTo);
    return json::write(v, false);
}

bool parseStatusContent(std::string_view text, StatusContent& out, std::string* why) {
    constexpr std::string_view what = "le contenu scell\xC3\xA9";
    json::Value v;
    if (!readObject(text, v, {"version", "message", "licence", "revocations", "code", "licence_en_ligne", "mail", "mail_a"}, what, why)) return false;
    StatusContent c;
    if (!readString(v, "message", c.message, false, 2000, what, why) || !readString(v, "licence", c.license, false, 300u * 1024u, what, why) ||
        !readString(v, "revocations", c.revocations, false, 8u * 1024u * 1024u, what, why) || !readString(v, "code", c.code, false, 64, what, why) ||
        !readString(v, "licence_en_ligne", c.onlineLicense, false, 64, what, why) || !readString(v, "mail", c.mail, false, 16, what, why) ||
        !readString(v, "mail_a", c.mailTo, false, 254, what, why))
        return false;
    if (!c.mail.empty() && c.mail != kMailSent && c.mail != kMailFailed)
        return fail(why, std::string(what) + " : l'\xC3\xA9tat du mail " + c.mail + " est inconnu");
    out = std::move(c);
    return true;
}

// ============================================================ 1.37.0 : un courriel lisible ==
bool plausibleEmail(std::string_view s) {
    if (s.empty() || s.size() > 254) return false;
    const auto at = s.find('@');
    if (at == std::string_view::npos || at == 0 || s.find('@', at + 1) != std::string_view::npos) return false;
    const std::string_view domain = s.substr(at + 1);
    const auto dot = domain.find('.');
    if (dot == std::string_view::npos || dot == 0 || domain.back() == '.' || domain.size() < 3) return false;
    for (unsigned char ch : s)
        if (ch <= 0x20 || ch == 0x7F || ch == '<' || ch == '>' || ch == '"' || ch == ',' || ch == ';' || ch == '\\') return false;
    return true;
}

// ============================================================ la demande en ligne ==
std::string writeOnlineRequest(const OnlineRequest& r) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("demande", r.request);
    v.set("client", r.customer);
    if (!r.contact.empty()) v.set("contact", r.contact);
    if (!r.message.empty()) v.set("message", r.message);
    if (!r.edition.empty()) v.set("edition", r.edition);   // 1.37.0
    if (!r.email.empty()) v.set("courriel", r.email);
    if (!r.option.empty()) {   // 1.42.0
        v.set("option", r.option);
        v.set("option_jours", r.optionDays);
        if (r.optionFrom) v.set("option_du", isoTime(r.optionFrom));
        if (r.optionLastDay) v.set("option_au", isoTime(r.optionLastDay));
    }
    return json::write(v, false);
}

bool parseOnlineRequest(std::string_view text, OnlineRequest& out, std::string* why) {
    constexpr std::string_view what = "la demande en ligne";
    json::Value v;
    if (!readObject(text, v, {"version", "demande", "client", "contact", "message", "edition", "courriel", "option", "option_jours", "option_du", "option_au"}, what,
                    why))
        return false;
    OnlineRequest r;
    if (!readString(v, "demande", r.request, true, 32u * 1024u, what, why) || !readString(v, "client", r.customer, true, 200, what, why) ||
        !readString(v, "contact", r.contact, false, 200, what, why) || !readString(v, "message", r.message, false, 2000, what, why) ||
        !readString(v, "edition", r.edition, false, 32, what, why) || !readString(v, "courriel", r.email, false, 254, what, why))
        return false;
    if (!r.edition.empty() && r.edition.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        return fail(why, std::string(what) + " : l'\xC3\xA9" "dition voulue n'est pas un identifiant d'\xC3\xA9" "dition");
    if (!r.email.empty() && !plausibleEmail(r.email)) return fail(why, std::string(what) + " : le courriel n'est pas lisible");
    // 1.42.0 : l'option demandee
    if (!readString(v, "option", r.option, false, 64, what, why)) return false;
    if (!r.option.empty()) {
        if (!isFeatureId(r.option)) return fail(why, std::string(what) + " : l'option demand\xC3\xA9" "e n'est pas un identifiant d'option");
        const json::Value* d = v.find("option_jours");
        if (!d || !d->isNumber() || d->asNumber() != static_cast<double>(d->asInt()) || d->asInt() < -1 || d->asInt() > 3660)
            return fail(why, std::string(what) + " : la dur\xC3\xA9" "e de l'option est illisible");
        r.optionDays = static_cast<int>(d->asInt());
        if (v.find("option_du") && !readTime(v, "option_du", r.optionFrom, what, why)) return false;
        if (v.find("option_au") && !readTime(v, "option_au", r.optionLastDay, what, why)) return false;
        if (r.optionDays == -1 && (!r.optionLastDay || (r.optionFrom && r.optionLastDay < r.optionFrom)))
            return fail(why, std::string(what) + " : les dates de l'option sont illisibles");
    } else if (v.find("option_jours") || v.find("option_du") || v.find("option_au")) {
        return fail(why, std::string(what) + " : une dur\xC3\xA9" "e d'option sans option");
    }
    if (!plainLine(r.customer) || !plainLine(r.contact) || !plainLine(r.message))
        return fail(why, std::string(what) + " : le client, le contact ou le message porte un caract\xC3\xA8re de contr\xC3\xB4le");
    if (r.customer.find_first_not_of(' ') == std::string::npos) return fail(why, std::string(what) + " : le client est vide");
    out = std::move(r);
    return true;
}

} // namespace licensing::gateway
