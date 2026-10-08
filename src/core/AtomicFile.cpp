#include "AtomicFile.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace core {

namespace fs = std::filesystem;

bool readFileAll(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    return static_cast<bool>(in) || in.eof();
}

Status writeFileAtomic(const fs::path& target, std::string_view content, const AtomicWrite& how) {
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail(ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + tmp.string() + " (dossier en lecture seule, ou acc\xC3\xA8s refus\xC3\xA9)");
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) return fail(ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + tmp.string() + " (disque plein ?)");
    }
    if (how.verify) {
        std::string back;
        if (!readFileAll(tmp, back) || back != content) {
            fs::remove(tmp, ec);
            return fail(ErrorCode::FileUnreadable, "le fichier temporaire relu ne correspond pas : " + tmp.string() + " (rien n'est remplac\xC3\xA9)");
        }
    }
    if (how.keepBackup && fs::exists(target, ec)) fs::copy_file(target, target.string() + ".bak", fs::copy_options::overwrite_existing, ec);
    fs::rename(tmp, target, ec);
    if (ec) {
        // Windows refuse de renommer par-dessus un fichier ouvert ailleurs : on retire
        // l'ancien (il est dans le .bak) et on reessaie.
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return fail(ErrorCode::FileUnreadable, "remplacement impossible : " + target.string() + " (" + ec.message() + ")");
    }
    return ok();
}

} // namespace core
