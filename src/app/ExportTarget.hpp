#pragma once
// =============================================================================
//  app/ExportTarget.hpp - lot 7 : ou exporter, pour les exports en un clic de l'IHM
// -----------------------------------------------------------------------------
//  Les volets de l'IHM exportaient d'un clic dans exports/ du projet (les
//  traductions, le plan d'adressage, la carte memoire, le CSV du jumeau, le scan,
//  l'outil Modbus, les rapports d'essais, le dossier Word / PDF, les mesures de
//  la simulation). Le bouton demande maintenant OU, comme Exporter CSV de l'API :
//  un petit dialogue, le dossier exports/ deja ecrit (Entree : comme avant, sous
//  le nom habituel), le bouton ... (l'explorateur) pour un autre dossier ou un
//  autre nom. Le volet ne change pas : il exporte comme avant, et l'ecriture
//  (MainAnalysisScreen::writeHmiExport) prend la cible choisie.
//
//  askExportTarget(quoi, filtre, faire) : l'ecran d'analyse, au-dessus, pose la
//  question puis appelle faire() ; faux : personne pour la poser (un essai sans
//  ecran, le poste d'exploitation) - le volet exporte alors tout de suite, comme
//  avant. Le crochet est pose par l'application (screens/HmiRuntimeDialogs.cpp) :
//  les essais qui compilent les volets seuls n'en ont pas.
// =============================================================================
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace app {

using ExportTargetHook = std::function<bool(const std::string& what, const std::string& filter, std::function<void()> then)>;

inline ExportTargetHook& exportTargetHook() {
    static ExportTargetHook hook;
    return hook;
}

// La cible choisie, le temps de l'export qui suit (vide : exports/ du projet).
// Un dossier : le fichier y va sous son nom habituel ; un fichier : sous ce nom.
inline std::string& exportTargetOverride() {
    static std::string target;
    return target;
}

inline bool askExportTarget(const std::string& what, const std::string& filter, std::function<void()> then) {
    const auto& hook = exportTargetHook();
    return hook && hook(what, filter, std::move(then));
}

// ---- Lot API 8 : les exports qui demandent ou (la fin) ----
//  Les exports de l'IHM EN MARCHE (le bouton d'export, l'action Exporter,
//  IHM_EXPORTER) demandent ou, eux aussi, quand un geste de l'operateur les
//  lance (hmi::Runtime::exportData) : dans la simulation de l'editeur, le
//  dialogue ci-dessus ; au poste d'exploitation, le sien, au doigt
//  (screens/StationExportDialog). Ce qui suit leur est commun.

// Ce que la question nomme : "les alarmes (CSV)", "la recette Gaz (Excel)",
// "l'objet Courbe_1 (PDF)" - d'apres la source et le format de l'export.
inline std::string exportWhatOf(std::string_view source, std::string_view format) {
    std::string s(source);
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string what;
    const auto named = [&](std::string_view prefix, const char* article) {
        if (s.rfind(prefix, 0) != 0) return false;
        what = std::string(article) + std::string(source.substr(prefix.size()));
        return true;
    };
    if (s == "alarmes") what = "les alarmes";
    else if (s == "historique") what = "l'historique des alarmes";
    else if (s == "evenements" || s == "\xC3\xA9v\xC3\xA9nements") what = "les \xC3\xA9v\xC3\xA9nements";
    else if (s == "systeme" || s == "syst\xC3\xA8me") what = "le journal syst\xC3\xA8me";
    else if (s == "mesures") what = "les mesures archiv\xC3\xA9" "es";
    else if (s == "audit") what = "le journal d'audit";
    else if (!named("recette:", "la recette ") && !named("objet:", "l'objet ") && !named("rapport:", "le rapport "))
        what = s.empty() ? std::string("les donn\xC3\xA9" "es") : std::string(source);
    return format.empty() ? what : what + " (" + std::string(format) + ")";
}

// Le filtre de l'explorateur (ui::saveFile) pour le format d'un export.
inline std::string exportFilterOf(std::string_view format) {
    if (format == "Excel") return "Classeurs Excel|*.xlsx";
    if (format == "PDF") return "Documents PDF|*.pdf";
    return "Fichiers CSV|*.csv";
}

// Les chemins de l'interface sont en UTF-8 (std::filesystem::path(std::string)
// les lirait dans la page de code du systeme sous Windows).
inline std::filesystem::path exportPathOf(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
inline std::string exportUtf8Of(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

// OU VA LE FICHIER. `folder` : exports/ du projet ; `fileName` : le nom habituel
// (un chemin complet reste tel quel) ; `target` : la reponse du dialogue - vide :
// le nom habituel dans `folder` ; un dossier (qui existe, ou qui finit par / ou
// \) : le nom habituel dedans ; un fichier : ce nom-la, l'extension du nom
// habituel ajoutee s'il n'en a pas ; un chemin relatif : sous `folder`. Les
// blancs et les guillemets autour (Copier en tant que chemin d'acces) s'en
// vont ; les dossiers manquants sont crees.
inline std::filesystem::path exportFileFor(const std::filesystem::path& folder, const std::string& fileName, std::string target) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path usual = exportPathOf(fileName);
    fs::path file = folder / usual;
    while (!target.empty() && (target.front() == ' ' || target.front() == '"')) target.erase(target.begin());
    while (!target.empty() && (target.back() == ' ' || target.back() == '"')) target.pop_back();
    if (!target.empty()) {
        fs::path t = exportPathOf(target);
        if (t.is_relative()) t = folder / t;
        if (target.back() == '/' || target.back() == '\\' || fs::is_directory(t, ec)) {
            file = t / usual.filename();
        } else {
            file = t;
            if (!file.has_extension()) file += usual.extension();
        }
    }
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    return file;
}

// Un clic venu d'un navigateur (Configuration > Acces web) est rejoue comme un
// clic sur la vue : personne n'est devant l'ecran pour choisir ou enregistrer,
// et le navigateur ne choisit pas un dossier de son appareil. Le temps de le
// rejouer (App), les exports vont dans exports/ du poste, sans question.
inline int& exportQuestionsMutedDepth() {
    static int depth = 0;
    return depth;
}
inline bool exportQuestionsMuted() { return exportQuestionsMutedDepth() > 0; }
struct ExportQuestionMute {
    ExportQuestionMute() { ++exportQuestionsMutedDepth(); }
    ~ExportQuestionMute() { --exportQuestionsMutedDepth(); }
    ExportQuestionMute(const ExportQuestionMute&) = delete;
    ExportQuestionMute& operator=(const ExportQuestionMute&) = delete;
};
// ---- fin Lot API 8 ----

} // namespace app
