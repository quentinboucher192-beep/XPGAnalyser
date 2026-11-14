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
//  1.46.1 : un .zip (choisi ou glisse) qui porte l'installateur : il en est
//  extrait (Workspace::installerFromZip : ce fichier seul, dans un dossier
//  temporaire), puis tout se passe comme avec le .exe - << extrait de
//  API-IHM-Studio-Setup-1.46.0.zip >>.
//
//  1.47.0 (planches 06, 07, 12) : << Versions publiees >> a la barre << Filtrer
//  et chercher >> (les puces Sorte - une version, un correctif -, Importante,
//  A jour - l'actuelle, encore sur des PC, plus aucun PC) ; une ligne se choisit
//  (un clic), se deplie (un clic sur la ligne choisie, ->, Entree ; <- replie) :
//  les notes, les fichiers (taille, pour qui, SHA-256, a renvoyer), les PC
//  dessus (par logiciel ; l'actuelle : les admins, a jour ou non) ; le clic
//  droit dans l'ordre de la regle (Deplier ; Renvoyer... ; Aller a ; Filtrer
//  sur ; Copier ; Exporter ; une version publiee ne se retire pas). Le panneau
//  << Publier une version >> (1.46.0 : Annuler a cote des notes, les notes
//  modifiables) ne change pas.
// =============================================================================
#include "AdminsKit.hpp"
#include "ClassicView.hpp"   // 1.52.0 : XPG Admin sous derogation (lire, jamais publier)
#include "ListKit.hpp"   // 1.47.0 : la barre de filtres, le clic droit, le depliage
#include "Page.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace license_admin::gui {

// 1.47.0 : les essais et la page d'essai, sans le service - la page montre ce releve comme s'il venait d'arriver
// (faux : ce n'est pas la page des versions).
bool adoptVersionsView(ui::Widget& page, VersionsView view, std::vector<FileToResend> resend);

namespace {

// 1.48.1 : pourquoi une version publiee ne se supprime pas (le bouton grise, le clic droit).
constexpr const char* kNoDelete = "une version publi\xC3\xA9" "e ne se supprime pas : des Studios et des Admins la t\xC3\xA9l\xC3\xA9" "chargent ou s'y mettent \xC3\xA0 jour - publie une version plus r\xC3\xA9" "cente";

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
    std::string   from;           // 1.46.1 : le .zip d'ou il est extrait (vide : choisi tel quel)
    bool          refused{false}; // 1.46.1 : le .zip refuse (`why` le dit)
    std::uint64_t size{0};
    void set(const std::string& p) {
        forget();
        path = p;
        name = baseName(p);
        size = fileSize(p);
        why.clear();
        from.clear();
        refused = false;
        version = Workspace::readInstallerVersion(p, &why);
        if (version.empty() && why.empty()) why = "pas de version dans ce fichier";
    }
    // 1.46.1 : un .zip - l'installateur attendu extrait, puis comme un .exe ; refuse : `why`. 1.49.0 : `kind` 1 les Studios,
    // 2 l'Admin+, 3 XPG Admin (Workspace::installerFromZip).
    void setZip(const std::string& zip, int kind) {
        std::string exe, w;
        if (!Workspace::installerFromZip(zip, kind, exe, &w)) {
            forget();
            path = zip;
            name = baseName(zip);
            size = fileSize(zip);
            version.clear();
            from.clear();
            why = w;
            refused = true;
            return;
        }
        set(exe);
        from = baseName(zip);
    }
    // Un fichier extrait d'un .zip qu'on ne prend plus : son dossier temporaire s'en va.
    void forget() {
        if (from.empty() || path.empty()) return;
        const auto dir = std::filesystem::path(std::u8string(path.begin(), path.end())).parent_path();
        const auto leaf = dir.filename().u8string();
        if (std::string(leaf.begin(), leaf.end()).rfind("xpg-versions-zip-", 0) == 0) {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
        from.clear();
    }
};

// L'avancement d'une publication (le fil du travail lent l'ecrit, l'interface le lit).
struct Progressing {
    std::atomic<std::uint64_t> done{0}, total{0};
};

// 1.47.0 : la sorte d'une version - << correctif >> (1.46.2 : le dernier nombre n'est pas 0), sinon << version >>.
std::string sortOf(const std::string& v) {
    const auto dot = v.find_last_of('.');
    if (dot == std::string::npos) return "version";
    const std::string last = v.substr(dot + 1);
    return !last.empty() && last.find_first_not_of('0') != std::string::npos && std::isdigit(static_cast<unsigned char>(last.front())) ? "correctif" : "version";
}

// Une case du CSV pour Excel : une formule devient du texte, ; " et les retours entre guillemets.
std::string csvCell(const std::string& raw) {
    const std::string x = licensing::looksLikeFormula(raw) && raw != "-" ? "'" + raw : raw;
    if (x.find_first_of(";\"\r\n") == std::string::npos) return x;
    std::string q = "\"";
    for (char c : x) q += c == '"' ? std::string("\"\"") : std::string(1, c);
    return q + "\"";
}

// 1.47.0 : la place de la ligne depliee - ce qui en depasse (la liste defile) est coupe a ses bords.
class ClipBox final : public ui::Widget {
public:
    explicit ClipBox(std::string id) : ui::Widget(std::move(id)) {}
};

class VersionsPage final : public Page {
public:
    // 1.47.0 : l'erreur du service tient sur une ligne (listkit::shortError) ; sous la souris, son texte entier.
    [[nodiscard]] bool hasTooltip() const override { return !problem_.empty() || Page::hasTooltip(); }
    [[nodiscard]] std::string liveTooltip(gfx::Point m) const override {
        if (!problem_.empty() && m.y >= (titleY_ + 40.f) - 4.f && m.y <= (titleY_ + 40.f) + 22.f) return problem_;
        return Page::liveTooltip(m);
    }

    explicit VersionsPage(ShellHost host) : Page("versions", std::move(host)) {
        open_ = &button(*this, "Publier une version\xE2\x80\xA6", "versions.publier", ui::Button::Style::Default, ui::Icon::Export);
        open_->setTooltip("Les trois installateurs (les Studios, l'Admin+, XPG Admin) de la m\xC3\xAA" "me version : sign\xC3\xA9s ici, d\xC3\xA9pos\xC3\xA9s sur le service, chacun "
                          "pr\xC3\xA9venu de ce qui le concerne.");
        reload_ = &button(*this, "Actualiser", "versions.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        reload_->setTooltip("Relire tout de suite les versions publi\xC3\xA9" "es et qui est \xC3\xA0 jour (l'Admin le fait seul en arrivant sur la page).");
        // 1.48.1 : Supprimer est la, grise : une version publiee ne se retire pas (la raison dans son infobulle).
        delete_ = &button(*this, "Supprimer\xE2\x80\xA6", "versions.supprimer", ui::Button::Style::Danger, ui::Icon::Delete);
        delete_->setEnabled(false);
        delete_->setTooltip(kNoDelete);
        pickStudios_ = &button(*this, "Choisir\xE2\x80\xA6", "versions.studios.choisir", ui::Button::Style::Default, ui::Icon::Open);
        pickAdmin_ = &button(*this, "Choisir\xE2\x80\xA6", "versions.admin.choisir", ui::Button::Style::Default, ui::Icon::Open);
        pickPlus_ = &button(*this, "Choisir\xE2\x80\xA6", "versions.adminplus.choisir", ui::Button::Style::Default, ui::Icon::Open);   // 1.49.0
        notes_ = &adopt(*this, std::make_unique<ui::MultiLineText>("versions.notes"));
        // 1.46.0 : << je ne peux pas ecrire dans Notes >> - un MultiLineText nait en lecture seule (l'apercu d'un code) : les
        // notes sont un texte a taper, une note par ligne (Entree : la suivante), sans numeros de ligne (une note plus
        // longue que le champ : sa barre du bas).
        notes_->setReadOnly(false);
        notes_->setShowLineNumbers(false);
        notes_->setLanguage(ui::Language::PlainText);
        notes_->setZoomSupport(ui::ZoomSupport::None);   // un champ du formulaire, pas un document
        important_ = &adopt(*this, std::make_unique<Switch>("Importante", "versions.importante"));
        cancel_ = &button(*this, "Annuler", "versions.annuler");
        sign_ = &button(*this, "Signer et publier", "versions.signer", ui::Button::Style::Primary, ui::Icon::Lock);
        // 1.47.0 (planche 12) : LA BARRE des versions publiees - chercher (versions.recherche), la sorte, importante, a jour.
        bar_ = &adopt(*this, std::make_unique<ui::FilterBar>("versions.barre", barFields(), "versions.recherche"));
        bar_->setNoun("versions");
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return texts_.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return r < texts_.size() && f < texts_[r].size() ? texts_[r][f] : std::string(); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t {
                return r < view_.versions.size() && f == 1 ? listkit::seconds(view_.versions[r].published) : 0;
            };
            src.extra = [this](std::size_t r) {
                std::string x;
                if (r < view_.versions.size())
                    for (const auto& f : view_.versions[r].files) x += f.file.sha256 + " " + f.file.kind + " ";
                return x;
            };
            bar_->setSource(std::move(src));
        }
        bar_->setMemoryKey("versions");
        links_ += bar_->changed->connect([this] { rebuildRows(); });
        clip_ = &adopt(*this, std::make_unique<ClipBox>("versions.depliage.cadre"));   // (planche 07) la ligne depliee
        menu_ = &adopt(*this, std::make_unique<ui::PopupMenu>("versions.menu"));         // (planche 06) le clic droit
        links_ += menu_->itemChosen->connect([this](int id) { (void)menuList_.run(id); });
        links_ += open_->clicked->connect([this] {
            panel_ = !panel_;
            update();
        });
        links_ += reload_->clicked->connect([this] { fetch(true); });
        links_ += pickStudios_->clicked->connect([this] { pick(false); });
        links_ += pickAdmin_->clicked->connect([this] { pick(3); });
        links_ += pickPlus_->clicked->connect([this] { pick(2); });
        links_ += cancel_->clicked->connect([this] {
            panel_ = false;
            studios_.forget();   // 1.46.1 : un fichier extrait d'un .zip : son dossier temporaire
            admin_.forget();
            plus_.forget();
            studios_ = Picked{};
            admin_ = Picked{};
            plus_ = Picked{};
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
        if (mayRead() && ws().service().ready() && !fetching_ && (lastFetch_ < 0 || steadySeconds() - lastFetch_ > 60.0)) fetch(false);
        update();
    }

    // Des installateurs glisses sur la page : celui dont le nom dit << Admin >> va a l'Admin, l'autre aux Studios. 1.46.1 : un
    // .zip - ce qu'il porte decide (l'installateur des Studios, celui de l'Admin, ou les deux).
    bool dropFiles(const std::vector<std::string>& paths) override {
        bool any = false;
        for (const auto& p : paths) {
            const std::string n = lower(baseName(p));
            if (Workspace::isZipPath(p)) {
                if (publishing_) continue;
                bool st = false, pl = false, ad = false;
                std::string why;
                if (!Workspace::installerKindsInZip(p, st, pl, ad, &why)) {
                    if (host_.flash) host_.flash("Ce .zip n'est pas pris : " + why + ".", Tone::Warning);
                    any = true;
                    continue;
                }
                if (st) studios_.setZip(p, 1);
                if (pl) plus_.setZip(p, 2);   // 1.49.0 : l'Admin+ (XPG-License-Admin-Setup)
                if (ad) admin_.setZip(p, 3);  // XPG Admin (XPG-Admin-Setup)
                any = true;
                continue;
            }
            if (n.size() < 4 || n.substr(n.size() - 4) != ".exe") continue;
            // 1.49.0 : XPG-License-Admin-Setup-... : l'Admin+ ; un autre nom qui dit << admin >> (XPG-Admin-Setup-...) : XPG Admin
            (n.rfind("xpg-license-admin-setup", 0) == 0 ? plus_ : n.find("admin") != std::string::npos ? admin_ : studios_).set(p);
            any = true;
        }
        if (!any) return false;
        panel_ = true;
        prefillNotes();
        update();
        return true;
    }

    // Les essais : choisir un installateur comme Choisir... ou un glisser. 1.46.1 : un .zip aussi.
    // 1.49.0 : `kind` 1 les Studios, 2 l'Admin+, 3 XPG Admin ; setInstaller(bool) : true = XPG Admin (l'Admin des admins).
    void setInstallerKind(int kind, const std::string& path) {
        Picked& p = slot(kind);
        if (Workspace::isZipPath(path)) p.setZip(path, kind);
        else p.set(path);
        panel_ = true;
        prefillNotes();
        update();
    }
    void setInstaller(bool admin, const std::string& path) { setInstallerKind(admin ? 3 : 1, path); }
    [[nodiscard]] const Picked& picked(bool admin) const { return admin ? admin_ : studios_; }
    [[nodiscard]] const Picked& pickedKind(int kind) const { return kind == 2 ? plus_ : kind == 3 ? admin_ : studios_; }
    Picked& slot(int kind) { return kind == 2 ? plus_ : kind == 3 ? admin_ : studios_; }
    [[nodiscard]] std::string version() const { return versionToPublish(); }

    // 1.47.0 : un releve arrive (le service, ou les essais : adoptVersionsView) - la liste, la barre, les boutons.
    void takeView(VersionsView view, std::vector<FileToResend> again) {
        view_ = std::move(view);
        resend_ = std::move(again);
        while (resendBtns_.size() < resend_.size()) {   // un bouton << Renvoyer... >> par fichier
            const std::size_t n = resendBtns_.size() + 1;
            ui::Button& rb = button(*this, "Renvoyer\xE2\x80\xA6", "versions.renvoyer." + std::to_string(n), ui::Button::Style::Primary, ui::Icon::Export);
            links_ += rb.clicked->connect([this, n] { resendAt(n - 1); });
            resendBtns_.push_back(&rb);
        }
        have_ = true;
        problem_.clear();
        texts_.clear();
        for (std::size_t i = 0; i < view_.versions.size(); ++i) texts_.push_back(cellsOf(i));
        bar_->refreshCounts();
        rebuildRows();
        update();
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(600.f, b.w - 2.f * pad);
        titleY_ = b.y + (b.h < 680.f ? 18.f : 26.f);
        float bx = x0_ + w_;
        for (ui::Button* btn : {open_, delete_, reload_}) {
            const float bw = buttonWidth(*btn, 110.f);
            bx -= bw;
            btn->setBounds({bx, titleY_ + 4.f, bw, 38.f});
            bx -= 10.f;
        }
        float y = titleY_ + 80.f;
        for (ui::Widget* wd : std::initializer_list<ui::Widget*>{pickStudios_, pickAdmin_, pickPlus_, notes_, important_, cancel_, sign_}) show(wd, panel_);
        if (panel_) {
            // 1.46.0 : << Annuler >> passait sur le coin des notes (il commencait a gauche de la colonne de droite : les deux
            // boutons cote a cote n'y tenaient pas). Les boutons restent dans la colonne de droite : elle s'elargit pour eux
            // quand la partie gauche garde sa place, sinon ils s'empilent (Annuler au-dessus) ; le panneau est alors plus haut.
            const float sw = buttonWidth(*sign_, 180.f), cw = buttonWidth(*cancel_, 90.f);
            const float rowNeed = sw + 8.f + cw + 40.f;
            float rightW = std::clamp(w_ * 0.27f, 280.f, 380.f);
            if (rowNeed > rightW && w_ - rowNeed >= 600.f) rightW = rowNeed;
            stacked_ = rowNeed > rightW;
            const float panelH = std::clamp(b.bottom() - y - 230.f, stacked_ ? 434.f : 404.f, 494.f);   // 1.49.0 : + 24 (les zones a trois lignes)
            panelRect_ = {x0_, y, w_, panelH};
            leftRect_ = {panelRect_.x, panelRect_.y, panelRect_.w - rightW, panelRect_.h};
            rightRect_ = {leftRect_.right(), panelRect_.y, rightW, panelRect_.h};
            // 1.49.0 : trois emplacements - les Studios, l'Admin+, XPG Admin
            const float zx = leftRect_.x + 20.f, zw = (leftRect_.w - 72.f) / 3.f;
            zones_[0] = {zx, leftRect_.y + 52.f, zw, 156.f};
            zones_[1] = {zx + zw + 16.f, leftRect_.y + 52.f, zw, 156.f};
            zones_[2] = {zx + 2.f * (zw + 16.f), leftRect_.y + 52.f, zw, 156.f};
            pickStudios_->setBounds({zones_[0].right() - 14.f - buttonWidth(*pickStudios_, 100.f), zones_[0].bottom() - 44.f, buttonWidth(*pickStudios_, 100.f), 32.f});
            pickPlus_->setBounds({zones_[1].right() - 14.f - buttonWidth(*pickPlus_, 100.f), zones_[1].bottom() - 44.f, buttonWidth(*pickPlus_, 100.f), 32.f});
            pickAdmin_->setBounds({zones_[2].right() - 14.f - buttonWidth(*pickAdmin_, 100.f), zones_[2].bottom() - 44.f, buttonWidth(*pickAdmin_, 100.f), 32.f});
            versionRow_ = zones_[0].bottom() + 34.f;
            const float ny = versionRow_ + 70.f;
            notes_->setBounds({zx, ny, leftRect_.w - 40.f, std::max(60.f, leftRect_.bottom() - 16.f - ny)});
            // 1.46.0 : 226 (et non 214) - les quatre etapes sur deux lignes (une colonne etroite) passent au-dessus du trait.
            important_->setBounds({rightRect_.x + 20.f, rightRect_.y + 226.f, 40.f, 22.f});
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
        layoutList();   // 1.47.0 : la barre, les lignes, la ligne depliee
        menu_->setBounds(b);
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        r.fillRect(bounds(), c.windowBg);
        grave_.clear();   // 1.47.0 : les lignes depliees remplacees pendant un evenement
        r.drawText({x0_, titleY_}, "Versions", gfx::semibold(kF28), c.text);
        std::string lead = "Ce que les Studios et les Admins installent";
        if (have_) lead += " \xC2\xB7 " + std::to_string(view_.total) + " PC suivis, dont " + std::to_string(view_.admins.size()) + " admins";
        lead += " \xC2\xB7 seul l'admin+ publie";
        if (fetching_) lead += " \xC2\xB7 relev\xC3\xA9 en cours\xE2\x80\xA6";
        else if (!problem_.empty()) {   // 1.47.0 : une ligne ; l'infobulle dit tout
            const std::string e = listkit::shortError(problem_);
            lead += " \xC2\xB7 " + (e.rfind("Le service", 0) == 0 ? e : "service : " + e);
        }
        r.drawText({x0_, titleY_ + 40.f}, fit(r, lead, kF14, reload_->bounds().x - 16.f - x0_), kF14, problem_.empty() ? c.textMuted : c.warning);
        if (panel_) paintPanel(ctx);
        if (!resend_.empty()) paintResend(ctx);
        paintList(ctx);
    }

    // 1.47.0 (planches 06, 07) : la liste des versions publiees - un clic choisit et deplie (ou replie), le double-clic
    // deplie ; -> deplie, <- replie, Entree deplie ou replie ; haut et bas changent de ligne ; le clic droit (ou la touche
    // Menu, Maj+F10) : le menu de la ligne ; la molette fait defiler.
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            const int k = rowAt(d->pos);
            if (k >= 0) {
                const std::string v = view_.versions[shown_[static_cast<std::size_t>(k)]].version;
                if (d->button == ui::MouseButton::Left) {
                    selected_ = v;
                    if (d->clickCount >= 2) {
                        if (expanded_ != v) unfold(v, true);
                    } else {
                        unfold(v, expanded_ != v);
                    }
                    return ui::EventResult::Consumed;
                }
                if (d->button == ui::MouseButton::Right) {
                    selected_ = v;
                    openMenu(k, d->pos);
                    return ui::EventResult::Consumed;
                }
            }
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !bar_->searchField().focused() && !notes_->focused()) {
            const int at = selectedRow();
            if (ui::isContextMenuKey(*k) && at >= 0) {
                const Rect row = rowRects_[static_cast<std::size_t>(at)];
                openMenu(at, {row.x + 40.f, row.y + row.h * 0.5f});
                return ui::EventResult::Consumed;
            }
            if (k->mods.none() && !shown_.empty()) {
                if (k->key == ui::Key::Down || k->key == ui::Key::Up) {
                    const int n = static_cast<int>(shown_.size());
                    const int next = at < 0 ? 0 : std::clamp(at + (k->key == ui::Key::Down ? 1 : -1), 0, n - 1);
                    selected_ = view_.versions[shown_[static_cast<std::size_t>(next)]].version;
                    reveal_ = true;
                    invalidateLayout();
                    invalidate();
                    return ui::EventResult::Consumed;
                }
                if (at >= 0 && (k->key == ui::Key::Right || k->key == ui::Key::Left || k->key == ui::Key::Return)) {
                    const bool open = k->key == ui::Key::Right || (k->key == ui::Key::Return && expanded_ != selected_);
                    unfold(selected_, open);
                    return ui::EventResult::Consumed;
                }
            }
        }
        if (const auto* wh = std::get_if<ui::MouseWheel>(&ev); wh && listRect_.contains(wh->pos) && wh->pos.y >= rowsTop_) {
            scroll_ -= wh->dy * 48.f;
            invalidateLayout();
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    // ------------------------------------------------------------------ la version
    [[nodiscard]] std::string versionToPublish() const {
        if (studios_.version.empty() || admin_.version.empty() || studios_.version != admin_.version) return {};
        if (!plus_.path.empty() && plus_.version != studios_.version) return {};   // 1.49.0 : l'Admin+, facultatif, la meme
        return studios_.version;
    }
    // La phrase sous la version ; `ok` : on peut publier.
    [[nodiscard]] std::string verdict(bool& ok) const {
        ok = false;
        if (studios_.path.empty() || admin_.path.empty())
            return "Choisis les installateurs des Studios et de XPG Admin (l'Admin+ si tu le publies), ou glisse-les sur la page : la version se lit dans les fichiers.";
        if (studios_.version.empty()) return "L'installateur des Studios : " + studios_.why + ".";
        if (admin_.version.empty()) return "L'installateur de XPG Admin : " + admin_.why + ".";
        if (!plus_.path.empty() && plus_.version.empty()) return "L'installateur de l'Admin+ : " + plus_.why + ".";
        if (studios_.version != admin_.version)
            return "Les fichiers ne sont pas de la m\xC3\xAA" "me version (Studios " + studios_.version + ", Admin " + admin_.version + ") : choisis les bons.";
        if (!plus_.path.empty() && plus_.version != studios_.version)
            return "Les fichiers ne sont pas de la m\xC3\xAA" "me version (Studios " + studios_.version + ", Admin+ " + plus_.version + ") : choisis les bons.";
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

    // 1.52.0 : lire les versions - l'admin+, ou XPG Admin sous derogation (le service en est l'arbitre) ; publier : l'admin+ seul.
    [[nodiscard]] bool mayRead() const { return ws().role() == Role::Plus || classic::derogationOpen(ws()); }

    void update() {
        const bool plus = ws().role() == Role::Plus;
        const bool ready = ws().service().ready();
        const bool busy = host_.busy && host_.busy();
        open_->setEnabled(plus);
        if (!plus) open_->setTooltip(classic::kEditorKeyOnly);   // 1.52.0 : XPG Admin sous derogation - lire, jamais publier
        for (ui::Widget* wd : std::initializer_list<ui::Widget*>{pickStudios_, pickAdmin_, pickPlus_, notes_, important_, cancel_, sign_}) show(wd, panel_);
        open_->setText(panel_ ? std::string("Fermer la publication") : std::string("Publier une version\xE2\x80\xA6"));
        reload_->setEnabled(mayRead() && ready && !fetching_);
        bool ok = false;
        (void)verdict(ok);
        const std::string v = versionToPublish();
        sign_->setText(v.empty() ? std::string("Signer et publier") : "Signer et publier la " + v);
        sign_->setEnabled(plus && ready && ok && !busy && !publishing_);
        sign_->setTooltip(!plus    ? std::string(classic::kEditorKeyOnly)   // 1.52.0
                          : !ready ? std::string("Il faut le service des licences (Service en ligne).") : std::string("La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe ; les 2 fichiers partent sur le service."));
        for (ui::Button* rb : resendBtns_) {   // 1.52.0 : renvoyer un fichier - la cle de l'editeur signe
            rb->setEnabled(plus);
            if (!plus) rb->setTooltip(classic::kEditorKeyOnly);
        }
        pickStudios_->setEnabled(!publishing_);
        pickAdmin_->setEnabled(!publishing_);
        pickPlus_->setEnabled(!publishing_);
        invalidateLayout();
        invalidate();
    }

    void pick(int kind) {
        ui::FilePick p;
        p.title = kind == 2   ? "L'installateur de l'Admin+ (XPG-License-Admin-Setup-...-EDITEUR.exe, ou un .zip qui le porte)"
                  : kind == 3 ? "L'installateur de XPG Admin (XPG-Admin-Setup-...exe, ou un .zip qui le porte)"
                              : "L'installateur des Studios (...-Setup-...exe, ou un .zip qui le porte)";
        p.filters = {{"Installateur Windows (.exe, ou un .zip qui le porte)", "exe;zip"}, {"Installateur Windows (.exe)", "exe"}, {"Archive (.zip)", "zip"}};
        auto alive = alive_;
        host_.pickFile(p, [this, alive, kind](std::string path) {
            if (!*alive || path.empty()) return;
            setInstallerKind(kind, path);
        });
    }

    void fetch(bool loud) {
        if (fetching_ || !mayRead() || !ws().service().ready()) return;
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
                takeView(std::move(*view), std::move(*again));   // 1.47.0 : la barre, la ligne depliee suivent
                return;
            }
            problem_ = r.message;
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
        const std::string s = studios_.path, a = admin_.path, pl = plus_.path, v = versionToPublish();
        const std::vector<std::string> n = notes();
        const bool important = important_->checked();
        progress_ = std::make_shared<Progressing>();
        auto prog = progress_;
        auto alive = alive_;
        publishing_ = true;
        if (!host_.runSlow("Publier la " + v,
                           [w, s, a, pl, n, important, prog] {
                               return w->publishVersion(s, a, n, important, [prog](std::uint64_t done, std::uint64_t total) {
                                   prog->done = done;
                                   prog->total = total;
                                   return true;
                               }, pl);
                           },
                           [this, alive](const OpResult& r) {
                               if (!*alive) return;
                               publishing_ = false;
                               if (r.ok) {
                                   panel_ = false;
                                   studios_ = Picked{};
                                   admin_ = Picked{};
                                   plus_ = Picked{};
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
    // 1.49.0 : `kind` 1 les Studios, 2 l'Admin+, 3 XPG Admin
    void paintZone(const ui::PaintContext& ctx, const Rect& z, const char* head, const Picked& p, int kind) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const bool admin = kind != 1;
        const Color tone = kind == 2 ? c.accent : kind == 3 ? rgb(0x0E7490) : rgb(0x0E9F6E);
        r.fillRoundedRect(z, tone.withAlpha(p.path.empty() ? 6 : 12), 12.f);
        ui::shapes::strokeRoundedRect(r, z, tone.withAlpha(110), 12.f, 1.5f);
        // 1.49.0 : trois zones cote a cote - l'icone passe a cote du titre, le texte prend toute la largeur (une ligne chacun)
        const Rect ib{z.x + 14.f, z.y + 10.f, 22.f, 22.f};
        r.fillRoundedRect(ib, tone, 6.f);
        ui::drawIcon(r, admin ? ui::Icon::Lock : ui::Icon::Import, {ib.x + 4.f, ib.y + 4.f, 14.f, 14.f}, rgb(0xFFFFFF));
        r.drawText({ib.right() + 8.f, z.y + 15.f}, head, gfx::semibold(kF11), c.textMuted);
        const float tx = z.x + 16.f, tw = z.w - 32.f;
        if (p.path.empty()) {
            r.drawText({tx, z.y + 44.f}, fit(r, "Glisse le fichier ici, ou \xC2\xAB Choisir\xE2\x80\xA6 \xC2\xBB", kF13, tw), kF13, c.textMuted);
        } else {
            r.drawText({tx, z.y + 40.f}, fit(r, p.name, gfx::semibold(kF12), tw), gfx::semibold(kF12), c.text);
            const std::string sub = p.refused ? p.why : p.version.empty() ? "version illisible : " + p.why : "version " + p.version + " lue dans le fichier \xE2\x9C\x93";
            r.drawText({tx, z.y + 60.f}, fit(r, sub, kF12, tw), kF12, p.version.empty() ? c.error : c.textMuted);
            if (!p.refused) r.drawText({tx, z.y + 78.f}, sizeFr(p.size), kF12, c.textMuted);
            if (!p.from.empty())   // 1.46.1
                r.drawText({tx, z.y + 96.f}, fit(r, "extrait de " + p.from, kF12, tw), kF12, c.textMuted);
        }
        const std::string who = kind == 2   ? "Pour l'admin+ seul (facultatif)"
                                : kind == 3 ? "Pour les admins (jamais un client)"
                                            : "Pour tous (chaque Studio)";
        r.drawText({z.x + 16.f, z.bottom() - 34.f}, fit(r, who, kF12, z.w - 140.f), kF12, c.textMuted);
    }

    void paintPanel(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        cardBox(r, th, panelRect_);
        r.fillRect({rightRect_.x, rightRect_.y + 1.f, 1.f, rightRect_.h - 2.f}, c.gridLine);
        r.drawText({leftRect_.x + 20.f, leftRect_.y + 18.f}, "Publier une version", gfx::semibold(kF16), c.text);
        (void)pill(r, leftRect_.x + 190.f, leftRect_.y + 17.f, "admin+", gfx::semibold(kF11), c.accent, c.accent.withAlpha(22), 20.f);
        const std::string hint = "glisse les installateurs, ou Choisir\xE2\x80\xA6";
        r.drawText({leftRect_.right() - 20.f - r.measure(hint, kF12).width, leftRect_.y + 20.f}, hint, kF12, c.textMuted);
        paintZone(ctx, zones_[0], "LES STUDIOS", studios_, 1);
        paintZone(ctx, zones_[1], "L'ADMIN+", plus_, 2);           // 1.49.0
        paintZone(ctx, zones_[2], "L'ADMIN (XPG ADMIN)", admin_, 3);
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
        const std::string steps[4] = {"La cl\xC3\xA9 de l'\xC3\xA9" "diteur signe les fichiers, ici, sur ton PC.",
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
        const float hy = headY_;   // 1.47.0 : sous la barre
        r.fillRect({listRect_.x + 1.f, hy - 8.f, listRect_.w - 2.f, 1.f}, c.gridLine);
        for (int i = 0; i < 5; ++i) r.drawText({cols[i], hy}, heads[i], gfx::semibold(kF11), c.textMuted);
        r.fillRect({listRect_.x + 1.f, hy + 22.f, listRect_.w - 2.f, 1.f}, c.gridLine);
        if (!have_ || view_.versions.empty()) {
            const std::string empty = !mayRead() ? std::string("R\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+.")
                                      : !ws().service().ready() ? std::string("Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9 (Service en ligne).")
                                      : !have_ ? std::string("Pas encore relev\xC3\xA9" "es.")
                                               : std::string("Aucune version publi\xC3\xA9" "e pour l'instant : \xC2\xAB Publier une version\xE2\x80\xA6 \xC2\xBB.");
            r.drawText({x, rowsTop_ + 8.f}, fit(r, empty, kF14, w), kF14, c.textMuted);
            return;
        }
        if (shown_.empty()) {   // 1.47.0 : la barre n'en garde aucune
            r.drawText({x, rowsTop_ + 8.f}, fit(r, "Aucune version ne correspond \xC3\xA0 la recherche ou aux filtres.", kF14, w), kF14, c.textMuted);
            return;
        }
        r.pushClip({listRect_.x, rowsTop_, listRect_.w, std::max(0.f, rowsBottom_ - rowsTop_)});
        for (std::size_t k = 0; k < shown_.size() && k < rowRects_.size(); ++k) {
            const Rect row = rowRects_[k];
            const std::size_t i = shown_[k];
            const auto& v = view_.versions[i];
            const bool open = v.version == expanded_;
            const float rowBottom = open ? expandRect_.bottom() : row.bottom();
            if (rowBottom < rowsTop_ || row.y > rowsBottom_) continue;
            const float y = row.y;
            // 1.47.0 (planche 07) : depliee - le violet tres pale, une barre a gauche (la carte peint le reste) ; choisie,
            // ou celle du clic droit : un violet plus pale encore.
            if (open) {
                r.fillRect(row, c.accent.withAlpha(18));
                r.fillRect({row.x, row.y, 3.f, row.h}, c.accent);
            } else if (v.version == selected_ || (menu_->isOpen() && v.version == menuVersion_)) {
                r.fillRect(row, c.accent.withAlpha(10));
            }
            {   // le chevron du depliage, devant la version
                const float ax = listRect_.x + 5.f, cy0 = y + 18.f;
                const Color chev = open ? c.accent : c.textMuted;
                if (open) {
                    r.line({ax, cy0 - 2.f}, {ax + 4.f, cy0 + 2.f}, chev, 1.5f);
                    r.line({ax + 4.f, cy0 + 2.f}, {ax + 8.f, cy0 - 2.f}, chev, 1.5f);
                } else {
                    r.line({ax + 2.f, cy0 - 4.f}, {ax + 6.f, cy0}, chev, 1.5f);
                    r.line({ax + 6.f, cy0}, {ax + 2.f, cy0 + 4.f}, chev, 1.5f);
                }
            }
            const FontId fv = gfx::semibold(kF15());
            r.drawText({cols[0], y + 10.f}, v.version, fv, c.text);
            if (i == 0) (void)pill(r, cols[0] + r.measure(v.version, fv).width + 10.f, y + 9.f, "actuelle", gfx::semibold(kF11), c.ok, c.ok.withAlpha(24), 20.f, true);
            if (v.important) r.drawText({cols[0], y + 32.f}, "importante", kF12, c.warning);
            r.drawText({cols[1], y + 8.f}, dateFr(v.published), kF13, c.text);
            r.drawText({cols[1], y + 28.f}, "par l'admin+", kF12, c.textMuted);
            float fy = y + 8.f;
            for (const auto& f : v.files) {
                std::string s = std::string(f.file.kind == "admin_plus" ? "Admin+" : f.file.kind == "admin" ? "Admin" : "Studios") + " \xC2\xB7 " + sizeFr(f.file.size);
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
            r.fillRect({listRect_.x + 1.f, rowBottom - 1.f, listRect_.w - 2.f, 1.f}, c.gridLine);
        }
        r.popClip();
    }
    static FontId kF15() { return FontId{15}; }

    // ------------------------------------------------------------------ 1.47.0 : la barre, les lignes, le depliage, le menu
    // Les colonnes vues par la barre (planche 12) : la version, la date, les trois puces, puis ce que la recherche lit.
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(7);
        f[0] = {.title = "Version", .kind = K::Text};
        f[1] = {.title = "Publi\xC3\xA9" "e", .kind = K::Period};
        f[2] = {.title = "Sorte", .chip = true, .noun = "une sorte", .order = {"version", "correctif"}};
        f[3] = {.title = "Importante", .chip = true, .order = {"importante", "normale"}};
        f[4] = {.title = "\xC3\x80 jour", .chip = true, .order = {"actuelle", "encore sur des PC", "plus aucun PC"}};
        f[5] = {.title = "Fichiers", .kind = K::Text};
        f[6] = {.title = "Notes", .kind = K::Text};
        return f;
    }
    // A jour : l'actuelle (la plus recente), sinon encore sur des PC, ou plus aucun.
    [[nodiscard]] std::string upToDateOf(std::size_t i) const {
        if (i == 0) return "actuelle";
        const auto it = view_.byVersion.find(view_.versions[i].version);
        return it != view_.byVersion.end() && it->second > 0 ? "encore sur des PC" : "plus aucun PC";
    }
    // Les cases d'une version (la barre, la ligne copiee, le CSV).
    [[nodiscard]] std::vector<std::string> cellsOf(std::size_t i) const {
        const auto& v = view_.versions[i];
        std::string files, notes;
        for (const auto& f : v.files) files += (files.empty() ? "" : " ; ") + f.file.name + (f.present ? std::string() : std::string(" (\xC3\xA0 renvoyer)"));
        for (const auto& n : v.notes) notes += (notes.empty() ? "" : " ; ") + n;
        return {v.version, dateFr(v.published), sortOf(v.version), v.important ? "importante" : "normale", upToDateOf(i), files, notes};
    }

    void rebuildRows() {
        shown_.clear();
        for (std::size_t i = 0; i < view_.versions.size(); ++i)
            if (bar_->accepts(i)) shown_.push_back(i);
        if (rowOf(selected_) < 0) selected_.clear();
        if (rowOf(expanded_) < 0) expanded_.clear();
        rebuildCard();
    }
    // La ligne montree d'une version (-1 : pas montree).
    [[nodiscard]] int rowOf(const std::string& v) const {
        if (v.empty()) return -1;
        for (std::size_t k = 0; k < shown_.size(); ++k)
            if (view_.versions[shown_[k]].version == v) return static_cast<int>(k);
        return -1;
    }
    [[nodiscard]] int selectedRow() const { return rowOf(selected_); }
    [[nodiscard]] static float rowHeight(std::size_t i) { return i == 0 ? 72.f : 60.f; }
    [[nodiscard]] int rowAt(gfx::Point p) const {
        if (p.y < rowsTop_ || p.y >= rowsBottom_ || p.x < listRect_.x || p.x > listRect_.right()) return -1;
        for (std::size_t k = 0; k < rowRects_.size(); ++k)
            if (rowRects_[k].contains(p)) return static_cast<int>(k);
        return -1;
    }

    // Deplier (une a la fois) ou replier.
    void unfold(const std::string& v, bool open) {
        expanded_ = open ? v : std::string();
        if (open) {
            selected_ = v;
            reveal_ = true;
        }
        rebuildCard();
    }
    // La carte de la ligne depliee, refaite ; l'ancienne attend le dessin suivant (elle peut etre en train d'emettre son clic).
    void rebuildCard() {
        if (card_) {
            grave_.push_back(clip_->removeChild(*card_));
            card_ = nullptr;
        }
        if (const int k = rowOf(expanded_); k >= 0)
            card_ = &static_cast<ui::UnfoldCard&>(clip_->addChild(makeCard(shown_[static_cast<std::size_t>(k)])));
        invalidateLayout();
        invalidate();
    }

    // La barre, puis les titres, puis les lignes (la depliee suivie de sa carte) ; la liste defile (la molette).
    void layoutList() {
        const float lx = listRect_.x + 18.f, lw = std::max(100.f, listRect_.w - 36.f);
        const float bh = bar_->heightFor(lw);
        bar_->setBounds({lx, listRect_.y + 44.f, lw, bh});
        headY_ = listRect_.y + 44.f + bh + 20.f;
        rowsTop_ = headY_ + 30.f;
        rowsBottom_ = std::max(rowsTop_, listRect_.bottom() - 4.f);
        const float cardH = card_ ? card_->heightFor(listRect_.w - 2.f) : 0.f;
        const int open = rowOf(expanded_), sel = selectedRow();
        std::vector<float> tops;
        float y = 0.f;
        for (std::size_t k = 0; k < shown_.size(); ++k) {
            tops.push_back(y);
            y += rowHeight(shown_[k]) + (static_cast<int>(k) == open ? cardH : 0.f);
        }
        const float view = rowsBottom_ - rowsTop_;
        if (reveal_) {   // la ligne choisie (et sa carte) dans la vue
            const int k = open >= 0 && (sel < 0 || sel == open) ? open : sel;
            if (k >= 0) {
                const auto ku = static_cast<std::size_t>(k);
                const float top = tops[ku], bottom = top + rowHeight(shown_[ku]) + (k == open ? cardH : 0.f);
                if (bottom - scroll_ > view) scroll_ = bottom - view;
                if (top - scroll_ < 0.f) scroll_ = top;
            }
            reveal_ = false;
        }
        scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, y - view));
        rowRects_.clear();
        expandRect_ = {};
        for (std::size_t k = 0; k < shown_.size(); ++k) {
            const Rect row{listRect_.x + 1.f, rowsTop_ + tops[k] - scroll_, listRect_.w - 2.f, rowHeight(shown_[k])};
            rowRects_.push_back(row);
            if (static_cast<int>(k) == open) expandRect_ = {row.x, row.bottom(), row.w, cardH};
        }
        const Rect area{listRect_.x, rowsTop_, listRect_.w, view};
        const Rect seen = expandRect_.empty() ? Rect{} : area.intersect(expandRect_);
        show(clip_, !seen.empty());
        clip_->setBounds(seen.empty() ? Rect{listRect_.x, rowsTop_, 0.f, 0.f} : seen);
        if (card_) card_->setBounds(expandRect_);
    }

    // ---- 1.47.0 (planche 07) : LA LIGNE DEPLIEE - les notes, les fichiers, les PC dessus ; Renvoyer..., les admins ----
    std::unique_ptr<ui::UnfoldCard> makeCard(std::size_t i) {
        const PublishedVersion& v = view_.versions[i];
        const Seconds now = ws().now();
        auto card = std::make_unique<ui::UnfoldCard>("versions.depliage");
        card->setOwnBackground(true);
        ui::UnfoldCard::Box notes;
        notes.title = "LES NOTES";
        notes.weight = 1.1f;
        const std::size_t few = std::min<std::size_t>(v.notes.size(), 5);
        for (std::size_t n = 0; n < few; ++n) notes.lines.push_back({.text = "\xE2\x80\xA2 " + v.notes[n]});
        if (v.notes.size() > few) notes.lines.push_back({.text = "et " + std::to_string(v.notes.size() - few) + " de plus (Copier \xE2\x80\xBA Les notes)"});
        if (v.notes.empty()) notes.lines.push_back({.text = "aucune note"});
        notes.lines.push_back({.icon = ui::Icon::History, .text = "publi\xC3\xA9" "e " + whenFr(now, v.published) + " par l'admin+"});
        if (v.important) notes.lines.push_back({.icon = ui::Icon::Warning, .text = "**importante** : rappel\xC3\xA9" "e \xC3\xA0 chaque lancement", .tone = ui::Tone::Warning});
        card->addBox(std::move(notes));

        // Chaque fichier : son nom (a renvoyer : en orange), puis sa taille, pour qui, le debut de son SHA-256.
        ui::UnfoldCard::Box files;
        files.title = "LES FICHIERS";
        files.weight = 1.3f;
        for (const auto& f : v.files) {
            files.lines.push_back({.icon = f.file.kind == "admin" || f.file.kind == "admin_plus" ? ui::Icon::Lock : ui::Icon::Import,
                                   .text = "**" + f.file.name + "**" + (f.present ? std::string() : std::string(" \xC2\xB7 \xC3\xA0 renvoyer")),
                                   .tone = f.present ? ui::Tone::None : ui::Tone::Warning});
            files.lines.push_back({.text = sizeFr(f.file.size) + " \xC2\xB7 " + (f.file.audience == "admins" ? "les admins" : "tout le monde") + " \xC2\xB7 SHA-256 "
                                           + (f.file.sha256.size() > 16 ? f.file.sha256.substr(0, 16) + "\xE2\x80\xA6" : f.file.sha256)});
        }
        if (!files.lines.empty()) card->addBox(std::move(files));

        ui::UnfoldCard::Box pcs;
        pcs.title = "LES PC DESSUS";
        pcs.weight = 1.3f;
        const auto at = view_.byVersion.find(v.version);
        const int n = at == view_.byVersion.end() ? 0 : at->second;
        if (i == 0)
            pcs.lines.push_back({.icon = ui::Icon::Ok, .text = "\xC3\xA0 jour : **" + std::to_string(view_.upToDate) + "** PC \xC2\xB7 pas \xC3\xA0 jour : **"
                                                               + std::to_string(std::max(0, view_.total - view_.upToDate)) + "** PC", .tone = ui::Tone::Ok});
        else
            pcs.lines.push_back({.icon = ui::Icon::Station, .text = n ? "encore sur **" + std::to_string(n) + "** PC" : std::string("plus aucun PC")});
        for (const auto& [software, counts] : view_.bySoftware)
            if (const auto c = counts.find(v.version); c != counts.end() && c->second > 0)
                pcs.lines.push_back({.icon = ui::Icon::Screen, .text = software + " : **" + std::to_string(c->second) + "** PC"});
        // Les admins : sur l'actuelle, qui est a jour et qui ne l'est pas ; sur une ancienne, ceux qui l'ont encore.
        std::vector<ui::UnfoldCard::Person> ok, late;
        for (const auto& s : view_.admins) {
            if (i == 0) (s.upToDate ? ok : late).push_back(listkit::person(host_, s.key, s.name, s.upToDate ? std::string() : (s.version.empty() ? std::string("?") : s.version)));
            else if (s.version == v.version) late.push_back(listkit::person(host_, s.key, s.name));
        }
        if (i == 0 && !ok.empty()) pcs.lines.push_back({.icon = ui::Icon::User, .text = "\xC3\xA0 jour :", .people = ok});
        if (!late.empty()) pcs.lines.push_back({.icon = ui::Icon::User, .text = i == 0 ? "pas \xC3\xA0 jour :" : "l'ont encore :", .tone = i == 0 ? ui::Tone::Warning : ui::Tone::None, .people = late});
        card->addBox(std::move(pcs));

        const bool busy = host_.busy && host_.busy();
        for (std::size_t r = 0; r < resend_.size(); ++r)
            if (resend_[r].version == v.version)
                card->addButton("Renvoyer " + std::string(resend_[r].file.kind == "admin_plus" ? "l'Admin+" : resend_[r].file.kind == "admin" ? "l'Admin" : "les Studios") + "\xE2\x80\xA6", "versions.depliage.renvoyer", ui::Icon::Export,
                                [this, r] { resendAt(r); }, busy ? std::string("un travail est en cours") : std::string());
        if (!view_.admins.empty()) card->addButton("Les admins", "versions.depliage.admins", ui::Icon::GoTo, [this] { host_.navigate("admins"); });
        card->addText("clic droit : le menu", true);
        return card;
    }

    // ---- 1.47.0 (planche 06) : le clic droit, dans l'ordre de la regle ----
    void openMenu(int k, gfx::Point at) {
        if (k < 0 || static_cast<std::size_t>(k) >= shown_.size()) return;
        const std::size_t i = shown_[static_cast<std::size_t>(k)];
        const PublishedVersion v = view_.versions[i];
        const std::string ver = v.version;
        menuVersion_ = ver;
        ui::ListMenu m({1, "versions", true});
        m.unfold(expanded_ == ver, [this, ver] { unfold(ver, expanded_ != ver); });
        const bool busy = host_.busy && host_.busy();
        for (std::size_t r = 0; r < resend_.size(); ++r)   // un fichier que le service n'a plus
            if (resend_[r].version == ver)
                m.action({.label = "Renvoyer " + resend_[r].file.name + "\xE2\x80\xA6", .icon = ui::Icon::Export, .why = busy ? std::string("un travail est en cours") : std::string(),
                          .run = [this, r] { resendAt(r); }});
        if (!view_.admins.empty()) m.goTo({.label = "Les admins (Administrateurs)", .icon = ui::Icon::User, .run = [this] { host_.navigate("admins"); }});
        if (i == 0) {
            int few = 0;
            for (const auto& s : view_.admins) {
                if (s.upToDate || s.key.empty() || ++few > 3) continue;
                m.goTo({.label = "Pas \xC3\xA0 jour : " + s.name + (s.version.empty() ? std::string() : " (" + s.version + ")"), .icon = ui::Icon::User, .run = [this, key = s.key] {
                            requestPageFocus("admins", "admin:" + key);
                            host_.navigate("admins");
                        }});
            }
        }
        const auto& t = texts_[i];
        const auto on = [&](std::size_t f, const std::string& label) {
            if (f >= t.size() || t[f].empty()) return;
            const std::string val = t[f];
            m.filterOn({.label = label + " : " + val, .run = [this, f, val] { bar_->filterOn(f, val); }});
        };
        on(2, "Cette sorte");
        on(3, "Cette importance");
        on(4, "\xC3\x80 jour");
        if (v.published > 0) {
            const std::int64_t when = listkit::seconds(v.published);
            m.filterOn({.label = "Ce jour : " + dateFr(v.published), .run = [this, when] { bar_->filterOnDay(1, when); }});
        }
        m.copy("La version", ver, ui::Icon::Copy);
        if (!v.notes.empty()) {
            std::string all;
            for (const auto& n : v.notes) all += n + "\n";
            m.copy("Les notes", all, ui::Icon::Document);
        }
        for (const auto& f : v.files)
            if (!f.file.sha256.empty()) m.copy(std::string("Le SHA-256 ") + (f.file.kind == "admin_plus" ? "de l'Admin+" : f.file.kind == "admin" ? "de l'Admin" : "des Studios"), f.file.sha256, ui::Icon::Lock);
        std::string line;
        for (std::size_t c = 0; c < t.size(); ++c) line += (c ? "\t" : "") + t[c];
        m.copy("La ligne", line + "\n", ui::Icon::Copy);
        m.exportRows([this, i] { exportVersions({i}); }, busy ? std::string("un travail est en cours") : std::string());
        m.danger({.label = "Supprimer\xE2\x80\xA6", .icon = ui::Icon::Delete, .why = kNoDelete});   // 1.48.1 : la, grise, avec sa raison
        menuList_ = m;
        menu_->setItems(menuList_.items(1));
        menu_->openInWindow(at);
        invalidate();
    }

    // Ces versions en CSV pour Excel (UTF-8 avec BOM, point-virgule) : l'explorateur, puis le fichier ; le resultat au bandeau.
    void exportVersions(const std::vector<std::size_t>& rows) {
        if (rows.empty()) return;
        if (host_.busy && host_.busy()) {
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
            return;
        }
        std::string csv = "\xEF\xBB\xBF";
        csv += "Version;Publi\xC3\xA9" "e;Sorte;Importante;\xC3\x80 jour;Fichiers;Notes\r\n";
        for (const std::size_t i : rows) {
            if (i >= texts_.size()) continue;
            for (std::size_t c = 0; c < texts_[i].size(); ++c) csv += (c ? ";" : "") + csvCell(texts_[i][c]);
            csv += "\r\n";
        }
        ui::FilePick pick;
        pick.title = "Exporter les versions pour Excel";
        pick.filters = {{"Tableau pour Excel (.csv)", "csv"}};
        pick.start = "versions.csv";
        pick.save = true;
        ShellHost host = host_;
        const std::size_t count = rows.size();
        host_.pickFile(pick, [csv, host, count](std::string path) {
            if (path.empty()) return;
            const auto dot = path.find_last_of("./\\");
            if (dot == std::string::npos || path[dot] != '.') path += ".csv";
            std::ofstream out(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
            if (out) out.write(csv.data(), static_cast<std::streamsize>(csv.size()));
            out.close();
            if (!out) {
                host.report(OpResult::fail("impossible d'\xC3\xA9" "crire " + path));
                return;
            }
            OpResult r = OpResult::done("Versions \xC3\xA9" "crites pour Excel : " + path + " (" + std::to_string(count) + (count > 1 ? " lignes)." : " ligne)."));
            r.output = path;
            host.report(r);
        });
    }

    // --------------------------------------------------------------------- l'etat
    bool                          panel_{false}, have_{false}, fetching_{false}, publishing_{false};
    bool                          stacked_{false};   // 1.46.0 : les deux boutons l'un sur l'autre (une colonne etroite)
    double                        lastFetch_{-1};
    VersionsView                  view_;
    std::vector<FileToResend>     resend_;
    std::string                   problem_, notesFor_;
    Picked                        studios_, admin_, plus_;   // 1.49.0 : admin_ = XPG Admin (sorte << admin >>), plus_ = l'Admin+
    std::shared_ptr<Progressing>  progress_;
    ui::Button*                   open_{nullptr};
    ui::Button*                   reload_{nullptr};
    ui::Button*                   delete_{nullptr};   // 1.48.1 : grise (kNoDelete)
    ui::Button*                   pickStudios_{nullptr};
    ui::Button*                   pickAdmin_{nullptr};
    ui::Button*                   pickPlus_{nullptr};
    ui::MultiLineText*            notes_{nullptr};
    Switch*                       important_{nullptr};
    ui::Button*                   cancel_{nullptr};
    ui::Button*                   sign_{nullptr};
    std::vector<ui::Button*>      resendBtns_;
    // 1.47.0 : la barre, les lignes montrees, la choisie, la depliee (par leur version), le menu du clic droit
    ui::FilterBar*                        bar_{nullptr};
    ClipBox*                              clip_{nullptr};
    ui::UnfoldCard*                       card_{nullptr};
    ui::PopupMenu*                        menu_{nullptr};
    ui::ListMenu                          menuList_;
    std::vector<ui::WidgetPtr>            grave_;            // les cartes remplacees (detruites au dessin suivant)
    std::vector<std::vector<std::string>> texts_;            // les cases de chaque version (la barre)
    std::vector<std::size_t>              shown_;            // les versions que la barre garde
    std::vector<Rect>                     rowRects_;
    Rect                                  expandRect_{};
    std::string                           selected_, expanded_, menuVersion_;
    float                                 headY_{0.f}, rowsTop_{0.f}, rowsBottom_{0.f}, scroll_{0.f};
    bool                                  reveal_{false};
    float                         x0_{0.f}, w_{0.f}, titleY_{0.f}, versionRow_{0.f};
    Rect                          panelRect_{}, leftRect_{}, rightRect_{}, zones_[3]{}, resendRect_{}, listRect_{};
    std::shared_ptr<bool>         alive_ = std::make_shared<bool>(true);
    core::ConnectionScope         links_;
};

} // namespace

PagePtr makeVersionsPage(ShellHost host) { return std::make_unique<VersionsPage>(std::move(host)); }

bool adoptVersionsView(ui::Widget& page, VersionsView view, std::vector<FileToResend> resend) {
    auto* p = dynamic_cast<VersionsPage*>(&page);
    if (!p) return false;
    p->takeView(std::move(view), std::move(resend));
    return true;
}

} // namespace license_admin::gui
