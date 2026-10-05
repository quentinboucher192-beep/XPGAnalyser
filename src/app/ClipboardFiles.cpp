// =============================================================================
//  app/ClipboardFiles.cpp - les fichiers copies dans l'Explorateur
// =============================================================================
#include "ClipboardFiles.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "shell32.lib")
#    pragma comment(lib, "user32.lib")
#  endif
#endif

#include <cctype>

namespace app {

std::vector<std::string> win32ClipboardFiles() {
    std::vector<std::string> out;
#if defined(_WIN32)
    if (!IsClipboardFormatAvailable(CF_HDROP)) return out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE handle = GetClipboardData(CF_HDROP)) {
        if (auto drop = static_cast<HDROP>(GlobalLock(handle))) {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring wide(static_cast<std::size_t>(length) + 1, L'\0');
                DragQueryFileW(drop, i, wide.data(), length + 1);
                wide.resize(length);
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                                      nullptr, 0, nullptr, nullptr);
                if (bytes <= 0) continue;
                std::string utf8(static_cast<std::size_t>(bytes), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), bytes,
                                    nullptr, nullptr);
                out.push_back(std::move(utf8));
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
#endif
    return out;
}

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// file:///C:/a%20b -> C:/a b ; file:///home/x -> /home/x ; file://serveur/p -> //serveur/p
std::string pathOfUri(std::string_view uri) {
    std::string_view rest = uri.substr(7);          // apres "file://"
    std::string path;
    if (!rest.empty() && rest.front() != '/') path = "//";   // un hote : un chemin reseau
    // "/C:/..." : la barre de tete saute sous Windows.
    if (rest.size() >= 3 && rest[0] == '/' && std::isalpha(static_cast<unsigned char>(rest[1])) && rest[2] == ':')
        rest.remove_prefix(1);
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size() && hexValue(rest[i + 1]) >= 0 && hexValue(rest[i + 2]) >= 0) {
            path += static_cast<char>(hexValue(rest[i + 1]) * 16 + hexValue(rest[i + 2]));
            i += 2;
            continue;
        }
        path += rest[i];
    }
    return path;
}

} // namespace

std::vector<std::string> parseUriList(std::string_view text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from < text.size()) {
        auto nl = text.find('\n', from);
        if (nl == std::string_view::npos) nl = text.size();
        auto line = text.substr(from, nl - from);
        from = nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\0')) line.remove_suffix(1);
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        if (line.size() > 7 && line.substr(0, 7) == "file://") out.push_back(pathOfUri(line));
    }
    return out;
}

std::string cleanPastedPath(std::string_view text) {
    // La premiere ligne seulement : un collage de plusieurs chemins n'en garde qu'un.
    auto nl = text.find_first_of("\r\n");
    auto line = text.substr(0, nl);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1);
    if (line.size() >= 2 && line.front() == '"' && line.back() == '"') line = line.substr(1, line.size() - 2);
    if (line.size() > 7 && line.substr(0, 7) == "file://") return pathOfUri(line);
    return std::string(line);
}

} // namespace app
