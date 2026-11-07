// =============================================================================
//  license_admin/Admin.cpp - 1.30.0 : l'outil de l'editeur (voir Admin.hpp)
//  JAMAIS LIVRE A UN CLIENT.
// =============================================================================
#include "Admin.hpp"

#include "../core/Json.hpp"
#include "../deploy/Paths.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace license_admin {

namespace crypto = deploy::crypto;
using core::json::Value;
using namespace licensing;

namespace {

std::string cite(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

// Une chaine C++ en ASCII : chaque octet non ASCII en \xHH, la chaine coupee si un chiffre hexadecimal suit.
std::string cppString(std::string_view s) {
    std::string out = "\"";
    bool lastHex = false;
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        const bool hexDigit = std::isxdigit(c) != 0;
        if (lastHex && hexDigit) out += "\" \"";
        lastHex = false;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += ch;
        } else if (c < 0x20 || c >= 0x7F) {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02X", c);
            out += b;
            lastHex = true;
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

// Le seul bloc d'un genre donne, signe par `key`.
bool readOwnBlock(std::string_view bytes, DocKind kind, const SigningKey& key, SignedBlock& out, std::string* why) {
    std::vector<SignedBlock> blocks;
    if (!readBlocks(bytes, blocks, why)) return false;
    const SignedBlock* found = nullptr;
    for (const auto& b : blocks)
        if (b.kind == kind) {
            if (found) {
                if (why) *why = "le fichier contient deux fois " + docKindLabel(kind);
                return false;
            }
            found = &b;
        }
    if (!found) {
        if (why) *why = "le fichier ne contient pas " + docKindLabel(kind);
        return false;
    }
    if (!verifyBlock(*found, key.publicKey())) {
        if (why) *why = "la signature de " + docKindLabel(kind) + " n'est pas celle de cette cl\xC3\xA9 d'\xC3\xA9" "diteur (" + key.keyId() + ")";
        return false;
    }
    out = *found;
    return true;
}

// Une demande d'activation : signee par la cle du PC qu'elle porte.
bool readRequest(std::string_view bytes, ActivationRequest& out, std::string* why) {
    std::vector<SignedBlock> blocks;
    if (!readBlocks(bytes, blocks, why)) return false;
    if (blocks.size() != 1 || blocks[0].kind != DocKind::Request) {
        if (why) *why = "ce n'est pas une demande d'activation";
        return false;
    }
    if (!parseRequest(blocks[0].document, out, why)) return false;
    crypto::Bytes raw;
    crypto::PublicKey pub{};
    if (!crypto::unhex(out.deviceKey, raw, 32)) {
        if (why) *why = "la cl\xC3\xA9 du PC est illisible";
        return false;
    }
    std::copy(raw.begin(), raw.end(), pub.begin());
    if (!verifyBlock(blocks[0], pub)) {
        if (why) *why = "la demande n'est pas sign\xC3\xA9" "e par la cl\xC3\xA9 du PC qu'elle porte (modifi\xC3\xA9" "e en chemin ?)";
        return false;
    }
    return true;
}

// Les champs "format", "version", "issued_at" d'un document ecrit a la main.
void completeHeader(Value& root, std::string_view format, Seconds now) {
    if (!root.has("format")) root.set("format", std::string(format));
    if (!root.has("version")) root.set("version", kFormatVersion);
    if (!root.has("issued_at")) root.set("issued_at", isoTime(now));
}

bool parseInput(std::string_view json, Value& root, const char* what, std::string* why) {
    std::string err;
    if (!core::json::parse(json, root, &err)) {
        if (why) *why = std::string(what) + " est illisible : " + err;
        return false;
    }
    if (!root.isObject()) {
        if (why) *why = std::string(what) + " n'est pas un objet JSON";
        return false;
    }
    return true;
}

} // namespace

const KeyFileSpec& publisherKeySpec() {
    static const KeyFileSpec spec{
        "xpg-cle-editeur-licences/1", /*machine=*/false,
        std::string(kAdminMarker) + "\n"
            "XPG - la cl\xC3\xA9 de signature de l'\xC3\x89" "DITEUR (licences, registres, r\xC3\xA9vocations). Ed25519.\n"
            "Elle ne quitte JAMAIS le poste Admin : ni copie, ni d\xC3\xA9p\xC3\xB4t, ni installateur.",
        "la cl\xC3\xA9 de l'\xC3\xA9" "diteur"};
    return spec;
}

std::string defaultKeyFolder() {
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    return deploy::joinPath(deploy::joinPath(la && *la ? la : ".", "XPG"), "LicenseAdmin");
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return deploy::joinPath(xdg, "xpg-license-admin");
    const char* home = std::getenv("HOME");
    return deploy::joinPath(deploy::joinPath(home && *home ? home : ".", ".config"), "xpg-license-admin");
#endif
}

std::string publisherKeyPath(const std::string& folder) { return deploy::joinPath(folder, kPublisherKeyFile); }

std::unique_ptr<SigningKey> createPublisherKey(const std::string& folder, const std::string& name, bool test, std::string* why) {
    std::string n = name;
    if (test) n = std::string("CLE D'ESSAI ") + kTestKeyMarker + " - NE PAS LIVRER" + (name.empty() ? "" : " (" + name + ")");
    else if (n.empty()) n = "Editeur XPG";
    if (!test && n.find(kTestKeyMarker) != std::string::npos) {
        if (why) *why = "ce nom est r\xC3\xA9serv\xC3\xA9 aux cl\xC3\xA9s d'essai";
        return nullptr;
    }
    return SigningKey::create(publisherKeyPath(folder), publisherKeySpec(), n, test, why);
}

std::unique_ptr<SigningKey> loadPublisherKey(const std::string& folder, std::string* why) {
    return SigningKey::load(publisherKeyPath(folder), publisherKeySpec(), why);
}

ExportedKey exportedKeyOf(const SigningKey& key) {
    ExportedKey e;
    e.id = key.keyId();
    e.name = key.name();
    e.test = key.test();
    e.key = key.publicKey();
    return e;
}

std::string publisherKeyHeader(const std::vector<ExportedKey>& keys, bool test, Seconds now) {
    const char* file = test ? kTestPublisherHeaderName : kPublisherHeaderName;
    const char* array = test ? "kTestKeys" : "kKeys";
    std::string h;
    h += "// =============================================================================\n";
    if (test) {
        h += "//  licensing/" + std::string(file) + " - LA CLE D'ESSAI DE L'EDITEUR (" + kTestKeyMarker + ")\n";
    } else {
        h += "//  licensing/" + std::string(file) + " - LES CLES PUBLIQUES DE L'EDITEUR, compilees dans\n";
        h += "//  les logiciels clients (API Studio, IHM Studio, License Manager, agent)\n";
    }
    h += "// -----------------------------------------------------------------------------\n";
    h += "//  GENERE par l'outil Admin des licences (XPG-License-Admin exporter-cle) le " + isoTime(now) + ".\n";
    h += "//  Ne le modifie pas a la main : regenere-le sur le poste Admin.\n";
    h += "//  Seules des cles PUBLIQUES sont ici : elles VERIFIENT les licences, les registres\n";
    h += "//  et les revocations ; elles ne permettent pas d'en signer.\n";
    h += "//\n";
    if (test) {
        h += "//  !!! CLE D'ESSAI : elle ne sert qu'au developpement, aux essais et aux captures.\n";
        h += "//  Elle n'est compilee QU'AVEC l'option CMake XPG_LICENCES_CLE_ESSAI (OFF par\n";
        h += "//  defaut) : sans elle, ce fichier est vide pour le compilateur et sa marque\n";
        h += "//  n'entre dans aucun programme. Un programme construit avec l'option n'est PAS\n";
        h += "//  livrable : le controle du paquet (outils/mingw/package_mingw.py,\n";
        h += "//  outils/lib/package.ps1) arrete la construction. !!!\n";
    } else if (keys.empty()) {
        h += "//  AUCUNE CLE : les licences ne sont pas encore actives dans ce programme ; cree\n";
        h += "//  la cle sur le poste Admin (creer-cle), puis exporter-cle vers ce fichier.\n";
        h += "//  En attendant, rien n'est verrouille : toutes les fonctionnalites sont ouvertes\n";
        h += "//  (LicenseStatus::NotConfigured, licensing/Licensing.hpp).\n";
    }
    if (!keys.empty()) {
        if (test) h += "//\n";
        h += "//  " + std::to_string(keys.size()) + (keys.size() > 1 ? " cles" : " cle") + " (identifiant, empreinte SHA-256) :\n";
        for (const auto& k : keys) h += "//    " + k.id + "  " + publicKeyFingerprint(k.key, 0) + "\n";
    }
    h += "//\n";
    if (test) {
        h += "//  Les vraies cles ne vont jamais ici : " + std::string(kPublisherHeaderName) + ".\n";
    } else {
        h += "//  Plusieurs cles (une rotation) : exporter-cle --garder ajoute la cle du poste a\n";
        h += "//  celles deja ecrites ici ; les clients croient chacune. Une cle retiree s'enleve\n";
        h += "//  (exporter-cle sans --garder) : ses licences tombent. Les cles d'ESSAI ne vont\n";
        h += "//  jamais ici : " + std::string(kTestPublisherHeaderName) + " (option CMake XPG_LICENCES_CLE_ESSAI).\n";
    }
    h += "// =============================================================================\n";
    h += "#pragma once\n\n#include \"PublisherKeyEntry.hpp\"\n\n";
    if (test) h += "#ifdef XPG_LICENCES_CLE_ESSAI\n\n";
    h += "#include <array>\n\nnamespace licensing::publisher {\n\n";
    h += "inline constexpr std::array<KeyEntry, " + std::to_string(keys.size()) + "> " + array;
    if (keys.empty()) {
        h += "{};\n";
    } else {
        h += "{{\n";
        for (const auto& k : keys) {
            h += "    {" + cppString(k.id) + ", " + cppString(k.name) + ", " + (k.test ? "true" : "false") + ",\n     {";
            for (std::size_t i = 0; i < k.key.size(); ++i) {
                char b[8];
                std::snprintf(b, sizeof b, "0x%02x", k.key[i]);
                if (i) h += i % 8 == 0 ? ",\n      " : ", ";
                h += b;
            }
            h += "}},\n";
        }
        h += "}};\n";
    }
    h += "\n} // namespace licensing::publisher\n";
    if (test) h += "\n#endif // XPG_LICENCES_CLE_ESSAI\n";
    return h;
}

std::string publisherKeyHeader(const SigningKey& key, Seconds now) { return publisherKeyHeader({exportedKeyOf(key)}, key.test(), now); }

namespace {

// Un petit lecteur du C++ que publisherKeyHeader ecrit (et rien d'autre).
class HeaderReader {
public:
    explicit HeaderReader(std::string_view t) : t_(t) {}
    std::size_t pos{0};
    void skip() {
        while (pos < t_.size() && std::isspace(static_cast<unsigned char>(t_[pos]))) ++pos;
    }
    bool eat(std::string_view tok) {
        skip();
        if (t_.substr(pos, tok.size()) != tok) return false;
        pos += tok.size();
        return true;
    }
    // Une chaine C++ (les morceaux "..." "..." mis bout a bout, \xHH, \" et \\).
    bool string(std::string& out) {
        out.clear();
        skip();
        if (pos >= t_.size() || t_[pos] != '"') return false;
        while (pos < t_.size() && t_[pos] == '"') {
            ++pos;
            while (pos < t_.size() && t_[pos] != '"') {
                char c = t_[pos++];
                if (c == '\n') return false;
                if (c == '\\') {
                    if (pos >= t_.size()) return false;
                    const char e = t_[pos++];
                    if (e == 'x') {
                        int v = 0, n = 0;
                        while (n < 2 && pos < t_.size() && std::isxdigit(static_cast<unsigned char>(t_[pos]))) {
                            const char d = t_[pos++];
                            v = v * 16 + (std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (std::tolower(static_cast<unsigned char>(d)) - 'a' + 10));
                            ++n;
                        }
                        if (n == 0) return false;
                        c = static_cast<char>(v);
                    } else if (e == '"' || e == '\\') {
                        c = e;
                    } else {
                        return false;
                    }
                }
                out += c;
            }
            if (pos >= t_.size()) return false;
            ++pos;   // le " de fin
            const std::size_t save = pos;
            skip();
            if (pos >= t_.size() || t_[pos] != '"') pos = save;
        }
        return true;
    }
    bool byte(std::uint8_t& out) {
        skip();
        if (t_.substr(pos, 2) != "0x" || pos + 4 > t_.size()) return false;
        int v = 0;
        for (int i = 0; i < 2; ++i) {
            const char d = t_[pos + 2 + static_cast<std::size_t>(i)];
            if (!std::isxdigit(static_cast<unsigned char>(d))) return false;
            v = v * 16 + (std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (std::tolower(static_cast<unsigned char>(d)) - 'a' + 10));
        }
        pos += 4;
        out = static_cast<std::uint8_t>(v);
        return true;
    }

private:
    std::string_view t_;
};

std::string baseName(const std::string& path) {
    const auto cut = path.find_last_of("/\\");
    std::string b = cut == std::string::npos ? path : path.substr(cut + 1);
    for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return b;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool readPublisherKeyHeader(std::string_view text, std::vector<ExportedKey>& keys, bool& test, std::string* why) {
    keys.clear();
    const auto fail = [&](const std::string& t) {
        if (why) *why = t;
        return false;
    };
    // Le tableau : "std::array<KeyEntry, N> kKeys" (ou kTestKeys).
    const std::string_view decl = "std::array<KeyEntry, ";
    const std::size_t at = text.find(decl);
    if (at == std::string_view::npos) return fail("ce fichier n'a pas \xC3\xA9t\xC3\xA9 \xC3\xA9" "crit par exporter-cle (pas de liste std::array<KeyEntry, N>)");
    HeaderReader r(text);
    r.pos = at + decl.size();
    std::size_t declared = 0;
    {
        r.skip();
        const std::size_t from = r.pos;
        while (r.pos < text.size() && std::isdigit(static_cast<unsigned char>(text[r.pos]))) declared = declared * 10 + static_cast<std::size_t>(text[r.pos++] - '0');
        if (r.pos == from || declared > 64) return fail("la taille de la liste des cl\xC3\xA9s est illisible");
    }
    if (!r.eat(">")) return fail("la liste des cl\xC3\xA9s est illisible");
    if (r.eat("kTestKeys")) test = true;
    else if (r.eat("kKeys")) test = false;
    else return fail("la liste n'est ni kKeys ni kTestKeys");
    if (declared == 0) {
        if (!r.eat("{}") || !r.eat(";")) return fail("la liste vide est illisible");
        return true;
    }
    if (!r.eat("{{")) return fail("la liste des cl\xC3\xA9s est illisible");
    for (std::size_t n = 0; n < declared; ++n) {
        ExportedKey k;
        const std::string where = "la cl\xC3\xA9 n\xC2\xB0 " + std::to_string(n + 1);
        if (!r.eat("{") || !r.string(k.id) || !r.eat(",") || !r.string(k.name) || !r.eat(",")) return fail(where + " est illisible");
        if (r.eat("true")) k.test = true;
        else if (r.eat("false")) k.test = false;
        else return fail(where + " : ni true ni false");
        if (!r.eat(",") || !r.eat("{")) return fail(where + " est illisible");
        for (std::size_t i = 0; i < k.key.size(); ++i)
            if ((i && !r.eat(",")) || !r.byte(k.key[i])) return fail(where + " : la cl\xC3\xA9 publique n'a pas 32 octets lisibles");
        if (!r.eat("}") || !r.eat("}") || !r.eat(",")) return fail(where + " est illisible");
        if (keyIdOf(k.key) != k.id) return fail(where + " : l'identifiant " + k.id + " n'est pas celui de sa cl\xC3\xA9 (" + keyIdOf(k.key) + ")");
        if (k.test != test) return fail(where + (k.test ? " est une cl\xC3\xA9 d'essai dans la liste des vraies cl\xC3\xA9s" : " est une vraie cl\xC3\xA9 dans la liste des cl\xC3\xA9s d'essai"));
        keys.push_back(std::move(k));
    }
    if (!r.eat("}}") || !r.eat(";")) return fail("la liste a plus de cl\xC3\xA9s que sa taille (" + std::to_string(declared) + ")");
    return true;
}

bool exportPublisherKeys(const SigningKey* key, std::string_view existing, const std::string& outName, bool keep, bool empty, Seconds now,
                         std::string& out, std::string* why, std::string* summary) {
    const auto fail = [&](const std::string& t) {
        if (why) *why = t;
        return false;
    };
    const std::string name = baseName(outName);
    const bool toReal = name == lower(kPublisherHeaderName);
    const bool toTest = name == lower(kTestPublisherHeaderName);
    if (empty) {
        if (keep) return fail("--vide et --garder ensemble : choisis l'un ou l'autre");
        if (toTest) return fail("--vide \xC3\xA9" "crit " + std::string(kPublisherHeaderName) + " (aucune cl\xC3\xA9), pas " + kTestPublisherHeaderName);
        out = publisherKeyHeader(std::vector<ExportedKey>{}, false, now);
        if (summary) *summary = "aucune cl\xC3\xA9 : les licences ne seront pas actives dans un programme compil\xC3\xA9 avec ce fichier (tout y sera ouvert)\n";
        return true;
    }
    if (!key) return fail("aucune cl\xC3\xA9 \xC3\xA0 exporter");
    const ExportedKey mine = exportedKeyOf(*key);
    if (mine.test && toReal)
        return fail("une cl\xC3\xA9 d'ESSAI ne va jamais dans " + std::string(kPublisherHeaderName) + " (elle serait compil\xC3\xA9" "e dans les clients) : --sortie src/licensing/"
                    + kTestPublisherHeaderName);
    if (!mine.test && toTest)
        return fail("une vraie cl\xC3\xA9 ne va pas dans " + std::string(kTestPublisherHeaderName) + " : --sortie src/licensing/" + kPublisherHeaderName);
    std::vector<ExportedKey> keys, before;
    bool beforeTest = false;
    std::string note;
    if (!existing.empty()) {
        std::string w;
        if (readPublisherKeyHeader(existing, before, beforeTest, &w)) {
            if (keep && beforeTest != mine.test)
                return fail(std::string("le fichier actuel liste des ") + (beforeTest ? "cl\xC3\xA9s d'essai" : "vraies cl\xC3\xA9s") + " : une " + (mine.test ? "cl\xC3\xA9 d'essai" : "vraie cl\xC3\xA9")
                            + " ne se garde pas \xC3\xA0 c\xC3\xB4t\xC3\xA9");
        } else if (keep) {
            return fail("--garder : " + w);
        }
    } else if (keep) {
        note = "  (--garder : le fichier n'existait pas, rien \xC3\xA0 garder)\n";
    }
    bool already = false;
    if (keep)
        for (const auto& k : before) {
            if (k.id == mine.id) already = true;
            keys.push_back(k);
        }
    if (!already) keys.push_back(mine);
    out = publisherKeyHeader(keys, mine.test, now);
    if (summary) {
        std::string s = std::to_string(keys.size()) + (keys.size() > 1 ? " cl\xC3\xA9s" : " cl\xC3\xA9") + (mine.test ? " d'ESSAI" : "") + " :\n";
        for (const auto& k : keys)
            s += "  " + k.id + (k.id == mine.id ? (already ? " (d\xC3\xA9j\xC3\xA0 l\xC3\xA0)" : " (celle de ce poste)") : " (gard\xC3\xA9" "e)") + "\n";
        if (!keep)
            for (const auto& k : before)
                if (k.id != mine.id) s += "  " + k.id + " : retir\xC3\xA9" "e (sans --garder) ; ses licences ne seront plus reconnues\n";
        *summary = s + note;
    }
    return true;
}

bool signRegistry(const SigningKey& key, std::string_view inputJson, Seconds now, std::string& out, std::string* why) {
    Value root;
    if (!parseInput(inputJson, root, "le registre \xC3\xA0 signer", why)) return false;
    completeHeader(root, kRegistryFormat, now);
    Registry r;
    if (!parseRegistry(core::json::write(root, true), r, why)) return false;
    for (const auto& e : r.editions)
        for (const auto& g : e.features)
            if (const FeatureDef* d = r.feature(g.id); d && d->retired && !e.retired) {
                if (why) *why = "l'\xC3\xA9" "dition " + cite(e.id) + " accorde " + cite(g.id) + ", qui est retir\xC3\xA9" "e";
                return false;
            }
    const std::string text = writeRegistry(r);
    Registry back;
    if (!parseRegistry(text, back, why)) return false;   // ce qui part se relit
    out = writeBlock(signBlock(DocKind::Registry, text, key.publicKey(), [&](std::string_view m) { return key.sign(m); }));
    return true;
}

namespace {

// L'empreinte courte d'une cle de PC (64 hexadecimaux) ; vide si elle est illisible.
std::string fingerprintOfHex(const std::string& hex) {
    crypto::Bytes raw;
    crypto::PublicKey pub{};
    if (!crypto::unhex(hex, raw, 32)) return {};
    std::copy(raw.begin(), raw.end(), pub.begin());
    return publicKeyFingerprint(pub);
}

} // namespace

bool signRevocations(const SigningKey& key, std::string_view inputJson, Seconds now, std::string& out, std::string* why,
                     std::vector<ActivationRecord>* records) {
    Value root;
    if (!parseInput(inputJson, root, "la liste de r\xC3\xA9vocation \xC3\xA0 signer", why)) return false;
    completeHeader(root, kRevocationsFormat, now);
    if (Value* arr = root.find("revoked"); arr && arr->isArray())
        for (auto& item : arr->items())
            if (item.isObject() && !item.has("revoked_at")) item.set("revoked_at", isoTime(now));
    RevocationList r;
    if (!parseRevocations(core::json::write(root, true), r, why)) return false;
    const std::string text = writeRevocations(r);
    RevocationList back;
    if (!parseRevocations(text, back, why)) return false;
    out = writeBlock(signBlock(DocKind::Revocations, text, key.publicKey(), [&](std::string_view m) { return key.sign(m); }));
    if (records)
        for (const auto& x : r.revoked) {
            ActivationRecord a;
            a.kind = "revocation";
            a.at = now;
            a.licenseId = x.licenseId;
            a.deviceKey = x.device;
            a.fingerprint = x.device.empty() ? std::string() : fingerprintOfHex(x.device);
            a.revokedAt = x.revokedAt;
            a.reason = x.reason;
            a.issuedBefore = x.issuedBefore;   // 1.32.0 : une emission precise (les plus recentes restent)
            records->push_back(std::move(a));
        }
    return true;
}

bool signLicense(const SigningKey& key, const LicenseInputs& in, Seconds now, std::string& out, std::string* why, std::string* summary,
                 std::vector<ActivationRecord>* records) {
    const auto fail = [&](const std::string& text) {
        if (why) *why = text;
        return false;
    };
    std::string err;
    // ---- le registre (signe par cette cle), et les revocations.
    SignedBlock regB, revB;
    Registry reg;
    RevocationList rev;
    if (in.registry.empty()) return fail("le registre sign\xC3\xA9 est obligatoire (--registre)");
    if (!readOwnBlock(in.registry, DocKind::Registry, key, regB, &err)) return fail("registre : " + err);
    if (!parseRegistry(regB.document, reg, &err)) return fail(err);
    const bool hasRev = !in.revocations.empty();
    if (hasRev) {
        if (!readOwnBlock(in.revocations, DocKind::Revocations, key, revB, &err)) return fail("r\xC3\xA9vocations : " + err);
        if (!parseRevocations(revB.document, rev, &err)) return fail(err);
    }
    // ---- les parametres.
    Value p;
    if (!parseInput(in.params, p, "le fichier de param\xC3\xA8tres", why)) return false;
    static const std::set<std::string> known = {"license_id", "customer", "edition", "type", "max_devices", "features", "issued_at",
                                                "expires_at", "duration_days", "offline_grace_days"};
    for (const auto& [k, v] : p.members()) {
        (void)v;
        if (!known.count(k)) return fail("param\xC3\xA8tre inconnu : " + cite(k) + " (attendus : license_id, customer, edition, type, max_devices, features, issued_at, expires_at, duration_days, offline_grace_days)");
    }
    License l;
    l.licenseId = p.str("license_id");
    l.customer = p.str("customer");
    l.edition = p.str("edition");
    if (!isLicenseId(l.licenseId)) return fail("license_id manque ou n'est pas valide ([A-Za-z0-9._-], 64 au plus)");
    if (l.customer.empty()) return fail("customer (le nom du client) manque");
    const EditionDef* ed = reg.edition(l.edition);
    if (!ed) return fail("l'\xC3\xA9" "dition " + cite(l.edition) + " n'est pas dans le registre n\xC2\xB0 " + std::to_string(reg.number));
    if (ed->retired) return fail("l'\xC3\xA9" "dition " + cite(l.edition) + " est retir\xC3\xA9" "e du registre");
    l.type = ed->type;
    if (p.has("type") && !parseLicenseType(p.str("type"), l.type)) return fail("type inconnu : " + cite(p.str("type")) + " (perpetual, temporary, evaluation)");
    if (l.type == LicenseType::Floating)
        return fail("les licences flottantes ne sont pas encore possibles : elles demandent le service de licences en ligne (pas encore h\xC3\xA9" "berg\xC3\xA9)");
    const auto intParam = [&](const char* name, int def, int lo, int hi, int& outv) {
        const Value* v = p.find(name);
        if (!v) {
            outv = def;
            return true;
        }
        const double d = v->asNumber(-1e300);
        if (!v->isNumber() || d != std::floor(d) || d < lo || d > hi) return false;
        outv = static_cast<int>(d);
        return true;
    };
    if (!intParam("max_devices", ed->maxDevices, 1, 100000, l.maxDevices)) return fail("max_devices n'est pas un entier entre 1 et 100000");
    if (!intParam("offline_grace_days", ed->offlineGraceDays, 0, 3660, l.offlineGraceDays)) return fail("offline_grace_days n'est pas un entier entre 0 et 3660");
    int duration = 0;
    if (!intParam("duration_days", ed->durationDays, 0, 36600, duration)) return fail("duration_days n'est pas un entier entre 0 et 36600");
    l.issuedAt = now;
    if (p.has("issued_at") && !parseIsoTime(p.str("issued_at"), l.issuedAt)) return fail("issued_at n'est pas une date (AAAA-MM-JJTHH:MM:SSZ)");
    if (p.has("expires_at")) {
        if (!parseIsoTime(p.str("expires_at"), l.expiresAt)) return fail("expires_at n'est pas une date (AAAA-MM-JJTHH:MM:SSZ)");
    } else if (l.type != LicenseType::Perpetual && duration > 0) {
        l.expiresAt = l.issuedAt + static_cast<Seconds>(duration) * kDay;
    }
    if (l.type == LicenseType::Perpetual && l.expiresAt != 0) return fail("une licence perp\xC3\xA9tuelle n'a pas de date de fin (enl\xC3\xA8ve expires_at)");
    if (l.type != LicenseType::Perpetual && l.expiresAt == 0) return fail("une licence " + licenseTypeLabel(l.type) + " demande expires_at ou duration_days");
    // ---- les fonctionnalites : celles de l'edition, ou la liste donnee.
    if (const Value* f = p.find("features")) {
        if (!f->isArray()) return fail("features doit \xC3\xAAtre une liste");
        for (const auto& item : f->items()) {
            FeatureGrant g;
            if (item.isString()) {
                g.id = item.asString();
            } else if (item.isObject()) {
                g.id = item.str("id");
                if (const Value* lim = item.find("limits")) {
                    if (!lim->isObject()) return fail("les limites de " + cite(g.id) + " doivent \xC3\xAAtre un objet");
                    for (const auto& [k, v] : lim->members()) {
                        const double d = v.asNumber(-1e300);
                        if (!v.isNumber() || d != std::floor(d)) return fail("la limite " + cite(k) + " de " + cite(g.id) + " n'est pas un entier");
                        g.limits.emplace_back(k, static_cast<std::int64_t>(d));
                    }
                }
            } else {
                return fail("chaque \xC3\xA9l\xC3\xA9ment de features est un identifiant, ou un objet {\"id\", \"limits\"}");
            }
            l.features.push_back(std::move(g));
        }
    } else {
        l.features = ed->features;
    }
    normalizeGrants(l.features);
    for (const auto& g : l.features) {
        const FeatureDef* d = reg.feature(g.id);
        if (!d) return fail("la fonctionnalit\xC3\xA9 " + cite(g.id) + " n'est pas dans le registre n\xC2\xB0 " + std::to_string(reg.number));
        if (d->retired) return fail("la fonctionnalit\xC3\xA9 " + cite(g.id) + " est retir\xC3\xA9" "e du registre");
    }
    // ---- les PC : ceux de la licence a etendre, puis ceux des demandes.
    if (!in.existing.empty()) {
        SignedBlock b;
        License old;
        if (!readOwnBlock(in.existing, DocKind::License, key, b, &err)) return fail("licence \xC3\xA0 \xC3\xA9tendre : " + err);
        if (!parseLicense(b.document, old, &err)) return fail(err);
        if (old.licenseId != l.licenseId)
            return fail("la licence \xC3\xA0 \xC3\xA9tendre est " + cite(old.licenseId) + ", pas " + cite(l.licenseId));
        l.devices = old.devices;
    }
    std::string added;
    // 1.33.0 : retirer des PC de la licence etendue (un PC remplace, un PC rendu).
    if (!in.dropDevices.empty() && in.existing.empty()) return fail("retirer un PC demande la licence \xC3\xA0 \xC3\xA9tendre (--licence)");
    for (const auto& gone : in.dropDevices) {
        auto it = std::find_if(l.devices.begin(), l.devices.end(), [&](const BoundDevice& x) { return x.key == gone; });
        if (it == l.devices.end()) return fail("le PC \xC3\xA0 retirer (" + fingerprintOfHex(gone) + ") n'est pas li\xC3\xA9 \xC3\xA0 la licence " + cite(l.licenseId));
        added += "  PC retir\xC3\xA9 : " + (it->name.empty() ? std::string("(sans nom)") : it->name) + " - " + fingerprintOfHex(it->key) + "\n";
        l.devices.erase(it);
    }
    std::vector<ActivationRequest> asked;   // pour le registre des activations : le logiciel, la date de chaque demande
    for (std::size_t i = 0; i < in.requests.size(); ++i) {
        ActivationRequest q;
        if (!readRequest(in.requests[i], q, &err)) return fail("demande n\xC2\xB0 " + std::to_string(i + 1) + " : " + err);
        if (!q.licenseId.empty() && q.licenseId != l.licenseId)
            return fail("demande n\xC2\xB0 " + std::to_string(i + 1) + " : ce PC demande la licence " + cite(q.licenseId) + ", pas " + cite(l.licenseId));
        asked.push_back(q);
        if (auto bound = std::find_if(l.devices.begin(), l.devices.end(), [&](const BoundDevice& x) { return x.key == q.deviceKey; });
            bound != l.devices.end()) {
            // deja lie (une reactivation). 1.32.0 (H2) : la meme cle depuis une AUTRE installation de Windows - la cle du
            // PC a ete recopiee : refuse (revoque ce PC, puis lie la nouvelle demande).
            if (!bound->station.empty() && !q.station.empty() && bound->station != q.station)
                return fail("demande n\xC2\xB0 " + std::to_string(i + 1) + " : ce PC (" + (q.deviceName.empty() ? std::string("sans nom") : q.deviceName)
                            + ") est d\xC3\xA9j\xC3\xA0 li\xC3\xA9 \xC3\xA0 la licence depuis une AUTRE installation de Windows (l'empreinte du poste diff\xC3\xA8re) : "
                              "sa cl\xC3\xA9 a sans doute \xC3\xA9t\xC3\xA9 recopi\xC3\xA9" "e. Refus\xC3\xA9 ; r\xC3\xA9voque ce PC si besoin.");
            if (bound->station.empty()) bound->station = q.station;   // une licence d'avant la 1.32.0 : l'empreinte s'ajoute
            continue;
        }
        BoundDevice d;
        d.key = q.deviceKey;
        d.name = q.deviceName;
        d.activatedAt = now;
        d.station = q.station;   // 1.32.0 (H2) : le client compare avec la sienne (vide : rien a comparer)
        l.devices.push_back(d);
        crypto::Bytes raw;
        crypto::PublicKey pub{};
        if (crypto::unhex(q.deviceKey, raw, 32)) std::copy(raw.begin(), raw.end(), pub.begin());
        added += "  PC ajout\xC3\xA9 : " + (q.deviceName.empty() ? std::string("(sans nom)") : q.deviceName) + " - " + publicKeyFingerprint(pub) + "\n";
    }
    if (l.devices.empty()) return fail("aucun PC : donne au moins une demande d'activation (--demande)");
    if (static_cast<int>(l.devices.size()) > l.maxDevices)
        return fail(std::to_string(l.devices.size()) + " PC pour " + std::to_string(l.maxDevices) + " autoris\xC3\xA9s (max_devices)");
    if (hasRev) {
        for (const auto& d : l.devices)
            if (const Revocation* r = rev.find(l.licenseId, d.key, l.issuedAt))
                return fail("la licence " + cite(l.licenseId) + (r->device.empty() ? "" : " (pour un de ces PC)") + " est dans la liste de r\xC3\xA9vocation n\xC2\xB0 "
                            + std::to_string(rev.number) + (r->device.empty() ? std::string() : " : retire ce PC de la licence (--retirer-pc " + d.key + ")"));
    }
    l.manifest = reg.number;
    l.nonce = newNonce();
    const std::string text = writeLicense(l);
    License back;
    if (!parseLicense(text, back, &err)) return fail(err);   // ce qui part se relit
    if (!checkAgainstRegistry(back, reg, &err)) return fail(err);
    out = writeBlock(signBlock(DocKind::License, text, key.publicKey(), [&](std::string_view m) { return key.sign(m); }));
    out += writeBlock(regB);
    if (hasRev) out += writeBlock(revB);
    if (records)
        for (const auto& d : l.devices) {
            ActivationRecord a;
            a.kind = "licence";
            a.at = now;
            a.licenseId = l.licenseId;
            a.customer = l.customer;
            a.edition = l.edition;
            a.type = licenseTypeKey(l.type);
            a.expiresAt = l.expiresAt;
            a.deviceKey = d.key;
            a.deviceName = d.name;
            a.fingerprint = fingerprintOfHex(d.key);
            for (const auto& q : asked)
                if (q.deviceKey == d.key) {
                    a.software = q.software;
                    a.requestedAt = q.createdAt;
                }
            a.activatedAt = d.activatedAt;
            a.signedAt = now;
            a.issuedAt = l.issuedAt;   // 1.32.0 : pour une revocation d'une emission precise (issued_before)
            a.extension = !in.existing.empty();
            records->push_back(std::move(a));
        }
    if (summary) {
        *summary = "Licence " + l.licenseId + " - " + l.customer + " - " + l.edition + " (" + licenseTypeLabel(l.type) + ")\n";
        *summary += "  " + std::to_string(l.features.size()) + " fonctionnalit\xC3\xA9s, " + std::to_string(l.devices.size()) + " PC li\xC3\xA9(s) sur "
                    + std::to_string(l.maxDevices) + "\n";
        *summary += "  \xC3\xA9mise le " + isoTime(l.issuedAt) + (l.expiresAt ? ", fin le " + isoTime(l.expiresAt) : std::string(", sans fin"))
                    + (l.type == LicenseType::Temporary ? ", gr\xC3\xA2" "ce " + std::to_string(l.offlineGraceDays) + " j" : std::string()) + "\n";
        *summary += "  registre n\xC2\xB0 " + std::to_string(reg.number) + (hasRev ? ", r\xC3\xA9vocations n\xC2\xB0 " + std::to_string(rev.number) : std::string()) + " joints\n";
        *summary += added;
    }
    return true;
}

bool describeFile(std::string_view bytes, const SigningKey* key, std::string& text) {
    std::vector<SignedBlock> blocks;
    std::string err;
    if (!readBlocks(bytes, blocks, &err)) {
        text = "REFUS\xC3\x89 : " + err + "\n";
        return false;
    }
    bool allOk = true;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const SignedBlock& b = blocks[i];
        text += std::to_string(i + 1) + ". " + docKindKey(b.kind) + " (" + std::to_string(b.document.size()) + " octets, cl\xC3\xA9 " + b.keyId + ")\n";
        bool sigOk = false;
        std::string what;
        if (b.kind == DocKind::Request) {
            ActivationRequest q;
            if (readRequest(writeBlock(b), q, &err)) {
                sigOk = true;
                what = "PC " + (q.deviceName.empty() ? std::string("(sans nom)") : cite(q.deviceName)) + ", cl\xC3\xA9 " + q.deviceKey.substr(0, 16) + "..., demand\xC3\xA9" "e le "
                       + isoTime(q.createdAt) + (q.licenseId.empty() ? "" : ", licence " + q.licenseId) + (q.software.empty() ? "" : ", " + q.software);
            } else {
                what = err;
            }
        } else {
            sigOk = key && verifyBlock(b, key->publicKey());
            if (b.kind == DocKind::License) {
                License l;
                what = parseLicense(b.document, l, &err)
                           ? l.licenseId + " - " + l.customer + " - " + l.edition + " - " + licenseTypeKey(l.type) + " - " + std::to_string(l.devices.size()) + "/"
                                 + std::to_string(l.maxDevices) + " PC - fin " + (l.expiresAt ? isoTime(l.expiresAt) : std::string("jamais")) + " - registre n\xC2\xB0 "
                                 + std::to_string(l.manifest)
                           : err;
            } else if (b.kind == DocKind::Registry) {
                Registry r;
                what = parseRegistry(b.document, r, &err) ? "n\xC2\xB0 " + std::to_string(r.number) + ", " + std::to_string(r.editions.size()) + " \xC3\xA9" "ditions, "
                                                                + std::to_string(r.features.size()) + " fonctionnalit\xC3\xA9s, sans licence : " + r.unlicensedEdition
                                                          : err;
            } else {
                RevocationList r;
                what = parseRevocations(b.document, r, &err) ? "n\xC2\xB0 " + std::to_string(r.number) + ", " + std::to_string(r.revoked.size()) + " r\xC3\xA9vocation(s)" : err;
            }
        }
        text += std::string("   signature : ") + (sigOk ? "bonne" : key || b.kind == DocKind::Request ? "FAUSSE" : "non v\xC3\xA9rifi\xC3\xA9" "e (pas de cl\xC3\xA9 d'\xC3\xA9" "diteur)") + "\n";
        text += "   " + what + "\n";
        if (!sigOk && (key || b.kind == DocKind::Request)) allOk = false;
    }
    return allOk;
}

// ============================================================ le registre des activations ==
std::string activationsPath(const std::string& folder) { return deploy::joinPath(folder, kActivationsFile); }

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

Seconds timeField(const Value& v, std::string_view key) {
    Seconds t = 0;
    if (!parseIsoTime(v.str(key), t)) t = 0;
    return t;
}

// Le nombre de caracteres (pas d'octets) d'un texte UTF-8.
std::size_t width(const std::string& s) {
    std::size_t n = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// Au plus `max` caracteres (le dernier : une ellipse).
std::string clip(const std::string& s, std::size_t max) {
    if (width(s) <= max) return s;
    std::string out;
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool lead = (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
        if (lead && ++n > max - 1) break;
        out += s[i];
    }
    return out + "\xE2\x80\xA6";
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string dateOr(Seconds t, const char* none) { return t ? isoDate(t) : std::string(none); }

std::string stateLabel(const std::string& state) {
    if (state == "active") return "activ\xC3\xA9";
    if (state == "revoque") return "r\xC3\xA9voqu\xC3\xA9";
    if (state == "expire") return "expir\xC3\xA9";
    return "demande";
}

// Les cellules d'un PC, dans l'ordre des colonnes.
std::vector<std::string> cellsOf(const KnownDevice& d) {
    const bool licensed = d.state != "demande";
    return {d.name.empty() ? std::string("(sans nom)") : d.name,
            d.fingerprint,
            d.customer,
            d.licenseId,
            d.edition,
            d.software,
            dateOr(d.requestedAt, "-"),
            dateOr(d.activatedAt, "-"),
            licensed ? dateOr(d.expiresAt, "jamais") : std::string("-"),
            stateLabel(d.state)};
}

const std::vector<std::string>& columns() {
    static const std::vector<std::string> c = {"PC", "Empreinte", "Client", "Licence", "\xC3\x89" "dition", "Logiciel", "Demand\xC3\xA9 le", "Activ\xC3\xA9 le",
                                               "Expire le", "\xC3\x89tat"};
    return c;
}

} // namespace

bool requestRecord(std::string_view requestBytes, Seconds now, ActivationRecord& out, std::string* why) {
    ActivationRequest q;
    if (!readRequest(requestBytes, q, why)) return false;
    out = ActivationRecord{};
    out.kind = "demande";
    out.at = now;
    out.licenseId = q.licenseId;
    out.deviceKey = q.deviceKey;
    out.deviceName = q.deviceName;
    out.fingerprint = fingerprintOfHex(q.deviceKey);
    out.software = q.software;
    out.requestedAt = q.createdAt;
    return true;
}

bool appendActivations(const std::string& folder, const std::vector<ActivationRecord>& records, std::string* why) {
    if (records.empty()) return true;
    std::string text;
    for (const auto& a : records) {
        Value v = Value::object();
        v.set("kind", a.kind);
        v.set("at", isoTime(a.at));
        const auto str = [&](const char* k, const std::string& x) {
            if (!x.empty()) v.set(k, x);
        };
        const auto stamp = [&](const char* k, Seconds t) {
            if (t) v.set(k, isoTime(t));
        };
        str("license_id", a.licenseId);
        str("customer", a.customer);
        str("edition", a.edition);
        str("type", a.type);
        if (a.kind == "licence") v.set("expires_at", a.expiresAt ? Value(isoTime(a.expiresAt)) : Value());
        str("device_key", a.deviceKey);
        str("device_name", a.deviceName);
        str("device_fingerprint", a.fingerprint);
        str("software", a.software);
        stamp("requested_at", a.requestedAt);
        stamp("activated_at", a.activatedAt);
        stamp("signed_at", a.signedAt);
        if (a.kind == "licence") v.set("extension", a.extension);
        stamp("issued_at", a.issuedAt);           // 1.32.0
        stamp("revoked_at", a.revokedAt);
        str("reason", a.reason);
        stamp("issued_before", a.issuedBefore);   // 1.32.0
        text += core::json::write(v, false) + "\n";
    }
    std::error_code ec;
    std::filesystem::create_directories(pathOf(folder), ec);
    const std::string path = activationsPath(folder);
    std::ofstream out(pathOf(path), std::ios::binary | std::ios::app);   // a la fin : jamais reecrit
    if (!out) {
        if (why) *why = "impossible d'\xC3\xA9" "crire " + path;
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        if (why) *why = "\xC3\xA9" "criture incompl\xC3\xA8te : " + path;
        return false;
    }
    return true;
}

bool readActivations(const std::string& folder, std::vector<ActivationRecord>& out, int* skipped, std::string* why) {
    out.clear();
    if (skipped) *skipped = 0;
    const std::string path = activationsPath(folder);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(pathOf(path), ec)) return true;
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, why, 256u * 1024u * 1024u)) return false;
    const std::string text(raw.begin(), raw.end());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Value v;
        const std::string kind = core::json::parse(line, v) && v.isObject() ? v.str("kind") : std::string();
        if (kind != "licence" && kind != "demande" && kind != "revocation") {
            if (skipped) ++*skipped;
            continue;
        }
        ActivationRecord a;
        a.kind = kind;
        a.at = timeField(v, "at");
        a.licenseId = v.str("license_id");
        a.customer = v.str("customer");
        a.edition = v.str("edition");
        a.type = v.str("type");
        a.expiresAt = timeField(v, "expires_at");
        a.deviceKey = v.str("device_key");
        a.deviceName = v.str("device_name");
        a.fingerprint = v.str("device_fingerprint");
        a.software = v.str("software");
        a.requestedAt = timeField(v, "requested_at");
        a.activatedAt = timeField(v, "activated_at");
        a.signedAt = timeField(v, "signed_at");
        a.extension = v.flag("extension");
        a.revokedAt = timeField(v, "revoked_at");
        a.reason = v.str("reason");
        a.issuedAt = timeField(v, "issued_at");
        a.issuedBefore = timeField(v, "issued_before");
        out.push_back(std::move(a));
    }
    return true;
}

std::vector<KnownDevice> knownDevices(const std::vector<ActivationRecord>& records, Seconds now) {
    // Les evenements dans l'ordre de leur heure (1.31.0 : apres une synchro, les lignes d'un autre poste
    // arrivent a la fin du fichier, plus anciennes ou non ; a la meme heure, l'ordre du fichier) : le
    // dernier etat de chaque PC (par sa cle).
    std::vector<const ActivationRecord*> ordered;
    ordered.reserve(records.size());
    for (const auto& r : records) ordered.push_back(&r);
    std::stable_sort(ordered.begin(), ordered.end(), [](const ActivationRecord* x, const ActivationRecord* y) { return x->at < y->at; });
    std::map<std::string, KnownDevice> byKey;
    std::map<std::string, bool> licensed, revoked;
    std::map<std::string, Seconds> issuedOf;   // 1.32.0 : l'emission de la licence de chaque PC (une revocation precise)
    for (const ActivationRecord* rec : ordered) {
        const ActivationRecord& a = *rec;
        if (a.kind == "revocation") {
            for (auto& [k, d] : byKey)
                if (licensed[k] && d.licenseId == a.licenseId && (a.deviceKey.empty() || a.deviceKey == k)
                    && (a.issuedBefore == 0 || issuedOf[k] < a.issuedBefore)) {
                    revoked[k] = true;
                    d.revokedAt = a.revokedAt ? a.revokedAt : a.at;
                }
            continue;
        }
        if (a.deviceKey.empty()) continue;
        KnownDevice& d = byKey[a.deviceKey];
        d.deviceKey = a.deviceKey;
        if (!a.deviceName.empty()) d.name = a.deviceName;
        if (!a.fingerprint.empty()) d.fingerprint = a.fingerprint;
        if (!a.software.empty()) d.software = a.software;
        if (a.requestedAt) d.requestedAt = a.requestedAt;
        if (a.kind == "demande") {
            if (!licensed[a.deviceKey] && !a.licenseId.empty()) d.licenseId = a.licenseId;
            continue;
        }
        licensed[a.deviceKey] = true;
        revoked[a.deviceKey] = false;
        issuedOf[a.deviceKey] = a.issuedAt ? a.issuedAt : a.signedAt;   // une ligne d'avant la 1.32.0 : l'heure de signature
        d.revokedAt = 0;
        d.customer = a.customer;
        d.licenseId = a.licenseId;
        d.edition = a.edition;
        d.expiresAt = a.expiresAt;
        d.activatedAt = a.activatedAt ? a.activatedAt : a.signedAt;
    }
    std::vector<KnownDevice> out;
    for (auto& [k, d] : byKey) {
        if (d.fingerprint.empty()) d.fingerprint = fingerprintOfHex(k);
        d.state = revoked[k] ? "revoque" : !licensed[k] ? "demande" : (d.expiresAt && d.expiresAt <= now) ? "expire" : "active";
        out.push_back(d);
    }
    std::sort(out.begin(), out.end(), [](const KnownDevice& x, const KnownDevice& y) {
        const bool xr = x.state == "demande", yr = y.state == "demande";
        if (xr != yr) return yr;   // les demandes en attente a la fin
        if (x.customer != y.customer) return x.customer < y.customer;
        if (x.licenseId != y.licenseId) return x.licenseId < y.licenseId;
        if (x.name != y.name) return x.name < y.name;
        return x.deviceKey < y.deviceKey;
    });
    return out;
}

std::vector<KnownDevice> filterDevices(const std::vector<KnownDevice>& all, const std::string& licenseId, const std::string& customer) {
    std::vector<KnownDevice> out;
    const std::string want = lowerAscii(customer);
    for (const auto& d : all) {
        if (!licenseId.empty() && d.licenseId != licenseId) continue;
        if (!want.empty() && lowerAscii(d.customer).find(want) == std::string::npos) continue;
        out.push_back(d);
    }
    return out;
}

std::string devicesTable(const std::vector<KnownDevice>& devices) {
    if (devices.empty())
        return "Aucun PC connu. Ce registre se remplit avec signer-licence (les PC li\xC3\xA9s), enregistrer-demande (une demande re\xC3\xA7ue) et "
               "signer-revocations.\n";
    std::vector<std::vector<std::string>> rows;
    rows.push_back(columns());
    for (const auto& d : devices) {
        auto c = cellsOf(d);
        for (auto& x : c) x = clip(x, 30);
        rows.push_back(std::move(c));
    }
    std::vector<std::size_t> w(columns().size(), 0);
    for (const auto& r : rows)
        for (std::size_t i = 0; i < r.size(); ++i) w[i] = std::max(w[i], width(r[i]));
    std::string out;
    for (std::size_t n = 0; n < rows.size(); ++n) {
        std::string line;
        for (std::size_t i = 0; i < rows[n].size(); ++i) {
            line += rows[n][i];
            if (i + 1 < rows[n].size()) line += std::string(w[i] - width(rows[n][i]) + 2, ' ');
        }
        out += line + "\n";
        if (n == 0) {
            std::size_t total = 0;
            for (std::size_t i = 0; i < w.size(); ++i) total += w[i] + (i + 1 < w.size() ? 2 : 0);
            out += std::string(total, '-') + "\n";
        }
    }
    std::set<std::string> licenses, customers;
    std::map<std::string, int> states;
    for (const auto& d : devices) {
        if (!d.licenseId.empty() && d.state != "demande") licenses.insert(d.licenseId);
        if (!d.customer.empty()) customers.insert(d.customer);
        ++states[d.state];
    }
    const auto count = [](std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); };
    out += "\nTotal : " + count(devices.size(), "PC", "PC") + ", " + count(licenses.size(), "licence", "licences") + ", " + count(customers.size(), "client", "clients")
           + " - " + count(static_cast<std::size_t>(states["active"]), "activ\xC3\xA9", "activ\xC3\xA9s") + ", "
           + count(static_cast<std::size_t>(states["demande"]), "demande en attente", "demandes en attente") + ", "
           + count(static_cast<std::size_t>(states["revoque"]), "r\xC3\xA9voqu\xC3\xA9", "r\xC3\xA9voqu\xC3\xA9s") + ", "
           + count(static_cast<std::size_t>(states["expire"]), "expir\xC3\xA9", "expir\xC3\xA9s") + "\n";
    return out;
}

std::string devicesCsv(const std::vector<KnownDevice>& devices) {
    const auto field = [](const std::string& raw) {
        // 1.32.0 (M4) : une cellule qui commence par = + - @ tabulation ou retour serait une FORMULE pour Excel (un nom
        // de PC ou de client venu d'ailleurs) : une apostrophe devant la rend texte. "-" seul (pas de date) n'en est pas une.
        const std::string x = looksLikeFormula(raw) && raw != "-" ? "'" + raw : raw;
        if (x.find_first_of(";\"\r\n") == std::string::npos) return x;
        std::string q = "\"";
        for (char c : x) q += c == '"' ? std::string("\"\"") : std::string(1, c);
        return q + "\"";
    };
    std::string out = "\xEF\xBB\xBF";   // UTF-8 avec BOM : Excel le lit comme tel
    const auto line = [&](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) out += (i ? ";" : "") + field(cells[i]);
        out += "\r\n";
    };
    line(columns());
    for (const auto& d : devices) line(cellsOf(d));
    return out;
}

} // namespace license_admin
