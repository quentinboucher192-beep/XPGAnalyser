// =============================================================================
//  ui/FileKinds.cpp - lot API 8 : le genre d'un fichier, d'apres son extension
// =============================================================================
#include "FileKinds.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace ui::files {

    namespace {

        std::string lower(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        std::string upper(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        bool oneOf(std::string_view ext, std::initializer_list<std::string_view> list) {
            return std::find(list.begin(), list.end(), ext) != list.end();
        }

        // Une couleur du theme, un peu vers une autre (un brun : l'orange des
        // chaines vers le texte discret) - on reste dans les couleurs du theme.
        gfx::Color mix(gfx::Color a, gfx::Color b, float t) {
            const auto ch = [t](std::uint8_t x, std::uint8_t y) {
                return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t + 0.5f);
            };
            return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 255};
        }

    } // namespace

    std::string extensionOf(std::string_view fileName) {
        // Le nom seul (un chemin complet est accepte).
        const auto slash = fileName.find_last_of("/\\");
        if (slash != std::string_view::npos) fileName.remove_prefix(slash + 1);
        const auto dot = fileName.rfind('.');
        if (dot == std::string_view::npos || dot == 0 || dot + 1 >= fileName.size()) return {};
        return lower(fileName.substr(dot + 1));
    }

    Family familyOfExtension(std::string_view extIn) {
        const std::string ext = lower(extIn.empty() || extIn.front() != '.' ? extIn : extIn.substr(1));
        if (ext.empty()) return Family::Other;
        if (oneOf(ext, {"xpg", "xhw", "stu", "sta", "xef", "zef", "stx"})) return Family::ControlExpert;
        if (oneOf(ext, {"xls", "xlsx", "xlsm", "csv"})) return Family::Workbook;
        if (oneOf(ext, {"png", "jpg", "jpeg", "bmp", "gif", "svg", "ico", "webp", "tif", "tiff"})) return Family::Image;
        if (ext == "pdf") return Family::Pdf;
        if (oneOf(ext, {"txt", "log", "json", "xml", "md", "ini", "cfg", "st", "il", "ld", "fbd", "sfc", "yaml", "yml", "htm", "html"}))
            return Family::Text;
        if (oneOf(ext, {"zip", "7z", "rar", "gz", "tar"})) return Family::Archive;
        if (oneOf(ext, {"xpgproj", "xpgtheme", "xpgvues", "xpglayout"})) return Family::AppFile;
        return Family::Other;
    }

    Family familyOf(std::string_view fileName, bool folder) {
        return folder ? Family::Folder : familyOfExtension(extensionOf(fileName));
    }

    Tint tintOf(Family f) noexcept {
        switch (f) {
            case Family::Folder:        return Tint::Yellow;
            case Family::ControlExpert: return Tint::Blue;
            case Family::Workbook:      return Tint::Green;
            case Family::Image:         return Tint::Violet;
            case Family::Pdf:           return Tint::Red;
            case Family::Text:          return Tint::Gray;
            case Family::Archive:       return Tint::Brown;
            case Family::AppFile:       return Tint::Orange;
            case Family::Drive:         return Tint::Neutral;
            case Family::Other:         return Tint::Neutral;
        }
        return Tint::Neutral;
    }

    Icon iconOf(Family f) noexcept {
        switch (f) {
            case Family::Folder:        return Icon::Folder;
            case Family::ControlExpert: return Icon::Project;
            case Family::Workbook:      return Icon::Chart;
            case Family::Image:         return Icon::Image;
            case Family::Pdf:           return Icon::Document;
            case Family::Text:          return Icon::Document;
            case Family::Archive:       return Icon::Library;
            case Family::AppFile:       return Icon::Settings;
            case Family::Drive:         return Icon::Station;
            case Family::Other:         return Icon::Document;
        }
        return Icon::Document;
    }

    std::string typeLabel(std::string_view fileName, bool folder) {
        if (folder) return "Dossier";
        const std::string ext = extensionOf(fileName);
        if (ext.empty()) return "Fichier";
        // Control Expert
        if (ext == "xpg") return "Export Control Expert";
        if (ext == "xhw") return "Configuration mat\xC3\xA9rielle";
        if (ext == "stu") return "Projet Control Expert";
        if (ext == "sta") return "Archive Control Expert";
        if (ext == "xef" || ext == "zef") return "Export Control Expert (XEF)";
        // classeurs
        if (ext == "xlsm") return "Classeur Excel avec macros";
        if (ext == "xlsx") return "Classeur Excel";
        if (ext == "xls") return "Classeur Excel 97-2003";
        if (ext == "csv") return "Fichier CSV";
        // images
        if (ext == "jpg" || ext == "jpeg") return "Image JPEG";
        if (ext == "ico") return "Ic\xC3\xB4ne";
        if (familyOfExtension(ext) == Family::Image) return "Image " + upper(ext);
        if (ext == "pdf") return "Document PDF";
        // texte
        if (ext == "txt") return "Document texte";
        if (ext == "log") return "Journal";
        if (ext == "md") return "Document Markdown";
        if (ext == "ini" || ext == "cfg") return "Param\xC3\xA8tres";
        if (ext == "st") return "Code ST";
        if (ext == "htm" || ext == "html") return "Page web";
        if (familyOfExtension(ext) == Family::Text) return "Fichier " + upper(ext);
        // archives
        if (ext == "7z") return "Archive 7-Zip";
        if (familyOfExtension(ext) == Family::Archive) return "Archive " + upper(ext);
        // l'appli
        if (ext == "xpgproj") return "Projet XpgAnalyzer";
        if (ext == "xpgtheme") return "Th\xC3\xA8me XpgAnalyzer";
        if (ext == "xpgvues") return "Vues XpgAnalyzer";
        if (ext == "xpglayout") return "Disposition XpgAnalyzer";
        // les autres courants
        if (ext == "docx" || ext == "doc") return "Document Word";
        if (ext == "exe") return "Application";
        return "Fichier " + upper(ext);
    }

    std::string badgeText(std::string_view fileName, bool folder) {
        if (folder) return {};
        auto ext = upper(extensionOf(fileName));
        if (ext.size() > 4) ext.resize(4);
        return ext;
    }

    std::string countLabel(Family f, std::string_view ext, std::size_t n) {
        const bool many = n > 1;
        std::string what;
        switch (f) {
            case Family::Folder:        what = many ? "dossiers" : "dossier"; break;
            case Family::ControlExpert: what = many ? "exports Control Expert" : "export Control Expert"; break;
            case Family::Workbook:
                if (lower(ext) == "csv") what = "CSV";
                else what = many ? "classeurs" : "classeur";
                break;
            case Family::Image:         what = many ? "images" : "image"; break;
            case Family::Pdf:           what = "PDF"; break;
            case Family::Text:          what = many ? "textes" : "texte"; break;
            case Family::Archive:       what = many ? "archives" : "archive"; break;
            case Family::AppFile:       what = many ? "fichiers de l'appli" : "fichier de l'appli"; break;
            case Family::Drive:         what = many ? "lecteurs" : "lecteur"; break;
            case Family::Other:         what = many ? "autres fichiers" : "autre fichier"; break;
        }
        return std::to_string(n) + " " + what;
    }

    gfx::Color tintColor(const Theme& t, Tint tint) noexcept {
        switch (tint) {
            case Tint::Blue:    return t.onSurface(t.brand.scopeIn);
            case Tint::Green:   return t.onSurface(t.brand.scopeOut);
            case Tint::Violet:  return t.onSurface(t.brand.scopeInOut);
            case Tint::Red:     return t.color.error;
            case Tint::Gray:    return t.color.textMuted;
            case Tint::Brown:   return mix(t.color.syntaxString, t.color.textMuted, 0.45f);
            case Tint::Orange:  return t.onSurface(t.color.syntaxString);
            case Tint::Yellow:  return t.color.warning;
            case Tint::Neutral: return t.color.textMuted;
        }
        return t.color.textMuted;
    }

    std::optional<ImageSize> imageSize(const std::string& utf8Path) {
        const std::u8string u8(utf8Path.begin(), utf8Path.end());
        std::ifstream in(std::filesystem::path(u8), std::ios::binary);
        if (!in) return std::nullopt;
        std::array<unsigned char, 32> h{};
        in.read(reinterpret_cast<char*>(h.data()), static_cast<std::streamsize>(h.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        const auto be32 = [&h](std::size_t i) {
            return (static_cast<std::uint32_t>(h[i]) << 24) | (static_cast<std::uint32_t>(h[i + 1]) << 16)
                 | (static_cast<std::uint32_t>(h[i + 2]) << 8) | static_cast<std::uint32_t>(h[i + 3]);
        };
        const auto le16 = [&h](std::size_t i) {
            return static_cast<std::uint32_t>(h[i]) | (static_cast<std::uint32_t>(h[i + 1]) << 8);
        };
        const auto le32 = [&h](std::size_t i) {
            return static_cast<std::uint32_t>(h[i]) | (static_cast<std::uint32_t>(h[i + 1]) << 8)
                 | (static_cast<std::uint32_t>(h[i + 2]) << 16) | (static_cast<std::uint32_t>(h[i + 3]) << 24);
        };
        // PNG : la signature, puis IHDR (largeur, hauteur en gros-boutiste).
        if (got >= 24 && h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') return ImageSize{be32(16), be32(20)};
        // GIF : "GIF8", largeur et hauteur en petit-boutiste sur 16 bits.
        if (got >= 10 && h[0] == 'G' && h[1] == 'I' && h[2] == 'F' && h[3] == '8') return ImageSize{le16(6), le16(8)};
        // BMP : "BM", l'en-tete BITMAPINFOHEADER (hauteur negative : de haut en bas).
        if (got >= 26 && h[0] == 'B' && h[1] == 'M') {
            const auto height = static_cast<std::int32_t>(le32(22));
            return ImageSize{le32(18), static_cast<std::uint32_t>(height < 0 ? -height : height)};
        }
        // JPEG : les segments jusqu'au premier SOFn (64 Ko lus au plus).
        if (got >= 4 && h[0] == 0xFF && h[1] == 0xD8) {
            in.clear();
            in.seekg(2);
            std::size_t read = 2;
            while (in && read < 65536) {
                int c = in.get();
                if (c != 0xFF) return std::nullopt;
                int marker = in.get();
                while (marker == 0xFF) marker = in.get();
                if (marker < 0) return std::nullopt;
                const int hi = in.get(), lo = in.get();
                if (hi < 0 || lo < 0) return std::nullopt;
                const int len = (hi << 8) | lo;
                read += 4;
                const bool sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
                if (sof) {
                    (void)in.get();   // la precision
                    const int h1 = in.get(), h2 = in.get(), w1 = in.get(), w2 = in.get();
                    if (w2 < 0) return std::nullopt;
                    return ImageSize{static_cast<std::uint32_t>((w1 << 8) | w2), static_cast<std::uint32_t>((h1 << 8) | h2)};
                }
                if (len < 2) return std::nullopt;
                in.seekg(len - 2, std::ios::cur);
                read += static_cast<std::size_t>(len - 2);
            }
        }
        return std::nullopt;
    }

    std::string imageTypeLabel(std::string_view fileName, std::optional<ImageSize> size) {
        std::string label = typeLabel(fileName, false);
        if (size && size->w > 0 && size->h > 0)
            label += " " + std::to_string(size->w) + " \xC3\x97 " + std::to_string(size->h);
        return label;
    }

} // namespace ui::files
