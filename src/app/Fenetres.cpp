// =============================================================================
//  app/Fenetres.cpp - 1.12.27 : plusieurs fenetres d'IHM Studio (voir Fenetres.hpp)
// =============================================================================
#include "Fenetres.hpp"

#include "Dossiers.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <cstdlib>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace app::fenetres {

namespace fs = std::filesystem;

namespace {

std::mutex          gMutex;
std::optional<fs::path> gPose;

// Le nom du fichier d'un projet : l'empreinte de son chemin (FNV-1a), sans souci des
// separateurs ni, sous Windows, de la casse.
std::string nomDe(const std::string& dossierProjet) {
    std::string cle = dossierProjet;
    for (auto& c : cle) {
        if (c == '\\') c = '/';
#ifdef _WIN32
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
#endif
    }
    while (cle.size() > 1 && cle.back() == '/') cle.pop_back();
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : cle) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "projet-%016llx.txt", static_cast<unsigned long long>(h));
    return buf;
}

std::string heureMaintenant() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M", &tm);
    return buf;
}

#ifdef _WIN32
std::wstring large(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}
// Un argument de ligne de commande Windows entre guillemets (les regles de CommandLineToArgvW).
std::wstring entreGuillemets(const std::wstring& a) {
    std::wstring out = L"\"";
    std::size_t barres = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++barres; continue; }
        if (c == L'"') out.append(barres * 2 + 1, L'\\');
        else out.append(barres, L'\\');
        barres = 0;
        out += c;
    }
    out.append(barres * 2, L'\\');
    out += L'"';
    return out;
}
#endif

} // namespace

fs::path dossier() {
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gPose) return *gPose;
    }
    const std::string donnees = dossiers::actif(dossiers::Cle::Donnees);
    std::error_code ec;
    if (!donnees.empty()) return dossiers::cheminDe(donnees) / "fenetres";
    return fs::temp_directory_path(ec) / "IHM-Studio-fenetres";
}

void poserDossier(fs::path p) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (p.empty()) gPose.reset();   // le dossier habituel revient
    else gPose = std::move(p);
}

long processusCourant() noexcept {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

bool processusVivant(long pid) noexcept {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;   // il existe, mais pas a nous
    DWORD code = 0;
    const bool vivant = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return vivant;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

void annoncer(const std::string& dossierProjet) {
    if (dossierProjet.empty()) return;
    std::error_code ec;
    const fs::path d = dossier();
    fs::create_directories(d, ec);
    std::ofstream out(d / nomDe(dossierProjet), std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "pid " << processusCourant() << "\n"
        << "depuis " << heureMaintenant() << "\n"
        << "projet " << dossierProjet << "\n";
}

void retirer(const std::string& dossierProjet) {
    if (dossierProjet.empty()) return;
    const fs::path f = dossier() / nomDe(dossierProjet);
    // Seulement le sien : une autre fenetre a pu l'ouvrir depuis (Ouvrir quand meme).
    std::ifstream in(f, std::ios::binary);
    long pid = 0;
    std::string mot;
    if (in >> mot >> pid && mot == "pid" && pid != processusCourant()) return;
    in.close();
    std::error_code ec;
    fs::remove(f, ec);
}

std::optional<Presence> autreFenetre(const std::string& dossierProjet) {
    if (dossierProjet.empty()) return std::nullopt;
    const fs::path f = dossier() / nomDe(dossierProjet);
    std::ifstream in(f, std::ios::binary);
    if (!in) return std::nullopt;
    Presence p;
    std::string ligne;
    while (std::getline(in, ligne)) {
        if (ligne.rfind("pid ", 0) == 0) p.pid = std::strtol(ligne.c_str() + 4, nullptr, 10);
        else if (ligne.rfind("depuis ", 0) == 0) p.depuis = ligne.substr(7);
    }
    in.close();
    if (p.pid == processusCourant()) return std::nullopt;
    if (!processusVivant(p.pid)) {   // une fenetre disparue (un plantage) : son fichier part
        std::error_code ec;
        fs::remove(f, ec);
        return std::nullopt;
    }
    return p;
}

bool ouvrirNouvelleFenetre(const std::string& projet, std::string* pourquoi) {
    const auto dire = [&](std::string t) {
        if (pourquoi) *pourquoi = std::move(t);
        return false;
    };
#ifdef _WIN32
    std::wstring exe(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (n == 0 || n >= exe.size()) return dire("le programme est introuvable");
    exe.resize(n);
    std::wstring ligne = entreGuillemets(exe) + L" --nouvelle-fenetre";
    if (!projet.empty()) ligne += L" " + entreGuillemets(large(projet));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    // Le dossier de travail de cette fenetre (la version portable y a ses donnees).
    if (!CreateProcessW(exe.c_str(), ligne.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return dire("le lancement est refus\xC3\xA9 (erreur " + std::to_string(GetLastError()) + ")");
    // Elle viendra au premier plan : c'est l'utilisateur qui l'a demandee.
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec || exe.empty()) return dire("le programme est introuvable");
    const pid_t pid = fork();
    if (pid < 0) return dire("le lancement est refus\xC3\xA9");
    if (pid == 0) {
        // Le petit-fils : detache (pas de zombie a attendre), il remplace le fils.
        if (fork() != 0) _exit(0);
        const std::string e = exe.string();
        if (projet.empty()) execl(e.c_str(), e.c_str(), "--nouvelle-fenetre", static_cast<char*>(nullptr));
        else execl(e.c_str(), e.c_str(), "--nouvelle-fenetre", projet.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    (void)waitpid(pid, &status, 0);
    return true;
#endif
}

} // namespace app::fenetres
