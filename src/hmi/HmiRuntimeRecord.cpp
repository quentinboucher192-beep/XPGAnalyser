// =============================================================================
//  hmi/HmiRuntimeRecord.cpp - 1.14.0 (M4) : enregistrer et rejouer l'IHM qui tourne
// -----------------------------------------------------------------------------
//  ENREGISTRER (voir HmiRecording.hpp). A chaque prise (Configuration >
//  Enregistrements : 10 par seconde, au moins le cycle de l'IHM) : toutes les
//  variables IHM, et ce que l'ecran a lu depuis le debut (variables de
//  l'automate, variables systeme, proprietes d'objets, constantes : Env::read le
//  note) ; l'ecran (la vue, les popups, la langue, le theme, les proprietes
//  ecrites en marche) ; les alarmes ; le pointeur. Les clics, les appuis longs,
//  les saisies validees, les sons et le journal s'ajoutent quand ils arrivent.
//
//  REJOUER. Le moteur est MIS DE COTE : tick() ne fait plus rien (ni scripts, ni
//  actions, ni alarmes, ni equipements : les liaisons ne suivent plus rien), les
//  gestes sont ignores. Ce qu'il montrait est mis de cote aussi (la vue, ses
//  parametres, les popups, les alarmes, la langue, le theme, les proprietes
//  ecrites, le pointeur) ; la vue et les popups suivent alors la piste, et
//  Env::read lit les valeurs de la piste avant tout le reste. Quitter rend ce qui
//  avait ete mis de cote tel quel : les variables, elles, n'ont pas bouge.
//  En simulation, l'automate simule est mis en pause pendant la relecture (par
//  l'ecran : HmiSimulation.cpp) ; au poste, l'automate reel continue : l'IHM
//  reprend ses vraies valeurs en quittant.
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiActionKinds.hpp"
#include "HmiCrypto.hpp"
#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"
#include "HmiNetInfo.hpp"
#include "HmiNotify.hpp"   // unmaskSecret : le mot de passe d'un Enregistreur, d'une action
#include "../core/Version.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <set>

namespace hmi {

namespace fs = std::filesystem;

namespace {

std::string upperOf(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}
std::string trimOf(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool sameText(std::string_view a, std::string_view b) { return upperOf(a) == upperOf(b); }
bool fileExists(const std::string& utf8) {
    std::error_code ec;
    return fs::exists(fs::path(std::u8string(utf8.begin(), utf8.end())), ec);
}
double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// La vitesse la plus proche de 0,25 0,5 1 2 4 8.
double snapSpeed(double s) {
    if (!(s > 0)) return 1.0;
    double best = 1.0, gap = 1e9;
    for (const double v : rec::kSpeeds)
        if (std::fabs(std::log2(v) - std::log2(s)) < gap) {
            gap = std::fabs(std::log2(v) - std::log2(s));
            best = v;
        }
    return best;
}
// La racine d'un chemin de variable ("FOUR1.VANNES[2].POS" -> "FOUR1").
std::string rootOf(const std::string& key) {
    const auto cut = key.find_first_of(".[");
    return cut == std::string::npos ? key : key.substr(0, cut);
}
// L'empreinte d'un projet : ses variables IHM (nom, type) et ses vues (nom, taille) - un projet change depuis
// l'enregistrement se voit a la relecture.
std::string fingerprint(const Project& p) {
    std::string s = p.config.name + "|";
    for (const auto& v : p.programs.variables) s += v.name + ":" + v.type + ";";
    s += "|";
    for (const auto& v : p.views) s += v.name + ":" + std::to_string(v.width) + "x" + std::to_string(v.height) + ";";
    return toHex(sha256(s)).substr(0, 16);
}

} // namespace

// ============================================================================ l'etat ==
struct Runtime::RecordState {
    // ---- enregistrer
    std::unique_ptr<rec::Recorder>                              recorder;
    std::string                                                 name, description, password, source, folder;
    bool                                                        askAtStop{false};
    bool                                                        stopping{false};   // arrete : la question de l'arret attend
    double                                                      startAt{0}, stopAt{0}, maxSeconds{0}, lastSample{-1e9};
    std::map<std::string, std::pair<rec::KeyKind, std::string>> reads;             // ce que l'ecran a lu (hors variables IHM)
    std::map<std::string, std::pair<Id, std::string>>           variables;         // racine -> (identifiant, type)
    // ---- rejouer
    std::shared_ptr<const rec::Track>                           track;
    std::unique_ptr<rec::Player>                                player;
    rec::Header                                                 header;
    std::string                                                 path, note, source2;
    double                                                      speed{1}, lastWall{-1};
    bool                                                        paused{false}, ended{false};
    const rec::Screen*                                          screen{nullptr};
    const rec::Overrides*                                       overrides{nullptr};
    const rec::Alarms*                                          alarms{nullptr};
    bool                                                        overridesApplied{false}, alarmsApplied{false};
    struct Live {
        Id                                   current{kNoId};
        std::string                          currentArgs;
        std::shared_ptr<const Scope>         currentScope;
        std::vector<PopupSlot>               slots;
        std::vector<LiveAlarm>               alarms;
        std::string                          language, theme;
        std::map<std::pair<Id, Id>, std::map<std::string, std::string>> overrides;
        Id                                   pressed{kNoId}, focused{kNoId};
    } live;
    // ---- la liste du dossier
    mutable std::vector<rec::Item>                              items;
    mutable double                                              itemsAt{-1e9};
    mutable std::string                                         itemsFolder;
    std::map<Id, int>                                           playerRows;
};

void Runtime::RecordStateDelete::operator()(RecordState* s) const noexcept { delete s; }

Runtime::RecordState& Runtime::recState() const {
    auto& self = const_cast<Runtime&>(*this);
    if (!self.rec_) self.rec_.reset(new RecordState);
    return *self.rec_;
}

std::string Runtime::recordingsFolder() const {
    if (!project_) return {};
    const std::string pf = hooks_.projectFolder ? hooks_.projectFolder() : std::string{};
    return rec::folderOf(project_->recording, pf);
}

bool Runtime::recording() const noexcept { return recordOn_; }

RecordingInfo Runtime::recordingInfo() const {
    RecordingInfo i;
    if (!rec_ || !rec_->recorder) return i;
    const auto& s = *rec_;
    i.active = recordOn_;
    i.stopping = s.stopping;
    i.name = s.name;
    i.seconds = s.stopping ? s.stopAt - s.startAt : std::max(0.0, now_ - s.startAt);
    i.frames = s.recorder->frames();
    i.events = s.recorder->eventCount();
    i.clicks = s.recorder->clicks();
    i.alarms = s.recorder->alarmEvents();
    i.keys = s.recorder->keyCount();
    i.bytes = s.recorder->bytes();
    i.maxSeconds = s.maxSeconds;
    return i;
}

// ======================================================================== enregistrer ==
bool Runtime::startRecording(const RecordingStart& req, double now, std::string* why) {
    const auto no = [why](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!project_ || !running_) return no("l'IHM ne tourne pas");
    if (replayOn_) return no("une relecture est affich\xC3\xA9" "e : quitte-la d'abord");
    auto& s = recState();
    if (recordOn_ || s.stopping) return no("un enregistrement tourne d\xC3\xA9j\xC3\xA0 (" + s.name + ")");
    const std::string folder = recordingsFolder();
    if (folder.empty()) return no("pas de dossier des enregistrements : enregistre d'abord le projet");
    now_ = std::max(now_, now);
    std::string name = trimOf(req.name);
    if (name.empty()) {
        std::string d = dateStampOf(now_).substr(0, 19);
        for (auto& c : d)
            if (c == ':') c = '-';
        name = "Enregistrement " + d;
    }
    name = rec::cleanName(name);
    // Le fichier en cours : un nom libre (ni un .xrec, ni un .part du meme nom).
    std::string partName = name;
    for (int n = 2; n < 10000 && (fileExists(rec::pathOf(folder, partName)) || fileExists(rec::pathOf(folder, partName) + std::string(rec::kPartSuffix)));
         ++n)
        partName = name + " (" + std::to_string(n) + ")";
    rec::RecorderOptions o;
    o.partPath = rec::pathOf(folder, partName) + std::string(rec::kPartSuffix);
    o.header.application = std::string("IHM Studio ") + XPG_ANALYZER_VERSION;
    o.header.name = partName;
    o.header.description = req.description;
    o.header.date = dateStampOf(now_).substr(0, 19);
    o.header.project = project_->config.name;
    o.header.projectVersion = project_->config.version;
    o.header.fingerprint = fingerprint(*project_);
    if (const View* v = viewOf(current_)) o.header.startView = v->name;
    const bool station = hooks_.station && hooks_.station().first;
    o.header.where = station ? netinfo::computerName() : std::string("Simulation");
    if (const User* u = user()) o.header.by = u->fullName.empty() ? u->login : u->fullName;
    else o.header.by = user_;
    o.header.rateHz = std::clamp(project_->recording.rateHz, 1, 50);
    o.password = req.password;
    auto recorder = std::make_unique<rec::Recorder>();
    std::string err;
    if (!recorder->open(std::move(o), &err)) return no(err);
    s = RecordState{};
    s.recorder = std::move(recorder);
    s.name = partName;
    s.description = req.description;
    s.password = req.password;
    s.source = req.source;
    s.folder = folder;
    s.askAtStop = req.askAtStop;
    s.startAt = now_;
    s.maxSeconds = req.maxSeconds > 0 ? req.maxSeconds : std::max(1, project_->recording.maxMinutes) * 60.0;
    for (const auto& v : project_->programs.variables) s.variables[upperOf(v.name)] = {v.id, v.type};
    recordOn_ = true;
    log("Enregistrement", req.source.empty() ? std::string("IHM") : req.source,
        "d\xC3\xA9" "but : " + partName + (req.password.empty() ? std::string{} : std::string(" (prot\xC3\xA9g\xC3\xA9 par un mot de passe)")));
    recordEvent(rec::EventKind::Note, "d\xC3\xA9" "but", req.source);
    recordSample(now_);
    return true;
}

void Runtime::noteRead(const std::string& upperKey, std::string_view shown, rec::KeyKind kind) {
    if (!recordOn_ || !rec_) return;
    if (upperKey.rfind("SYS.RECORDING", 0) == 0 || upperKey.rfind("SYS.REPLAY", 0) == 0) return;
    rec_->reads.emplace(upperKey, std::make_pair(kind, std::string(shown)));
}

void Runtime::recordTick(double now) {
    if (!recordOn_ || !rec_ || !rec_->recorder) return;
    auto& s = *rec_;
    const double elapsed = now - s.startAt;
    if (s.maxSeconds > 0 && elapsed >= s.maxSeconds) {
        log("Enregistrement", s.source.empty() ? std::string("IHM") : s.source,
            s.name + " : dur\xC3\xA9" "e maximale atteinte (" + rec::clock(s.maxSeconds) + ") - il s'arr\xC3\xAAte et se garde");
        (void)stopRecording(true, now);
        return;
    }
    const double rate = std::clamp(project_ ? project_->recording.rateHz : 10, 1, 50);
    const double cycle = project_ ? std::max(10, project_->config.cycleMs) / 1000.0 : 0.1;
    const double period = std::min(1.0 / rate, cycle);
    if (now - s.lastSample >= period - 1e-6) recordSample(now);
    (void)s.recorder->flush(now - s.startAt);
}

void Runtime::recordSample(double now) {
    if (!recordOn_ || !rec_ || !rec_->recorder || !project_) return;
    auto& s = *rec_;
    auto& r = *s.recorder;
    s.lastSample = now;
    const double t = std::max(0.0, now - s.startAt);
    // ---- les valeurs : les variables IHM, puis ce que l'ecran a lu
    r.beginSample(t);
    forEachIhmValue([&](const std::string& key, const sim::Value& v) {
        const auto it = s.variables.find(rootOf(key));
        r.value(key, v, rec::KeyKind::Variable, key, it == s.variables.end() ? kNoId : it->second.first,
                it == s.variables.end() ? std::string_view{} : std::string_view(it->second.second));
    });
    const auto reads = s.reads;   // une lecture peut en noter une autre
    for (const auto& [key, info] : reads) {
        sim::Value v;
        if (environment().read(info.second, v)) r.value(key, v, info.first, info.second);
    }
    r.endSample();
    // ---- l'ecran
    rec::Screen sc;
    sc.t = t;
    if (const View* v = viewOf(current_)) sc.view = v->name;
    sc.arguments = currentArgs_;
    if (project_->recording.popups)
        for (const auto& sl : slots_) {
            const View* pv = viewOf(sl.view);
            if (!pv) continue;
            sc.popups.push_back(rec::PopupState{pv->name, sl.arguments, sl.x, sl.y});
        }
    sc.language = language_;
    sc.theme = settings_.theme;
    r.screen(std::move(sc));
    // ---- les proprietes ecrites en marche (variables d'instances)
    rec::Overrides ov;
    ov.t = t;
    for (const auto& [ids, keys] : overrides_) {
        const View* v = project_->view(ids.first);
        const Object* o = v ? v->object(ids.second) : nullptr;
        if (!o) {
            v = viewOf(ids.first);
            o = v ? v->object(ids.second) : nullptr;
        }
        if (!v || !o) continue;
        for (const auto& [k, value] : keys) ov.list.push_back(rec::Override{v->name, o->name, k, value});
    }
    r.overrides(std::move(ov));
    // ---- les alarmes
    rec::Alarms al;
    al.t = t;
    for (const auto& a : alarms_)
        al.list.push_back(rec::Alarm{a.name, a.message, a.group, a.priority, a.active, a.acked, a.ackRequired, a.appeared, a.ackedAt, a.ackedBy, a.cleared});
    r.alarms(std::move(al));
    // ---- le pointeur
    if (project_->recording.pointer) {
        rec::Pointer p;
        p.t = t;
        p.view = pointer_.viewName;
        p.x = pointer_.x;
        p.y = pointer_.y;
        p.inside = pointer_.inside;
        p.buttons = pointer_.buttons;
        r.pointer(std::move(p));
    }
}

void Runtime::recordEvent(rec::EventKind kind, std::string a, std::string b, double x, double y) {
    if (!recordOn_ || !rec_ || !rec_->recorder || !project_) return;
    if (kind == rec::EventKind::Sound && !project_->recording.sounds) return;
    rec_->recorder->event(rec::Event{std::max(0.0, now_ - rec_->startAt), kind, std::move(a), std::move(b), x, y});
}

void Runtime::recordClick(rec::EventKind kind, Id object) {
    if (!recordOn_ || !project_ || !project_->recording.pointer) return;
    const View* v = shownViewOf(object);
    const Object* o = v ? v->object(object) : nullptr;
    double x = pointer_.x, y = pointer_.y;
    if (o && (!pointer_.inside || pointer_.view != v->id)) {
        const Box b = o->box();
        x = b.cx();
        y = b.cy();
    }
    recordEvent(kind, v && o ? v->name + "." + o->name : std::string{}, v ? v->name : std::string{}, x, y);
}

std::string Runtime::stopRecording(bool keep, double now, const std::string* name, const std::string* description,
                                   const std::string* password, std::string* why) {
    if (!rec_ || !rec_->recorder || (!recordOn_ && !rec_->stopping)) {
        if (why) *why = "aucun enregistrement ne tourne";
        return {};
    }
    auto& s = *rec_;
    now_ = std::max(now_, now);
    if (recordOn_) {
        recordEvent(rec::EventKind::Note, keep ? "fin" : "jet\xC3\xA9", {});
        recordSample(now_);
        s.stopAt = now_;
    }
    recordOn_ = false;
    s.stopping = false;
    const double t = std::max(0.0, s.stopAt - s.startAt);
    std::string finalName = rec::cleanName(name && !trimOf(*name).empty() ? trimOf(*name) : s.name);
    // Un autre fichier de ce nom : un nom libre (le fichier en cours n'en est pas un).
    if (fileExists(rec::pathOf(s.folder, finalName))) finalName = rec::uniqueName(s.folder, finalName);
    const std::string desc = description ? *description : s.description;
    const std::string pwd = password ? *password : s.password;
    std::string err;
    rec::Header written;
    const bool ok = s.recorder->close(t, keep, rec::pathOf(s.folder, finalName), finalName, desc, pwd, &err, &written);
    const std::string source = s.source.empty() ? std::string("IHM") : s.source;
    s.recorder.reset();
    s.itemsAt = -1e9;
    if (!ok) {
        log("Erreur", source, "enregistrement " + s.name + " : " + err);
        if (why) *why = err;
        return {};
    }
    if (!keep) {
        log("Enregistrement", source, s.name + " : jet\xC3\xA9 (rien n'est gard\xC3\xA9)");
        return {};
    }
    log("Enregistrement", source,
        "gard\xC3\xA9 : " + finalName + " (" + rec::clock(written.duration) + ", " + rec::sizeText(written.trackBytes)
            + (written.locked() ? ", prot\xC3\xA9g\xC3\xA9 par un mot de passe" : "") + ")");
    // La taille du dossier : les plus anciens partent (le journal le dit).
    if (project_) {
        const std::uint64_t max = static_cast<std::uint64_t>(std::max(1, project_->recording.maxFolderMb)) * 1024ull * 1024ull;
        for (const auto& gone : rec::prune(s.folder, max, finalName))
            log("Enregistrement", source,
                gone + " retir\xC3\xA9 : le dossier des enregistrements d\xC3\xA9passait " + rec::sizeText(max)
                    + " (les plus anciens partent les premiers ; Configuration \xE2\x80\xBA Enregistrements)");
    }
    return finalName;
}

void Runtime::askStopRecording(double now, const std::string& source) {
    if (!recordOn_ || !rec_ || !rec_->recorder) return;
    auto& s = *rec_;
    now_ = std::max(now_, now);
    if (!hooks_.recordingStop) {
        (void)stopRecording(true, now_);
        return;
    }
    recordEvent(rec::EventKind::Note, "fin", source);
    recordSample(now_);
    s.stopAt = now_;
    recordOn_ = false;
    s.stopping = true;
    RecordingStopAsk ask;
    ask.name = s.name;
    ask.description = s.description;
    ask.seconds = s.stopAt - s.startAt;
    ask.clicks = s.recorder->clicks();
    ask.alarms = s.recorder->alarmEvents();
    ask.frames = s.recorder->frames();
    ask.bytes = s.recorder->bytes();
    ask.locked = !s.password.empty();
    hooks_.recordingStop(ask);
}

std::string Runtime::finishRecording(bool keep, const std::string& name, const std::string& description, const std::string& password,
                                     double now, std::string* why) {
    return stopRecording(keep, now, &name, &description, &password, why);
}

// ======================================================================== rejouer ==
const rec::Player* Runtime::replayPlayer() const { return rec_ && replayOn_ ? rec_->player.get() : nullptr; }

const sim::Value* Runtime::replayValue(const std::string& upperKey) const {
    if (!replayOn_ || !rec_ || !rec_->player) return nullptr;
    if (upperKey.rfind("SYS.RECORDING", 0) == 0 || upperKey.rfind("SYS.REPLAY", 0) == 0) return nullptr;
    return rec_->player->valueOf(upperKey);
}

std::vector<rec::Item> Runtime::recordings(bool fresh) const {
    auto& s = recState();
    const std::string folder = recordingsFolder();
    const double now = wallSeconds();
    if (fresh || folder != s.itemsFolder || now - s.itemsAt > 2.0) {
        std::vector<std::string> notes;
        s.items = rec::listFolder(folder, &notes);
        s.itemsAt = now;
        s.itemsFolder = folder;
        for (const auto& n : notes) const_cast<Runtime*>(this)->log("Enregistrement", "dossier", n);
    }
    return s.items;
}

rec::Status Runtime::startReplay(const ReplayRequest& req, double now, std::string* why) {
    const auto no = [why](rec::Status st, std::string w) {
        if (why) *why = std::move(w);
        return st;
    };
    if (!project_ || !running_) return no(rec::Status::Unreadable, "l'IHM ne tourne pas");
    auto& s = recState();
    if (recordOn_ || s.stopping) return no(rec::Status::Unreadable, "un enregistrement tourne : arr\xC3\xAAte-le d'abord");
    now_ = std::max(now_, now);
    // Le fichier : un chemin donne, sinon le nom dans le dossier (sans casse).
    std::string path = trimOf(req.path);
    if (path.empty()) {
        const std::string folder = recordingsFolder();
        const std::string name = trimOf(req.name);
        if (name.empty()) return no(rec::Status::NotFound, "quel enregistrement rejouer ? (son nom)");
        path = rec::pathOf(folder, name);
        if (!fileExists(path)) {
            path.clear();
            for (const auto& it : recordings(true))
                if (sameText(it.name, name) || sameText(it.header.name, name)) path = it.path;
            if (path.empty()) return no(rec::Status::NotFound, "enregistrement introuvable : " + name);
        }
    }
    rec::Header header;
    auto track = std::make_shared<rec::Track>();
    std::string err;
    const rec::Status st = rec::readTrack(path, req.password, header, *track, &err);
    if (st != rec::Status::Ok) return no(st, err);
    if (replayOn_) (void)quitReplay(now_, req.source);
    // ---- ce qui est mis de cote
    s.live = RecordState::Live{};
    s.live.current = current_;
    s.live.currentArgs = currentArgs_;
    s.live.currentScope = currentScope_;
    s.live.slots = slots_;
    s.live.alarms = alarms_;
    s.live.language = language_;
    s.live.theme = settings_.theme;
    s.live.overrides = overrides_;
    s.live.pressed = pressed_;
    s.live.focused = focused_;
    pressed_ = kNoId;
    focused_ = kNoId;
    animation_.reset();
    unfollowAll();                                     // les liaisons ne suivent plus rien pour l'IHM
    if (hooks_.stopSounds) hooks_.stopSounds();
    // ---- la piste
    s.track = track;
    s.player = std::make_unique<rec::Player>(track);
    s.header = header;
    s.path = path;
    s.speed = snapSpeed(req.speed);
    s.paused = false;
    s.ended = false;
    s.lastWall = -1;
    s.screen = nullptr;
    s.overrides = nullptr;
    s.alarms = nullptr;
    s.overridesApplied = s.alarmsApplied = false;
    s.source2 = req.source;
    s.player->seek(std::max(0.0, req.from));
    // ---- ce qui n'existe plus dans le projet : ignore, dit une fois
    std::vector<std::string> vars, views, objects;
    for (const auto& k : track->keys)
        if (k.kind == rec::KeyKind::Variable && !const_cast<Runtime*>(this)->ihmSlot(k.key)
            && std::find(vars.begin(), vars.end(), rootOf(k.key)) == vars.end())
            vars.push_back(rootOf(k.key));
    const auto viewMissing = [&](const std::string& name) {
        if (!name.empty() && !project_->viewByName(name) && std::find(views.begin(), views.end(), name) == views.end()) views.push_back(name);
    };
    for (const auto& sc : track->screens) {
        viewMissing(sc.view);
        for (const auto& p : sc.popups) viewMissing(p.view);
    }
    const auto objectMissing = [&](const std::string& full) {
        const auto dot = full.find('.');
        if (dot == std::string::npos) return;
        const View* v = project_->viewByName(full.substr(0, dot));
        if (v && !v->objectByName(full.substr(dot + 1)) && std::find(objects.begin(), objects.end(), full) == objects.end()) objects.push_back(full);
    };
    for (const auto& e : track->events)
        if (e.kind == rec::EventKind::Click || e.kind == rec::EventKind::LongPress || e.kind == rec::EventKind::DoubleClick) objectMissing(e.a);
    for (const auto& o : track->overrides)
        for (const auto& x : o.list) objectMissing(x.view + "." + x.object);
    const auto list = [](const std::vector<std::string>& l) {
        std::string out;
        for (std::size_t i = 0; i < l.size() && i < 5; ++i) out += (i ? ", " : "") + l[i];
        if (l.size() > 5) out += "\xE2\x80\xA6";
        return out;
    };
    std::vector<std::string> parts;
    if (!vars.empty()) parts.push_back(std::to_string(vars.size()) + (vars.size() > 1 ? " variables" : " variable"));
    if (!views.empty()) parts.push_back(std::to_string(views.size()) + (views.size() > 1 ? " vues" : " vue"));
    if (!objects.empty()) parts.push_back(std::to_string(objects.size()) + (objects.size() > 1 ? " objets" : " objet"));
    s.note.clear();
    if (!parts.empty()) {
        std::string what;
        for (std::size_t i = 0; i < parts.size(); ++i) what += (i == 0 ? "" : i + 1 == parts.size() ? " et " : ", ") + parts[i];
        s.note = "Le projet a chang\xC3\xA9 depuis l'enregistrement : ce qui n'existe plus est ignor\xC3\xA9 (" + what + ") - la Console le d\xC3\xA9taille";
        if (!vars.empty()) logAt(LogLevel::Warning, "Relecture", header.name, "variables absentes du projet, ignor\xC3\xA9" "es : " + list(vars));
        if (!views.empty()) logAt(LogLevel::Warning, "Relecture", header.name, "vues absentes du projet, ignor\xC3\xA9" "es : " + list(views));
        if (!objects.empty()) logAt(LogLevel::Warning, "Relecture", header.name, "objets absents du projet, ignor\xC3\xA9" "s : " + list(objects));
    } else if (!header.fingerprint.empty() && header.fingerprint != fingerprint(*project_)) {
        s.note = "Le projet a chang\xC3\xA9 depuis l'enregistrement : il se rejoue quand m\xC3\xAAme";
        logAt(LogLevel::Info, "Relecture", header.name, s.note);
    }
    replayOn_ = true;
    composed_.clear();
    boundCalls_.clear();
    replayApply(true);
    log("Relecture", req.source.empty() ? std::string("IHM") : req.source,
        "d\xC3\xA9" "but : " + header.name + " (enregistr\xC3\xA9 le " + header.date + (header.where.empty() ? std::string{} : ", " + header.where)
            + (header.by.empty() ? std::string{} : ", par " + header.by) + ", " + rec::clock(track->end) + ") - l'IHM ne commande rien");
    return rec::Status::Ok;
}

void Runtime::replayApply(bool force) {
    if (!replayOn_ || !rec_ || !rec_->player || !project_) return;
    auto& s = *rec_;
    composed_.clear();
    boundCalls_.clear();
    jokerViews_.clear();
    linkViews_.clear();
    rangeViews_.clear();
    // ---- la vue et les popups
    const rec::Screen* sc = s.player->screen();
    if (sc && (force || sc != s.screen)) {
        s.screen = sc;
        const View* v = project_->viewByName(sc->view);
        if (v) {
            current_ = v->id;
            currentArgs_ = sc->arguments;
            currentScope_ = buildScope(*v, sc->arguments);
        }
        slots_.clear();
        for (const auto& p : sc->popups) {
            const View* pv = project_->viewByName(p.view);
            if (!pv) continue;
            PopupSlot slot;
            slot.view = pv->id;
            slot.x = p.x;
            slot.y = p.y;
            slot.placement = formatNumber(p.x) + "," + formatNumber(p.y);
            slot.arguments = p.arguments;
            slot.scope = buildScope(*pv, p.arguments);
            slots_.push_back(std::move(slot));
        }
        syncPopupIds();
        if (!sc->language.empty()) language_ = sc->language;
        if (!sc->theme.empty()) settings_.theme = sc->theme;
        animation_.reset();
    }
    // ---- les proprietes ecrites en marche
    const rec::Overrides* ov = s.player->overrides();
    if (force || ov != s.overrides || !s.overridesApplied) {
        s.overrides = ov;
        s.overridesApplied = true;
        overrides_.clear();
        if (ov)
            for (const auto& e : ov->list) {
                const View* v = project_->viewByName(e.view);
                const Object* o = v ? v->objectByName(e.object) : nullptr;
                if (v && o) overrides_[{v->id, o->id}][e.key] = e.value;
            }
    }
    // ---- les alarmes, comme elles etaient
    const rec::Alarms* al = s.player->alarms();
    if (force || al != s.alarms || !s.alarmsApplied) {
        s.alarms = al;
        s.alarmsApplied = true;
        alarms_.clear();
        if (al)
            for (const auto& a : al->list) {
                LiveAlarm x;
                x.name = a.name;
                x.message = a.message;
                x.group = a.group;
                x.priority = a.priority;
                x.active = a.active;
                x.acked = a.acked;
                x.ackRequired = a.ackRequired;
                x.appeared = a.appeared;
                x.ackedAt = a.ackedAt;
                x.ackedBy = a.ackedBy;
                x.cleared = a.cleared;
                if (const AlarmDef* d = project_->alarmByName(a.name)) x.alarm = d->id;
                alarms_.push_back(std::move(x));
            }
    }
}

void Runtime::replaySounds(double from, double to) {
    if (!rec_ || !rec_->player || !hooks_.playSound || to <= from || to - from > 1.0 || rec_->speed > 2.0) return;
    for (const rec::Event* e : rec_->player->eventsIn(from, to))
        if (e->kind == rec::EventKind::Sound && project_ && project_->resourceByName(e->a))
            // M4 -> M3 : un son rejoue, de source "relecture" - il sort sur le canal Relecture du melangeur.
            hooks_.playSound(e->a);
}

void Runtime::replayTick(double wallNow) {
    if (!replayOn_ || !rec_ || !rec_->player) return;
    auto& s = *rec_;
    if (s.lastWall < 0) s.lastWall = wallNow;
    const double dt = std::max(0.0, wallNow - s.lastWall);
    s.lastWall = wallNow;
    if (!s.paused) {
        const double from = s.player->position();
        double to = from + dt * s.speed;
        if (to >= s.player->length()) {
            to = s.player->length();
            s.paused = true;
            s.ended = true;
        }
        s.player->advanceTo(to);
        replaySounds(from, to);
    }
    replayApply(false);
}

bool Runtime::replayPause(std::optional<bool> paused) {
    if (!replayOn_ || !rec_ || !rec_->player) return false;
    auto& s = *rec_;
    const bool want = paused ? *paused : !s.paused;
    if (!want && s.ended && s.player->position() >= s.player->length() - 1e-6) {
        s.player->seek(0);                            // au bout : Lecture repart du debut
        replayApply(false);
    }
    s.paused = want;
    if (!want) s.ended = false;
    s.lastWall = -1;
    return true;
}

bool Runtime::replaySeek(double seconds) {
    if (!replayOn_ || !rec_ || !rec_->player) return false;
    auto& s = *rec_;
    s.player->seek(seconds);
    if (s.player->position() < s.player->length() - 1e-6) s.ended = false;
    replayApply(false);
    return true;
}

bool Runtime::replayStep(double delta) {
    if (!replayOn_ || !rec_ || !rec_->player) return false;
    return replaySeek(rec_->player->position() + delta);
}

bool Runtime::replaySpeed(double speed) {
    if (!replayOn_ || !rec_) return false;
    rec_->speed = snapSpeed(speed);
    return true;
}

bool Runtime::replaySpeedStep(int direction) {
    if (!replayOn_ || !rec_) return false;
    const auto* b = std::begin(rec::kSpeeds);
    const auto* e = std::end(rec::kSpeeds);
    const auto* at = std::find(b, e, rec_->speed);
    if (at == e) at = std::find(b, e, 1.0);
    if (direction > 0 && at + 1 != e) ++at;
    if (direction < 0 && at != b) --at;
    rec_->speed = *at;
    return true;
}

bool Runtime::quitReplay(double now, const std::string& source) {
    if (!replayOn_ || !rec_) return false;
    auto& s = *rec_;
    const double at = s.player ? s.player->position() : 0;
    current_ = s.live.current;
    currentArgs_ = s.live.currentArgs;
    currentScope_ = s.live.currentScope;
    slots_ = s.live.slots;
    syncPopupIds();
    alarms_ = s.live.alarms;
    language_ = s.live.language;
    settings_.theme = s.live.theme;
    overrides_ = s.live.overrides;
    pressed_ = kNoId;
    focused_ = s.live.focused;
    animation_.reset();
    replayOn_ = false;
    s.player.reset();
    s.track.reset();
    s.screen = nullptr;
    s.overrides = nullptr;
    s.alarms = nullptr;
    composed_.clear();
    boundCalls_.clear();
    jokerViews_.clear();
    linkViews_.clear();
    rangeViews_.clear();
    if (hooks_.stopSounds) hooks_.stopSounds();
    now_ = std::max(now_, now);
    followWatched(now_, true);                         // les liaisons reprennent ce qu'elles suivaient
    log("Relecture", source.empty() ? (s.source2.empty() ? std::string("IHM") : s.source2) : source,
        "quitt\xC3\xA9" "e \xC3\xA0 " + rec::clock(at) + " : l'IHM reprend en direct");
    return true;
}

ReplayInfo Runtime::replayInfo() const {
    ReplayInfo i;
    if (!replayOn_ || !rec_ || !rec_->player) return i;
    const auto& s = *rec_;
    i.active = true;
    i.header = s.header;
    i.path = s.path;
    i.position = s.player->position();
    i.length = s.player->length();
    i.speed = s.speed;
    i.paused = s.paused;
    i.ended = s.ended;
    i.note = s.note;
    return i;
}

std::vector<rec::Event> Runtime::replayRecentClicks(double window) const {
    std::vector<rec::Event> out;
    if (!replayOn_ || !rec_ || !rec_->player) return out;
    const double pos = rec_->player->position();
    for (const rec::Event* e : rec_->player->eventsIn(pos - window, pos))
        if (e->kind == rec::EventKind::Click || e->kind == rec::EventKind::LongPress || e->kind == rec::EventKind::DoubleClick) out.push_back(*e);
    return out;
}

std::optional<rec::Pointer> Runtime::replayPointer() const {
    if (!replayOn_ || !rec_ || !rec_->player) return std::nullopt;
    if (const rec::Pointer* p = rec_->player->pointer()) return *p;
    return std::nullopt;
}

int Runtime::recordingPlayerRow(Id object) const {
    if (!rec_) return -1;
    const auto it = rec_->playerRows.find(object);
    return it == rec_->playerRows.end() ? -1 : it->second;
}

// ======================================================================== SYS. ==
bool Runtime::recordSysValue(std::string_view n, sim::Value& out) const {
    const auto ms = [](double s) { return static_cast<std::int64_t>(std::llround(std::max(0.0, s) * 1000.0)); };
    const bool replay = replayOn_ && rec_ && rec_->player;
    if (n == "Recording") { out = sim::Value::boolean(recordOn_); return true; }
    if (n == "RecordingName") { out = sim::Value::text(recordOn_ && rec_ ? rec_->name : std::string{}); return true; }
    if (n == "RecordingTime") { out = sim::Value::time(recordOn_ && rec_ ? ms(now_ - rec_->startAt) : 0); return true; }
    if (n == "Replay") { out = sim::Value::boolean(replay); return true; }
    if (n == "ReplayPosition") { out = sim::Value::time(replay ? ms(rec_->player->position()) : 0); return true; }
    if (n == "ReplayLength") { out = sim::Value::time(replay ? ms(rec_->player->length()) : 0); return true; }
    if (n == "ReplaySpeed") { out = sim::Value::real(replay ? rec_->speed : 0.0); return true; }
    if (n == "ReplayPaused") { out = sim::Value::boolean(replay && rec_->paused); return true; }
    return false;
}

// ======================================================================== les natives ==
//  IHM_ENREG_DEMARRER(NOM [, DUREE_MAX : TIME] [, MOT_DE_PASSE]) : BOOL      IHM_ENREG_ARRETER([GARDER]) : STRING
//  IHM_ENREG_EN_COURS() : BOOL       IHM_REJOUER(NOM [, VITESSE] [, MOT_DE_PASSE]) : BOOL
//  IHM_RELECTURE_PAUSE([PAUSE]) IHM_RELECTURE_ALLER(POSITION : TIME) IHM_RELECTURE_VITESSE(V) IHM_RELECTURE_QUITTER() : BOOL
//  IHM_ENREG_EXISTE(NOM) : BOOL      IHM_ENREG_DUREE(NOM) : TIME      IHM_ENREG_EXPORTER(NOM, DOSSIER) : BOOL
//  IHM_ENREG_IMPORTER(CHEMIN) : STRING        IHM_ENREG_SUPPRIMER(NOM) : BOOL        (IHM_ENREG_LISTE : recordingFileOp)
bool Runtime::recordingNative(const std::string& u, const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result) {
    const auto text = [&](std::size_t i) {
        return i < args.size() ? (args[i].second.type() == sim::Type::String ? args[i].second.asString() : args[i].second.display()) : std::string{};
    };
    const std::string source = source_.empty() ? std::string("script") : source_;
    const auto said = [&](const std::string& w) { log("Enregistrement", source, u + " : " + w); };
    if (u == "IHM_ENREG_DEMARRER") {
        RecordingStart r;
        r.name = text(0);
        if (args.size() > 1) {
            const auto& d = args[1].second;
            r.maxSeconds = d.type() == sim::Type::Time ? static_cast<double>(d.asInteger()) / 1000.0 : d.asReal();
        }
        r.password = text(2);
        r.source = source;
        std::string why;
        const bool ok = startRecording(r, now_, &why);
        if (!ok) said(why);
        result = sim::Value::boolean(ok);
        return true;
    }
    if (u == "IHM_ENREG_ARRETER") {
        const bool keep = args.empty() || args[0].second.isTruthy();
        std::string why;
        const std::string kept = stopRecording(keep, now_, nullptr, nullptr, nullptr, &why);
        if (kept.empty() && keep && !why.empty()) said(why);
        result = sim::Value::text(kept);
        return true;
    }
    if (u == "IHM_ENREG_EN_COURS") {
        result = sim::Value::boolean(recordOn_);
        return true;
    }
    if (u == "IHM_REJOUER") {
        ReplayRequest r;
        r.name = text(0);
        r.speed = args.size() > 1 ? args[1].second.asReal() : 1.0;
        r.password = text(2);
        r.source = source;
        std::string why;
        const rec::Status st = startReplay(r, now_, &why);
        if (st == rec::Status::PasswordNeeded && hooks_.replayPassword && exportAsk_ > 0) {
            hooks_.replayPassword(r);                 // un geste de l'operateur : l'ecran demande le mot de passe
        } else if (st != rec::Status::Ok) {
            said(why);
        }
        result = sim::Value::boolean(st == rec::Status::Ok);
        return true;
    }
    if (u == "IHM_RELECTURE_PAUSE") {
        result = sim::Value::boolean(args.empty() ? replayPause() : replayPause(args[0].second.isTruthy()));
        return true;
    }
    if (u == "IHM_RELECTURE_ALLER") {
        const auto& v = args.empty() ? sim::Value{} : args[0].second;
        const double s = v.type() == sim::Type::Time ? static_cast<double>(v.asInteger()) / 1000.0 : v.asReal();
        result = sim::Value::boolean(replaySeek(s));
        return true;
    }
    if (u == "IHM_RELECTURE_VITESSE") {
        result = sim::Value::boolean(!args.empty() && replaySpeed(args[0].second.asReal()));
        return true;
    }
    if (u == "IHM_RELECTURE_QUITTER") {
        result = sim::Value::boolean(quitReplay(now_, source));
        return true;
    }
    const auto item = [&](const std::string& name) -> std::optional<rec::Item> {
        for (const auto& it : recordings())
            if (sameText(it.name, name) || sameText(it.header.name, name)) return it;
        return std::nullopt;
    };
    if (u == "IHM_ENREG_EXISTE") {
        result = sim::Value::boolean(item(trimOf(text(0))).has_value());
        return true;
    }
    if (u == "IHM_ENREG_DUREE") {
        const auto it = item(trimOf(text(0)));
        result = sim::Value::time(it ? static_cast<std::int64_t>(std::llround(it->header.duration * 1000.0)) : 0);
        return true;
    }
    if (u == "IHM_ENREG_EXPORTER") {
        std::string why, written;
        const auto it = item(trimOf(text(0)));
        const bool ok = it && rec::exportFile(recordingsFolder(), it->name, text(1), &written, &why);
        if (!it) said("enregistrement introuvable : " + text(0));
        else if (!ok) said(why);
        else log("Enregistrement", source, it->name + " export\xC3\xA9 vers " + written);
        result = sim::Value::boolean(ok);
        return true;
    }
    if (u == "IHM_ENREG_IMPORTER") {
        std::string why, name;
        const bool ok = rec::importFile(text(0), recordingsFolder(), name, &why);
        if (!ok) said(why);
        else log("Enregistrement", source, "import\xC3\xA9 : " + name);
        if (rec_) rec_->itemsAt = -1e9;
        result = sim::Value::text(ok ? name : std::string{});
        return true;
    }
    if (u == "IHM_ENREG_SUPPRIMER") {
        std::string why;
        const auto it = item(trimOf(text(0)));
        const std::string pf = hooks_.projectFolder ? hooks_.projectFolder() : std::string{};
        const std::string trash = pf.empty() ? std::string{} : pf + "/ihm/corbeille";
        const bool ok = it && rec::removeFile(recordingsFolder(), it->name, trash, &why);
        if (!it) said("enregistrement introuvable : " + text(0));
        else if (!ok) said(why);
        else log("Enregistrement", source, it->name + " supprim\xC3\xA9 (dans la corbeille de l'IHM)");
        if (rec_) rec_->itemsAt = -1e9;
        result = sim::Value::boolean(ok);
        return true;
    }
    return false;
}

// IHM_ENREG_LISTE([PREFIXE]) : LIST OF STRING - les noms du dossier (le dialecte en fait la liste).
bool Runtime::recordingFileOp(std::string_view op, const std::vector<std::string>& args, sim::Environment::FileResult& out, std::string& why) {
    if (op != "IHM_ENREG_LISTE") return false;
    const std::string prefix = args.empty() ? std::string{} : trimOf(args[0]);
    if (recordingsFolder().empty()) {
        why = "pas de dossier des enregistrements (le projet n'est pas enregistr\xC3\xA9)";
        return false;
    }
    for (const auto& it : rec::withPrefix(recordings(), prefix)) out.lines.push_back(it.name);
    out.number = static_cast<std::int64_t>(out.lines.size());
    return true;
}

// ======================================================================== une autre application ==
int Runtime::runCommand(const rec::Command& c, double now, std::string* message) {
    const auto say = [message](int code, std::string text) {
        if (message) *message = std::move(text);
        return code;
    };
    if (!c.error.empty()) return say(rec::kExitBadOption, c.error);
    if (!project_ || !running_) return say(rec::kExitNoHmi, "aucune IHM en marche");
    if (!project_->recording.external)
        return say(rec::kExitRefused, "refus\xC3\xA9 : le projet " + project_->config.name
                                          + " n'accepte pas les commandes d'une autre application (Configuration \xE2\x80\xBA Enregistrements)");
    const std::string source = "autre application";
    std::string why;
    switch (c.kind) {
        case rec::Command::Kind::Record: {
            RecordingStart r;
            r.name = c.name;
            r.maxSeconds = c.duration;
            r.password = c.password;
            r.source = source;
            if (!startRecording(r, now, &why)) return say(rec::kExitNoHmi, why);
            return say(rec::kExitDone, "enregistrement d\xC3\xA9marr\xC3\xA9 : " + rec_->name);
        }
        case rec::Command::Kind::Stop: {
            if (!recordOn_) return say(rec::kExitNotFound, "aucun enregistrement ne tourne");
            const std::string kept = stopRecording(!c.discard, now, nullptr, nullptr, nullptr, &why);
            if (c.discard) return say(rec::kExitDone, "enregistrement jet\xC3\xA9");
            if (kept.empty()) return say(rec::kExitNoHmi, why);
            return say(rec::kExitDone, "enregistrement gard\xC3\xA9 : " + kept);
        }
        case rec::Command::Kind::Replay: {
            ReplayRequest r;
            r.name = c.name;
            r.speed = c.speed;
            r.password = c.password;
            r.source = source;
            const rec::Status st = startReplay(r, now, &why);
            if (st == rec::Status::Ok) return say(rec::kExitDone, "relecture de " + c.name);
            if (st == rec::Status::NotFound) return say(rec::kExitNotFound, why);
            if (st == rec::Status::BadPassword || st == rec::Status::PasswordNeeded) return say(rec::kExitBadPassword, why);
            return say(rec::kExitNoHmi, why);
        }
        case rec::Command::Kind::QuitReplay:
            if (!quitReplay(now, source)) return say(rec::kExitNotFound, "aucune relecture n'est affich\xC3\xA9" "e");
            return say(rec::kExitDone, "relecture quitt\xC3\xA9" "e");
        case rec::Command::Kind::None: break;
    }
    return say(rec::kExitBadOption, "aucune commande d'enregistrement");
}

// ======================================================================== les actions ==
bool Runtime::recordingCommand(const View& v, const Object* o, const Action& a, double now) {
    if (!operationIsRecording(a.operation)) return false;
    const std::string source = where(v, o);
    const auto params = actionkinds::params(a);
    const auto param = [&](std::string_view key) {
        for (const auto& [k, val] : params)
            if (sameText(k, key)) return trimOf(val);
        return std::string{};
    };
    const auto rendered = [&](const std::string& t) { return trimOf(TextTemplate::compile(markers::strip(t, markers::Mode::Text)).render(environment())); };
    const auto number = [&](const std::string& text, double fallback) {
        std::string t = trimOf(text);
        if (t.empty()) return fallback;
        if (t.front() == '+') t.erase(0, 1);
        if (double direct = 0; parseNumber(t, direct)) return direct;
        bool ok = false;
        const std::string s = evalText(t, &ok);
        double x = fallback;
        if (!ok || !parseNumber(s, x)) return fallback;
        return x;
    };
    const auto flag = [&](const std::string& t, bool fallback) {
        if (t.empty()) return fallback;
        const std::string u = upperOf(t);
        if (u == "TRUE" || u == "OUI" || u == "1" || u == "VRAI") return true;
        if (u == "FALSE" || u == "NON" || u == "0" || u == "FAUX") return false;
        return evalBool(t, fallback);
    };
    const auto start = [&] {
        RecordingStart r;
        r.name = rendered(a.target);
        r.maxSeconds = number(param("duree"), 0) * 60.0;
        r.password = notify::unmaskSecret(param("mot_de_passe"));
        r.source = source;
        std::string why;
        if (!startRecording(r, now, &why)) log("Enregistrement", source, std::string(operationLabel(a.operation)) + " : " + why);
    };
    const auto stop = [&](bool keep) {
        if (!recordOn_) {
            log("Enregistrement", source, std::string(operationLabel(a.operation)) + " : aucun enregistrement ne tourne");
            return;
        }
        if (keep && flag(param("demander"), false)) askStopRecording(now, source);
        else (void)stopRecording(keep, now);
    };
    switch (a.operation) {
        case Operation::RecordStart:
            if (recordOn_) {
                if (sameText(param("deja"), "redemarrer")) {
                    (void)stopRecording(true, now);
                    start();
                } else {
                    log("Enregistrement", source, "d\xC3\xA9marrer : " + rec_->name + " tourne d\xC3\xA9j\xC3\xA0 (rien n'est fait)");
                }
            } else {
                start();
            }
            break;
        case Operation::RecordStop: stop(true); break;
        case Operation::RecordToggle:
            if (recordOn_) stop(true);
            else start();
            break;
        case Operation::RecordCancel: stop(false); break;
        case Operation::ReplayStart: {
            ReplayRequest r;
            r.name = rendered(a.target);
            r.speed = number(a.value, 1.0);
            r.from = number(param("depuis"), 0.0);
            r.password = notify::unmaskSecret(param("mot_de_passe"));
            r.source = source;
            std::string why;
            const rec::Status st = startReplay(r, now, &why);
            if (st == rec::Status::PasswordNeeded && hooks_.replayPassword) hooks_.replayPassword(r);
            else if (st != rec::Status::Ok) log("Relecture", source, "rejouer : " + why);
            break;
        }
        case Operation::ReplayPause:
            if (!replayPause()) log("Relecture", source, "pause : aucune relecture");
            break;
        case Operation::ReplaySeek:
            if (!replayStep(number(a.value, 10.0))) log("Relecture", source, "avancer ou reculer : aucune relecture");
            break;
        case Operation::ReplaySpeed:
            if (!replaySpeed(number(a.value, 1.0))) log("Relecture", source, "vitesse : aucune relecture");
            break;
        case Operation::ReplayQuit:
            if (!quitReplay(now, source)) log("Relecture", source, "quitter : aucune relecture");
            break;
        case Operation::RecordingExport: {
            const std::string name = rendered(a.target);
            std::string why, written;
            if (rec::exportFile(recordingsFolder(), name, rendered(a.value), &written, &why)) log("Enregistrement", source, name + " export\xC3\xA9 vers " + written);
            else log("Erreur", source, "exporter l'enregistrement : " + why);
            break;
        }
        case Operation::RecordingDelete: {
            const std::string name = rendered(a.target);
            const std::string pf = hooks_.projectFolder ? hooks_.projectFolder() : std::string{};
            std::string why;
            if (rec::removeFile(recordingsFolder(), name, pf.empty() ? std::string{} : pf + "/ihm/corbeille", &why)) {
                log("Enregistrement", source, name + " supprim\xC3\xA9 (dans la corbeille de l'IHM)");
                if (rec_) rec_->itemsAt = -1e9;
            } else {
                log("Erreur", source, "supprimer l'enregistrement : " + why);
            }
            break;
        }
        default: return false;
    }
    return true;
}

// ======================================================================== les objets ==
//  L'Enregistreur : "basculer" (son bouton) - demarre, ou arrete (la question si sa case Demander le nom a l'arret).
//  Le Lecteur : "ligne:N" choisit, "jouer:N" rejoue la ligne N, "jouer" l'enregistrement fixe (ou la ligne choisie).
void Runtime::recorderPart(const View& v, const Object& o, std::string_view part, double now) {
    const std::string source = where(v, &o);
    GestureGuard gesture(*this, source, "Clic sur");
    std::string why;
    if (!objectAllowed(o, &why)) {
        event("Acc\xC3\xA8s refus\xC3\xA9", source, why);
        return;
    }
    if (!shownFlag(v.id, o, "pilotable", true)) return;
    if (o.kind == Kind::Recorder) {
        if (part != "basculer") return;
        if (recordOn_) {
            if (shownFlag(v.id, o, "askName", true)) askStopRecording(now, source);
            else (void)stopRecording(true, now);
            return;
        }
        if (rec_ && rec_->stopping) return;            // la question de l'arret attend sa reponse
        RecordingStart r;
        // Le Nom accepte $reperes$ et {formules}, comme un texte.
        const auto rendered = [&](const char* key) {
            return trimOf(TextTemplate::compile(markers::strip(shownText(v.id, o, key), markers::Mode::Text)).render(environment(), viewScope(v.id)));
        };
        r.name = rendered("recName");
        r.description = rendered("description");
        r.maxSeconds = std::max(0.0, shownNumber(v.id, o, "maxMinutes", 0)) * 60.0;
        r.password = notify::unmaskSecret(o.text("password"));
        r.askAtStop = shownFlag(v.id, o, "askName", true);
        r.source = source;
        if (!startRecording(r, now, &why)) log("Enregistrement", source, "enregistrer : " + why);
        return;
    }
    if (o.kind != Kind::RecordingPlayer) return;
    const std::string fixed = trimOf(shownText(v.id, o, "fixed"));
    const std::string prefix = trimOf(shownText(v.id, o, "prefix"));
    const std::vector<rec::Item> rows = rec::withPrefix(recordings(), prefix);
    const auto play = [&](const std::string& name) {
        ReplayRequest r;
        r.name = name;
        r.speed = std::max(0.25, shownNumber(v.id, o, "speed", 1));
        r.source = source;
        std::string w;
        const rec::Status st = startReplay(r, now, &w);
        if (st == rec::Status::PasswordNeeded && hooks_.replayPassword) hooks_.replayPassword(r);
        else if (st != rec::Status::Ok) log("Relecture", source, "rejouer : " + w);
    };
    if (part.rfind("ligne:", 0) == 0) {
        const int row = std::atoi(std::string(part.substr(6)).c_str());
        if (row >= 0 && row < static_cast<int>(rows.size())) recState().playerRows[o.id] = row;
        return;
    }
    if (part.rfind("jouer:", 0) == 0) {
        const int row = std::atoi(std::string(part.substr(6)).c_str());
        if (row >= 0 && row < static_cast<int>(rows.size())) {
            recState().playerRows[o.id] = row;
            play(rows[static_cast<std::size_t>(row)].name);
        }
        return;
    }
    if (part == "jouer") {
        if (!fixed.empty()) play(fixed);
        else if (const int row = recordingPlayerRow(o.id); row >= 0 && row < static_cast<int>(rows.size())) play(rows[static_cast<std::size_t>(row)].name);
        else if (!rows.empty()) play(rows.front().name);
    }
}

} // namespace hmi
