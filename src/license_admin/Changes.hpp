// =============================================================================
//  license_admin/Changes.hpp - 1.48.1 : << QUELQUE CHOSE A CHANGE >> (tout
//  l'Admin ; docs/STATISTIQUES.md, par. 15)
// -----------------------------------------------------------------------------
//  Un seul numero pour tout l'Admin : +1 apres chaque ecriture reussie (une
//  suppression, une modification, une publication, une livraison...), apres
//  << Actualiser >> et << Reevaluer >>. Chaque page et chaque vue des
//  statistiques garde le numero de ce qu'elle montre et se relit quand il a
//  change : rien de vieux jusqu'au redemarrage.
//  - QUI L'AVANCE : Shell::report (toute operation reussie), la file du journal
//    des admins (detail::queueJournal : toute action journalisee), et
//    changes::bump() la ou une ecriture reussie ne passe par aucun des deux.
//  - QUI LE LIT : changes::current() ; changes::Seen (changed() : vrai une fois
//    par changement) ; les pages des statistiques le mettent dans la signature
//    de leurs chiffres.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>

namespace license_admin::changes {

namespace detail {
inline std::atomic<std::uint64_t>& counter() noexcept {
    static std::atomic<std::uint64_t> c{1};
    return c;
}
} // namespace detail

// Le numero du dernier changement (jamais 0).
[[nodiscard]] inline std::uint64_t current() noexcept { return detail::counter().load(std::memory_order_acquire); }

// Quelque chose a change (une ecriture reussie) : toutes les vues se reliront. Sur n'importe quel fil.
inline void bump() noexcept { detail::counter().fetch_add(1, std::memory_order_acq_rel); }

// Ce qu'une vue a deja vu : changed() est vrai une fois apres chaque changement (et au premier appel).
class Seen {
public:
    [[nodiscard]] bool changed() noexcept {
        const std::uint64_t c = current();
        if (c == seen_) return false;
        seen_ = c;
        return true;
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return seen_; }
    void forget() noexcept { seen_ = 0; }

private:
    std::uint64_t seen_{0};
};

} // namespace license_admin::changes
