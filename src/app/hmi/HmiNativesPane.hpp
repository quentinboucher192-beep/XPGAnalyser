// =============================================================================
//  app/hmi/HmiNativesPane.hpp - 1.12.0 : LE VOLET DES NATIVES
// -----------------------------------------------------------------------------
//  IHM > Programmation generale > Natives : un onglet, une fiche a la fois (celle
//  du noeud choisi dans l'arbre, d'un lien, de F1 sur un nom dans un script).
//  Les fiches sont composees par app::natives (HmiNativesCards) ; ce volet les
//  montre, suit leurs liens (native:<cle>, notation:ST|C|C++), et pose ce qu'elles
//  decrivent dans le script montre (Inserer) ou dans le presse-papiers (Copier).
//  Rien ne s'y modifie : les natives sont verrouillees.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ui {
class HelpArticleView;
class StatusBar;
} // namespace ui

namespace app {

class HmiToolStrip;

class HmiNativesPane final : public ui::Widget {
public:
    explicit HmiNativesPane(std::string id);

    struct Hosts {
        // Ecrire dans le script (ou la fonction) en cours d'edition ; faux : aucun.
        std::function<bool(const std::string& text)> insert;
        // L'aide (le centre d'aide, sur ce sujet).
        std::function<void(const std::string& topic)> help;
        // Une autre fiche montree (un lien, Precedente) : l'arbre la suit.
        std::function<void(const std::string& key)> shown;
    };
    void setHosts(Hosts h);

    // La fiche d'une cle (app::natives) ; faux : cle inconnue, rien ne change.
    bool show(const std::string& key);
    [[nodiscard]] const std::string& current() const noexcept { return key_; }
    // Un lien d'une fiche : "native:<cle>", "notation:<N>" ; faux : rien a faire.
    bool follow(const std::string& target);
    bool back();                                     // la fiche d'avant ; faux : aucune
    bool insertCurrent();                            // Inserer ; faux : rien a inserer, ou copie a la place
    bool copyCurrent();                              // Copier ; faux : rien a copier
    // La notation des exemples (ST, C, C++ : celle de l'aide, gardee).
    [[nodiscard]] static std::string notation();
    [[nodiscard]] ui::HelpArticleView& view() noexcept { return *view_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    [[nodiscard]] std::size_t historySize() const noexcept { return history_.size(); }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void say(std::string text);
    void compose(bool keepScroll);

    HmiToolStrip*            tools_{nullptr};
    ui::HelpArticleView*     view_{nullptr};
    ui::StatusBar*           status_{nullptr};
    Hosts                    hosts_;
    std::string              key_;
    std::vector<std::string> history_;               // les fiches d'avant (Precedente)
    std::string              message_;
    core::ConnectionScope    links_;
};

} // namespace app
