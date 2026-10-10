#include "HmiStore.hpp"
#include "HmiKeys.hpp"            // 1.11.23 : la touche d'un raccourci
#include "HmiEnums.hpp"            // 1.10 (E) : les enumerations IHM
#include "HmiPopupParams.hpp"
#include "HmiTypeRegistry.hpp"     // 1.11.19 (refonte, lot 6) : la cle du type d'une declaration
#include "HmiZones.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace hmi {

namespace {

constexpr const char* kIndexFile = "ihm.txt";

std::string readAll(const fs::path& p, bool& ok) {
    std::ifstream in(p, std::ios::binary);
    ok = static_cast<bool>(in);
    if (!ok) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// Ecrire a cote, puis remplacer d'un renommage : l'ancien fichier reste
// intact tant que le nouveau n'est pas complet.
core::Status writeAtomically(const fs::path& target, const std::string& content) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + tmp.string());
        out << content;
        out.flush();
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + tmp.string());
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        // Windows refuse de renommer par-dessus un fichier ouvert ailleurs :
        // on retire l'ancien et on reessaie, l'ancien restant dans le .tmp
        // jusque-la.
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return core::fail(core::ErrorCode::FileUnreadable,
                                  "remplacement impossible : " + target.string() + " (" + ec.message() + ")");
    }
    return core::ok();
}

std::string sanitize(std::string_view name) {
    std::string out;
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-') out += c;
        else if (c == ' ') out += '_';
    }
    if (out.empty()) out = "vue";
    if (out.size() > 48) out.resize(48);
    return out;
}

// "0031-Demarrage.st" : le corps d'un script general, lisible et comparable
// d'une version a l'autre avec n'importe quel outil de texte.
std::string scriptFileName(const Script& sc) {
    char b[16];
    std::snprintf(b, sizeof b, "%04u-", static_cast<unsigned>(sc.id));
    const char* ext = sc.lang == ScriptLang::ST ? ".st" : sc.lang == ScriptLang::C ? ".c" : ".cpp";
    std::string name = sanitize(sc.name);
    if (name == "vue") name = "script";
    return std::string(b) + name + ext;
}

// "0012-logo_site.png" : l'identifiant d'abord (deux ressources de meme nom
// apres un renommage ne se marchent pas dessus), l'extension gardee.
std::string resourceFileName(const Resource& r) {
    char b[16];
    std::snprintf(b, sizeof b, "%04u-", static_cast<unsigned>(r.id));
    std::string out;
    for (char c : r.name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-' || c == '.') out += c;
        else if (c == ' ') out += '_';
    }
    if (out.empty() || out == "." || out == "..") out = "ressource";
    if (out.size() > 64) out.erase(0, out.size() - 64);
    return std::string(b) + out;
}

// Le fichier sur le disque a-t-il deja exactement ce contenu ? Une ressource
// ne se reecrit pas a chaque enregistrement.
bool sameContent(const fs::path& file, const Bytes& data) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) != data.size()) return false;
    std::ifstream in(file, std::ios::binary);
    std::vector<char> buf(64 * 1024);
    std::size_t at = 0;
    while (in && at < data.size()) {
        in.read(buf.data(), static_cast<std::streamsize>(std::min(buf.size(), data.size() - at)));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0 || std::memcmp(buf.data(), data.data() + at, got) != 0) return false;
        at += got;
    }
    return at == data.size();
}

core::Status writeBytes(const fs::path& target, const Bytes& data) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + tmp.string());
        if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + tmp.string());
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return core::fail(core::ErrorCode::FileUnreadable, "remplacement impossible : " + target.string());
    }
    return core::ok();
}

bool needsQuotes(std::string_view s) {
    if (s.empty()) return true;
    for (char c : s)
        if (std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '=' || c == '\\'
            || c == '#') return true;
    return false;
}

std::string field(std::string_view key, std::string_view value) {
    std::string out = " ";
    out += key;
    out += '=';
    out += quote(value);
    return out;
}
std::string fieldBare(std::string_view key, std::string_view value) {
    std::string out = " ";
    out += key;
    out += '=';
    out += needsQuotes(value) ? quote(value) : std::string(value);
    return out;
}
std::string fieldInt(std::string_view key, long long v) { return fieldBare(key, std::to_string(v)); }
std::string fieldBool(std::string_view key, bool v) { return fieldBare(key, v ? "1" : "0"); }

std::string todayStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min);
    return b;
}

long long toInt(const std::string* s, long long fallback) {
    if (!s || s->empty()) return fallback;
    char* end = nullptr;
    const long long v = std::strtoll(s->c_str(), &end, 10);
    return (end && *end == '\0') ? v : fallback;
}
bool toBool(const std::string* s, bool fallback) {
    if (!s) return fallback;
    return parseBool(*s, fallback);
}
std::string toStr(const std::string* s) { return s ? *s : std::string{}; }

// 1.11.19 (refonte, lot 6) : le registre des types du projet qu'on enregistre (serializeProject le
// pose le temps d'ecrire) - une declaration d'un type IHM y ajoute sa cle (type_cle).
thread_local const typereg::Registry* tSavedTypes = nullptr;

// 1.11.18 (refonte, lot 3) : les declarations d'un code (constantes, variables,
// parametres), chacune sur sa ligne, juste apres la ligne de son porteur (script,
// fonction, fonction_symbole, redefinition, operateur_symbole, operateur_type).
std::string serializeDeclarations(const std::vector<Declaration>& decls) {
    std::string s;
    for (const auto& d : decls) {
        s += "declaration" + fieldInt("id", d.id) + fieldBare("genre", declKindKey(d.kind)) + field("nom", d.name)
           + field("type", d.type);
        // 1.11.19 (lot 6) : un type IHM (ou un tableau, une reference de lui) : sa cle stable,
        // qui le retrouve s'il est renomme hors de l'application. Le format 23 la lit ou l'ignore.
        if (tSavedTypes) {
            const auto r = tSavedTypes->resolve(d.type);
            if (r.ok && r.key.find("ihm:") != std::string::npos) s += field("type_cle", r.key);
        }
        if (!d.value.empty()) s += field("valeur", d.value);
        if (d.kind == DeclKind::Variable) s += fieldBare("stockage", storageKey(d.storage));
        if (d.kind == DeclKind::Parameter) s += fieldBare("mode", passModeKey(d.mode));
        s += fieldBare("visibilite", visibilityKey(d.visibility));
        if (!d.description.empty()) s += field("description", d.description);
        s += "\n";
    }
    return s;
}
// Faux : sans nom, ou d'un genre inconnu (la ligne est ignoree, avec un avertissement).
bool parseDeclaration(const Record& r, Declaration& d) {
    d.id = static_cast<Id>(toInt(r.get("id"), 0));
    const auto kind = declKindFromKey(toStr(r.get("genre")));
    if (!kind) return false;
    d.kind = *kind;
    d.name = toStr(r.get("nom"));
    d.type = toStr(r.get("type"));
    d.typeKey = toStr(r.get("type_cle"));                 // 1.11.19 (lot 6) : suivi au chargement, puis vide
    d.value = toStr(r.get("valeur"));
    d.description = toStr(r.get("description"));
    d.storage = storageFromKey(toStr(r.get("stockage"))).value_or(Storage::Execution);
    d.mode = passModeFromKey(toStr(r.get("mode"))).value_or(PassMode::In);
    d.visibility = visibilityFromKey(toStr(r.get("visibilite"))).value_or(Visibility::Public);
    return !d.name.empty();
}
// Le message d'une ligne "declaration" ignoree.
std::string declarationIgnored(bool holder) {
    return holder ? "d\xC3\xA9" "claration sans nom ou d'un genre inconnu : ignor\xC3\xA9" "e"
                  : "d\xC3\xA9" "claration hors d'un script, d'une fonction ou d'un op\xC3\xA9rateur : ignor\xC3\xA9" "e";
}

// 1.10 : un operateur d'un symbole ("operateur_symbole", dans le fichier de la vue)
// ou d'un type IHM ("operateur_type", dans l'index, apres les membres du type).
std::string serializeOperator(std::string_view word, const HmiOperator& o) {
    std::string s(word);
    s += fieldInt("id", o.id) + field("op", o.op) + field("gauche", o.left);
    if (!o.right.empty()) s += field("droite", o.right);
    if (!o.result.empty()) s += field("resultat", o.result);
    if (!o.description.empty()) s += field("description", o.description);
    return s + field("corps", o.body) + "\n" + serializeDeclarations(o.decls);   // 1.11.18 (lot 3)
}
// Faux : ni genre ni operande (la ligne est ignoree, avec un avertissement).
bool parseOperator(const Record& r, HmiOperator& o) {
    o.id = static_cast<Id>(toInt(r.get("id"), 0));
    o.op = toStr(r.get("op"));
    o.left = toStr(r.get("gauche"));
    o.right = toStr(r.get("droite"));
    o.result = toStr(r.get("resultat"));
    o.description = toStr(r.get("description"));
    o.body = toStr(r.get("corps"));
    return !o.op.empty() && !o.left.empty();
}

} // namespace

// ------------------------------------------------------------ records -----
std::string quote(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    out += '"';
    return out;
}

const std::string* Record::get(std::string_view key) const noexcept {
    for (const auto& f : fields) if (f.first == key) return &f.second;
    return nullptr;
}

bool parseRecord(std::string_view line, Record& out, std::string& error) {
    out.word.clear();
    out.fields.clear();
    std::size_t i = 0;
    auto skipSpace = [&] { while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i; };
    auto readValue = [&](std::string& v) -> bool {
        v.clear();
        if (i < line.size() && line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    const char e = line[i + 1];
                    v += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                    i += 2;
                } else {
                    v += line[i++];
                }
            }
            if (i >= line.size()) { error = "guillemet non ferm\xC3\xA9"; return false; }
            ++i;
            return true;
        }
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) v += line[i++];
        return true;
    };
    skipSpace();
    while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) out.word += line[i++];
    for (;;) {
        skipSpace();
        if (i >= line.size()) break;
        std::string key;
        while (i < line.size() && line[i] != '=' && !std::isspace(static_cast<unsigned char>(line[i])))
            key += line[i++];
        if (i >= line.size() || line[i] != '=') { error = "'" + key + "' sans '='"; return false; }
        ++i;
        std::string value;
        if (!readValue(value)) return false;
        out.fields.emplace_back(std::move(key), std::move(value));
    }
    return true;
}

// -------------------------------------------------------------- vues ------
std::string viewFileName(const View& v) {
    char b[16];
    std::snprintf(b, sizeof b, "%04u-", static_cast<unsigned>(v.id));
    return std::string(b) + sanitize(v.name) + ".vue";
}

std::string serializeAction(Id owner, const Action& a) {
    const auto& t = a.transition;
    std::string s = "action" + fieldInt("objet", owner) + field("declencheur", triggerLabel(a.trigger));
    if (!a.watch.empty()) s += field("surveille", a.watch);
    if (a.delayMs != 0) s += fieldInt("delai", a.delayMs);
    if (!a.guard.empty()) s += field("garde", a.guard);
    s += field("operation", operationLabel(a.operation));
    if (!a.target.empty()) s += field("cible", a.target);
    if (!a.value.empty()) s += field("valeur", a.value);
    if (!a.placement.empty()) s += field("position", a.placement);   // lot 8
    if (!a.params.empty()) s += field("parametres", a.params);        // 1.11.6 : Maths, clavier virtuel
    if (!a.key.empty()) s += field("touche", a.key);                  // 1.11.23 : un raccourci
    // Lot API 8 : "Demander ou enregistrer", ecrit seulement decoche - absent (un
    // projet d'avant, ou coche) se relit coche : les fichiers d'avant ne changent pas.
    if (!a.askWhere) s += fieldBool("demander_ou", false);
    if (t.kind != TransitionKind::Instant || operationOpensView(a.operation) || a.operation == Operation::ClosePopup) {
        s += field("transition", transitionLabel(t.kind)) + fieldInt("duree", t.durationMs) + field("direction", t.direction)
           + field("courbe", t.easing);
        if (t.kind == TransitionKind::Custom)
            s += fieldBare("opacite", formatNumber(t.fromOpacity)) + fieldBare("dx", formatNumber(t.fromOffsetX))
               + fieldBare("dy", formatNumber(t.fromOffsetY)) + fieldBare("echelle", formatNumber(t.fromScale))
               + fieldBare("angle", formatNumber(t.fromAngle));
    }
    return s + "\n";
}

std::string serializeView(const View& v) {
    std::string s;
    s += "# XpgAnalyzer - vue IHM (format " + std::to_string(kFormatVersion) + ")\n";
    s += "vue" + fieldInt("id", v.id) + field("nom", v.name) + field("description", v.description)
       + fieldInt("largeur", v.width) + fieldInt("hauteur", v.height) + field("fond", v.background)
       + fieldInt("calque_actif", v.activeLayer);
    // Lot 6 (format 5) : le role et l'heritage, seulement quand ils disent quelque chose.
    if (v.role != "vue") s += fieldBare("role", v.role);
    if (v.templateView != kNoId) s += fieldInt("modele", v.templateView);
    if (v.showHeader || v.header != kNoId) s += fieldBool("entete", v.showHeader) + fieldInt("entete_vue", v.header);
    if (v.showFooter || v.footer != kNoId) s += fieldBool("pied", v.showFooter) + fieldInt("pied_vue", v.footer);
    // Lot 12 (format 11) : la vue parente (fil d'Ariane), le zoom en marche.
    if (v.upView != kNoId) s += fieldInt("vue_parente", v.upView);
    if (v.zoomable) s += fieldBool("zoom", v.zoomable);
    // 1.11.10 : une popup d'un symbole - le symbole qui la porte (une version plus ancienne l'ignore).
    if (v.ownerSymbol != kNoId) s += fieldInt("symbole", v.ownerSymbol);
    // Lot 8 (format 7) : les reglages de popup, quand ils different du defaut.
    const PopupSettings defaults;
    if (v.popup != defaults) {
        const auto& pp = v.popup;
        s += fieldBool("popup_titre", pp.titleBar) + field("popup_libelle", pp.title) + fieldBool("popup_modale", pp.modal)
           + fieldBool("popup_deplacable", pp.movable) + fieldBool("popup_fermer", pp.closeButton)
           + fieldBool("popup_dehors", pp.closeOutside) + field("popup_position", pp.placement);
    }
    s += "\n";
    for (const auto& prm : v.params)
        s += "parametre" + field("nom", prm.name) + field("defaut", prm.defaultValue) + field("description", prm.description)
           // 1.9 : le type (vide : ANY) et le mode (absent : reference), seulement s'ils servent
           + (prm.type.empty() ? std::string{} : field("type", prm.type))
           + (prm.mode == ParamMode::Reference ? std::string{} : field("mode", std::string(params::paramModeKey(prm.mode))))
           + "\n";
    // 1.9 : les alarmes d'un symbole (leurs textes citent les parametres).
    for (const auto& a : v.alarms)
        s += "alarme_symbole" + fieldInt("id", a.id) + field("nom", a.name) + field("condition", a.condition)
           + field("message", a.message) + fieldInt("priorite", a.priority) + field("categorie", a.category)
           + field("groupe", a.group) + fieldBool("acquittement", a.ackRequired) + fieldInt("delai", a.delayMs)
           + field("description", a.description) + field("consigne", a.instruction) + "\n";
    for (const auto& o : v.operators) s += serializeOperator("operateur_symbole", o);   // 1.10
    // 1.11.10 : les fonctions d'un symbole, leur corps sur la ligne (une version plus ancienne les ignore).
    for (const auto& fn : v.functions)
        s += "fonction_symbole" + fieldInt("id", fn.id) + field("nom", fn.name) + field("retour", fn.returnType)
           + (fn.isVirtual ? fieldBool("virtuelle", true) : std::string{})
           + (fn.description.empty() ? std::string{} : field("description", fn.description)) + field("corps", fn.body) + "\n"
           + serializeDeclarations(fn.decls);                                     // 1.11.18 (lot 3)
    s += "grille" + fieldBool("visible", v.grid.visible) + fieldInt("pas", v.grid.step)
       + fieldBool("magnetisme_grille", v.grid.snapGrid)
       + fieldBool("magnetisme_objets", v.grid.snapObjects)
       + fieldBool("magnetisme_guides", v.grid.snapGuides) + "\n";
    for (const auto& g : v.guides)
        s += "guide" + fieldBare("sens", g.vertical ? "V" : "H") + fieldBare("position", formatNumber(g.position)) + "\n";
    for (const auto& l : v.layers)
        s += "calque" + fieldInt("id", l.id) + field("nom", l.name) + fieldBool("visible", l.visible)
           + fieldBool("verrou", l.locked) + "\n";
    for (const auto& o : v.objects) {
        s += "objet" + fieldInt("id", o.id) + fieldBare("type", kindKey(o.kind)) + field("nom", o.name)
           + fieldInt("calque", o.layer) + fieldInt("parent", o.parent) + fieldBool("verrou", o.locked)
           + fieldBool("cache", o.hidden) + "\n";
        for (const auto& p : o.props) {
            s += "prop" + fieldBare("cle", p.key) + field("valeur", p.value);
            if (!p.expr.empty()) s += field("expr", p.expr);
            s += "\n";
        }
        // 1.11.10 : ses fonctions redefinies (une instance).
        for (const auto& fo : o.functionOverrides)
            s += "redefinition" + field("fonction", fo.function) + field("corps", fo.body) + "\n"
               + serializeDeclarations(fo.decls);                                   // 1.11.18 (lot 3)
        // 1.9 : ses alarmes surchargees - seulement les champs surcharges.
        for (const auto& ov : o.alarmOverrides) {
            s += "surcharge_alarme" + field("alarme", ov.alarm);
            if (!ov.path.empty()) s += field("chemin", ov.path);
            if (ov.active) s += fieldBool("active", *ov.active);
            if (ov.condition) s += field("condition", *ov.condition);
            if (ov.message) s += field("message", *ov.message);
            if (ov.priority) s += fieldInt("priorite", *ov.priority);
            if (ov.category) s += field("categorie", *ov.category);
            if (ov.group) s += field("groupe", *ov.group);
            if (ov.delayMs) s += fieldInt("delai", *ov.delayMs);
            if (ov.ackRequired) s += fieldBool("acquittement", *ov.ackRequired);
            if (ov.instruction) s += field("consigne", *ov.instruction);
            if (ov.description) s += field("description", *ov.description);
            s += "\n";
        }
    }
    for (const auto& sc : v.scripts)
        s += "script" + fieldInt("id", sc.id) + field("nom", sc.name) + fieldBare("langage", scriptLangKey(sc.lang))
           + fieldBare("evenement", sc.event.empty() ? "-" : sc.event) + field("corps", sc.body) + "\n"
           + serializeDeclarations(sc.decls);                                     // 1.11.18 (lot 3)
    // Les actions, apres tous les objets : chacune designe le sien (0 = la vue).
    for (const auto& a : v.actions) s += serializeAction(kNoId, a);
    for (const auto& o : v.objects)
        for (const auto& a : o.actions) s += serializeAction(o.id, a);
    s += "fin\n";
    return s;
}

// Une ligne "action" relue. Faux quand le declencheur ou l'operation est inconnu.
bool parseAction(const Record& r, Action& a, std::string& why) {
    const auto trigger = triggerFromLabel(toStr(r.get("declencheur")));
    const auto operation = operationFromLabel(toStr(r.get("operation")));
    if (!trigger) { why = "d\xC3\xA9" "clencheur inconnu '" + toStr(r.get("declencheur")) + "'"; return false; }
    if (!operation) { why = "op\xC3\xA9ration inconnue '" + toStr(r.get("operation")) + "'"; return false; }
    a.trigger = *trigger;
    a.operation = *operation;
    a.watch = toStr(r.get("surveille"));
    a.delayMs = static_cast<int>(toInt(r.get("delai"), 0));
    a.guard = toStr(r.get("garde"));
    a.target = toStr(r.get("cible"));
    if (operationWritesVariable(a.operation)) a.target = targetVariable(a.target);   // 1.11.7 : "=Vanne.CMD_OUV" d'avant
    a.value = toStr(r.get("valeur"));
    a.placement = toStr(r.get("position"));
    a.params = toStr(r.get("parametres"));                  // 1.11.6
    // 1.11.23 : la touche d'un raccourci, remise sous sa forme enregistree si elle se lit ;
    // sinon gardee telle quelle (Compiler la dit).
    a.key = toStr(r.get("touche"));
    if (const auto chord = keys::parseChord(a.key)) a.key = keys::canonical(*chord);
    a.askWhere = toBool(r.get("demander_ou"), true);        // Lot API 8 : absent (projet d'avant) = coche
    auto& t = a.transition;
    t.kind = transitionFromLabel(toStr(r.get("transition"))).value_or(TransitionKind::Instant);
    t.durationMs = static_cast<int>(toInt(r.get("duree"), 400));
    if (const auto* d = r.get("direction")) t.direction = *d;
    if (const auto* c = r.get("courbe")) t.easing = *c;
    const auto number = [&](const char* key, double fallback) {
        double v = fallback;
        if (const auto* x = r.get(key)) (void)parseNumber(*x, v);
        return v;
    };
    t.fromOpacity = number("opacite", 0);
    t.fromOffsetX = number("dx", 0);
    t.fromOffsetY = number("dy", 0);
    t.fromScale = number("echelle", 1);
    t.fromAngle = number("angle", 0);
    return true;
}

core::Result<View> parseView(std::string_view text, LoadReport* report) {
    View v;
    bool sawView = false, sawEnd = false;
    int fileFormat = kFormatVersion;     // "# XpgAnalyzer - vue IHM (format 6)" : l'en-tete le dit
    Object* current = nullptr;
    std::vector<Declaration>* holder = nullptr;   // 1.11.18 (lot 3) : le porteur des lignes "declaration" qui suivent
    std::size_t lineNo = 0;
    std::size_t pos = 0;
    auto warn = [&](std::string w) { if (report) report->warnings.push_back(std::move(w)); };
    while (pos <= text.size()) {
        const std::size_t eol = text.find('\n', pos);
        std::string_view line = text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
        pos = eol == std::string_view::npos ? text.size() + 1 : eol + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::size_t k = 0;
        while (k < line.size() && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
        if (k < line.size() && line[k] == '#') {
            if (const auto at = line.find("(format "); at != std::string_view::npos && lineNo <= 3)
                fileFormat = static_cast<int>(std::strtol(std::string(line.substr(at + 8)).c_str(), nullptr, 10));
            continue;
        }
        if (k >= line.size()) continue;
        if (sawEnd) { warn("ligne " + std::to_string(lineNo) + " apr\xC3\xA8s 'fin' ignor\xC3\xA9" "e"); continue; }

        Record r;
        std::string error;
        if (!parseRecord(line, r, error))
            return core::fail(core::ErrorCode::XmlMalformed,
                              "ligne " + std::to_string(lineNo) + " : " + error);
        if (r.word != "declaration") holder = nullptr;
        if (r.word == "declaration") {                                // 1.11.18 (lot 3)
            Declaration d;
            if (!holder || !parseDeclaration(r, d)) { warn("ligne " + std::to_string(lineNo) + " : " + declarationIgnored(holder)); continue; }
            holder->push_back(std::move(d));
        } else if (r.word == "vue") {
            sawView = true;
            v.id = static_cast<Id>(toInt(r.get("id"), 0));
            v.name = toStr(r.get("nom"));
            v.description = toStr(r.get("description"));
            v.width = static_cast<int>(toInt(r.get("largeur"), 1920));
            v.height = static_cast<int>(toInt(r.get("hauteur"), 1080));
            v.background = r.get("fond") ? *r.get("fond") : v.background;
            v.activeLayer = static_cast<Id>(toInt(r.get("calque_actif"), 0));
            if (const auto role = toStr(r.get("role")); !role.empty()) v.role = role;
            v.templateView = static_cast<Id>(toInt(r.get("modele"), 0));
            v.showHeader = toBool(r.get("entete"), false);
            v.header = static_cast<Id>(toInt(r.get("entete_vue"), 0));
            v.showFooter = toBool(r.get("pied"), false);
            v.footer = static_cast<Id>(toInt(r.get("pied_vue"), 0));
            v.upView = static_cast<Id>(toInt(r.get("vue_parente"), 0));      // lot 12
            v.zoomable = toBool(r.get("zoom"), false);
            v.ownerSymbol = static_cast<Id>(toInt(r.get("symbole"), 0));   // 1.11.10 : une popup d'un symbole
            // Lot 8
            auto& pp = v.popup;
            pp.titleBar = toBool(r.get("popup_titre"), pp.titleBar);
            pp.title = toStr(r.get("popup_libelle"));
            pp.modal = toBool(r.get("popup_modale"), pp.modal);
            pp.movable = toBool(r.get("popup_deplacable"), pp.movable);
            pp.closeButton = toBool(r.get("popup_fermer"), pp.closeButton);
            pp.closeOutside = toBool(r.get("popup_dehors"), pp.closeOutside);
            if (const auto* pl = r.get("popup_position"); pl && !pl->empty()) pp.placement = *pl;
        } else if (r.word == "parametre") {
            ViewParam prm;
            prm.name = toStr(r.get("nom"));
            prm.defaultValue = toStr(r.get("defaut"));
            prm.description = toStr(r.get("description"));
            // 1.9 : un projet d'avant n'a ni type (ANY) ni mode (Reference)
            prm.type = toStr(r.get("type"));
            if (const auto* m = r.get("mode"); m && !m->empty()) prm.mode = params::paramModeFrom(*m);
            if (prm.name.empty()) { warn("ligne " + std::to_string(lineNo) + " : param\xC3\xA8tre sans nom"); continue; }
            v.params.push_back(std::move(prm));
        } else if (r.word == "alarme_symbole") {                      // 1.9
            AlarmDef a;
            a.id = static_cast<Id>(toInt(r.get("id"), 0));
            a.name = toStr(r.get("nom"));
            a.condition = toStr(r.get("condition"));
            a.message = toStr(r.get("message"));
            a.priority = static_cast<int>(std::clamp<long long>(toInt(r.get("priorite"), 3), 1, kAlarmPriorities));
            a.category = toStr(r.get("categorie"));
            if (a.category.empty()) a.category = "Alarme";
            a.group = toStr(r.get("groupe"));
            a.ackRequired = toBool(r.get("acquittement"), true);
            a.delayMs = static_cast<int>(std::max<long long>(0, toInt(r.get("delai"), 0)));
            a.description = toStr(r.get("description"));
            a.instruction = toStr(r.get("consigne"));
            if (a.name.empty()) { warn("ligne " + std::to_string(lineNo) + " : alarme de symbole sans nom"); continue; }
            v.alarms.push_back(std::move(a));
        } else if (r.word == "operateur_symbole") {                   // 1.10
            HmiOperator o;
            if (!parseOperator(r, o)) { warn("ligne " + std::to_string(lineNo) + " : op\xC3\xA9rateur sans genre ou sans op\xC3\xA9rande"); continue; }
            v.operators.push_back(std::move(o));
            holder = &v.operators.back().decls;
        } else if (r.word == "fonction_symbole") {                    // 1.11.10
            HmiFunction fn;
            fn.id = static_cast<Id>(toInt(r.get("id"), 0));
            fn.name = toStr(r.get("nom"));
            fn.returnType = toStr(r.get("retour"));
            fn.isVirtual = toBool(r.get("virtuelle"), false);
            fn.description = toStr(r.get("description"));
            fn.body = toStr(r.get("corps"));
            if (fn.name.empty()) { warn("ligne " + std::to_string(lineNo) + " : fonction de symbole sans nom"); continue; }
            v.functions.push_back(std::move(fn));
            holder = &v.functions.back().decls;
        } else if (r.word == "redefinition") {                        // 1.11.10
            if (!current) { warn("ligne " + std::to_string(lineNo) + " : red\xC3\xA9" "finition hors d'un objet"); continue; }
            FunctionOverride fo;
            fo.function = toStr(r.get("fonction"));
            fo.body = toStr(r.get("corps"));
            if (fo.function.empty()) { warn("ligne " + std::to_string(lineNo) + " : red\xC3\xA9" "finition sans fonction"); continue; }
            current->functionOverrides.push_back(std::move(fo));
            holder = &current->functionOverrides.back().decls;
        } else if (r.word == "surcharge_alarme") {                    // 1.9
            if (!current) { warn("ligne " + std::to_string(lineNo) + " : surcharge d'alarme hors d'un objet"); continue; }
            AlarmOverride ov;
            ov.alarm = toStr(r.get("alarme"));
            ov.path = toStr(r.get("chemin"));
            if (ov.alarm.empty()) { warn("ligne " + std::to_string(lineNo) + " : surcharge d'alarme sans nom"); continue; }
            if (r.get("active")) ov.active = toBool(r.get("active"), true);
            if (const auto* x = r.get("condition")) ov.condition = *x;
            if (const auto* x = r.get("message")) ov.message = *x;
            if (r.get("priorite"))
                ov.priority = static_cast<int>(std::clamp<long long>(toInt(r.get("priorite"), 3), 1, kAlarmPriorities));
            if (const auto* x = r.get("categorie")) ov.category = *x;
            if (const auto* x = r.get("groupe")) ov.group = *x;
            if (r.get("delai")) ov.delayMs = static_cast<int>(std::max<long long>(0, toInt(r.get("delai"), 0)));
            if (r.get("acquittement")) ov.ackRequired = toBool(r.get("acquittement"), true);
            if (const auto* x = r.get("consigne")) ov.instruction = *x;
            if (const auto* x = r.get("description")) ov.description = *x;
            current->alarmOverrides.push_back(std::move(ov));
        } else if (r.word == "grille") {
            v.grid.visible = toBool(r.get("visible"), true);
            v.grid.step = static_cast<int>(toInt(r.get("pas"), 10));
            v.grid.snapGrid = toBool(r.get("magnetisme_grille"), true);
            v.grid.snapObjects = toBool(r.get("magnetisme_objets"), true);
            v.grid.snapGuides = toBool(r.get("magnetisme_guides"), true);
        } else if (r.word == "guide") {
            Guide g;
            g.vertical = toStr(r.get("sens")) != "H";
            double p = 0;
            if (const auto* s = r.get("position")) (void)parseNumber(*s, p);
            g.position = p;
            v.guides.push_back(g);
        } else if (r.word == "calque") {
            Layer l;
            l.id = static_cast<Id>(toInt(r.get("id"), 0));
            l.name = toStr(r.get("nom"));
            l.visible = toBool(r.get("visible"), true);
            l.locked = toBool(r.get("verrou"), false);
            v.layers.push_back(std::move(l));
        } else if (r.word == "objet") {
            Object o;
            o.id = static_cast<Id>(toInt(r.get("id"), 0));
            const auto kind = kindFromKey(toStr(r.get("type")));
            if (!kind) {
                warn("ligne " + std::to_string(lineNo) + " : type d'objet inconnu '" + toStr(r.get("type"))
                     + "', lu comme un rectangle");
            }
            o.kind = kind.value_or(Kind::Rectangle);
            o.name = toStr(r.get("nom"));
            o.layer = static_cast<Id>(toInt(r.get("calque"), 0));
            o.parent = static_cast<Id>(toInt(r.get("parent"), 0));
            o.locked = toBool(r.get("verrou"), false);
            o.hidden = toBool(r.get("cache"), false);
            v.objects.push_back(std::move(o));
            current = &v.objects.back();
        } else if (r.word == "prop") {
            if (!current)
                return core::fail(core::ErrorCode::XmlMalformed,
                                  "ligne " + std::to_string(lineNo) + " : propri\xC3\xA9t\xC3\xA9 hors d'un objet");
            Prop p;
            p.key = toStr(r.get("cle"));
            p.value = toStr(r.get("valeur"));
            p.expr = toStr(r.get("expr"));
            if (p.key.empty()) { warn("ligne " + std::to_string(lineNo) + " : propri\xC3\xA9t\xC3\xA9 sans cl\xC3\xA9"); continue; }
            current->props.push_back(std::move(p));
        } else if (r.word == "script") {
            Script sc;
            sc.id = static_cast<Id>(toInt(r.get("id"), 0));
            sc.name = toStr(r.get("nom"));
            sc.lang = scriptLangFromKey(toStr(r.get("langage"))).value_or(ScriptLang::ST);
            sc.event = toStr(r.get("evenement"));
            if (sc.event == "-") sc.event.clear();
            sc.body = toStr(r.get("corps"));
            v.scripts.push_back(std::move(sc));
            holder = &v.scripts.back().decls;
        } else if (r.word == "action") {
            Action a;
            std::string why;
            if (!parseAction(r, a, why)) { warn("ligne " + std::to_string(lineNo) + " : action ignor\xC3\xA9" "e (" + why + ")"); continue; }
            // Avant le format 7 (lot 8), la valeur d'une navigation ou d'une popup
            // ne servait a rien (un reste d'une autre operation) : ce sont
            // maintenant ses parametres, on ne la reprend pas.
            if (fileFormat > 0 && fileFormat < 7 && operationTakesArguments(a.operation)) a.value.clear();
            const Id owner = static_cast<Id>(toInt(r.get("objet"), 0));
            if (owner == kNoId) {
                v.actions.push_back(std::move(a));
            } else if (auto* o = v.object(owner)) {
                o->actions.push_back(std::move(a));
            } else {
                warn("ligne " + std::to_string(lineNo) + " : action d'un objet " + std::to_string(owner) + " introuvable");
            }
        } else if (r.word == "fin") {
            sawEnd = true;
        } else {
            warn("ligne " + std::to_string(lineNo) + " : '" + r.word + "' inconnu, ignore");
        }
    }
    if (!sawView) return core::fail(core::ErrorCode::XmlMalformed, "aucune ligne 'vue'");
    if (!sawEnd)
        return core::fail(core::ErrorCode::FileEmpty,
                          "fichier coup\xC3\xA9 : la ligne 'fin' manque (vue '" + v.name + "')");
    if (v.layers.empty()) {
        Layer l;
        l.id = v.activeLayer != kNoId ? v.activeLayer : 0xFFFFFF00u;
        l.name = "Calque 1";
        v.layers.push_back(l);
        warn("vue '" + v.name + "' sans calque : un calque est ajoute");
    }
    for (auto& o : v.objects)
        if (v.layerRank(o.layer) < 0) {
            warn("objet '" + o.name + "' : calque " + std::to_string(o.layer) + " introuvable, range dans '"
                 + v.layers.front().name + "'");
            o.layer = v.layers.front().id;
        }
    // 1.10.4 : une vanne 3 voies qu'une version plus ancienne a reenregistree -
    // elle l'avait lue comme un rectangle ("type d'objet inconnu"), en gardant
    // toutes ses proprietes. Un rectangle n'a ni "valve3Function" ni
    // "positionMode" : elle redevient une vanne 3 voies, rien n'est perdu.
    for (auto& o : v.objects)
        if (o.kind == Kind::Rectangle && o.find("valve3Function") && o.find("positionMode")) {
            o.kind = Kind::ThreeWayValve;
            warn("objet '" + o.name + "' : enregistr\xC3\xA9 en rectangle par une version plus ancienne, relu comme une vanne 3 voies");
        }
    if (!v.layer(v.activeLayer)) v.activeLayer = v.layers.front().id;
    return v;
}

// ------------------------------------------------------------ projet ------
std::string nowStamp() { return todayStamp(); }

bool exists(const std::string& projectFolder) {
    std::error_code ec;
    return fs::exists(fs::path(projectFolder) / "ihm" / kIndexFile, ec);
}

namespace {

std::string joinList(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? ";" : "") + items[i];
    return out;
}
std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= s.size() && !s.empty()) {
        const std::size_t at = s.find(';', from);
        std::string item = s.substr(from, at == std::string::npos ? std::string::npos : at - from);
        if (!item.empty()) out.push_back(std::move(item));
        if (at == std::string::npos) break;
        from = at + 1;
    }
    return out;
}

std::shared_ptr<const Bytes> textBytes(const std::string& text) {
    return std::make_shared<const Bytes>(text.begin(), text.end());
}

} // namespace

std::vector<ProjectFile> serializeProject(const Project& p) {
    std::vector<ProjectFile> files;
    // 1.11.19 (lot 6) : les cles des types des declarations, le temps d'ecrire.
    const auto savedTypes = typereg::Registry::build(p);
    struct TypesScope {
        const typereg::Registry* before;
        explicit TypesScope(const typereg::Registry* r) : before(tSavedTypes) { tSavedTypes = r; }
        ~TypesScope() { tSavedTypes = before; }
    } typesScope(savedTypes.get());
    const Config& cfg = p.config;
    // 1.10.2 (AL) : le format 22 n'ajoute que les groupes d'alarmes regles et les liens.
    // Un projet qui n'en a pas s'ecrit encore au format 21 : la 1.10.1 l'ouvre.
    // 1.11.18 (lot 3) : le format 23 n'ajoute que les declarations du modele ; un projet
    // qui n'en a pas s'ecrit au format 22 (ou 21) : la 1.11.17 l'ouvre.
    bool declared = false;
    forEachDeclarations(p, [&](const std::vector<Declaration>& list) { declared = declared || !list.empty(); });
    // 1.11.23 : le format 24 n'ajoute que les raccourcis des vues ; un projet qui n'en a pas
    // s'ecrit au format 23 (ou 22, 21) : la 1.11.22 l'ouvre.
    bool shortcuts = false;
    const auto keyed = [](const std::vector<Action>& list) {
        return std::any_of(list.begin(), list.end(), [](const Action& a) { return triggerIsKey(a.trigger) || !a.key.empty(); });
    };
    for (const auto& v : p.views) {
        shortcuts = shortcuts || keyed(v.actions);
        for (const auto& o : v.objects) shortcuts = shortcuts || keyed(o.actions);
    }
    const int format = shortcuts ? kFormatVersion : declared ? 23 : (p.alarmGroups.empty() && p.alarmGroupLinks.empty()) ? 21 : 22;
    std::string index;
    index += "# XpgAnalyzer - projet IHM (format " + std::to_string(format) + ")\n";
    index += "ihm" + fieldInt("format", format) + fieldInt("prochain_id", p.nextId) + "\n";
    index += "config" + field("nom", cfg.name) + field("description", cfg.description)
           + field("version", cfg.version) + field("auteur", cfg.author) + field("cree", cfg.created)
           + field("modifie", cfg.modified) + fieldInt("largeur", cfg.width) + fieldInt("hauteur", cfg.height)
           + field("orientation", cfg.orientation) + fieldInt("demarrage", cfg.startView)
           + fieldInt("cycle", cfg.cycleMs) + (cfg.swipeNavigation ? fieldBool("glisser", true) : std::string{})
           // Lot 13 : Generer plus exigeant - ecrit seulement s'il change.
           + (!cfg.quality ? fieldBool("qualite", false) : std::string{})
           + (cfg.touchMin != 32 ? fieldInt("cible", cfg.touchMin) : std::string{})
           + (cfg.contrastMin != 3.0 ? fieldBare("contraste", formatNumber(cfg.contrastMin)) : std::string{})
           + (cfg.heavyObjects != 400 ? fieldInt("objets_max", cfg.heavyObjects) : std::string{})
           // Lot 13 : l'affichage au lancement (taille des textes, couleurs, symboles, theme).
           + (cfg.textScale != 100 ? fieldInt("texte_pct", cfg.textScale) : std::string{})
           + (cfg.colorMode != "normal" ? field("couleurs", cfg.colorMode) : std::string{})
           + (cfg.statusSymbols ? fieldBool("symboles", true) : std::string{})
           + (cfg.theme != "nuit" ? field("theme", cfg.theme) : std::string{}) + "\n";
    for (const auto& v : p.views) {
        const std::string name = viewFileName(v);
        std::string text = serializeView(v);
        if (format != kFormatVersion)   // 1.10.2 : l'en-tete de la vue dit le meme format que l'index
            if (const auto at = text.find("(format " + std::to_string(kFormatVersion) + ")"); at != std::string::npos && at < 80)
                text.replace(at, 8 + std::to_string(kFormatVersion).size(), "(format " + std::to_string(format));
        files.push_back({"vues/" + name, textBytes(text)});
        index += "vue" + fieldInt("id", v.id) + field("fichier", "vues/" + name)
               + (v.folder.empty() ? std::string{} : field("dossier", v.folder)) + "\n";   // lot 21 : son dossier
    }
    // Les ressources : leur fichier dans ihm/ressources/, ce qu'on en sait dans
    // l'index (pour le montrer meme si le fichier venait a manquer).
    for (const auto& r : p.assets.resources) {
        const std::string file = resourceFileName(r);
        if (r.data) files.push_back({"ressources/" + file, r.data});
        index += "ressource" + fieldInt("id", r.id) + field("nom", r.name) + field("fichier", "ressources/" + file)
               + fieldBare("format", r.format.empty() ? "-" : r.format) + fieldInt("octets", static_cast<long long>(r.bytes))
               + fieldInt("largeur", r.width) + fieldInt("hauteur", r.height) + fieldBare("duree", formatNumber(r.seconds))
               + field("detail", r.detail) + field("origine", r.origin) + field("ajoute", r.added)
               + (r.folder.empty() ? std::string{} : field("dossier", r.folder)) + "\n";   // lot 21
    }
    // Lot 20 : les modeles de vues du projet - leur paquet dans ihm/modeles/.
    for (const auto& m : p.viewTemplates) {
        std::string stem;
        for (const char ch : m.name) stem += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') ? ch : '_';
        if (stem.empty()) stem = "modele";
        char num[16];
        std::snprintf(num, sizeof num, "%04llu-", static_cast<unsigned long long>(m.id));
        const std::string file = "modeles/" + std::string(num) + stem + ".xpgmodele";
        if (m.data) files.push_back({file, m.data});
        index += "modele" + fieldInt("id", m.id) + field("nom", m.name) + field("categorie", m.category)
               + field("description", m.description) + field("cree", m.created) + field("fichier", file) + "\n";
    }
    for (const auto& f : p.assets.files)
        index += "externe" + fieldInt("id", f.id) + field("nom", f.name) + field("type", externalKindKey(f.kind))
               + field("chemin", f.path) + field("partie", f.part) + fieldInt("octets", static_cast<long long>(f.bytes))
               + field("modifie", f.modified) + field("lie", f.linked) + field("description", f.description) + "\n";
    // La programmation generale : les variables IHM dans l'index, chaque script
    // dans son fichier (ihm/scripts/), comme une vue.
    // Lot 16 (format 15) : les types IHM (chacun suivi de ses membres) et les dossiers.
    for (const auto& ty : p.programs.types) {
        index += "type_ihm" + fieldInt("id", ty.id) + field("nom", ty.name) + field("description", ty.description)
               + (ty.folder.empty() ? std::string{} : field("dossier", ty.folder)) + "\n";   // lot 21
        // 1.10 (decision 15, chantier E) : une enumeration le dit (type_genre), puis ses valeurs.
        if (ty.kind != HmiTypeKind::Structure) index += "type_genre" + fieldBare("genre", typeKindKey(ty.kind)) + "\n";
        for (const auto& m : ty.members)
            index += "membre" + field("nom", m.name) + field("type", m.type) + field("initiale", m.initial)
                   + field("description", m.description) + "\n";
        for (const auto& v : ty.values)
            index += "valeur_enum" + field("nom", v.name) + fieldInt("valeur", v.value) + field("texte", v.text)
                   + field("description", v.description) + "\n";
        for (const auto& o : ty.operators) index += serializeOperator("operateur_type", o);   // 1.10
    }
    for (const auto& f : p.programs.folders) index += "dossier_variables" + field("nom", f) + "\n";
    // Lot 21 (format 19) : les dossiers vides des autres listes (vues, scripts, styles...).
    for (const auto& [list, folders] : p.listFolders)
        for (const auto& f : folders) index += "dossier_liste" + field("liste", list) + field("nom", f) + "\n";
    for (const auto& var : p.programs.variables) {
        index += "variable" + fieldInt("id", var.id) + field("nom", var.name) + field("type", var.type)
               + field("initiale", var.initial) + field("description", var.description);
        if (!var.folder.empty()) index += field("dossier", var.folder);   // lot 16
        if (!var.packBools) index += fieldBool("bool_par_mot", false);
        if (var.retain) index += fieldBool("remanente", true);              // 1.11.16 : en exploitation
        if (!var.places.empty()) {
            std::string places;
            for (const auto& pl : var.places) places += (places.empty() ? "" : ";") + pl.path + "=" + pl.address;
            index += field("places", places);
        }
        // 1.11.8 : les membres internes d'une variable liee ; la place recalculee.
        if (!var.internal.empty()) {
            std::string internal;
            for (const auto& m : var.internal) internal += (internal.empty() ? "" : ";") + m;
            index += field("internes", internal);
        }
        if (var.compact) index += fieldBool("place_recalculee", true);
        // Lot 15 : liee a un equipement - seulement si elle l'est.
        if (var.bound()) {
            index += field("equipement", var.equipment) + field("adresse", var.address)
                   + (var.readOnly ? fieldBool("lecture_seule", true) : std::string{});
            if (var.scaled())
                index += fieldBare("brut_min", formatNumber(var.rawMin)) + fieldBare("brut_max", formatNumber(var.rawMax))
                       + fieldBare("echelle_min", formatNumber(var.engMin)) + fieldBare("echelle_max", formatNumber(var.engMax))
                       + (var.rawType.empty() ? std::string{} : fieldBare("type_brut", var.rawType));
        }
        index += "\n";
    }
    for (const auto& sc : p.programs.scripts) {
        const std::string file = scriptFileName(sc);
        files.push_back({"scripts/" + file, textBytes(sc.body)});
        index += "script" + fieldInt("id", sc.id) + field("nom", sc.name) + fieldBare("langage", scriptLangKey(sc.lang))
               + fieldBare("evenement", sc.event.empty() ? "Appel" : sc.event) + fieldInt("periode", sc.periodMs)
               + field("surveille", sc.watch) + field("fichier", "scripts/" + file) + field("description", sc.description)
               + (sc.folder.empty() ? std::string{} : field("dossier", sc.folder))           // lot 21
               + "\n" + serializeDeclarations(sc.decls);                                    // 1.11.18 (lot 3)
    }
    // Lot 7 : les fonctions IHM, chacune dans son fichier (ihm/fonctions/).
    for (const auto& f : p.programs.functions) {
        Script named;
        named.id = f.id;
        named.name = f.name;
        const std::string file = scriptFileName(named);
        files.push_back({"fonctions/" + file, textBytes(f.body)});
        index += "fonction" + fieldInt("id", f.id) + field("nom", f.name) + field("retour", f.returnType)
               + field("fichier", "fonctions/" + file) + field("description", f.description) + "\n"
               + serializeDeclarations(f.decls);                                            // 1.11.18 (lot 3)
    }
    // Lot 4 : alarmes, recettes, securite, historiques.
    for (const auto& a : p.alarms)
        index += "alarme" + fieldInt("id", a.id) + field("nom", a.name) + field("condition", a.condition)
               + field("message", a.message) + fieldInt("priorite", a.priority) + field("categorie", a.category)
               + field("groupe", a.group) + fieldBool("acquittement", a.ackRequired) + fieldInt("delai", a.delayMs)
               + field("description", a.description) + field("consigne", a.instruction) + "\n";
    // Lot 11 : les sons des alarmes (un par priorite), leur repetition, la mise de cote.
    {
        const auto& as = p.alarmSettings;
        index += "alarmes_reglages";
        for (std::size_t k = 0; k < as.sounds.size(); ++k) index += field("son" + std::to_string(k + 1), as.sounds[k]);
        index += fieldInt("repetition", as.repeatS) + fieldInt("mise_de_cote_max", as.maxShelveMin) + "\n";
    }
    // 1.10.2 : les groupes d'alarmes (IHM > Alarmes > Groupes) et leurs reglages ; les
    // liens des groupes d'alarmes des objets vers eux.
    for (const auto& g : p.alarmGroups) {
        const char* ack = g.ack == AlarmAckMode::Group ? "groupe" : g.ack == AlarmAckMode::Auto ? "auto" : "un";
        index += "groupe_alarmes" + fieldInt("id", g.id) + field("nom", g.name) + field("description", g.description)
               + fieldInt("priorite", g.priority) + field("couleur_active", g.colorActive)
               + field("couleur_acquittee", g.colorAcked) + field("couleur_disparue", g.colorCleared)
               + field("acquittement", ack) + field("son", g.sound) + field("zone", g.zone)
               + fieldInt("niveau", g.level) + fieldBool("archive", g.archive) + "\n";
    }
    for (const auto& l : p.alarmGroupLinks)
        index += "lien_groupe_alarmes" + field("objets", l.objectGroup) + field("groupe", l.group) + "\n";
    for (const auto& r : p.recipes) {
        index += "recette" + fieldInt("id", r.id) + field("nom", r.name) + field("description", r.description) + "\n";
        for (const auto& f : r.fields)
            index += "element" + fieldInt("recette", r.id) + field("nom", f.name) + field("variable", f.variable)
                   + field("unite", f.unit) + field("min", f.min) + field("max", f.max) + "\n";
        for (const auto& rec : r.records) {
            index += "jeu" + fieldInt("recette", r.id) + fieldInt("id", rec.id) + field("nom", rec.name)
                   + field("modifie", rec.modified) + field("description", rec.description);
            for (std::size_t i = 0; i < rec.values.size(); ++i) index += field("v" + std::to_string(i + 1), rec.values[i]);
            index += "\n";
        }
    }
    const auto& sec = p.security;
    // Lot 13 (format 12) : la politique des mots de passe, le verrouillage,
    // l'avertissement de deconnexion, le badge - seulement ce qui differe du defaut.
    std::string policy;
    {
        const Security d{};
        if (sec.pwMinLength != d.pwMinLength) policy += fieldInt("mdp_longueur", sec.pwMinLength);
        if (sec.pwDigit) policy += fieldBool("mdp_chiffre", true);
        if (sec.pwLetter) policy += fieldBool("mdp_lettre", true);
        if (sec.pwMixedCase) policy += fieldBool("mdp_casse", true);
        if (sec.pwSpecial) policy += fieldBool("mdp_special", true);
        if (sec.pwMaxAgeDays != d.pwMaxAgeDays) policy += fieldInt("mdp_duree", sec.pwMaxAgeDays);
        if (sec.pwHistory != d.pwHistory) policy += fieldInt("mdp_historique", sec.pwHistory);
        if (sec.pwChangeFirst) policy += fieldBool("mdp_premiere", true);
        if (sec.lockAttempts != d.lockAttempts) policy += fieldInt("verrou_essais", sec.lockAttempts);
        if (sec.lockMinutes != d.lockMinutes) policy += fieldInt("verrou_minutes", sec.lockMinutes);
        if (sec.logoutWarnS != d.logoutWarnS) policy += fieldInt("avertir", sec.logoutWarnS);
        if (sec.badgeLogin) policy += fieldBool("badge", true);
    }
    index += "securite" + fieldBool("active", sec.enabled) + field("depart", sec.startUser)
           + fieldInt("periode", sec.dynamicPeriodS) + fieldInt("chiffres", sec.dynamicDigits)
           + fieldInt("deconnexion", sec.autoLogoutMin) + fieldInt("menu_comptes", sec.menuLevelAccounts)
           + fieldInt("menu_acces", sec.menuLevelAccess) + fieldInt("menu_journal", sec.menuLevelJournal) + policy + "\n";
    for (const auto& r : sec.roles)
        index += "role" + field("nom", r.name) + field("permissions", joinList(r.permissions)) + field("description", r.description) + "\n";
    for (const auto& g : sec.groups)
        index += "groupe" + fieldInt("id", g.id) + field("nom", g.name) + fieldInt("niveau", g.level)
               + field("roles", joinList(g.roles)) + field("description", g.description)
               + (g.startView != kNoId ? fieldInt("vue_demarrage", g.startView) : std::string{}) + "\n";
    for (const auto& u : sec.users)
        index += "utilisateur" + fieldInt("id", u.id) + field("login", u.login) + field("nom", u.fullName)
               + fieldInt("groupe", u.group) + fieldBare("protection", u.protection) + field("empreinte", u.passwordHash)
               + field("sel", u.salt) + field("secret", u.secret) + field("expression", u.expression)
               + fieldBool("actif", u.enabled) + field("description", u.description)
               // Lot 13 : la date du mot de passe, les precedents, le changement exige, le badge.
               + (u.passwordSet.empty() ? std::string{} : field("mdp_date", u.passwordSet))
               + (u.previous.empty() ? std::string{} : field("precedents", joinList(u.previous)))
               + (u.mustChange ? fieldBool("mdp_changer", true) : std::string{})
               + (u.badge.empty() ? std::string{} : field("badge", u.badge)) + "\n";
    const auto& hs = p.history;
    index += "historique" + fieldBool("alarmes", hs.alarms) + fieldBool("evenements", hs.events) + fieldBool("systeme", hs.system)
           + fieldInt("max", hs.maxEntries) + fieldInt("conservation", hs.retentionDays)
           + fieldInt("echantillonnage", hs.samplePeriodMs) + field("archivees", joinList(hs.archived))
           + (hs.audit ? fieldBool("audit", true) : std::string{})                 // lot 13
           + (hs.auditScripts ? fieldBool("audit_scripts", true) : std::string{}) + "\n";   // 1.12.3
    // Lot 12 (format 11) : les styles nommes, et leurs proprietes.
    for (const auto& st : p.styles) {
        index += "style" + fieldInt("id", st.id) + field("nom", st.name) + field("description", st.description)
               + (st.folder.empty() ? std::string{} : field("dossier", st.folder)) + "\n";   // lot 21
        for (const auto& pr : st.props)
            index += "style_prop" + fieldInt("style", st.id) + fieldBare("cle", pr.key) + field("valeur", pr.value) + "\n";
    }
    // Lot 13 : les langues (la premiere : celle du projet) et les traductions -
    // seulement si le projet en a (un projet en une langue n'ecrit rien).
    {
        const Languages def{};
        const auto& lg = p.languages;
        if (!(lg == def)) {
            index += "langues" + field("depart", lg.startLanguage) + "\n";
            for (const auto& l : lg.list) index += "langue" + field("code", l.code) + field("nom", l.name) + "\n";
            for (const auto& [src, byCode] : lg.texts)
                for (const auto& [code, text] : byCode)
                    index += "traduction" + field("langue", code) + field("source", src) + field("texte", text) + "\n";
        }
    }
    // Lot 13 : les unites et les formats des variables.
    for (const auto& d : p.displays)
        index += "affichage" + field("variable", d.path) + field("unite", d.unit) + field("format", d.format) + "\n";
    // Lot 14 : la communication avec l'automate reel - seulement si elle change
    // (un projet qui reste sur le simulateur n'ecrit rien).
    {
        const Communication def{};
        const auto& c = p.comm;
        if (!(c == def)) {
            index += "communication" + field("mode", c.mode) + field("hote", c.host) + fieldInt("port", c.port)
                   + fieldInt("esclave", c.unit) + fieldInt("delai", c.timeoutMs) + fieldInt("periode", c.periodMs)
                   + fieldInt("reessai", c.retryS) + field("ordre_mots", c.wordOrder) + fieldInt("mots_max", c.maxWords)
                   + fieldInt("bits_max", c.maxBits) + fieldInt("ecart", c.gap) + fieldBool("ecritures", c.writes)
                   + fieldInt("mauvaise_apres", c.badAfterS) + fieldBool("demo", c.demoServer) + fieldInt("demo_port", c.demoPort)
                   + fieldBool("demo_reseau", c.demoAllInterfaces) + "\n";
            for (const auto& a : c.addresses)
                index += "adresse_modbus" + field("variable", a.variable) + field("adresse", a.address) + field("type", a.type)
                       + (a.readOnly ? fieldBool("lecture_seule", true) : std::string{}) + field("description", a.description) + "\n";
        }
    }
    // Lot 14 : le poste d'exploitation - seulement s'il change.
    {
        const Station def{};
        const auto& st = p.station;
        if (!(st == def)) {
            index += "poste" + fieldBool("plein_ecran", st.fullScreen) + fieldBool("kiosque", st.kiosk) + field("sel_sortie", st.exitSalt)
                   + field("sortie", st.exitHash) + fieldBool("coin", st.cornerExit) + fieldBool("sans_curseur", st.hideCursor)
                   + fieldBool("simulateur", st.runSimulator) + (st.simPage ? std::string{} : fieldBool("page_simulation", false))
                   + (st.simMarks ? std::string{} : fieldBool("reperes_simules", false)) + "\n";
            for (const auto& e : st.screens) index += "poste_ecran" + fieldInt("ecran", e.display) + field("vue", e.view) + "\n";
        }
    }
    // Lot 14 : les notifications des alarmes - seulement si elles changent. Le
    // mot de passe SMTP est deja masque (jamais en clair).
    {
        const Notifications def{};
        const auto& n = p.notify;
        if (!(n == def)) {
            index += "notifications" + fieldBool("actives", n.enabled) + field("smtp", n.smtpHost) + fieldInt("smtp_port", n.smtpPort)
                   + field("expediteur", n.smtpFrom) + field("utilisateur", n.smtpUser) + field("mot_de_passe", n.smtpPassword)
                   + fieldInt("delai", n.timeoutS) + field("sms_url", n.smsUrl) + field("sms_corps", n.smsBody) + field("sujet", n.subject)
                   + field("corps", n.body) + field("sms", n.sms) + fieldInt("attente", n.delayS) + fieldInt("repetition", n.repeatS)
                   + fieldInt("max_heure", n.maxPerHour) + fieldBool("boite", n.testBox) + fieldInt("boite_port", n.testPort) + "\n";
            for (const auto& r : n.recipients)
                index += "destinataire" + field("nom", r.name) + field("courriel", r.email) + field("telephone", r.phone)
                       + fieldInt("priorite", r.maxPriority) + field("groupes", r.groups) + fieldBool("astreinte", r.duty)
                       + field("jours", r.dutyDays) + field("de", r.dutyFrom) + field("a", r.dutyTo) + fieldBool("disparition", r.onClear)
                       + fieldBool("actif", r.enabled) + "\n";
        }
    }
    // Lot 14 : les rapports periodiques.
    for (const auto& r : p.reports)
        index += "rapport" + fieldInt("id", r.id) + field("nom", r.name) + field("titre", r.title) + field("periode", r.period)
               + field("heure", r.time) + fieldInt("jour_semaine", r.weekday) + fieldInt("jour_mois", r.monthDay) + field("format", r.format)
               + fieldBool("alarmes", r.alarms) + field("mesures", r.measures) + fieldBool("production", r.production)
               + fieldBool("evenements", r.events) + field("destinataires", r.recipients) + fieldBool("actif", r.enabled) + "\n";
    // Lot 15 : les equipements du reseau.
    for (const auto& e : p.equipments) {
        index += "equipement" + fieldInt("id", e.id) + field("nom", e.name) + fieldBare("type", equipmentTypeKey(e.type))
               + field("hote", e.host) + fieldInt("port", e.port) + fieldInt("esclave", e.unit) + fieldInt("delai", e.timeoutMs)
               + fieldInt("periode", e.periodMs) + fieldInt("reessai", e.retryS) + field("ordre_mots", e.wordOrder)
               + fieldInt("mots_max", e.maxWords) + fieldInt("bits_max", e.maxBits) + fieldInt("ecart", e.gap)
               + fieldBool("ecritures", e.writes) + fieldInt("mauvaise_apres", e.badAfterS) + fieldInt("ping", e.pingS)
               + fieldBool("simule", e.simulated) + fieldBool("actif", e.enabled) + field("description", e.description);
        // Lot 17 : ses zones memoire et son jumeau - seulement ce qui n'est pas la valeur par defaut.
        const Equipment def{};
        if (e.zones.declared) {
            index += fieldBool("zones", true);
            for (const auto t : kMemTables)
                index += field(std::string("zone_") + std::string(zones::tableKey(t)), zones::rangesText(e.zones.of(t)));
            if (!e.zones.origin.empty()) index += field("zones_origine", e.zones.origin);
        }
        if (e.twin) index += fieldBool("jumeau", true);
        if (!e.twinName.empty()) index += field("jumeau_nom", e.twinName);
        if (!e.twinHost.empty()) index += field("jumeau_hote", e.twinHost);
        if (e.twinPort != def.twinPort) index += fieldInt("jumeau_port", e.twinPort);
        if (e.twinRunning != def.twinRunning) index += fieldBool("jumeau_marche", e.twinRunning);
        if (e.useTwin != def.useTwin) index += fieldBool("jumeau_ihm", e.useTwin);
        if (e.twinDelayMs != def.twinDelayMs) index += fieldInt("jumeau_delai", e.twinDelayMs);
        if (e.twinJitterMs != def.twinJitterMs) index += fieldInt("jumeau_gigue", e.twinJitterMs);
        if (e.twinResponds != def.twinResponds) index += fieldBool("jumeau_repond", e.twinResponds);
        if (e.twinException != def.twinException) index += fieldInt("jumeau_exception", e.twinException);
        if (e.twinPing != def.twinPing) index += fieldBool("jumeau_ping", e.twinPing);
        if (e.twinExpose != def.twinExpose) index += fieldBool("jumeau_visible", e.twinExpose);
        if (e.twinExposePort != def.twinExposePort) index += fieldInt("jumeau_port_visible", e.twinExposePort);
        if (e.twinStart != def.twinStart) index += fieldBare("jumeau_depart", twinStartKey(e.twinStart));
        // 1.9 : ce que lit l'IHM (l'esclave simule lie) - la bascule automatique, le poste.
        if (e.twinAuto != def.twinAuto) index += fieldBool("jumeau_auto", e.twinAuto);
        if (e.fallbackAfterS != def.fallbackAfterS) index += fieldInt("bascule_apres", e.fallbackAfterS);
        if (e.fallbackReturn != def.fallbackReturn) index += fieldBool("bascule_retour", e.fallbackReturn);
        if (e.stationRead != def.stationRead) index += fieldBare("jumeau_poste", stationReadKey(e.stationRead));
        index += "\n";
        for (const auto& b : e.behaviors)
            index += "comportement" + field("equipement", e.name) + field("adresse", b.address) + fieldBare("type", b.type)
                   + fieldBare("genre", behaviorKindKey(b.kind)) + fieldBare("a", formatNumber(b.a)) + fieldBare("b", formatNumber(b.b))
                   + fieldBare("periode", formatNumber(b.period)) + fieldBare("retard", formatNumber(b.delay)) + field("source", b.source)
                   + (b.enabled ? std::string{} : fieldBool("actif", false))              // lot 18 : decoche
                   + (b.noise > 0 ? fieldBare("bruit", formatNumber(b.noise)) : std::string{}) + "\n";
        // Lot 18 : ses cases forcees.
        for (const auto& f : e.forcings)
            index += "forcage" + field("equipement", e.name) + field("adresse", f.address) + fieldBare("type", f.type)
                   + fieldBare("valeur", formatNumber(f.value)) + field("depuis", f.since) + "\n";
        for (const auto& m : e.twinMemory) {
            std::string values;
            for (std::size_t i = 0; i < m.values.size(); ++i) values += (i ? "," : "") + std::to_string(m.values[i]);
            index += "memoire_jumeau" + field("equipement", e.name) + fieldBare("table", zones::tableKey(m.table)) + fieldInt("debut", m.first)
                   + field("valeurs", values) + "\n";
        }
    }
    // Lot 17 : les ports du PC simule.
    for (const auto& sp : p.simPorts)
        index += "port_simule" + field("nom", sp.name) + field("ip", sp.ip) + fieldInt("masque", sp.prefix) + field("copie", sp.copyOf) + "\n";
    // 1.9 : les jeux de lecture de l'outil Modbus (Lecture cyclique), chacun suivi
    // de ses requetes ; "dernier" : celui qui revient a l'ouverture de l'outil.
    for (const auto& set : p.modbusSets) {
        index += "jeu_modbus" + field("nom", set.name) + fieldInt("periode", set.periodMs) + fieldInt("echecs", set.maxFailures)
               + fieldInt("pause", set.pauseMs) + fieldBare("enchainement", set.perTarget ? "equipement" : "serie")
               + fieldInt("fenetre", set.windowS) + fieldBool("pistes", set.lanes)
               + (!set.name.empty() && set.name == p.modbusSetLast ? fieldBool("dernier", true) : std::string{}) + "\n";
        for (const auto& r : set.reads) {
            index += "requete_modbus" + field("jeu", set.name) + field("nom", r.name) + (r.active ? std::string{} : fieldBool("active", false))
                   + fieldBare("cible", r.target);
            if (r.target == "equipement" || r.target == "esclave") index += field("equipement", r.equipment);
            if (r.target == "adresse")
                index += field("hote", r.host) + fieldInt("port", r.port) + fieldInt("esclave", r.unit) + fieldInt("delai", r.timeoutMs);
            index += fieldInt("fonction", r.function) + fieldInt("adresse", r.address) + fieldInt("nombre", r.count) + fieldBare("format", r.format)
                   + fieldBare("ordre", r.lowFirst ? "faible" : "fort") + fieldInt("periode", r.periodMs);
            // Les variables : "nom|type|decalage|mots|bit;..." (un nom de variable n'a ni | ni ;).
            if (!r.values.empty()) {
                std::string values;
                for (const auto& v : r.values)
                    values += (values.empty() ? "" : ";") + v.name + "|" + v.type + "|" + std::to_string(v.offset) + "|" + std::to_string(v.words) + "|"
                            + std::to_string(v.bit);
                index += field("valeurs", values);
            }
            if (!r.traced.empty()) {
                std::string traced;
                for (const int k : r.traced) traced += (traced.empty() ? "" : ";") + std::to_string(k);
                index += field("tracees", traced);
            }
            index += "\n";
        }
    }
    // Lot 14 : l'acces par navigateur - seulement s'il change.
    {
        const WebAccess def{};
        const auto& w = p.web;
        if (!(w == def))
            index += "web" + fieldBool("actif", w.enabled) + fieldInt("port", w.port) + fieldBool("reseau", w.allInterfaces)
                   + fieldBool("connexion", w.login) + fieldBool("commande", w.control) + fieldInt("rafraichir", w.refreshMs)
                   + fieldInt("qualite", w.quality) + fieldInt("clients", w.maxClients) + fieldInt("session", w.sessionMin) + "\n";
    }
    // Lot 13 : les essais de reception, et leurs pas dans l'ordre.
    for (const auto& sc : p.scenarios) {
        index += "essai" + fieldInt("id", sc.id) + field("nom", sc.name) + field("description", sc.description) + "\n";
        for (const auto& st : sc.steps)
            index += "pas" + fieldInt("essai", sc.id) + field("action", st.action) + field("cible", st.target) + field("valeur", st.value)
                   + field("attendu", st.expected) + field("note", st.note) + "\n";
    }
    index += "fin\n";
    files.insert(files.begin(), ProjectFile{kIndexFile, textBytes(index)});
    return files;
}

core::Status save(const Project& project, const std::string& projectFolder) {
    const fs::path root = fs::path(projectFolder) / "ihm";
    const fs::path views = root / "vues";
    std::error_code ec;
    fs::create_directories(views, ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, "dossier " + views.string() + " : " + ec.message());

    Project p = project;
    if (p.config.created.empty()) p.config.created = todayStamp();
    p.config.modified = todayStamp();
    const auto files = serializeProject(p);

    // L'index en dernier : il ne designe jamais un fichier pas encore ecrit.
    std::set<std::string> written;
    const ProjectFile* index = nullptr;
    for (const auto& f : files) {
        if (f.path == kIndexFile) { index = &f; continue; }
        written.insert(f.path);
        const fs::path target = root / f.path;
        if (f.path.rfind("ressources/", 0) == 0 || f.path.rfind("modeles/", 0) == 0) {
            // Une ressource ne se reecrit pas a chaque enregistrement.
            if (!sameContent(target, *f.data))
                if (auto st = writeBytes(target, *f.data); !st) return st;
        } else if (auto st = writeAtomically(target, std::string(f.data->begin(), f.data->end())); !st) {
            return st;
        }
    }
    if (!index) return core::fail(core::ErrorCode::InvalidArgument, "index IHM absent");
    if (auto st = writeAtomically(root / kIndexFile, std::string(index->data->begin(), index->data->end())); !st) return st;

    // Ce que l'index ne designe plus part a la corbeille, jamais a la poubelle.
    for (const char* sub : {"vues", "scripts", "ressources", "fonctions", "modeles"}) {
        const fs::path dir = root / sub;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file() || e.path().extension() == ".tmp") continue;
            if (written.count(std::string(sub) + "/" + e.path().filename().string())) continue;
            const fs::path bin = root / "corbeille";
            fs::create_directories(bin, ec);
            fs::rename(e.path(), bin / e.path().filename(), ec);
        }
    }
    return core::ok();
}

core::Result<Project> parseProject(const FileReader& read, LoadReport* report) {
    std::string index;
    if (!read(kIndexFile, index)) return core::fail(core::ErrorCode::FileNotFound, kIndexFile);

    Project p;
    bool sawEnd = false, sawSecurity = false;
    bool languagesSeen = false;                  // lot 13 : la premiere "langue" remplace la liste par defaut
    std::size_t lineNo = 0, pos = 0;
    std::vector<std::pair<Id, std::string>> files;
    std::map<Id, std::string> viewFolders;                          // lot 21 : le dossier de chaque vue (l'index le dit)
    std::vector<Declaration>* holder = nullptr;                     // 1.11.18 (lot 3) : le porteur des lignes "declaration"
    while (pos <= index.size()) {
        const std::size_t eol = index.find('\n', pos);
        std::string_view line(index.data() + pos, (eol == std::string::npos ? index.size() : eol) - pos);
        pos = eol == std::string::npos ? index.size() + 1 : eol + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::size_t k = 0;
        while (k < line.size() && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
        if (k >= line.size() || line[k] == '#') continue;
        Record r;
        std::string error;
        if (!parseRecord(line, r, error))
            return core::fail(core::ErrorCode::XmlMalformed, "ihm.txt ligne " + std::to_string(lineNo) + " : " + error);
        if (r.word != "declaration") holder = nullptr;
        if (r.word == "declaration") {                                 // 1.11.18 (lot 3)
            Declaration d;
            if (!holder || !parseDeclaration(r, d)) {
                if (report) report->warnings.push_back("ihm.txt ligne " + std::to_string(lineNo) + " : " + declarationIgnored(holder));
                continue;
            }
            holder->push_back(std::move(d));
        } else if (r.word == "ihm") {
            const auto format = toInt(r.get("format"), kFormatVersion);
            if (format > kFormatVersion)
                return core::fail(core::ErrorCode::XmlUnsupportedDtd,
                                  "format IHM " + std::to_string(format) + " plus r\xC3\xA9" "cent que cet outil ("
                                  + std::to_string(kFormatVersion) + ")");
            p.nextId = static_cast<Id>(toInt(r.get("prochain_id"), 1));
        } else if (r.word == "config") {
            p.config.name = toStr(r.get("nom"));
            p.config.description = toStr(r.get("description"));
            p.config.version = toStr(r.get("version"));
            p.config.author = toStr(r.get("auteur"));
            p.config.created = toStr(r.get("cree"));
            p.config.modified = toStr(r.get("modifie"));
            p.config.width = static_cast<int>(toInt(r.get("largeur"), 1920));
            p.config.height = static_cast<int>(toInt(r.get("hauteur"), 1080));
            p.config.orientation = toStr(r.get("orientation"));
            p.config.startView = static_cast<Id>(toInt(r.get("demarrage"), 0));
            p.config.cycleMs = static_cast<int>(std::clamp<long long>(toInt(r.get("cycle"), 100), 10, 60000));
            p.config.swipeNavigation = toBool(r.get("glisser"), false);      // lot 12
            p.config.quality = toBool(r.get("qualite"), true);               // lot 13
            p.config.touchMin = static_cast<int>(std::clamp<long long>(toInt(r.get("cible"), 32), 0, 400));
            {
                double c = 3.0;
                if (const auto* t = r.get("contraste"); t && parseNumber(*t, c)) p.config.contrastMin = std::clamp(c, 0.0, 21.0);
            }
            p.config.heavyObjects = static_cast<int>(std::clamp<long long>(toInt(r.get("objets_max"), 400), 0, 100000));
            p.config.textScale = static_cast<int>(std::clamp<long long>(toInt(r.get("texte_pct"), 100), 50, 300));
            p.config.colorMode = r.get("couleurs") ? toStr(r.get("couleurs")) : std::string("normal");
            p.config.statusSymbols = toBool(r.get("symboles"), false);
            p.config.theme = r.get("theme") ? toStr(r.get("theme")) : std::string("nuit");
        } else if (r.word == "vue") {
            files.emplace_back(static_cast<Id>(toInt(r.get("id"), 0)), toStr(r.get("fichier")));
            if (const auto* d = r.get("dossier")) viewFolders[static_cast<Id>(toInt(r.get("id"), 0))] = *d;   // lot 21
        } else if (r.word == "ressource") {
            Resource res;
            res.id = static_cast<Id>(toInt(r.get("id"), 0));
            res.name = toStr(r.get("nom"));
            res.format = toStr(r.get("format"));
            if (res.format == "-") res.format.clear();
            res.bytes = static_cast<std::uint64_t>(toInt(r.get("octets"), 0));
            res.width = static_cast<int>(toInt(r.get("largeur"), 0));
            res.height = static_cast<int>(toInt(r.get("hauteur"), 0));
            double seconds = 0;
            if (const auto* d = r.get("duree"); d && parseNumber(*d, seconds)) res.seconds = seconds;
            res.detail = toStr(r.get("detail"));
            res.origin = toStr(r.get("origine"));
            res.added = toStr(r.get("ajoute"));
            res.folder = toStr(r.get("dossier"));                   // lot 21
            const std::string file = toStr(r.get("fichier"));
            std::string content;
            if (!file.empty() && read(file, content)) {
                res.data = std::make_shared<Bytes>(content.begin(), content.end());
            } else if (report) {
                report->warnings.push_back("ressource '" + res.name + "' : fichier " + file + " absent");
            }
            p.assets.resources.push_back(std::move(res));
        } else if (r.word == "modele") {
            // Lot 20 : un modele de vue garde dans le projet.
            ViewTemplateFile m;
            m.id = static_cast<Id>(toInt(r.get("id"), 0));
            m.name = toStr(r.get("nom"));
            m.category = toStr(r.get("categorie"));
            m.description = toStr(r.get("description"));
            m.created = toStr(r.get("cree"));
            const std::string file = toStr(r.get("fichier"));
            std::string content;
            if (!file.empty() && read(file, content)) m.data = std::make_shared<Bytes>(content.begin(), content.end());
            else if (report) report->warnings.push_back("mod\xC3\xA8le '" + m.name + "' : fichier " + file + " absent");
            p.viewTemplates.push_back(std::move(m));
        } else if (r.word == "externe") {
            ExternalFile f;
            f.id = static_cast<Id>(toInt(r.get("id"), 0));
            f.name = toStr(r.get("nom"));
            f.kind = externalKindFromKey(toStr(r.get("type"))).value_or(ExternalKind::Text);
            f.path = toStr(r.get("chemin"));
            f.part = toStr(r.get("partie"));
            f.bytes = static_cast<std::uint64_t>(toInt(r.get("octets"), 0));
            f.modified = toStr(r.get("modifie"));
            f.linked = toStr(r.get("lie"));
            f.description = toStr(r.get("description"));
            p.assets.files.push_back(std::move(f));
        } else if (r.word == "variable") {
            Variable var;
            var.id = static_cast<Id>(toInt(r.get("id"), 0));
            var.name = toStr(r.get("nom"));
            var.type = toStr(r.get("type"));
            if (var.type.empty()) var.type = "INT";
            var.initial = toStr(r.get("initiale"));
            var.description = toStr(r.get("description"));
            // Lot 15 : liee a un equipement.
            var.equipment = toStr(r.get("equipement"));
            var.address = toStr(r.get("adresse"));
            var.readOnly = toBool(r.get("lecture_seule"), false);
            const auto real = [&](const char* key) {
                double d = 0;
                return r.get(key) && parseNumber(toStr(r.get(key)), d) ? d : 0.0;
            };
            var.rawMin = real("brut_min");
            var.rawMax = real("brut_max");
            var.engMin = real("echelle_min");
            var.engMax = real("echelle_max");
            var.rawType = toStr(r.get("type_brut"));
            // Lot 16 : le dossier, les BOOL (16 par mot, ou un mot chacun), les adresses corrigees.
            var.folder = toStr(r.get("dossier"));
            var.packBools = toBool(r.get("bool_par_mot"), true);
            var.retain = toBool(r.get("remanente"), false);                   // 1.11.16
            {
                const std::string places = toStr(r.get("places"));
                std::size_t from = 0;
                while (from < places.size()) {
                    auto semi = places.find(';', from);
                    if (semi == std::string::npos) semi = places.size();
                    const std::string item = places.substr(from, semi - from);
                    if (const auto eq = item.find('='); eq != std::string::npos && eq > 0)
                        var.places.push_back({item.substr(0, eq), item.substr(eq + 1)});
                    from = semi + 1;
                }
            }
            {
                // 1.11.8 : les membres internes, la place recalculee.
                const std::string internal = toStr(r.get("internes"));
                std::size_t from = 0;
                while (from < internal.size()) {
                    auto semi = internal.find(';', from);
                    if (semi == std::string::npos) semi = internal.size();
                    if (semi > from) var.internal.push_back(internal.substr(from, semi - from));
                    from = semi + 1;
                }
                var.compact = toBool(r.get("place_recalculee"), false);
            }
            p.programs.variables.push_back(std::move(var));
        } else if (r.word == "type_ihm") {
            HmiType ty;
            ty.id = static_cast<Id>(toInt(r.get("id"), 0));
            ty.name = toStr(r.get("nom"));
            ty.description = toStr(r.get("description"));
            ty.folder = toStr(r.get("dossier"));                    // lot 21
            p.programs.types.push_back(std::move(ty));
        } else if (r.word == "membre") {
            if (p.programs.types.empty()) {
                if (report) report->warnings.push_back("membre sans type IHM : ignor\xC3\xA9");
                continue;
            }
            TypeMember m;
            m.name = toStr(r.get("nom"));
            m.type = toStr(r.get("type"));
            if (m.type.empty()) m.type = "INT";
            m.initial = toStr(r.get("initiale"));
            m.description = toStr(r.get("description"));
            p.programs.types.back().members.push_back(std::move(m));
        } else if (r.word == "type_genre") {                          // 1.10 (E) : une enumeration
            if (p.programs.types.empty()) {
                if (report) report->warnings.push_back("genre de type sans type IHM : ignor\xC3\xA9");
                continue;
            }
            p.programs.types.back().kind = typeKindFromKey(toStr(r.get("genre")));
        } else if (r.word == "valeur_enum") {                         // 1.10 (E) : une valeur d'enumeration
            if (p.programs.types.empty()) {
                if (report) report->warnings.push_back("valeur d'\xC3\xA9num\xC3\xA9ration sans type IHM : ignor\xC3\xA9" "e");
                continue;
            }
            HmiEnumValue v;
            v.name = toStr(r.get("nom"));
            v.value = static_cast<std::int64_t>(toInt(r.get("valeur"), 0));
            v.text = toStr(r.get("texte"));
            v.description = toStr(r.get("description"));
            p.programs.types.back().values.push_back(std::move(v));
        } else if (r.word == "operateur_type") {                      // 1.10
            HmiOperator o;
            if (p.programs.types.empty() || !parseOperator(r, o)) {
                if (report) report->warnings.push_back("op\xC3\xA9rateur de type sans type IHM, sans genre ou sans op\xC3\xA9rande : ignor\xC3\xA9");
                continue;
            }
            p.programs.types.back().operators.push_back(std::move(o));
            holder = &p.programs.types.back().operators.back().decls;
        } else if (r.word == "dossier_variables") {
            const std::string f = toStr(r.get("nom"));
            if (!f.empty()) p.programs.folders.push_back(f);
        } else if (r.word == "dossier_liste") {                     // lot 21
            const std::string list = toStr(r.get("liste")), f = toStr(r.get("nom"));
            if (!list.empty() && !f.empty()) p.listFolders[list].push_back(f);
        } else if (r.word == "script") {
            Script sc;
            sc.id = static_cast<Id>(toInt(r.get("id"), 0));
            sc.name = toStr(r.get("nom"));
            sc.lang = scriptLangFromKey(toStr(r.get("langage"))).value_or(ScriptLang::ST);
            sc.event = toStr(r.get("evenement"));
            sc.periodMs = static_cast<int>(toInt(r.get("periode"), 1000));
            sc.watch = toStr(r.get("surveille"));
            sc.description = toStr(r.get("description"));
            sc.folder = toStr(r.get("dossier"));                    // lot 21
            const std::string file = toStr(r.get("fichier"));
            if ((file.empty() || !read(file, sc.body)) && report)
                report->warnings.push_back("script '" + sc.name + "' : fichier " + file + " absent");
            p.programs.scripts.push_back(std::move(sc));
            holder = &p.programs.scripts.back().decls;
        } else if (r.word == "fonction") {
            HmiFunction f;
            f.id = static_cast<Id>(toInt(r.get("id"), 0));
            f.name = toStr(r.get("nom"));
            f.returnType = toStr(r.get("retour"));
            f.description = toStr(r.get("description"));
            const std::string file = toStr(r.get("fichier"));
            if ((file.empty() || !read(file, f.body)) && report)
                report->warnings.push_back("fonction '" + f.name + "' : fichier " + file + " absent");
            p.programs.functions.push_back(std::move(f));
            holder = &p.programs.functions.back().decls;
        } else if (r.word == "alarme") {
            AlarmDef a;
            a.id = static_cast<Id>(toInt(r.get("id"), 0));
            a.name = toStr(r.get("nom"));
            a.condition = toStr(r.get("condition"));
            a.message = toStr(r.get("message"));
            a.priority = static_cast<int>(std::clamp<long long>(toInt(r.get("priorite"), 3), 1, kAlarmPriorities));
            a.category = toStr(r.get("categorie"));
            if (a.category.empty()) a.category = "Alarme";
            a.group = toStr(r.get("groupe"));
            a.ackRequired = toBool(r.get("acquittement"), true);
            a.delayMs = static_cast<int>(std::max<long long>(0, toInt(r.get("delai"), 0)));
            a.description = toStr(r.get("description"));
            a.instruction = toStr(r.get("consigne"));
            p.alarms.push_back(std::move(a));
        } else if (r.word == "groupe_alarmes") {                         // 1.10.2
            AlarmGroupDef g;
            g.id = static_cast<Id>(toInt(r.get("id"), 0));
            g.name = toStr(r.get("nom"));
            g.description = toStr(r.get("description"));
            g.priority = static_cast<int>(std::clamp<long long>(toInt(r.get("priorite"), 0), 0, kAlarmPriorities));
            g.colorActive = toStr(r.get("couleur_active"));
            g.colorAcked = toStr(r.get("couleur_acquittee"));
            g.colorCleared = toStr(r.get("couleur_disparue"));
            const auto ack = toStr(r.get("acquittement"));
            g.ack = ack == "groupe" ? AlarmAckMode::Group : ack == "auto" ? AlarmAckMode::Auto : AlarmAckMode::Single;
            g.sound = toStr(r.get("son"));
            g.zone = toStr(r.get("zone"));
            g.level = static_cast<int>(std::clamp<long long>(toInt(r.get("niveau"), 0), 0, 4));
            g.archive = toBool(r.get("archive"), true);
            if (!g.name.empty()) p.alarmGroups.push_back(std::move(g));
        } else if (r.word == "lien_groupe_alarmes") {                    // 1.10.2
            AlarmGroupLink l{toStr(r.get("objets")), toStr(r.get("groupe"))};
            if (!l.objectGroup.empty()) p.alarmGroupLinks.push_back(std::move(l));
        } else if (r.word == "alarmes_reglages") {
            auto& as = p.alarmSettings;
            for (std::size_t s = 0; s < as.sounds.size(); ++s) as.sounds[s] = toStr(r.get("son" + std::to_string(s + 1)));
            as.repeatS = static_cast<int>(std::clamp<long long>(toInt(r.get("repetition"), 0), 0, 3600));
            as.maxShelveMin = static_cast<int>(std::clamp<long long>(toInt(r.get("mise_de_cote_max"), 480), 1, 100000));
        } else if (r.word == "langues") {                                 // lot 13
            p.languages.startLanguage = toStr(r.get("depart"));
        } else if (r.word == "langue") {
            if (!languagesSeen) {
                p.languages.list.clear();
                languagesSeen = true;
            }
            Language l;
            l.code = toStr(r.get("code"));
            l.name = toStr(r.get("nom"));
            if (!l.code.empty() && !p.languages.find(l.code)) p.languages.list.push_back(std::move(l));
        } else if (r.word == "traduction") {
            const std::string code = toStr(r.get("langue")), src = toStr(r.get("source"));
            if (!code.empty() && !src.empty()) p.languages.texts[src][code] = toStr(r.get("texte"));
        } else if (r.word == "affichage") {                               // lot 13
            VariableDisplay d;
            d.path = toStr(r.get("variable"));
            d.unit = toStr(r.get("unite"));
            d.format = toStr(r.get("format"));
            if (!d.path.empty()) p.displays.push_back(std::move(d));
        } else if (r.word == "communication") {                           // lot 14
            auto& c = p.comm;
            const Communication def{};
            c.mode = r.get("mode") ? toStr(r.get("mode")) : def.mode;
            if (c.mode != "modbus") c.mode = "simulateur";
            c.host = r.get("hote") ? toStr(r.get("hote")) : def.host;
            c.port = static_cast<int>(std::clamp<long long>(toInt(r.get("port"), def.port), 1, 65535));
            c.unit = static_cast<int>(std::clamp<long long>(toInt(r.get("esclave"), def.unit), 0, 255));
            c.timeoutMs = static_cast<int>(std::clamp<long long>(toInt(r.get("delai"), def.timeoutMs), 50, 60000));
            c.periodMs = static_cast<int>(std::clamp<long long>(toInt(r.get("periode"), def.periodMs), 50, 600000));
            c.retryS = static_cast<int>(std::clamp<long long>(toInt(r.get("reessai"), def.retryS), 1, 3600));
            c.wordOrder = r.get("ordre_mots") && toStr(r.get("ordre_mots")) == "fort" ? std::string("fort") : std::string("faible");
            c.maxWords = static_cast<int>(std::clamp<long long>(toInt(r.get("mots_max"), def.maxWords), 1, 125));
            c.maxBits = static_cast<int>(std::clamp<long long>(toInt(r.get("bits_max"), def.maxBits), 1, 2000));
            c.gap = static_cast<int>(std::clamp<long long>(toInt(r.get("ecart"), def.gap), 0, 100));
            c.writes = toBool(r.get("ecritures"), true);
            c.badAfterS = static_cast<int>(std::clamp<long long>(toInt(r.get("mauvaise_apres"), def.badAfterS), 1, 86400));
            c.demoServer = toBool(r.get("demo"), false);
            c.demoPort = static_cast<int>(std::clamp<long long>(toInt(r.get("demo_port"), def.demoPort), 1, 65535));
            c.demoAllInterfaces = toBool(r.get("demo_reseau"), false);
        } else if (r.word == "poste") {                                   // lot 14
            auto& st = p.station;
            st.fullScreen = toBool(r.get("plein_ecran"), true);
            st.kiosk = toBool(r.get("kiosque"), true);
            st.exitSalt = toStr(r.get("sel_sortie"));
            st.exitHash = toStr(r.get("sortie"));
            st.cornerExit = toBool(r.get("coin"), true);
            st.hideCursor = toBool(r.get("sans_curseur"), false);
            st.runSimulator = toBool(r.get("simulateur"), true);
            st.simPage = toBool(r.get("page_simulation"), true);          // 1.9
            st.simMarks = toBool(r.get("reperes_simules"), true);
        } else if (r.word == "poste_ecran") {
            StationScreen e;
            e.display = static_cast<int>(std::clamp<long long>(toInt(r.get("ecran"), 2), 1, 16));
            e.view = toStr(r.get("vue"));
            p.station.screens.push_back(std::move(e));
        } else if (r.word == "notifications") {                          // lot 14
            auto& n = p.notify;
            const Notifications def{};
            const auto num = [&](const char* key, int d, long long lo, long long hi) {
                return static_cast<int>(std::clamp<long long>(toInt(r.get(key), d), lo, hi));
            };
            n.enabled = toBool(r.get("actives"), false);
            n.smtpHost = toStr(r.get("smtp"));
            n.smtpPort = num("smtp_port", def.smtpPort, 1, 65535);
            n.smtpFrom = r.get("expediteur") ? toStr(r.get("expediteur")) : def.smtpFrom;
            n.smtpUser = toStr(r.get("utilisateur"));
            n.smtpPassword = toStr(r.get("mot_de_passe"));
            n.timeoutS = num("delai", def.timeoutS, 1, 120);
            n.smsUrl = toStr(r.get("sms_url"));
            n.smsBody = toStr(r.get("sms_corps"));
            n.subject = r.get("sujet") ? toStr(r.get("sujet")) : def.subject;
            n.body = toStr(r.get("corps"));
            n.sms = r.get("sms") ? toStr(r.get("sms")) : def.sms;
            n.delayS = num("attente", def.delayS, 0, 86400);
            n.repeatS = num("repetition", def.repeatS, 0, 86400);
            n.maxPerHour = num("max_heure", def.maxPerHour, 1, 10000);
            n.testBox = toBool(r.get("boite"), false);
            n.testPort = num("boite_port", def.testPort, 1, 65534);
        } else if (r.word == "destinataire") {
            NotifyRecipient d;
            const NotifyRecipient def{};
            d.name = toStr(r.get("nom"));
            d.email = toStr(r.get("courriel"));
            d.phone = toStr(r.get("telephone"));
            d.maxPriority = static_cast<int>(std::clamp<long long>(toInt(r.get("priorite"), def.maxPriority), 1, kAlarmPriorities));
            d.groups = toStr(r.get("groupes"));
            d.duty = toBool(r.get("astreinte"), false);
            d.dutyDays = r.get("jours") ? toStr(r.get("jours")) : def.dutyDays;
            d.dutyFrom = r.get("de") ? toStr(r.get("de")) : def.dutyFrom;
            d.dutyTo = r.get("a") ? toStr(r.get("a")) : def.dutyTo;
            d.onClear = toBool(r.get("disparition"), false);
            d.enabled = toBool(r.get("actif"), true);
            if (!d.name.empty()) p.notify.recipients.push_back(std::move(d));
        } else if (r.word == "rapport") {                                 // lot 14
            Report rp;
            const Report def{};
            rp.id = static_cast<Id>(toInt(r.get("id"), 0));
            rp.name = toStr(r.get("nom"));
            rp.title = toStr(r.get("titre"));
            rp.period = r.get("periode") ? toStr(r.get("periode")) : def.period;
            if (rp.period != "semaine" && rp.period != "mois") rp.period = "jour";
            rp.time = r.get("heure") ? toStr(r.get("heure")) : def.time;
            rp.weekday = static_cast<int>(std::clamp<long long>(toInt(r.get("jour_semaine"), def.weekday), 1, 7));
            rp.monthDay = static_cast<int>(std::clamp<long long>(toInt(r.get("jour_mois"), def.monthDay), 1, 28));
            rp.format = r.get("format") && toStr(r.get("format")) == "Excel" ? std::string("Excel") : std::string("PDF");
            rp.alarms = toBool(r.get("alarmes"), true);
            rp.measures = toStr(r.get("mesures"));
            rp.production = toBool(r.get("production"), true);
            rp.events = toBool(r.get("evenements"), false);
            rp.recipients = toStr(r.get("destinataires"));
            rp.enabled = toBool(r.get("actif"), true);
            if (rp.id != kNoId && !rp.name.empty()) p.reports.push_back(std::move(rp));
        } else if (r.word == "web") {                                     // lot 14
            auto& w = p.web;
            const WebAccess def{};
            const auto num = [&](const char* key, int d, long long lo, long long hi) {
                return static_cast<int>(std::clamp<long long>(toInt(r.get(key), d), lo, hi));
            };
            w.enabled = toBool(r.get("actif"), false);
            w.port = num("port", def.port, 1, 65535);
            w.allInterfaces = toBool(r.get("reseau"), false);
            w.login = toBool(r.get("connexion"), true);
            w.control = toBool(r.get("commande"), false);
            w.refreshMs = num("rafraichir", def.refreshMs, 100, 10000);
            w.quality = num("qualite", def.quality, 20, 100);
            w.maxClients = num("clients", def.maxClients, 1, 64);
            w.sessionMin = num("session", def.sessionMin, 1, 1440);
        } else if (r.word == "equipement") {                              // lot 15
            Equipment e;
            const Equipment def{};
            const auto num = [&](const char* key, int d, long long lo, long long hi) {
                return static_cast<int>(std::clamp<long long>(toInt(r.get(key), d), lo, hi));
            };
            e.id = static_cast<Id>(toInt(r.get("id"), 0));
            e.name = toStr(r.get("nom"));
            e.type = equipmentTypeFrom(toStr(r.get("type"))).value_or(EquipmentType::ModbusTcp);
            e.host = r.get("hote") ? toStr(r.get("hote")) : def.host;
            e.port = num("port", def.port, 0, 65535);
            e.unit = num("esclave", def.unit, 0, 255);
            e.timeoutMs = num("delai", def.timeoutMs, 50, 60000);
            e.periodMs = num("periode", def.periodMs, 50, 600000);
            e.retryS = num("reessai", def.retryS, 1, 3600);
            e.wordOrder = r.get("ordre_mots") && toStr(r.get("ordre_mots")) == "fort" ? std::string("fort") : std::string("faible");
            e.maxWords = num("mots_max", def.maxWords, 1, 125);
            e.maxBits = num("bits_max", def.maxBits, 1, 2000);
            e.gap = num("ecart", def.gap, 0, 100);
            e.writes = toBool(r.get("ecritures"), true);
            e.badAfterS = num("mauvaise_apres", def.badAfterS, 1, 86400);
            e.pingS = num("ping", def.pingS, 0, 86400);
            e.simulated = toBool(r.get("simule"), false);
            e.enabled = toBool(r.get("actif"), true);
            e.description = toStr(r.get("description"));
            // Lot 17 : les zones memoire, le jumeau.
            if (toBool(r.get("zones"), false)) {
                e.zones.declared = true;
                for (const auto t : kMemTables) {
                    std::vector<MemRange> ranges;
                    if (zones::parseRanges(t, toStr(r.get(std::string("zone_") + std::string(zones::tableKey(t)))), ranges)) e.zones.of(t) = std::move(ranges);
                }
                e.zones.origin = toStr(r.get("zones_origine"));
            }
            e.twin = toBool(r.get("jumeau"), false);
            e.twinName = toStr(r.get("jumeau_nom"));
            e.twinHost = toStr(r.get("jumeau_hote"));
            e.twinPort = num("jumeau_port", def.twinPort, 1, 65535);
            e.twinRunning = toBool(r.get("jumeau_marche"), def.twinRunning);
            e.useTwin = toBool(r.get("jumeau_ihm"), def.useTwin);
            e.twinDelayMs = num("jumeau_delai", def.twinDelayMs, 0, 60000);
            e.twinJitterMs = num("jumeau_gigue", def.twinJitterMs, 0, 60000);
            e.twinResponds = toBool(r.get("jumeau_repond"), def.twinResponds);
            e.twinException = num("jumeau_exception", def.twinException, 0, 255);
            e.twinPing = toBool(r.get("jumeau_ping"), def.twinPing);
            e.twinExpose = toBool(r.get("jumeau_visible"), def.twinExpose);
            e.twinExposePort = num("jumeau_port_visible", def.twinExposePort, 1, 65535);
            {
                const std::string s = toStr(r.get("jumeau_depart"));
                for (const auto start : {TwinStart::Zeros, TwinStart::Initial, TwinStart::Saved})
                    if (s == twinStartKey(start)) e.twinStart = start;
            }
            // 1.9 : la bascule automatique, le poste d'exploitation.
            e.twinAuto = toBool(r.get("jumeau_auto"), def.twinAuto);
            e.fallbackAfterS = num("bascule_apres", def.fallbackAfterS, 1, 3600);
            e.fallbackReturn = toBool(r.get("bascule_retour"), def.fallbackReturn);
            if (const auto sr = stationReadFrom(toStr(r.get("jumeau_poste")))) e.stationRead = *sr;
            if (!e.name.empty()) p.equipments.push_back(std::move(e));
        } else if (r.word == "comportement") {                            // lot 17
            Behavior b;
            b.address = toStr(r.get("adresse"));
            b.type = r.get("type") ? toStr(r.get("type")) : std::string("INT");
            b.kind = behaviorKindFrom(toStr(r.get("genre"))).value_or(BehaviorKind::Constant);
            const auto real = [&](const char* key, double d) {
                double x = d;
                return r.get(key) && parseNumber(toStr(r.get(key)), x) ? x : d;
            };
            b.a = real("a", 0);
            b.b = real("b", 100);
            b.period = real("periode", 10);
            b.delay = real("retard", 0);
            b.source = toStr(r.get("source"));
            b.enabled = toBool(r.get("actif"), true);                     // lot 18
            b.noise = std::max(0.0, real("bruit", 0));
            const std::string owner = toStr(r.get("equipement"));
            for (auto& e : p.equipments)
                if (e.name == owner) e.behaviors.push_back(b);
        } else if (r.word == "forcage") {                                 // lot 18
            Forcing f;
            f.address = toStr(r.get("adresse"));
            f.type = r.get("type") ? toStr(r.get("type")) : std::string("INT");
            if (r.get("valeur")) (void)parseNumber(toStr(r.get("valeur")), f.value);
            f.since = toStr(r.get("depuis"));
            const std::string owner = toStr(r.get("equipement"));
            for (auto& e : p.equipments)
                if (e.name == owner && !f.address.empty()) e.forcings.push_back(f);
        } else if (r.word == "memoire_jumeau") {                          // lot 17
            SavedMemory m;
            m.table = zones::tableFrom(toStr(r.get("table"))).value_or(MemTable::Holding);
            m.first = static_cast<std::uint32_t>(std::clamp<long long>(toInt(r.get("debut"), 0), 0, 65535));
            const std::string values = toStr(r.get("valeurs"));
            std::string cur;
            for (std::size_t i = 0; i <= values.size(); ++i) {
                if (i == values.size() || values[i] == ',') {
                    if (!cur.empty()) m.values.push_back(static_cast<std::uint16_t>(std::clamp<long long>(std::atoll(cur.c_str()), 0, 65535)));
                    cur.clear();
                } else {
                    cur.push_back(values[i]);
                }
            }
            const std::string owner = toStr(r.get("equipement"));
            for (auto& e : p.equipments)
                if (e.name == owner) e.twinMemory.push_back(m);
        } else if (r.word == "port_simule") {                             // lot 17
            SimPort sp;
            sp.name = toStr(r.get("nom"));
            sp.ip = toStr(r.get("ip"));
            sp.prefix = static_cast<int>(std::clamp<long long>(toInt(r.get("masque"), 24), 1, 32));
            sp.copyOf = toStr(r.get("copie"));
            if (!sp.name.empty()) p.simPorts.push_back(std::move(sp));
        } else if (r.word == "jeu_modbus") {                              // 1.9 : un jeu de lecture de l'outil Modbus
            ModbusReadSet set;
            set.name = toStr(r.get("nom"));
            set.periodMs = static_cast<int>(std::clamp<long long>(toInt(r.get("periode"), 500), 20, 3600000));
            set.maxFailures = static_cast<int>(std::clamp<long long>(toInt(r.get("echecs"), 10), 0, 100000));
            set.pauseMs = static_cast<int>(std::clamp<long long>(toInt(r.get("pause"), 0), 0, 60000));
            set.perTarget = toStr(r.get("enchainement")) != "serie";
            set.windowS = static_cast<int>(std::clamp<long long>(toInt(r.get("fenetre"), 60), 10, 900));
            set.lanes = toBool(r.get("pistes"), true);
            if (toBool(r.get("dernier"), false)) p.modbusSetLast = set.name;
            if (!set.name.empty()) p.modbusSets.push_back(std::move(set));
        } else if (r.word == "requete_modbus") {                          // 1.9 : une requete d'un jeu
            const std::string owner = toStr(r.get("jeu"));
            ModbusReadSet* set = nullptr;
            for (auto& s : p.modbusSets)
                if (s.name == owner) set = &s;
            if (!set) {
                if (report) report->warnings.push_back("ihm.txt ligne " + std::to_string(lineNo) + " : jeu de lecture introuvable");
                continue;
            }
            ModbusRead q;
            const auto num = [&](const char* key, long long def, long long lo, long long hi) {
                return static_cast<int>(std::clamp<long long>(toInt(r.get(key), def), lo, hi));
            };
            q.name = toStr(r.get("nom"));
            q.active = toBool(r.get("active"), true);
            q.target = r.get("cible") ? toStr(r.get("cible")) : std::string("automate");
            q.equipment = toStr(r.get("equipement"));
            if (r.get("hote")) q.host = toStr(r.get("hote"));
            q.port = num("port", 502, 1, 65535);
            q.unit = num("esclave", 1, 0, 255);
            q.timeoutMs = num("delai", 1000, 50, 60000);
            q.function = num("fonction", 3, 1, 4);
            q.address = num("adresse", 0, 0, 65535);
            q.count = num("nombre", 10, 1, 2000);
            if (r.get("format")) q.format = toStr(r.get("format"));
            q.lowFirst = toStr(r.get("ordre")) != "fort";
            q.periodMs = num("periode", 0, 0, 3600000);
            for (const auto& item : splitList(toStr(r.get("valeurs")))) {
                std::vector<std::string> part;
                std::string cur;
                for (const char c : item + "|") {
                    if (c == '|') {
                        part.push_back(cur);
                        cur.clear();
                    } else {
                        cur += c;
                    }
                }
                if (part.size() < 5 || part[0].empty()) continue;
                ModbusReadValue v;
                v.name = part[0];
                v.type = part[1];
                v.offset = static_cast<int>(std::clamp<long long>(std::atoll(part[2].c_str()), 0, 2000));
                v.words = static_cast<int>(std::clamp<long long>(std::atoll(part[3].c_str()), 1, 125));
                v.bit = static_cast<int>(std::clamp<long long>(std::atoll(part[4].c_str()), -1, 15));
                q.values.push_back(std::move(v));
            }
            for (const auto& rank : splitList(toStr(r.get("tracees"))))
                q.traced.push_back(static_cast<int>(std::clamp<long long>(std::atoll(rank.c_str()), 0, 100000)));
            set->reads.push_back(std::move(q));
        } else if (r.word == "adresse_modbus") {
            CommAddress a;
            a.variable = toStr(r.get("variable"));
            a.address = toStr(r.get("adresse"));
            a.type = toStr(r.get("type"));
            a.readOnly = toBool(r.get("lecture_seule"), false);
            a.description = toStr(r.get("description"));
            if (!a.variable.empty()) p.comm.addresses.push_back(std::move(a));
        } else if (r.word == "essai") {                                   // lot 13
            TestScenario sc;
            sc.id = static_cast<Id>(toInt(r.get("id"), 0));
            sc.name = toStr(r.get("nom"));
            sc.description = toStr(r.get("description"));
            p.scenarios.push_back(std::move(sc));
        } else if (r.word == "pas") {
            auto* sc = p.scenario(static_cast<Id>(toInt(r.get("essai"), 0)));
            if (!sc) {
                if (report) report->warnings.push_back("ihm.txt ligne " + std::to_string(lineNo) + " : essai introuvable");
                continue;
            }
            TestStep st;
            st.action = toStr(r.get("action"));
            st.target = toStr(r.get("cible"));
            st.value = toStr(r.get("valeur"));
            st.expected = toStr(r.get("attendu"));
            st.note = toStr(r.get("note"));
            sc->steps.push_back(std::move(st));
        } else if (r.word == "recette") {
            Recipe rec;
            rec.id = static_cast<Id>(toInt(r.get("id"), 0));
            rec.name = toStr(r.get("nom"));
            rec.description = toStr(r.get("description"));
            p.recipes.push_back(std::move(rec));
        } else if (r.word == "element" || r.word == "jeu") {
            auto* rec = p.recipe(static_cast<Id>(toInt(r.get("recette"), 0)));
            if (!rec) {
                if (report) report->warnings.push_back("ihm.txt ligne " + std::to_string(lineNo) + " : recette introuvable");
                continue;
            }
            if (r.word == "element") {
                RecipeField f;
                f.name = toStr(r.get("nom"));
                f.variable = toStr(r.get("variable"));
                f.unit = toStr(r.get("unite"));
                f.min = toStr(r.get("min"));
                f.max = toStr(r.get("max"));
                rec->fields.push_back(std::move(f));
            } else {
                RecipeRecord set;
                set.id = static_cast<Id>(toInt(r.get("id"), 0));
                set.name = toStr(r.get("nom"));
                set.modified = toStr(r.get("modifie"));
                set.description = toStr(r.get("description"));
                set.values.resize(rec->fields.size());
                for (std::size_t i = 0; i < set.values.size(); ++i) set.values[i] = toStr(r.get("v" + std::to_string(i + 1)));
                rec->records.push_back(std::move(set));
            }
        } else if (r.word == "securite") {
            // Les roles et groupes qui suivent remplacent ceux par defaut.
            sawSecurity = true;
            p.security.roles.clear();
            p.security.groups.clear();
            p.security.enabled = toBool(r.get("active"), false);
            p.security.startUser = toStr(r.get("depart"));
            p.security.dynamicPeriodS = static_cast<int>(std::clamp<long long>(toInt(r.get("periode"), 30), 10, 3600));
            p.security.dynamicDigits = static_cast<int>(std::clamp<long long>(toInt(r.get("chiffres"), 6), 4, 9));
            p.security.autoLogoutMin = static_cast<int>(std::max<long long>(0, toInt(r.get("deconnexion"), 0)));
            // Lot 12 : les onglets du menu de connexion (absents : 4, 4, 3).
            p.security.menuLevelAccounts = static_cast<int>(std::clamp<long long>(toInt(r.get("menu_comptes"), 4), 0, 99));
            p.security.menuLevelAccess = static_cast<int>(std::clamp<long long>(toInt(r.get("menu_acces"), 4), 0, 99));
            p.security.menuLevelJournal = static_cast<int>(std::clamp<long long>(toInt(r.get("menu_journal"), 3), 0, 99));
            // Lot 13 : la politique des mots de passe (absente : aucune regle).
            auto& s = p.security;
            s.pwMinLength = static_cast<int>(std::clamp<long long>(toInt(r.get("mdp_longueur"), 0), 0, 64));
            s.pwDigit = toBool(r.get("mdp_chiffre"), false);
            s.pwLetter = toBool(r.get("mdp_lettre"), false);
            s.pwMixedCase = toBool(r.get("mdp_casse"), false);
            s.pwSpecial = toBool(r.get("mdp_special"), false);
            s.pwMaxAgeDays = static_cast<int>(std::clamp<long long>(toInt(r.get("mdp_duree"), 0), 0, 3650));
            s.pwHistory = static_cast<int>(std::clamp<long long>(toInt(r.get("mdp_historique"), 0), 0, 24));
            s.pwChangeFirst = toBool(r.get("mdp_premiere"), false);
            s.lockAttempts = static_cast<int>(std::clamp<long long>(toInt(r.get("verrou_essais"), 0), 0, 99));
            s.lockMinutes = static_cast<int>(std::clamp<long long>(toInt(r.get("verrou_minutes"), 15), 0, 10080));
            s.logoutWarnS = static_cast<int>(std::clamp<long long>(toInt(r.get("avertir"), 30), 0, 600));
            s.badgeLogin = toBool(r.get("badge"), false);
        } else if (r.word == "role") {
            Role role;
            role.name = toStr(r.get("nom"));
            role.permissions = splitList(toStr(r.get("permissions")));
            role.description = toStr(r.get("description"));
            p.security.roles.push_back(std::move(role));
        } else if (r.word == "groupe") {
            UserGroup g;
            g.id = static_cast<Id>(toInt(r.get("id"), 0));
            g.name = toStr(r.get("nom"));
            g.level = static_cast<int>(std::clamp<long long>(toInt(r.get("niveau"), 1), 0, 9));
            g.roles = splitList(toStr(r.get("roles")));
            g.description = toStr(r.get("description"));
            g.startView = static_cast<Id>(toInt(r.get("vue_demarrage"), 0));   // lot 12
            p.security.groups.push_back(std::move(g));
        } else if (r.word == "utilisateur") {
            User u;
            u.id = static_cast<Id>(toInt(r.get("id"), 0));
            u.login = toStr(r.get("login"));
            u.fullName = toStr(r.get("nom"));
            u.group = static_cast<Id>(toInt(r.get("groupe"), 0));
            u.protection = toStr(r.get("protection"));
            if (u.protection.empty()) u.protection = "classique";
            u.passwordHash = toStr(r.get("empreinte"));
            u.salt = toStr(r.get("sel"));
            u.secret = toStr(r.get("secret"));
            u.expression = toStr(r.get("expression"));
            u.enabled = toBool(r.get("actif"), true);
            u.description = toStr(r.get("description"));
            u.passwordSet = toStr(r.get("mdp_date"));                   // lot 13
            u.previous = splitList(toStr(r.get("precedents")));
            u.mustChange = toBool(r.get("mdp_changer"), false);
            u.badge = toStr(r.get("badge"));
            p.security.users.push_back(std::move(u));
        } else if (r.word == "historique") {
            auto& hs = p.history;
            hs.alarms = toBool(r.get("alarmes"), true);
            hs.events = toBool(r.get("evenements"), true);
            hs.system = toBool(r.get("systeme"), true);
            hs.maxEntries = static_cast<int>(std::clamp<long long>(toInt(r.get("max"), 2000), 10, 1000000));
            hs.retentionDays = static_cast<int>(std::clamp<long long>(toInt(r.get("conservation"), 30), 0, 36500));
            hs.samplePeriodMs = static_cast<int>(std::clamp<long long>(toInt(r.get("echantillonnage"), 1000), 50, 3600000));
            hs.archived = splitList(toStr(r.get("archivees")));
            hs.audit = toBool(r.get("audit"), false);                   // lot 13
            hs.auditScripts = toBool(r.get("audit_scripts"), false);    // 1.12.3
        } else if (r.word == "style") {
            Style st;                                                   // lot 12
            st.id = static_cast<Id>(toInt(r.get("id"), 0));
            st.name = toStr(r.get("nom"));
            st.description = toStr(r.get("description"));
            st.folder = toStr(r.get("dossier"));                    // lot 21
            p.styles.push_back(std::move(st));
        } else if (r.word == "style_prop") {
            const auto sid = static_cast<Id>(toInt(r.get("style"), 0));
            Style* st = p.style(sid);
            const std::string key = toStr(r.get("cle"));
            if (!st || key.empty()) {
                if (report) report->warnings.push_back("ihm.txt ligne " + std::to_string(lineNo) + " : propri\xC3\xA9t\xC3\xA9 de style sans style");
                continue;
            }
            st->props.push_back({key, toStr(r.get("valeur")), {}});
        } else if (r.word == "fin") {
            sawEnd = true;
        } else if (report) {
            report->warnings.push_back("ihm.txt ligne " + std::to_string(lineNo) + " : '" + r.word + "' ignor\xC3\xA9");
        }
    }
    if (!sawEnd) return core::fail(core::ErrorCode::FileEmpty, "ihm.txt coup\xC3\xA9 : la ligne 'fin' manque");
    (void)sawSecurity;

    Id highest = 0;
    for (const auto& [id, file] : files) {
        std::string text;
        if (!read(file, text)) return core::fail(core::ErrorCode::FileNotFound, "vue " + std::to_string(id) + " : " + file);
        auto v = parseView(text, report);
        if (!v) return core::fail(v.error().code, file + " : " + v.error().context);
        if (v->id != id && report)
            report->warnings.push_back(file + " : identifiant " + std::to_string(v->id) + " au lieu de "
                                       + std::to_string(id));
        highest = std::max(highest, v->id);
        if (const auto it = viewFolders.find(id); it != viewFolders.end()) v->folder = it->second;   // lot 21
        for (const auto& l : v->layers) highest = std::max(highest, l.id);
        for (const auto& o : v->objects) highest = std::max(highest, o.id);
        for (const auto& s : v->scripts) highest = std::max(highest, s.id);
        for (const auto& a : v->alarms) highest = std::max(highest, a.id);     // 1.9 : les alarmes d'un symbole
        for (const auto& o : v->operators) highest = std::max(highest, o.id);  // 1.10 : ses operateurs
        for (const auto& fn : v->functions) highest = std::max(highest, fn.id);  // 1.11.10 : ses fonctions
        p.views.push_back(std::move(*v));
    }
    for (const auto& r : p.assets.resources) highest = std::max(highest, r.id);
    for (const auto& sc : p.programs.scripts) highest = std::max(highest, sc.id);
    for (const auto& var : p.programs.variables) highest = std::max(highest, var.id);
    for (const auto& ty : p.programs.types) {
        highest = std::max(highest, ty.id);                                     // lot 16
        for (const auto& o : ty.operators) highest = std::max(highest, o.id);  // 1.10
    }
    for (const auto& f : p.assets.files) highest = std::max(highest, f.id);
    for (const auto& a : p.alarms) highest = std::max(highest, a.id);
    for (const auto& g : p.alarmGroups) highest = std::max(highest, g.id);   // 1.10.2
    for (const auto& r : p.recipes) {
        highest = std::max(highest, r.id);
        for (const auto& rec : r.records) highest = std::max(highest, rec.id);
    }
    for (const auto& u : p.security.users) highest = std::max(highest, u.id);
    for (const auto& st : p.styles) highest = std::max(highest, st.id);      // lot 12
    for (const auto& sc : p.scenarios) highest = std::max(highest, sc.id);   // lot 13
    for (const auto& rp : p.reports) highest = std::max(highest, rp.id);     // lot 14
    for (const auto& e : p.equipments) highest = std::max(highest, e.id);    // lot 15
    for (const auto& g : p.security.groups)
        if (g.id < 0xFFFFFE00u) highest = std::max(highest, g.id);      // hors groupes par defaut
    // 1.11.18 (lot 3) : les declarations des scripts, fonctions, redefinitions et operateurs.
    forEachDeclarations(static_cast<const Project&>(p), [&](const std::vector<Declaration>& list) {
        for (const auto& d : list) highest = std::max(highest, d.id);
    });
    // Un index edite a la main ne doit pas faire reattribuer un identifiant
    // deja pris : le compteur repart au-dessus du plus grand vu.
    if (p.nextId <= highest) p.nextId = highest + 1;
    // 1.11.18 (lot 3) : une declaration sans identifiant, ou en double, en recoit un neuf.
    if (uniqueDeclarationIds(p) && report)
        report->warnings.push_back("d\xC3\xA9" "clarations : identifiants absents ou en double, renouvel\xC3\xA9s");
    // 1.11.19 (lot 6) : un type IHM renomme hors de l'application (le fichier edite a la main, un
    // morceau venu d'ailleurs) : la cle lue (type_cle) retrouve son nom d'aujourd'hui.
    for (const auto& said : typereg::followTypeKeys(p))
        if (report) report->warnings.push_back(said);
    // Lot 15 : un equipement ecrit a la main sans identifiant en recoit un.
    for (auto& e : p.equipments)
        if (e.id == kNoId) e.id = p.nextId++;
    if (p.config.startView != kNoId && !p.view(p.config.startView) && report)
        report->warnings.push_back("vue de d\xC3\xA9marrage " + std::to_string(p.config.startView) + " introuvable");
    for (const auto& u : p.security.users)
        if (!p.group(u.group) && report) report->warnings.push_back("utilisateur '" + u.login + "' : groupe introuvable");
    return p;
}

core::Result<Project> load(const std::string& projectFolder, LoadReport* report) {
    const fs::path root = fs::path(projectFolder) / "ihm";
    return parseProject([&root](const std::string& path, std::string& content) {
        bool ok = false;
        content = readAll(root / path, ok);
        return ok;
    }, report);
}

} // namespace hmi
