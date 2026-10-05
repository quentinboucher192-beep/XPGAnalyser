// app/Recovery.cpp - la reprise apres un arret brutal (lot 15). Le seul appel
// systeme : savoir si un processus tourne encore (windows.h reste ici).
#include "Recovery.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

namespace app {

namespace fs = std::filesystem;

namespace {

double wallNow() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string stamp(double epoch) {
    const auto t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

// Ecrit entier, puis renomme : jamais un fichier coupe.
bool writeAtomic(const fs::path& file, const std::string& text) {
    std::error_code ec;
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) return false;
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    return !ec;
}

std::map<std::string, std::string> readKeys(const fs::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq != std::string::npos) out[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

std::string newId() {
    std::random_device rd;
    std::mt19937_64 gen((static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^ static_cast<std::uint64_t>(wallNow() * 1000));
    char b[32];
    std::snprintf(b, sizeof b, "%012llx", static_cast<unsigned long long>(gen() & 0xFFFFFFFFFFFFULL));
    return b;
}

} // namespace

Recovery::~Recovery() { stop(); }

long Recovery::currentProcess() {
#if defined(_WIN32)
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(::getpid());
#endif
}

bool Recovery::processAlive(long pid) {
    if (pid <= 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno == EPERM;
#endif
}

std::string Recovery::readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void Recovery::start(const std::string& folder) {
    stop();
    folder_ = folder;
    std::error_code ec;
    fs::create_directories(folder_, ec);
    id_ = newId();
    lockFile_ = (fs::path(folder_) / ("session-" + id_ + ".verrou")).string();
    started_ = stamp(wallNow());
    running_ = true;
    // Les sessions d'avant : un verrou dont le processus n'existe plus.
    crashed_.reset();
    std::vector<Found> stale;
    for (const auto& e : fs::directory_iterator(folder_, ec)) {
        const auto name = e.path().filename().string();
        if (name.rfind("session-", 0) != 0 || e.path().extension() != ".verrou" || e.path().string() == lockFile_) continue;
        const auto k = readKeys(e.path());
        const long pid = k.count("processus") ? std::atol(k.at("processus").c_str()) : 0;
        // Toujours vivante (un autre lancement) : on n'y touche pas. Un numero
        // repris par un autre programme apres un redemarrage : le battement dit
        // si c'est bien elle (plus de 60 s sans battement : arretee).
        const double beat = k.count("battement_s") ? std::atof(k.at("battement_s").c_str()) : 0;
        if (processAlive(pid) && wallNow() - beat < 60 && pid != currentProcess()) continue;
        Found f;
        f.lockFile = e.path().string();
        f.started = k.count("debut") ? k.at("debut") : std::string{};
        f.lastBeat = beat > 0 ? stamp(beat) : std::string{};
        f.project = k.count("projet") ? k.at("projet") : std::string{};
        f.mode = k.count("mode") ? k.at("mode") : std::string{};
        if (k.count("reprise") && fs::exists(k.at("reprise"), ec)) {
            f.autosave = k.at("reprise");
            f.autosavedAt = k.count("reprise_a") ? k.at("reprise_a") : std::string{};
        }
        if (k.count("poste_etat") && fs::exists(k.at("poste_etat"), ec)) {
            f.stationState = k.at("poste_etat");
            f.stationAt = k.count("poste_a") ? k.at("poste_a") : std::string{};
        }
        stale.push_back(std::move(f));
    }
    // La plus recente avec des donnees est proposee ; les autres s'effacent.
    for (auto& f : stale) {
        if (f.hasData() && (!crashed_ || f.lastBeat > crashed_->lastBeat)) {
            if (crashed_) {
                const Found old = *crashed_;
                crashed_ = f;
                fs::remove(old.lockFile, ec);
                if (!old.autosave.empty()) fs::remove_all(fs::path(old.autosave).parent_path(), ec);
                if (!old.stationState.empty()) fs::remove(old.stationState, ec);
            } else {
                crashed_ = f;
            }
        } else if (!crashed_ || f.lockFile != crashed_->lockFile) {
            fs::remove(f.lockFile, ec);
            if (!f.autosave.empty()) fs::remove_all(fs::path(f.autosave).parent_path(), ec);
            if (!f.stationState.empty()) fs::remove(f.stationState, ec);
        }
    }
    writeLock();
}

void Recovery::stop() {
    if (!running_) return;
    running_ = false;
    std::error_code ec;
    fs::remove(lockFile_, ec);
    fs::remove_all(fs::path(folder_) / id_, ec);
}

void Recovery::writeLock() {
    if (!running_) return;
    std::string text = "# XpgAnalyzer - une session en cours : effacee a la sortie normale\n";
    text += "processus=" + std::to_string(currentProcess()) + "\n";
    text += "debut=" + started_ + "\n";
    text += "battement_s=" + std::to_string(static_cast<long long>(wallNow())) + "\n";
    text += "projet=" + project_ + "\n";
    text += "mode=" + mode_ + "\n";
    if (!autosave_.empty()) text += "reprise=" + autosave_ + "\nreprise_a=" + autosavedAt_ + "\n";
    if (!station_.empty()) text += "poste_etat=" + station_ + "\nposte_a=" + stationAt_ + "\n";
    (void)writeAtomic(lockFile_, text);
}

void Recovery::tick(double dt) {
    if (!running_) return;
    clock_ += dt;
    if (clock_ >= nextBeat_) {
        nextBeat_ = clock_ + 10;
        writeLock();
    }
}

void Recovery::setProject(const std::string& folder, const std::string& mode) {
    if (folder == project_ && mode == mode_) return;
    if (folder != project_) clearAutosave();
    project_ = folder;
    mode_ = mode;
    writeLock();
}

bool Recovery::autosaveDue(bool modified) const { return running_ && modified && !project_.empty() && clock_ - lastSave_ >= period_; }

bool Recovery::autosave(const std::function<bool(const std::string& folder, std::string* why)>& write, std::string* why) {
    lastSave_ = clock_;
    if (!running_ || !write) return false;
    std::error_code ec;
    // Ecrit a cote, puis remplace : la sauvegarde d'avant reste tant que la
    // nouvelle n'est pas complete.
    const fs::path base = fs::path(folder_) / id_;
    const fs::path next = base / "projet.nouveau";
    const fs::path done = base / "projet";
    fs::remove_all(next, ec);
    fs::create_directories(next, ec);
    if (!write(next.string(), why)) {
        fs::remove_all(next, ec);
        return false;
    }
    fs::remove_all(done, ec);
    fs::rename(next, done, ec);
    if (ec) {
        if (why) *why = ec.message();
        return false;
    }
    autosave_ = done.string();
    autosavedAt_ = stamp(wallNow());
    writeLock();
    return true;
}

void Recovery::clearAutosave() {
    if (autosave_.empty()) return;
    std::error_code ec;
    fs::remove_all(autosave_, ec);
    autosave_.clear();
    autosavedAt_.clear();
    writeLock();
}

bool Recovery::saveStationState(const std::string& text) {
    if (!running_) return false;
    std::error_code ec;
    const fs::path base = fs::path(folder_) / id_;
    fs::create_directories(base, ec);
    const fs::path file = base / "poste.txt";
    if (!writeAtomic(file, text)) return false;
    const bool first = station_.empty();
    station_ = file.string();
    stationAt_ = stamp(wallNow());
    if (first) writeLock();
    return true;
}

void Recovery::forget() {
    if (!crashed_) return;
    std::error_code ec;
    fs::remove(crashed_->lockFile, ec);
    if (!crashed_->autosave.empty()) fs::remove_all(fs::path(crashed_->autosave).parent_path(), ec);
    if (!crashed_->stationState.empty()) fs::remove(crashed_->stationState, ec);
    crashed_.reset();
}

std::optional<Recovery::Found> Recovery::take() {
    auto out = crashed_;
    if (!crashed_) return out;
    // Le verrou de la session arretee s'efface ; ses donnees passent dans le
    // dossier de cette session, le temps de les recharger.
    std::error_code ec;
    fs::remove(crashed_->lockFile, ec);
    const fs::path base = fs::path(folder_) / id_;
    fs::create_directories(base, ec);
    fs::path oldDir;
    bool moved = true;
    const auto move = [&](std::string& path, const char* name) {
        if (path.empty()) return;
        oldDir = fs::path(path).parent_path();
        const fs::path copy = base / name;
        std::error_code e1;
        fs::remove_all(copy, e1);
        e1.clear();
        fs::rename(path, copy, e1);
        if (e1) {
            std::error_code e2;
            fs::copy(path, copy, fs::copy_options::recursive | fs::copy_options::overwrite_existing, e2);
            if (e2) {
                moved = false;
                return;
            }
        }
        path = copy.string();
    };
    move(out->autosave, "projet_repris");
    move(out->stationState, "poste_repris.txt");
    if (moved && !oldDir.empty() && oldDir != base) fs::remove_all(oldDir, ec);
    crashed_.reset();
    return out;
}

} // namespace app
