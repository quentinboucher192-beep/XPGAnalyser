// =============================================================================
//  tests/deploy_fake_station.cpp - 1.29.0 : un faux << poste >> pour deployinstall
// -----------------------------------------------------------------------------
//  L'agent le lance comme IHM-Studio : deploy_fake_station --ihm "<dossier>".
//  Il fait ce que dit <dossier>/donnees/comportement.txt :
//    attendre  (par defaut) surveille XPG_ARRET_FICHIER ; a la demande, recopie
//              la demande dans <dossier>/arret-recu.txt, ecrit son etat dans
//              XPG_REPRISE_FICHIER et sort (code 0) ;
//    sortir    sort tout de suite (code 3) : le controle de demarrage echoue ;
//    ignorer   ne regarde pas la demande d'arret (il faut le terminer).
//  Au lancement, il ajoute une ligne a <dossier>/poste-trace.txt (pid, les
//  deux variables, l'etat repris de XPG_REPRISE_FICHIER, puis efface).
//  Il sort de lui-meme apres 5 minutes (un essai qui casse ne le laisse pas).
//  Rien d'autre que la bibliotheque standard : il se compile aussi avec MinGW.
// =============================================================================
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#if defined(_WIN32)
#  include <process.h>
#  define XPG_GETPID _getpid
#else
#  include <unistd.h>
#  define XPG_GETPID getpid
#endif

namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || std::strcmp(argv[1], "--ihm") != 0) {
        std::printf("usage : deploy_fake_station --ihm DOSSIER\n");
        return 2;
    }
    const fs::path folder = argv[2];
    std::string mode = readAll(folder / "donnees" / "comportement.txt");
    while (!mode.empty() && (mode.back() == '\n' || mode.back() == '\r' || mode.back() == ' ')) mode.pop_back();
    if (mode.empty()) mode = "attendre";
    const std::string stopFile = env("XPG_ARRET_FICHIER"), resumeFile = env("XPG_REPRISE_FICHIER");
    std::string resumed;
    std::error_code ec;
    if (!resumeFile.empty() && fs::exists(resumeFile, ec)) {
        resumed = readAll(resumeFile);
        while (!resumed.empty() && (resumed.back() == '\n' || resumed.back() == '\r')) resumed.pop_back();
        fs::remove(resumeFile, ec);
    }
    {
        std::ofstream trace(folder / "poste-trace.txt", std::ios::binary | std::ios::app);
        trace << "pid=" << XPG_GETPID() << " mode=" << mode << " arret=" << stopFile << " reprise=" << resumeFile << " repris=" << resumed << "\n";
    }
    if (mode == "sortir") return 3;
    const auto end = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    while (std::chrono::steady_clock::now() < end) {
        if (mode == "attendre" && !stopFile.empty() && fs::exists(stopFile, ec)) {
            const std::string request = readAll(stopFile);
            {
                std::ofstream out(folder / "arret-recu.txt", std::ios::binary | std::ios::trunc);
                out << request;
            }
            if (!resumeFile.empty()) {
                const std::string tmp = resumeFile + ".tmp";
                {
                    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                    out << "etat de " << folder.filename().string() << "\n";
                }
                fs::rename(tmp, resumeFile, ec);
            }
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 4;
}
