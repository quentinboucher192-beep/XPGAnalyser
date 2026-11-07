// =============================================================================
//  licensing/Service.cpp - 1.35.0 : le service des licences, les formats
//  partages (voir Service.hpp et docs/SERVICE-LICENCES.md)
// =============================================================================
#include "Service.hpp"
#include "ServiceAddress.hpp"

#include "../core/Json.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace licensing::service {

namespace json = core::json;

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

bool isLowerHex(std::string_view s) noexcept {
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
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

bool readCount(const json::Value& v, std::string_view key, std::uint64_t& out, std::uint64_t min, std::string_view what, std::string* why) {
    const json::Value* f = v.find(key);
    if (!f || !f->isNumber() || f->asNumber() < static_cast<double>(min) || f->asNumber() > 9007199254740991.0 ||
        f->asNumber() != static_cast<double>(f->asInt()))
        return fail(why, std::string(what) + " : le champ " + std::string(key) + " doit \xC3\xAAtre un entier positif");
    out = static_cast<std::uint64_t>(f->asInt());
    return true;
}

bool readTime(const json::Value& v, std::string_view key, Seconds& out, std::string_view what, std::string* why) {
    std::string t;
    if (!readString(v, key, t, true, 40, what, why)) return false;
    if (!parseIsoTime(t, out)) return fail(why, std::string(what) + " : la date " + std::string(key) + " est illisible (AAAA-MM-JJTHH:MM:SSZ)");
    return true;
}

bool readKey(const json::Value& v, std::string_view key, std::string& out, std::string_view what, std::string* why) {
    if (!readString(v, key, out, true, 64, what, why)) return false;
    if (!isKeyHex(out)) return fail(why, std::string(what) + " : le champ " + std::string(key) + " n'est pas une cl\xC3\xA9 (64 hexad\xC3\xA9" "cimaux)");
    return true;
}

constexpr std::string_view kCrockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

} // namespace

// ============================================================ l'adresse ==
bool validUrl(std::string_view url) noexcept {
    std::string_view rest;
    bool https = false;
    if (url.rfind("https://", 0) == 0) {
        rest = url.substr(8);
        https = true;
    } else if (url.rfind("http://", 0) == 0) {
        rest = url.substr(7);
    } else {
        return false;
    }
    if (rest.empty() || rest.size() > 200 || rest.find('/') != std::string_view::npos || rest.find('@') != std::string_view::npos) return false;
    // l'hote et le port facultatif
    const auto colon = rest.find(':');
    const std::string_view host = rest.substr(0, colon);
    if (host.empty()) return false;
    for (char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    if (colon != std::string_view::npos) {
        const std::string_view port = rest.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    }
    if (!https) return host == "127.0.0.1" || host == "localhost";   // http : ce poste seulement (les essais)
    return true;
}

Address compiledAddress() {
    Address a;
    a.url = kCompiledServiceUrl;
#if defined(XPG_SERVICE_URL)
    if (std::string_view(XPG_SERVICE_URL).size()) a.url = XPG_SERVICE_URL;
#endif
#ifdef XPG_LICENCES_CLE_ESSAI
    // Seulement dans une construction d'essai : le service des essais, sur ce poste.
    if (const char* env = std::getenv("XPG_SERVICE"); env && *env) a.url = env;
#endif
    while (!a.url.empty() && a.url.back() == '/') a.url.pop_back();
    if (!a.url.empty() && !validUrl(a.url)) a.url.clear();
    return a;
}

// ============================================================ les appels signes ==
std::string requestMessage(std::string_view method, std::string_view pathAndQuery, std::string_view date, std::string_view nonce,
                           std::string_view body) {
    std::string m = "XPG-SERVICE-1\n";
    m.append(method).append("\n").append(pathAndQuery).append("\n").append(date).append("\n").append(nonce).append("\n");
    m += crypto::sha256Hex(body);
    return m;
}

std::vector<std::pair<std::string, std::string>> signRequest(std::string_view method, std::string_view pathAndQuery, std::string_view body,
                                                             const crypto::PublicKey& pub, const Signer& signer, Seconds now) {
    std::uint8_t n[16];
    crypto::randomBytes(n, sizeof n);
    const std::string nonce = crypto::hex(n, sizeof n);
    const std::string date = std::to_string(now);
    const crypto::Signature sig = signer(requestMessage(method, pathAndQuery, date, nonce, body));
    return {{kHeaderKey, crypto::hex(pub)}, {kHeaderDate, date}, {kHeaderNonce, nonce}, {kHeaderSignature, crypto::hex(sig)}};
}

bool verifyRequest(std::string_view method, std::string_view pathAndQuery, std::string_view body, std::string_view keyHex, std::string_view date,
                   std::string_view nonce, std::string_view signatureHex, Seconds now, std::string* why) {
    if (!isKeyHex(keyHex)) return fail(why, "la cl\xC3\xA9 de l'appel est illisible");
    if (nonce.size() != 32 || !isLowerHex(nonce)) return fail(why, "le nonce de l'appel est illisible");
    if (date.empty() || date.size() > 12 || !std::all_of(date.begin(), date.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return fail(why, "l'heure de l'appel est illisible");
    const Seconds t = std::strtoll(std::string(date).c_str(), nullptr, 10);
    if (t < now - 600 || t > now + 600) return fail(why, "l'heure de l'appel est trop loin de celle du service");
    crypto::Bytes k, s;
    if (!crypto::unhex(keyHex, k, 32) || !crypto::unhex(signatureHex, s, 64)) return fail(why, "la signature de l'appel est illisible");
    crypto::PublicKey pk{};
    crypto::Signature sg{};
    std::copy(k.begin(), k.end(), pk.begin());
    std::copy(s.begin(), s.end(), sg.begin());
    if (!crypto::verify(pk, requestMessage(method, pathAndQuery, date, nonce, body), sg)) return fail(why, "la signature de l'appel est fausse");
    return true;
}

// ============================================================ les postes ==
std::string deviceId(const crypto::PublicKey& deviceKey) {
    std::string m = "xpg-poste-passerelle/1";   // le meme qu'en 1.34 : un poste garde son identifiant
    m.append(reinterpret_cast<const char*>(deviceKey.data()), deviceKey.size());
    const auto d = crypto::sha256(m);
    return crypto::hex(d.data(), 16);
}

bool isDeviceId(std::string_view s) noexcept { return s.size() == 32 && isLowerHex(s); }
bool isKeyHex(std::string_view s) noexcept { return s.size() == 64 && isLowerHex(s); }

// ============================================================ le code d'activation ==
std::string newActivationCode() {
    std::uint8_t raw[10];
    crypto::randomBytes(raw, sizeof raw);
    std::string norm;
    std::uint32_t acc = 0;
    int bits = 0;
    for (std::uint8_t b : raw) {
        acc = (acc << 8) | b;
        bits += 8;
        while (bits >= 5) {
            norm.push_back(kCrockford[(acc >> (bits - 5)) & 31]);
            bits -= 5;
        }
    }
    crypto::wipe(raw, sizeof raw);
    return formatCode(norm);
}

bool normalizeCode(std::string_view text, std::string& normalized) {
    std::string s;
    for (char c : text) {
        if (c == ' ' || c == '-' || c == '\t') continue;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        s.push_back(c);
    }
    if (s.size() == 19 && s.rfind("XPG", 0) == 0) s.erase(0, 3);
    if (s.size() != 16) return false;
    for (char& c : s) {
        if (c == 'O') c = '0';
        else if (c == 'I' || c == 'L') c = '1';
        if (kCrockford.find(c) == std::string_view::npos) return false;
    }
    normalized = std::move(s);
    return true;
}

std::string formatCode(std::string_view n) {
    if (n.size() != 16) return {};
    std::string out = "XPG";
    for (std::size_t i = 0; i < 16; i += 4) out += "-" + std::string(n.substr(i, 4));
    return out;
}

std::string codeHash(std::string_view normalized) { return crypto::sha256Hex("xpg-code/1|" + std::string(normalized)); }

std::string machineHash(std::string_view machineId) {
    if (machineId.empty()) return {};
    return crypto::sha256Hex("xpg-decouverte/1|" + std::string(machineId)).substr(0, 32);
}

// ============================================================ les documents ==
bool ServiceCert::grants(std::string_view right) const { return std::find(rights.begin(), rights.end(), right) != rights.end(); }

std::string writeCert(const ServiceCert& c) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("cle", c.key);
    v.set("url", c.url);
    v.set("emis", isoTime(c.issuedAt));
    v.set("expire", isoTime(c.expiresAt));
    json::Value r = json::Value::array();
    for (const auto& x : c.rights) r.push(x);
    v.set("droits", r);
    return json::write(v, false);
}

bool parseCert(std::string_view text, ServiceCert& out, std::string* why) {
    constexpr std::string_view what = "le certificat du service";
    json::Value v;
    if (!readObject(text, v, {"version", "cle", "url", "emis", "expire", "droits"}, what, why)) return false;
    ServiceCert c;
    if (!readKey(v, "cle", c.key, what, why) || !readString(v, "url", c.url, true, 220, what, why) || !readTime(v, "emis", c.issuedAt, what, why) ||
        !readTime(v, "expire", c.expiresAt, what, why))
        return false;
    if (!validUrl(c.url)) return fail(why, std::string(what) + " : l'adresse est mal \xC3\xA9" "crite");
    const json::Value* r = v.find("droits");
    if (!r || !r->isArray() || r->items().size() > 16) return fail(why, std::string(what) + " : les droits sont illisibles");
    for (const auto& x : r->items()) {
        if (!x.isString() || x.asString().empty() || x.asString().size() > 32) return fail(why, std::string(what) + " : un droit est illisible");
        c.rights.push_back(x.asString());
    }
    if (c.expiresAt <= c.issuedAt) return fail(why, std::string(what) + " : il expire avant d'\xC3\xAAtre \xC3\xA9mis");
    out = std::move(c);
    return true;
}

std::string writeActivation(const Activation& a) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("licence", a.licenseId);
    v.set("emission", isoTime(a.licenseIssuedAt));
    v.set("poste", a.deviceKey);
    if (!a.station.empty()) v.set("station", a.station);
    v.set("type", a.floating ? "flottante" : "fixe");
    v.set("numero", a.number);
    v.set("emis", isoTime(a.issuedAt));
    v.set("jusqu_a", isoTime(a.until));
    return json::write(v, false);
}

bool parseActivation(std::string_view text, Activation& out, std::string* why) {
    constexpr std::string_view what = "l'activation";
    json::Value v;
    if (!readObject(text, v, {"version", "licence", "emission", "poste", "station", "type", "numero", "emis", "jusqu_a"}, what, why)) return false;
    Activation a;
    std::string type;
    if (!readString(v, "licence", a.licenseId, true, 64, what, why) || !readTime(v, "emission", a.licenseIssuedAt, what, why) ||
        !readKey(v, "poste", a.deviceKey, what, why) || !readString(v, "station", a.station, false, 32, what, why) ||
        !readString(v, "type", type, true, 16, what, why) || !readCount(v, "numero", a.number, 1, what, why) ||
        !readTime(v, "emis", a.issuedAt, what, why) || !readTime(v, "jusqu_a", a.until, what, why))
        return false;
    if (!isLicenseId(a.licenseId)) return fail(why, std::string(what) + " : l'identifiant de la licence est mal \xC3\xA9" "crit");
    if (!a.station.empty() && (a.station.size() != 32 || !isLowerHex(a.station))) return fail(why, std::string(what) + " : la station est mal \xC3\xA9" "crite");
    if (type != "fixe" && type != "flottante") return fail(why, std::string(what) + " : le type \xC2\xAB " + type + " \xC2\xBB est inconnu");
    a.floating = type == "flottante";
    if (a.until <= a.issuedAt) return fail(why, std::string(what) + " : elle finit avant d'\xC3\xAAtre \xC3\xA9mise");
    out = std::move(a);
    return true;
}

std::string writeLease(const Lease& l) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("licence", l.licenseId);
    v.set("poste", l.deviceKey);
    v.set("emis", isoTime(l.issuedAt));
    v.set("jusqu_a", isoTime(l.until));
    return json::write(v, false);
}

bool parseLease(std::string_view text, Lease& out, std::string* why) {
    constexpr std::string_view what = "le jeton de la licence flottante";
    json::Value v;
    if (!readObject(text, v, {"version", "licence", "poste", "emis", "jusqu_a"}, what, why)) return false;
    Lease l;
    if (!readString(v, "licence", l.licenseId, true, 64, what, why) || !readKey(v, "poste", l.deviceKey, what, why) ||
        !readTime(v, "emis", l.issuedAt, what, why) || !readTime(v, "jusqu_a", l.until, what, why))
        return false;
    if (!isLicenseId(l.licenseId)) return fail(why, std::string(what) + " : l'identifiant de la licence est mal \xC3\xA9" "crit");
    if (l.until <= l.issuedAt) return fail(why, std::string(what) + " : il finit avant d'\xC3\xAAtre \xC3\xA9mis");
    out = std::move(l);
    return true;
}

std::string writeTime(const SignedTime& t) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("heure", isoTime(t.time));
    v.set("n", t.nonce);
    return json::write(v, false);
}

bool parseTime(std::string_view text, SignedTime& out, std::string* why) {
    constexpr std::string_view what = "l'heure du service";
    json::Value v;
    if (!readObject(text, v, {"version", "heure", "n"}, what, why)) return false;
    SignedTime t;
    if (!readTime(v, "heure", t.time, what, why) || !readString(v, "n", t.nonce, true, 32, what, why)) return false;
    if (t.nonce.size() != 32 || !isLowerHex(t.nonce)) return fail(why, std::string(what) + " : le nonce est mal \xC3\xA9" "crit");
    out = std::move(t);
    return true;
}

std::string writeTrial(const Trial& t) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("poste", t.deviceKey);
    v.set("machine", t.machine);
    v.set("debut", isoTime(t.start));
    v.set("jours", t.days);
    return json::write(v, false);
}

bool parseTrial(std::string_view text, Trial& out, std::string* why) {
    constexpr std::string_view what = "la D\xC3\xA9" "couverte";
    json::Value v;
    if (!readObject(text, v, {"version", "poste", "machine", "debut", "jours"}, what, why)) return false;
    Trial t;
    std::uint64_t days = 0;
    if (!readKey(v, "poste", t.deviceKey, what, why) || !readString(v, "machine", t.machine, true, 32, what, why) ||
        !readTime(v, "debut", t.start, what, why) || !readCount(v, "jours", days, 1, what, why))
        return false;
    if (t.machine.size() != 32 || !isLowerHex(t.machine)) return fail(why, std::string(what) + " : l'empreinte de la machine est mal \xC3\xA9" "crite");
    if (days > 366) return fail(why, std::string(what) + " : la dur\xC3\xA9" "e est trop longue");
    t.days = static_cast<int>(days);
    out = std::move(t);
    return true;
}

// ============================================================ verifier ==
bool findBlock(std::string_view fileText, DocKind kind, SignedBlock& out, std::string* why) {
    std::vector<SignedBlock> blocks;
    if (!readBlocks(fileText, blocks, why)) return false;
    for (auto& b : blocks)
        if (b.kind == kind) {
            out = std::move(b);
            return true;
        }
    return fail(why, "il n'y a pas de bloc \xC2\xAB " + std::string(docKindKey(kind)) + " \xC2\xBB");
}

bool verifyCert(std::string_view certText, const std::vector<crypto::PublicKey>& publisherKeys, Seconds now, ServiceCert& out, std::string* why) {
    SignedBlock b;
    if (!findBlock(certText, DocKind::ServiceCert, b, why)) return false;
    bool ok = false;
    for (const auto& k : publisherKeys)
        if (b.keyId == keyIdOf(k) && verifyBlock(b, k)) {
            ok = true;
            break;
        }
    if (!ok) return fail(why, "le certificat du service n'est pas sign\xC3\xA9 par une cl\xC3\xA9 de l'\xC3\xA9" "diteur que ce programme croit");
    ServiceCert c;
    if (!parseCert(b.document, c, why)) return false;
    if (now && now > c.expiresAt) return fail(why, "le certificat du service a expir\xC3\xA9 le " + isoDate(c.expiresAt));
    out = std::move(c);
    return true;
}

bool verifyServiceBlock(const SignedBlock& block, DocKind kind, const ServiceCert& cert, std::string_view right, std::string* why) {
    if (block.kind != kind) return fail(why, "ce n'est pas " + docKindLabel(kind));
    if (!cert.grants(right)) return fail(why, "le certificat du service ne donne pas le droit \xC2\xAB " + std::string(right) + " \xC2\xBB");
    crypto::Bytes k;
    if (!crypto::unhex(cert.key, k, 32)) return fail(why, "la cl\xC3\xA9 du certificat est illisible");
    crypto::PublicKey pk{};
    std::copy(k.begin(), k.end(), pk.begin());
    if (block.keyId != keyIdOf(pk) || !verifyBlock(block, pk)) return fail(why, docKindLabel(kind) + " : la signature n'est pas celle de la cl\xC3\xA9 du service certifi\xC3\xA9" "e");
    return true;
}

} // namespace licensing::service
