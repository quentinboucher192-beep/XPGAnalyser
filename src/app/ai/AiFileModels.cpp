// =============================================================================
//  app/ai/AiFileModels.cpp - voir AiFileModels.hpp
// -----------------------------------------------------------------------------
//  LE CHAMP "donnees" d'un modele de fichier (connaissances/<id>.json) :
//    { "format": 1, "genre": "es" | "reseau" | "reports" | "autre",
//      "mise": { "genre", "feuille", "ligneEntete" (1 : la premiere),
//                "colonnes": [ { "role", "colonne" ("A"), "intitule" } ],
//                "regles": { "sauterSousEntete", "valeursVides": [..], "recopierVersLeBas": [roles],
//                            "fusions": true, "cles": [roles] } },
//      "signature": [ intitules normalises, par colonne ], "utilisations": 3, "dernierUsage": "ISO" }
//  La piece jointe : son analyse d'extraction porte "classeur" (la reconnaissance) ; le choix de la fenetre
//  << Modele... >> est dans son extra "modeleFichier" ({ "modele", "nom", "confiance", "mise" }).
// =============================================================================
#include "AiFileModels.hpp"

#include "AiKnowledge.hpp"
#include "AiMemory.hpp"
#include "AiMemoryJson.hpp"
#include "AiPaths.hpp"
#include "../Dossiers.hpp"
#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../xls/Workbook.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

namespace app::ai::sheets {

namespace fs = std::filesystem;
using memory::Json;

namespace {

std::string guill(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) { return upperOf(a) == upperOf(b); }

bool allDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string percent(double v) { return std::to_string(static_cast<int>(v * 100.0 + 0.5)) + " %"; }

std::string plural(std::size_t n, std::string_view one, std::string_view many) {
    return std::to_string(n) + " " + std::string(n > 1 ? many : one);
}

std::string joinList(const std::vector<std::string>& items, std::size_t max = 6) {
    std::string out;
    for (std::size_t i = 0; i < items.size() && i < max; ++i) out += (i ? ", " : "") + items[i];
    if (items.size() > max) out += ", \xE2\x80\xA6";
    return out;
}

std::string clip(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\xE2\x80\xA6";
}

// json::Value (la piece jointe) <-> memory::Json (le registre) : par le texte (les deux lisent le meme JSON).
Json toMemory(const json::Value& v) {
    Json out;
    (void)memory::parseJson(json::write(v, false), out);
    return out;
}
json::Value toStore(const Json& j) {
    json::Value out;
    (void)json::parse(memory::writeJson(j, false), out);
    return out;
}

// Une empreinte courte (FNV-1a) de la signature : l'identifiant d'un modele.
std::string fingerprintOf(const std::vector<std::string>& sig) {
    std::uint32_t h = 2166136261u;
    for (const auto& s : sig) {
        for (const char c : s) {
            h ^= static_cast<unsigned char>(c);
            h *= 16777619u;
        }
        h ^= 0x1F;
        h *= 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", h);
    return buf;
}

std::string slugOf(std::string_view name) {
    std::string out;
    for (const char ch : ui::foldForSearch(name)) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 40) out.resize(40);
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("modele") : out;
}

std::string modelFile(std::string_view id) { return knowledgeDir() + "/" + std::string(id) + ".json"; }

json::Value modelData(const FileModel& m) {
    json::Value d = json::Value::object();
    d.set("format", kModelFormat);
    d.set("genre", std::string(genreKey(m.layout.genre)));
    d.set("mise", layoutToJson(m.layout));
    json::Value sig = json::Value::array();
    for (const auto& s : m.signature) sig.push(s);
    d.set("signature", sig);
    d.set("utilisations", m.uses);
    d.set("dernierUsage", m.lastUsed);
    return d;
}

// Ecrire le fichier du modele (les champs inconnus d'un fichier existant restent) ; `history` : sa ligne d'historique.
bool writeModelFile(FileModel& m, const std::string& history, std::string* why, bool content = true) {
    const std::string dir = knowledgeDir();
    if (dir.empty() || root().empty()) {
        if (why) *why = "pas de dossier pour l'Assistant IA";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dossiers::cheminDe(dir), ec);
    m.file = modelFile(m.id);
    Json j = Json::makeObject();
    std::string text;
    if (memory::readText(m.file, text)) {
        Json old;
        if (memory::parseJson(text, old) && old.isObject()) j = old;
    }
    const std::string now = memory::nowIso();
    if (m.added.empty()) m.added = j.str("ajoute", now);
    m.modified = now;
    j.set("format", Json::makeNumber(memory::kKnowledgeFormat));
    j.set("id", Json::makeString(m.id));
    j.set("nom", Json::makeString(m.name));
    j.set("description", Json::makeString("Mod\xC3\xA8le de fichier (" + genreLabel(m.layout.genre) + ") : reconnu \xC3\xA0 ses intitul\xC3\xA9s, lu sans analyse"));
    j.set("categorie", Json::makeString(std::string(kModelCategory)));
    j.set("source", Json::makeString(m.source.empty() ? std::string("fichier") : m.source));
    j.set("projet", Json::makeString(""));
    j.set("actif", Json::makeBool(m.active));
    j.set("ajoute", Json::makeString(m.added));
    j.set("modifie", Json::makeString(m.modified));
    if (content || !j.get("contenu")) j.set("contenu", Json::makeString(describe(m)));   // une utilisation de plus : le texte retouche reste
    if (!j.get("portee")) j.set("portee", Json::makeString("technique"));
    if (!j.get("statut")) j.set("statut", Json::makeString("confirme"));
    if (!j.get("confiance")) j.set("confiance", Json::makeNumber(1.0));
    if (!j.get("epingle")) j.set("epingle", Json::makeBool(false));
    if (!j.get("projetId")) j.set("projetId", Json::makeString(""));
    if (!j.get("sujet")) j.set("sujet", Json::makeString("modele-fichier:" + m.id));
    if (!j.get("conflits")) j.set("conflits", Json::makeArray());
    j.set("donnees", toMemory(modelData(m)));
    if (!history.empty()) {
        Json* h = j.get("historique");
        if (!h || !h->isArray()) {
            j.set("historique", Json::makeArray());
            h = j.get("historique");
        }
        Json line = Json::makeObject();
        line.set("date", Json::makeString(now));
        line.set("texte", Json::makeString(history));
        h->items.push_back(std::move(line));
        if (h->items.size() > 50) h->items.erase(h->items.begin(), h->items.begin() + static_cast<std::ptrdiff_t>(h->items.size() - 50));
    }
    std::string err;
    if (!memory::writeText(m.file, memory::writeJson(j, true), &err)) {
        if (why) *why = err.empty() ? std::string("\xC3\xA9" "criture impossible") : err;
        return false;
    }
    return true;
}

std::optional<FileModel> modelFrom(const memory::Knowledge& k) {
    if (k.builtIn || k.category != kModelCategory || !k.raw.isObject()) return std::nullopt;
    const Json* d = k.raw.get("donnees");
    if (!d || !d->isObject()) return std::nullopt;
    const json::Value v = toStore(*d);
    FileModel m;
    m.id = k.id;
    m.name = k.name;
    m.layout = layoutFromJson(v.at("mise"));
    if (const auto g = genreFromKey(v.str("genre"))) m.layout.genre = *g;
    for (const auto& s : v.at("signature").items())
        if (s.isString()) m.signature.push_back(s.asString());
    m.uses = static_cast<int>(v.integer("utilisations", 0));
    m.lastUsed = v.str("dernierUsage");
    m.added = k.added;
    m.modified = k.modified;
    m.active = k.active;
    m.source = k.source;
    m.file = k.file;
    if (m.layout.columns.empty() || m.signature.empty()) return std::nullopt;
    return m;
}

// Le sens d'une case : lecture seule (vrai), ecriture (faux), ou rien de sur.
std::optional<bool> readOnlyOf(const std::string& sens) {
    const std::string f = normalizeHeader(sens);
    if (f.empty()) return std::nullopt;
    if (f.find("ecri") != std::string::npos || f.find("write") != std::string::npos || f == "w" || f == "rw" || f == "r w" || f == "l e"
        || f == "e")
        return false;
    if (f.rfind("lect", 0) == 0 || f == "l" || f == "r" || f.rfind("read", 0) == 0 || f == "ro") return true;
    return std::nullopt;
}

// Une lettre latine accentuee (0xC3 xx) -> sa lettre sans accent, la casse gardee ; 0 : aucune.
char plainLetter(unsigned char d) {
    if (d >= 0x80 && d <= 0x85) return 'A';
    if (d == 0x87) return 'C';
    if (d >= 0x88 && d <= 0x8B) return 'E';
    if (d >= 0x8C && d <= 0x8F) return 'I';
    if (d == 0x91) return 'N';
    if (d >= 0x92 && d <= 0x96) return 'O';
    if (d >= 0x99 && d <= 0x9C) return 'U';
    if (d == 0x9D) return 'Y';
    if (d >= 0xA0 && d <= 0xA5) return 'a';
    if (d == 0xA7) return 'c';
    if (d >= 0xA8 && d <= 0xAB) return 'e';
    if (d >= 0xAC && d <= 0xAF) return 'i';
    if (d == 0xB1) return 'n';
    if (d >= 0xB2 && d <= 0xB6) return 'o';
    if (d >= 0xB9 && d <= 0xBC) return 'u';
    if (d == 0xBD || d == 0xBF) return 'y';
    return 0;
}

std::string lineWord(std::size_t line, const std::string& sheet, bool many) {
    return "ligne " + std::to_string(line) + (many ? " de " + guill(sheet) : std::string{});
}

// L'equipement Modbus des adresses : celui qu'on nomme, sinon le seul du projet. `note` : ce qu'il faut dire.
const hmi::Equipment* addressEquipment(const hmi::Project* project, const std::string& wanted, std::string& note) {
    if (!project) {
        note = "aucun projet ouvert : les adresses restent dans la description";
        return nullptr;
    }
    std::vector<const hmi::Equipment*> modbus;
    for (const auto& e : project->equipments)
        if (e.modbus()) modbus.push_back(&e);
    if (!wanted.empty()) {
        for (const auto& e : project->equipments)
            if (iequal(e.name, wanted)) {
                if (!e.modbus()) {
                    note = guill(e.name) + " est un \xC3\xA9quipement Ethernet TCP/IP : il n'a pas d'adresses (Modbus TCP/IP seulement) ; les adresses restent dans la description";
                    return nullptr;
                }
                return &e;
            }
        note = "l'\xC3\xA9quipement " + guill(wanted) + " n'existe pas dans le projet : les adresses restent dans la description";
        return nullptr;
    }
    if (modbus.size() == 1) {
        note = "sur " + guill(modbus.front()->name) + ", le seul \xC3\xA9quipement Modbus du projet";
        return modbus.front();
    }
    if (modbus.empty()) {
        note = "aucun \xC3\xA9quipement Modbus dans le projet : les adresses %MW/%M restent dans la description - cr\xC3\xA9" "e l'\xC3\xA9quipement, puis demande "
               "`Adresse les variables du fichier joint sur l'\xC3\xA9quipement API_1`";
        return nullptr;
    }
    std::vector<std::string> names;
    for (const auto* e : modbus) names.push_back(e->name);
    note = "plusieurs \xC3\xA9quipements Modbus (" + joinList(names) + ") : dis lequel, par exemple `\xE2\x80\xA6 sur l'\xC3\xA9quipement " + names.front()
           + "` ; en attendant, les adresses restent dans la description";
    return nullptr;
}

std::string headerOf(const Layout& L, std::string_view role) {
    for (const auto& c : L.columns)
        if (c.role == role) return c.header.empty() ? roleLabel(role) : c.header;
    return roleLabel(role);
}

std::string sourceLine(const Attached& f, const std::vector<Reading>& readings) {
    std::size_t rows = 0, ignored = 0;
    std::vector<std::string> names;
    for (const auto& r : readings) {
        rows += r.rows.size();
        ignored += r.ignored;
        names.push_back(guill(r.sheet));
    }
    std::string s = "**" + genreLabel(f.layout.genre) + "** " + guill(f.name);
    if (!f.modelName.empty()) s += " (mod\xC3\xA8le " + guill(f.modelName) + (f.byUser ? std::string(", choisi") : std::string(" reconnu")) + ")";
    else if (f.byUser) s += " (correspondance choisie)";
    else s += " (d\xC3\xA9tect\xC3\xA9, confiance " + percent(f.confidence) + ")";
    s += ", " + std::string(names.size() > 1 ? "feuilles " : "feuille ") + joinList(names) + " : " + plural(rows, "ligne lue", "lignes lues");
    if (ignored) s += " (" + plural(ignored, "ignor\xC3\xA9" "e", "ignor\xC3\xA9" "es") + " : ni rep\xC3\xA8re ni adresse)";
    return s + ".";
}

// 1.21.1 : une feuille, en quelques mots (pour l'IA et pour l'utilisateur).
std::string sheetWords(const SheetInfo& si, bool tool) {
    std::string d = guill(si.name) + " : ";
    if (si.hidden) d += "masqu\xC3\xA9" "e, ";
    if (!si.headerFound) d += "aucun en-t\xC3\xAA" "te reconnu";
    else {
        d += (si.genre == Genre::Other ? std::string("ni E/S, ni r\xC3\xA9seau, ni reports") : genreLabel(si.genre)) + ", en-t\xC3\xAA" "te ligne "
             + std::to_string(si.headerLine) + ", " + plural(si.rows, "ligne", "lignes");
    }
    if (si.read) d += " \xE2\x80\x94 lue";
    else if (tool) d += " \xE2\x80\x94 non lue : " + std::string(kRowsTool) + " avec feuille=\"" + si.name + "\"";
    else d += " \xE2\x80\x94 non lue";
    return d;
}

// La mise en page d'une AUTRE feuille, lue avec celle de `layout` : la meme (une jumelle : les memes intitules sur la
// meme ligne), sinon sa propre detection si elle est du meme genre ; nullopt : elle n'est pas lue avec cette mise en page.
std::optional<Layout> sameKindLayout(const Sheet& s, const Layout& layout, const std::vector<std::string>& sig, std::size_t named,
                                     const SheetDetection* known, std::size_t index) {
    if (s.hidden) return std::nullopt;
    if (named >= 2 && headerCells(s, layout.headerRow) == sig) {
        Layout twin = layout;
        twin.sheet = s.name;
        return twin;
    }
    if (layout.genre == Genre::Other) return std::nullopt;
    const SheetDetection d = known && known->layout.columns.size() > 0 && known->sheet == s.name ? *known : detectSheet(s, index);
    if (!d.headerFound || d.genre != layout.genre) return std::nullopt;
    Layout own = d.layout;
    own.sheet = s.name;
    own.genre = d.genre;
    return own;
}

} // namespace

// ------------------------------------------------------------------------- la memoire ---
std::vector<FileModel> modelsIn(const memory::Registry& reg, bool withInactive) {
    std::vector<FileModel> out;
    for (const auto& k : reg.all())
        if (auto m = modelFrom(k); m && (withInactive || m->active)) out.push_back(std::move(*m));
    return out;
}

std::vector<FileModel> loadModels(bool withInactive) {
    if (root().empty()) return {};
    memory::Registry reg;
    reg.load();
    return modelsIn(reg, withInactive);
}

std::optional<FileModel> findModel(std::string_view idOrName) {
    for (auto& m : loadModels(true))
        if (m.id == idOrName || iequal(m.name, idOrName)) return m;
    return std::nullopt;
}

FileModel makeModel(const Workbook& wb, const Layout& layout, std::string name) {
    FileModel m;
    m.name = std::move(name);
    m.layout = layout;
    if (const Sheet* s = wb.sheet(layout.sheet)) {
        m.layout.sheet = s->name;
        m.signature = headerCells(*s, layout.headerRow);
        // les intitules ecrits, tels quels (la fenetre les montre)
        for (auto& c : m.layout.columns)
            if (c.header.empty()) c.header = s->cell(layout.headerRow, c.col);
    }
    return m;
}

bool saveModel(FileModel& m, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    while (!m.name.empty() && std::isspace(static_cast<unsigned char>(m.name.back()))) m.name.pop_back();
    while (!m.name.empty() && std::isspace(static_cast<unsigned char>(m.name.front()))) m.name.erase(0, 1);
    if (m.name.empty()) return fail("un mod\xC3\xA8le de fichier a un nom");
    if (m.name.size() > 80) return fail("nom trop long (80 caract\xC3\xA8res au plus)");
    if (m.layout.columns.empty()) return fail("aucune colonne reconnue : choisis au moins la colonne d'un r\xC3\xB4le");
    if (m.signature.empty()) return fail("la ligne d'en-t\xC3\xAA" "te est vide : elle ne peut pas reconna\xC3\xAEtre un fichier");
    const auto all = loadModels(true);
    for (const auto& o : all)
        if (o.id != m.id && iequal(o.name, m.name)) return fail("un mod\xC3\xA8le porte d\xC3\xA9j\xC3\xA0 ce nom : " + guill(o.name));
    const bool fresh = m.id.empty();
    if (fresh) {
        const std::string base = "fichier." + slugOf(m.name) + "." + fingerprintOf(m.signature);
        m.id = base;
        std::error_code ec;
        for (int n = 2; std::any_of(all.begin(), all.end(), [&](const FileModel& o) { return o.id == m.id; }) || fs::exists(dossiers::cheminDe(modelFile(m.id)), ec);
             ++n)
            m.id = base + "-" + std::to_string(n);
        if (m.source.empty()) m.source = "fichier";
    }
    if (!m.layout.sheet.empty() && m.layout.rules.keyRoles.empty()) m.layout.rules.keyRoles = defaultKeyRoles(m.layout.genre);
    if (!writeModelFile(m, fresh ? "Mod\xC3\xA8le de fichier enregistr\xC3\xA9 (" + genreLabel(m.layout.genre) + ", feuille " + guill(m.layout.sheet) + ")"
                                 : std::string("Mod\xC3\xA8le de fichier modifi\xC3\xA9"),
                        why))
        return false;
    (void)memory::appendActivity("fichier", std::string(fresh ? "Mod\xC3\xA8le de fichier enregistr\xC3\xA9 : " : "Mod\xC3\xA8le de fichier modifi\xC3\xA9 : ") + m.name + " ("
                                                 + genreLabel(m.layout.genre) + ")");
    return true;
}

bool renameModel(std::string_view id, const std::string& name, std::string* why) {
    auto m = findModel(id);
    if (!m || m->id != id) {
        if (why) *why = "mod\xC3\xA8le introuvable";
        return false;
    }
    const std::string before = m->name;
    m->name = name;
    if (!saveModel(*m, why)) return false;
    (void)before;
    return true;
}

bool removeModel(std::string_view id, std::string* why) {
    const auto m = findModel(id);
    if (!m || m->id != id) {
        if (why) *why = "mod\xC3\xA8le introuvable";
        return false;
    }
    std::error_code ec;
    fs::remove(dossiers::cheminDe(m->file.empty() ? modelFile(id) : m->file), ec);
    if (ec) {
        if (why) *why = ec.message();
        return false;
    }
    (void)memory::appendActivity("suppression", "Mod\xC3\xA8le de fichier supprim\xC3\xA9 : " + m->name);
    return true;
}

bool noteUse(std::string_view id) {
    auto m = findModel(id);
    if (!m || m->id != id) return false;
    ++m->uses;
    m->lastUsed = memory::nowIso();
    std::string why;
    return writeModelFile(*m, {}, &why, false);
}

std::string mappingText(const Layout& L) {
    std::string out;
    for (const auto& c : L.columns)
        out += (out.empty() ? "" : ", ") + roleLabel(c.role) + " = " + columnLetter(c.col) + (c.header.empty() ? std::string{} : " (" + c.header + ")");
    return out;
}

std::string chatText(const FileModel& m, bool created) {
    json::Value o = json::Value::object();
    o.set("nom", m.name);
    o.set("id", m.id);
    o.set("format", kModelFormat);
    o.set("mise", layoutToJson(m.layout));
    json::Value sig = json::Value::array();
    for (const auto& x : m.signature) sig.push(x);
    o.set("signature", std::move(sig));
    std::string s = std::string(created ? "**Mod\xC3\xA8le de fichier enregistr\xC3\xA9**" : "**Mod\xC3\xA8le de fichier mis \xC3\xA0 jour**") + " : " + guill(m.name) + " \xE2\x80\x94 "
                    + genreLabel(m.layout.genre) + ", feuille " + guill(m.layout.sheet) + ", en-t\xC3\xAA" "te ligne " + std::to_string(m.layout.headerRow + 1) + ".\n\n";
    s += "Colonnes : " + mappingText(m.layout) + ".\n\n";
    s += "Il est gard\xC3\xA9 dans la m\xC3\xA9moire de l'IA (M\xC3\xA9moire et apprentissage \xE2\x80\xBA Apprentissage \xE2\x80\xBA Mod\xC3\xA8les de fichiers), "
         "partag\xC3\xA9 avec toutes les IA et emport\xC3\xA9 par l'export .xia : un classeur aux m\xC3\xAAmes intitul\xC3\xA9s sera lu directement, sans analyse.\n\n";
    s += "```json\n" + json::write(o, true) + "\n```";
    return s;
}

std::string describe(const FileModel& m) {
    const Layout& L = m.layout;
    std::string s = "Mod\xC3\xA8le de fichier " + guill(m.name) + " : " + genreLabel(L.genre) + ".\n";
    s += "Reconnu \xC3\xA0 ses intitul\xC3\xA9s (feuille " + guill(L.sheet) + ", ligne d'en-t\xC3\xAA" "te " + std::to_string(L.headerRow + 1) + ")";
    std::vector<std::string> heads;
    for (const auto& c : L.columns) heads.push_back(c.header.empty() ? roleLabel(c.role) : c.header);
    if (!heads.empty()) s += " : " + joinList(heads, 12);
    s += ".\nCorrespondance : " + mappingText(L) + ".\n";
    std::vector<std::string> rules;
    if (L.rules.skipAfterHeader) rules.push_back(plural(L.rules.skipAfterHeader, "ligne saut\xC3\xA9" "e", "lignes saut\xC3\xA9" "es") + " sous l'en-t\xC3\xAA" "te");
    if (L.rules.mergedCells) rules.push_back("cases fusionn\xC3\xA9" "es recopi\xC3\xA9" "es");
    if (!L.rules.carryDown.empty()) {
        std::vector<std::string> r;
        for (const auto& x : L.rules.carryDown) r.push_back(roleLabel(x));
        rules.push_back("recopi\xC3\xA9" "es vers le bas : " + joinList(r));
    }
    const auto keys = L.rules.keyRoles.empty() ? defaultKeyRoles(L.genre) : L.rules.keyRoles;
    if (!keys.empty()) {
        std::vector<std::string> r;
        for (const auto& x : keys) r.push_back(roleLabel(x));
        rules.push_back("une ligne sans " + joinList(r) + " est ignor\xC3\xA9" "e");
    }
    if (!rules.empty()) s += "R\xC3\xA8gles : " + joinList(rules, 8) + ".\n";
    s += "Un classeur joint qui a ces intitul\xC3\xA9s est lu directement avec cette correspondance (l'outil " + std::string(kRowsTool)
         + " en donne les lignes) : propose ensuite les variables, les types, les \xC3\xA9quipements ou les adresses qu'il d\xC3\xA9" "crit, "
           "sans rien inventer.";
    return s;
}

// --------------------------------------------------------------------------- reconnaitre ---
std::optional<Match> match(const Workbook& wb, const std::vector<FileModel>& models) {
    if (models.empty()) return std::nullopt;
    // les intitules de chaque ligne candidate, calcules une fois
    std::vector<std::vector<std::vector<std::string>>> cells(wb.sheets.size());
    for (std::size_t i = 0; i < wb.sheets.size(); ++i) {
        const std::size_t n = std::min<std::size_t>(wb.sheets[i].rows.size(), 40);
        for (std::size_t r = 0; r < n; ++r) cells[i].push_back(headerCells(wb.sheets[i], r));
    }
    std::optional<Match> best;
    for (std::size_t mi = 0; mi < models.size(); ++mi) {
        const auto& m = models[mi];
        if (!m.active || m.signature.empty()) continue;
        std::size_t want = 0;
        for (const auto& s : m.signature) want += s.empty() ? 0 : 1;
        if (want < 2) continue;
        for (std::size_t si = 0; si < wb.sheets.size(); ++si)
            for (std::size_t r = 0; r < cells[si].size(); ++r) {
                const auto& got = cells[si][r];
                std::size_t same = 0;
                for (std::size_t c = 0; c < m.signature.size(); ++c)
                    if (!m.signature[c].empty() && c < got.size() && got[c] == m.signature[c]) ++same;
                double score = static_cast<double>(same) / static_cast<double>(want);
                if (score < 0.9) continue;
                // chaque colonne d'un role : son intitule a l'identique
                const bool roles = std::all_of(m.layout.columns.begin(), m.layout.columns.end(), [&](const Column& col) {
                    return col.col < m.signature.size() && col.col < got.size() && got[col.col] == m.signature[col.col];
                });
                if (!roles) continue;
                if (got.size() == m.signature.size() && same == want) score = 1.0;
                if (wb.sheets[si].name == m.layout.sheet) score += 0.02;
                if (r == m.layout.headerRow) score += 0.01;
                if (wb.sheets[si].hidden) score -= 0.05;
                if (!best || score > best->score) best = Match{mi, si, r, score};
            }
    }
    return best;
}

Recognition recognize(const Workbook& wb, const std::vector<FileModel>& models) {
    Recognition r;
    r.detection = detect(wb);
    if (const auto m = match(wb, models)) {
        const FileModel& fm = models[m->model];
        r.layout = fm.layout;
        r.layout.sheet = wb.sheets[m->sheet].name;
        r.layout.headerRow = m->headerRow;
        r.genre = fm.layout.genre;
        r.confidence = std::min(1.0, m->score);
        r.modelId = fm.id;
        r.modelName = fm.name;
        r.hasLayout = true;
    } else if (const auto* d = r.detection.chosen(); d && d->genre != Genre::Other) {
        r.layout = d->layout;
        r.genre = d->genre;
        r.confidence = d->confidence;
        r.hasLayout = true;
    } else if (const auto* d2 = r.detection.chosen()) {
        r.layout = d2->layout;
        r.genre = Genre::Other;
        r.confidence = d2->confidence;
    }
    std::vector<std::string> others;
    if (r.hasLayout) {
        const auto readings = readAll(wb, r.layout, &r.detection);
        for (const auto& rd : readings) {
            r.rows += rd.rows.size();
            r.ignored += rd.ignored;
            r.empty += rd.empty;
            r.sheetsRead.emplace_back(rd.sheet, rd.rows.size());
            if (rd.sheet != r.layout.sheet) others.push_back(guill(rd.sheet) + " (" + plural(rd.rows.size(), "ligne", "lignes") + ")");
        }
    }
    const std::string lines = plural(r.rows, "ligne lue", "lignes lues");
    if (!r.modelName.empty()) r.summary = "Mod\xC3\xA8le reconnu : " + r.modelName + " \xE2\x80\x94 " + lines;
    else if (r.hasLayout) r.summary = "D\xC3\xA9tection : " + genreLabel(r.genre) + " (confiance " + percent(r.confidence) + ") \xE2\x80\x94 " + lines;
    if (r.hasLayout) {
        const std::size_t mainRows = r.sheetsRead.empty() ? 0 : r.sheetsRead.front().second;
        r.details.push_back(genreLabel(r.genre) + " : feuille " + guill(r.layout.sheet) + " (" + plural(mainRows, "ligne", "lignes") + "), en-t\xC3\xAA" "te ligne "
                            + std::to_string(r.layout.headerRow + 1) + (others.empty() ? std::string{} : " ; lues aussi : " + joinList(others, 30)));
        r.details.push_back("colonnes : " + mappingText(r.layout));
        if (r.ignored) r.details.push_back(plural(r.ignored, "ligne ignor\xC3\xA9" "e", "lignes ignor\xC3\xA9" "es") + " (ni rep\xC3\xA8re ni adresse)");
        // 1.21.1 : les feuilles qui ne sont pas lues, et pourquoi (l'IA peut les lire une a une)
        for (const auto& d : r.detection.sheets) {
            if (std::any_of(r.sheetsRead.begin(), r.sheetsRead.end(), [&](const auto& x) { return x.first == d.sheet; })) continue;
            std::string why = d.hidden                 ? std::string("masqu\xC3\xA9" "e")
                              : !d.headerFound         ? std::string("aucun en-t\xC3\xAA" "te reconnu")
                              : d.genre == Genre::Other ? std::string("ni E/S, ni r\xC3\xA9seau, ni reports")
                                                        : genreLabel(d.genre) + " (confiance " + percent(d.confidence) + "), un autre genre";
            r.details.push_back("feuille " + guill(d.sheet) + " non lue : " + why);
        }
    } else {
        for (const auto& d : r.detection.sheets)
            if (d.genre != Genre::Other && d.sheet != r.layout.sheet)
                r.details.push_back("feuille " + guill(d.sheet) + " : " + genreLabel(d.genre) + " (confiance " + percent(d.confidence) + ")");
    }
    if (r.modelName.empty())
        if (const auto* d = r.detection.chosen())
            for (const auto& why : d->reasons) r.details.push_back("d\xC3\xA9tection : " + why);
    return r;
}

// ----------------------------------------------------------------------------------- le JSON ---
json::Value layoutToJson(const Layout& L) {
    json::Value o = json::Value::object();
    o.set("genre", std::string(genreKey(L.genre)));
    o.set("feuille", L.sheet);
    o.set("ligneEntete", static_cast<int>(L.headerRow + 1));
    json::Value cols = json::Value::array();
    for (const auto& c : L.columns) {
        json::Value x = json::Value::object();
        x.set("role", c.role);
        x.set("colonne", columnLetter(c.col));
        x.set("intitule", c.header);
        cols.push(std::move(x));
    }
    o.set("colonnes", std::move(cols));
    json::Value rules = json::Value::object();
    rules.set("sauterSousEntete", static_cast<int>(L.rules.skipAfterHeader));
    const auto list = [](const std::vector<std::string>& v) {
        json::Value a = json::Value::array();
        for (const auto& s : v) a.push(s);
        return a;
    };
    rules.set("valeursVides", list(L.rules.emptyValues));
    rules.set("recopierVersLeBas", list(L.rules.carryDown));
    rules.set("fusions", L.rules.mergedCells);
    rules.set("cles", list(L.rules.keyRoles));
    o.set("regles", std::move(rules));
    return o;
}

Layout layoutFromJson(const json::Value& v) {
    Layout L;
    if (!v.isObject()) return L;
    if (const auto g = genreFromKey(v.str("genre"))) L.genre = *g;
    L.sheet = v.str("feuille");
    const auto h = v.integer("ligneEntete", 1);
    L.headerRow = h > 0 ? static_cast<std::size_t>(h - 1) : 0;
    for (const auto& c : v.at("colonnes").items()) {
        if (!c.isObject()) continue;
        const std::string role = c.str("role"), letter = c.str("colonne");
        if (!knownRole(role) || letter.empty() || !std::isalpha(static_cast<unsigned char>(letter[0]))) continue;
        L.set(role, static_cast<int>(xls::columnOf(letter)), c.str("intitule"));
    }
    const json::Value& r = v.at("regles");
    if (r.isObject()) {
        const auto n = r.integer("sauterSousEntete", 0);
        L.rules.skipAfterHeader = n > 0 && n < 1000 ? static_cast<std::size_t>(n) : 0;
        const auto list = [](const json::Value& a, bool roles) {
            std::vector<std::string> out;
            for (const auto& x : a.items())
                if (x.isString() && (!roles || knownRole(x.asString()))) out.push_back(x.asString());
            return out;
        };
        if (r.has("valeursVides")) L.rules.emptyValues = list(r.at("valeursVides"), false);
        L.rules.carryDown = list(r.at("recopierVersLeBas"), true);
        L.rules.mergedCells = r.flag("fusions", true);
        L.rules.keyRoles = list(r.at("cles"), true);
    }
    return L;
}

json::Value recognitionToJson(const Recognition& r) {
    json::Value o = json::Value::object();
    o.set("genre", std::string(genreKey(r.genre)));
    o.set("confiance", r.confidence);
    o.set("structure", r.hasLayout);
    o.set("modele", r.modelId);
    o.set("nomModele", r.modelName);
    o.set("lignes", static_cast<std::uint64_t>(r.rows));
    o.set("ignorees", static_cast<std::uint64_t>(r.ignored));
    o.set("vides", static_cast<std::uint64_t>(r.empty));
    o.set("mise", layoutToJson(r.layout));
    json::Value sheets = json::Value::array();
    for (const auto& d : r.detection.sheets) {
        json::Value s = json::Value::object();
        s.set("nom", d.sheet);
        s.set("genre", std::string(genreKey(d.genre)));
        s.set("confiance", d.confidence);
        s.set("lignes", static_cast<std::uint64_t>(d.dataRows));
        s.set("entete", static_cast<int>(d.layout.headerRow + 1));
        s.set("enteteTrouvee", d.headerFound);   // 1.21.1
        s.set("masquee", d.hidden);
        const auto it = std::find_if(r.sheetsRead.begin(), r.sheetsRead.end(), [&](const auto& x) { return x.first == d.sheet; });
        s.set("lue", it != r.sheetsRead.end());
        if (it != r.sheetsRead.end()) s.set("lues", static_cast<std::uint64_t>(it->second));
        sheets.push(std::move(s));
    }
    o.set("feuilles", std::move(sheets));
    o.set("resume", r.summary);
    return o;
}

void applyRecognition(AttachmentAnalysis& an, const Recognition& r) {
    an.extra.set("classeur", recognitionToJson(r));
    if (!r.hasLayout) return;   // << autre >> : l'analyse reste celle du texte
    if (!an.summary.empty()) an.details.insert(an.details.begin(), an.summary);
    an.summary = r.summary;
    for (auto it = r.details.rbegin(); it != r.details.rend(); ++it) an.details.insert(an.details.begin() + 1, *it);
}

// ------------------------------------------------------------------------- le choix ---
std::optional<Recognition> recognitionOf(const Attachment& a) {
    for (auto it = a.analyses.rbegin(); it != a.analyses.rend(); ++it) {
        if (it->kind != "extraction") continue;
        const json::Value* c = it->extra.find("classeur");
        if (it->state != "analysee" || !c || !c->isObject()) return std::nullopt;
        Recognition r;
        if (const auto g = genreFromKey(c->str("genre"))) r.genre = *g;
        r.confidence = c->num("confiance", 0);
        r.hasLayout = c->flag("structure", false);
        r.modelId = c->str("modele");
        r.modelName = c->str("nomModele");
        r.rows = static_cast<std::size_t>(std::max<std::int64_t>(0, c->integer("lignes", 0)));
        r.ignored = static_cast<std::size_t>(std::max<std::int64_t>(0, c->integer("ignorees", 0)));
        r.empty = static_cast<std::size_t>(std::max<std::int64_t>(0, c->integer("vides", 0)));
        r.layout = layoutFromJson(c->at("mise"));
        r.summary = c->str("resume");
        for (const auto& s : c->at("feuilles").items()) {
            SheetDetection d;
            d.sheet = s.str("nom");
            if (const auto g = genreFromKey(s.str("genre"))) d.genre = *g;
            d.confidence = s.num("confiance", 0);
            d.dataRows = static_cast<std::size_t>(std::max<std::int64_t>(0, s.integer("lignes", 0)));
            const auto e = s.integer("entete", 1);
            d.layout.headerRow = e > 0 ? static_cast<std::size_t>(e - 1) : 0;
            d.layout.sheet = d.sheet;
            d.layout.genre = d.genre;
            // 1.21.1 (une analyse d'avant : un genre reconnu avait son en-tete ; la feuille de la mise en page etait lue)
            d.headerFound = s.flag("enteteTrouvee", d.genre != Genre::Other);
            d.hidden = s.flag("masquee", false);
            if (s.flag("lue", r.hasLayout && d.sheet == r.layout.sheet))
                r.sheetsRead.emplace_back(d.sheet, static_cast<std::size_t>(std::max<std::int64_t>(0, s.integer("lues", d.sheet == r.layout.sheet ? static_cast<std::int64_t>(r.rows) : 0))));
            d.index = r.detection.sheets.size();
            if (d.sheet == r.layout.sheet) r.detection.best = d.index;
            r.detection.sheets.push_back(std::move(d));
        }
        return r;
    }
    return std::nullopt;
}

std::optional<Choice> choiceOf(const Attachment& a) {
    if (const json::Value* c = a.extra.find("modeleFichier"); c && c->isObject()) {
        Choice ch;
        ch.layout = layoutFromJson(c->at("mise"));
        ch.modelId = c->str("modele");
        ch.modelName = c->str("nom");
        ch.byUser = true;
        ch.confidence = c->num("confiance", 1.0);
        ch.rows = static_cast<std::size_t>(std::max<std::int64_t>(0, c->integer("lignes", 0)));
        if (!ch.layout.sheet.empty() && !ch.layout.columns.empty()) return ch;
    }
    if (const auto r = recognitionOf(a); r && r->hasLayout) {
        Choice ch;
        ch.layout = r->layout;
        ch.modelId = r->modelId;
        ch.modelName = r->modelName;
        ch.confidence = r->confidence;
        ch.rows = r->rows;
        return ch;
    }
    return std::nullopt;
}

void setChoice(Attachment& a, const Choice& c) {
    json::Value o = json::Value::object();
    o.set("modele", c.modelId);
    o.set("nom", c.modelName);
    o.set("confiance", c.confidence);
    o.set("lignes", static_cast<std::uint64_t>(c.rows));
    o.set("date", isoDate(now()));
    o.set("mise", layoutToJson(c.layout));
    if (!a.extra.isObject()) a.extra = json::Value::object();
    a.extra.set("modeleFichier", std::move(o));
}

void clearChoice(Attachment& a) {
    if (a.extra.isObject()) (void)a.extra.erase("modeleFichier");
}

Attached attachedOf(const Attachment& a, std::string path, std::shared_ptr<const Workbook> workbook) {
    Attached f;
    f.id = a.id;
    f.name = a.name;
    f.path = std::move(path);
    f.workbook = std::move(workbook);
    if (const auto c = choiceOf(a)) {
        f.hasLayout = true;
        f.layout = c->layout;
        f.modelId = c->modelId;
        f.modelName = c->modelName;
        f.byUser = c->byUser;
        f.confidence = c->confidence;
        f.rows = c->rows;
    }
    // 1.21.1 : toutes les feuilles, telles que l'analyse les a vues
    if (const auto r = recognitionOf(a)) {
        for (const auto& d : r->detection.sheets) {
            SheetInfo si;
            si.name = d.sheet;
            si.genre = d.genre;
            si.headerFound = d.headerFound;
            si.headerLine = d.headerFound ? d.layout.headerRow + 1 : 0;
            si.hidden = d.hidden;
            si.rows = d.dataRows;
            const auto it = std::find_if(r->sheetsRead.begin(), r->sheetsRead.end(), [&](const auto& x) { return x.first == d.sheet; });
            if (f.hasLayout && !d.hidden) {
                si.read = d.sheet == f.layout.sheet || (it != r->sheetsRead.end() && f.layout.genre == r->layout.genre)
                          || (d.headerFound && d.genre == f.layout.genre && d.genre != Genre::Other);
            }
            if (si.read && it != r->sheetsRead.end()) si.rows = it->second;
            f.sheets.push_back(std::move(si));
        }
    }
    return f;
}

std::vector<Reading> readAll(const Workbook& wb, const Layout& layout, const Detection* detection) {
    std::vector<Reading> out;
    out.push_back(read(wb, layout));
    const Sheet* main = wb.sheet(layout.sheet);
    if (!main) return out;
    const auto sig = headerCells(*main, layout.headerRow);
    std::size_t named = 0;
    for (const auto& s : sig) named += s.empty() ? 0 : 1;
    for (std::size_t i = 0; i < wb.sheets.size(); ++i) {
        const Sheet& s = wb.sheets[i];
        if (&s == main) continue;
        const SheetDetection* known = detection && i < detection->sheets.size() ? &detection->sheets[i] : nullptr;
        const auto L = sameKindLayout(s, layout, sig, named, known, i);
        if (!L) continue;
        Reading rd = read(wb, *L);
        if (rd.rows.empty() && L->columns != layout.columns) continue;   // une feuille du meme genre, mais vide
        out.push_back(std::move(rd));
    }
    return out;
}

// ------------------------------------------------------------------------- les actions ---
std::string identifierFrom(std::string_view raw, bool* adjusted) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const auto c = static_cast<unsigned char>(raw[i]);
        if (std::isalnum(c) || c == '_') {
            out += static_cast<char>(c);
            continue;
        }
        if (c == 0xC3 && i + 1 < raw.size()) {
            if (const char p = plainLetter(static_cast<unsigned char>(raw[i + 1]))) {
                out += p;
                ++i;
                continue;
            }
        }
        if (c >= 0x80) {   // un autre caractere non ASCII : ses octets suivants partent avec lui
            while (i + 1 < raw.size() && (static_cast<unsigned char>(raw[i + 1]) & 0xC0) == 0x80) ++i;
        }
        if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    // les _ du debut viennent d'un caractere retire : ils partent ; un chiffre en tete prend un _
    std::size_t lead = 0;
    while (lead < out.size() && out[lead] == '_' && !(lead < raw.size() && raw[lead] == '_')) ++lead;
    out.erase(0, lead);
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    if (out.size() > 64) out.resize(64);
    if (adjusted) *adjusted = out != raw;
    return out;
}

Plan planVariables(const Attached& f, const std::vector<Reading>& readings, const hmi::Project* project, const PlanOptions& o) {
    Plan p;
    const Layout& L = f.layout;
    const Genre g = L.genre;
    const bool many = readings.size() > 1;
    if (!L.has("repere")) {
        p.text = sourceLine(f, readings) + "\nCe fichier n'a pas de colonne **rep\xC3\xA8re** (le nom, le tag) reconnue : je ne fabrique pas de noms de variables. "
                 "Choisis la colonne du rep\xC3\xA8re dans **Contexte de la discussion \xE2\x80\xBA " + f.name + " \xE2\x80\xBA Mod\xC3\xA8le\xE2\x80\xA6**, puis redemande.";
        return p;
    }
    struct Item {
        std::string name, raw, type, desc, address, sens, sheet;
        std::size_t line{0};
        bool        exists{false};
        bool        addressed{false};
    };
    std::vector<Item> items;
    std::vector<std::string> noName, unreadable, duplicates, noType, adjusted, otherType, existing, absent;
    std::map<std::string, std::string> seen;   // NOM -> ou
    std::size_t total = 0;
    bool capped = false;
    for (const auto& rd : readings)
        for (const auto& row : rd.rows) {
            ++total;
            if (items.size() >= o.maxRows) {
                capped = true;
                continue;
            }
            const std::string where = lineWord(row.line, rd.sheet, many);
            const std::string raw = row.value("repere");
            if (raw.empty()) {
                noName.push_back(where);
                continue;
            }
            bool adj = false;
            const std::string name = identifierFrom(raw, &adj);
            if (name.empty() || !hmi::isIdentifier(name) || hmi::isReservedWord(name) || iequal(name, "SYS") || iequal(name, "API")) {
                unreadable.push_back(guill(raw) + " (" + where + ")");
                continue;
            }
            const std::string key = upperOf(name);
            if (const auto it = seen.find(key); it != seen.end()) {
                duplicates.push_back(name + " (" + where + " ; d\xC3\xA9j\xC3\xA0 " + it->second + ")");
                continue;
            }
            seen[key] = where;
            Item it;
            it.name = name;
            it.raw = raw;
            it.line = row.line;
            it.sheet = rd.sheet;
            it.sens = row.value("sens");
            it.address = row.value("adresse");
            if (it.address.empty()) {
                const std::string mot = row.value("mot"), bit = row.value("bit");
                if (!mot.empty() && isMemoryAddress(mot)) {
                    it.address = mot;
                    if (allDigits(bit) && std::atoi(bit.c_str()) <= 15 && mot.find('.') == std::string::npos && mot.find(':') == std::string::npos)
                        it.address = mot + "." + bit;
                }
            }
            // le type : celui du fichier (BOOL, INT...), sinon le signal (DI/DO -> BOOL, AI/AO -> REAL), sinon l'adresse
            std::string t = dataTypeOf(row.value("type"));
            if (t.empty()) {
                const std::string sig = signalOf(row.value("type"));
                if (sig == "DI" || sig == "DO" || sig == "TOR") t = "BOOL";
                else if (sig == "AI" || sig == "AO" || sig == "ANA") t = "REAL";
            }
            if (t.empty()) t = typeOfAddress(it.address);
            if (t.empty()) {
                noType.push_back(name + " (" + where + ")");
                continue;
            }
            it.type = t;
            // la description : la designation (sinon le commentaire), et ce que le fichier dit de plus
            std::string d = row.value("designation");
            if (d.empty()) d = row.value("commentaire");
            std::vector<std::string> extra;
            if (!it.address.empty()) extra.push_back(it.address);
            else {
                std::vector<std::string> place;
                if (!row.value("rack").empty()) place.push_back("rack " + row.value("rack"));
                if (!row.value("slot").empty()) place.push_back("emplacement " + row.value("slot"));
                if (!row.value("voie").empty()) place.push_back("voie " + row.value("voie"));
                if (!place.empty()) extra.push_back(joinList(place, 3));
            }
            const std::string mn = row.value("min"), mx = row.value("max"), un = row.value("unite");
            if (!mn.empty() || !mx.empty()) extra.push_back((mn.empty() ? std::string("?") : mn) + " \xC3\xA0 " + (mx.empty() ? std::string("?") : mx) + (un.empty() ? std::string{} : " " + un));
            else if (!un.empty()) extra.push_back(un);
            it.desc = d;
            for (std::size_t i = 0; i < extra.size(); ++i) it.desc += (i == 0 ? (d.empty() ? "" : " (") : ", ") + extra[i];
            if (!d.empty() && !extra.empty()) it.desc += ")";
            if (it.desc.empty()) it.desc = "Lu de " + f.name + ", " + where;
            it.desc = clip(it.desc, 200);
            if (adj && adjusted.size() < 60) adjusted.push_back(raw + " \xE2\x86\x92 " + name);
            if (project)
                if (const auto* v = project->variable(name)) {
                    if (iequal(hmi::types::normalized(v->type), t)) {
                        it.exists = true;
                        existing.push_back(name);
                    } else {
                        otherType.push_back(name + " (" + v->type + " dans le projet, " + t + " dans le fichier)");
                        continue;
                    }
                }
            if (o.onlyExisting && !it.exists) {
                absent.push_back(name);
                continue;
            }
            items.push_back(std::move(it));
        }
    // l'equipement des adresses memoire
    std::string eqNote;
    const bool memAddresses = std::any_of(items.begin(), items.end(), [](const Item& x) { return !x.address.empty() && x.address[0] == '%' && isMemoryAddress(x.address); });
    const hmi::Equipment* eq = (o.address && memAddresses) ? addressEquipment(project, o.equipment, eqNote) : nullptr;
    const auto addressable = [&](const Item& x) {
        if (!eq || x.address.empty() || x.address[0] != '%' || !isMemoryAddress(x.address)) return false;
        std::string why;
        return !hmi::equip::canonicalAddress(x.address, &why).empty();
    };
    // LES FAMILLES : P101_MARCHE, P101_DEFAUT, P102_MARCHE, P102_DEFAUT -> un type T_P (MARCHE, DEFAUT), P101 et P102
    struct Family {
        std::string                         typeName;
        std::vector<std::string>            prefixes;
        std::vector<Operation::MemberSpec>  members;
    };
    std::vector<Family> families;
    std::set<std::size_t> inFamily;
    if (o.types && !o.onlyExisting) {
        std::map<std::string, std::vector<std::size_t>> byPrefix;
        std::vector<std::string> order;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].exists) continue;
            const auto cut = items[i].name.rfind('_');
            if (cut == std::string::npos || cut == 0 || cut + 1 >= items[i].name.size()) continue;
            const std::string prefix = items[i].name.substr(0, cut), member = items[i].name.substr(cut + 1);
            if (!hmi::isIdentifier(member) || hmi::isReservedWord(member) || !hmi::isIdentifier(prefix)) continue;
            if (!byPrefix.count(prefix)) order.push_back(prefix);
            byPrefix[prefix].push_back(i);
        }
        std::map<std::string, std::vector<std::string>> bySig;
        std::vector<std::string> sigOrder;
        for (const auto& prefix : order) {
            const auto& idx = byPrefix[prefix];
            if (idx.size() < 2) continue;
            if (project && project->variable(prefix)) continue;     // un nom deja pris
            if (seen.count(upperOf(prefix))) continue;              // une ligne porte deja ce nom
            std::vector<std::string> parts;
            for (const auto i : idx) parts.push_back(upperOf(items[i].name.substr(items[i].name.rfind('_') + 1)) + ":" + items[i].type);
            std::sort(parts.begin(), parts.end());
            if (std::adjacent_find(parts.begin(), parts.end()) != parts.end()) continue;
            std::string sig;
            for (const auto& x : parts) sig += x + "|";
            if (!bySig.count(sig)) sigOrder.push_back(sig);
            bySig[sig].push_back(prefix);
        }
        std::set<std::string> typeNames;
        for (const auto& sig : sigOrder) {
            const auto& prefixes = bySig[sig];
            if (prefixes.size() < 2) continue;
            Family fam;
            fam.prefixes = prefixes;
            // le nom du type : la partie commune des equipements (P101, P102 -> P), sans chiffres ni _ a la fin
            std::string stem = prefixes.front();
            for (const auto& x : prefixes) {
                std::size_t n = 0;
                while (n < stem.size() && n < x.size() && std::toupper(static_cast<unsigned char>(stem[n])) == std::toupper(static_cast<unsigned char>(x[n]))) ++n;
                stem.resize(n);
            }
            while (!stem.empty() && (std::isdigit(static_cast<unsigned char>(stem.back())) || stem.back() == '_')) stem.pop_back();
            if (stem.empty()) {
                for (const char c : prefixes.front()) {
                    if (!std::isalpha(static_cast<unsigned char>(c))) break;
                    stem += c;
                }
            }
            std::string name = "T_" + (stem.empty() ? std::string("Equipement") : stem);
            const std::string base = name;
            for (int n = 2; typeNames.count(upperOf(name)) || (project && project->hmiTypeByName(name)) || seen.count(upperOf(name)); ++n)
                name = base + "_" + std::to_string(n);
            typeNames.insert(upperOf(name));
            fam.typeName = name;
            families.push_back(std::move(fam));
            for (const auto& prefix : prefixes)
                for (const auto i : byPrefix[prefix]) inFamily.insert(i);
        }
        // les membres : ceux du premier equipement, dans l'ordre du fichier (leur designation)
        for (auto& fam : families)
            for (const auto i : byPrefix[fam.prefixes.front()]) {
                const Item& it = items[i];
                Operation::MemberSpec ms;
                ms.name = it.name.substr(it.name.rfind('_') + 1);
                ms.type = it.type;
                ms.description = it.desc;
                fam.members.push_back(std::move(ms));
            }
    }
    // les operations : les types, puis les variables (500 par operation), puis les adresses
    std::size_t nVars = 0, nAddr = 0;
    for (const auto& fam : families) {
        Operation op;
        op.action = Operation::Action::Type;
        op.name = fam.typeName;
        op.members = fam.members;
        p.ops.push_back(std::move(op));
    }
    std::vector<Operation::VariableSpec> specs;
    for (const auto& fam : families)
        for (const auto& prefix : fam.prefixes)
            specs.push_back({prefix, fam.typeName, "\xC3\x89quipement " + prefix + " (" + plural(fam.members.size(), "membre", "membres") + ", lu de " + f.name + ")", {}});
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (inFamily.count(i) || items[i].exists) continue;
        Item& it = items[i];
        if (addressable(it)) {
            it.addressed = true;
            // l'adresse posee sur l'equipement : la description garde la designation seule
            std::string d = it.desc;
            if (const auto at = d.find(" (" + it.address); at != std::string::npos) {
                d.erase(at, it.address.size() + 2);
                if (d.size() > at && d[at] == ',') d.replace(at, 2, " (");
                else if (d.size() > at && d[at] == ')') d.erase(at, 1);
            } else if (d == it.address) {
                d.clear();
            }
            if (!d.empty()) it.desc = d;
        }
        specs.push_back({it.name, it.type, it.desc, {}});
    }
    for (std::size_t from = 0; from < specs.size(); from += 500) {
        Operation op;
        op.action = Operation::Action::Variables;
        op.variables.assign(specs.begin() + static_cast<std::ptrdiff_t>(from),
                            specs.begin() + static_cast<std::ptrdiff_t>(std::min(specs.size(), from + 500)));
        p.ops.push_back(std::move(op));
    }
    nVars = specs.size();
    if (eq)
        for (std::size_t i = 0; i < items.size(); ++i) {
            const Item& it = items[i];
            if (!addressable(it)) continue;
            Operation op;
            op.action = Operation::Action::Address;
            if (inFamily.count(i)) {
                const auto cut = it.name.rfind('_');
                op.variable = it.name.substr(0, cut) + "." + it.name.substr(cut + 1);
            } else {
                op.variable = it.name;
            }
            op.props.emplace_back("equipement", eq->name);
            op.props.emplace_back("adresse", it.address);
            if (const auto ro = readOnlyOf(it.sens)) op.props.emplace_back("lecture_seule", *ro ? "oui" : "non");
            p.ops.push_back(std::move(op));
            ++nAddr;
        }
    // la reponse
    std::string& s = p.text;
    s = sourceLine(f, readings) + "\n";
    if (p.ops.empty()) {
        s += o.onlyExisting ? std::string("Aucune variable du fichier n'existe dans le projet avec une adresse \xC3\xA0 poser : rien \xC3\xA0 proposer.")
                            : std::string("Rien \xC3\xA0 proposer : aucune ligne ne donne une variable nouvelle.");
        if (!eqNote.empty() && o.onlyExisting) s += "\nAdresses : " + eqNote + ".";
    } else {
        std::vector<std::string> what;
        if (!families.empty()) {
            std::vector<std::string> fams;
            for (const auto& fam : families) {
                std::vector<std::string> mem;
                for (const auto& m : fam.members) mem.push_back(m.name);
                fams.push_back(fam.typeName + " (" + joinList(fam.prefixes, 3) + " : " + joinList(mem, 5) + ")");
            }
            what.push_back("**" + plural(families.size(), "type IHM", "types IHM") + "** " + joinList(fams, 3));
        }
        if (nVars) what.push_back("**" + plural(nVars, "variable IHM", "variables IHM") + "**");
        if (nAddr) what.push_back("**" + plural(nAddr, "adresse", "adresses") + "** " + (eqNote.empty() ? "sur " + guill(eq->name) : eqNote));
        s += "Je propose : " + joinList(what, 4) + ".\n";
        s += "Les noms viennent de la colonne " + guill(headerOf(L, "repere")) + ", le type " + (L.has("type") ? "de la colonne " + guill(headerOf(L, "type")) + " (DI/DO \xE2\x86\x92 BOOL, AI/AO \xE2\x86\x92 REAL) ou " : std::string{})
             + "de l'adresse (%M \xE2\x86\x92 BOOL, %MW \xE2\x86\x92 INT, %MD \xE2\x86\x92 DINT, %MF \xE2\x86\x92 REAL), la description de "
             + (L.has("designation") ? "la colonne " + guill(headerOf(L, "designation")) : std::string("la ligne"))
             + (g == Genre::IoList ? " (l'adresse d'E/S y est not\xC3\xA9" "e : une entr\xC3\xA9" "e ou une sortie ne se lit pas par Modbus)." : ".") + "\n";
        if (!families.empty())
            s += "Chaque famille d'\xC3\xA9quipements r\xC3\xA9p\xC3\xA9t\xC3\xA9" "e devient un **type IHM** (l'\xC3\xA9quivalent IHM d'un DDT) et une variable par \xC3\xA9quipement.\n";
        else if (o.types)
            s += "Aucune famille d'\xC3\xA9quipements ne se r\xC3\xA9p\xC3\xA8te (m\xC3\xAAmes suffixes, m\xC3\xAAmes types) : pas de type IHM, des variables simples.\n";
        if (!eqNote.empty() && !nAddr && memAddresses) s += "Adresses : " + eqNote + ".\n";
    }
    std::vector<std::string> aside;
    if (!noName.empty()) aside.push_back(plural(noName.size(), "ligne sans rep\xC3\xA8re", "lignes sans rep\xC3\xA8re") + " (" + joinList(noName, 5) + ")");
    if (!unreadable.empty()) aside.push_back(plural(unreadable.size(), "rep\xC3\xA8re illisible", "rep\xC3\xA8res illisibles") + " (" + joinList(unreadable, 4) + ")");
    if (!duplicates.empty()) aside.push_back(plural(duplicates.size(), "ligne en double", "lignes en double") + " (" + joinList(duplicates, 4) + ")");
    if (!noType.empty()) aside.push_back(plural(noType.size(), "ligne sans type ni adresse", "lignes sans type ni adresse") + " (" + joinList(noType, 4) + ")");
    if (!otherType.empty()) aside.push_back(plural(otherType.size(), "variable d\xC3\xA9j\xC3\xA0 dans le projet avec un autre type", "variables d\xC3\xA9j\xC3\xA0 dans le projet avec un autre type") + ", jamais remplac\xC3\xA9" "es (" + joinList(otherType, 3) + ")");
    if (!absent.empty()) aside.push_back(plural(absent.size(), "variable absente du projet", "variables absentes du projet") + " (" + joinList(absent, 5) + ") : cr\xC3\xA9" "e-les d'abord");
    if (capped) aside.push_back("seules les " + std::to_string(o.maxRows) + " premi\xC3\xA8res lignes sur " + std::to_string(total) + " sont propos\xC3\xA9" "es : redemande pour la suite");
    if (!aside.empty()) s += "Laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 : " + joinList(aside, 8) + ".\n";
    if (!existing.empty() && !o.onlyExisting) s += plural(existing.size(), "variable existe", "variables existent") + " d\xC3\xA9j\xC3\xA0 avec ce type : gard\xC3\xA9" "e(s) telle(s) quelle(s) (" + joinList(existing, 4) + ").\n";
    if (!adjusted.empty()) s += plural(adjusted.size(), "nom ajust\xC3\xA9", "noms ajust\xC3\xA9s") + " pour \xC3\xAAtre un identifiant (" + joinList(adjusted, 3) + ").\n";
    if (!p.ops.empty()) {
        s += "Rien n'est cr\xC3\xA9\xC3\xA9 avant **Appliquer** : v\xC3\xA9rifie l'aper\xC3\xA7u (un seul Ctrl+Z reprend tout).";
        if (!o.types && !o.onlyExisting) {
            // une famille possible : le dire
            std::map<std::string, std::size_t> pre;
            for (const auto& it : items)
                if (const auto cut = it.name.rfind('_'); cut != std::string::npos && cut > 0) ++pre[it.name.substr(0, cut)];
            std::size_t rep = 0;
            for (const auto& [k, n] : pre) rep += n >= 2 ? 1 : 0;
            if (rep >= 2) s += "\nDes \xC3\xA9quipements se r\xC3\xA9p\xC3\xA8tent (" + plural(rep, "pr\xC3\xA9" "fixe", "pr\xC3\xA9" "fixes") + " \xC3\xA0 plusieurs lignes) : demande `Cr\xC3\xA9" "e les types IHM du fichier joint` pour un type par famille et une variable par \xC3\xA9quipement.";
        }
        p.ok = true;
        p.summary = o.onlyExisting ? "Adresses de " + f.name + " (" + std::to_string(nAddr) + ")"
                                   : "Variables de " + f.name + " (" + std::to_string(nVars) + (families.empty() ? std::string{} : ", " + plural(families.size(), "type", "types")) + ")";
    }
    return p;
}

Plan planEquipments(const Attached& f, const std::vector<Reading>& readings, const hmi::Project* project) {
    Plan p;
    const Layout& L = f.layout;
    const bool many = readings.size() > 1;
    if (!L.has("ip")) {
        p.text = sourceLine(f, readings) + "\nJe ne trouve pas de colonne **adresse IP** dans ce fichier : je ne cr\xC3\xA9" "e pas d'\xC3\xA9quipement sans son adresse. "
                 "Choisis-la dans **Contexte de la discussion \xE2\x80\xBA " + f.name + " \xE2\x80\xBA Mod\xC3\xA8le\xE2\x80\xA6**, puis redemande.";
        return p;
    }
    std::vector<std::string> noName, noIp, badIp, duplicates, sameIp, existing, conflicts, unsupported, ethernet;
    std::map<std::string, std::string> seenName, seenIp;
    std::size_t made = 0;
    for (const auto& rd : readings)
        for (const auto& row : rd.rows) {
            const std::string where = lineWord(row.line, rd.sheet, many);
            std::string name = row.value("equipement");
            if (name.empty()) name = row.value("repere");
            if (name.empty()) name = row.value("designation");
            for (auto& c : name)
                if (c == '"' || c == '\'' || c == ';' || c == '|' || c == '$' || c == '\n' || c == '\t') c = '_';
            if (name.size() > 64) {
                std::size_t cut = 64;
                while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
                name.resize(cut);
            }
            if (name.empty()) {
                noName.push_back(where);
                continue;
            }
            std::string ip = row.value("ip");
            if (const auto slash = ip.find('/'); slash != std::string::npos) ip.resize(slash);
            while (!ip.empty() && std::isspace(static_cast<unsigned char>(ip.back()))) ip.pop_back();
            if (ip.empty()) {
                noIp.push_back(name + " (" + where + ")");
                continue;
            }
            std::uint32_t parsed = 0;
            if (!isIpv4(ip) || !hmi::equip::parseIpv4(ip, parsed)) {
                badIp.push_back(name + " : " + guill(ip) + " (" + where + ")");
                continue;
            }
            if (const auto it = seenName.find(upperOf(name)); it != seenName.end()) {
                duplicates.push_back(name + " (" + where + " ; d\xC3\xA9j\xC3\xA0 " + it->second + ")");
                continue;
            }
            seenName[upperOf(name)] = where;
            if (const auto it = seenIp.find(ip); it != seenIp.end()) sameIp.push_back(ip + " : " + it->second + " et " + name);
            else seenIp[ip] = name;
            // le protocole : Modbus TCP/IP (ou le port 502) ; Modbus RTU : pas pris en charge ; le reste : Ethernet TCP/IP
            const std::string proto = row.value("protocole");
            const std::string fp = ui::foldForSearch(proto);   // tel quel, replie (pas au singulier : << modbus >>)
            const std::string portText = row.value("port");
            const int port = allDigits(portText) && portText.size() <= 5 ? std::atoi(portText.c_str()) : -1;
            std::string type;
            if (fp.find("modbus") != std::string::npos) {
                if (fp.find("rtu") != std::string::npos || fp.find("serie") != std::string::npos || fp.find("serial") != std::string::npos
                    || fp.find("rs485") != std::string::npos || fp.find("rs232") != std::string::npos) {
                    unsupported.push_back(name + " (" + proto + ")");
                    continue;
                }
                type = "modbus_tcp";
            } else if (proto.empty() && port == 502) {
                type = "modbus_tcp";
            } else {
                type = "ethernet_tcp";
                ethernet.push_back(name + (proto.empty() ? std::string{} : " (" + proto + ")"));
            }
            if (project) {
                bool skip = false;
                for (const auto& e : project->equipments)
                    if (iequal(e.name, name)) {
                        if (e.host == ip) existing.push_back(name);
                        else conflicts.push_back(name + " (" + e.host + " dans le projet, " + ip + " dans le fichier)");
                        skip = true;
                    }
                if (skip) continue;
            }
            Operation op;
            op.action = Operation::Action::Equipment;
            op.name = name;
            op.props.emplace_back("type", type);
            op.props.emplace_back("adresse", ip);
            if (port > 0 && port <= 65535) op.props.emplace_back("port", std::to_string(port));
            if (const std::string u = row.value("esclave"); allDigits(u) && std::atoi(u.c_str()) <= 255 && type == "modbus_tcp") op.props.emplace_back("esclave", u);
            std::vector<std::string> notes;
            std::string d = row.value("designation");
            if (d == name) d.clear();
            if (d.empty()) d = row.value("commentaire");
            if (!row.value("masque").empty()) notes.push_back("masque " + row.value("masque"));
            if (!row.value("passerelle").empty()) notes.push_back("passerelle " + row.value("passerelle"));
            if (!row.value("vlan").empty()) notes.push_back("VLAN " + row.value("vlan"));
            if (!row.value("mac").empty()) notes.push_back("MAC " + row.value("mac"));
            if (!proto.empty()) notes.push_back(proto);
            std::string desc = d;
            if (!notes.empty()) desc += (desc.empty() ? "" : " \xE2\x80\x94 ") + joinList(notes, 6);
            if (desc.empty()) desc = "Lu de " + f.name + ", " + where;
            op.props.emplace_back("description", clip(desc, 200));
            p.ops.push_back(std::move(op));
            ++made;
        }
    std::string& s = p.text;
    s = sourceLine(f, readings) + "\n";
    if (made) s += "Je propose **" + plural(made, "\xC3\xA9quipement", "\xC3\xA9quipements") + "** (nom, adresse IP" + (L.has("port") ? ", port" : "") + (L.has("protocole") ? ", protocole" : "")
                   + " lus du fichier ; le masque, la passerelle, le VLAN et l'adresse MAC vont dans la description).\n";
    else s += "Rien \xC3\xA0 proposer : aucune ligne ne donne un \xC3\xA9quipement nouveau.\n";
    if (!ethernet.empty())
        s += plural(ethernet.size(), "\xC3\xA9quipement", "\xC3\xA9quipements") + " en **Ethernet TCP/IP** (surveill\xC3\xA9s, sans adresses Modbus) : " + joinList(ethernet, 4) + ".\n";
    std::vector<std::string> aside;
    if (!noName.empty()) aside.push_back(plural(noName.size(), "ligne sans nom", "lignes sans nom") + " (" + joinList(noName, 5) + ")");
    if (!noIp.empty()) aside.push_back(plural(noIp.size(), "ligne sans adresse IP", "lignes sans adresse IP") + " (" + joinList(noIp, 4) + ")");
    if (!badIp.empty()) aside.push_back(plural(badIp.size(), "adresse IP illisible", "adresses IP illisibles") + " (" + joinList(badIp, 3) + ")");
    if (!duplicates.empty()) aside.push_back(plural(duplicates.size(), "ligne en double", "lignes en double") + " (" + joinList(duplicates, 4) + ")");
    if (!unsupported.empty()) aside.push_back("Modbus RTU (s\xC3\xA9rie), pas encore pris en charge : " + joinList(unsupported, 4));
    if (!conflicts.empty()) aside.push_back(plural(conflicts.size(), "\xC3\xA9quipement d\xC3\xA9j\xC3\xA0 dans le projet avec une autre adresse", "\xC3\xA9quipements d\xC3\xA9j\xC3\xA0 dans le projet avec une autre adresse") + ", jamais remplac\xC3\xA9s (" + joinList(conflicts, 3) + ")");
    if (!aside.empty()) s += "Laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 : " + joinList(aside, 8) + ".\n";
    if (!existing.empty()) s += plural(existing.size(), "\xC3\xA9quipement existe", "\xC3\xA9quipements existent") + " d\xC3\xA9j\xC3\xA0 avec cette adresse : gard\xC3\xA9(s) (" + joinList(existing, 4) + ").\n";
    if (!sameIp.empty()) s += "Attention, la m\xC3\xAAme adresse IP pour deux \xC3\xA9quipements : " + joinList(sameIp, 3) + ".\n";
    if (made) {
        s += "Rien n'est cr\xC3\xA9\xC3\xA9 avant **Appliquer** ; aucune d\xC3\xA9tection ne part sur le r\xC3\xA9seau.";
        p.ok = true;
        p.summary = "\xC3\x89quipements de " + f.name + " (" + std::to_string(made) + ")";
    }
    return p;
}

std::string describeFile(const Attached& f, const std::vector<Reading>& readings) {
    std::string s = sourceLine(f, readings) + "\n";
    if (!readings.empty()) s += "En-t\xC3\xAA" "te ligne " + std::to_string(readings.front().headerLine) + " ; colonnes : " + mappingText(f.layout) + ".\n";
    // 1.21.1 : quelques lignes de CHAQUE feuille lue (3 par feuille, 12 en tout), puis les feuilles non lues
    std::size_t shown = 0;
    for (const auto& rd : readings) {
        std::size_t here = 0;
        for (const auto& row : rd.rows) {
            if (here >= (readings.size() > 1 ? 3u : 5u) || shown >= 12) break;
            std::string line;
            for (const auto& [role, v] : row.values)
                if (!v.empty()) line += (line.empty() ? "" : " \xC2\xB7 ") + v;
            s += "- " + (readings.size() > 1 ? guill(rd.sheet) + " " : std::string{}) + "ligne " + std::to_string(row.line) + " : " + clip(line, 160) + "\n";
            ++shown;
            ++here;
        }
    }
    std::vector<std::string> unread;
    for (const auto& si : f.sheets)
        if (!si.read && std::none_of(readings.begin(), readings.end(), [&](const Reading& rd) { return rd.sheet == si.name; }))
            unread.push_back(sheetWords(si, false));
    if (!unread.empty()) {
        s += "Les autres feuilles du classeur :\n";
        for (const auto& u : unread) s += "- " + u + "\n";
    }
    switch (f.layout.genre) {
        case Genre::IoList:
        case Genre::Exchange:
            s += "Je peux en faire une proposition : `Cr\xC3\xA9" "e les variables du fichier joint`, `Cr\xC3\xA9" "e les types IHM du fichier joint`"
                 + std::string(f.layout.genre == Genre::Exchange ? ", `Adresse les variables du fichier joint`" : "") + ".";
            break;
        case Genre::Network: s += "Je peux en faire une proposition : `Cr\xC3\xA9" "e les \xC3\xA9quipements du fichier r\xC3\xA9seau`."; break;
        case Genre::Other: s += "Ce classeur n'est ni une liste d'E/S, ni un fichier r\xC3\xA9seau, ni des reports : choisis sa correspondance dans **Mod\xC3\xA8le\xE2\x80\xA6** du contexte de la discussion."; break;
    }
    if (f.modelName.empty())
        s += "\nPour le relire sans analyse la prochaine fois : **Contexte de la discussion \xE2\x80\xBA " + f.name + " \xE2\x80\xBA Mod\xC3\xA8le\xE2\x80\xA6 \xE2\x80\xBA Enregistrer comme mod\xC3\xA8le**.";
    return s;
}

// -------------------------------------------------------------------------- l'IA connectee ---
std::string rowsToolJson() {
    return "{\"name\": \"lignes_classeur\", \"description\": \"Lit les lignes d'un classeur joint a la discussion (liste d'entrees/sorties, "
           "fichier reseau, reports %MW/%M) telles que le logiciel les a lues avec son modele de fichier ou sa detection : une ligne par "
           "entree, ses valeurs par role (repere, designation, type, adresse, ip...), le numero de la ligne du classeur entre crochets. "
           "Par pages : a_partir_de (1 : la premiere ligne lue) et combien (200 au plus). Une valeur absente est vide : n'invente jamais "
           "une ligne, un nom ou une adresse.\", \"input_schema\": {\"type\": \"object\", \"properties\": {"
           "\"fichier\": {\"type\": \"string\", \"description\": \"le nom du fichier joint (vide : le seul classeur joint)\"}, "
           "\"feuille\": {\"type\": \"string\", \"description\": \"le nom d'une feuille du classeur (vide : celle du modele) ; toutes ses "
           "feuilles sont listees dans les consignes, lues ou non ; une feuille sans en-tete reconnu est rendue case par case\"}, "
           "\"a_partir_de\": {\"type\": \"integer\", \"description\": \"la premiere ligne lue voulue (1 : la premiere)\"}, "
           "\"combien\": {\"type\": \"integer\", \"description\": \"le nombre de lignes (200 au plus)\"}}}}";
}

std::string runRowsTool(const std::vector<Attached>& files, std::string_view inputJson, bool* isError) {
    if (isError) *isError = true;
    json::Value in;
    std::string bad;
    if (!json::parse(inputJson.empty() ? std::string_view("{}") : inputJson, in, &bad) || !in.isObject()) return "Entr\xC3\xA9" "e illisible (" + bad + ").";
    if (files.empty()) return "Aucun classeur n'est joint \xC3\xA0 cette discussion.";
    const std::string want = in.str("fichier");
    const Attached* f = nullptr;
    if (want.empty()) {
        if (files.size() > 1) {
            std::vector<std::string> names;
            for (const auto& x : files) names.push_back(guill(x.name));
            return "Plusieurs classeurs sont joints : " + joinList(names, 10) + ". Donne \"fichier\".";
        }
        f = &files.front();
    } else {
        const std::string w = ui::foldForSearch(want);
        for (const auto& x : files)
            if (x.id == want || ui::foldForSearch(x.name) == w) f = &x;
        if (!f)
            for (const auto& x : files)
                if (ui::foldForSearch(x.name).find(w) != std::string::npos) f = &x;
        if (!f) {
            std::vector<std::string> names;
            for (const auto& x : files) names.push_back(guill(x.name));
            return "Aucun classeur joint ne s'appelle " + guill(want) + " ; ceux de la discussion : " + joinList(names, 10) + ".";
        }
    }
    std::shared_ptr<const Workbook> wb = f->workbook;
    if (!wb) {
        auto fresh = std::make_shared<Workbook>();
        std::string why;
        if (!readWorkbookFile(f->path, *fresh, &why)) return guill(f->name) + " : " + why + ".";
        wb = std::move(fresh);
    }
    Layout L = f->layout;
    bool structured = f->hasLayout;
    const std::string sheetWanted = in.str("feuille");
    const Sheet* sheet = wb->sheet(sheetWanted.empty() ? L.sheet : sheetWanted);
    if (!sheet && !wb->sheets.empty() && !structured) sheet = &wb->sheets.front();
    if (!sheet) {
        std::vector<std::string> names;
        for (const auto& s : wb->sheets) names.push_back(guill(s.name));
        return "Feuille " + guill(sheetWanted.empty() ? L.sheet : sheetWanted) + " absente de " + guill(f->name) + " ; ses feuilles : " + joinList(names, 30) + ".";
    }
    if (!structured || sheet->name != L.sheet) {
        // une autre feuille : sa detection, sinon ses cases telles quelles
        const SheetDetection d = detectSheet(*sheet);
        structured = d.headerFound;
        L = d.layout;
        L.sheet = sheet->name;
        if (structured && d.genre == Genre::Other) L.rules.keyRoles.clear();
    }
    const auto from = std::max<std::int64_t>(1, in.integer("a_partir_de", 1));
    const auto count = std::clamp<std::int64_t>(in.integer("combien", 100), 1, static_cast<std::int64_t>(kRowsPage));
    std::string out;
    if (structured) {
        const Reading rd = read(*wb, L);
        const std::size_t total = rd.rows.size();
        out = "Classeur " + guill(f->name) + " \xE2\x80\x94 feuille " + guill(rd.sheet) + " \xE2\x80\x94 " + genreLabel(L.genre)
              + (f->modelName.empty() || L.sheet != f->layout.sheet ? std::string{} : " (mod\xC3\xA8le " + guill(f->modelName) + ")") + ", en-t\xC3\xAA" "te ligne "
              + std::to_string(rd.headerLine) + ", " + plural(total, "ligne lue", "lignes lues")
              + (rd.ignored ? " (" + plural(rd.ignored, "ignor\xC3\xA9" "e", "ignor\xC3\xA9" "es") + " : sans cl\xC3\xA9)" : std::string{}) + ".\n";
        std::string cols;
        for (const auto& c : rd.columns) cols += (cols.empty() ? "" : " | ") + c.role;
        out += "Colonnes : " + cols + "\n";
        const auto first = static_cast<std::size_t>(from - 1);
        if (first >= total) {
            out += "Aucune ligne \xC3\xA0 partir de " + std::to_string(from) + " (il y en a " + std::to_string(total) + ").";
        } else {
            const std::size_t last = std::min(total, first + static_cast<std::size_t>(count));
            out += "Lignes " + std::to_string(first + 1) + " \xC3\xA0 " + std::to_string(last) + " :\n";
            for (std::size_t i = first; i < last; ++i) {
                const Row& r = rd.rows[i];
                out += "[" + std::to_string(r.line) + "]";
                for (std::size_t c = 0; c < r.values.size(); ++c) out += (c ? " | " : " ") + clip(r.values[c].second, 120);
                out += "\n";
            }
            if (last < total) out += "Suite : a_partir_de=" + std::to_string(last + 1) + ".";
            else out += "Fin du tableau.";
        }
    } else {
        // les cases telles quelles (aucun en-tete reconnu)
        std::vector<std::size_t> filled;
        for (std::size_t r = 0; r < sheet->rows.size(); ++r)
            if (std::any_of(sheet->rows[r].begin(), sheet->rows[r].end(), [](const std::string& c) { return !c.empty(); })) filled.push_back(r);
        out = "Classeur " + guill(f->name) + " \xE2\x80\x94 feuille " + guill(sheet->name) + " : aucun en-t\xC3\xAA" "te reconnu, les cases telles quelles ("
              + plural(filled.size(), "ligne non vide", "lignes non vides") + ").\n";
        const auto first = static_cast<std::size_t>(from - 1);
        const std::size_t last = std::min(filled.size(), first + static_cast<std::size_t>(count));
        for (std::size_t i = first; i < last; ++i) {
            out += "[" + std::to_string(filled[i] + 1) + "]";
            const auto& row = sheet->rows[filled[i]];
            for (std::size_t c = 0; c < row.size(); ++c) out += (c ? " | " : " ") + clip(row[c], 120);
            out += "\n";
        }
        if (last < filled.size()) out += "Suite : a_partir_de=" + std::to_string(last + 1) + ".";
    }
    if (isError) *isError = false;
    return out;
}

std::string workbooksPrompt(const std::vector<Attached>& files, bool tool) {
    std::string list;
    for (const auto& f : files) {
        if (!f.hasLayout) continue;
        list += "- " + guill(f.name) + " : " + genreLabel(f.layout.genre) + " \xE2\x80\x94 "
                + (!f.modelName.empty() ? "mod\xC3\xA8le de fichier " + guill(f.modelName) + (f.byUser ? " (choisi par l'utilisateur)" : " (reconnu)")
                   : f.byUser          ? std::string("correspondance choisie par l'utilisateur")
                                       : "d\xC3\xA9tect\xC3\xA9 (confiance " + percent(f.confidence) + ")")
                + " \xE2\x80\x94 feuille " + guill(f.layout.sheet) + ", en-t\xC3\xAA" "te ligne " + std::to_string(f.layout.headerRow + 1) + " \xE2\x80\x94 colonnes : "
                + mappingText(f.layout) + "\n";
        // 1.21.1 : TOUTES les feuilles du classeur (lues ou non) - l'IA ne conclut jamais sur une seule
        if (f.sheets.size() > 1) {
            list += "  " + plural(f.sheets.size(), "feuille", "feuilles") + " :\n";
            for (const auto& si : f.sheets) list += "  - " + sheetWords(si, tool) + "\n";
        }
    }
    if (list.empty()) return {};
    std::string s = "## Classeurs joints (lus par le logiciel)\n" + list;
    s += "Un classeur a souvent plusieurs feuilles (entr\xC3\xA9" "es TOR, sorties TOR, analogiques, r\xC3\xA9seau...) : la liste ci-dessus les donne "
         "toutes ; les feuilles du m\xC3\xAAme genre que la premi\xC3\xA8re sont lues avec elle" + std::string(tool ? ", une autre se lit avec l'outil (param\xC3\xA8tre feuille)" : "")
         + ". Ne d\xC3\xA9" "cris jamais le classeur entier d'apr\xC3\xA8s une seule feuille.\n";
    s += tool ? "Lis leurs lignes avec l'outil " + std::string(kRowsTool) + " (par pages, 200 au plus) plutot que leur texte brut, qui n'est pas envoy\xC3\xA9. "
              : std::string("Leurs lignes sont dans le texte extrait joint. ");
    s += "Pour agir, propose : creer_variables (le nom = le rep\xC3\xA8re tel quel, ou ajust\xC3\xA9 en identifiant et dit ; le type tir\xC3\xA9 de DI/DO/AI/AO, "
         "BOOL/INT/REAL ou de l'adresse ; la description = la d\xC3\xA9signation), creer_type quand des lignes se r\xC3\xA9p\xC3\xA8tent par \xC3\xA9quipement "
         "(P101_MARCHE, P101_DEFAUT, P102_MARCHE... : un type et une variable par \xC3\xA9quipement), creer_equipement depuis un fichier r\xC3\xA9seau "
         "(nom, adresse IP, port, protocole), adresser pour une adresse %MW / %M sur un \xC3\xA9quipement Modbus qui existe. Toujours une "
         "proposition que l'utilisateur v\xC3\xA9rifie ; n'invente jamais un nom, une adresse ou une ligne absents du fichier ; dis les lignes "
         "sans rep\xC3\xA8re, illisibles ou en double.\n";
    return s;
}

std::string compactBlock(const Attached& f, std::size_t sampleRows) {
    std::string s = "Pi\xC3\xA8" "ce jointe " + guill(f.name) + " (classeur lu par le logiciel) : " + genreLabel(f.layout.genre)
                    + (f.modelName.empty() ? std::string{} : " \xE2\x80\x94 mod\xC3\xA8le " + guill(f.modelName)) + " \xE2\x80\x94 feuille " + guill(f.layout.sheet)
                    + ", en-t\xC3\xAA" "te ligne " + std::to_string(f.layout.headerRow + 1) + ".\nColonnes : " + mappingText(f.layout) + ".\n";
    if (f.rows > 0) s += plural(f.rows, "ligne lue", "lignes lues") + " en tout (toutes les feuilles lues).\n";
    // 1.21.1 : toutes les feuilles du classeur
    if (!f.sheets.empty()) {
        s += plural(f.sheets.size(), "feuille", "feuilles") + " dans le classeur :\n";
        for (const auto& si : f.sheets) s += "- " + sheetWords(si, true) + "\n";
    } else if (f.workbook && f.workbook->sheets.size() > 1) {
        std::vector<std::string> names;
        for (const auto& sh : f.workbook->sheets) names.push_back(guill(sh.name));
        s += "Feuilles du classeur : " + joinList(names, 30) + ".\n";
    }
    if (f.workbook) {
        // un apercu seulement (le fil de l'interface) : les premieres lignes de la feuille du modele, puis quelques-unes
        // de chaque autre feuille lue (8 feuilles au plus)
        const auto sample = [&](const Reading& rd, std::size_t max) {
            std::size_t n = 0;
            for (const auto& r : rd.rows) {
                if (n++ >= max) break;
                s += "[" + std::to_string(r.line) + "]";
                for (std::size_t c = 0; c < r.values.size(); ++c) s += (c ? " | " : " ") + clip(r.values[c].second, 80);
                s += "\n";
            }
        };
        s += "Les premi\xC3\xA8res lignes de " + guill(f.layout.sheet) + " :\n";
        sample(read(*f.workbook, f.layout, sampleRows), sampleRows);
        const Sheet* main = f.workbook->sheet(f.layout.sheet);
        const auto sig = main ? headerCells(*main, f.layout.headerRow) : std::vector<std::string>{};
        std::size_t named = 0;
        for (const auto& x : sig) named += x.empty() ? 0 : 1;
        std::size_t shown = 0;
        for (std::size_t i = 0; i < f.workbook->sheets.size() && main && shown < 8; ++i) {
            const Sheet& sh = f.workbook->sheets[i];
            if (&sh == main) continue;
            if (!f.sheets.empty() && std::none_of(f.sheets.begin(), f.sheets.end(), [&](const SheetInfo& si) { return si.name == sh.name && si.read; })) continue;
            const auto L = sameKindLayout(sh, f.layout, sig, named, nullptr, i);
            if (!L) continue;
            const Reading rd = read(*f.workbook, *L, 5);
            if (rd.rows.empty()) continue;
            s += "Les premi\xC3\xA8res lignes de " + guill(sh.name) + " (en-t\xC3\xAA" "te ligne " + std::to_string(L->headerRow + 1) + ", colonnes : " + mappingText(*L) + ") :\n";
            sample(rd, 5);
            ++shown;
        }
    }
    s += "(Le texte brut du classeur n'est pas envoy\xC3\xA9 : toutes ses lignes, de toutes ses feuilles, se lisent avec l'outil " + std::string(kRowsTool) + ".)";
    return s;
}

} // namespace app::ai::sheets
