// =============================================================================
//  web/WebStubs.cpp - 1.12.31 : les morceaux absents de la page (ce qu'elle ne
//  sait pas faire)
// -----------------------------------------------------------------------------
//  Quelques fonctions que le moteur appelle mais dont le code (sockets, PDFium,
//  garde anti-plantage de l'application) n'est pas compile pour la page. On les
//  rend ici de maniere nette : la visionneuse PDF dit que le lecteur est absent
//  (pdfium.dll n'est pas dans la page), la garde anti-plantage ne fait rien.
//
//  Compile seulement pour la page (XPG_WEB).
// =============================================================================
#ifdef XPG_WEB

#include "../app/hmi/HmiParamPanes.hpp"
#include "../core/CrashGuard.hpp"
#include "../hmi/HmiPdf.hpp"

#include <array>
#include <cstddef>
#include <cstring>

// ---- ce que WASI libc ne donne pas -------------------------------------------
//  gethostname : le nom du poste (IHM_NOM_POSTE, la fiche des versions). Sur une
//  page, c'est le navigateur : on rend un nom neutre.
extern "C" int gethostname(char* name, std::size_t len) {
    const char* kName = "navigateur";
    if (!name || len == 0) return -1;
    std::size_t n = std::strlen(kName);
    if (n >= len) n = len - 1;
    std::memcpy(name, kName, n);
    name[n] = '\0';
    return 0;
}

// ---- la garde anti-plantage de l'application (CrashGuard, non compile ici) ----
namespace core::crash {
void holdWatchdog(bool) noexcept {}
}

// ---- PDFium n'est pas dans la page -------------------------------------------
//  La visionneuse rend alors les documents image/texte, et dit, pour un PDF,
//  "Lecteur PDF absent" (comme l'application quand pdfium.dll manque).
namespace hmi::pdf {

void setSearchPaths(std::vector<std::string>) {}
Library library() {
    Library l;
    l.ok = false;
    l.found = false;
    l.why = "une page web n'embarque pas le lecteur PDF";
    return l;
}
std::string missingAdvice() {
    return "Lecteur PDF absent : une page web n'embarque pas PDFium. Ouvre ce document dans IHM Studio, "
           "ou exporte ses pages en images.";
}
std::string_view libraryFileName() noexcept { return "pdfium"; }
bool initOnThisThread() { return false; }
DocHandle open(const std::uint8_t*, std::size_t, std::string* error) {
    if (error) *error = "lecteur PDF absent (page web)";
    return nullptr;
}
void close(DocHandle) {}
int pageCount(DocHandle) { return 0; }
bool pageSize(DocHandle, int, double&, double&) { return false; }
bool render(DocHandle, int, int, int, int, Rgba&, std::string* error) {
    if (error) *error = "lecteur PDF absent (page web)";
    return false;
}
std::vector<Found> search(DocHandle, int, const std::u16string&) { return {}; }

} // namespace hmi::pdf

// ---- le repere "modifie, pas encore applique" des popups a parametres --------
//  Il vit avec les volets de l'editeur (app/hmi/HmiParamPanes.cpp, pas dans la
//  page) : la page ne le pose pas. Une popup a parametres marche (la copie, le
//  bouton Appliquer) ; seul le lisere jaune sur un champ modifie manque.
namespace app::hmiparams {
bool copyModified(const hmi::Runtime&, hmi::Id, std::string_view) { return false; }
void paintCopyMark(gfx::IRenderer&, const gfx::Rect&, float, float) {}
} // namespace app::hmiparams

#endif // XPG_WEB
