// =============================================================================
//  web/WebFilesystem.cpp - 1.12.31 : std::filesystem sur une page (pas de disque)
// -----------------------------------------------------------------------------
//  La libc++ wasm32-wasi d'ici est construite SANS <filesystem> : les operations
//  (etat d'un fichier, parcours d'un dossier, creation, suppression...) ne sont
//  pas dans la bibliotheque. La page n'a de toute facon pas de disque : le projet
//  vient de l'archive embarquee (en memoire), et ce que l'IHM ecrit (recettes,
//  remanence, journaux) va en memoire puis dans le navigateur (IndexedDB), pas par
//  std::filesystem. On fournit donc ces fonctions comme un DISQUE VIDE : rien
//  n'existe, un dossier ne contient rien, et une operation d'ecriture ne fait rien
//  (ces chemins ne sont jamais empruntes sur la page - ils sont seulement compiles).
//
//  Elles ne LEVENT jamais (sur la page, une exception arrete tout) : quand aucun
//  error_code n'est donne, elles rendent une valeur neutre au lieu de lever.
//
//  Compile seulement pour la page (XPG_WEB).
// =============================================================================
#ifdef XPG_WEB

#include <filesystem>
#include <system_error>

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace {
void clear(error_code* ec) { if (ec) ec->clear(); }
}

// ---- l'etat d'un fichier : rien n'existe -------------------------------------
file_status __status(const path&, error_code* ec) { clear(ec); return file_status{file_type::not_found, perms::none}; }
file_status __symlink_status(const path&, error_code* ec) { clear(ec); return file_status{file_type::not_found, perms::none}; }

// ---- les tailles, les dates, le compte ---------------------------------------
uintmax_t __file_size(const path&, error_code* ec) { clear(ec); return 0; }
uintmax_t __hard_link_count(const path&, error_code* ec) { clear(ec); return 0; }
file_time_type __last_write_time(const path&, error_code* ec) { clear(ec); return file_time_type::min(); }
void __last_write_time(const path&, file_time_type, error_code* ec) { clear(ec); }

// ---- les chemins -------------------------------------------------------------
path __absolute(const path& p, error_code* ec) { clear(ec); return p; }
path __canonical(const path& p, error_code* ec) { clear(ec); return p; }
path __weakly_canonical(const path& p, error_code* ec) { clear(ec); return p; }
path __read_symlink(const path& p, error_code* ec) { clear(ec); return p; }
path __current_path(error_code* ec) { clear(ec); return path("/"); }
void __current_path(const path&, error_code* ec) { clear(ec); }
path __temp_directory_path(error_code* ec) { clear(ec); return path("/tmp"); }

// ---- creer, retirer, deplacer (sans effet : pas de disque) -------------------
bool __create_directory(const path&, error_code* ec) { clear(ec); return false; }
bool __create_directory(const path&, const path&, error_code* ec) { clear(ec); return false; }
bool __create_directories(const path&, error_code* ec) { clear(ec); return false; }
void __create_directory_symlink(const path&, const path&, error_code* ec) { clear(ec); }
void __create_hard_link(const path&, const path&, error_code* ec) { clear(ec); }
void __create_symlink(const path&, const path&, error_code* ec) { clear(ec); }
bool __remove(const path&, error_code* ec) { clear(ec); return false; }
uintmax_t __remove_all(const path&, error_code* ec) { clear(ec); return 0; }
void __rename(const path&, const path&, error_code* ec) { clear(ec); }
void __resize_file(const path&, uintmax_t, error_code* ec) { clear(ec); }
void __copy(const path&, const path&, copy_options, error_code* ec) { clear(ec); }
bool __copy_file(const path&, const path&, copy_options, error_code* ec) { clear(ec); return false; }
void __copy_symlink(const path&, const path&, error_code* ec) { clear(ec); }

// ---- divers ------------------------------------------------------------------
bool __equivalent(const path&, const path&, error_code* ec) { clear(ec); return false; }
bool __fs_is_empty(const path&, error_code* ec) { clear(ec); return true; }
void __permissions(const path&, perms, perm_options, error_code* ec) { clear(ec); }
space_info __space(const path&, error_code* ec) { clear(ec); return space_info{0, 0, 0}; }

// ---- parcourir un dossier : vide ---------------------------------------------
//  Le constructeur laisse __imp_ nul : l'iterateur vaut tout de suite la fin.
directory_iterator::directory_iterator(const path&, error_code* ec, directory_options) { clear(ec); }
directory_iterator& directory_iterator::__increment(error_code* ec) { clear(ec); __imp_.reset(); return *this; }
// Jamais appele (l'iterateur vaut la fin) ; donne une entree vide par surete.
const directory_entry& directory_iterator::__dereference() const {
    static const directory_entry empty;
    return empty;
}

recursive_directory_iterator::recursive_directory_iterator(const path&, directory_options, error_code* ec) { clear(ec); }
recursive_directory_iterator& recursive_directory_iterator::__increment(error_code* ec) { clear(ec); __imp_.reset(); return *this; }
void recursive_directory_iterator::__advance(error_code* ec) { clear(ec); }
bool recursive_directory_iterator::__try_recursion(error_code* ec) { clear(ec); return false; }
void recursive_directory_iterator::__pop(error_code* ec) { clear(ec); }

// ---- une entree de dossier ---------------------------------------------------
error_code directory_entry::__do_refresh() noexcept { return std::make_error_code(std::errc::no_such_file_or_directory); }

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // XPG_WEB
