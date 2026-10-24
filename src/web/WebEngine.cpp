// =============================================================================
//  web/WebEngine.cpp - 1.12.31 : le moteur de l'IHM, pour la page web
// -----------------------------------------------------------------------------
//  LE VRAI MOTEUR (hmi::Runtime), celui de la simulation et du poste
//  d'exploitation, compile en WebAssembly et pilote par la page : elle charge le
//  projet (l'archive .xihm embarquee), demarre l'IHM, l'avance a chaque image, lit
//  et ecrit ses variables, lui donne les gestes de l'operateur, et recoit ce qu'il
//  faut dessiner. Les equipements (Modbus TCP) n'y sont pas relies : une page n'a
//  pas de socket (src/web/WebNetStub.cpp), leurs variables gardent leur valeur.
//
//  L'interface est une poignee de fonctions C (export_name) : la page passe et
//  recoit des octets par la memoire du module (xpg_malloc/xpg_free). Les chaines
//  rendues restent valides jusqu'au prochain appel qui les remplit (un tampon par
//  role).
// =============================================================================
#ifdef XPG_WEB

#include "WebEngine.hpp"

#include "../hmi/HmiRuntime.hpp"
#include "../hmi/HmiSimData.hpp"
#include "../hmi/HmiStore.hpp"
#include "../hmi/HmiModel.hpp"
#include "../project/ProjectArchive.hpp"
#include "../sim/Interpreter.hpp"
#include "../sim/ValueText.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace app::web {

Engine& engine() {
    static Engine e;
    return e;
}

namespace {

// La fonction de lecture pour parseProject : les fichiers de l'archive, sous ihm/.
hmi::FileReader readerFrom(const project::archive::Contents& c) {
    return [&c](const std::string& rel, std::string& content) {
        if (const auto* f = c.file("ihm/" + rel)) {
            content = *f;
            return true;
        }
        return false;
    };
}

const char* give(const std::string& s) {
    engine().scratch = s;
    return engine().scratch.c_str();
}

} // namespace

} // namespace app::web

using app::web::engine;

// =============================================================================
//  L'interface C de la page.
// =============================================================================
extern "C" {

XPG_WEB_EXPORT(xpg_malloc) void* xpg_malloc(std::size_t n) { return std::malloc(n); }
XPG_WEB_EXPORT(xpg_free)   void  xpg_free(void* p) { std::free(p); }

// La version du moteur.
XPG_WEB_EXPORT(xpg_version) const char* xpg_version() { return "1.12.31"; }

// La derniere erreur (UTF-8) ; vide : aucune.
XPG_WEB_EXPORT(xpg_last_error) const char* xpg_last_error() { return engine().lastError.c_str(); }

// Charger le projet depuis l'archive .xihm (les octets du zip). 1 : charge.
XPG_WEB_EXPORT(xpg_load) int xpg_load(const std::uint8_t* zip, int len) {
    auto& e = engine();
    e.lastError.clear();
    if (!zip || len <= 0) { e.lastError = "archive vide"; return 0; }
    project::archive::Bytes bytes(zip, zip + len);
    auto read = project::archive::read(bytes);
    if (!read) { e.lastError = "archive illisible : " + read.error().message(); return 0; }
    if (read->newer) { e.lastError = "archive d'une version plus recente"; return 0; }
    hmi::LoadReport report;
    auto parsed = hmi::parseProject(app::web::readerFrom(*read), &report);
    if (!parsed) { e.lastError = "projet illisible : " + parsed.error().message(); return 0; }
    e.project = std::move(*parsed);
    e.runtime = std::make_unique<hmi::Runtime>();
    hmi::Runtime::Hooks hooks;
    hooks.journaled = [&e](const hmi::JournalEntry& j) {
        e.journal.push_back(j.stamp + " [" + j.kind + "] " + j.source + " : " + j.message);
        if (e.journal.size() > 5000) e.journal.erase(e.journal.begin(), e.journal.begin() + 2500);
    };
    e.runtime->setHooks(std::move(hooks));
    e.runtime->bind(&e.project, nullptr);     // la simulation : pas d'automate du projet
    e.runtime->setHistory(&e.history);
    e.canvas.bind(e.runtime.get(), &e.project, &e.history);
    return 1;
}

// Demarrer / arreter / avancer l'IHM (le temps en secondes depuis l'ouverture).
XPG_WEB_EXPORT(xpg_start) void xpg_start(double now) {
    if (engine().runtime) engine().runtime->start(now);
}
XPG_WEB_EXPORT(xpg_stop) void xpg_stop(double now) {
    if (engine().runtime) engine().runtime->stop(now);
}
XPG_WEB_EXPORT(xpg_tick) void xpg_tick(double now) {
    if (engine().runtime) engine().runtime->tick(now);
}
XPG_WEB_EXPORT(xpg_running) int xpg_running() {
    return engine().runtime && engine().runtime->running() ? 1 : 0;
}

// LES VARIABLES REMANENTES (celles marquees Remanente), gardees par le navigateur comme le
// poste d'exploitation les garde sur son disque : xpg_retained les prend (un instantane
// hmi::simdata, vide : aucune), xpg_set_retained les rend au prochain xpg_start.
XPG_WEB_EXPORT(xpg_retained) const char* xpg_retained() {
    auto& e = engine();
    if (!e.runtime) return app::web::give("");
    hmi::simdata::Snapshot s;
    s.date = hmi::simdata::nowStamp();
    s.cells = e.runtime->captureData([](const hmi::Variable& v) { return v.retain; });
    return app::web::give(s.cells.empty() ? std::string{} : hmi::simdata::serialize(s));
}
XPG_WEB_EXPORT(xpg_set_retained) int xpg_set_retained(const char* text) {
    auto& e = engine();
    hmi::simdata::Snapshot s;
    if (!e.runtime || !text || !hmi::simdata::parse(text, s)) return 0;
    e.runtime->setStartData(std::move(s.cells), "Variables r\xC3\xA9manentes rendues (la m\xC3\xA9moire du navigateur)");
    return 1;
}

// La vue courante (son identifiant) ; 0 : aucune.
XPG_WEB_EXPORT(xpg_current_view) int xpg_current_view() {
    return engine().runtime ? static_cast<int>(engine().runtime->currentView()) : 0;
}

// Lire une variable (IHM ou equipement), rendue comme on la taperait (fixedText).
XPG_WEB_EXPORT(xpg_read) const char* xpg_read(const char* name) {
    auto& e = engine();
    if (!e.runtime || !name) return app::web::give("");
    sim::Value v;
    if (!e.runtime->environment().read(name, v)) return app::web::give("");
    return app::web::give(sim::fixedText(v));
}

// Ecrire une variable (comme une saisie de l'operateur : lue dans son type).
XPG_WEB_EXPORT(xpg_write) int xpg_write(const char* name, const char* text) {
    auto& e = engine();
    if (!e.runtime || !name || !text) return 0;
    sim::Value cur;
    sim::Type type = sim::Type::Unknown;
    if (e.runtime->environment().read(name, cur)) type = cur.type();
    sim::Value v;
    std::string why;
    if (!sim::readTyped(text, type, v, why)) { e.lastError = why; return 0; }
    return e.runtime->environment().write(name, v) ? 1 : 0;
}

// Appeler un script general par son nom (un bouton de la page, un essai).
XPG_WEB_EXPORT(xpg_call_script) int xpg_call_script(const char* name, double now) {
    auto& e = engine();
    if (!e.runtime || !name) return 0;
    std::string err;
    const bool ok = e.runtime->callScript(name, now, &err);
    if (!ok) e.lastError = err;
    return ok ? 1 : 0;
}

// Le journal depuis le debut, une ligne par entree (separees par \n).
XPG_WEB_EXPORT(xpg_journal) const char* xpg_journal() {
    std::string out;
    for (const auto& l : engine().journal) { out += l; out += '\n'; }
    return app::web::give(out);
}

// Combien de scripts detaches tournent (pour les essais du WAIT).
XPG_WEB_EXPORT(xpg_running_scripts) int xpg_running_scripts() {
    return engine().runtime ? static_cast<int>(engine().runtime->runningScripts().size()) : 0;
}

// ---- le dessin : la police, une image ---------------------------------------
// La police embarquee (un TTF) : celle des mesures du texte dans le wasm ; la page
// dessine avec la meme (une FontFace), la mise en page est donc la meme.
XPG_WEB_EXPORT(xpg_set_font) void xpg_set_font(const std::uint8_t* ttf, int len) {
    auto& e = engine();
    if (!ttf || len <= 0) return;
    e.font.assign(ttf, ttf + len);
    app::web::setEmbeddedFont(e.font.data(), e.font.size());
}

// Une image : l'IHM avance jusqu'a `now`, puis ses vues (et popups) se dessinent sur
// une surface w x h (pixels de l'ecran). Rend le tampon des commandes ; sa longueur
// par xpg_frame_len. Les images neuves : xpg_images / xpg_images_len, puis
// xpg_images_done quand la page les a prises.
XPG_WEB_EXPORT(xpg_frame) const std::uint8_t* xpg_frame(double now, float w, float h) {
    auto& e = engine();
    if (e.runtime && e.runtime->running()) e.runtime->tick(now);
    e.canvas.paint(e.renderer, w, h, now);
    return e.renderer.commands().data();
}
XPG_WEB_EXPORT(xpg_frame_len) int xpg_frame_len() { return static_cast<int>(engine().renderer.commands().size()); }
XPG_WEB_EXPORT(xpg_images) const std::uint8_t* xpg_images() { return engine().renderer.newImages().data(); }
XPG_WEB_EXPORT(xpg_images_len) int xpg_images_len() { return static_cast<int>(engine().renderer.newImages().size()); }
XPG_WEB_EXPORT(xpg_images_done) void xpg_images_done() { engine().renderer.clearNewImages(); }

// ---- les gestes de l'operateur (pixels de l'ecran) --------------------------
// type : 0 appui, 1 deplacement, 2 relachement. button : 0 gauche, 1 droit, 2 milieu.
XPG_WEB_EXPORT(xpg_mouse) void xpg_mouse(int type, float x, float y, int button, int clicks, double now) {
    auto& c = engine().canvas;
    if (type == 0) c.mouseDown(x, y, button, clicks, now);
    else if (type == 1) c.mouseMove(x, y, now);
    else c.mouseUp(x, y, button, now);
}
XPG_WEB_EXPORT(xpg_wheel) void xpg_wheel(double notches) { engine().canvas.wheel(notches); }
XPG_WEB_EXPORT(xpg_text) void xpg_text(const char* utf8, double now) {
    if (utf8) engine().canvas.text(utf8, now);
}
// Une touche d'edition (hmi::EditKey : effacer, valider, annuler, fleches...).
XPG_WEB_EXPORT(xpg_edit_key) void xpg_edit_key(int key, double now) { engine().canvas.editKey(key, now); }
// Une touche (son jeton : hmi/HmiKeys.hpp) ; mods : 1 Ctrl, 2 Maj, 4 Alt. Rend 1 si un
// raccourci de la vue l'a prise (la page ne la passe pas plus loin).
XPG_WEB_EXPORT(xpg_key) int xpg_key(const char* token, int mods, int down, int repeat, double now) {
    return engine().canvas.key(token ? token : "", (mods & 1) != 0, (mods & 2) != 0, (mods & 4) != 0, down != 0, repeat != 0, now) ? 1 : 0;
}

// ---- pour les essais : ou est un objet, ce qu'il y a sous un point ----------
XPG_WEB_EXPORT(xpg_object_at) const char* xpg_object_at(float x, float y) {
    return app::web::give(engine().canvas.objectNameAt(x, y));
}
// "x y" : le centre a l'ecran de l'objet `name` ; vide : pas dans les vues montrees.
XPG_WEB_EXPORT(xpg_object_center) const char* xpg_object_center(const char* name) {
    float x = 0, y = 0;
    if (!name || !engine().canvas.objectCenter(name, x, y)) return app::web::give("");
    return app::web::give(std::to_string(x) + " " + std::to_string(y));
}
// "x y l h" : sa boite a l'ecran ; vide : pas dans les vues montrees.
XPG_WEB_EXPORT(xpg_object_box) const char* xpg_object_box(const char* name) {
    float x = 0, y = 0, w = 0, h = 0;
    if (!name || !engine().canvas.objectBox(name, x, y, w, h)) return app::web::give("");
    return app::web::give(std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " + std::to_string(h));
}
// Un champ de saisie a-t-il le clavier (la page lui passe alors les caracteres) ?
XPG_WEB_EXPORT(xpg_focused) int xpg_focused() {
    auto& e = engine();
    return e.runtime && e.runtime->focusedObject() != hmi::kNoId ? 1 : 0;
}

// L'utilisateur connecte (son identifiant ; vide : personne).
XPG_WEB_EXPORT(xpg_user) const char* xpg_user() {
    auto& e = engine();
    return app::web::give(e.runtime ? e.runtime->userLogin() : std::string{});
}

// L'etat de chaque equipement, en JSON : [{"nom":..,"etat":..,"pourquoi":..,"connecte":0|1,"simule":0|1,"adresse":..}].
XPG_WEB_EXPORT(xpg_equipments_json) const char* xpg_equipments_json() {
    auto& e = engine();
    const auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
            else o += c;
        }
        return o;
    };
    std::string out = "[";
    if (e.runtime)
        for (const auto& st : e.runtime->equipmentStatuses()) {
            if (out.size() > 1) out += ',';
            out += "{\"nom\":\"" + esc(st.name) + "\",\"etat\":\"" + esc(st.state) + "\",\"pourquoi\":\"" + esc(st.why)
                 + "\",\"adresse\":\"" + esc(st.address) + "\",\"connecte\":" + (st.reachable ? "1" : "0")
                 + ",\"simule\":" + (st.simulated ? "1" : "0") + ",\"actif\":" + (st.enabled ? "1" : "0") + "}";
        }
    out += "]";
    return app::web::give(out);
}

// Le nom de la vue courante.
XPG_WEB_EXPORT(xpg_current_view_name) const char* xpg_current_view_name() {
    auto& e = engine();
    if (!e.runtime) return app::web::give("");
    const auto* v = e.project.view(e.runtime->currentView());
    return app::web::give(v ? v->name : std::string{});
}

} // extern "C"

#endif // XPG_WEB
