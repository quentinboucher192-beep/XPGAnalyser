// =============================================================================
//  app/hmi/HmiExprPageView.hpp - 1.11 (chantier T3, D5) : la page des
//                                expressions a l'ecran
// -----------------------------------------------------------------------------
//  Au centre, l'article du type (app::exprpage::article, HelpArticleView) ; a
//  droite, « Essaie ici » : le champ d'essai, le resultat et son type, ou la
//  raison soulignee et « Remplacer par ... ». Le champ est juge a chaque frappe
//  par le vrai moteur (hmi::exprguide::Bench), sur les variables d'exemple ou
//  sur le projet ouvert (onProject).
//
//  POUR T2 : la zone de page du centre d'aide cree la page et appelle
//  show("expr-<cle>") ; la page n'a pas d'arbre a elle (le centre liste les 11
//  sujets) ; un lien vers un autre sujet du centre remonte par `openTopic`.
// =============================================================================
#pragma once

#include "HmiExprPage.hpp"
#include "../../hmi/HmiExprBench.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/HelpArticleView.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace app {

class HmiExprPageView final : public ui::Widget {
public:
    explicit HmiExprPageView(std::string id);

    // "couleur" ou "expr-couleur" ; faux : type inconnu (la page ne change pas).
    bool show(std::string_view key);
    [[nodiscard]] const hmi::exprguide::TypeEntry* current() const noexcept { return type_; }

    // Mettre une source dans le champ et la juger (un exemple, le faux ou le juste
    // d'une erreur) ; `typeKey` : le type de la case (vide : celui de la page).
    void tryIt(std::string_view source, std::string_view typeKey = {});
    // « Remplacer par » : le champ prend la correction sure ; faux : aucune.
    bool applyReplacement();
    [[nodiscard]] const hmi::exprguide::Outcome& outcome() const noexcept { return outcome_; }
    // Tranche 13 (decision du chef, 03/10) : le texte du champ d'essai, que lit le chemin
    // essai.texte de la scene de T1 (l'« A toi » d'un type d'expression verifie ce que
    // l'utilisateur a TAPE, pas le premier exemple que show() y met). Tel qu'un @verifier le
    // compare : sans espaces en tete ni en queue, chaque suite d'espaces reduite a une seule,
    // sauf entre apostrophes ou guillemets ('Vanne  ' reste tel quel). Champ vide : "".
    [[nodiscard]] std::string trialText() const;

    // Le projet ouvert (lecture seule) ou les variables d'exemple.
    void onProject(const hmi::Project& project) { bench_.onProject(project, nullptr); judge(); }
    void onSamples() { bench_.onSamples(); judge(); }
    // Vrai : le champ juge sur les variables d'exemple ; faux : sur le projet ouvert.
    [[nodiscard]] bool usesSamples() const noexcept { return bench_.usesSamples(); }
    // Tranche 8 : ce que la page dit a cote du resultat (vide : les variables d'exemple).
    [[nodiscard]] std::string sourceNote() const { return bench_.sourceNote(); }

    [[nodiscard]] ui::HelpArticleView& article() noexcept { return *view_; }
    [[nodiscard]] ui::InputText&       field() noexcept { return *field_; }

    // Un lien que la page ne traite pas (un sujet du centre) : l'hote l'ouvre.
    const core::SignalPtr<const std::string&> openTopic = core::Signal<const std::string&>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void judge();
    void onLink(const std::string& target);
    void paintSourceNote(const ui::PaintContext& ctx, float x, float y, float w, float stop) const;

    static constexpr float kSide = 340.f;      // la colonne « Essaie ici »

    const hmi::exprguide::TypeEntry* type_{nullptr};
    const hmi::exprguide::TypeEntry* caseType_{nullptr};   // le type de la case essayee (caseKey)
    hmi::exprguide::Bench   bench_;
    hmi::exprguide::Outcome outcome_;
    ui::HelpArticleView*    view_{nullptr};
    ui::InputText*          field_{nullptr};
    ui::Button*             replace_{nullptr};
    core::ConnectionScope   links_;
};

} // namespace app
