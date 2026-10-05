// =============================================================================
//  app/BackgroundTasks.hpp - lot API 8 : bandeau haut, les taches de fond
// -----------------------------------------------------------------------------
//  Une generation, un import, un export qui tourne s'inscrit ici ; le bandeau
//  haut en montre une petite jauge ("Generation IHM 62 %"), un clic donne le
//  detail et Annuler. L'API minimale, sure entre fils :
//
//      const int id = bgtasks::begin("G\xC3\xA9n\xC3\xA9ration IHM", "hmi.generate.cancel");
//      bgtasks::progress(id, 0.62f);        // 0..1 ; negatif : on ne sait pas
//      bgtasks::end(id);                    // fini (ou annule)
//
//  L'action d'annulation (facultative) part par la barre du haut comme ses
//  autres actions (MainAnalysisScreen::onBarAction). bgtasks::Scope fait
//  begin / end sur une portee.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace app::bgtasks {

struct Task {
    int         id{0};
    std::string label;
    float       progress{-1.f};
    std::string cancelAction;
};

int  begin(std::string label, std::string cancelAction = {});
void progress(int id, float fraction);
void end(int id);
[[nodiscard]] std::vector<Task> list();     // dans l'ordre d'inscription

// ---- les avis de la cloche -------------------------------------------------
//  Ce qui merite l'attention, venu d'ailleurs que la simulation (SimStatus en
//  fait sa propre liste) : un Compiler qui laisse des expressions
//  impossibles, des mises a jour de bibliotheque, un export termine. Une cle
//  par avis : post() remplace celui de meme cle, withdraw() le retire (le
//  probleme est regle). `action` : l'action du bouton, par la barre du haut.
struct Notice {
    std::string key, group, title, detail, button, action;
    std::string tone{"info"};             // "error", "warning", "ok", "info"
};
void post(Notice n);
void withdraw(const std::string& key);
[[nodiscard]] std::vector<Notice> notices();

// Change a chaque begin / progress / end / post / withdraw : le bandeau se
// rafraichit tout de suite (sinon une fois par seconde).
[[nodiscard]] unsigned long long revision();

class Scope {
public:
    explicit Scope(std::string label, std::string cancelAction = {}) : id_(begin(std::move(label), std::move(cancelAction))) {}
    ~Scope() { end(id_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void progress(float fraction) const { bgtasks::progress(id_, fraction); }
    [[nodiscard]] int id() const noexcept { return id_; }

private:
    int id_;
};

} // namespace app::bgtasks
