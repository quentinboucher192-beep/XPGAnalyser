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

std::string publisherKeyHeader(const SigningKey& key, Seconds now) {
    std::string h;
    h += "// =============================================================================\n";
    h += "//  licensing/PublisherKey.hpp - LA CLE PUBLIQUE DE L'EDITEUR, compilee dans les\n";
    h += "//  logiciels clients (API Studio, IHM Studio, License Manager, agent)\n";
    h += "// -----------------------------------------------------------------------------\n";
    h += "//  GENERE par l'outil Admin des licences (commande exporter-cle) le " + isoTime(now) + ".\n";
    h += "//  Ne le modifie pas a la main : regenere-le sur le poste Admin.\n";
    h += "//  Seule la cle PUBLIQUE est ici : elle VERIFIE les licences, les registres et les\n";
    h += "//  revocations ; elle ne permet pas d'en signer. La cle privee reste sur le poste Admin.\n";
    h += "//  Empreinte (SHA-256 de la cle) : " + key.fingerprint(0) + "\n";
    if (key.test()) {
        h += "//\n";
        h += "//  !!! CLE D'ESSAI (" + std::string(kTestKeyMarker) + ") : elle ne sert qu'au developpement et aux\n";
        h += "//  essais. Un programme qui l'embarque n'est PAS livrable : le controle du paquet\n";
        h += "//  (outils/mingw/package_mingw.py) arrete la construction. Avant de livrer : cree la\n";
        h += "//  vraie cle sur le poste Admin (creer-cle), puis exporter-cle vers ce fichier. !!!\n";
    }
    h += "//\n";
    h += "//  Plusieurs cles (une rotation) : une ligne par cle dans kKeys ; les clients\n";
    h += "//  croient chacune. Une cle retiree s'enleve d'ici (et ses licences tombent).\n";
    h += "// =============================================================================\n";
    h += "#pragma once\n\n#include <cstdint>\n\nnamespace licensing::publisher {\n\n";
    h += "struct KeyEntry {\n";
    h += "    const char*  id;     // \"xpg-\" + 16 hexadecimaux du SHA-256 de la cle\n";
    h += "    const char*  name;   // le nom de la cle (une cle d'essai le dit)\n";
    h += "    bool         test;   // une cle d'ESSAI : jamais dans un programme livre\n";
    h += "    std::uint8_t key[32];\n";
    h += "};\n\n";
    h += "inline constexpr KeyEntry kKeys[] = {\n";
    h += "    {" + cppString(key.keyId()) + ", " + cppString(key.name()) + ", " + (key.test() ? "true" : "false") + ",\n     {";
    const auto& k = key.publicKey();
    for (std::size_t i = 0; i < k.size(); ++i) {
        char b[8];
        std::snprintf(b, sizeof b, "0x%02x", k[i]);
        if (i) h += i % 8 == 0 ? ",\n      " : ", ";
        h += b;
    }
    h += "}},\n};\n\n} // namespace licensing::publisher\n";
    return h;
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

bool signRevocations(const SigningKey& key, std::string_view inputJson, Seconds now, std::string& out, std::string* why) {
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
    return true;
}

bool signLicense(const SigningKey& key, const LicenseInputs& in, Seconds now, std::string& out, std::string* why, std::string* summary) {
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
    for (std::size_t i = 0; i < in.requests.size(); ++i) {
        ActivationRequest q;
        if (!readRequest(in.requests[i], q, &err)) return fail("demande n\xC2\xB0 " + std::to_string(i + 1) + " : " + err);
        if (!q.licenseId.empty() && q.licenseId != l.licenseId)
            return fail("demande n\xC2\xB0 " + std::to_string(i + 1) + " : ce PC demande la licence " + cite(q.licenseId) + ", pas " + cite(l.licenseId));
        if (l.boundTo(q.deviceKey)) continue;   // deja lie (une reactivation)
        BoundDevice d;
        d.key = q.deviceKey;
        d.name = q.deviceName;
        d.activatedAt = now;
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
            if (const Revocation* r = rev.find(l.licenseId, d.key))
                return fail("la licence " + cite(l.licenseId) + (r->device.empty() ? "" : " (pour un de ces PC)") + " est dans la liste de r\xC3\xA9vocation n\xC2\xB0 "
                            + std::to_string(rev.number));
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

} // namespace license_admin
