// =============================================================================
//  app/screens/FoldersDialog.cpp - 1.8.0 : les dossiers de l'application
// -----------------------------------------------------------------------------
//  Voir Screens.hpp (FoldersDialog) et app/Dossiers.hpp (la regle).
//
//  Une ligne par dossier : son nom, son chemin (un champ, pour les quatre qui
//  se changent), Dossier... (l'explorateur de l'appli ou du systeme, comme
//  partout) et Ouvrir (l'Explorateur de Windows) ; dessous, d'ou il vient.
//  Un champ laisse tel quel ne change rien ; un champ vide revient au defaut.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../Dossiers.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace app {

using namespace ui;

namespace {

menu::MenuTraits traitsOfDialog() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = false;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

// Le cadre (comme DialogFrame de Dialogs.cpp : un petit double, plutot qu'un en-tete commun).
class Frame final : public Widget {
public:
    Frame(std::string title, float w, float h) : title_(std::move(title)), width_(w), height_(h) {}
    void attachBody(WidgetPtr b) { body_ = &addChild(std::move(b)); }
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(width_, std::max(240.f, r.w - 24.f));
        const float h = std::min(height_, std::max(120.f, r.h - 24.f));
        panel_ = {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h};
        if (body_) body_->setBounds({panel_.x + 14.f, panel_.y + 40.f, panel_.w - 28.f, panel_.h - 54.f});
    }
    void onPaint(const PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        ctx.r.drawText({panel_.x + 12.f, panel_.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, title_,
                       ctx.theme.font.uiBold, c.text);
    }
private:
    std::string title_;
    float       width_, height_;
    gfx::Rect   panel_{};
    Widget*     body_{nullptr};
};

// Un texte d'une ligne : le nom d'un dossier (gras, largeur fixe), un chemin, une note.
class Text final : public Widget {
public:
    enum class Kind : std::uint8_t { Name, Path, Caption, Intro, Note, Warning };
    Text(std::string text, Kind kind, float width = 0.f) : text_(std::move(text)), kind_(kind), width_(width) {}
    void setText(std::string t, Kind k) {
        text_ = std::move(t);
        kind_ = k;
        invalidate();
    }
    [[nodiscard]] SizeHint sizeHint() const override {
        const gfx::FontId f = font();
        const float line = lineHeight(f);
        SizeHint h;
        const float w = width_ > 0.f ? width_ : measureWidth(text_, f) + 8.f;
        h.preferred = {w, line + 6.f};
        h.minimum = {width_ > 0.f ? width_ : 40.f, line + 4.f};
        h.stretchX = width_ > 0.f ? 0.f : 1.f;
        return h;
    }
protected:
    void onPaint(const PaintContext& ctx) override {
        const auto r = contentRect();
        const gfx::FontId f = kind_ == Kind::Name ? ctx.theme.font.uiBold
                              : (kind_ == Kind::Caption || kind_ == Kind::Note || kind_ == Kind::Warning) ? ctx.theme.font.smallUi
                                                                                                        : ctx.theme.font.ui;
        const gfx::Color col = kind_ == Kind::Caption ? ctx.theme.color.textMuted
                               : kind_ == Kind::Warning ? ctx.theme.color.warning
                               : kind_ == Kind::Note    ? ctx.theme.color.info
                                                        : ctx.theme.color.text;
        // Un chemin trop long pour sa place : son debut, "...", et sa fin (le nom du dossier).
        std::string shown = text_;
        const float room = r.w - 4.f;
        if (!shown.empty() && ctx.r.measure(shown, f).width > room && shown.size() > 8) {
            std::size_t keepTail = std::min<std::size_t>(shown.size() / 2, 40);
            std::string head = shown.substr(0, shown.size() - keepTail);
            const std::string tail = shown.substr(shown.size() - keepTail);
            while (head.size() > 4 && ctx.r.measure(head + "\xE2\x80\xA6" + tail, f).width > room) {
                head.pop_back();
                while (!head.empty() && (static_cast<unsigned char>(head.back()) & 0xC0) == 0x80) head.pop_back();
            }
            shown = head + "\xE2\x80\xA6" + tail;
        }
        ctx.r.drawText({r.x, r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f}, shown, f, col);
    }
private:
    [[nodiscard]] gfx::FontId font() const { return (kind_ == Kind::Caption || kind_ == Kind::Note || kind_ == Kind::Warning) ? gfx::FontId{13} : gfx::FontId{16}; }
    std::string text_;
    Kind        kind_;
    float       width_;
};

// Un trait entre les dossiers qui se changent et ceux qui s'ouvrent seulement.
class Rule final : public Widget {
public:
    [[nodiscard]] SizeHint sizeHint() const override {
        SizeHint h;
        h.preferred = {10.f, 9.f};
        h.minimum = {10.f, 9.f};
        h.stretchX = 1.f;
        return h;
    }
protected:
    void onPaint(const PaintContext& ctx) override {
        const auto r = contentRect();
        ctx.r.fillRect({r.x, r.y + std::floor(r.h * 0.5f), r.w, 1.f}, ctx.theme.color.border);
    }
};

class Gap final : public Widget {
public:
    [[nodiscard]] SizeHint sizeHint() const override {
        SizeHint h;
        h.stretchX = h.stretchY = 1.f;
        return h;
    }
};

constexpr float kNameWidth = 118.f;

std::string captionOf(const dossiers::Dossier& d, const dossiers::Etat& e) {
    std::string s(dossiers::texteSource(d.source));
    if (d.cle == dossiers::Cle::Donnees)
        s += " \xC2\xB7 projets\\, libs\\, resources\\, captures\\ par d\xC3\xA9" "faut";
    else if (d.source == dossiers::Source::ParDefaut || d.source == dossiers::Source::Developpement)
        s += " \xC2\xB7 " + std::string(dossiers::sousDossier(d.cle)) + "\\ du dossier des donn\xC3\xA9" "es";
    if (d.cle == dossiers::Cle::Bibliotheque) s += " \xC2\xB7 DFB, DDT, macros";
    if (d.cle == dossiers::Cle::Captures) s += " \xC2\xB7 F12";
    (void)e;
    return s;
}

} // namespace

// ============================================================== FoldersDialog ====
FoldersDialog::FoldersDialog(App& app) : menu::WidgetMenu("dialog.folders"), app_(app) {}

menu::MenuTraits FoldersDialog::traits() const { return traitsOfDialog(); }

std::string FoldersDialog::title() const { return "Dossiers de l'application"; }

void FoldersDialog::setNote(std::string text, bool warning) {
    if (auto* n = dynamic_cast<Text*>(note_)) n->setText(std::move(text), warning ? Text::Kind::Warning : Text::Kind::Note);
}

void FoldersDialog::openPath(const std::string& utf8, bool create) {
    if (utf8.empty()) {
        setNote("Pas de chemin \xC3\xA0 ouvrir.", true);
        return;
    }
    std::error_code ec;
    if (create) std::filesystem::create_directories(dossiers::cheminDe(utf8), ec);
    const std::string why = dossiers::ouvrirDansLeSysteme(utf8);
    setNote(why.empty() ? "Ouvert : " + utf8 : why, !why.empty());
}

void FoldersDialog::save() {
    const auto& e = dossiers::actuel();
    if (!e.installe) {
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        return;
    }
    const auto ctx = dossiers::contexteSysteme(dossiers::cheminDe(e.programme));
    std::vector<dossiers::Changement> changes;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (!fields_[i]) continue;
        const std::string now = fields_[i]->text();
        if (now == initial_[i]) continue;
        changes.push_back({dossiers::kCles[i], now, copy_ && copy_->isChecked()});
    }
    if (changes.empty()) {
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        return;
    }
    const auto b = dossiers::changer(changes, ctx);
    if (!b.ecrit) {
        // Rien n'a change (un dossier refuse) : le dialogue reste, avec la raison.
        setNote(b.lignes.empty() ? std::string("Rien n'a chang\xC3\xA9.") : b.lignes.back(), true);
        return;
    }
    std::string payload = b.ok ? "ok\n" : "attention\n";
    for (const auto& l : b.lignes) payload += l + "\n";
    if (b.redemarrer) payload += "\nRelance XPGAnalyser pour que les nouveaux dossiers servent partout.\n";
    manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, payload});
}

core::Status FoldersDialog::buildUi() {
    const auto& e = dossiers::actuel();
    const bool installed = e.installe;
    auto frame = std::make_unique<Frame>(title(), 900.f, 560.f);
    auto body = std::make_unique<BoxLayout>(Orientation::Vertical, "dialog.folders.body");
    body->setSpacing(4.f);

    // Un Text tient sur une ligne (au-dela, il coupe au milieu) : des phrases courtes.
    if (installed) {
        body->addChild(std::make_unique<Text>(
            std::string("Tes dossiers, pour ce compte Windows. Changer un dossier n'efface jamais l'ancien."), Text::Kind::Intro));
    } else {
        body->addChild(std::make_unique<Text>(
            std::string("Version de d\xC3\xA9veloppement (pas d'installation.ini \xC3\xA0 c\xC3\xB4t\xC3\xA9 du programme) :"), Text::Kind::Intro));
        body->addChild(std::make_unique<Text>(
            std::string("les dossiers suivent le dossier de travail ; XPGAnalyser.ini ne sert qu'\xC3\xA0 la version install\xC3\xA9" "e."),
            Text::Kind::Intro));
    }

    // ---- les quatre qui se changent ----
    for (std::size_t i = 0; i < dossiers::kNombreCles; ++i) {
        const auto cle = dossiers::kCles[i];
        const auto& d = e[cle];
        const std::string base = "dialog.folders." + std::string(dossiers::nomCle(cle));
        auto row = std::make_unique<BoxLayout>(Orientation::Horizontal, base + ".row");
        row->setSpacing(6.f);
        row->addChild(std::make_unique<Text>(std::string(dossiers::libelle(cle)), Text::Kind::Name, kNameWidth));
        auto input = std::make_unique<InputText>(base);
        input->setText(d.chemin);
        input->setPlaceholder("vide : le dossier par d\xC3\xA9" "faut");
        input->setReadOnly(!installed);
        fields_[i] = &static_cast<InputText&>(row->addChild(std::move(input)));
        initial_[i] = d.chemin;
        auto browse = std::make_unique<BrowseButton>(*fields_[i], chooseFolder(d.chemin, std::string(dossiers::libelle(cle))),
                                                     base + ".parcourir", "Dossier\xE2\x80\xA6");
        browse->setFieldLabel(std::string(dossiers::libelle(cle)));
        browse->setEnabled(installed);
        row->addChild(std::move(browse));
        auto open = std::make_unique<Button>("Ouvrir", base + ".ouvrir");
        open->setTooltip("Ouvre ce dossier dans l'Explorateur (cr\xC3\xA9\xC3\xA9 s'il n'existe pas encore)");
        auto& openRef = static_cast<Button&>(row->addChild(std::move(open)));
        links_ += openRef.clicked->connect([this, i] {
            const auto& now = dossiers::actuel();
            std::string text = fields_[i] ? fields_[i]->text() : std::string{};
            // Ce qui est tape peut porter des jetons ({Documents}...) ou etre relatif.
            if (!text.empty() && now.installe) {
                const auto ctx = dossiers::contexteSysteme(dossiers::cheminDe(now.programme));
                auto c = ctx;
                c.donnees = now[dossiers::Cle::Donnees].chemin;
                text = dossiers::absolu(text, c, i == 0 ? now.programme : now[dossiers::Cle::Donnees].chemin);
            }
            openPath(text, true);
        });
        body->addChild(std::move(row));
        auto caption = std::make_unique<Text>(captionOf(d, e), Text::Kind::Caption);
        auto capRow = std::make_unique<BoxLayout>(Orientation::Horizontal, base + ".note");
        capRow->setSpacing(6.f);
        capRow->addChild(std::make_unique<Text>(std::string{}, Text::Kind::Caption, kNameWidth));
        capRow->addChild(std::move(caption));
        body->addChild(std::move(capRow));
    }

    body->addChild(std::make_unique<Rule>());

    // ---- ceux qui s'ouvrent seulement ----
    struct Fixed {
        const char* id;
        std::string name, path, caption;
    };
    const std::vector<Fixed> fixed{
        {"reglages", "R\xC3\xA9glages", e.reglages, "settings.txt, th\xC3\xA8mes, reprise apr\xC3\xA8s un arr\xC3\xAAt, XPGAnalyser.ini"},
        {"journaux", "Journaux", e.journaux, e.journaux.empty() ? std::string("version de d\xC3\xA9veloppement : aucun")
                                                               : std::string("installation, mises \xC3\xA0 jour, r\xC3\xA9parations")},
        {"programme", "Programme", e.programme, installed ? std::string("remplac\xC3\xA9 \xC3\xA0 chaque mise \xC3\xA0 jour ; rien \xC3\xA0 y ranger")
                                                           : std::string("le dossier de l'ex\xC3\xA9" "cutable")},
    };
    for (const auto& f : fixed) {
        const std::string base = std::string("dialog.folders.") + f.id;
        auto row = std::make_unique<BoxLayout>(Orientation::Horizontal, base + ".row");
        row->setSpacing(6.f);
        row->addChild(std::make_unique<Text>(f.name, Text::Kind::Name, kNameWidth));
        row->addChild(std::make_unique<Text>(f.path.empty() ? std::string("-") : f.path, Text::Kind::Path));
        auto open = std::make_unique<Button>("Ouvrir", base + ".ouvrir");
        open->setEnabled(!f.path.empty());
        auto& openRef = static_cast<Button&>(row->addChild(std::move(open)));
        const std::string path = f.path;
        links_ += openRef.clicked->connect([this, path] { openPath(path, false); });
        body->addChild(std::move(row));
        auto capRow = std::make_unique<BoxLayout>(Orientation::Horizontal, base + ".note");
        capRow->setSpacing(6.f);
        capRow->addChild(std::make_unique<Text>(std::string{}, Text::Kind::Caption, kNameWidth));
        capRow->addChild(std::make_unique<Text>(f.caption, Text::Kind::Caption));
        body->addChild(std::move(capRow));
    }

    body->addChild(std::make_unique<Rule>());
    auto copy = std::make_unique<Checkbox>("Copier le contenu des anciens dossiers dans les nouveaux (rien n'est effac\xC3\xA9, rien n'est \xC3\xA9" "cras\xC3\xA9)",
                                           "dialog.folders.copier");
    copy->setState(Checkbox::State::Checked);
    copy->setEnabled(installed);
    copy_ = &static_cast<Checkbox&>(body->addChild(std::move(copy)));
    note_ = &body->addChild(std::make_unique<Text>(
        installed ? std::string("Donn\xC3\xA9" "es et biblioth\xC3\xA8que : au prochain d\xC3\xA9marrage. Projets et captures : tout de suite.")
                  : std::string("Ouvrir marche ; Changer demande la version install\xC3\xA9" "e."),
        Text::Kind::Note));
    body->addChild(std::make_unique<Gap>());

    auto buttons = std::make_unique<BoxLayout>(Orientation::Horizontal, "dialog.folders.buttons");
    buttons->setSpacing(8.f);
    buttons->setAlignment(Align::End);
    auto ini = std::make_unique<Button>("Ouvrir XPGAnalyser.ini", "dialog.folders.ini");
    ini->setTooltip("Le fichier des dossiers, dans le Bloc-notes (cr\xC3\xA9\xC3\xA9 avec ses explications s'il n'existe pas)");
    auto& iniRef = static_cast<Button&>(buttons->addChild(std::move(ini)));
    links_ += iniRef.clicked->connect([this] {
        const auto& now = dossiers::actuel();
        if (now.iniUtilisateur.empty()) {
            setNote("Le dossier des r\xC3\xA9glages est inconnu.", true);
            return;
        }
        const auto file = dossiers::cheminDe(now.iniUtilisateur);
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) {
            std::string why;
            if (!dossiers::ecrireIni(file, dossiers::Ini::depuisTexte(dossiers::modeleIniUtilisateur()), &why)) {
                setNote(why, true);
                return;
            }
        }
        openPath(now.iniUtilisateur, false);
    });
    buttons->addChild(std::make_unique<Gap>());
    auto cancel = std::make_unique<Button>(installed ? "Annuler" : "Fermer", "dialog.folders.cancel");
    auto& cancelRef = static_cast<Button&>(buttons->addChild(std::move(cancel)));
    links_ += cancelRef.clicked->connect([this] { manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}}); });
    if (installed) {
        auto ok = std::make_unique<Button>("Enregistrer", "dialog.folders.ok");
        ok->setStyle(Button::Style::Primary);
        auto& okRef = static_cast<Button&>(buttons->addChild(std::move(ok)));
        links_ += okRef.clicked->connect([this] { save(); });
    }
    body->addChild(std::move(buttons));

    frame->attachBody(std::move(body));
    setRoot(std::move(frame));
    return core::ok();
}

void showFoldersDialog(App& app) {
    app.menus().ShowDialog(std::make_unique<FoldersDialog>(app), [&app](const menu::DialogResult& r) {
        if (!r.accepted() || r.payload.empty()) return;
        const bool ok = r.payload.rfind("ok\n", 0) == 0;
        const std::string text = r.payload.substr(r.payload.find('\n') + 1);
        app.menus().ShowDialog(std::make_unique<MessageDialog>("Dossiers de l'application", text,
                                                               ok ? MessageDialog::Icon::Info : MessageDialog::Icon::Warning),
                               [](const menu::DialogResult&) {});
    });
}

} // namespace app
