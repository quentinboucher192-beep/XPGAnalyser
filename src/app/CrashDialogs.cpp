// =============================================================================
//  app/CrashDialogs.cpp - 1.10.2 (CR) : les fenetres du plantage et du blocage
// -----------------------------------------------------------------------------
//  La fenetre de blocage s'ouvre depuis le fil du chien de garde, pendant que
//  le fil principal est arrete : une boite native de SDL (sans fenetre parente),
//  qui a sa propre boucle de messages. Le rapport est deja ecrit quand elle
//  s'ouvre.
// =============================================================================
#include "CrashDialogs.hpp"
#include "../core/Edition.hpp"   // 1.12.20 : le nom de l'application (API Studio, IHM Studio)

#include "App.hpp"
#include "Dossiers.hpp"
#include "Settings.hpp"
#include "../core/Version.hpp"
#include "../help/ProblemReport.hpp"   // 1.12.28 : « Signaler le probleme » (venu de main.cpp)
#include "../menu/MenuManager.hpp"
#include "screens/Screens.hpp"            // MessageDialog
#include "../core/CallTrail.hpp"
#include "../core/CrashGuard.hpp"
#include "../ui/Widget.hpp"               // ui::setClipboardText

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <vector>

#if XPG_WITH_SDL
#  include <SDL3/SDL.h>
#endif

namespace app::crashdialogs {
namespace {

#if XPG_WITH_SDL
bool hangDialog(const char* title, const char* message) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Attendre"},
        {0, 1, "Fermer"},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_WARNING | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    data.window = nullptr;
    data.title = title;
    data.message = message;
    data.numbuttons = 2;
    data.buttons = buttons;
    int chosen = 0;
    if (!SDL_ShowMessageBox(&data, &chosen)) return false;
    return chosen == 1;
}
#endif

ReportHook& reportHook() {
    static ReportHook hook;
    return hook;
}

bool containsNoCase(std::string_view line, std::string_view filter) {
    if (filter.empty()) return true;
    const auto low = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(line.begin(), line.end(), filter.begin(), filter.end(),
                       [&](char a, char b) { return low(a) == low(b); }) != line.end();
}

} // namespace

void install() {
#if XPG_WITH_SDL
    core::crash::setHangDialog(hangDialog);
#endif
}

void setReportHook(ReportHook hook) { reportHook() = std::move(hook); }

ui::CrashReportSetup setupFor(const core::crash::PendingReport& report, ui::crashreport::Mode mode) {
    ui::CrashReportSetup s;
    s.mode = mode;
    s.product = core::productName();
    s.path = report.path;
    s.text = report.text;
    s.when = report.when;
    const std::string folder = dossiers::utf8De(std::filesystem::path(dossiers::cheminDe(report.path)).parent_path());
    s.openFolder = [folder]() -> std::string {
        const std::string why = dossiers::ouvrirDansLeSysteme(folder);
        return why.empty() ? std::string("Le dossier des rapports s'ouvre.") : "Le dossier ne s'ouvre pas : " + why;
    };
    const std::string text = report.text;
    s.copyReport = [text]() -> std::string {
        ui::setClipboardText(text);
        return "Le rapport est copi\xC3\xA9 dans le presse-papiers : colle-le dans un courriel ou un ticket.";
    };
    if (reportHook()) s.signal = [report]() -> std::string { return reportHook() ? reportHook()(report) : std::string{}; };
    return s;
}

bool showPendingReport(App& app) {
    auto report = core::crash::takeNewReport();
    if (!report) return false;
    XPG_TRACE(Dialog, "au lancement : la fen\xC3\xAAtre du %s du %s", report->kind.c_str(), report->when.c_str());
    // 1.12.28 : le dialogue de l'appli, a la premiere image (plus la boite native d'avant).
    app.menus().ShowDialog(std::make_unique<ui::CrashReportDialog>(setupFor(*report, ui::crashreport::Mode::Pending)), {});
    return true;
}

bool showNewestReport(App& app) {
    auto report = core::crash::newestReport();
    if (!report) return false;
    app.menus().ShowDialog(std::make_unique<ui::CrashReportDialog>(setupFor(*report, ui::crashreport::Mode::Pending)), {});
    return true;
}

std::string prepareSignal(const core::crash::PendingReport& r) {
    namespace fs = std::filesystem;
    help::report::Form form;
    form.what = std::string(r.kind == "blocage" ? "Un blocage" : "Un plantage") + " s'est produit le " + r.when + ".";
    form.withProject = false;   // aucun projet n'est encore ouvert
    form.withLog = false;       // le rapport joint a deja l'historique interne
    form.attachedName = dossiers::utf8De(dossiers::cheminDe(r.path).filename());
    form.attachedText = r.text;
    help::report::Facts facts;
    facts.version = XPG_ANALYZER_VERSION;
    facts.system = help::report::systemName();
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const help::report::Stamp when{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min};
    std::string why;
    const fs::path zip = help::report::writeZip(fs::path(Settings::defaultPath()).parent_path(), form, facts, {}, when, &why);
    if (zip.empty()) return "Le signalement n'a pas pu \xC3\xAAtre pr\xC3\xA9par\xC3\xA9 : " + why;
    return "Le signalement est pr\xC3\xAAt (rien n'est envoy\xC3\xA9) : " + dossiers::utf8De(zip);
}

std::string callTrailText(std::size_t entries, std::string_view filter) {
    std::string out;
    char line[core::trail::kLineMax];
    // La pile des portees ouvertes de chaque fil.
    static core::trail::ThreadStack all[core::trail::kMaxThreads];
    const std::size_t n = core::trail::stacks(all, core::trail::kMaxThreads);
    const std::uint64_t now = core::trail::nowMs();
    out += "== Les port\xC3\xA9" "es ouvertes de chaque fil ==\n";
    for (std::size_t t = 0; t < n; ++t) {
        const auto& st = all[t];
        std::snprintf(line, sizeof line, "#%u %s : %zu port\xC3\xA9" "e(s)\n", unsigned(st.thread), st.name, st.depth);
        out += line;
        for (std::size_t i = 0; i < st.kept; ++i) {
            const double since = st.frames[i].sinceMs <= now ? double(now - st.frames[i].sinceMs) / 1000.0 : 0.0;
            std::snprintf(line, sizeof line, "    %s  (depuis %.1f s)\n", st.frames[i].name ? st.frames[i].name : "?", since);
            out += line;
        }
    }
    // Les entrees, les plus recentes en tete.
    const auto list = core::trail::recent(entries);
    std::snprintf(line, sizeof line, "\n== Les %zu derni\xC3\xA8res entr\xC3\xA9" "es (les plus r\xC3\xA9" "centes en t\xC3\xAAte)%s%.*s ==\n",
                  list.size(), filter.empty() ? "" : ", filtre : ", int(filter.size()), filter.data());
    out += line;
    std::size_t kept = 0;
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        const std::size_t len = core::trail::formatEntry(*it, line, sizeof line);
        const std::string_view text(line, len);
        if (!containsNoCase(text, filter)) continue;
        out.append(text);
        out += '\n';
        ++kept;
    }
    if (kept == 0) out += "(aucune)\n";
    return out;
}

void showCallTrail(App& app) {
    XPG_TRACE(Dialog, "Aide : journal interne");
    std::string text = callTrailText(40);
    text += "\n(Les 40 derni\xC3\xA8res. \xC2\xAB Copier et exporter \xC2\xBB prend les 2 000 derni\xC3\xA8res,"
            " avec la pile de chaque fil, dans le presse-papiers et dans un fichier du dossier crashs.)";
    auto dialog = std::make_unique<MessageDialog>("Journal interne (historique des appels)", text,
                                                  MessageDialog::Icon::Info, "Copier et exporter");
    dialog->setCancelLabel("Fermer");
    app.menus().ShowDialog(std::move(dialog), [&app](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        ui::setClipboardText(callTrailText(2000));
        const std::string path = core::crash::writeReportNow(core::crash::ReportKind::Manual,
                                                             "export du journal interne (Aide)");
        app.events().publish(StatusNotice{path.empty()
            ? std::string("Journal interne copi\xC3\xA9 dans le presse-papiers (le fichier n'a pas pu \xC3\xAAtre \xC3\xA9" "crit).")
            : "Journal interne copi\xC3\xA9 dans le presse-papiers et export\xC3\xA9 : " + path, 10.0});
    });
}

std::string recoveryNote(double stalledSeconds) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.1f", stalledSeconds);
    for (char* c = buf; *c; ++c) if (*c == '.') *c = ',';
    std::string note = core::productName() + " ne r\xC3\xA9pondait plus depuis " + buf + " s ; il est reparti.";
    const std::string report = core::crash::lastHangReport();
    if (!report.empty()) note += " Rapport de blocage : " + report;
    core::trail::note(core::trail::Kind::Watchdog, note);
    return note;
}

} // namespace app::crashdialogs
