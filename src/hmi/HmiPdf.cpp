// =============================================================================
//  hmi/HmiPdf.cpp - 1.12.30 : le moteur PDF (PDFium, charge a la demande).
//                   Voir HmiPdf.hpp.
// -----------------------------------------------------------------------------
//  Les signatures de l'API C de PDFium (fpdfview.h, fpdf_text.h) sont redites
//  ici : seules celles qui servent, sans ses en-tetes. Sous Windows x64 comme
//  sous Linux, PDFium suit la convention d'appel ordinaire du C (FPDF_CALLCONV
//  n'est __stdcall qu'en Windows 32 bits).
// =============================================================================
#include "HmiPdf.hpp"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace hmi::pdf {

namespace {

namespace fs = std::filesystem;

// Un chemin en UTF-8 <-> std::filesystem (C++20 : u8path est deprecie, u8string rend un std::u8string).
fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8Of(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---- l'API C de PDFium (ce qui sert) -----------------------------------------------
using FPDF_DOCUMENT = void*;
using FPDF_PAGE = void*;
using FPDF_BITMAP = void*;
using FPDF_TEXTPAGE = void*;
using FPDF_SCHHANDLE = void*;
using FPDF_BOOL = int;
using FPDF_DWORD = unsigned long;
struct FS_SIZEF { float width, height; };

constexpr int kBitmapBGRA = 4;              // FPDFBitmap_BGRA
constexpr int kRenderAnnot = 0x01;          // FPDF_ANNOT
constexpr int kRenderReverseBytes = 0x10;   // FPDF_REVERSE_BYTE_ORDER : RGBA au lieu de BGRA

struct Api {
    void (*initLibrary)() = nullptr;
    FPDF_DOCUMENT (*loadMemDocument64)(const void*, std::size_t, const char*) = nullptr;
    void (*closeDocument)(FPDF_DOCUMENT) = nullptr;
    unsigned long (*getLastError)() = nullptr;
    int (*getPageCount)(FPDF_DOCUMENT) = nullptr;
    FPDF_BOOL (*getPageSizeByIndexF)(FPDF_DOCUMENT, int, FS_SIZEF*) = nullptr;
    FPDF_PAGE (*loadPage)(FPDF_DOCUMENT, int) = nullptr;
    void (*closePage)(FPDF_PAGE) = nullptr;
    FPDF_BITMAP (*bitmapCreateEx)(int, int, int, void*, int) = nullptr;
    FPDF_BOOL (*bitmapFillRect)(FPDF_BITMAP, int, int, int, int, FPDF_DWORD) = nullptr;
    void (*bitmapDestroy)(FPDF_BITMAP) = nullptr;
    void (*renderPageBitmap)(FPDF_BITMAP, FPDF_PAGE, int, int, int, int, int, int) = nullptr;
    FPDF_TEXTPAGE (*textLoadPage)(FPDF_PAGE) = nullptr;
    void (*textClosePage)(FPDF_TEXTPAGE) = nullptr;
    FPDF_SCHHANDLE (*textFindStart)(FPDF_TEXTPAGE, const unsigned short*, unsigned long, int) = nullptr;
    FPDF_BOOL (*textFindNext)(FPDF_SCHHANDLE) = nullptr;
    int (*textGetSchResultIndex)(FPDF_SCHHANDLE) = nullptr;
    int (*textGetSchCount)(FPDF_SCHHANDLE) = nullptr;
    void (*textFindClose)(FPDF_SCHHANDLE) = nullptr;
    int (*textCountRects)(FPDF_TEXTPAGE, int, int) = nullptr;
    FPDF_BOOL (*textGetRect)(FPDF_TEXTPAGE, int, double*, double*, double*, double*) = nullptr;
};

std::mutex gLock;
std::vector<std::string> gPaths;
Library gLibrary;
bool gTried = false;
Api gApi;
bool gInitialized = false;   // FPDF_InitLibrary fait (sur le fil des documents)

#if defined(_WIN32)
using Module = HMODULE;
std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
Module openModule(const std::string& path) {
    // Le chemin complet : ses dependances (KERNEL32, GDI32...) se cherchent depuis son dossier, puis le systeme.
    return LoadLibraryExW(wide(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}
void* symbol(Module m, const char* name) { return reinterpret_cast<void*>(GetProcAddress(m, name)); }
std::string executableDir() {
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return {};
    std::wstring p(buf, n);
    const auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::string{} : narrow(p.substr(0, slash));
}
#else
using Module = void*;
Module openModule(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* symbol(Module m, const char* name) { return dlsym(m, name); }
std::string executableDir() {
    std::error_code ec;
    const auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : p.parent_path().string();
}
#endif

template <class F>
bool bind(F& f, Module m, const char* name, std::string& missing) {
    f = reinterpret_cast<F>(symbol(m, name));
    if (!f && missing.empty()) missing = name;
    return f != nullptr;
}

// Un chemin donne : un fichier, ou un dossier ou chercher le fichier de la plateforme.
std::string candidateFile(const std::string& path) {
    std::error_code ec;
    if (fs::is_directory(pathOf(path), ec)) return utf8Of(pathOf(path) / pathOf(std::string(libraryFileName())));
    return path;
}

// Sous le verrou : un essai de chargement, chemin par chemin.
void loadLocked() {
    std::vector<std::string> candidates;
    for (const auto& p : gPaths) candidates.push_back(candidateFile(p));
    if (const char* env = std::getenv("XPG_PDFIUM"); env && *env) candidates.push_back(candidateFile(env));
    if (const std::string dir = executableDir(); !dir.empty())
        candidates.push_back(utf8Of(pathOf(dir) / pathOf(std::string(libraryFileName()))));
#if !defined(_WIN32)
    candidates.push_back(std::string(libraryFileName()));   // le chemin de recherche du systeme
#endif
    gLibrary = Library{};
    std::string lastBroken;
    for (const auto& file : candidates) {
        std::error_code ec;
        const bool bare = file.find_first_of("/\\") == std::string::npos;
        if (!bare && !fs::exists(pathOf(file), ec)) continue;
        gLibrary.file = file;
        Module m = openModule(file);
        if (!m) {
            if (!bare) lastBroken = file;   // le chemin du systeme : pas trouve, ce n'est pas un fichier abime
            continue;
        }
        std::string missing;
        Api a;
        bind(a.initLibrary, m, "FPDF_InitLibrary", missing);
        bind(a.loadMemDocument64, m, "FPDF_LoadMemDocument64", missing);
        bind(a.closeDocument, m, "FPDF_CloseDocument", missing);
        bind(a.getLastError, m, "FPDF_GetLastError", missing);
        bind(a.getPageCount, m, "FPDF_GetPageCount", missing);
        bind(a.getPageSizeByIndexF, m, "FPDF_GetPageSizeByIndexF", missing);
        bind(a.loadPage, m, "FPDF_LoadPage", missing);
        bind(a.closePage, m, "FPDF_ClosePage", missing);
        bind(a.bitmapCreateEx, m, "FPDFBitmap_CreateEx", missing);
        bind(a.bitmapFillRect, m, "FPDFBitmap_FillRect", missing);
        bind(a.bitmapDestroy, m, "FPDFBitmap_Destroy", missing);
        bind(a.renderPageBitmap, m, "FPDF_RenderPageBitmap", missing);
        bind(a.textLoadPage, m, "FPDFText_LoadPage", missing);
        bind(a.textClosePage, m, "FPDFText_ClosePage", missing);
        bind(a.textFindStart, m, "FPDFText_FindStart", missing);
        bind(a.textFindNext, m, "FPDFText_FindNext", missing);
        bind(a.textGetSchResultIndex, m, "FPDFText_GetSchResultIndex", missing);
        bind(a.textGetSchCount, m, "FPDFText_GetSchCount", missing);
        bind(a.textFindClose, m, "FPDFText_FindClose", missing);
        bind(a.textCountRects, m, "FPDFText_CountRects", missing);
        bind(a.textGetRect, m, "FPDFText_GetRect", missing);
        if (!missing.empty()) {
            // Une autre version (plus ancienne) : on la garde chargee (rien ne se decharge), on ne s'en sert pas.
            gLibrary.found = true;
            gLibrary.why = std::string(libraryFileName()) + " incompatible (" + file + ") : la fonction " + missing
                         + " manque - une version trop ancienne";
            return;
        }
        gApi = a;
        gLibrary.ok = true;
        gLibrary.found = true;
        gLibrary.why.clear();
        return;
    }
    gLibrary.found = !lastBroken.empty();
    if (gLibrary.found)
        gLibrary.why = std::string(libraryFileName()) + " illisible ou incompatible avec ce syst\xC3\xA8me (" + lastBroken + ")";
    else
        gLibrary.why = std::string(libraryFileName()) + " introuvable";
}

std::string errorText(unsigned long code) {
    switch (code) {
        case 2: return "fichier PDF illisible";
        case 3: return "PDF illisible ou ab\xC3\xAEm\xC3\xA9";
        case 4: return "PDF prot\xC3\xA9g\xC3\xA9 par un mot de passe";
        case 5: return "PDF prot\xC3\xA9g\xC3\xA9 (s\xC3\xA9" "curit\xC3\xA9 non prise en charge)";
        case 6: return "page introuvable dans le PDF";
        default: return "PDF illisible";
    }
}

} // namespace

std::string_view libraryFileName() noexcept {
#if defined(_WIN32)
    return "pdfium.dll";
#else
    return "libpdfium.so";
#endif
}

void setSearchPaths(std::vector<std::string> paths) {
    const std::lock_guard<std::mutex> lock(gLock);
    gPaths = std::move(paths);
    if (!gLibrary.ok) gTried = false;   // un chargement manque se refait avec ces chemins
}

Library library() {
    const std::lock_guard<std::mutex> lock(gLock);
    if (!gTried) {
        gTried = true;
        loadLocked();
    }
    return gLibrary;
}

std::string missingAdvice() {
    const Library lib = library();
    if (lib.ok) return {};
    // Introuvable : ou il doit etre ; abime ou trop ancien : le remplacer.
    const bool notFound = !lib.found;
#if defined(_WIN32)
    const std::string where = notFound ? "il doit \xC3\xAAtre dans le dossier de l'application (l'installateur l'y met)"
                                       : "r\xC3\xA9installe l'application pour le remplacer";
#else
    const std::string where = notFound ? "il doit \xC3\xAAtre dans le dossier de l'application, ou dans le dossier que dit XPG_PDFIUM"
                                       : "remplace-le (python3 outils/pdfium/obtenir_pdfium.py)";
#endif
    return "Lecteur PDF absent : " + lib.why + " - " + where + ". Les images et le texte s'affichent toujours.";
}

// ---- un fil -----------------------------------------------------------------------------
bool initOnThisThread() {
    if (!library().ok) return false;
    if (!gInitialized) {
        gApi.initLibrary();
        gInitialized = true;
    }
    return true;
}

DocHandle open(const std::uint8_t* data, std::size_t size, std::string* error) {
    if (!initOnThisThread()) {
        if (error) *error = missingAdvice();
        return nullptr;
    }
    if (!data || size == 0) {
        if (error) *error = "fichier vide";
        return nullptr;
    }
    FPDF_DOCUMENT d = gApi.loadMemDocument64(data, size, nullptr);
    if (!d && error) *error = errorText(gApi.getLastError());
    return d;
}

void close(DocHandle d) {
    if (d && gInitialized) gApi.closeDocument(d);
}

int pageCount(DocHandle d) { return d && gInitialized ? std::max(0, gApi.getPageCount(d)) : 0; }

bool pageSize(DocHandle d, int index, double& width, double& height) {
    if (!d || !gInitialized) return false;
    FS_SIZEF s{0.f, 0.f};
    if (!gApi.getPageSizeByIndexF(d, index, &s) || s.width <= 0 || s.height <= 0) return false;
    width = s.width;
    height = s.height;
    return true;
}

bool render(DocHandle d, int index, int pxW, int pxH, int quarter, Rgba& out, std::string* error) {
    if (!d || !gInitialized || pxW <= 0 || pxH <= 0) return false;
    FPDF_PAGE page = gApi.loadPage(d, index);
    if (!page) {
        if (error) *error = errorText(gApi.getLastError());
        return false;
    }
    out.width = pxW;
    out.height = pxH;
    out.pixels.assign(static_cast<std::size_t>(pxW) * static_cast<std::size_t>(pxH) * 4u, 0);
    FPDF_BITMAP bmp = gApi.bitmapCreateEx(pxW, pxH, kBitmapBGRA, out.pixels.data(), pxW * 4);
    if (!bmp) {
        gApi.closePage(page);
        if (error) *error = "m\xC3\xA9moire insuffisante pour la page";
        return false;
    }
    (void)gApi.bitmapFillRect(bmp, 0, 0, pxW, pxH, 0xFFFFFFFFul);
    gApi.renderPageBitmap(bmp, page, 0, 0, pxW, pxH, ((quarter % 4) + 4) % 4, kRenderAnnot | kRenderReverseBytes);
    gApi.bitmapDestroy(bmp);
    gApi.closePage(page);
    return true;
}

std::vector<Found> search(DocHandle d, int index, const std::u16string& query) {
    std::vector<Found> out;
    if (!d || !gInitialized || query.empty()) return out;
    FPDF_PAGE page = gApi.loadPage(d, index);
    if (!page) return out;
    double pw = 0, ph = 0;
    (void)pageSize(d, index, pw, ph);
    FPDF_TEXTPAGE text = gApi.textLoadPage(page);
    if (text) {
        std::vector<unsigned short> wide(query.begin(), query.end());
        wide.push_back(0);
        FPDF_SCHHANDLE h = gApi.textFindStart(text, wide.data(), 0, 0);
        if (h) {
            while (gApi.textFindNext(h) && out.size() < 2000) {
                const int at = gApi.textGetSchResultIndex(h), n = gApi.textGetSchCount(h);
                Found f;
                const int rects = gApi.textCountRects(text, at, n);
                for (int k = 0; k < rects; ++k) {
                    double l = 0, t = 0, r = 0, b = 0;
                    if (!gApi.textGetRect(text, k, &l, &t, &r, &b)) continue;
                    // Le repere du PDF (origine en bas a gauche, y vers le haut) -> en haut a gauche, y vers le bas.
                    f.rects.push_back({std::min(l, r), ph - std::max(t, b), std::max(l, r), ph - std::min(t, b)});
                }
                if (!f.rects.empty()) out.push_back(std::move(f));
            }
            gApi.textFindClose(h);
        }
        gApi.textClosePage(text);
    }
    gApi.closePage(page);
    return out;
}

} // namespace hmi::pdf
