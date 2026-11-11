// =============================================================================
//  license_admin/gui/ShellShop.cpp - 1.46.0 : LA COQUILLE ET LA BOUTIQUE
//  (Shell.hpp ; docs/BOUTIQUE.md, par. 5) - la livraison automatique des
//  commandes payees quand l'Admin est ouvert
// -----------------------------------------------------------------------------
//  Le regard (toutes les 15 s, Shell.cpp) voit le compteur des commandes bouger :
//  autoOrders relit les commandes et le reglage sans bruit (adoptes par le
//  Workspace : les pages, le tableau de bord, le nombre du menu) ; une commande
//  payee pas encore annoncee : un avis. << Livrer seul >> coche et cet Admin qui
//  peut signer : autoDeliver lance la livraison (Workspace::deliverOrders, sur
//  un fil a part, dans un Workspace neuf) - un travail lent qui se voit (le
//  haut de la fenetre, le journal de la seance) ; une commande livree : un avis ;
//  a revoir : un avis ambre. Une panne (le reseau) : de nouveau dans 5 minutes.
// =============================================================================
#include "Shell.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

namespace license_admin::gui {

namespace {
double steadyClock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// L'admin+, ou un admin avec << demandes >> et << licence_creer >> (docs/BOUTIQUE.md, par. 5).
bool canDeliver(const Workspace& w) { return w.signer() && w.service().ready() && w.whyNotDeliver("edition").empty(); }
} // namespace

bool Shell::autoOrders() {
    if (bg_.running() || !ws_->service().ready() || !ws_->signer() || !ws_->can("demandes")) return false;
    const Workspace* w = ws_.get();
    auto s = std::make_shared<shop::Settings>();
    auto v = std::make_shared<shop::OrdersView>();
    auto gotSettings = std::make_shared<bool>(false);
    if (!runSlow("Boutique : relev\xC3\xA9 des commandes",
                 [w, s, v, gotSettings] {
                     *gotSettings = w->shopSettings(*s).ok;
                     return w->shopOrders(*v);
                 },
                 [this, s, v, gotSettings](const OpResult& r) {
                     if (*gotSettings) ws_->adoptShop(*s);
                     if (!r.ok) return;
                     ws_->adoptOrders(*v);
                     toDeliver_ = v->toDeliver + v->toValidate;   // 1.48.0 : et les lignes a valider (les demandes)
                     refreshPages();
                     std::vector<const shop::Order*> fresh;
                     for (const auto& o : v->orders)
                         if (o.toDeliver() && std::find(seenPaid_.begin(), seenPaid_.end(), o.number) == seenPaid_.end()) {
                             fresh.push_back(&o);
                             seenPaid_.push_back(o.number);
                         }
                     const bool automatic = *gotSettings && s->autoDeliver && canDeliver(*ws_);
                     if (toDeliver_ > 0 && automatic) {
                         wantDelivery_ = true;   // la livraison dira elle-meme ce qu'elle a livre
                         return;
                     }
                     if (fresh.empty()) return;
                     if (fresh.size() == 1) {
                         const auto& o = *fresh.front();
                         flash("Nouvelle commande pay\xC3\xA9" "e : n\xC2\xB0 " + std::to_string(o.number) + " " + o.company + " (" + (o.label.empty() ? o.article : o.label) + ", " +
                                   shop::amountText(o.amount, o.currency) + ") \xE2\x80\x94 Commandes et demandes \xE2\x80\xBA D\xC3\xA9livrer maintenant.",
                               Tone::Warning);
                     } else {
                         flash(std::to_string(fresh.size()) + " nouvelles commandes pay\xC3\xA9" "es \xE2\x80\x94 Commandes et demandes \xE2\x80\xBA D\xC3\xA9livrer maintenant.", Tone::Warning);
                     }
                 }))
        return false;
    quietSlow_ = true;   // sans bruit : seul l'avis parle
    return true;
}

bool Shell::autoDeliver() {
    if (bg_.running() || !canDeliver(*ws_)) return false;
    const Workspace* w = ws_.get();
    auto rep = std::make_shared<DeliveryReport>();
    if (!runSlow("Boutique : livraison des commandes pay\xC3\xA9" "es", [w, rep] { return w->deliverOrders(0, true, rep.get()); },
                 [this, rep](const OpResult& r) {
                     ws_->reload();   // la licence signee par l'autre Workspace (l'archive, le registre des activations)
                     wantOrders_ = true;
                     wantLicences_ = true;
                     // une panne qui passera (le reseau, le service) : de nouveau dans 5 minutes ; une commande qu'un autre Admin
                     // livrera (un droit qui manque ici) ne fait pas recommencer.
                     if (!r.ok && rep->review == 0) nextDelivery_ = steadyClock() + kDeliveryRetry;
                     for (const auto& l : rep->lines) {
                         if (l.delivered)
                             flash("Commande n\xC2\xB0 " + std::to_string(l.number) + " livr\xC3\xA9" "e : licence " + l.licence + " (" + l.customer + ", " + l.article + ").", Tone::Ok);
                         else if (l.review)
                             flash("Commande n\xC2\xB0 " + std::to_string(l.number) + " \xC3\xA0 revoir : " + l.problem + " \xE2\x80\x94 Commandes et demandes.", Tone::Warning);
                     }
                     refreshPages();
                 }))
        return false;
    return true;   // un travail qui se voit (il ecrit l'archive) : pas sans bruit
}

} // namespace license_admin::gui
