// =============================================================================
//  licensing/Documents.cpp - 1.30.0 : les documents signes (voir Documents.hpp)
// =============================================================================
#include "Documents.hpp"

#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace licensing {

using core::json::Value;

namespace {

std::string cite(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

bool validUtf8(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = 0;
        if (c < 0x80) n = 0;
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) n = 1;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) n = 3;
        else return false;
        for (std::size_t k = 1; k <= n; ++k) {
            if (i + k >= s.size()) return false;
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += n + 1;
    }
    return true;
}

// Un texte d'une ligne : UTF-8 valide, sans caractere de controle.
bool plainText(std::string_view s) noexcept {
    if (!validUtf8(s)) return false;
    for (char c : s)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    return true;
}

bool duplicateKey(const Value& v, std::string& key) {
    if (v.isObject()) {
        std::set<std::string> seen;
        for (const auto& [k, x] : v.members()) {
            if (!seen.insert(k).second) {
                key = k;
                return true;
            }
            if (duplicateKey(x, key)) return true;
        }
    } else if (v.isArray()) {
        for (const auto& x : v.items())
            if (duplicateKey(x, key)) return true;
    }
    return false;
}

// Lit les champs d'un objet JSON, strictement ; retient la premiere erreur.
class Fields {
public:
    Fields(const Value& obj, std::string where) : obj_(obj), where_(std::move(where)) {}

    [[nodiscard]] bool ok() const noexcept { return err_.empty(); }
    [[nodiscard]] const std::string& error() const noexcept { return err_; }
    void fail(const std::string& text) {
        if (err_.empty()) err_ = text;
    }
    [[nodiscard]] std::string name(std::string_view key) const { return cite(where_ + std::string(key)); }

    const Value* get(std::string_view key, bool required) {
        seen_.insert(std::string(key));
        const Value* v = obj_.find(key);
        if (!v && required) fail("le champ " + name(key) + " manque");
        return v;
    }
    // Une chaine : `maxBytes` octets au plus, une ligne de texte. Absente et facultative : `out` vide.
    void text(std::string_view key, std::string& out, bool required, std::size_t maxBytes = 200) {
        out.clear();
        const Value* v = get(key, required);
        if (!v) return;
        if (!v->isString()) return fail("le champ " + name(key) + " n'est pas une cha\xC3\xAEne");
        if (v->asString().size() > maxBytes) return fail("le champ " + name(key) + " est trop long");
        if (!plainText(v->asString())) return fail("le champ " + name(key) + " contient des caract\xC3\xA8res interdits");
        out = v->asString();
    }
    void integer(std::string_view key, std::int64_t& out, std::int64_t lo, std::int64_t hi, bool required) {
        const Value* v = get(key, required);
        if (!v) return;
        const double d = v->asNumber(-1e300);
        if (!v->isNumber() || d != std::floor(d) || d < static_cast<double>(lo) || d > static_cast<double>(hi))
            return fail("le champ " + name(key) + " n'est pas un entier entre " + std::to_string(lo) + " et " + std::to_string(hi));
        out = static_cast<std::int64_t>(d);
    }
    void count(std::string_view key, int& out, int lo, int hi, bool required) {
        std::int64_t n = out;
        integer(key, n, lo, hi, required);
        out = static_cast<int>(n);
    }
    void number(std::string_view key, std::uint64_t& out, bool required) {
        std::int64_t n = static_cast<std::int64_t>(out);
        integer(key, n, 0, 9007199254740991LL, required);
        out = static_cast<std::uint64_t>(n);
    }
    void boolean(std::string_view key, bool& out, bool required) {
        const Value* v = get(key, required);
        if (!v) return;
        if (!v->isBool()) return fail("le champ " + name(key) + " n'est pas vrai ou faux");
        out = v->asBool();
    }
    // Une date ISO ; `nullable` : null est permis (0).
    void time(std::string_view key, Seconds& out, bool required, bool nullable = false) {
        out = 0;
        const Value* v = get(key, required);
        if (!v) return;
        if (nullable && v->isNull()) return;
        if (!v->isString() || !parseIsoTime(v->asString(), out))
            return fail("le champ " + name(key) + " n'est pas une date (AAAA-MM-JJTHH:MM:SSZ)" + (nullable ? " ni null" : ""));
    }
    const Value* array(std::string_view key, bool required, std::size_t maxItems) {
        const Value* v = get(key, required);
        if (!v) return nullptr;
        if (!v->isArray()) {
            fail("le champ " + name(key) + " n'est pas une liste");
            return nullptr;
        }
        if (v->items().size() > maxItems) {
            fail("le champ " + name(key) + " a trop d'\xC3\xA9l\xC3\xA9ments");
            return nullptr;
        }
        return v;
    }
    // Les cles que personne n'a demandees : refusees (voir l'en-tete).
    void finish() {
        if (!ok()) return;
        for (const auto& [k, x] : obj_.members())
            if (!seen_.count(k)) return fail("le champ " + name(k) + " est inconnu (une version plus r\xC3\xA9" "cente l'a \xC3\xA9" "crit ?)");
    }
    [[nodiscard]] const std::string& where() const noexcept { return where_; }

private:
    const Value&          obj_;
    std::string           where_;
    std::set<std::string> seen_;
    std::string           err_;
};

// La racine d'un document : JSON, objet, sans cle en double ; format et version.
bool openDocument(std::string_view json, std::string_view format, const char* what, Value& root, std::string* why) {
    std::string err;
    const auto fail = [&](const std::string& text) {
        if (why) *why = std::string(what) + " est illisible : " + text;
        return false;
    };
    if (!core::json::parse(json, root, &err)) return fail(err);
    if (!root.isObject()) return fail("ce n'est pas un objet JSON");
    std::string dup;
    if (duplicateKey(root, dup)) return fail("le champ " + cite(dup) + " y est deux fois");
    const Value* f = root.find("format");
    if (!f || !f->isString() || f->asString() != format)
        return fail("ce n'est pas le bon genre de document (format " + cite(f && f->isString() ? f->asString() : std::string("?")) + ")");
    const Value* v = root.find("version");
    const double d = v ? v->asNumber(-1) : -1;
    if (!v || !v->isNumber() || d != std::floor(d) || d < 1) return fail("le champ \xC2\xAB version \xC2\xBB manque ou n'est pas un entier");
    if (d > kFormatVersion) {
        if (why) *why = std::string(what) + " est d'un format plus r\xC3\xA9" "cent (version " + std::to_string(static_cast<long long>(d)) + ") : mets \xC3\xA0 jour ce programme";
        return false;
    }
    return true;
}

bool finishDocument(Fields& f, const char* what, std::string* why) {
    f.finish();
    if (f.ok()) return true;
    if (why) *why = std::string(what) + " est illisible : " + f.error();
    return false;
}

void readLimits(Fields& parent, const Value* obj, std::string_view key, Limits& out) {
    out.clear();
    if (!obj) return;
    if (!obj->isObject()) return parent.fail("le champ " + parent.name(key) + " n'est pas un objet");
    if (obj->members().size() > 32) return parent.fail("le champ " + parent.name(key) + " a trop de limites");
    for (const auto& [k, v] : obj->members()) {
        if (!isFeatureId(k)) return parent.fail("la limite " + cite(k) + " n'a pas un nom valide");
        const double d = v.asNumber(-1e300);
        if (!v.isNumber() || d != std::floor(d) || std::fabs(d) > 9007199254740991.0)
            return parent.fail("la limite " + cite(k) + " n'est pas un entier");
        out.emplace_back(k, static_cast<std::int64_t>(d));
    }
    std::sort(out.begin(), out.end());
}

void readGrants(Fields& parent, std::string_view key, std::vector<FeatureGrant>& out, bool required) {
    out.clear();
    const Value* arr = parent.array(key, required, 512);
    if (!arr) return;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < arr->items().size() && parent.ok(); ++i) {
        const Value& item = arr->items()[i];
        const std::string where = parent.where() + std::string(key) + "[" + std::to_string(i) + "].";
        if (!item.isObject()) return parent.fail("l'\xC3\xA9l\xC3\xA9ment " + cite(where.substr(0, where.size() - 1)) + " n'est pas un objet");
        Fields f(item, where);
        FeatureGrant g;
        f.text("id", g.id, true, 64);
        if (f.ok() && !isFeatureId(g.id)) f.fail("l'identifiant de fonctionnalit\xC3\xA9 " + cite(g.id) + " n'est pas valide");
        readLimits(f, f.get("limits", false), "limits", g.limits);
        f.finish();
        if (!f.ok()) return parent.fail(f.error());
        if (!ids.insert(g.id).second) return parent.fail("la fonctionnalit\xC3\xA9 " + cite(g.id) + " est deux fois dans " + parent.name(key));
        out.push_back(std::move(g));
    }
    std::sort(out.begin(), out.end(), [](const FeatureGrant& a, const FeatureGrant& b) { return a.id < b.id; });
}

Value grantsValue(const std::vector<FeatureGrant>& grants) {
    Value arr = Value::array();
    for (const auto& g : grants) {
        Value o = Value::object();
        o.set("id", g.id);
        if (!g.limits.empty()) {
            Value l = Value::object();
            for (const auto& [k, n] : g.limits) l.set(k, n);
            o.set("limits", std::move(l));
        }
        arr.push(std::move(o));
    }
    return arr;
}

Value header(std::string_view format, int version) {
    Value root = Value::object();
    root.set("format", std::string(format));
    root.set("version", version);
    return root;
}

std::string finishText(const Value& root) { return core::json::write(root, true); }

void readType(Fields& f, std::string_view key, LicenseType& out, bool required) {
    std::string t;
    f.text(key, t, required, 32);
    if (!f.ok() || t.empty()) return;
    if (!parseLicenseType(t, out)) f.fail("le type " + cite(t) + " est inconnu (perpetual, temporary, evaluation, floating)");
}

} // namespace

// ================================================================== genres de licence ==
const char* licenseTypeKey(LicenseType t) noexcept {
    switch (t) {
        case LicenseType::Perpetual: return "perpetual";
        case LicenseType::Temporary: return "temporary";
        case LicenseType::Evaluation: return "evaluation";
        case LicenseType::Floating: return "floating";
    }
    return "?";
}

std::string licenseTypeLabel(LicenseType t) {
    switch (t) {
        case LicenseType::Perpetual: return "perp\xC3\xA9tuelle";
        case LicenseType::Temporary: return "temporaire";
        case LicenseType::Evaluation: return "\xC3\xA9valuation";
        case LicenseType::Floating: return "flottante";
    }
    return "?";
}

bool parseLicenseType(std::string_view key, LicenseType& out) noexcept {
    for (LicenseType t : {LicenseType::Perpetual, LicenseType::Temporary, LicenseType::Evaluation, LicenseType::Floating})
        if (key == licenseTypeKey(t)) {
            out = t;
            return true;
        }
    return false;
}

// ======================================================================== aides ==
bool isFeatureId(std::string_view s) noexcept {
    if (s.empty() || s.size() > 64 || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool isEditionId(std::string_view s) noexcept {
    if (s.empty() || s.size() > 32 || !(s[0] >= 'A' && s[0] <= 'Z')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool isLicenseId(std::string_view s) noexcept {
    if (s.empty() || s.size() > 64) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

bool isKeyHex(std::string_view s) noexcept {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool isStationHex(std::string_view s) noexcept {
    return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool looksLikeFormula(std::string_view s) noexcept {
    // les caracteres qui font d'une cellule une formule (Excel, LibreOffice) : injection par un CSV
    return !s.empty() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@' || s[0] == '\t' || s[0] == '\r');
}

std::string newNonce() {
    std::uint8_t b[16];
    deploy::crypto::randomBytes(b, sizeof b);
    return deploy::crypto::hex(b, sizeof b);
}

void normalizeGrants(std::vector<FeatureGrant>& grants) {
    for (auto& g : grants) {
        std::sort(g.limits.begin(), g.limits.end());
        g.limits.erase(std::unique(g.limits.begin(), g.limits.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), g.limits.end());
    }
    std::stable_sort(grants.begin(), grants.end(), [](const FeatureGrant& a, const FeatureGrant& b) { return a.id < b.id; });
    grants.erase(std::unique(grants.begin(), grants.end(), [](const FeatureGrant& a, const FeatureGrant& b) { return a.id == b.id; }), grants.end());
}

const FeatureDef* Registry::feature(std::string_view id) const noexcept {
    for (const auto& f : features)
        if (f.id == id) return &f;
    return nullptr;
}

const EditionDef* Registry::edition(std::string_view id) const noexcept {
    for (const auto& e : editions)
        if (e.id == id) return &e;
    return nullptr;
}

const FeatureGrant* License::grant(std::string_view id) const noexcept {
    for (const auto& g : features)
        if (g.id == id) return &g;
    return nullptr;
}

bool License::boundTo(std::string_view deviceKeyHex) const noexcept { return device(deviceKeyHex) != nullptr; }

const BoundDevice* License::device(std::string_view deviceKeyHex) const noexcept {
    for (const auto& d : devices)
        if (d.key == deviceKeyHex) return &d;
    return nullptr;
}

const Revocation* RevocationList::find(std::string_view licenseId, std::string_view deviceKeyHex, Seconds licenseIssuedAt) const noexcept {
    for (const auto& r : revoked)
        if (r.licenseId == licenseId && (r.device.empty() || r.device == deviceKeyHex) && (r.issuedBefore == 0 || licenseIssuedAt < r.issuedBefore))
            return &r;
    return nullptr;
}

// ===================================================================== registre ==
std::string writeRegistry(const Registry& r) {
    Value root = header(kRegistryFormat, r.version);
    root.set("number", static_cast<std::uint64_t>(r.number));
    root.set("issued_at", isoTime(r.issuedAt));
    root.set("unlicensed_edition", r.unlicensedEdition);
    Value feats = Value::array();
    for (const auto& f : r.features) {
        Value o = Value::object();
        o.set("id", f.id);
        o.set("product", f.product);
        o.set("label", f.label);
        if (!f.limits.empty()) {
            Value l = Value::array();
            for (const auto& d : f.limits) {
                Value x = Value::object();
                x.set("id", d.id);
                x.set("label", d.label);
                l.push(std::move(x));
            }
            o.set("limits", std::move(l));
        }
        if (f.retired) o.set("retired", true);
        feats.push(std::move(o));
    }
    root.set("features", std::move(feats));
    Value eds = Value::array();
    for (const auto& e : r.editions) {
        Value o = Value::object();
        o.set("id", e.id);
        o.set("label", e.label);
        o.set("type", licenseTypeKey(e.type));
        o.set("max_devices", e.maxDevices);
        o.set("duration_days", e.durationDays);
        o.set("offline_grace_days", e.offlineGraceDays);
        o.set("features", grantsValue(e.features));
        if (e.retired) o.set("retired", true);
        eds.push(std::move(o));
    }
    root.set("editions", std::move(eds));
    return finishText(root);
}

bool parseRegistry(std::string_view json, Registry& out, std::string* why) {
    static constexpr const char* kWhat = "le registre des \xC3\xA9" "ditions";
    Value root;
    if (!openDocument(json, kRegistryFormat, kWhat, root, why)) return false;
    Fields f(root, "");
    Registry r;
    f.get("format", true);
    f.count("version", r.version, 1, kFormatVersion, true);
    f.number("number", r.number, true);
    if (f.ok() && r.number == 0) f.fail("le num\xC3\xA9ro du registre doit \xC3\xAAtre au moins 1");
    f.time("issued_at", r.issuedAt, true);
    f.text("unlicensed_edition", r.unlicensedEdition, true, 32);
    if (const Value* arr = f.array("features", true, 1024)) {
        for (std::size_t i = 0; i < arr->items().size() && f.ok(); ++i) {
            const Value& item = arr->items()[i];
            const std::string where = "features[" + std::to_string(i) + "].";
            if (!item.isObject()) {
                f.fail("l'\xC3\xA9l\xC3\xA9ment " + cite(where.substr(0, where.size() - 1)) + " n'est pas un objet");
                break;
            }
            Fields g(item, where);
            FeatureDef d;
            g.text("id", d.id, true, 64);
            if (g.ok() && !isFeatureId(d.id)) g.fail("l'identifiant de fonctionnalit\xC3\xA9 " + cite(d.id) + " n'est pas valide");
            g.text("product", d.product, true, 64);
            g.text("label", d.label, true, 200);
            if (const Value* lim = g.array("limits", false, 32)) {
                for (std::size_t k = 0; k < lim->items().size() && g.ok(); ++k) {
                    const Value& li = lim->items()[k];
                    if (!li.isObject()) {
                        g.fail("une limite de " + cite(d.id) + " n'est pas un objet");
                        break;
                    }
                    Fields h(li, where + "limits[" + std::to_string(k) + "].");
                    LimitDef ld;
                    h.text("id", ld.id, true, 64);
                    if (h.ok() && !isFeatureId(ld.id)) h.fail("le nom de limite " + cite(ld.id) + " n'est pas valide");
                    h.text("label", ld.label, true, 200);
                    h.finish();
                    if (!h.ok()) g.fail(h.error());
                    for (const auto& prev : d.limits)
                        if (prev.id == ld.id) g.fail("la limite " + cite(ld.id) + " de " + cite(d.id) + " est deux fois");
                    d.limits.push_back(std::move(ld));
                }
            }
            g.boolean("retired", d.retired, false);
            g.finish();
            if (!g.ok()) {
                f.fail(g.error());
                break;
            }
            if (r.feature(d.id)) {
                f.fail("la fonctionnalit\xC3\xA9 " + cite(d.id) + " est d\xC3\xA9" "clar\xC3\xA9" "e deux fois");
                break;
            }
            r.features.push_back(std::move(d));
        }
    }
    if (const Value* arr = f.array("editions", true, 256)) {
        for (std::size_t i = 0; i < arr->items().size() && f.ok(); ++i) {
            const Value& item = arr->items()[i];
            const std::string where = "editions[" + std::to_string(i) + "].";
            if (!item.isObject()) {
                f.fail("l'\xC3\xA9l\xC3\xA9ment " + cite(where.substr(0, where.size() - 1)) + " n'est pas un objet");
                break;
            }
            Fields g(item, where);
            EditionDef e;
            g.text("id", e.id, true, 32);
            if (g.ok() && !isEditionId(e.id)) g.fail("l'identifiant d'\xC3\xA9" "dition " + cite(e.id) + " n'est pas valide");
            g.text("label", e.label, true, 200);
            readType(g, "type", e.type, true);
            g.count("max_devices", e.maxDevices, 1, 100000, true);
            g.count("duration_days", e.durationDays, 0, 36600, true);
            g.count("offline_grace_days", e.offlineGraceDays, 0, 3660, true);
            readGrants(g, "features", e.features, true);
            g.boolean("retired", e.retired, false);
            g.finish();
            if (!g.ok()) {
                f.fail(g.error());
                break;
            }
            if (r.edition(e.id)) {
                f.fail("l'\xC3\xA9" "dition " + cite(e.id) + " est d\xC3\xA9" "clar\xC3\xA9" "e deux fois");
                break;
            }
            r.editions.push_back(std::move(e));
        }
    }
    if (!finishDocument(f, kWhat, why)) return false;
    // La coherence : l'edition sans licence existe ; chaque edition n'accorde que des fonctionnalites et limites declarees.
    const auto bad = [&](const std::string& text) {
        if (why) *why = std::string(kWhat) + " n'est pas coh\xC3\xA9rent : " + text;
        return false;
    };
    if (!r.edition(r.unlicensedEdition)) return bad("l'\xC3\xA9" "dition sans licence " + cite(r.unlicensedEdition) + " n'y est pas");
    for (const auto& e : r.editions)
        for (const auto& g : e.features) {
            const FeatureDef* d = r.feature(g.id);
            if (!d) return bad("l'\xC3\xA9" "dition " + cite(e.id) + " accorde " + cite(g.id) + ", qui n'est pas d\xC3\xA9" "clar\xC3\xA9" "e");
            for (const auto& [name, n] : g.limits) {
                (void)n;
                if (std::none_of(d->limits.begin(), d->limits.end(), [&](const LimitDef& l) { return l.id == name; }))
                    return bad("l'\xC3\xA9" "dition " + cite(e.id) + " fixe la limite " + cite(name) + " de " + cite(g.id) + ", qui n'est pas d\xC3\xA9" "clar\xC3\xA9" "e");
            }
        }
    out = std::move(r);
    return true;
}

// ======================================================================= licence ==
std::string writeLicense(const License& l) {
    Value root = header(kLicenseFormat, l.version);
    root.set("license_id", l.licenseId);
    root.set("customer", l.customer);
    root.set("edition", l.edition);
    root.set("type", licenseTypeKey(l.type));
    root.set("service", l.service.empty() ? Value() : Value(l.service));
    root.set("max_devices", l.maxDevices);
    root.set("features", grantsValue(l.features));
    root.set("issued_at", isoTime(l.issuedAt));
    root.set("expires_at", l.expiresAt ? Value(isoTime(l.expiresAt)) : Value());
    root.set("offline_grace_days", l.offlineGraceDays);
    Value devs = Value::array();
    for (const auto& d : l.devices) {
        Value o = Value::object();
        o.set("key", d.key);
        o.set("name", d.name);
        o.set("activated_at", isoTime(d.activatedAt));
        if (!d.station.empty()) o.set("station", d.station);   // 1.32.0 : seulement s'il sert (la 1.31 relit le reste)
        devs.push(std::move(o));
    }
    root.set("devices", std::move(devs));
    root.set("manifest", static_cast<std::uint64_t>(l.manifest));
    root.set("nonce", l.nonce);
    return finishText(root);
}

bool parseLicense(std::string_view json, License& out, std::string* why) {
    static constexpr const char* kWhat = "la licence";
    Value root;
    if (!openDocument(json, kLicenseFormat, kWhat, root, why)) return false;
    Fields f(root, "");
    License l;
    f.get("format", true);
    f.count("version", l.version, 1, kFormatVersion, true);
    f.text("license_id", l.licenseId, true, 64);
    if (f.ok() && !isLicenseId(l.licenseId)) f.fail("l'identifiant de licence " + cite(l.licenseId) + " n'est pas valide");
    f.text("customer", l.customer, true, 200);
    f.text("edition", l.edition, true, 32);
    if (f.ok() && !isEditionId(l.edition)) f.fail("l'identifiant d'\xC3\xA9" "dition " + cite(l.edition) + " n'est pas valide");
    readType(f, "type", l.type, true);
    if (const Value* s = f.get("service", true)) {
        if (!s->isNull() && !s->isString()) f.fail("le champ \xC2\xAB service \xC2\xBB n'est ni une cha\xC3\xAEne ni null");
        else if (s->isString()) {
            if (s->asString().empty() || s->asString().size() > 200 || !plainText(s->asString())) f.fail("le champ \xC2\xAB service \xC2\xBB n'est pas valide");
            else l.service = s->asString();
        }
    }
    f.count("max_devices", l.maxDevices, 1, 100000, true);
    readGrants(f, "features", l.features, true);
    f.time("issued_at", l.issuedAt, true);
    f.time("expires_at", l.expiresAt, true, /*nullable=*/true);
    f.count("offline_grace_days", l.offlineGraceDays, 0, 3660, true);
    if (const Value* arr = f.array("devices", true, 100000)) {
        for (std::size_t i = 0; i < arr->items().size() && f.ok(); ++i) {
            const Value& item = arr->items()[i];
            const std::string where = "devices[" + std::to_string(i) + "].";
            if (!item.isObject()) {
                f.fail("l'\xC3\xA9l\xC3\xA9ment " + cite(where.substr(0, where.size() - 1)) + " n'est pas un objet");
                break;
            }
            Fields g(item, where);
            BoundDevice d;
            g.text("key", d.key, true, 64);
            if (g.ok() && !isKeyHex(d.key)) g.fail("la cl\xC3\xA9 de PC " + g.name("key") + " n'est pas une cl\xC3\xA9 publique (64 hexad\xC3\xA9" "cimaux)");
            g.text("name", d.name, true, 100);
            g.time("activated_at", d.activatedAt, true);
            g.text("station", d.station, false, 32);
            if (g.ok() && !d.station.empty() && !isStationHex(d.station)) g.fail("le champ " + g.name("station") + " n'est pas une empreinte de poste (32 hexad\xC3\xA9" "cimaux)");
            g.finish();
            if (!g.ok()) {
                f.fail(g.error());
                break;
            }
            if (l.boundTo(d.key)) {
                f.fail("le m\xC3\xAAme PC est li\xC3\xA9 deux fois");
                break;
            }
            l.devices.push_back(std::move(d));
        }
    }
    f.number("manifest", l.manifest, true);
    f.text("nonce", l.nonce, true, 64);
    if (f.ok() && (l.nonce.size() != 32 || !std::all_of(l.nonce.begin(), l.nonce.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })))
        f.fail("le champ \xC2\xAB nonce \xC2\xBB n'est pas valide");
    if (!finishDocument(f, kWhat, why)) return false;
    // Les regles du format.
    const auto bad = [&](const std::string& text) {
        if (why) *why = std::string(kWhat) + " n'est pas coh\xC3\xA9rente : " + text;
        return false;
    };
    if (l.devices.empty()) return bad("aucun PC n'y est li\xC3\xA9");
    if (static_cast<int>(l.devices.size()) > l.maxDevices)
        return bad(std::to_string(l.devices.size()) + " PC li\xC3\xA9s pour " + std::to_string(l.maxDevices) + " autoris\xC3\xA9s");
    const bool timed = l.type == LicenseType::Temporary || l.type == LicenseType::Evaluation;
    if (timed && l.expiresAt == 0) return bad("une licence " + licenseTypeLabel(l.type) + " doit avoir une date de fin");
    if (l.type == LicenseType::Perpetual && l.expiresAt != 0) return bad("une licence perp\xC3\xA9tuelle n'a pas de date de fin");
    if (l.expiresAt != 0 && l.expiresAt <= l.issuedAt) return bad("elle finit avant d'avoir commenc\xC3\xA9");
    out = std::move(l);
    return true;
}

// =================================================================== revocations ==
std::string writeRevocations(const RevocationList& r) {
    Value root = header(kRevocationsFormat, r.version);
    root.set("number", static_cast<std::uint64_t>(r.number));
    root.set("issued_at", isoTime(r.issuedAt));
    Value arr = Value::array();
    for (const auto& x : r.revoked) {
        Value o = Value::object();
        o.set("license_id", x.licenseId);
        if (!x.device.empty()) o.set("device", x.device);
        o.set("revoked_at", isoTime(x.revokedAt));
        o.set("reason", x.reason);
        if (x.issuedBefore) o.set("issued_before", isoTime(x.issuedBefore));   // 1.32.0 : seulement s'il sert
        arr.push(std::move(o));
    }
    root.set("revoked", std::move(arr));
    return finishText(root);
}

bool parseRevocations(std::string_view json, RevocationList& out, std::string* why) {
    static constexpr const char* kWhat = "la liste de r\xC3\xA9vocation";
    Value root;
    if (!openDocument(json, kRevocationsFormat, kWhat, root, why)) return false;
    Fields f(root, "");
    RevocationList r;
    f.get("format", true);
    f.count("version", r.version, 1, kFormatVersion, true);
    f.number("number", r.number, true);
    if (f.ok() && r.number == 0) f.fail("le num\xC3\xA9ro de la liste doit \xC3\xAAtre au moins 1");
    f.time("issued_at", r.issuedAt, true);
    if (const Value* arr = f.array("revoked", true, 200000)) {
        for (std::size_t i = 0; i < arr->items().size() && f.ok(); ++i) {
            const Value& item = arr->items()[i];
            const std::string where = "revoked[" + std::to_string(i) + "].";
            if (!item.isObject()) {
                f.fail("l'\xC3\xA9l\xC3\xA9ment " + cite(where.substr(0, where.size() - 1)) + " n'est pas un objet");
                break;
            }
            Fields g(item, where);
            Revocation x;
            g.text("license_id", x.licenseId, true, 64);
            if (g.ok() && !isLicenseId(x.licenseId)) g.fail("l'identifiant de licence " + cite(x.licenseId) + " n'est pas valide");
            g.text("device", x.device, false, 64);
            if (g.ok() && !x.device.empty() && !isKeyHex(x.device)) g.fail("la cl\xC3\xA9 de PC " + g.name("device") + " n'est pas valide");
            g.time("revoked_at", x.revokedAt, true);
            g.text("reason", x.reason, true, 200);
            g.time("issued_before", x.issuedBefore, false);
            g.finish();
            if (!g.ok()) {
                f.fail(g.error());
                break;
            }
            r.revoked.push_back(std::move(x));
        }
    }
    if (!finishDocument(f, kWhat, why)) return false;
    out = std::move(r);
    return true;
}

// ======================================================================= demande ==
std::string writeRequest(const ActivationRequest& q) {
    Value root = header(kRequestFormat, q.version);
    root.set("device_key", q.deviceKey);
    root.set("device_name", q.deviceName);
    root.set("license_id", q.licenseId);
    root.set("software", q.software);
    root.set("created_at", isoTime(q.createdAt));
    root.set("nonce", q.nonce);
    if (!q.station.empty()) root.set("station", q.station);   // 1.32.0
    return finishText(root);
}

bool parseRequest(std::string_view json, ActivationRequest& out, std::string* why) {
    static constexpr const char* kWhat = "la demande d'activation";
    Value root;
    if (!openDocument(json, kRequestFormat, kWhat, root, why)) return false;
    Fields f(root, "");
    ActivationRequest q;
    f.get("format", true);
    f.count("version", q.version, 1, kFormatVersion, true);
    f.text("device_key", q.deviceKey, true, 64);
    if (f.ok() && !isKeyHex(q.deviceKey)) f.fail("la cl\xC3\xA9 du PC n'est pas une cl\xC3\xA9 publique (64 hexad\xC3\xA9" "cimaux)");
    f.text("device_name", q.deviceName, true, 100);
    // 1.32.0 : le nom finit dans le CSV de l'Admin (liste --csv) : jamais une formule de tableur
    if (f.ok() && looksLikeFormula(q.deviceName))
        f.fail("le nom du PC " + cite(q.deviceName) + " ne peut pas commencer par = + - ou @ (un tableur le prendrait pour une formule)");
    f.text("license_id", q.licenseId, true, 64);
    if (f.ok() && !q.licenseId.empty() && !isLicenseId(q.licenseId)) f.fail("l'identifiant de licence " + cite(q.licenseId) + " n'est pas valide");
    f.text("software", q.software, true, 100);
    f.time("created_at", q.createdAt, true);
    f.text("nonce", q.nonce, true, 64);
    f.text("station", q.station, false, 32);
    if (f.ok() && !q.station.empty() && !isStationHex(q.station)) f.fail("le champ \xC2\xAB station \xC2\xBB n'est pas une empreinte de poste (32 hexad\xC3\xA9" "cimaux)");
    if (!finishDocument(f, kWhat, why)) return false;
    out = std::move(q);
    return true;
}

// ======================================================== licence et registre ==
bool checkAgainstRegistry(const License& l, const Registry& r, std::string* why) {
    const std::string hint = l.manifest > r.number
                                 ? " : la licence vient du registre n\xC2\xB0 " + std::to_string(l.manifest) + ", ce PC n'a que le n\xC2\xB0 "
                                       + std::to_string(r.number) + " (importe le fichier complet re\xC3\xA7u de l'\xC3\xA9" "diteur)"
                                 : "";
    const auto fail = [&](const std::string& text) {
        if (why) *why = text + hint;
        return false;
    };
    if (!r.edition(l.edition)) return fail("l'\xC3\xA9" "dition " + cite(l.edition) + " est inconnue du registre");
    for (const auto& g : l.features) {
        const FeatureDef* d = r.feature(g.id);
        if (!d) return fail("la fonctionnalit\xC3\xA9 " + cite(g.id) + " est inconnue du registre");
        for (const auto& [name, n] : g.limits) {
            (void)n;
            if (std::none_of(d->limits.begin(), d->limits.end(), [&](const LimitDef& x) { return x.id == name; }))
                return fail("la limite " + cite(name) + " de " + cite(g.id) + " est inconnue du registre");
        }
    }
    return true;
}

} // namespace licensing
