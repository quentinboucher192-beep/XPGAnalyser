// =============================================================================
//  app/FilePreview.cpp - lot API 8 : l'explorateur de fichiers, 2e partie
// =============================================================================
#include "FilePreview.hpp"

#include "../hmi/HmiMedia.hpp"
#include "../ui/FileExplorerModel.hpp"
#include "../ui/FileKinds.hpp"
#include "../xls/XlsmSource.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>

namespace app::preview {

    namespace {

        namespace fs = std::filesystem;
        namespace f = ui::files;

        constexpr std::uintmax_t kCountLimit = 48u * 1024u * 1024u;   // au-dela : l'en-tete seulement
        constexpr std::uintmax_t kImageLimit = 64u * 1024u * 1024u;
        constexpr std::size_t    kTextHead = 8u * 1024u;
        constexpr std::size_t    kTextLines = 8;
        constexpr std::size_t    kTextChars = 160;

        fs::path pathOf(std::string_view utf8) {
            const std::u8string u8(utf8.begin(), utf8.end());
            return fs::path(u8);
        }

        std::string nameOf(const std::string& path) {
            std::string p = path;
            while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
            const auto cut = p.find_last_of("/\\");
            return cut == std::string::npos ? p : p.substr(cut + 1);
        }

        // Le debut du fichier (au plus `limit` octets) ; faux : illisible.
        bool readHead(const std::string& path, std::size_t limit, std::string& out) {
            std::ifstream in(pathOf(path), std::ios::binary);
            if (!in) return false;
            out.assign(limit, '\0');
            in.read(out.data(), static_cast<std::streamsize>(limit));
            out.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));
            return true;
        }

        std::size_t countOf(std::string_view text, std::string_view pat, std::size_t from = 0,
                            std::size_t to = std::string_view::npos) {
            std::size_t n = 0;
            const std::size_t end = std::min(to, text.size());
            for (std::size_t p = text.find(pat, from); p != std::string_view::npos && p < end; p = text.find(pat, p + pat.size())) ++n;
            return n;
        }

        // L'attribut `name` de la premiere balise `<tag` (sans les entites).
        std::string attrOf(std::string_view text, std::string_view tag, std::string_view name) {
            const auto at = text.find(tag);
            if (at == std::string_view::npos) return {};
            const auto close = text.find('>', at);
            const std::string_view el = text.substr(at, close == std::string_view::npos ? std::string_view::npos : close - at);
            const std::string key = " " + std::string(name) + "=\"";
            const auto k = el.find(key);
            if (k == std::string_view::npos) return {};
            const auto v = k + key.size();
            const auto e = el.find('"', v);
            return std::string(el.substr(v, e == std::string_view::npos ? std::string_view::npos : e - v));
        }

        // Une ligne montrable : UTF-8 valide (un octet isole est lu en Latin-1,
        // comme un CSV d'Excel), tabulations en espaces, coupee a `maxChars`.
        std::string cleanLine(std::string_view s, std::size_t maxChars) {
            std::string out;
            std::size_t chars = 0;
            for (std::size_t i = 0; i < s.size() && chars < maxChars;) {
                const auto c = static_cast<unsigned char>(s[i]);
                if (c == '\t') {
                    out += "    ";
                    chars += 4;
                    ++i;
                    continue;
                }
                if (c < 0x20 || c == 0x7F) {
                    out += ' ';
                    ++chars;
                    ++i;
                    continue;
                }
                if (c < 0x80) {
                    out += static_cast<char>(c);
                    ++chars;
                    ++i;
                    continue;
                }
                const std::size_t len = (c >= 0xC2 && c < 0xE0) ? 2u : (c >= 0xE0 && c < 0xF0) ? 3u : (c >= 0xF0 && c < 0xF5) ? 4u : 0u;
                bool ok = len > 0 && i + len <= s.size();
                for (std::size_t k = 1; ok && k < len; ++k) ok = (static_cast<unsigned char>(s[i + k]) & 0xC0u) == 0x80u;
                if (ok) {
                    out.append(s.substr(i, len));
                    i += len;
                } else {
                    if (c >= 0xA0) {
                        out += static_cast<char>(0xC0u | (c >> 6));
                        out += static_cast<char>(0x80u | (c & 0x3Fu));
                    } else {
                        out += '?';
                    }
                    ++i;
                }
                ++chars;
            }
            return out;
        }

        // Les premieres lignes d'un texte ; faux : un fichier binaire.
        bool textLines(const std::string& head, std::vector<std::string>& out, std::size_t maxLines) {
            if (head.find('\0') != std::string::npos) return false;
            std::string_view v(head);
            if (v.size() >= 3 && v.substr(0, 3) == "\xEF\xBB\xBF") v.remove_prefix(3);
            while (!v.empty() && out.size() < maxLines) {
                const auto nl = v.find('\n');
                std::string_view line = v.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                out.push_back(cleanLine(line, kTextChars));
                if (nl == std::string_view::npos) break;
                v.remove_prefix(nl + 1);
            }
            return true;
        }

        std::int64_t modifiedOf(const fs::path& p) {
            std::error_code ec;
            const auto ft = fs::last_write_time(p, ec);
            if (ec) return 0;
            const auto sys = std::chrono::system_clock::now()
                           + std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
            return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
        }

        std::string upper(std::string s) {
            for (auto& ch : s)
                if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            return s;
        }

        void describeXpg(const std::string& path, const ProjectFacts& project, ui::FilePreviewInfo& p) {
            XpgSummary s;
            std::string why;
            if (!readXpg(path, s, &why)) {
                p.error = why;
                return;
            }
            if (!s.project.empty()) p.facts.emplace_back("Projet", s.version.empty() ? s.project : s.project + " (v" + s.version + ")");
            if (!s.plc.empty()) p.facts.emplace_back("Automate", s.plc);
            if (const auto d = dateLabel(s.exportedAt); !d.empty()) p.facts.emplace_back("Export\xC3\xA9 le", d);
            if (s.counted) {
                p.facts.emplace_back("Sections", std::to_string(s.sections));
                p.facts.emplace_back("Variables", std::to_string(s.variables));
                p.facts.emplace_back("DFB", std::to_string(s.dfbs));
            }
            if (!project.sourceXpg.empty() && f::samePath(project.sourceXpg, path))
                p.notes.emplace_back("d\xC3\xA9j\xC3\xA0 import\xC3\xA9 dans ce projet (son .XPG d'origine)");
            const long long mine = dateKey(s.exportedAt), theirs = dateKey(project.exportedAt);
            if (mine > 0 && theirs > 0 && mine > theirs) p.notes.emplace_back("plus r\xC3\xA9" "cent que le projet ouvert");
        }

        void describeXhw(const std::string& path, ui::FilePreviewInfo& p) {
            std::string head;
            if (!readHead(path, 8u * 1024u * 1024u, head)) {
                p.error = "illisible";
                return;
            }
            if (head.find("<IOExchangeFile") == std::string::npos) {
                p.error = "pas une configuration Control Expert";
                return;
            }
            const auto plcAt = head.find("<PLC");
            std::string plc = plcAt == std::string::npos ? std::string{} : attrOf(std::string_view(head).substr(plcAt), "<partItem", "partNumber");
            if (!plc.empty()) {
                const std::string family = attrOf(std::string_view(head).substr(plcAt), "<partItem", "family");
                p.facts.emplace_back("Automate", family.empty() ? plc : plc + " (" + family + ")");
            }
            if (const auto name = attrOf(head, "<contentHeader", "name"); !name.empty()) p.facts.emplace_back("Projet", name);
            if (const auto d = dateLabel(attrOf(head, "<fileHeader", "dateTime")); !d.empty()) p.facts.emplace_back("Export\xC3\xA9 le", d);
            p.facts.emplace_back("Racks", std::to_string(countOf(head, "<rackATS")));
            p.facts.emplace_back("Modules", std::to_string(countOf(head, "<moduleATS")));
        }

        void describeWorkbook(const std::string& path, const std::string& ext, ui::FilePreviewInfo& p) {
            if (ext == "xlsm") p.notes.emplace_back("avec macros");
            if (ext != "xlsx" && ext != "xlsm") {
                p.openable = true;   // un .xls ancien : Excel seulement
                return;
            }
            auto wb = xls::Workbook::open(path);
            if (!wb) {
                p.error = "classeur illisible : " + wb.error().message();
                return;
            }
            p.sheets = wb.value().sheetNames();
            if (p.sheets.empty()) return;
            if (const xls::Sheet* first = wb.value().sheet(p.sheets.front())) {
                const std::size_t lines = std::min<std::size_t>(first->rawLineCount(), 4);
                for (std::size_t i = 0; i < lines; ++i) {
                    std::vector<std::string> row;
                    for (std::size_t j = 0; j < 4; ++j) row.push_back(cleanLine(first->raw(i, j), 40));
                    while (!row.empty() && row.back().empty()) row.pop_back();
                    p.cells.push_back(std::move(row));
                }
                while (!p.cells.empty() && p.cells.back().empty()) p.cells.pop_back();
            }
            p.openable = true;
        }

        void describeFolder(const std::string& path, ui::FilePreviewInfo& p) {
            const f::Listing l = f::listFolder(path);
            if (l.error != f::ListError::None) {
                p.error = l.message;
                return;
            }
            // Les genres, les plus nombreux d'abord : "3 classeurs, 8 CSV, 1 dossier".
            std::map<std::pair<int, std::string>, std::size_t> kinds;
            for (const auto& e : l.entries) {
                const std::string key = e.family == f::Family::Workbook && e.ext == "csv" ? std::string("csv") : std::string{};
                ++kinds[{static_cast<int>(e.family), key}];
            }
            std::vector<std::pair<std::size_t, std::string>> parts;
            for (const auto& [k, n] : kinds) parts.emplace_back(n, f::countLabel(static_cast<f::Family>(k.first), k.second, n));
            std::stable_sort(parts.begin(), parts.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            std::string what = std::to_string(l.entries.size()) + (l.entries.size() > 1 ? " \xC3\xA9l\xC3\xA9ments" : " \xC3\xA9l\xC3\xA9ment");
            for (std::size_t i = 0; i < parts.size() && i < 5; ++i) what += (i == 0 ? " : " : ", ") + parts[i].second;
            if (parts.size() > 5) what += ", \xE2\x80\xA6";
            if (l.entries.empty()) what = "vide";
            p.facts.emplace_back("Contenu", what);
            if (l.systemHidden > 0) p.facts.emplace_back("Cach\xC3\xA9s", std::to_string(l.systemHidden) + " (syst\xC3\xA8me)");
        }

        std::shared_ptr<const ui::PreviewImage> shrink(const hmi::Rgba& src, int maxSide) {
            if (src.width <= 0 || src.height <= 0
                || src.pixels.size() < static_cast<std::size_t>(src.width) * static_cast<std::size_t>(src.height) * 4u)
                return nullptr;
            auto img = std::make_shared<ui::PreviewImage>();
            img->fileWidth = static_cast<std::uint32_t>(src.width);
            img->fileHeight = static_cast<std::uint32_t>(src.height);
            const int big = std::max(src.width, src.height);
            if (maxSide <= 0 || big <= maxSide) {
                img->width = src.width;
                img->height = src.height;
                img->rgba = src.pixels;
                return img;
            }
            const int w = std::max(1, static_cast<int>(static_cast<long long>(src.width) * maxSide / big));
            const int h = std::max(1, static_cast<int>(static_cast<long long>(src.height) * maxSide / big));
            img->width = w;
            img->height = h;
            img->rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0);
            const auto W = static_cast<std::size_t>(src.width);
            for (int y = 0; y < h; ++y) {
                const int y0 = static_cast<int>(static_cast<long long>(y) * src.height / h);
                const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * src.height / h));
                for (int x = 0; x < w; ++x) {
                    const int x0 = static_cast<int>(static_cast<long long>(x) * src.width / w);
                    const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * src.width / w));
                    std::uint64_t acc[4] = {0, 0, 0, 0};
                    for (int yy = y0; yy < y1; ++yy) {
                        const std::uint8_t* row = src.pixels.data() + static_cast<std::size_t>(yy) * W * 4u;
                        for (int xx = x0; xx < x1; ++xx)
                            for (std::size_t c = 0; c < 4; ++c) acc[c] += row[static_cast<std::size_t>(xx) * 4u + c];
                    }
                    const auto n = static_cast<std::uint64_t>(y1 - y0) * static_cast<std::uint64_t>(x1 - x0);
                    std::uint8_t* out = img->rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
                    for (std::size_t c = 0; c < 4; ++c) out[c] = static_cast<std::uint8_t>(acc[c] / n);
                }
            }
            return img;
        }

    } // namespace

    long long dateKey(std::string_view s) {
        const auto hash = s.find('#');
        if (hash != std::string_view::npos) s.remove_prefix(hash + 1);
        long long v[6] = {0, 0, 0, 0, 0, 0};
        int field = 0;
        bool digits = false;
        for (const char ch : s) {
            if (ch >= '0' && ch <= '9') {
                v[field] = v[field] * 10 + (ch - '0');
                digits = true;
            } else if ((ch == '-' || ch == ':' || ch == ' ' || ch == 'T') && digits) {
                if (++field >= 6) break;
                digits = false;
            } else if (ch != ' ') {
                return 0;
            }
        }
        if (v[0] < 1970 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31) return 0;
        return ((((v[0] * 100 + v[1]) * 100 + v[2]) * 100 + v[3]) * 100 + v[4]) * 100 + v[5];
    }

    std::string dateLabel(std::string_view s) {
        const long long k = dateKey(s);
        if (k == 0) return {};
        const auto two = [](long long n) { return (n < 10 ? "0" : "") + std::to_string(n); };
        const long long y = k / 10000000000LL, mo = (k / 100000000LL) % 100, d = (k / 1000000LL) % 100;
        const long long h = (k / 10000LL) % 100, mi = (k / 100LL) % 100;
        return two(d) + "/" + two(mo) + "/" + std::to_string(y) + " " + two(h) + ":" + two(mi);
    }

    bool readXpg(const std::string& path, XpgSummary& out, std::string* why) {
        out = {};
        std::error_code ec;
        const fs::path fp = pathOf(path);
        const std::uintmax_t size = fs::file_size(fp, ec);
        if (ec) {
            if (why) *why = "fichier introuvable";
            return false;
        }
        const bool whole = size <= kCountLimit;
        std::string text;
        if (!readHead(path, whole ? static_cast<std::size_t>(size) : 256u * 1024u, text)) {
            if (why) *why = "illisible";
            return false;
        }
        const std::string_view head = std::string_view(text).substr(0, 256u * 1024u);
        if (head.find("<PGMExchangeFile") == std::string_view::npos && head.find("<fileHeader") == std::string_view::npos) {
            if (why) *why = "pas un export Control Expert";
            return false;
        }
        out.product = attrOf(head, "<fileHeader", "product");
        out.exportedAt = attrOf(head, "<fileHeader", "dateTime");
        out.project = attrOf(head, "<contentHeader", "name");
        out.version = attrOf(head, "<contentHeader", "version");
        out.plc = attrOf(head, "<resource ", "resIdent");
        if (out.plc.empty()) out.plc = attrOf(head, "<resource ", "resName");
        if (whole) {
            const std::string_view all(text);
            out.sections = countOf(all, "<sectionDesc ");
            out.dfbs = countOf(all, "<FBSource ");
            const auto data = all.find("<dataBlock");
            if (data != std::string_view::npos) out.variables = countOf(all, "<variables ", data, all.find("</dataBlock>", data));
            out.counted = true;
        }
        return true;
    }

    ui::FilePreviewInfo describe(const std::string& path, const ProjectFacts& project) {
        ui::FilePreviewInfo p;
        p.path = path;
        p.name = nameOf(path);
        const fs::path fp = pathOf(path);
        std::error_code ec;
        const auto st = fs::status(fp, ec);
        if (ec || !fs::exists(st)) {
            p.kind = f::typeLabel(p.name, false);
            p.error = "introuvable";
            return p;
        }
        if (fs::is_directory(st)) {
            p.folder = true;
            p.kind = f::typeLabel(p.name, true);
            describeFolder(path, p);
            return p;
        }
        const std::string ext = f::extensionOf(p.name);
        const f::Family fam = f::familyOf(p.name, false);
        p.kind = f::typeLabel(p.name, false);
        const std::uintmax_t size = fs::file_size(fp, ec);
        if (ext == "xpg") {
            describeXpg(path, project, p);
        } else if (ext == "xhw") {
            describeXhw(path, p);
        } else if (fam == f::Family::Workbook && ext != "csv") {
            describeWorkbook(path, ext, p);
        } else if (fam == f::Family::Image) {
            const auto dims = f::imageSize(path);
            p.kind = f::imageTypeLabel(p.name, dims);
            p.image = thumbnail(path, 240);
            if (p.image) p.facts.emplace_back("Dimensions", std::to_string(p.image->fileWidth) + " \xC3\x97 " + std::to_string(p.image->fileHeight));
            else if (dims) p.facts.emplace_back("Dimensions", std::to_string(dims->w) + " \xC3\x97 " + std::to_string(dims->h));
            p.openable = true;
        } else if (fam == f::Family::Text || ext == "csv" || fam == f::Family::AppFile || ext == "st" || ext == "txt") {
            std::string head;
            if (!readHead(path, kTextHead, head)) p.error = "illisible";
            else if (!textLines(head, p.text, kTextLines)) p.notes.emplace_back("fichier binaire : pas d'aper\xC3\xA7u du texte");
            p.openable = fam != f::Family::AppFile;
        } else {
            p.openable = true;   // PDF, archive, autre : la grande icone et les faits
        }
        if (!ec) p.facts.emplace_back("Taille", f::sizeLabel(size));
        if (const auto when = modifiedOf(fp); when > 0) p.facts.emplace_back("Modifi\xC3\xA9", f::relativeTime(when, f::nowSeconds()));
        return p;
    }

    std::shared_ptr<const ui::PreviewImage> thumbnail(const std::string& path, int maxSide) {
        const std::string ext = f::extensionOf(nameOf(path));
        if (f::familyOfExtension(ext) != f::Family::Image) return nullptr;
        if (const auto dims = f::imageSize(path); dims && (dims->w > 16384u || dims->h > 16384u)) return nullptr;
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(pathOf(path), ec);
        if (ec || size == 0 || size > kImageLimit) return nullptr;
        std::string raw;
        if (!readHead(path, static_cast<std::size_t>(size), raw)) return nullptr;
        const hmi::Bytes bytes(raw.begin(), raw.end());
        hmi::Rgba rgba;
        const std::string format = upper(ext == "jpeg" ? std::string("jpg") : ext);
        if (!hmi::decodeImage(bytes, format, rgba, std::max(16, maxSide))) return nullptr;
        return shrink(rgba, maxSide);
    }

    std::string openWithSystem(const std::string& path) {
        // Comme les Fichiers externes de l'IHM (HmiAssetPanes.cpp) et l'aide :
        // le chemin entre guillemets ; un chemin qui en contient est refuse.
        if (path.find('"') != std::string::npos) return "chemin refus\xC3\xA9 : " + path;
        std::error_code ec;
        if (!fs::is_regular_file(pathOf(path), ec)) return "fichier absent : " + path;
#if defined(_WIN32)
        const std::string command = "start \"\" \"" + path + "\"";
#elif defined(__APPLE__)
        const std::string command = "open \"" + path + "\"";
#else
        const std::string command = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
#endif
        if (std::system(command.c_str()) != 0) return "aucun programme n'a pu ouvrir " + path;
        return {};
    }

} // namespace app::preview
