// =============================================================================
//  license_admin/gui/PageVersions.cpp - 1.44.0 : LA PAGE << VERSIONS >>
//  (maquette 1.43.0, planche 07 ; docs/ADMINS.md, par. 6 << Les versions >>) -
//  l'admin+ seul.
// -----------------------------------------------------------------------------
//  Ce que les Studios et les Admins installent :
//    - << Publier une version... >> : les deux installateurs (choisis, ou glisses
//      sur la page), chacun avec son nom, sa taille et << version 1.44.1 lue dans
//      le fichier >> (Workspace::readInstallerVersion : la ressource de version
//      de l'exe, jamais lance) ; la version qui en sort (elles concordent ? plus
//      recente que la derniere publiee ?) ; les notes (preremplies par
//      suggestedNotes quand l'Admin les connait) ; << Importante >> ; ce qui va
//      se passer ; << Signer et publier la 1.44.1 >> (publishVersion : les deux
//      fichiers deposes, une barre d'avancement, le bloc << version >> signe) ;
//    - << Versions publiees >> : chaque version, ses fichiers, pour qui, qui est a
//      jour (les PC, les admins : << Hugo a encore l'Admin 1.42.0 >>) ;
//    - << Fichiers a renvoyer >> apres une restauration ailleurs : le service n'a
//      plus les octets d'un installateur publie ; << Renvoyer >> (ce poste l'a
//      encore, ou on le choisit).
//  Ce qui parle au service passe par runSlow / runQuiet.
// =============================================================================
#include "AdminsKit.hpp"
#include "Page.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace license_admin::gui {

namespace {

using namespace kit;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr FontId kF28{28}, kF16{16}, kF14{14}, kF13{13}, kF12{12}, kF11{11};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string baseName(const std::string& path) {
    const auto u = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    return std::string(u.begin(), u.end());
}
std::uint64_t fileSize(const std::string& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
    return ec ? 0 : static_cast<std::uint64_t>(n);
}
// << 57 Mo >>, << 6,6 Mo >>, << 820 Ko >>.
std::string sizeFr(std::uint64_t n) {
    char b[32];
    if (n >= 10ull * 1024 * 1024) std::snprintf(b, sizeof b, "%.0f Mo", static_cast<double>(n) / (1024.0 * 1024.0));
    else if (n >= 1024ull * 1024) std::snprintf(b, sizeof b, "%.1f Mo", static_cast<double>(n) / (1024.0 * 1024.0));
    else std::snprintf(b, sizeof b, "%.0f Ko", static_cast<double>(n) / 1024.0);
    std::string s = b;
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

// Un installateur choisi : son chemin, sa taille, sa version lue (ou pourquoi elle ne se lit pas).
struct Picked {
    std::string   path, name, version, why;
    std::uint64_t size{0};
    void set(const std::string& p) {
        path = p;
        name = baseName(p);
        size = fileSize(p);
        why.clear();
        version = Workspace::readInstallerVersion(p, &why);
        if (version.empty() && why.empty()) why = "pas de version dans ce fichier";
    }
};

// L'avancement d'une publication (le fil du travail lent l'ecrit, l'interface le lit).
struct Progressing {
    std::atomic<std::uint64_t> done{0}, total{0};
};

class VersionsPage final : public Page {
public:
    explicit VersionsPage(ShellHost host) : Page("versions", std::move(host)) {
        open_ = &button(*this, "Publier une version\xE2\x80\xA6", "versions.publier", ui::Button::Style::Default, ui::Icon::Export);
        open_->setTooltip("Les deux installateurs (les Studios, l'Admin) de la m\xC3\xAA" "me version : sign\xC3\xA9s ici, d\xC3\xA9pos\xC3\xA9s sur le service, chaque PC pas \xC3\xA0 jour pr\xC3\xA9venu.");
        reload_ = &button(*this, "Relever", "versions.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        reload_->setTooltip("Relire tout de suite les versions publi\xC3\xA9" "es et qui est \xC3\xA0 jour (l'Admin le fait seul en arrivant sur la page).");
        pickStudios_ = &button(*this, "Choisir\xE2\x80\xA6", "versions.studios.choisir", ui::Button::Style::Default, ui::Icon::Open);
        pickAdmin_ = &button(*this, "Choisir\xE2\x80\xA6", "versions.admin.choisir", ui::Button::Style::Default, ui::Icon::Open);
        notes_ = &adopt(*this, std::make_unique<ui::MultiLineText>("versions.notes"));
        // 1.46.0 : « je ne peux pas ecrire dans Notes » - un MultiLineText nait en lecture seule (l'apercu d'un code) : les
        // notes sont un texte a taper, une note par ligne (Entree : la suivante), sans numeros de ligne, repliees a la largeur.
        notes_->setReadOnly(false);
        notes_->setShowLineNumbers(false);
        notes_->setWordWrap(true);
        notes_->setLanguage(ui::Language::PlainText);
        notes_->setZoomSupport(ui::ZoomSupport::None);   // un champ du formulaire, pas un document
        important_ = &adopt(*this, std::make_unique<Switch>("Importante", "versions.importante"));
        cancel_ = &button(*this, "Annuler", "versions.annuler");
        sign_ = &button(*this, "Signer et publier", "versions.signer", ui::Button::Style::Primary, ui::Icon::Lock);
        links_ += open_->clicked->connect([this] {
            panel_ = !panel_;
            update();
        });
        links_ += reload_->clicked->connect([this] { fetch(true); });
        links_ += pickStudios_->clicked->connect([this] { pick(false); });
        links_ += pickAdmin_->clicked->connect([this] { pick(true); });
        links_ += cancel_->clicked->connect([this] {
            panel_ = false;
            studios_ = Picked{};
            admin_ = Picked{};
            notes_->setText({});
            notesFor_.clear();
            important_->setChecked(false);
            update();
        });
        links_ += sign_->clicked->connect([this] { publish(); });
        refresh();
    }
    ~VersionsPage() override { *alive_ = false; }

    [[nodiscard]] std::string title() const override { return "Versions"; }

    void refresh() override {
        if (ws().role() == Role::Plus && ws().service().ready() && !fetching_ && (lastFetch_ < 0 || steadySeconds() - lastFetch_ > 60.0)) fetch(false);
        update();
    }

    // Des installateurs glisses sur la page : celui dont le nom dit << Admin >> va a l'Admin, l'autre aux Studios.
    bool dropFiles(const std::vector<std::string>& paths) override {
        bool any = false;
        for (const auto& p : paths) {
            const std::string n = lower(baseName(p));
            if (n.size() < 4 || n.substr(n.size() - 4) != ".exe") continue;
            (n.find("admin") != std::string::npos ? admin_ : studios_).set(p);
            any = true;
        }
        if (!any) return false;
        panel_ = true;
        prefillNotes();
        update();
        return true;
    }

    // Les essais : choisir un installateur comme Choisir... ou un glisser.
    void setInstaller(bool admin, const std::string& path) {
        (admin ? admin_ : studios_).set(path);
        panel_ = true;
        prefillNotes();
        update();
    }
    [[nodiscard]] std::string version() const { return versionToPublish(); }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(600.f, b.w - 2.f * pad);
        titleY_ = b.y + (b.h < 680.f ? 18.f : 26.f);
        float bx = x0_ + w_;
        for (ui::Button* btn : {open_, reload_}) {
            const float bw = buttonWidth(*btn, 110.f);
            bx -= bw;
            btn->setBounds({bx, titleY_ + 4.f, bw, 38.f});
            bx -= 10.f;
        }
        float y = titleY_ + 80.f;
        for (ui::Widget* wd : std::initializer_list<ui::Widget*>{pickStudios_, pickAdmin_, notes_, important_, cancel_, sign_}) show(wd, panel_);
        if (panel_) {
            // 1.46.0 : << Annuler >> passait sur le coin des notes (il commencait a gauche de la colonne de droite : les deux
            // boutons cote a cote n'y tenaient pas). Les boutons restent dans la colonne de droite : elle s'elargit pour eux
            // quand la partie gauche garde sa place, sinon ils s'empilent (Annuler au-dessus) ; le panneau est alors plus haut.
            const float sw = buttonWidth(*sign_, 180.f), cw = buttonWidth(*cancel_, 90.f);
            const float rowNeed = sw + 8.f + cw + 40.f;
            float rightW = std::clamp(w_ * 0.27f, 280.f, 380.f);
            if (rowNeed > rightW && w_ - rowNeed >= 600.f) rightW = rowNeed;
            stacked_ = rowNeed > rightW;
            const float panelH = std::clamp(b.bottom() - y - 230.f, stacked_ ? 400.f : 380.f, 470.f);
            panelRect_ = {x0_, y, w_, panelH};
            leftRect_ = {panelRect_.x, panelRect_.y, panelRect_.w - rightW, panelRect_.h};
            rightRect_ = {leftRect_.right(), panelRect_.y, rightW, panelRect_.h};
            const float zx = leftRect_.x + 20.f, zw = (leftRect_.w - 56.f) * 0.5f;
            zones_[0] = {zx, leftRect_.y + 52.f, zw, 132.f};
            zones_[1] = {zx + zw + 16.f, leftRect_.y + 52.f, zw, 132.f};
            pickStudios_->setBounds({zones_[0].right() - 14.f - buttonWidth(*pickStudios_, 100.f), zones_[0].bottom() - 44.f, buttonWidth(*pickStudios_, 100.f), 32.f});
            pickAdmin_->setBounds({zones_[1].right() - 14.f - buttonWidth(*pickAdmin_, 100.f), zones_[1].bottom() - 44.f, buttonWidth(*pickAdmin_, 100.f), 32.f});
            versionRow_ = zones_[0].bottom() + 34.f;
            const float ny = versionRow_ + 70.f;
            notes_->setBounds({zx, ny, leftRect_.w - 40.f, std::max(60.f, leftRect_.bottom() - 16.f - ny)});
            important_->setBounds({rightRect_.x + 20.f, rightRect_.y + 214.f, 40.f, 22.f});
            if (!stacked_) {
                sign_->setBounds({rightRect_.right() - 20.f - sw, rightRect_.bottom() - 56.f, sw, 38.f});
                cancel_->setBounds({sign_->bounds().x - 8.f - cw, rightRect_.bottom() - 56.f, cw, 38.f});
            } else {
                const float bw = rightRect_.w - 40.f;
                sign_->setBounds({rightRect_.x + 20.f, rightRect_.bottom() - 56.f, bw, 38.f});
                cancel_->setBounds({rightRect_.x + 20.f, rightRect_.bottom() - 102.f, bw, 38.f});
            }
            y = panelRect_.bottom() + 16.f;
        } else {
            panelRect_ = {};
        }
        // Les fichiers a renvoyer (apres une restauration), puis les versions publiees.
        resendRect_ = {};
        for (std::size_t i = 0; i < resendBtns_.size(); ++i) show(resendBtns_[i], i < resend_.size());
        if (!resend_.empty()) {
            resendRect_ = {x0_, y, w_, 54.f + 40.f * static_cast<float>(resend_.size())};
            for (std::size_t i = 0; i < resend_.size(); ++i) {
                const float bw = buttonWidth(*resendBtns_[i], 120.f);
                resendBtns_[i]->setBounds({resendRect_.right() - 18.f - bw, resendRect_.y + 46.f + 40.f * static_cast<float>(i), bw, 32.f});
            }
            y = resendRect_.bottom() + 16.f;
        }
        listRect_ = {x0_, y, w_, std::max(140.f, b.bottom() - 16.f - y)};
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        r.fillRect(bounds(), c.windowBg);
        r.drawText({x0_, titleY_}, "Versions", gfx::semibold(kF28), c.text);
        std::string lead = "Ce que les Studios et les Admins installent";
        if (have_) lead += " \xC2\xB7 " + std::to_string(view_.total) + " PC suivis, dont " + std::to_string(view_.admins.size()) + " admins";
        lead += " \xC2\xB7 seul l'admin+ publie";
        if (fetching_) lead += " \xC2\xB7 relev\xC3\xA9 en cours\xE2\x80\xA6";
        else if (!problem_.empty()) lead += " \xC2\xB7 service : " + problem_;
        r.drawText({x0_, titleY_ + 40.f}, fit(r, lead, kF14, reload_->bounds().x - 16.f - x0_), kF14, problem_.empty() ? c.textMuted : c.warning);
        if (panel_) paintPanel(ctx);
        if (!resend_.empty()) paintResend(ctx);
        paintList(ctx);
    }

private:
    // ------------------------------------------------------------------ la version
    [[nodiscard]] std::string versionToPublish() const {
        if (studios_.version.empty() || admin_.version.empty() || studios_.version != admin_.version) return {};
        return studios_.version;
    }
    // La phrase sous la version ; `ok` : on peut publier.
    [[nodiscard]] std::string verdict(bool& ok) const {
        ok = false;
        if (studios_.path.empty() || admin_.path.empty()) return "Choisis les 2 installateurs (ou glisse-les sur la page) : la version se lit dans les fichiers.";
        if (studios_.version.empty()) return "L'installateur des Studios : " + studios_.why + ".";
        if (admin_.version.empty()) return "L'installateur de l'Admin : " + admin_.why + ".";
        if (studios_.version != admin_.version)
            return "Les 2 fichiers ne sont pas de la m\xC3\xAA" "me version (Studios " + studios_.version + ", Admin " + admin_.version + ") : choisis les bons.";
        const std::string latest = view_.latest();
        if (!latest.empty() && adm::compareSemver(studios_.version, latest) <= 0)
            return "Lue dans les 2 fichiers : " + studios_.version + ", pas plus r\xC3\xA9" "cente que la " + latest + " publi\xC3\xA9" "e.";
        ok = true;
        return "Lue dans les 2 fichiers : elles concordent." + (latest.empty() ? std::string(" La premi\xC3\xA8re version publi\xC3\xA9" "e.")
                                                                              : " Plus r\xC3\xA9" "cente que la " + latest + " publi\xC3\xA9" "e.");
    }
    void prefillNotes() {
        const std::string v = versionToPublish();
        if (v.empty() || v == notesFor_ || !trimmedEmpty(notes_->text())) return;
        notesFor_ = v;
        std::string text;
        for (const auto& n : ws().suggestedNotes(v)) text += "\xE2\x80\xA2 " + n + "\n";
        if (!text.empty()) notes_->setText(text);
    }
    static bool trimmedEmpty(const std::string& s) {
        return std::all_of(s.begin(), s.end(), [](char ch) { return std::isspace(static_cast<unsigned char>(ch)); });
    }
    [[nodiscard]] std::vector<std::string> notes() const {
        std::vector<std::string> out;
        const std::string t = notes_->text();
        std::size_t a = 0;
        while (a <= t.size()) {
            std::size_t b = t.find('\n', a);
            if (b == std::string::npos) b = t.size();
            std::string l = t.substr(a, b - a);
            for (const char* bullet : {"\xE2\x80\xA2", "-", "*"})
                if (l.rfind(bullet, 0) == 0) l = l.substr(std::string(bullet).size());
            while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.erase(l.begin());
            while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
            if (!l.empty()) out.push_back(l);
            a = b + 1;
        }
        return out;
    }

    void update() {
        const bool plus = ws().role() == Role::Plus;
        const bool ready = ws().service().ready();
        const bool busy = host_.busy && host_.busy();
        open_->setEnabled(plus);
        for (ui::Widget* wd : std::initializer_list<ui::Widget*>{pickStudios_, pickAdmin_, notes_, important_, cancel_, sign_}) show(wd, panel_);
        open_->setText(panel_ ? std::string("Fermer la publication") : std::string("Publier une version\xE2\x80\xA6"));
        reload_->setEnabled(plus && ready && !fetching_);
        bool ok = false;
        (void)verdict(ok);
        const std::string v = versionToPublish();
        sign_->setText(v.empty() ? std::string("Signer et publier") : "Signer et publier la " + v);
        sign_->setEnabled(plus && ready && ok && !busy && !publishing_);
        sign_->setTooltip(!ready ? std::string("Il faut le service des licences (Service en ligne).") : std::string("La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe ; les 2 fichiers partent sur le service."));
        pickStudios_->setEnabled(!publishing_);
        pickAdmin_->setEnabled(!publishing_);
        invalidateLayout();
        invalidate();
    }

    void pick(bool admin) {
        ui::FilePick p;
        p.title = admin ? "L'installateur de l'Admin (XPG-License-Admin-Setup-...exe)" : "L'installateur des Studios (...-Setup-...exe)";
        p.filters = {{"Installateur Windows (.exe)", "exe"}};
        auto alive = alive_;
        host_.pickFile(p, [this, alive, admin](std::string path) {
            if (!*alive || path.empty()) return;
            setInstaller(admin, path);
        });
    }

    void fetch(bool loud) {
        if (fetching_ || ws().role() != Role::Plus || !ws().service().ready()) return;
        fetching_ = true;
        lastFetch_ = steadySeconds();
        const Workspace* w = &ws();
        auto view = std::make_shared<VersionsView>();
        auto again = std::make_shared<std::vector<FileToResend>>();
        auto alive = alive_;
        const auto work = [w, view, again] {
            OpResult r = w->versions(*view);
            if (r.ok) (void)w->filesToResend(*again);
            return r;
        };
        const auto done = [this, alive, view, again](const OpResult& r) {
            if (!*alive) return;
            fetching_ = false;
            if (r.ok) {
                view_ = std::move(*view);
                resend_ = std::move(*again);
                while (resendBtns_.size() < resend_.size()) {   // un bouton << Renvoyer... >> par fichier
                    const std::size_t n = resendBtns_.size() + 1;
                    ui::Button& rb = button(*this, "Renvoyer\xE2\x80\xA6", "versions.renvoyer." + std::to_string(n), ui::Button::Style::Primary, ui::Icon::Export);
                    links_ += rb.clicked->connect([this, n] { resendAt(n - 1); });
                    resendBtns_.push_back(&rb);
                }
                have_ = true;
                problem_.clear();
            } else {
                problem_ = r.message;
            }
            update();
        };
        const bool started = loud ? host_.runSlow("Versions : relev\xC3\xA9", work, done) : runQuietly(host_, "Versions : relev\xC3\xA9", work, done);
        if (!started) {
            fetching_ = false;
            if (loud) host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        update();
    }

    void publish() {
        bool ok = false;
        (void)verdict(ok);
        if (!ok || publishing_) return;
        const Workspace* w = &ws();
        const std::string s = studios_.path, a = admin_.path, v = versionToPublish();
        const std::vector<std::string> n = notes();
        const bool important = important_->checked();
        progress_ = std::make_shared<Progressing>();
        auto prog = progress_;
        auto alive = alive_;
        publishing_ = true;
        if (!host_.runSlow("Publier la " + v,
                           [w, s, a, n, important, prog] {
                               return w->publishVersion(s, a, n, important, [prog](std::uint64_t done, std::uint64_t total) {
                                   prog->done = done;
                                   prog->total = total;
                                   return true;
                               });
                           },
                           [this, alive](const OpResult& r) {
                               if (!*alive) return;
                               publishing_ = false;
                               if (r.ok) {
                                   panel_ = false;
                                   studios_ = Picked{};
                                   admin_ = Picked{};
                                   notes_->setText({});
                                   notesFor_.clear();
                                   important_->setChecked(false);
                                   lastFetch_ = -1;
                                   fetch(false);
                               }
                               update();
                           })) {
            publishing_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        update();
    }

    void resendAt(std::size_t i) {
        if (i >= resend_.size()) return;
        const FileToResend f = resend_[i];
        auto alive = alive_;
        const auto send = [this, alive, f](const std::string& path) {
            if (!*alive || path.empty()) return;
            const Workspace* w = &ws();
            const std::string id = f.file.id;
            if (!host_.runSlow("Renvoyer " + f.file.name, [w, id, path] { return w->resendFile(id, path); },
                               [this, alive](const OpResult& r) {
                                   if (*alive && r.ok) {
                                       lastFetch_ = -1;
                                       fetch(false);
                                   }
                               }))
                host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        };
        if (!f.localPath.empty() && fileSize(f.localPath) == f.file.size) {
            send(f.localPath);
            return;
        }
        ui::FilePick p;
        p.title = "Le fichier " + f.file.name + " (version " + f.version + ")";
        p.filters = {{"Installateur Windows (.exe)", "exe"}};
        host_.pickFile(p, send);
    }

    // ------------------------------------------------------------------ dessiner
    void paintZone(const ui::PaintContext& ctx, const Rect& z, const char* head, const Picked& p, bool admin) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const Color tone = admin ? c.accent : rgb(0x0E9F6E);
        r.fillRoundedRect(z, tone.withAlpha(p.path.empty() ? 6 : 12), 12.f);
        ui::shapes::strokeRoundedRect(r, z, tone.withAlpha(110), 12.f, 1.5f);
        r.drawText({z.x + 16.f, z.y + 14.f}, head, gfx::semibold(kF11), c.textMuted);
        const Rect ib{z.x + 16.f, z.y + 38.f, 38.f, 38.f};
        r.fillRoundedRect(ib, tone, 10.f);
        ui::drawIcon(r, admin ? ui::Icon::Lock : ui::Icon::Import, {ib.x + 10.f, ib.y + 10.f, 18.f, 18.f}, rgb(0xFFFFFF));
        const float tx = ib.right() + 12.f, tw = z.right() - 16.f - tx;
        if (p.path.empty()) {
            r.drawText({tx, z.y + 40.f}, fit(r, "Glisse le fichier ici, ou \xC2\xAB Choisir\xE2\x80\xA6 \xC2\xBB", kF13, tw), kF13, c.textMuted);
        } else {
            r.drawText({tx, z.y + 38.f}, fit(r, p.name, ctx.theme.font.mono, tw), ctx.theme.font.mono, c.text);
            const std::string sub = sizeFr(p.size) + " \xC2\xB7 " + (p.version.empty() ? "version illisible : " + p.why : "version " + p.version + " lue dans le fichier \xE2\x9C\x93");
            r.drawText({tx, z.y + 60.f}, fit(r, sub, kF12, tw), kF12, p.version.empty() ? c.error : c.textMuted);
        }
        const std::string who = admin ? "Qui le re\xC3\xA7oit : les admins (jamais les clients)" : "Qui le re\xC3\xA7oit : tout le monde (chaque Studio pas \xC3\xA0 jour)";
        r.drawText({z.x + 16.f, z.bottom() - 36.f}, fit(r, who, kF12, z.w - 140.f), kF12, c.textMuted);
    }

    void paintPanel(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        cardBox(r, th, panelRect_);
        r.fillRect({rightRect_.x, rightRect_.y + 1.f, 1.f, rightRect_.h - 2.f}, c.gridLine);
        r.drawText({leftRect_.x + 20.f, leftRect_.y + 18.f}, "Publier une version", gfx::semibold(kF16), c.text);
        (void)pill(r, leftRect_.x + 190.f, leftRect_.y + 17.f, "admin+", gfx::semibold(kF11), c.accent, c.accent.withAlpha(22), 20.f);
        const std::string hint = "glisse les 2 installateurs, ou Choisir\xE2\x80\xA6";
        r.drawText({leftRect_.right() - 20.f - r.measure(hint, kF12).width, leftRect_.y + 20.f}, hint, kF12, c.textMuted);
        paintZone(ctx, zones_[0], "L'INSTALLATEUR DES STUDIOS", studios_, false);
        paintZone(ctx, zones_[1], "L'INSTALLATEUR DE L'ADMIN", admin_, true);
        // La version
        const float vx = leftRect_.x + 20.f;
        r.drawText({vx, versionRow_ - 20.f}, "Version", gfx::semibold(kF13), c.text);
        const Rect vb{vx, versionRow_, 140.f, 36.f};
        ui::shapes::fillFramedRect(r, vb, c.rowAltBg, c.border, 8.f);
        ui::drawIcon(r, ui::Icon::Lock, {vb.x + 10.f, vb.y + 11.f, 13.f, 13.f}, c.textDisabled);
        const std::string v = versionToPublish();
        r.drawText({vb.x + 30.f, vb.y + 9.f}, v.empty() ? std::string("\xE2\x80\x94") : v, gfx::semibold(kF14), c.text);
        bool ok = false;
        const std::string why = verdict(ok);
        const Rect box{vb.right() + 16.f, versionRow_, leftRect_.right() - 20.f - (vb.right() + 16.f), 36.f};
        r.fillRoundedRect(box, ok ? c.ok.withAlpha(22) : (studios_.path.empty() || admin_.path.empty()) ? c.rowAltBg : c.warning.withAlpha(22), 8.f);
        if (ok) drawTick(r, box.x + 12.f, box.y + 12.f, c.ok);
        r.drawText({box.x + 32.f, box.y + 10.f}, fit(r, why, kF13, box.w - 40.f), kF13, ok ? c.ok : c.text);
        const Rect nb = notes_->bounds();
        r.drawText({vx, nb.y - 22.f}, "Les notes", gfx::semibold(kF13), c.text);
        r.drawText({vx + 76.f, nb.y - 21.f}, fit(r, "une par ligne ; pr\xC3\xA9remplies quand l'Admin les conna\xC3\xAEt ; tu peux les changer", kF12, nb.w - 80.f), kF12,
                   c.textMuted);
        // A droite : ce qui va se passer, << Importante >>, ce qu'ils verront.
        const float rx = rightRect_.x + 20.f, rw = rightRect_.w - 40.f;
        float y = rightRect_.y + 18.f;
        r.drawText({rx, y}, "Ce qui va se passer", gfx::semibold(kF14), c.text);
        y += 28.f;
        const int studios = have_ ? std::max(0, view_.total - static_cast<int>(view_.admins.size())) : 0;
        const std::string steps[4] = {"La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe les 2 fichiers, ici, sur ton PC.",
                                      "Le service les garde, avec leur empreinte (SHA-256).",
                                      "Chaque PC pas \xC3\xA0 jour est pr\xC3\xA9venu dans les 2 minutes" +
                                          (have_ ? " : " + std::to_string(studios) + " Studios, " + std::to_string(view_.admins.size()) + " Admins." : std::string(".")),
                                      "Toi aussi, en direct, dans l'Admin et tes Studios."};
        for (int i = 0; i < 4; ++i) {
            const Rect n{rx, y, 20.f, 20.f};
            r.fillRoundedRect(n, c.accent.withAlpha(22), 6.f);
            const std::string s = std::to_string(i + 1);
            r.drawText({n.x + (20.f - r.measure(s, kF12).width) * 0.5f, n.y + 3.f}, s, gfx::semibold(kF12), c.accent);
            y += drawWrapped(r, rx + 30.f, y + 1.f, rw - 30.f, steps[i], kF12, c.text, 17.f, 2) + 8.f;
        }
        const Rect ib = important_->bounds();
        r.fillRect({rx, ib.y - 14.f, rw, 1.f}, c.gridLine);
        r.drawText({ib.right() + 12.f, ib.y - 2.f}, "Importante", gfx::semibold(kF13), c.text);
        r.drawText({ib.right() + 12.f, ib.y + 16.f}, "la rappeler \xC3\xA0 chaque lancement", kF12, c.textMuted);
        // 1.46.0 : ce qu'ils verront, s'il tient au-dessus des boutons (et de l'avancement pendant l'envoi) - un panneau bas
        // (1280 x 800) le posait sous << Signer et publier >>.
        const float buttonsTop = std::min(sign_->bounds().y, cancel_->bounds().y);
        const bool  sending = publishing_ && progress_;
        y = ib.bottom() + 30.f;
        if (y + 20.f + 58.f <= buttonsTop - (sending ? 46.f : 0.f) - 10.f) {
            r.drawText({rx, y}, "CE QU'ILS VERRONT", gfx::semibold(kF11), c.textMuted);
            y += 20.f;
            const Rect toast{rx, y, rw, 58.f};
            r.fillRoundedRect(toast, rgb(0x111827), 12.f);
            r.drawText({toast.x + 16.f, toast.y + 11.f}, fit(r, "Mise \xC3\xA0 jour " + (v.empty() ? std::string("X.Y.Z") : v) + " disponible", gfx::semibold(kF13), toast.w - 32.f),
                       gfx::semibold(kF13), rgb(0xFFFFFF));
            r.drawText({toast.x + 16.f, toast.y + 32.f}, fit(r, (studios_.size ? sizeFr(studios_.size) + " \xC2\xB7 " : std::string()) + "sign\xC3\xA9" "e par l'\xC3\xA9" "diteur \xE2\x9C\x93", kF12, toast.w - 32.f),
                       kF12, rgb(0xC7D0E0));
        }
        // L'avancement (au-dessus du premier bouton)
        if (sending) {
            const std::uint64_t d = progress_->done, t = progress_->total;
            const float f = t ? static_cast<float>(static_cast<double>(d) / static_cast<double>(t)) : 0.f;
            const Rect bar{rx, buttonsTop - 26.f, rw, 8.f};
            r.fillRoundedRect(bar, c.gridLine, 4.f);
            r.fillRoundedRect({bar.x, bar.y, std::max(8.f, bar.w * f), bar.h}, c.accent, 4.f);
            r.drawText({rx, bar.y - 20.f}, "Envoi : " + sizeFr(d) + " sur " + sizeFr(t), kF12, c.textMuted);
        }
    }

    void paintResend(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        cardBox(r, th, resendRect_);
        r.drawText({resendRect_.x + 18.f, resendRect_.y + 16.f}, "Fichiers \xC3\xA0 renvoyer", gfx::semibold(kF14), c.warning);
        r.drawText({resendRect_.x + 170.f, resendRect_.y + 17.f},
                   fit(r, "le service a \xC3\xA9t\xC3\xA9 restaur\xC3\xA9 ailleurs : ses installateurs publi\xC3\xA9s n'y sont plus (trop gros pour la sauvegarde)", kF12, resendRect_.w - 200.f),
                   kF12, c.textMuted);
        for (std::size_t i = 0; i < resend_.size(); ++i) {
            const auto& f = resend_[i];
            const float y = resendRect_.y + 52.f + 40.f * static_cast<float>(i);
            std::string s = f.file.name + " \xC2\xB7 " + sizeFr(f.file.size) + " \xC2\xB7 version " + f.version;
            s += f.localPath.empty() ? std::string(" \xC2\xB7 \xC3\xA0 choisir sur ce PC") : " \xC2\xB7 encore sur ce PC";
            r.drawText({resendRect_.x + 18.f, y}, fit(r, s, kF13, resendRect_.w - 180.f), kF13, c.text);
        }
    }

    void paintList(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        cardBox(r, th, listRect_);
        r.drawText({listRect_.x + 18.f, listRect_.y + 16.f}, "Versions publi\xC3\xA9" "es", gfx::semibold(kF14), c.text);
        r.drawText({listRect_.x + 140.f, listRect_.y + 17.f}, "la plus r\xC3\xA9" "cente d'abord", kF12, c.textMuted);
        const float x = listRect_.x + 18.f, w = listRect_.w - 36.f;
        const float cols[5] = {x, x + w * 0.14f, x + w * 0.29f, x + w * 0.44f, x + w * 0.57f};
        const char* heads[5] = {"VERSION", "PUBLI\xC3\x89" "E", "FICHIERS", "POUR QUI", "LES PC"};
        const float hy = listRect_.y + 50.f;
        r.fillRect({listRect_.x + 1.f, hy - 8.f, listRect_.w - 2.f, 1.f}, c.gridLine);
        for (int i = 0; i < 5; ++i) r.drawText({cols[i], hy}, heads[i], gfx::semibold(kF11), c.textMuted);
        r.fillRect({listRect_.x + 1.f, hy + 22.f, listRect_.w - 2.f, 1.f}, c.gridLine);
        float y = hy + 30.f;
        if (!have_ || view_.versions.empty()) {
            const std::string empty = ws().role() != Role::Plus ? std::string("R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+.")
                                      : !ws().service().ready() ? std::string("Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).")
                                      : !have_ ? std::string("Pas encore relev\xC3\xA9" "es.")
                                               : std::string("Aucune version publi\xC3\xA9" "e pour l'instant : \xC2\xAB Publier une version\xE2\x80\xA6 \xC2\xBB.");
            r.drawText({x, y + 8.f}, fit(r, empty, kF14, w), kF14, c.textMuted);
            return;
        }
        r.pushClip({listRect_.x, y, listRect_.w, listRect_.bottom() - y - 4.f});
        for (std::size_t i = 0; i < view_.versions.size(); ++i) {
            const auto& v = view_.versions[i];
            const float rowH = i == 0 ? 72.f : 60.f;
            const FontId fv = gfx::semibold(kF15());
            r.drawText({cols[0], y + 10.f}, v.version, fv, c.text);
            if (i == 0) (void)pill(r, cols[0] + r.measure(v.version, fv).width + 10.f, y + 9.f, "actuelle", gfx::semibold(kF11), c.ok, c.ok.withAlpha(24), 20.f, true);
            if (v.important) r.drawText({cols[0], y + 32.f}, "importante", kF12, c.warning);
            r.drawText({cols[1], y + 8.f}, dateFr(v.published), kF13, c.text);
            r.drawText({cols[1], y + 28.f}, "par l'admin+", kF12, c.textMuted);
            float fy = y + 8.f;
            for (const auto& f : v.files) {
                std::string s = std::string(f.file.kind == "admin" ? "Admin" : "Studios") + " \xC2\xB7 " + sizeFr(f.file.size);
                if (!f.present) s += " (\xC3\xA0 renvoyer)";
                r.drawText({cols[2], fy}, fit(r, s, kF13, cols[3] - cols[2] - 8.f), kF13, f.present ? c.text : c.warning);
                r.drawText({cols[3], fy}, f.file.audience == "admins" ? "les admins" : "tout le monde", kF13, c.text);
                fy += 20.f;
            }
            const float bx = cols[4], bw = std::min(220.f, listRect_.right() - 18.f - bx - 220.f);
            if (i == 0) {
                const int up = view_.upToDate, total = std::max(1, view_.total), late = std::max(0, view_.total - view_.upToDate);
                const Rect bar{bx, y + 14.f, std::max(80.f, bw), 6.f};
                r.fillRoundedRect(bar, rgb(0xF59E0B), 3.f);
                r.fillRoundedRect({bar.x, bar.y, std::max(6.f, bar.w * static_cast<float>(up) / static_cast<float>(total)), bar.h}, c.ok, 3.f);
                const float tx = bar.right() + 12.f;
                const std::string a = "\xC3\xA0 jour : " + std::to_string(up) + " PC";
                r.drawText({tx, y + 8.f}, a, gfx::semibold(kF12), c.ok);
                r.drawText({tx + r.measure(a, gfx::semibold(kF12)).width + 8.f, y + 8.f}, "\xC2\xB7 pas \xC3\xA0 jour : " + std::to_string(late) + " PC", gfx::semibold(kF12), rgb(0xC2410C));
                // Les admins : combien sont a jour, qui ne l'est pas.
                int ok = 0;
                std::string late1;
                for (const auto& s : view_.admins) {
                    if (s.upToDate) ++ok;
                    else if (late1.empty()) late1 = s.name + " a encore l'Admin " + (s.version.empty() ? std::string("?") : s.version);
                }
                std::string line = "Admins : " + std::to_string(ok) + " sur " + std::to_string(view_.admins.size()) + " \xC3\xA0 jour";
                if (!late1.empty()) line += " \xC2\xB7 " + late1 + ", il est pr\xC3\xA9venu";
                r.drawText({bx, y + 32.f}, fit(r, line, kF12, listRect_.right() - 18.f - bx), kF12, c.textMuted);
            } else {
                const auto it = view_.byVersion.find(v.version);
                const int n = it == view_.byVersion.end() ? 0 : it->second;
                const Rect bar{bx, y + 14.f, std::max(80.f, bw), 6.f};
                r.fillRoundedRect(bar, c.gridLine, 3.f);
                if (view_.total > 0 && n > 0) r.fillRoundedRect({bar.x, bar.y, std::max(6.f, bar.w * static_cast<float>(n) / static_cast<float>(view_.total)), bar.h}, c.textDisabled, 3.f);
                r.drawText({bar.right() + 12.f, y + 8.f}, n ? "encore sur " + std::to_string(n) + " PC" : std::string("plus aucun PC"), kF12, c.textMuted);
            }
            y += rowH;
            r.fillRect({listRect_.x + 1.f, y - 1.f, listRect_.w - 2.f, 1.f}, c.gridLine);
            if (y > listRect_.bottom()) break;
        }
        r.popClip();
    }
    static FontId kF15() { return FontId{15}; }

    // --------------------------------------------------------------------- l'etat
    bool                          panel_{false}, have_{false}, fetching_{false}, publishing_{false};
    bool                          stacked_{false};   // 1.46.0 : les deux boutons l'un sur l'autre (une colonne etroite)
    double                        lastFetch_{-1};
    VersionsView                  view_;
    std::vector<FileToResend>     resend_;
    std::string                   problem_, notesFor_;
    Picked                        studios_, admin_;
    std::shared_ptr<Progressing>  progress_;
    ui::Button*                   open_{nullptr};
    ui::Button*                   reload_{nullptr};
    ui::Button*                   pickStudios_{nullptr};
    ui::Button*                   pickAdmin_{nullptr};
    ui::MultiLineText*            notes_{nullptr};
    Switch*                       important_{nullptr};
    ui::Button*                   cancel_{nullptr};
    ui::Button*                   sign_{nullptr};
    std::vector<ui::Button*>      resendBtns_;
    float                         x0_{0.f}, w_{0.f}, titleY_{0.f}, versionRow_{0.f};
    Rect                          panelRect_{}, leftRect_{}, rightRect_{}, zones_[2]{}, resendRect_{}, listRect_{};
    std::shared_ptr<bool>         alive_ = std::make_shared<bool>(true);
    core::ConnectionScope         links_;
};

} // namespace

PagePtr makeVersionsPage(ShellHost host) { return std::make_unique<VersionsPage>(std::move(host)); }

} // namespace license_admin::gui
