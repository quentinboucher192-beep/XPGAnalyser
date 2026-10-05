// =============================================================================
//  ui/widgets/PathBrowse.cpp - le bouton ... d'un champ de chemin
// =============================================================================
#include "PathBrowse.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace ui {

    namespace {

        namespace fs = std::filesystem;

        // Les chemins de l'interface sont en UTF-8 ; std::filesystem::path(std::string)
        // les lirait dans la page de code du systeme sous Windows.
        fs::path pathOf(std::string_view utf8) {
            const std::u8string u8(utf8.begin(), utf8.end());
            return fs::path(u8);
        }

        std::string utf8Of(const fs::path& p) {
            const auto u8 = p.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::string trimmed(std::string_view s) {
            std::size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return std::string(s.substr(a, b - a));
        }

        // Ce qu'un champ montre : sans blancs autour ni guillemets ("Copier en
        // tant que chemin d'acces" les met).
        std::string cleanPath(std::string_view text) {
            std::string s = trimmed(text);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = trimmed(std::string_view(s).substr(1, s.size() - 2));
            return s;
        }

        std::vector<std::string> splitOn(std::string_view s, std::string_view separators) {
            std::vector<std::string> out;
            std::string part;
            for (const char c : s) {
                if (separators.find(c) != std::string_view::npos) {
                    out.push_back(part);
                    part.clear();
                } else {
                    part += c;
                }
            }
            out.push_back(part);
            return out;
        }

        // "*.xpg; *.XHW ;.csv" -> les extensions "xpg", "XHW", "csv" (SDL ne veut
        // que des lettres, des chiffres, '-', '_' et '.'). "*" et "*.*" ne
        // donnent rien : "Tous les fichiers" suit de toute facon.
        std::vector<std::string> extensionsOf(std::string_view patterns) {
            std::vector<std::string> out;
            for (auto token : splitOn(patterns, ";, ")) {
                token = trimmed(token);
                while (!token.empty() && (token.front() == '*' || token.front() == '.')) token.erase(token.begin());
                if (token.empty()) continue;
                const bool valid = std::all_of(token.begin(), token.end(), [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
                });
                if (!valid) continue;
                // Windows ne regarde pas la casse ; les autres, si : "logo.png"
                // est le cas courant.
                for (auto& c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (std::find(out.begin(), out.end(), token) == out.end()) out.push_back(token);
            }
            return out;
        }

        std::string upper(std::string s) {
            for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return s;
        }

        // Les filtres de SDL : des paires ("nom", "xpg;xhw"), puis "Tous les fichiers".
        std::vector<std::pair<std::string, std::string>> sdlFilters(std::string_view spec) {
            std::vector<std::pair<std::string, std::string>> out;
            const auto parts = splitOn(spec, "|");
            const auto add = [&out](std::string name, std::string_view patterns) {
                const auto exts = extensionsOf(patterns);
                if (exts.empty()) return;          // "*" : "Tous les fichiers" suit de toute facon
                std::string joined, shown;
                for (const auto& e : exts) {
                    joined += (joined.empty() ? "" : ";") + e;
                    shown += (shown.empty() ? "" : ", ") + upper(e);
                }
                if (trimmed(name).empty()) name = "Fichiers " + shown;
                out.emplace_back(trimmed(name), joined);
            };
            if (parts.size() == 1) {
                add({}, parts.front());
            } else {
                for (std::size_t i = 0; i + 1 < parts.size(); i += 2) add(parts[i], parts[i + 1]);
            }
            out.emplace_back("Tous les fichiers", "*");
            return out;
        }

        std::string firstExtension(std::string_view spec) {
            const auto filters = sdlFilters(spec);
            for (const auto& [name, patterns] : filters) {
                if (patterns == "*") continue;
                const auto cut = patterns.find(';');
                return patterns.substr(0, cut);
            }
            return {};
        }

        bool isDir(const fs::path& p) {
            std::error_code ec;
            return !p.empty() && fs::is_directory(p, ec);
        }

        bool pathExists(const fs::path& p) {
            std::error_code ec;
            return !p.empty() && fs::exists(p, ec);
        }

        // Le plus proche dossier qui existe, en remontant ("" : aucun).
        fs::path existingFolder(fs::path p) {
            while (!p.empty()) {
                if (isDir(p)) return p;
                auto up = p.parent_path();
                if (up == p) break;
                p = std::move(up);
            }
            return {};
        }

        // Un dossier de depart finit par un separateur : sous Windows, SDL ne
        // prend un chemin pour un DOSSIER (lpstrInitialDir) qu'a cette
        // condition - sans lui, c'est un nom de fichier propose.
        std::string asFolder(const fs::path& p) {
            std::string s = utf8Of(p);
            if (!s.empty() && s.back() != '/' && s.back() != '\\')
                s += static_cast<char>(fs::path::preferred_separator);
            return s;
        }

        // Ou l'explorateur s'ouvre : la ou pointe le champ, sinon `start`.
        std::string startFor(const PathBrowse& spec, std::string_view current) {
            const std::string text = cleanPath(current);
            const fs::path base = spec.start.empty() ? fs::path{} : pathOf(cleanPath(spec.start)).make_preferred();
            // Le dossier de `start` : lui-meme, ou celui du fichier propose.
            const fs::path baseDir = base.empty() ? fs::path{} : (isDir(base) || !base.has_extension() ? base : base.parent_path());
            fs::path wanted;
            if (!text.empty()) {
                wanted = pathOf(text).make_preferred();
                if (wanted.is_relative() && !baseDir.empty()) wanted = baseDir / wanted;   // un nom seul : dans `start`
            } else {
                wanted = base;
            }
            if (wanted.empty()) return {};
            if (spec.mode == BrowseMode::Folder) {
                // Un dossier a creer : l'explorateur s'ouvre la ou il ira (son parent).
                const auto folder = existingFolder(spec.newFolder && !isDir(wanted) ? wanted.parent_path() : wanted);
                return folder.empty() ? std::string{} : utf8Of(folder);
            }
            if (isDir(wanted)) return asFolder(wanted);
            // Un fichier : dans un dossier qui existe, il est propose (un
            // enregistrement) ou choisi (une ouverture, s'il existe).
            const auto folder = existingFolder(wanted.parent_path());
            if (folder.empty()) return {};
            if (spec.mode == BrowseMode::SaveFile) return utf8Of(folder / wanted.filename());
            if (folder == wanted.parent_path() && pathExists(wanted)) return utf8Of(wanted);
            return asFolder(folder);
        }

        std::string defaultTitle(BrowseMode mode) {
            switch (mode) {
                case BrowseMode::SaveFile: return "Enregistrer sous";
                case BrowseMode::Folder:   return "Choisir un dossier";
                default:                   return "Choisir un fichier";
            }
        }

        std::string defaultTooltip(BrowseMode mode, const std::string& label) {
            const std::string what = mode == BrowseMode::Folder   ? "Choisir le dossier dans l'explorateur de fichiers"
                                   : mode == BrowseMode::SaveFile ? "Choisir o\xC3\xB9 enregistrer, dans l'explorateur de fichiers"
                                                                  : "Choisir le fichier dans l'explorateur de fichiers";
            return label.empty() ? what : label + " : " + what;
        }

    } // namespace

    PathBrowse openFile(std::string filters, std::string start, std::string title) {
        PathBrowse b;
        b.mode = BrowseMode::OpenFile;
        b.filters = std::move(filters);
        b.start = std::move(start);
        b.title = std::move(title);
        return b;
    }

    PathBrowse saveFile(std::string filters, std::string start, std::string title) {
        auto b = openFile(std::move(filters), std::move(start), std::move(title));
        b.mode = BrowseMode::SaveFile;
        return b;
    }

    PathBrowse chooseFolder(std::string start, std::string title) {
        auto b = openFile({}, std::move(start), std::move(title));
        b.mode = BrowseMode::Folder;
        return b;
    }

    std::string pathIn(const std::string& folder, std::string_view name) {
        if (folder.empty()) return {};
        const auto joined = name.empty() ? pathOf(folder) : pathOf(folder) / pathOf(name);
        return utf8Of(fs::path(joined).make_preferred());
    }

    PathBrowse newFolder(std::string start, std::string title) {
        auto b = chooseFolder(std::move(start), std::move(title));
        b.newFolder = true;
        return b;
    }

    FilePick filePickFor(const PathBrowse& spec, std::string_view current) {
        FilePick pick;
        pick.title = spec.title.empty() ? defaultTitle(spec.mode) : spec.title;
        pick.save = spec.mode == BrowseMode::SaveFile;
        pick.folder = spec.mode == BrowseMode::Folder;
        if (!pick.folder) pick.filters = sdlFilters(spec.filters);
        pick.start = startFor(spec, current);
        return pick;
    }

    std::string finishPick(const PathBrowse& spec, std::string chosen, std::string_view current) {
        if (chosen.empty()) return chosen;
        if (spec.mode == BrowseMode::Folder && spec.newFolder) {
            // Le champ nomme le dossier A CREER : l'explorateur a choisi ou le
            // mettre, le nom du champ reste. Un dossier vide (tout juste cree
            // dans l'explorateur) ou du meme nom est pris tel quel.
            auto was = pathOf(cleanPath(current));
            if (!was.has_filename()) was = was.parent_path();
            const auto leaf = was.filename();
            const auto picked = pathOf(chosen);
            if (leaf.empty() || picked.filename() == leaf) return chosen;
            std::error_code ec;
            if (fs::is_directory(picked, ec) && fs::is_empty(picked, ec)) return chosen;
            return utf8Of(picked / leaf);
        }
        if (spec.mode != BrowseMode::SaveFile) return chosen;
        // L'explorateur de Windows (celui de SDL) n'ajoute pas l'extension.
        const auto p = pathOf(chosen);
        const auto ext = firstExtension(spec.filters);
        if (p.has_extension() || ext.empty() || !p.has_filename()) return chosen;
        return chosen + "." + ext;
    }

    bool browsePath(const PathBrowse& spec, std::string_view current, std::function<void(std::string)> done) {
        if (!spec.active()) return false;
        return pickFile(filePickFor(spec, current), [spec, was = std::string(current), done = std::move(done)](std::string chosen) {
            auto path = finishPick(spec, std::move(chosen), was);
            if (!path.empty() && done) done(std::move(path));
        });
    }

    // ============================================================ BrowseButton ===
    BrowseButton::BrowseButton(InputText& target, PathBrowse spec, std::string id, std::string text)
        : Button(text.empty() ? std::string("\xE2\x80\xA6") : std::move(text), std::move(id)),
          target_(&target), spec_(std::move(spec)), alive_(std::make_shared<char>('\0')) {
        setTooltip(defaultTooltip(spec_.mode, {}));
        links_ += clicked->connect([this] { (void)browse(); });
    }

    void BrowseButton::setFieldLabel(std::string label) {
        fieldLabel_ = std::move(label);
        setTooltip(defaultTooltip(spec_.mode, fieldLabel_));
    }

    SizeHint BrowseButton::sizeHint() const {
        // La hauteur d'un champ (InputText) : le bouton se range a cote de lui.
        auto h = Button::sizeHint();
        const float line = lineHeight(gfx::FontId{ 16 });
        h.preferred.h = line + 10.f;
        h.preferred.w = std::max(34.f, h.preferred.w - 4.f);
        h.minimum = h.preferred;
        h.stretchX = 0.f;
        h.stretchY = 0.f;
        return h;
    }

    bool BrowseButton::browse() {
        if (pending_ || !spec_.active() || !target_ || !target_->enabled()) return false;
        auto pick = filePickFor(spec_, target_->text());
        if (spec_.title.empty() && !fieldLabel_.empty()) pick.title = fieldLabel_;
        pending_ = true;
        setEnabled(false);                   // l'explorateur est ouvert : pas un second
        const std::weak_ptr<char> alive = alive_;
        const PathBrowse spec = spec_;
        const bool opened = pickFile(pick, [this, alive, spec](std::string answer) {
            // Le dialogue du champ a ete ferme pendant que l'explorateur etait
            // ouvert : le bouton n'est plus, la reponse ne va nulle part.
            if (alive.expired()) return;
            pending_ = false;
            setEnabled(target_->enabled());   // le champ a pu s'eteindre entretemps
            const auto path = finishPick(spec, std::move(answer), target_->text());
            if (path.empty()) return;        // annule
            // Le bouton rend le focus : Entree ne rouvre pas l'explorateur (le
            // dialogue le donne au champ - Entree y valide, comme apres une frappe).
            if (focused()) releaseFocus();
            target_->setText(path);
            chosen->emit(path);
        });
        if (!opened) {
            // Pas d'explorateur ici (essais, console) : le champ reste a taper.
            pending_ = false;
            setEnabled(target_->enabled());
        }
        return opened;
    }

} // namespace ui
