// =============================================================================
//  app/hmi/HmiSessionPane.hpp - 1.45.0 : L'ONGLET << SESSION >> DU PANNEAU DU BAS
// -----------------------------------------------------------------------------
//  Le client : << a cote de sorties, performances, erreur etc, ajouter 'chat de
//  session' que quand une session est active >>. L'onglet n'existe QUE pendant
//  une session (HmiBuildOutputPane l'ajoute quand elle commence, le retire quand
//  elle finit) ; sa pastille : les messages des autres pas encore lus.
//
//  CE QU'IL MONTRE (app/SessionMarks : le Board que le service des sessions
//  remplit) : la discussion et le JOURNAL de la session (qui a change quoi,
//  quand : << 14:32 Lea : Vue_Accueil > Texte_Etat deplace >>), meles dans
//  l'ordre des heures, ou filtres (Tout / Discussion / Journal). Le champ du bas
//  ecrit a l'equipe (Entree ou Envoyer), comme la fenetre de la session ; l'hote
//  exporte le journal (CSV, dans exports/ du projet).
//
//  Sans ecran : les essais lisent les lignes montrees (rowCount, rowText) et
//  ecrivent par send().
// =============================================================================
#pragma once

#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiSessionPane final : public ui::Widget {
public:
    enum class Filter { All, Chat, Journal };

    explicit HmiSessionPane(std::string id);
    // Relit ce que montre la session (si elle a change) ; a chaque image.
    void refresh();
    void setFilter(Filter f);
    [[nodiscard]] Filter filter() const noexcept { return filter_; }
    // Les lignes montrees : "14:32 Lea : bonjour" ; le journal : "14:32 Lea : Vue_Accueil > Texte_Etat deplace (x3)".
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::string rowText(std::size_t i) const;
    // Ecrire a l'equipe (le champ, Entree) ; faux : rien a envoyer.
    bool send(const std::string& text);
    [[nodiscard]] ui::InputText* field() noexcept { return field_; }
    [[nodiscard]] ui::TableView* table() noexcept { return table_; }
    // Dit dans le pied (un export, un refus) ; la barre d'etat du panneau le montre.
    std::function<void(const std::string&, bool ok)> said;

    struct Row {
        std::string time, who, text;
        std::uint32_t color{0};
        bool journal{false}, system{false}, mine{false};
        int count{1};
    };

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuild();
    std::vector<Row> rows_;
    Filter filter_{Filter::All};
    std::uint64_t seen_{~0ull};
    ui::Button* all_{nullptr};
    ui::Button* chat_{nullptr};
    ui::Button* journal_{nullptr};
    ui::Button* export_{nullptr};
    ui::Button* sendButton_{nullptr};
    ui::InputText* field_{nullptr};
    ui::TableView* table_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::string title_;
    float titleAt_{0.f};
    core::ConnectionScope links_;
};

} // namespace app
