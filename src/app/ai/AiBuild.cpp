// =============================================================================
//  app/ai/AiBuild.cpp - voir AiBuild.hpp (1.22.5 : l'avis du logiciel)
// -----------------------------------------------------------------------------
//  GENERER ET COMPILER TELS QUE LE LOGICIEL LES FAIT : hmi::generateWith et
//  hmi::compileWith, comme la verification globale (hmi::audit::run) et la
//  fenetre Generer / Compiler (HmiReportPane) - les memes options (aucune
//  mesure de police : l'estimation, sur un fil de fond ; aucun dossier de
//  projet : les fichiers externes relatifs ne sont pas relus, ni avant ni apres,
//  la comparaison reste juste). Les informations (et le bilan de Compiler) ne
//  sont pas des constats ; un constat que Generer et Compiler disent tous deux
//  est dit une fois, comme l'audit - par Compiler, comme le build (HmiPipeline :
//  la compilation d'abord, la validation ne redit pas ce qu'elle a dit).
//
//  LE CACHE DES CONSTATS D'AVANT. La cle est L'EMPREINTE DU PROJET
//  (mission::printOf : le projet mis en fichiers, sha256 - deja ce que les
//  missions comparent) et les noms de l'automate (Env::plcNameList). Pas le
//  pointeur du projet ni une revision du Document : le verdict tourne sur une
//  COPIE prise pour le fil de fond (une nouvelle a chaque verification), et un
//  hmi::Project ne connait pas la revision de son Document ; l'empreinte ne
//  depend que du contenu - deux copies du meme projet ont la meme. Elle coute
//  une mise en fichiers (bien moins que Generer et Compiler). Huit projets au
//  plus sont gardes (les plus recents), sous un verrou.
// =============================================================================
#include "AiBuild.hpp"

#include "AiMissionStore.hpp"   // printOf : l'empreinte du projet
#include "../../core/Perf.hpp"
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiPipeline.hpp"   // hash (les noms de l'automate)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>
#include <utility>

namespace app::ai::build {

namespace {

const char* const kGenerate = "G\xC3\xA9n\xC3\xA9rer";
const char* const kCompile = "Compiler";
const char* const kDash = " \xE2\x80\x94 ";

// ---- ESSAIS : le faux verdict ------------------------------------------------------------------------------------------
std::mutex                                                     g_fakeMutex;
std::function<Verdict(const hmi::Project&, const Proposal&)>   g_fake;

// ---- un constat de Generer ou de Compiler, sa cle ------------------------------------------------------------------------
struct Raw {
    hmi::Issue  issue;
    const char* step{kGenerate};
};

// La cle d'un constat : ce qui le distingue d'un autre (les identifiants des elements existants sont les memes dans la
// copie ; un objet neuf a un identifiant neuf).
std::string keyOf(const hmi::Issue& i) {
    return std::to_string(static_cast<int>(i.severity)) + "|" + i.category + "|" + std::to_string(i.view) + "|" + std::to_string(i.object) + "|"
         + std::to_string(i.script) + "|" + std::to_string(i.item) + "|" + std::to_string(i.line) + "|" + i.property + "|" + i.message;
}

// Compiler puis Generer sur `p` : les erreurs et les avertissements, chacun une fois (dans l'ordre trouve). Comme le build
// (HmiPipeline : la compilation, puis la validation qui ne redit pas ce que la compilation a dit) : une expression fausse
// que les deux disent est dite par Compiler ; << variable inexistante dans l'IHM : X >> de Generer se tait quand Compiler
// dit deja X dans la meme vue.
std::vector<Raw> findingsOf(const hmi::Project& p, const hmi::NameExists& plc, std::vector<std::string>* keys) {
    std::vector<Raw> out;
    std::unordered_set<std::string> seen;
    const auto take = [&](std::vector<hmi::Issue> issues, const char* step) {
        for (auto& i : issues) {
            if (i.severity == hmi::Issue::Severity::Info || i.category == "Bilan") continue;
            std::string key = keyOf(i);
            if (!seen.insert(key).second) continue;
            if (keys) keys->push_back(std::move(key));
            out.push_back({std::move(i), step});
        }
    };
    take(hmi::compileWith(p, plc), kCompile);
    const std::size_t compiled = out.size();
    const std::string_view prefix = hmi::unknownVariablePrefix();
    const auto saidByCompile = [&](const hmi::Issue& i) {
        if (i.category != "Variable" || i.view == hmi::kNoId || i.message.rfind(prefix, 0) != 0) return false;
        std::string name = i.message.substr(prefix.size());
        if (const auto cut = name.find_first_of(" :"); cut != std::string::npos) name.resize(cut);
        if (name.empty()) return false;
        for (std::size_t k = 0; k < compiled; ++k)
            if (out[k].issue.view == i.view && out[k].issue.severity == hmi::Issue::Severity::Error && out[k].issue.message.find(name) != std::string::npos)
                return true;
        return false;
    };
    hmi::GenerateOptions options;   // comme l'audit : sans mesure de police (l'estimation), sans dossier de projet
    auto generated = hmi::generateWith(p, plc, options);
    std::erase_if(generated, saidByCompile);
    take(std::move(generated), kGenerate);
    return out;
}

// ---- le cache des constats d'avant -------------------------------------------------------------------------------------
struct Known {
    std::string                                           key;
    std::shared_ptr<const std::unordered_set<std::string>> keys;
    std::size_t                                           count{0};
};
std::mutex               g_cacheMutex;
std::deque<Known>        g_cache;            // les plus recents devant
constexpr std::size_t    kCacheSize = 8;
std::atomic<std::size_t> g_beforeRuns{0};   // les projets d'avant generes et compiles (essais)

// La cle du cache : l'empreinte du projet et les noms de l'automate. Vide : pas de cache (un automate dont les noms ne se
// listent pas).
std::string cacheKey(const hmi::Project& p, const Env& env) {
    std::string plc = "-";
    if (env.plcName) {
        if (!env.plcNameList) return {};
        std::vector<std::string> names = env.plcNameList();
        for (auto& n : names)
            for (auto& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::sort(names.begin(), names.end());
        std::string all;
        for (const auto& n : names) all += n + "\n";
        plc = hmi::pipeline::hash(all);
    }
    return mission::printOf(p).all + "|" + plc;
}

Known knownOf(const hmi::Project& before, const hmi::NameExists& plc, const Env& env) {
    const std::string key = cacheKey(before, env);
    if (!key.empty()) {
        const std::lock_guard<std::mutex> lock(g_cacheMutex);
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it)
            if (it->key == key) {
                Known k = *it;
                if (it != g_cache.begin()) {
                    g_cache.erase(it);
                    g_cache.push_front(k);
                }
                return k;
            }
    }
    std::vector<std::string> keys;
    const auto raw = findingsOf(before, plc, &keys);
    ++g_beforeRuns;
    Known k;
    k.key = key;
    k.count = raw.size();
    k.keys = std::make_shared<const std::unordered_set<std::string>>(keys.begin(), keys.end());
    if (!key.empty()) {
        const std::lock_guard<std::mutex> lock(g_cacheMutex);
        std::erase_if(g_cache, [&](const Known& x) { return x.key == key; });
        g_cache.push_front(k);
        while (g_cache.size() > kCacheSize) g_cache.pop_back();
    }
    return k;
}

// ---- l'endroit, lisible (comme l'audit) ----------------------------------------------------------------------------------
std::string placeOf(const hmi::Project& p, const hmi::Issue& i) {
    std::string w;
    if (const hmi::View* v = p.view(i.view)) {
        w = v->name;
        if (const hmi::Object* o = v->object(i.object)) w += " . " + o->name;
    }
    const hmi::Script* sc = i.script != hmi::kNoId ? p.script(i.script) : nullptr;
    if (sc) w += (w.empty() ? "script " : " . ") + sc->name;
    if (!i.property.empty() && !(sc && i.property == sc->name)) w += (w.empty() ? i.category + " " : " . ") + i.property;
    if (i.line > 0) w += ", ligne " + std::to_string(i.line);
    if (w.empty()) w = i.category;
    return w;
}

bool same(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// `name` est-il un mot entier de `text` (le code brut d'une operation) ?
bool mentions(std::string_view text, std::string_view name) {
    if (name.empty()) return false;
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; };
    for (std::size_t at = text.find(name); at != std::string_view::npos; at = text.find(name, at + 1)) {
        const bool left = at == 0 || !ident(text[at - 1]);
        const bool right = at + name.size() >= text.size() || !ident(text[at + name.size()]);
        if (left && right) return true;
    }
    return false;
}

// ---- l'operation en cause --------------------------------------------------------------------------------------------
//  Par les objets que la validation a notes (Check::spots, quand l'appelant les a) ; sinon par le NOM, dans la
//  proposition : l'operation qui cree, modifie, associe ou pose en code cet objet dans sa vue (celle qui donne la
//  propriete en cause d'abord, puis celle qui le cree). Un objet neuf renomme (Cuve_1 pris : Cuve_1_1) se retrouve par le
//  nom voulu. Un script, une fonction, une alarme crees : l'operation de ce nom.
class OpFinder {
public:
    OpFinder(const hmi::Project& before, const Proposal& p, const Check* validated) : before_(before), p_(p), ck_(validated) {}

    std::size_t object(const std::string& view, hmi::Id id, const std::string& name, const std::string& property) const {
        if (ck_)
            for (const auto& s : ck_->spots)
                if (s.message.empty() && s.op != Check::Fault::kNoOp && same(s.object, name) && (s.view.empty() || same(s.view, view))) return s.op;
        const hmi::View* was = nullptr;
        for (const auto& v : before_.views)
            if (same(v.name, view)) was = &v;
        const bool fresh = !was || was->object(id) == nullptr;
        std::size_t best = Check::Fault::kNoOp;
        int score = -1;
        for (std::size_t i = 0; i < p_.ops.size(); ++i) {
            if (std::find(p_.excluded.begin(), p_.excluded.end(), i) != p_.excluded.end()) continue;
            const Operation& op = p_.ops[i];
            if (projectLevel(op)) continue;
            std::string target = opTarget(p_, i);
            if (target.empty()) target = p_.view;
            if (!target.empty() && !view.empty() && !same(target, view)) continue;
            int s = -1;
            const bool gives = std::any_of(op.props.begin(), op.props.end(), [&](const auto& kv) { return !property.empty() && kv.first == property; });
            using A = Operation::Action;
            switch (op.action) {
                case A::Create:
                    if (same(op.name, name)) s = gives ? 4 : 3;
                    else if (fresh && !op.name.empty() && name.size() > op.name.size() && same(std::string_view(name).substr(0, op.name.size()), op.name)
                             && name[op.name.size()] == '_')
                        s = 1;   // renomme (le nom etait pris : Cuve_1 -> Cuve_1_1)
                    break;
                case A::Modify:
                case A::Bind:
                    if (same(op.name, name)) s = gives ? 4 : 2;
                    break;
                case A::Code:
                    if (mentions(op.code, name)) s = 2;
                    break;
                case A::Duplicate:
                    if (std::any_of(op.names.begin(), op.names.end(), [&](const std::string& n) { return same(n, name); })) s = 2;
                    break;
                default:
                    if (std::any_of(op.objects.begin(), op.objects.end(), [&](const std::string& n) { return same(n, name); })) s = 0;
                    break;
            }
            if (s > score) {
                score = s;
                best = i;
            }
        }
        return best;
    }

    // Un element du projet (un script, une fonction, une alarme...) : l'operation qui le cree, par son nom.
    std::size_t element(const std::string& name) const {
        if (name.empty()) return Check::Fault::kNoOp;
        for (std::size_t i = 0; i < p_.ops.size(); ++i) {
            if (std::find(p_.excluded.begin(), p_.excluded.end(), i) != p_.excluded.end()) continue;
            const Operation& op = p_.ops[i];
            using A = Operation::Action;
            if (op.action != A::Script && op.action != A::Function && op.action != A::Alarm && op.action != A::View && op.action != A::Type) continue;
            if (same(op.name, name)) return i;
            for (const auto& [k, v] : op.props)
                if ((k == "nom" || k == "name") && same(v, name)) return i;
        }
        return Check::Fault::kNoOp;
    }

private:
    const hmi::Project& before_;
    const Proposal&     p_;
    const Check*        ck_;
};

Finding findingOf(const hmi::Project& after, const Raw& r, const OpFinder& ops) {
    const hmi::Issue& i = r.issue;
    Finding f;
    f.error = i.severity == hmi::Issue::Severity::Error;
    f.step = r.step;
    f.category = i.category;
    f.message = i.message;
    f.where = placeOf(after, i);
    hmi::Id viewId = i.view;
    if (viewId == hmi::kNoId && i.script != hmi::kNoId) viewId = after.viewOfScript(i.script);
    const hmi::View* v = after.view(viewId);
    if (v) f.view = v->name;
    if (const hmi::Object* o = v ? v->object(i.object) : nullptr) {
        f.object = o->id;
        f.objectName = o->name;
    }
    f.property = i.property;
    f.fixFrom = i.fixFrom;
    f.fixTo = i.fixTo;
    if (f.object != hmi::kNoId) {
        f.op = ops.object(f.view, f.object, f.objectName, f.property);
    } else {
        std::string name;
        if (const hmi::Script* sc = i.script != hmi::kNoId ? after.script(i.script) : nullptr) name = sc->name;
        else if (!i.property.empty()) name = i.property;
        f.op = ops.element(name);
        if (f.op == Check::Fault::kNoOp && v) f.op = ops.element(v->name);   // une vue creee (creer_vue) : ses constats
    }
    return f;
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

Verdict compute(const hmi::Project& before, const Proposal& p, const Env& env, const Check* validated) {
    {
        std::function<Verdict(const hmi::Project&, const Proposal&)> fake;
        {
            const std::lock_guard<std::mutex> lock(g_fakeMutex);
            fake = g_fake;
        }
        if (fake) return fake(before, p);
    }
    XPG_MESURE("ia.avis-logiciel");
    const auto t0 = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    Verdict v;
    // La proposition appliquee a une copie, malgre ses erreurs de validation (ce qui s'applique).
    std::string why;
    std::vector<std::string> skipped;
    const auto after = appliedCopy(before, p, env, &why, &skipped);
    if (!after) {
        v.why = why.empty() ? std::string("la proposition ne s'applique pas") : why;
        v.ms = elapsed();
        return v;
    }
    // Sans automate : aucun nom de l'automate (un nom inconnu de l'IHM est une erreur, comme l'audit et l'Assistant le jugent).
    const hmi::NameExists plc = env.plcName ? hmi::NameExists(env.plcName) : hmi::NameExists([](std::string_view) { return false; });
    const Known known = knownOf(before, plc, env);
    std::vector<std::string> keys;
    const auto raw = findingsOf(*after, plc, &keys);
    v.ran = true;
    v.before = known.count;
    v.after = raw.size();
    const OpFinder ops(before, p, validated);
    std::vector<Finding> errors, warnings;
    for (std::size_t k = 0; k < raw.size(); ++k) {
        if (known.keys && known.keys->count(keys[k])) continue;   // deja la avant la proposition : pas repris
        Finding f = findingOf(*after, raw[k], ops);
        (f.error ? errors : warnings).push_back(std::move(f));
    }
    v.errors = errors.size();
    v.warnings = warnings.size();
    v.findings = std::move(errors);
    for (auto& f : warnings) v.findings.push_back(std::move(f));
    v.ms = elapsed();
    return v;
}

const char* const kOpen = "\xC2\xAB ";    // << (et son espace)
const char* const kClose = " \xC2\xBB";   // >>

} // namespace

// ---- le resume ---------------------------------------------------------------------------------------------------------
std::string countText(const Verdict& v) {
    if (v.errors == 0 && v.warnings == 0) return "aucun constat nouveau";
    if (v.warnings == 0) return plural(v.errors, "erreur nouvelle", "erreurs nouvelles");
    if (v.errors == 0) return plural(v.warnings, "avertissement nouveau", "avertissements nouveaux");
    return plural(v.errors, "erreur", "erreurs") + ", " + plural(v.warnings, "avertissement", "avertissements") + " nouveaux";
}

std::string durationText(double ms) {
    if (ms < 0.0) ms = 0.0;
    if (ms < 1000.0) return std::to_string(static_cast<long long>(std::lround(ms))) + " ms";
    const long long tenths = std::llround(ms / 100.0);
    return std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " s";
}

std::string Verdict::summary() const {
    if (!ran) return std::string("G\xC3\xA9n\xC3\xA9rer et Compiler pas lanc\xC3\xA9s") + (why.empty() ? std::string{} : " : " + why);
    return "G\xC3\xA9n\xC3\xA9rer et Compiler : " + countText(*this);
}

std::string lineOf(const Finding& f) {
    return f.step + " : " + (f.where.empty() ? f.category : f.where) + kDash + f.message;
}

// ---- le verdict --------------------------------------------------------------------------------------------------------
Verdict verdict(const hmi::Project& before, const Proposal& p, const Env& env) { return compute(before, p, env, nullptr); }

Verdict verdict(const hmi::Project& before, const Proposal& p, const Env& env, const Check& validated) {
    return compute(before, p, env, &validated);
}

void merge(Check& ck, const Verdict& v) {
    if (!v.ran) return;
    for (const auto& f : v.findings) {
        const std::string line = lineOf(f);
        auto& list = f.error ? ck.errors : ck.warnings;
        if (std::find(list.begin(), list.end(), line) != list.end()) continue;   // deja verse (merge deux fois : rien de plus)
        list.push_back(line);
        if (f.object != hmi::kNoId || !f.objectName.empty()) ck.spots.push_back({f.op, f.view, f.object, f.objectName, line, f.error});
        if (f.error) {
            Check::Fault fault;
            fault.op = f.op;
            fault.kind = "build";
            fault.name = f.objectName;
            fault.message = line;
            ck.faults.push_back(std::move(fault));
        }
    }
}

std::string feedbackText(const Verdict& v, std::size_t max) {
    if (!v.ran || v.findings.empty()) return {};
    std::string t = "Le logiciel a g\xC3\xA9n\xC3\xA9r\xC3\xA9 et compil\xC3\xA9 le projet avec ta proposition appliqu\xC3\xA9" "e (une copie) : "
                    + countText(v) + ".\n";
    std::size_t shown = 0;
    for (const auto& f : v.findings) {
        if (shown >= max) break;
        ++shown;
        t += "- " + std::string(f.error ? "erreur" : "avertissement") + " (" + f.step + "), " + (f.where.empty() ? f.category : f.where) + " : " + f.message;
        if (f.op != Check::Fault::kNoOp) t += " [op\xC3\xA9ration " + std::to_string(f.op + 1) + "]";
        if (!f.fixFrom.empty()) t += " ; correction du logiciel : remplacer " + (kOpen + f.fixFrom + kClose) + " par " + (kOpen + f.fixTo + kClose);
        t += "\n";
    }
    if (const std::size_t rest = v.findings.size() - shown; rest > 0) t += "- ... et " + plural(rest, "autre", "autres") + ".\n";
    t += "Corrige ta proposition : renvoie-la enti\xC3\xA8re avec proposer_operations, ne touche qu'\xC3\xA0 ce qui est en cause, et dis en une "
         "phrase ce que tu as chang\xC3\xA9. Un avertissement voulu peut rester : dis pourquoi.";
    return t;
}

// ---- le JSON -------------------------------------------------------------------------------------------------------------
json::Value toJson(const Verdict& v) {
    json::Value o = json::Value::object();
    o.set("ran", v.ran);
    if (!v.why.empty()) o.set("why", v.why);
    o.set("ms", std::round(v.ms * 10.0) / 10.0);
    o.set("before", static_cast<std::uint64_t>(v.before));
    o.set("after", static_cast<std::uint64_t>(v.after));
    o.set("errors", static_cast<std::uint64_t>(v.errors));
    o.set("warnings", static_cast<std::uint64_t>(v.warnings));
    json::Value list = json::Value::array();
    for (const auto& f : v.findings) {
        json::Value x = json::Value::object();
        x.set("error", f.error);
        x.set("step", f.step);
        if (!f.category.empty()) x.set("category", f.category);
        x.set("message", f.message);
        if (!f.where.empty()) x.set("where", f.where);
        if (!f.view.empty()) x.set("view", f.view);
        if (f.object != hmi::kNoId) x.set("object", static_cast<std::uint64_t>(f.object));
        if (!f.objectName.empty()) x.set("objectName", f.objectName);
        if (!f.property.empty()) x.set("property", f.property);
        if (f.op != Check::Fault::kNoOp) x.set("op", static_cast<std::uint64_t>(f.op));
        if (!f.fixFrom.empty()) {
            x.set("fixFrom", f.fixFrom);
            x.set("fixTo", f.fixTo);
        }
        list.push(std::move(x));
    }
    o.set("findings", std::move(list));
    return o;
}

Verdict fromJson(const json::Value& o) {
    Verdict v;
    if (!o.isObject()) return v;
    v.ran = o.flag("ran");
    v.why = o.str("why");
    v.ms = o.num("ms");
    const auto count = [&](std::string_view k) { return static_cast<std::size_t>(std::max<std::int64_t>(0, o.integer(k))); };
    v.before = count("before");
    v.after = count("after");
    for (const auto& x : o.at("findings").items()) {
        if (!x.isObject()) continue;
        Finding f;
        f.error = x.flag("error", true);
        f.step = x.str("step");
        f.category = x.str("category");
        f.message = x.str("message");
        f.where = x.str("where");
        f.view = x.str("view");
        const std::int64_t id = x.integer("object");
        f.object = id > 0 ? static_cast<hmi::Id>(id) : hmi::kNoId;
        f.objectName = x.str("objectName");
        f.property = x.str("property");
        const std::int64_t op = x.integer("op", -1);
        f.op = op >= 0 ? static_cast<std::size_t>(op) : Check::Fault::kNoOp;
        f.fixFrom = x.str("fixFrom");
        f.fixTo = x.str("fixTo");
        if (f.message.empty()) continue;
        v.findings.push_back(std::move(f));
    }
    // Les comptes suivent les constats (un fichier abime ne fait pas mentir le resume) ; les erreurs d'abord.
    std::stable_partition(v.findings.begin(), v.findings.end(), [](const Finding& f) { return f.error; });
    v.errors = static_cast<std::size_t>(std::count_if(v.findings.begin(), v.findings.end(), [](const Finding& f) { return f.error; }));
    v.warnings = v.findings.size() - v.errors;
    return v;
}

// ---- les essais ------------------------------------------------------------------------------------------------------
void setVerdictForTests(std::function<Verdict(const hmi::Project&, const Proposal&)> fake) {
    const std::lock_guard<std::mutex> lock(g_fakeMutex);
    g_fake = std::move(fake);
}

std::size_t beforeRunsForTests() noexcept { return g_beforeRuns.load(); }

void clearCacheForTests() {
    const std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cache.clear();
}

} // namespace app::ai::build
