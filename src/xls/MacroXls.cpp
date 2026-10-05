// =============================================================================
//  xls/MacroXls.cpp
// =============================================================================
#include "MacroXls.hpp"

#include <algorithm>
#include <filesystem>

namespace xls {

    namespace {
        // La date de modification et la taille d'un fichier, en texte : de quoi
        // savoir qu'il a change depuis qu'on l'a lu. Vide s'il est illisible.
        std::string stampOf(const std::string& path) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec) return {};
            const auto when = std::filesystem::last_write_time(path, ec);
            if (ec) return {};
            return std::to_string(size) + "@" + std::to_string(when.time_since_epoch().count());
        }
    } // namespace

    MacroXls& MacroXls::instance() {
        static MacroXls one;
        return one;
    }

    void MacroXls::setLoadedWorkbook(std::string path) { loaded_ = std::move(path); }

    int MacroXls::openWorkbook(const std::string& path, std::string& error) {
        error.clear();
        const std::string wanted = path.empty() ? loaded_ : path;
        if (wanted.empty()) {
            error = "aucun classeur choisi : donne-le dans le formulaire de la macro "
                "(bouton ..., Ctrl+V ou glisser le fichier), ou son chemin a OpenWorkbook";
            return -1;
        }

        // Deja ouvert et c'est le meme fichier : on ne relit pas un megaoctet parce
        // qu'une macro enchainee redemande le meme classeur.
        //
        // LOT MACROS 1 : SAUF S'IL A CHANGE SUR LE DISQUE. Le formulaire relance
        // la macro a chaque reponse ; on corrige le classeur dans Excel, on
        // l'enregistre, on relance - et l'on relisait l'ancien, garde en memoire.
        // La date et la taille du fichier le disent pour presque rien.
        const auto stamp = stampOf(wanted);
        if (open_ && openedFrom_ == wanted && stamp == openedStamp_) return 0;

        auto opened = Workbook::open(wanted);
        if (!opened) {
            error = opened.error().message();
            return -1;
        }
        wb_ = std::move(*opened);
        open_ = true;
        openedFrom_ = wanted;
        openedStamp_ = stamp;
        handles_.clear();
        // Le dernier classeur ouvert avec succes devient celui auquel `''` renvoie.
        // C'est ce qui permet a `ImporterClasseur` de ne demander le chemin qu'une
        // fois : la premiere macro le donne, les suivantes passent la chaine vide.
        loaded_ = wanted;
        return 0;
    }

    int MacroXls::openSheet(const std::string& name) {
        if (!open_) return -1;
        const Sheet* s = wb_.sheet(name);
        if (s == nullptr) return -1;

        // Un meme onglet demande deux fois rend le meme identifiant : une macro qui
        // ouvre `ES` dans deux boucles ne doit pas faire grossir la table.
        for (std::size_t i = 0; i < handles_.size(); ++i)
            if (handles_[i] == s) return kFirstHandle + static_cast<int>(i);

        handles_.push_back(s);
        return kFirstHandle + static_cast<int>(handles_.size()) - 1;
    }

    const Sheet* MacroXls::resolve(int handle) const {
        if (!open_ || handle < kFirstHandle) return nullptr;
        const auto index = static_cast<std::size_t>(handle - kFirstHandle);
        if (index >= handles_.size()) return nullptr;
        return handles_[index];
    }

    int MacroXls::rowCount(int handle) const {
        const Sheet* s = resolve(handle);
        return s == nullptr ? 0 : static_cast<int>(s->rowCount());
    }

    std::string MacroXls::cell(int handle, int row, const std::string& column) const {
        const Sheet* s = resolve(handle);
        if (s == nullptr || row < 0) return {};
        return s->cell(static_cast<std::size_t>(row), column);
    }

    bool MacroXls::hasColumn(int handle, const std::string& column) const {
        const Sheet* s = resolve(handle);
        return s != nullptr && s->hasColumn(column);
    }

    bool MacroXls::skipRow(int handle, int row) const {
        (void)handle;
        (void)row;
        return false;
    }

    std::string MacroXls::setting(const std::string& key) const {
        if (!open_) return {};
        return wb_.setting(key);
    }

    std::vector<std::string> MacroXls::sheetNames() const {
        if (!open_) return {};
        return wb_.sheetNames();
    }

    void MacroXls::close() {
        wb_ = Workbook{};
        open_ = false;
        openedFrom_.clear();
        openedStamp_.clear();
        handles_.clear();
    }

} // namespace xls