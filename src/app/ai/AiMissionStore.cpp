// =============================================================================
//  app/ai/AiMissionStore.cpp - 1.22.0 : les missions sur le disque, et leur reprise
// =============================================================================
#include "AiMissionStore.hpp"

#include "AiAttachments.hpp"
#include "AiPaths.hpp"

#include "../../core/AtomicFile.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiStore.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace app::ai::mission {

namespace {

namespace fs = std::filesystem;

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---- l'empreinte : les familles de l'index du projet (ihm.txt) ---------------------------------------------------
// Une ligne de l'index commence par son genre (variable, alarme, equipement...) : les lignes d'une meme famille ont
// une empreinte commune ("ihm.txt#variables"), pour dire << les variables (modifiees) >> plutot que << ihm.txt >>.
struct Family {
    std::string_view key;      // "variables"
    std::string_view label;    // "les variables"
    bool             feminine;
};
const std::vector<Family>& families() {
    static const std::vector<Family> f = {
        {"variables", "les variables", true},          {"types", "les types IHM", false},
        {"alarmes", "les alarmes", true},              {"equipements", "les \xC3\xA9quipements", false},
        {"scripts", "les scripts", false},             {"fonctions", "les fonctions", true},
        {"vues", "les vues", true},                    {"ressources", "les ressources", true},
        {"modeles", "les mod\xC3\xA8les de vues", false},     {"recettes", "les recettes", true},
        {"styles", "les styles", false},               {"securite", "les utilisateurs et les r\xC3\xB4les", false},
        {"bibliotheques", "les biblioth\xC3\xA8ques", true},  {"unites", "les unit\xC3\xA9s de mesure", true},
        {"langues", "les langues", true},              {"essais", "les essais", false},
        {"dossiers", "les dossiers", false},           {"reglages", "les r\xC3\xA9glages du projet", false},
    };
    return f;
}
std::string_view familyOfRecord(std::string_view kw) {
    static const std::map<std::string_view, std::string_view> m = {
        {"variable", "variables"},          {"dossier_variables", "variables"},  {"forcage", "variables"},
        {"memoire_jumeau", "variables"},    {"type_ihm", "types"},               {"membre", "types"},
        {"type_genre", "types"},            {"valeur_enum", "types"},            {"methode_type", "types"},
        {"generique", "types"},             {"alarme", "alarmes"},               {"alarmes_reglages", "alarmes"},
        {"groupe_alarmes", "alarmes"},      {"lien_groupe_alarmes", "alarmes"},  {"destinataire", "alarmes"},
        {"notifications", "alarmes"},       {"equipement", "equipements"},       {"liaison_equipement", "equipements"},
        {"communication", "equipements"},   {"adresse_modbus", "equipements"},   {"jeu_modbus", "equipements"},
        {"requete_modbus", "equipements"},  {"port_simule", "equipements"},      {"script", "scripts"},
        {"fonction", "fonctions"},          {"vue", "vues"},                     {"ressource", "ressources"},
        {"externe", "ressources"},          {"modele", "modeles"},               {"recette", "recettes"},
        {"jeu", "recettes"},                {"style", "styles"},                 {"style_prop", "styles"},
        {"role", "securite"},               {"groupe", "securite"},              {"utilisateur", "securite"},
        {"securite", "securite"},           {"bibliotheque", "bibliotheques"},   {"bib_type", "bibliotheques"},
        {"bib_fonction", "bibliotheques"},  {"bib_script", "bibliotheques"},     {"bib_unite", "bibliotheques"},
        {"bib_famille_unite", "bibliotheques"}, {"unite_mesure", "unites"},      {"famille_unite", "unites"},
        {"unites_strictes", "unites"},      {"langue", "langues"},               {"langues", "langues"},
        {"traduction", "langues"},          {"essai", "essais"},                 {"pas", "essais"},
        {"dossier_liste", "dossiers"},
    };
    const auto it = m.find(kw);
    return it == m.end() ? std::string_view("reglages") : it->second;
}

// Retirer d'une ligne un champ qui change sans que le projet change (la date d'enregistrement, le prochain numero).
void dropField(std::string& line, std::string_view key) {
    const std::string needle = " " + std::string(key) + "=";
    const auto at = line.find(needle);
    if (at == std::string::npos) return;
    std::size_t end = at + needle.size();
    if (end < line.size() && line[end] == '"') {
        ++end;
        while (end < line.size() && line[end] != '"') end += (line[end] == '\\') ? 2 : 1;
        if (end < line.size()) ++end;
    } else {
        while (end < line.size() && line[end] != ' ') ++end;
    }
    line.erase(at, end - at);
}

std::string bytesOf(const hmi::ProjectFile& f) {
    return f.data ? std::string(reinterpret_cast<const char*>(f.data->data()), f.data->size()) : std::string{};
}

// "vues/0003-Vue_Accueil.vue" -> "Vue_Accueil"
std::string nameOfFile(std::string_view path) {
    std::string n(path.substr(path.find('/') == std::string_view::npos ? 0 : path.find('/') + 1));
    if (const auto dot = n.rfind('.'); dot != std::string::npos && dot > 0) n.resize(dot);
    std::size_t i = 0;
    while (i < n.size() && n[i] >= '0' && n[i] <= '9') ++i;
    if (i > 0 && i < n.size() && n[i] == '-') n.erase(0, i + 1);
    return n;
}

struct FileKind {
    std::string_view dir, article, noun, family;
    bool             feminine;
};
const FileKind* kindOfFile(std::string_view path) {
    static const FileKind k[] = {
        {"vues/", "la", "vue", "vues", true},           {"scripts/", "le", "script", "scripts", false},
        {"fonctions/", "la", "fonction", "fonctions", true}, {"ressources/", "la", "ressource", "ressources", true},
        {"modeles/", "le", "mod\xC3\xA8le", "modeles", false},
    };
    for (const auto& x : k)
        if (path.substr(0, x.dir.size()) == x.dir) return &x;
    return nullptr;
}

std::string verdict(bool feminine, bool plural, std::string_view what) {
    std::string s(what);
    if (feminine) s += "e";
    if (plural) s += "s";
    return s;
}

std::string iso(std::string_view now) { return std::string(now); }

} // namespace

// ---- le disque ----------------------------------------------------------------------------------------------------
std::string missionsDir() { return root().empty() ? std::string{} : U(P(root()) / P("missions")); }

std::string missionFile(std::string_view id) { return U(P(missionsDir()) / P(std::string(id) + ".json")); }

bool save(const Mission& m, std::string* why) {
    if (m.format > kFormat) {
        if (why)
            *why = "La mission " + m.id + " vient d'une version plus r\xC3\xA9" "cente (format " + std::to_string(m.format)
                   + ") : elle n'est pas r\xC3\xA9\xC3\xA9" "crite par celle-ci.";
        return false;
    }
    if (root().empty()) return true;   // rien a ecrire : tout reste en memoire (les essais)
    if (m.id.empty()) {
        if (why) *why = "mission sans identifiant";
        return false;
    }
    std::error_code ec;
    fs::create_directories(P(missionsDir()), ec);
    core::AtomicWrite how;
    how.keepBackup = true;   // la version precedente en <id>.json.bak
    how.verify = true;
    if (auto r = core::writeFileAtomic(P(missionFile(m.id)), json::write(toJson(m)), how); !r) {
        if (why) *why = "\xC3\x89" "criture impossible : " + missionFile(m.id);
        return false;
    }
    return true;
}

namespace {
// Lire un fichier de mission ; faux et `why` : illisible ou pas une mission.
bool readOne(const fs::path& file, Mission& out, int* format, bool* readOnly, std::string* why) {
    std::string text;
    if (!core::readFileAll(file, text)) {
        if (why) *why = "illisible";
        return false;
    }
    json::Value v;
    std::string err;
    if (!json::parse(text, v, &err) || !v.isObject()) {
        if (why) *why = err.empty() ? std::string("pas un objet JSON") : err;
        return false;
    }
    out = fromJson(v, format, readOnly);
    if (out.id.empty()) out.id = U(file.stem());
    return true;
}
} // namespace

Loaded loadAll() {
    Loaded l;
    if (root().empty()) return l;
    std::error_code ec;
    const fs::path dir = P(missionsDir());
    if (!fs::is_directory(dir, ec)) return l;
    std::set<std::string> seen;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const fs::path file = e.path();
        if (file.extension() != ".json") continue;
        Mission m;
        int format = kFormat;
        bool readOnly = false;
        std::string why;
        if (!readOne(file, m, &format, &readOnly, &why)) {
            // Un arret brutal pendant un remplacement : la copie de la version precedente.
            fs::path bak = file;
            bak += ".bak";
            std::string why2;
            if (fs::exists(bak, ec) && readOne(bak, m, &format, &readOnly, &why2)) {
                l.problems.push_back(U(file.filename()) + " : illisible (" + why + "), repris de sa copie de sauvegarde");
                // La copie devient le fichier (le fichier abime est garde a cote, jamais efface).
                fs::path broken = file;
                broken += ".abime";
                fs::rename(file, broken, ec);
                if (!ec) {
                    std::string text;
                    if (core::readFileAll(bak, text)) {
                        core::AtomicWrite how;
                        how.keepBackup = false;
                        (void)core::writeFileAtomic(file, text, how);
                    }
                }
                ec.clear();
            } else {
                l.problems.push_back(U(file.filename()) + " : illisible (" + why + "), laiss\xC3\xA9 tel quel");
                continue;
            }
        }
        if (!seen.insert(m.id).second) continue;
        if (readOnly) l.readOnly.push_back(m.id);
        l.missions.push_back(std::move(m));
    }
    // Un .bak seul (le fichier principal a disparu pendant un remplacement) : repris aussi.
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = U(e.path().filename());
        if (name.size() < 9 || name.substr(name.size() - 9) != ".json.bak") continue;
        const std::string id = name.substr(0, name.size() - 9);
        if (seen.count(id)) continue;
        fs::path main = e.path().parent_path() / P(id + ".json");
        if (fs::exists(main, ec)) continue;
        Mission m;
        int format = kFormat;
        bool readOnly = false;
        std::string why;
        if (!readOne(e.path(), m, &format, &readOnly, &why)) continue;
        l.problems.push_back(id + ".json : absent, repris de sa copie de sauvegarde");
        seen.insert(m.id);
        if (readOnly) l.readOnly.push_back(m.id);
        l.missions.push_back(std::move(m));
    }
    std::stable_sort(l.missions.begin(), l.missions.end(), [](const Mission& a, const Mission& b) { return a.modified > b.modified; });
    return l;
}

std::optional<Mission> load(std::string_view id, std::string* why) {
    if (root().empty()) {
        if (why) *why = "pas de dossier IA";
        return std::nullopt;
    }
    Mission m;
    std::string w;
    if (readOne(P(missionFile(id)), m, nullptr, nullptr, &w)) return m;
    fs::path bak = P(missionFile(id));
    bak += ".bak";
    std::string w2;
    if (readOne(bak, m, nullptr, nullptr, &w2)) return m;
    if (why) *why = missionFile(id) + " : " + w;
    return std::nullopt;
}

bool remove(std::string_view id, std::string* why) {
    if (root().empty()) return true;
    std::error_code ec;
    const fs::path file = P(missionFile(id));
    fs::path bak = file;
    bak += ".bak";
    const bool had = fs::exists(file, ec);
    fs::remove(file, ec);
    if (ec) {
        if (why) *why = "Suppression impossible : " + U(file) + " (" + ec.message() + ")";
        return false;
    }
    fs::remove(bak, ec);
    if (!had && why) *why = "La mission " + std::string(id) + " n'avait pas de fichier.";
    return true;
}

// ---- l'empreinte du projet ------------------------------------------------------------------------------------------
ProjectPrint printOf(const hmi::Project& project) {
    ProjectPrint out;
    std::map<std::string, std::string> parts;   // cle -> contenu
    for (const auto& f : hmi::serializeProject(project)) {
        if (f.path == "ihm.txt") {
            std::map<std::string, std::string> groups;
            const std::string text = bytesOf(f);
            std::size_t at = 0;
            while (at < text.size()) {
                std::size_t end = text.find('\n', at);
                if (end == std::string::npos) end = text.size();
                std::string line = text.substr(at, end - at);
                at = end + 1;
                if (line.empty() || line[0] == '#') continue;
                const std::string kw = line.substr(0, line.find(' '));
                if (kw == "ihm" || kw == "fin") continue;   // le format, le prochain numero : pas le contenu
                if (kw == "config") dropField(line, "modifie");   // la date d'enregistrement
                groups[std::string(familyOfRecord(kw))] += line + "\n";
            }
            for (auto& [k, v] : groups) parts["ihm.txt#" + k] = std::move(v);
            continue;
        }
        parts[f.path] = bytesOf(f);
    }
    std::string all;
    for (const auto& [k, v] : parts) {
        std::string h = attach::sha256Hex(v);
        all += k + "=" + h + "\n";
        out.files.emplace_back(k, std::move(h));
    }
    out.all = attach::sha256Hex(all);
    return out;
}

std::vector<std::string> changesBetween(const std::vector<std::pair<std::string, std::string>>& before,
                                        const std::vector<std::pair<std::string, std::string>>& after) {
    std::map<std::string, std::string> a(before.begin(), before.end()), b(after.begin(), after.end());
    // 0 : modifie, 1 : ajoute, 2 : supprime
    std::vector<std::pair<std::string, int>> changed;
    for (const auto& [k, h] : b) {
        const auto it = a.find(k);
        if (it == a.end()) changed.emplace_back(k, 1);
        else if (it->second != h) changed.emplace_back(k, 0);
    }
    for (const auto& [k, h] : a)
        if (!b.count(k)) changed.emplace_back(k, 2);
    std::sort(changed.begin(), changed.end());
    // Les familles dont un fichier a change : leur ligne d'index ne se dit pas en plus (<< la vue X (ajoutee) >> suffit).
    std::set<std::string> fileFamilies;
    for (const auto& [k, how] : changed)
        if (const FileKind* fk = kindOfFile(k)) fileFamilies.insert(std::string(fk->family));
    static const char* const words[] = {"modifi\xC3\xA9", "ajout\xC3\xA9", "supprim\xC3\xA9"};
    std::vector<std::string> out;
    std::vector<std::string> groups;
    for (const auto& [k, how] : changed) {
        if (const FileKind* fk = kindOfFile(k)) {
            out.push_back(std::string(fk->article) + " " + std::string(fk->noun) + " " + nameOfFile(k) + " ("
                          + verdict(fk->feminine, false, words[how]) + ")");
            continue;
        }
        if (k.rfind("ihm.txt#", 0) == 0) {
            const std::string fam = k.substr(8);
            if (fileFamilies.count(fam)) continue;
            for (const auto& f : families())
                if (f.key == fam) {
                    groups.push_back(std::string(f.label) + " (" + verdict(f.feminine, true, words[how]) + ")");
                    break;
                }
            continue;
        }
        out.push_back("le fichier " + k + " (" + std::string(words[how]) + ")");
    }
    // Les familles d'abord (les variables, les alarmes...), puis les fichiers.
    groups.insert(groups.end(), out.begin(), out.end());
    return groups;
}

void stamp(Mission& m, const ProjectPrint& print, std::string now) {
    m.project.print = print.all;
    m.project.files = print.files;
    m.project.printed = iso(now);
}

// ---- la reprise ---------------------------------------------------------------------------------------------------------
namespace {
std::string listText(const std::vector<std::string>& v, std::size_t max) {
    std::string s;
    for (std::size_t i = 0; i < v.size() && i < max; ++i) s += (i ? ", " : "") + v[i];
    if (v.size() > max) s += " (+" + std::to_string(v.size() - max) + ")";
    return s;
}
} // namespace

Recovery inspect(const Mission& m, std::string_view openProjectId, const hmi::Project* project) {
    Recovery r;
    r.interrupted = m.pending.active;
    // Le meme projet : la meme identite. Une mission lancee sur un projet jamais enregistre (sans identite) suit le
    // projet ouvert (recover lui donne son identite des qu'il en a une) ; une mission d'un projet enregistre ne
    // reconnait pas un projet sans identite.
    const bool sameProject = project && (m.project.id.empty() || (!openProjectId.empty() && m.project.id == openProjectId));
    if (!sameProject) {
        r.coherence = Coherence::Missing;
        r.text = project ? "Le projet ouvert n'est pas celui de la mission (" + (m.project.name.empty() ? std::string("?") : m.project.name) + ")."
                         : "Aucun projet ouvert : la mission travaille sur " + (m.project.name.empty() ? std::string("un projet inconnu") : m.project.name) + ".";
        if (!m.project.path.empty()) r.text += " Son dossier : " + m.project.path + ".";
        r.nextAction = "Ouvrir le projet de la mission, puis la reprendre.";
    } else if (m.project.print.empty()) {
        r.coherence = Coherence::Unknown;
        r.text = "L'\xC3\xA9tat du projet n'avait pas \xC3\xA9t\xC3\xA9 enregistr\xC3\xA9 avec la mission : il sera v\xC3\xA9rifi\xC3\xA9 \xC3\xA0 la prochaine proposition.";
    } else {
        const ProjectPrint now = printOf(*project);
        if (now.all == m.project.print) {
            r.coherence = Coherence::Same;
            r.text = "Le projet est identique \xC3\xA0 la derni\xC3\xA8re sauvegarde de la mission.";
        } else {
            r.coherence = Coherence::Changed;
            r.changes = changesBetween(m.project.files, now.files);
            r.text = "Le projet a chang\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde de la mission"
                     + (r.changes.empty() ? std::string(".") : " : " + listText(r.changes, 8) + ".");
            r.nextAction = "Actualiser le contexte de la mission (ce qui a chang\xC3\xA9 est relu avant toute nouvelle proposition).";
        }
    }
    if (r.interrupted)
        r.text += " Un appel \xC3\xA0 l'IA (" + (m.pending.purpose.empty() ? std::string("proposition") : m.pending.purpose)
                  + ") \xC3\xA9tait en cours \xC3\xA0 la fermeture : il est marqu\xC3\xA9 interrompu, rien n'est rejou\xC3\xA9.";
    if (r.nextAction.empty()) r.nextAction = nextAction(m);
    return r;
}

bool recover(Mission& m, std::string_view openProjectId, const hmi::Project* project, std::string now, Recovery* out) {
    Recovery r = inspect(m, openProjectId, project);
    bool changed = false;
    if (r.coherence != Coherence::Missing && m.project.id.empty() && !openProjectId.empty()) {
        m.project.id = std::string(openProjectId);   // le projet a ete enregistre depuis : son identite
        changed = true;
    }
    if (m.pending.active) {
        const std::string purpose = m.pending.purpose.empty() ? std::string("proposition") : m.pending.purpose;
        // 1.22.1 : les etapes visees sont nommees (le journal de chaque etape le montre)
        log(m, "interruption",
            "Appel \xC3\xA0 l'IA interrompu (" + purpose + (m.pending.steps.empty() ? std::string{} : " pour " + listText(m.pending.steps, 12))
                + ", commenc\xC3\xA9 le " + m.pending.started + ") : sa r\xC3\xA9ponse n'existe pas, rien n'est rejou\xC3\xA9.",
            now);
        for (const auto& id : m.pending.steps)
            if (Step* s = m.step(id); s && s->state == StepState::Running) {
                s->state = StepState::Ready;
                s->note = "L'appel \xC3\xA0 l'IA a \xC3\xA9t\xC3\xA9 interrompu : \xC3\xA0 relancer.";
                s->updated = now;
            }
        m.pending = {};
        changed = true;
    }
    for (auto& s : m.steps)
        if (s.state == StepState::Running) {   // une etape en cours sans appel : jamais laissee en cours
            s.state = StepState::Ready;
            s.note = "Reprise apr\xC3\xA8s la fermeture : \xC3\xA0 relancer.";
            s.updated = now;
            changed = true;
        }
    if (m.coherence != r.coherence || m.coherenceText != r.text) {
        m.coherence = r.coherence;
        m.coherenceText = r.text;
        changed = true;
        if (r.coherence == Coherence::Changed)
            log(m, "actualisation", "Projet modifi\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde : " + listText(r.changes, 8) + ".", now);
        else if (r.coherence == Coherence::Missing)
            log(m, "actualisation", r.text, now);
    }
    // Le projet du moment est la source de verite : ce que les propositions appliquees ont cree y est-il encore ?
    if (project && r.coherence != Coherence::Missing) changed = verifyAgainst(m, *project, now) || changed;
    if (changed) {
        refreshReadiness(m);
        refreshState(m, now);
        m.resumePoint = "Reprise du " + now;
    }
    if (out) *out = std::move(r);
    return changed;
}

} // namespace app::ai::mission
