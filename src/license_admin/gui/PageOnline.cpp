// =============================================================================
//  license_admin/gui/PageOnline.cpp - 1.33.0 : LES PAGES DE LA VENTE EN LIGNE
//  (Boutique, Commandes)
// -----------------------------------------------------------------------------
//  1.35.0 : le service des licences existe (Licences flottantes est devenue une
//  vraie page : PageFloating.cpp). La Boutique et les Commandes demandent en plus
//  un PAIEMENT EN LIGNE (un prestataire, ses confirmations), qui n'est pas fait.
//  Chaque page le DIT, honnetement : ce qu'elle fera (la maquette, section 11),
//  pourquoi ce n'est pas encore fait, et ce qu'on fait en attendant (un bouton
//  vers la page qui marche aujourd'hui). Rien n'est simule : aucune commande,
//  aucun montant.
// =============================================================================
#include "Page.hpp"

#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

using gfx::FontId;
using gfx::Rect;

struct OnlineText {
    const char*              id;
    const char*              title;
    const char*              lead;
    std::vector<const char*> will;     // ce que fera la page
    std::vector<const char*> why;      // pourquoi il faut le service en ligne
    const char*              meanwhile;
    const char*              gotoLabel;
    const char*              gotoPage;
};

const OnlineText& textFor(const std::string& which) {
    static const std::vector<OnlineText> all = {
        {"boutique",
         "Boutique",
         "La vente en ligne : le client choisit une \xC3\xA9" "dition, paie, et re\xC3\xA7oit sa licence sans attendre que tu la signes \xC3\xA0 la main.",
         {"Le catalogue : les \xC3\xA9" "ditions \xC3\xA0 vendre (d'apr\xC3\xA8s le registre), leurs prix, leurs dur\xC3\xA9" "es.",
          "Le paiement par un prestataire (carte bancaire) : aucune donn\xC3\xA9" "e de carte ne passe par ce poste.",
          "La licence sign\xC3\xA9" "e et envoy\xC3\xA9" "e d\xC3\xA8s que le paiement est confirm\xC3\xA9, puis rang\xC3\xA9" "e dans l'archive.",
          "Les renouvellements et les passages \xC3\xA0 une \xC3\xA9" "dition sup\xC3\xA9rieure."},
         {"Un paiement est confirm\xC3\xA9 en ligne, par un prestataire (carte bancaire), \xC3\xA0 n'importe quelle heure : le service des licences "
          "devrait recevoir cette confirmation et produire la licence et son code tout de suite.",
          "Le service des licences sait d\xC3\xA9j\xC3\xA0 activer une licence par un code ; c'est le paiement qui n'y est pas branch\xC3\xA9 (aucun "
          "prestataire choisi, aucun compte)."},
         "Vends sur devis et facture, puis cr\xC3\xA9" "e la licence en ligne ici et donne son code au client (Licences \xE2\x80\xBA Nouvelle licence en ligne\xE2\x80\xA6).",
         "Aller aux licences",
         "licences"},
        {"commandes",
         "Commandes",
         "Les achats faits dans la boutique en ligne : qui a achet\xC3\xA9 quoi, quand, et si le paiement est pass\xC3\xA9.",
         {"La liste des commandes : client, \xC3\xA9" "dition, montant, date, \xC3\xA9tat du paiement.",
          "La licence de chaque commande, et la renvoyer au client si besoin.",
          "Les remboursements : la licence r\xC3\xA9voqu\xC3\xA9" "e en m\xC3\xAAme temps.",
          "L'export pour la comptabilit\xC3\xA9."},
         {"Une commande na\xC3\xAEt dans la boutique en ligne et son paiement est confirm\xC3\xA9 par le prestataire : sans paiement en ligne, il n'y a "
          "aucune commande \xC3\xA0 montrer.",
          "Rien n'est invent\xC3\xA9 ici : pas de commande d'exemple, pas de montant."},
         "Chaque licence sign\xC3\xA9" "e est dans l'archive (Licences), et chaque op\xC3\xA9ration dans l'historique.",
         "Voir l'historique",
         "historique"},
    };
    for (const auto& t : all)
        if (which == t.id) return t;
    return all.front();
}

// Un texte coupe en lignes a la largeur (mesure des services de plate-forme).
std::vector<std::string> wrap(const std::string& text, FontId f, float w) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i <= text.size()) {
        const std::size_t sp = text.find(' ', i);
        const std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        if (sp == std::string::npos) break;
        i = sp + 1;
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}

class OnlinePage final : public Page {
public:
    OnlinePage(ShellHost host, const std::string& which) : Page("page." + std::string(textFor(which).id), std::move(host)), t_(textFor(which)) {
        auto go = std::make_unique<ui::Button>(t_.gotoLabel, std::string("en-ligne.") + t_.id + ".aller");
        go->setIcon(ui::Icon::GoTo);
        go_ = &static_cast<ui::Button&>(addChild(std::move(go)));
        links_ += go_->clicked->connect([this] { host_.navigate(t_.gotoPage); });
    }
    [[nodiscard]] std::string title() const override { return t_.title; }
    void refresh() override { invalidate(); }   // rien a relire : la page ne montre aucune donnee

protected:
    enum class Ink : std::uint8_t { Text, Muted, Warning, Heading };
    enum class Kind : std::uint8_t { Line, Bold, Dot, Card, WarnCard, Icon };
    struct Item {
        Kind        kind{Kind::Line};
        Rect        r{};
        std::string text;
        FontId      font{16};
        Ink         ink{Ink::Text};
        ui::Icon    icon{ui::Icon::None};
    };

    void onLayout() override {
        items_.clear();
        const Rect  b = bounds();
        const float pad = b.w < 700.f ? 16.f : 24.f;
        const float x0 = b.x + pad, w = std::max(200.f, std::min(b.w - 2 * pad, 1100.f));
        const float top = b.y + 20.f - scroll_;
        float       y = top;
        const auto  line = [&](const std::string& s, float x, float yy, FontId f, Ink ink, Kind k = Kind::Line) {
            items_.push_back({k, {x, yy, 0.f, 0.f}, s, f, ink, ui::Icon::None});
        };
        const auto para = [&](const std::string& s, float x, float width, FontId f, Ink ink, float lh) {
            for (const auto& l : wrap(s, f, width)) {
                line(l, x, y, f, ink);
                y += lh;
            }
        };
        // Le titre, la phrase d'explication.
        line(t_.title, x0, y, FontId{26}, Ink::Heading, Kind::Bold);
        y += 38.f;
        para(t_.lead, x0, w, FontId{16}, Ink::Muted, 22.f);
        y += 14.f;

        // Pas encore fait : le cadre ambre.
        {
            const float cardY = y;
            const std::size_t at = items_.size();
            items_.push_back({Kind::WarnCard, {}, {}, FontId{16}, Ink::Warning, ui::Icon::None});
            items_.push_back({Kind::Icon, {x0 + 16.f, y + 16.f, 18.f, 18.f}, {}, FontId{16}, Ink::Warning, ui::Icon::Warning});
            y += 14.f;
            line("Pas encore fait", x0 + 44.f, y, FontId{17}, Ink::Warning, Kind::Bold);
            y += 24.f;
            para("Cette page demande un paiement en ligne (un prestataire de paiement), pas encore branch\xC3\xA9 sur le service des licences. "
                 "Rien n'y est simul\xC3\xA9 : aucune commande, aucun montant.",
                 x0 + 44.f, w - 60.f, FontId{15}, Ink::Text, 20.f);
            y += 12.f;
            items_[at].r = {x0, cardY, w, y - cardY};
            y += 16.f;
        }

        // Les deux cadres : ce que fera la page, pourquoi un service en ligne.
        const bool side = w >= 720.f;
        const float colW = side ? (w - 16.f) * 0.5f : w;
        const auto card = [&](const char* heading, const std::vector<const char*>& bullets, float x, float startY) {
            const std::size_t at = items_.size();
            items_.push_back({Kind::Card, {}, {}, FontId{16}, Ink::Text, ui::Icon::None});
            float yy = startY + 14.f;
            items_.push_back({Kind::Bold, {x + 16.f, yy, 0.f, 0.f}, heading, FontId{16}, Ink::Heading, ui::Icon::None});
            yy += 28.f;
            for (const char* bullet : bullets) {
                items_.push_back({Kind::Dot, {x + 18.f, yy + 7.f, 6.f, 6.f}, {}, FontId{15}, Ink::Muted, ui::Icon::None});
                for (const auto& l : wrap(bullet, FontId{15}, colW - 52.f)) {
                    items_.push_back({Kind::Line, {x + 34.f, yy, 0.f, 0.f}, l, FontId{15}, Ink::Text, ui::Icon::None});
                    yy += 20.f;
                }
                yy += 6.f;
            }
            yy += 8.f;
            items_[at].r = {x, startY, colW, yy - startY};
            return yy;
        };
        const float startY = y;
        const std::size_t first = items_.size();
        const float endA = card("Ce que fera cette page", t_.will, x0, startY);
        const std::size_t second = items_.size();
        float endB;
        if (side) {
            endB = card("Pourquoi ce n'est pas encore fait", t_.why, x0 + colW + 16.f, startY);
            const float h = std::max(endA, endB) - startY;   // les deux cadres a la meme hauteur
            items_[first].r.h = h;
            items_[second].r.h = h;
            y = startY + h + 16.f;
        } else {
            endB = card("Pourquoi ce n'est pas encore fait", t_.why, x0, endA + 12.f);
            y = endB + 16.f;
        }

        // En attendant : ce qui marche aujourd'hui.
        {
            const float cardY = y;
            const std::size_t at = items_.size();
            items_.push_back({Kind::Card, {}, {}, FontId{16}, Ink::Text, ui::Icon::None});
            y += 14.f;
            line("En attendant", x0 + 16.f, y, FontId{16}, Ink::Heading, Kind::Bold);
            y += 26.f;
            const float bw = go_->sizeHint().preferred.w;
            para(t_.meanwhile, x0 + 16.f, w - 48.f - bw, FontId{15}, Ink::Text, 20.f);
            go_->setBounds({x0 + w - 16.f - bw, cardY + 16.f, bw, 32.f});
            y = std::max(y, cardY + 56.f) + 12.f;
            items_[at].r = {x0, cardY, w, y - cardY};
        }
        contentH_ = y + scroll_ - b.y + 20.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        ctx.r.fillRect(bounds(), c.windowBg);
        const auto ink = [&](Ink i) {
            switch (i) {
                case Ink::Muted: return c.textMuted;
                case Ink::Warning: return c.warning;
                case Ink::Heading: return c.text;
                case Ink::Text: break;
            }
            return c.text;
        };
        for (const auto& it : items_) {
            switch (it.kind) {
                case Kind::Card:
                    ctx.r.fillRoundedRect(it.r, th.brand.cardBorder, 8.f);
                    ctx.r.fillRoundedRect(it.r.inset(1.f, 1.f), th.brand.card, 7.f);
                    break;
                case Kind::WarnCard:
                    ctx.r.fillRoundedRect(it.r, c.warning.withAlpha(90), 8.f);
                    ctx.r.fillRoundedRect(it.r.inset(1.f, 1.f), c.panelBg, 7.f);
                    ctx.r.fillRoundedRect(it.r.inset(1.f, 1.f), c.warning.withAlpha(22), 7.f);
                    break;
                case Kind::Icon: ui::drawIcon(ctx.r, it.icon, it.r, ink(it.ink)); break;
                case Kind::Dot: ctx.r.fillRoundedRect(it.r, ink(it.ink), 3.f); break;
                case Kind::Bold:
                    ctx.r.drawText({it.r.x, it.r.y}, it.text, it.font, ink(it.ink));
                    ctx.r.drawText({it.r.x + 0.6f, it.r.y}, it.text, it.font, ink(it.ink));
                    break;
                case Kind::Line: ctx.r.drawText({it.r.x, it.r.y}, it.text, it.font, ink(it.ink)); break;
            }
        }
        // Plus bas que la page : une barre fine dit qu'on peut descendre (la molette).
        const Rect b = bounds();
        if (contentH_ > b.h + 1.f) {
            const float frac = b.h / contentH_;
            const float barH = std::max(30.f, b.h * frac);
            const float barY = b.y + (b.h - barH) * (scroll_ / std::max(1.f, contentH_ - b.h));
            ctx.r.fillRoundedRect({b.right() - 6.f, barY, 4.f, barH}, c.scrollbar, 2.f);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            const float maxScroll = std::max(0.f, contentH_ - bounds().h);
            const float next = std::clamp(scroll_ - w->dy * 48.f, 0.f, maxScroll);
            if (next != scroll_) {
                scroll_ = next;
                invalidateLayout();
            }
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    const OnlineText&     t_;
    ui::Button*           go_{nullptr};
    std::vector<Item>     items_;
    float                 scroll_{0.f}, contentH_{0.f};
    core::ConnectionScope links_;
};

} // namespace

PagePtr makeOnlinePage(ShellHost host, const std::string& which) { return std::make_unique<OnlinePage>(std::move(host), which); }

} // namespace license_admin::gui
