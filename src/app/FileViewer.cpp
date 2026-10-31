// =============================================================================
//  app/FileViewer.cpp - 1.21.0 : l'apercu de tous les fichiers, a l'ecran.
//                       Voir FileViewer.hpp.
// =============================================================================
#include "FileViewer.hpp"

#include "Dossiers.hpp"
#include "hmi/HmiSound.hpp"
#include "../hmi/HmiDocView.hpp"
#include "../hmi/HmiDocument.hpp"
#include "../hmi/HmiModel.hpp"
#include "../hmi/HmiPdf.hpp"
#include "../hmi/HmiRuntime.hpp"
#include "../hmi/HmiVideo.hpp"
#include "../ui/Shapes.hpp"
#include "../ui/Theme.hpp"
#include "../ui/Zoom.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace app {

namespace fv = fileview;

namespace {

namespace fs = std::filesystem;

double clockNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

gfx::Color mix(gfx::Color a, gfx::Color b, float t) {
    const auto m = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), a.a};
}

fs::path pathOf(std::string_view utf8) {
    const std::u8string u8(utf8.begin(), utf8.end());
    return fs::path(u8);
}

std::string textOf(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---- LE FIL DE FOND : un seul, une file ; un travail abandonne (un autre fichier choisi) ne se fait pas ----
struct Job {
    fv::Source        src;
    fv::Limits        lim;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    fv::Preview       result;
};

std::atomic<std::uint64_t> g_finished{0};

class Worker {
public:
    static Worker& instance() {
        static Worker w;
        return w;
    }
    void submit(std::shared_ptr<Job> job) {
        {
            std::lock_guard<std::mutex> lk(m_);
            queue_.push_back(std::move(job));
            if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
        }
        cv_.notify_one();
    }
    ~Worker() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
            for (auto& j : queue_) j->cancel = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

private:
    Worker() = default;
    void run() {
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            if (!job->cancel.load()) {
                job->lim.cancel = &job->cancel;
                try {
                    job->result = fv::load(job->src, job->lim);
                } catch (...) {
                    job->result = fv::Preview{};
                    job->result.name = job->src.name;
                    job->result.error = "illisible : erreur inattendue";
                }
            }
            job->done.store(true, std::memory_order_release);
            g_finished.fetch_add(1, std::memory_order_relaxed);
        }
    }
    std::mutex                       m_;
    std::condition_variable          cv_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::thread                      thread_;
    bool                             stop_{false};
};

// ---- LES TEXTURES ORPHELINES : celles d'un apercu detruit, rendues au prochain dessin du meme renderer ----
struct Orphan {
    const gfx::IRenderer* owner{nullptr};
    gfx::TextureId        id{};
};
std::mutex          g_orphansMutex;
std::vector<Orphan> g_orphans;

std::string colName(std::size_t c) {
    std::string s;
    ++c;
    while (c > 0) {
        const std::size_t r = (c - 1) % 26;
        s.insert(s.begin(), static_cast<char>('A' + r));
        c = (c - 1) / 26;
    }
    return s;
}

bool isTextKind(fv::Kind k) {
    return k == fv::Kind::Text || k == fv::Kind::Json || k == fv::Kind::Xml || k == fv::Kind::ControlExpert || k == fv::Kind::Binary;
}

} // namespace

// ============================================================================ l'etat ==
struct FileViewer::Impl {
    fv::Source                         source;
    std::string                        key;
    std::string                        hint;      // follow() : le choix suivi
    std::shared_ptr<Job>               job;
    std::shared_ptr<const fv::Preview> preview;
    bool                               showHeader{true}, allowSystem{true}, allowFolder{true};
    std::string                        emptyText{"Choisis un fichier pour voir son aper\xC3\xA7u."};
    std::string                        message;
    bool                               messageError{false};
    // l'image, le PDF : ajuste ou un zoom, le defilement (pixels)
    bool   fit{true};
    double zoom{1}, shownZoom{1};
    double scrollX{0}, scrollY{0};
    // le texte, les grilles, le zip (lignes) ; le DOCX (pixels) ; le decalage horizontal (pixels)
    double top{0}, left{0};
    double maxTop{0}, maxLeft{0}, maxScrollX{0}, maxScrollY{0};
    std::vector<bool> folded;
    std::size_t       sheet{0};
    std::vector<float> colWidths;
    std::size_t        widthsFor{static_cast<std::size_t>(-1)};
    // le DOCX mis en lignes a une largeur
    struct WLine {
        std::string text;
        int         level{0};
        bool        table{false};
        float       y{0}, h{0};
    };
    std::vector<WLine> wrapped;
    float              wrappedW{-1}, wrappedH{0};
    // le PDF
    std::shared_ptr<hmi::doc::Document> pdf;
    int                                 page{0};
    std::shared_ptr<const hmi::Rgba>    pdfImage;
    // la video
    std::unique_ptr<hmi::video::FrameSource> video;
    bool                                     videoFailed{false};
    std::string                              videoWhy;
    std::uint64_t                            videoSerial{0};
    bool                                     playing{false};
    double                                   playStart{0}, playOffset{0};
    // le son : le lecteur de l'appli, cette ressource le temps de l'ecoute
    hmi::Resource sound;
    // le GIF anime
    double gifStart{0};
    // les textures
    struct Tex {
        gfx::TextureId id{};
        int            w{0}, h{0};
        const void*    key{nullptr};
        std::uint64_t  serial{0};
        bool           tried{false};
        std::shared_ptr<const hmi::Rgba> hold;
    };
    const gfx::IRenderer*      owner{nullptr};
    std::map<std::string, Tex> textures;
    std::vector<gfx::TextureId> retired;
    // les gestes : ce qui se clique, releve au dernier dessin
    struct Hit {
        gfx::Rect   r;
        std::string action;
    };
    std::vector<Hit> hits;
    bool             animating{false};

    ~Impl() {
        stopSound();
        if (job) job->cancel = true;
        std::lock_guard<std::mutex> lk(g_orphansMutex);
        for (const auto& [k, t] : textures)
            if (t.id.v) g_orphans.push_back({owner, t.id});
        for (const auto id : retired)
            if (id.v) g_orphans.push_back({owner, id});
    }

    void retire(Tex& t) {
        if (t.id.v) retired.push_back(t.id);
        t = Tex{};
    }
    void retireAll() {
        for (auto& [k, t] : textures) retire(t);
        textures.clear();
    }
    void stopSound() {
        auto& player = HmiSoundPlayer::instance();
        if (sound.data && player.current() == sound.data.get()) player.stop();
    }
    void resetView() {
        fit = true;
        zoom = shownZoom = 1;
        scrollX = scrollY = top = left = 0;
        maxTop = maxLeft = maxScrollX = maxScrollY = 0;
        folded.clear();
        sheet = 0;
        colWidths.clear();
        widthsFor = static_cast<std::size_t>(-1);
        wrapped.clear();
        wrappedW = -1;
        pdf.reset();
        pdfImage.reset();
        page = 0;
        video.reset();
        videoFailed = false;
        videoWhy.clear();
        videoSerial = 0;
        playing = false;
        playOffset = 0;
        stopSound();
        sound = hmi::Resource{};
        message.clear();
        messageError = false;
        gifStart = clockNow();
    }
    // Le travail du fil de fond est fini : son apercu devient le courant.
    void poll() {
        if (!job || !job->done.load(std::memory_order_acquire)) return;
        auto p = std::make_shared<fv::Preview>(std::move(job->result));
        job.reset();
        adopt(std::move(p));
    }
    void adopt(std::shared_ptr<const fv::Preview> p) {
        preview = std::move(p);
        if (!preview) return;
        folded.assign(preview->lines.size(), false);
        if (preview->kind == fv::Kind::Pdf && preview->bytes)
            pdf = hmi::doc::openBytes(preview->bytes, hmi::doc::Type::Pdf, preview->name, "apercu:" + key);
        if (preview->bytes && (preview->kind == fv::Kind::Sound || preview->kind == fv::Kind::Video)) {
            sound.name = preview->name;
            sound.format = preview->media.format.empty() ? preview->format : preview->media.format;
            sound.data = preview->bytes;
            sound.bytes = preview->bytes->size();
            sound.seconds = preview->media.seconds;
        }
        gifStart = clockNow();
    }
};

// ==================================================================== la base ==
FileViewer::Palette FileViewer::paletteOf(const ui::Theme& t) {
    const auto& c = t.color;
    return {c.panelBg, c.inputBg, c.text, c.textMuted, c.accent, c.selectionText, c.error, c.warning, c.border};
}

FileViewer::Fonts FileViewer::fontsOf(const ui::Theme& t) {
    Fonts f;
    f.ui = t.font.ui;
    f.small = t.font.smallUi;
    f.mono = gfx::FontId{static_cast<std::uint16_t>(std::max(10, static_cast<int>(t.font.mono.v) - 2))};
    f.title = t.font.lead;
    return f;
}

FileViewer::Fonts FileViewer::fontsAt(double px) {
    const auto at = [](double v) { return gfx::FontId{static_cast<std::uint16_t>(std::clamp(std::lround(v), 6L, 200L))}; };
    Fonts f;
    f.ui = at(px);
    f.small = at(px * 0.86);
    f.mono = at(px * 0.92);
    f.title = at(px * 1.25);
    return f;
}

FileViewer::FileViewer() : impl_(std::make_unique<Impl>()) {}
FileViewer::~FileViewer() = default;

void FileViewer::show(fv::Source src) {
    auto& d = *impl_;
    const std::string k = src.empty() ? std::string{} : src.key();
    if (k == d.key && (d.preview || d.job || k.empty())) return;
    if (d.job) d.job->cancel = true;
    d.job.reset();
    d.resetView();
    d.retireAll();
    d.preview.reset();
    d.source = std::move(src);
    d.key = k;
    if (d.source.empty()) return;
    if (!fv::slow(d.source)) {
        d.adopt(std::make_shared<fv::Preview>(fv::load(d.source)));
        return;
    }
    d.job = std::make_shared<Job>();
    d.job->src = d.source;
    Worker::instance().submit(d.job);
}

void FileViewer::follow(const std::string& hint, const std::function<fv::Source()>& make) {
    if (hint == impl_->hint && (impl_->preview || impl_->job || impl_->source.empty())) return;
    impl_->hint = hint;
    show(make ? make() : fv::Source{});
}

void FileViewer::clear() { show(fv::Source{}); }

void FileViewer::reload() {
    auto src = impl_->source;
    impl_->key.clear();
    impl_->preview.reset();
    show(std::move(src));
}

const fv::Source& FileViewer::source() const noexcept { return impl_->source; }

bool FileViewer::loading() const {
    impl_->poll();
    return impl_->job != nullptr;
}

const fv::Preview* FileViewer::current() const {
    impl_->poll();
    return impl_->preview.get();
}

bool FileViewer::waitLoaded(int ms) {
    const double until = clockNow() + ms / 1000.0;
    while (loading()) {
        if (clockNow() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

void FileViewer::setShowHeader(bool on) noexcept { impl_->showHeader = on; }
void FileViewer::setActions(bool system, bool folder) noexcept {
    impl_->allowSystem = system;
    impl_->allowFolder = folder;
}
void FileViewer::setEmptyText(std::string text) { impl_->emptyText = std::move(text); }

bool FileViewer::animating() const { return impl_->animating || impl_->job != nullptr; }
const std::string& FileViewer::message() const noexcept { return impl_->message; }
std::size_t FileViewer::textureCount() const noexcept {
    std::size_t n = 0;
    for (const auto& [k, t] : impl_->textures) n += t.id.v != 0;
    return n;
}
std::size_t FileViewer::sheet() const noexcept { return impl_->sheet; }
int FileViewer::page() const noexcept { return impl_->page; }
double FileViewer::zoom() const noexcept { return impl_->fit ? impl_->shownZoom : impl_->zoom; }
bool FileViewer::fitted() const noexcept { return impl_->fit; }

// ================================================================== les gestes ==
bool FileViewer::openWithSystem(std::string* why) {
    auto& d = *impl_;
    const auto fail = [&](std::string m) {
        d.message = m;
        d.messageError = true;
        if (why) *why = std::move(m);
        return false;
    };
    const fv::Preview* p = current();
    if (!p) return fail("rien \xC3\xA0 ouvrir");
    std::string path = p->onDisk ? p->path : std::string{};
    if (path.empty() && !d.source.path.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(pathOf(d.source.path), ec)) path = d.source.path;
    }
    if (path.empty()) {
        // En memoire (une ressource, une entree de zip) : une copie temporaire, que le programme du systeme ouvre.
        if (!p->bytes) return fail("le contenu n'est pas en m\xC3\xA9moire : rien \xC3\xA0 ouvrir");
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "xpg-apercu";
        fs::create_directories(dir, ec);
        std::string name = p->name.empty() ? std::string("fichier") : p->name;
        for (auto& c : name)
            if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*') c = '_';
        const fs::path file = dir / pathOf(name);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(p->bytes->data()), static_cast<std::streamsize>(p->bytes->size()));
        out.close();
        if (!out) return fail("copie temporaire impossible : " + textOf(file));
        path = textOf(file);
    }
    const std::string error = dossiers::ouvrirDansLeSysteme(path);
    if (!error.empty()) return fail(error);
    d.message = "Ouvert avec le programme du syst\xC3\xA8me : " + p->name;
    d.messageError = false;
    return true;
}

bool FileViewer::openFolder(std::string* why) {
    auto& d = *impl_;
    std::string folder = d.source.openFolder;
    if (folder.empty() && !d.source.path.empty()) {
        const fs::path p = pathOf(d.source.path);
        std::error_code ec;
        folder = fs::is_directory(p, ec) ? d.source.path : textOf(p.parent_path());
    }
    if (folder.empty()) {
        d.message = "pas de dossier sur le disque (le contenu est en m\xC3\xA9moire)";
        d.messageError = true;
        if (why) *why = d.message;
        return false;
    }
    const std::string error = dossiers::ouvrirDansLeSysteme(folder);
    d.message = error.empty() ? "Dossier ouvert : " + folder : error;
    d.messageError = !error.empty();
    if (why) *why = d.message;
    return error.empty();
}

void FileViewer::zoomBy(double factor) {
    auto& d = *impl_;
    const double base = d.fit ? d.shownZoom : d.zoom;
    d.fit = false;
    d.zoom = std::clamp(base * factor, 0.05, 16.0);
}
void FileViewer::zoomFit() {
    impl_->fit = true;
    impl_->scrollX = impl_->scrollY = 0;
}
void FileViewer::zoomActual() {
    impl_->fit = false;
    impl_->zoom = 1;
}

bool FileViewer::setSheet(std::size_t index) {
    const auto* p = current();
    if (!p || index >= p->sheets.size()) return false;
    impl_->sheet = index;
    impl_->top = impl_->left = 0;
    impl_->widthsFor = static_cast<std::size_t>(-1);
    return true;
}

bool FileViewer::goPage(int delta) {
    auto& d = *impl_;
    if (!d.pdf || d.pdf->state() != hmi::doc::Document::State::Ready) return false;
    const int n = d.pdf->pageCount();
    const int to = std::clamp(d.page + delta, 0, std::max(0, n - 1));
    if (to == d.page) return false;
    d.page = to;
    d.scrollX = d.scrollY = 0;
    return true;
}

bool FileViewer::togglePlay(std::string* why) {
    auto& d = *impl_;
    const auto* p = current();
    if (!p) return false;
    if (p->kind == fv::Kind::Video) {
        if (d.playing) {
            d.playOffset += clockNow() - d.playStart;
            d.playing = false;
        } else {
            if (p->media.seconds > 0 && d.playOffset >= p->media.seconds - 0.05) d.playOffset = 0;
            d.playStart = clockNow();
            d.playing = true;
        }
        return true;
    }
    if (p->kind != fv::Kind::Sound || !d.sound.data) {
        if (why) *why = "ce n'est pas un son";
        return false;
    }
    auto& player = HmiSoundPlayer::instance();
    if (player.current() == d.sound.data.get() && player.playing(clockNow())) {
        player.stop();
        d.message = "Arr\xC3\xAAt\xC3\xA9";
        d.messageError = false;
        return true;
    }
    std::string reason;
    if (!player.play(d.sound, &reason)) {
        d.message = "Lecture impossible : " + reason;
        d.messageError = true;
        if (why) *why = reason;
        return false;
    }
    d.message.clear();
    return true;
}

bool FileViewer::toggleFold(std::size_t line) {
    auto& d = *impl_;
    const auto* p = current();
    if (!p || line >= p->depth.size() || !fv::opensBlock(p->depth, line)) return false;
    if (d.folded.size() != p->lines.size()) d.folded.assign(p->lines.size(), false);
    d.folded[line] = !d.folded[line];
    return true;
}

void FileViewer::scrollBy(double lines, double pixelsX) {
    auto& d = *impl_;
    const auto* p = current();
    if (!p) return;
    const bool imagey = p->kind == fv::Kind::Image || p->kind == fv::Kind::Pdf || p->kind == fv::Kind::Video;
    if (imagey) {
        d.scrollY = std::clamp(d.scrollY + lines * 20.0, 0.0, d.maxScrollY);
        d.scrollX = std::clamp(d.scrollX + pixelsX, 0.0, d.maxScrollX);
    } else if (p->kind == fv::Kind::Docx) {
        d.top = std::clamp(d.top + lines * 20.0, 0.0, d.maxTop);
    } else {
        d.top = std::clamp(d.top + lines, 0.0, d.maxTop);
        d.left = std::clamp(d.left + pixelsX, 0.0, d.maxLeft);
    }
}

std::vector<std::string> FileViewer::lines() const {
    auto& d = *impl_;
    d.poll();
    std::vector<std::string> out;
    if (d.job) {
        out.push_back("Nom : " + (d.source.name.empty() ? d.source.path : d.source.name));
        out.push_back("Chargement\xE2\x80\xA6");
        return out;
    }
    if (!d.preview) return out;
    out = fv::describeLines(*d.preview);
    if (d.pdf) {
        const auto st = d.pdf->state();
        if (st == hmi::doc::Document::State::Ready) out.push_back("Pages : " + std::to_string(d.pdf->pageCount()) + " (page " + std::to_string(d.page + 1) + ")");
        else if (st == hmi::doc::Document::State::Failed) out.push_back("PDF : " + d.pdf->error());
        else out.push_back("PDF : chargement\xE2\x80\xA6");
    }
    if (!d.message.empty()) out.push_back("Message : " + d.message);
    return out;
}

// ================================================================== le dessin ==
namespace {

struct Ctx {
    gfx::IRenderer&           r;
    const FileViewer::Palette& pal;
    const FileViewer::Fonts&   f;
    FileViewer::Impl&          d;
};

float lineH(const Ctx& c, gfx::FontId f) { return c.r.lineHeight(f); }
float width(const Ctx& c, std::string_view s, gfx::FontId f) { return c.r.measure(s, f).width; }

// Ce qui tient dans `w` pixels (les points de suite sinon), coupe sur un caractere entier.
std::string fit(const Ctx& c, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0) return {};
    if (width(c, s, f) <= w) return s;
    const char* dots = "\xE2\x80\xA6";
    std::size_t n = c.r.fitCharacters(s, f, std::max(0.f, w - width(c, dots, f)));
    n = std::min(n, s.size());
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n) + dots;
}

void text(const Ctx& c, float x, float y, const std::string& s, gfx::FontId f, gfx::Color col) { c.r.drawText({x, y}, s, f, col); }

// Un bouton : son cadre, son libelle ; actif, il se clique (`action`).
gfx::Rect button(const Ctx& c, float x, float y, const std::string& label, const std::string& action, bool enabled = true, bool on = false,
                 float minW = 0) {
    const float h = lineH(c, c.f.small) + 10.f;
    const float w = std::max(minW, label.empty() ? h : width(c, label, c.f.small) + 18.f);
    const gfx::Rect b{x, y, w, h};
    c.r.fillRoundedRect(b, on ? mix(c.pal.surface, c.pal.accent, 0.35f) : c.pal.surface, 4.f);
    c.r.strokeRect(b, on ? c.pal.accent : c.pal.border, 1.f);
    if (!label.empty())
        text(c, x + (w - width(c, label, c.f.small)) / 2.f, y + (h - lineH(c, c.f.small)) / 2.f, label, c.f.small, enabled ? c.pal.text : c.pal.muted);
    if (enabled) c.d.hits.push_back({b, action});
    return b;
}

// Un triangle (lecture, page suivante) ou deux barres (pause), dans `b`.
void glyph(const Ctx& c, const gfx::Rect& b, char what, gfx::Color col) {
    const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f, s = std::min(b.w, b.h) * 0.28f;
    if (what == '>') ui::shapes::fillPolygon(c.r, {{cx - s * 0.8f, cy - s}, {cx + s, cy}, {cx - s * 0.8f, cy + s}}, col);
    else if (what == '<') ui::shapes::fillPolygon(c.r, {{cx + s * 0.8f, cy - s}, {cx - s, cy}, {cx + s * 0.8f, cy + s}}, col);
    else if (what == '|') {
        c.r.fillRect({cx - s, cy - s, s * 0.7f, s * 2.f}, col);
        c.r.fillRect({cx + s * 0.3f, cy - s, s * 0.7f, s * 2.f}, col);
    }
}

void checker(const Ctx& c, const gfx::Rect& b) {
    const gfx::Color a = mix(c.pal.back, c.pal.text, 0.06f), bb = mix(c.pal.back, c.pal.text, 0.12f);
    c.r.pushClip(b);
    const float s = 10.f;
    for (float y = b.y; y < b.y + b.h; y += s)
        for (float x = b.x; x < b.x + b.w; x += s) {
            const bool first = (static_cast<int>((x - b.x) / s) + static_cast<int>((y - b.y) / s)) % 2 == 0;
            c.r.fillRect({x, y, s, s}, first ? a : bb);
        }
    c.r.popClip();
}

// Une texture pour une image ; la meme image : la meme texture ; une autre : l'ancienne rendue.
gfx::TextureId textureFor(const Ctx& c, const std::string& slot, const std::shared_ptr<const hmi::Rgba>& img) {
    auto& t = c.d.textures[slot];
    if (t.tried && t.key == img.get()) return t.id;
    c.d.retire(t);
    t.tried = true;
    t.key = img.get();
    t.hold = img;
    if (!img || img->width <= 0 || img->height <= 0) return {};
    t.id = c.r.createImage(img->pixels.data(), img->width, img->height);
    t.w = img->width;
    t.h = img->height;
    return t.id;
}

// Une barre de defilement (montree seulement) : `first` sur `total`, `shown` a la fois.
void scrollbar(const Ctx& c, const gfx::Rect& area, double first, double shown, double total) {
    if (total <= shown || total <= 0) return;
    const float h = std::max(18.f, static_cast<float>(area.h * shown / total));
    const float y = area.y + static_cast<float>((area.h - h) * std::clamp(first / std::max(1.0, total - shown), 0.0, 1.0));
    c.r.fillRoundedRect({area.x + area.w - 6.f, y, 4.f, h}, mix(c.pal.back, c.pal.text, 0.35f), 2.f);
}

// Un texte replie a la largeur (les mots ; un mot trop long, coupe).
std::vector<std::string> wrapText(const Ctx& c, const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string_view rest(s);
    while (!rest.empty()) {
        const auto nl = rest.find('\n');
        std::string_view part = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        if (part.empty()) {
            out.emplace_back();
            continue;
        }
        while (!part.empty()) {
            std::size_t n = std::min(part.size(), c.r.fitCharacters(part, f, std::max(10.f, w)));
            if (n < part.size()) {
                const auto sp = part.substr(0, n + 1).rfind(' ');
                if (sp != std::string_view::npos && sp > 0) n = sp;
                while (n > 0 && n < part.size() && (static_cast<unsigned char>(part[n]) & 0xC0) == 0x80) --n;
                if (n == 0) n = std::min<std::size_t>(part.size(), 1);
            }
            out.emplace_back(part.substr(0, n));
            part.remove_prefix(n);
            while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        }
    }
    return out;
}

// ---- les genres ----
void drawImage(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    // la barre : -, +, Ajuster, 100 %
    float x = area.x;
    const float y = area.y;
    const auto bm = button(c, x, y, "\xE2\x88\x92", "zoom-", true, false, lineH(c, c.f.small) + 14.f);
    x += bm.w + 4;
    x += button(c, x, y, "+", "zoom+", true, false, bm.w).w + 4;
    x += button(c, x, y, "Ajuster", "fit", true, d.fit).w + 4;
    x += button(c, x, y, "100 %", "actual", true, !d.fit && std::abs(d.zoom - 1.0) < 1e-6).w + 8;
    // l'image montree : un GIF anime tourne, sinon l'image
    std::shared_ptr<const hmi::Rgba> img = p.image;
    if (!p.frames.empty()) {
        int total = 0;
        for (const int ms : p.frameMs) total += ms;
        double at = std::fmod((clockNow() - d.gifStart) * 1000.0, std::max(1, total));
        std::size_t i = 0;
        for (; i + 1 < p.frames.size() && at >= p.frameMs[i]; ++i) at -= p.frameMs[i];
        img = p.frames[i];
        d.animating = true;
    }
    const int ow = p.media.width > 0 ? p.media.width : (img ? img->width : 0);
    const int oh = p.media.height > 0 ? p.media.height : (img ? img->height : 0);
    area.y += bm.h + 8;
    area.h -= bm.h + 8;
    if (!img || ow <= 0 || oh <= 0 || area.h < 8) return;
    const double fitZoom = std::min({area.w / static_cast<double>(ow), area.h / static_cast<double>(oh), 4.0});
    const double z = d.fit ? fitZoom : d.zoom;
    d.shownZoom = z;
    const char* label = d.fit ? "ajust\xC3\xA9 " : "";
    text(c, x, y + 5, std::string(label) + std::to_string(static_cast<int>(std::lround(z * 100))) + " %", c.f.small, c.pal.muted);
    const float w = static_cast<float>(ow * z), h = static_cast<float>(oh * z);
    d.maxScrollX = std::max(0.0, static_cast<double>(w - area.w));
    d.maxScrollY = std::max(0.0, static_cast<double>(h - area.h));
    d.scrollX = std::clamp(d.scrollX, 0.0, d.maxScrollX);
    d.scrollY = std::clamp(d.scrollY, 0.0, d.maxScrollY);
    const gfx::Rect dst{w <= area.w ? area.x + (area.w - w) / 2.f : area.x - static_cast<float>(d.scrollX),
                        h <= area.h ? area.y + (area.h - h) / 2.f : area.y - static_cast<float>(d.scrollY), w, h};
    c.r.pushClip(area);
    checker(c, {std::max(dst.x, area.x), std::max(dst.y, area.y), std::min(dst.right(), area.right()) - std::max(dst.x, area.x),
                std::min(dst.bottom(), area.bottom()) - std::max(dst.y, area.y)});
    const auto tex = textureFor(c, "image", img);
    if (tex.v) c.r.drawImage(tex, dst);
    else c.r.strokeRect(dst, c.pal.border, 1.f);
    c.r.popClip();
}

void drawVideo(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    const double now = clockNow();
    const double dur = p.media.seconds > 0 ? p.media.seconds : (d.video ? d.video->duration() : 0.0);
    double t = d.playing ? d.playOffset + (now - d.playStart) : d.playOffset;
    if (dur > 0 && t >= dur) {
        t = dur;
        if (d.playing) {
            d.playing = false;
            d.playOffset = dur;
        }
    }
    const auto b = button(c, area.x, area.y, "", "play", p.bytes != nullptr, d.playing);
    glyph(c, b, d.playing ? '|' : '>', c.pal.text);
    text(c, b.right() + 8, area.y + 5, hmi::formatDuration(t) + " / " + (dur > 0 ? hmi::formatDuration(dur) : std::string("?")), c.f.small, c.pal.muted);
    area.y += b.h + 8;
    area.h -= b.h + 8;
    if (area.h < 8) return;
    c.r.fillRect(area, mix(c.pal.back, gfx::Color{0, 0, 0, 255}, 0.5f));
    if (!d.video && !d.videoFailed && p.bytes) {
        d.video = hmi::video::openFrameSource(p.bytes, p.media.format.empty() ? p.format : p.media.format, &d.videoWhy);
        d.videoFailed = !d.video;
    }
    const hmi::Rgba* frame = nullptr;
    std::uint64_t serial = 0;
    if (d.video) {
        frame = d.video->frameAt(t, serial);
        if (!frame && d.video->failed(&d.videoWhy)) d.videoFailed = true;
    }
    auto& tex = d.textures["video"];
    if (frame && (serial != d.videoSerial || !tex.id.v)) {
        d.retire(tex);
        tex.id = c.r.createImage(frame->pixels.data(), frame->width, frame->height);
        tex.w = frame->width;
        tex.h = frame->height;
        tex.tried = true;
        d.videoSerial = serial;
    }
    if (tex.id.v && tex.w > 0 && tex.h > 0) {
        const float s = std::min(area.w / static_cast<float>(tex.w), area.h / static_cast<float>(tex.h));
        const float w = tex.w * s, h = tex.h * s;
        c.r.drawImage(tex.id, {area.x + (area.w - w) / 2.f, area.y + (area.h - h) / 2.f, w, h});
    } else {
        const std::string said = d.videoFailed ? (d.videoWhy.empty() ? std::string("cette vid\xC3\xA9o ne se d\xC3\xA9" "code pas ici") : d.videoWhy)
                                               : std::string("Chargement de l'image\xE2\x80\xA6");
        for (const auto& l : wrapText(c, said, c.f.small, area.w - 24)) {
            text(c, area.x + 12, area.y + 12, l, c.f.small, gfx::Color{230, 230, 230, 255});
            area.y += lineH(c, c.f.small) + 2;
        }
    }
    if (d.playing || (d.video && !frame && !d.videoFailed)) d.animating = true;
}

void drawSound(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    auto& player = HmiSoundPlayer::instance();
    const double now = clockNow();
    const bool mine = d.sound.data && player.current() == d.sound.data.get();
    const bool playing = mine && player.playing(now);
    const auto b = button(c, area.x, area.y, "", "play", d.sound.data != nullptr, playing);
    glyph(c, b, playing ? '|' : '>', c.pal.text);
    const double dur = p.media.seconds;
    const double at = playing ? player.position(now) : 0.0;
    text(c, b.right() + 8, area.y + 5, hmi::formatDuration(at) + " / " + hmi::formatDuration(dur), c.f.small, c.pal.muted);
    area.y += b.h + 8;
    area.h = std::min(area.h - b.h - 8, std::max(60.f, area.h * 0.6f));
    if (area.h < 8) return;
    c.r.fillRect(area, mix(c.pal.back, c.pal.text, 0.04f));
    const float mid = area.y + area.h / 2;
    c.r.line({area.x, mid}, {area.x + area.w, mid}, mix(c.pal.back, c.pal.text, 0.25f), 1.f);
    const std::size_t buckets = p.envelope.size() / 2;
    for (std::size_t k = 0; k < buckets; ++k) {
        const float x = area.x + static_cast<float>(k) * area.w / static_cast<float>(buckets);
        const float lo = p.envelope[k * 2], hi = p.envelope[k * 2 + 1];
        c.r.fillRect({x, mid - hi * area.h / 2, std::max(1.f, area.w / static_cast<float>(buckets) + 0.5f), std::max(1.f, (hi - lo) * area.h / 2)}, c.pal.accent);
    }
    if (buckets == 0) text(c, area.x + 10, area.y + 10, "Onde indisponible", c.f.small, c.pal.muted);
    if (playing && dur > 0) {
        const float x = area.x + static_cast<float>(std::clamp(at / dur, 0.0, 1.0)) * area.w;
        c.r.fillRect({x - 1, area.y, 2, area.h}, c.pal.warning);
        d.animating = true;
    }
}

void drawPdf(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    if (!d.pdf) {
        text(c, area.x, area.y, "Le PDF n'est pas en m\xC3\xA9moire (trop gros ?) : Ouvrir avec le programme du syst\xC3\xA8me", c.f.ui, c.pal.muted);
        return;
    }
    (void)p;
    const auto st = d.pdf->state();
    if (st == hmi::doc::Document::State::Loading) {
        text(c, area.x, area.y, "Chargement du PDF\xE2\x80\xA6", c.f.ui, c.pal.muted);
        d.animating = true;
        return;
    }
    if (st == hmi::doc::Document::State::Failed) {
        const std::string why = d.pdf->needsEngine() ? hmi::pdf::missingAdvice() : "PDF illisible : " + d.pdf->error();
        float y = area.y;
        for (const auto& l : wrapText(c, why, c.f.ui, area.w)) {
            text(c, area.x, y, l, c.f.ui, c.pal.error);
            y += lineH(c, c.f.ui) + 2;
        }
        return;
    }
    const int n = d.pdf->pageCount();
    d.page = std::clamp(d.page, 0, std::max(0, n - 1));
    float x = area.x;
    const float y = area.y;
    const auto prev = button(c, x, y, "", "page-", d.page > 0);
    glyph(c, prev, '<', d.page > 0 ? c.pal.text : c.pal.muted);
    x += prev.w + 4;
    const std::string label = "Page " + std::to_string(d.page + 1) + " / " + std::to_string(n);
    text(c, x + 4, y + 5, label, c.f.small, c.pal.text);
    x += width(c, label, c.f.small) + 12;
    const auto next = button(c, x, y, "", "page+", d.page + 1 < n);
    glyph(c, next, '>', d.page + 1 < n ? c.pal.text : c.pal.muted);
    x += next.w + 10;
    x += button(c, x, y, "\xE2\x88\x92", "zoom-", true, false, prev.w).w + 4;
    x += button(c, x, y, "+", "zoom+", true, false, prev.w).w + 4;
    x += button(c, x, y, "Ajuster", "fit", true, d.fit).w + 8;
    area.y += prev.h + 8;
    area.h -= prev.h + 8;
    const auto size = d.pdf->pageSize(d.page);
    if (size.w <= 0 || size.h <= 0 || area.h < 8) return;
    const double fitZoom = std::min(area.w / size.w, area.h / size.h);
    const double z = d.fit ? fitZoom : d.zoom;
    d.shownZoom = z;
    text(c, x, y + 5, std::to_string(static_cast<int>(std::lround(z * 100))) + " %", c.f.small, c.pal.muted);
    const float w = static_cast<float>(size.w * z), h = static_cast<float>(size.h * z);
    d.maxScrollX = std::max(0.0, static_cast<double>(w - area.w));
    d.maxScrollY = std::max(0.0, static_cast<double>(h - area.h));
    d.scrollX = std::clamp(d.scrollX, 0.0, d.maxScrollX);
    d.scrollY = std::clamp(d.scrollY, 0.0, d.maxScrollY);
    const gfx::Rect dst{w <= area.w ? area.x + (area.w - w) / 2.f : area.x - static_cast<float>(d.scrollX),
                        h <= area.h ? area.y + (area.h - h) / 2.f : area.y - static_cast<float>(d.scrollY), w, h};
    c.r.pushClip(area);
    c.r.fillRect(dst, gfx::Color{255, 255, 255, 255});   // une page blanche, le temps que la page arrive (la couleur du papier)
    const auto pic = d.pdf->page(d.page, z * c.r.dpiScale());
    if (pic.image) {
        d.pdfImage = pic.image;
        const auto tex = textureFor(c, "pdf", pic.image);
        if (tex.v) c.r.drawImage(tex, dst);
    }
    if (!pic.exact) d.animating = true;
    c.r.strokeRect(dst, c.pal.border, 1.f);
    c.r.popClip();
}

void drawFont(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    float y = area.y;
    for (std::size_t i = 0; i < p.fontSamples.size(); ++i) {
        const auto& img = p.fontSamples[i];
        if (!img || y > area.bottom()) break;
        const auto tex = textureFor(c, "police" + std::to_string(i), img);
        const float w = std::min(static_cast<float>(img->width), area.w);
        const float h = static_cast<float>(img->height) * (w / static_cast<float>(std::max(1, img->width)));
        if (tex.v) c.r.drawImage(tex, {area.x, y, w, h}, 0.f, false, false, c.pal.text);
        else text(c, area.x, y, i < p.fontSampleTexts.size() ? p.fontSampleTexts[i] : std::string{}, c.f.ui, c.pal.text);
        y += h + 8;
    }
}

void drawLines(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    const bool tree = !p.depth.empty() && p.depth.size() == p.lines.size();
    const bool hex = p.kind == fv::Kind::Binary;
    if (tree) {
        float x = area.x;
        x += button(c, x, area.y, "Tout replier", "foldall").w + 4;
        const auto b = button(c, x, area.y, "Tout d\xC3\xA9plier", "unfoldall");
        area.y += b.h + 6;
        area.h -= b.h + 6;
    }
    if (d.folded.size() != p.lines.size()) d.folded.assign(p.lines.size(), false);
    std::vector<std::size_t> shown;
    if (tree) shown = fv::visibleLines(p.depth, d.folded);
    else {
        shown.resize(p.lines.size());
        for (std::size_t i = 0; i < shown.size(); ++i) shown[i] = i;
    }
    const gfx::FontId f = c.f.mono;
    const float lh = lineH(c, f) + 2.f;
    const std::size_t rows = static_cast<std::size_t>(std::max(1.f, std::floor(area.h / lh)));
    d.maxTop = static_cast<double>(shown.size() > rows ? shown.size() - rows : 0);
    d.top = std::clamp(std::floor(d.top), 0.0, d.maxTop);
    const float gutter = hex ? 0.f : width(c, std::to_string(p.lines.size()), c.f.small) + 14.f;
    const float marker = tree ? lineH(c, f) : 0.f;
    const float indent = tree ? width(c, "  ", f) : 0.f;
    c.r.pushClip(area);
    if (!hex) c.r.fillRect({area.x, area.y, gutter - 4.f, area.h}, mix(c.pal.back, c.pal.text, 0.04f));
    const float tx = area.x + gutter + marker;
    float widest = 0;
    for (std::size_t k = 0; k < rows && static_cast<std::size_t>(d.top) + k < shown.size(); ++k) {
        const std::size_t i = shown[static_cast<std::size_t>(d.top) + k];
        const float y = area.y + static_cast<float>(k) * lh;
        if (!hex) {
            const std::string no = std::to_string(i + 1);
            text(c, area.x + gutter - 8.f - width(c, no, c.f.small), y + 1, no, c.f.small, c.pal.muted);
        }
        const float ind = tree ? indent * static_cast<float>(p.depth[i]) : 0.f;
        if (tree && fv::opensBlock(p.depth, i)) {
            const gfx::Rect m{area.x + gutter + ind - static_cast<float>(d.left), y, marker, lh};
            const float s = marker * 0.22f, cx = m.x + m.w / 2.f, cy = m.y + m.h / 2.f;
            if (d.folded[i]) ui::shapes::fillPolygon(c.r, {{cx - s * 0.7f, cy - s}, {cx + s, cy}, {cx - s * 0.7f, cy + s}}, c.pal.muted);
            else ui::shapes::fillPolygon(c.r, {{cx - s, cy - s * 0.7f}, {cx + s, cy - s * 0.7f}, {cx, cy + s}}, c.pal.muted);
            d.hits.push_back({m, "fold:" + std::to_string(i)});
        }
        c.r.pushClip({area.x + gutter, area.y, area.w - gutter, area.h});
        const float lx = tx + ind - static_cast<float>(d.left);
        text(c, lx, y, p.lines[i], f, c.pal.text);
        float w = ind + width(c, p.lines[i], f);
        if (tree && d.folded[i] && fv::opensBlock(p.depth, i)) {
            text(c, lx + width(c, p.lines[i], f) + 6.f, y, "\xE2\x80\xA6", f, c.pal.muted);
            w += 20.f;
        }
        widest = std::max(widest, w);
        c.r.popClip();
    }
    c.r.popClip();
    d.maxLeft = std::max(0.0, static_cast<double>(widest - (area.w - gutter - marker) + 20.f));
    d.left = std::clamp(d.left, 0.0, std::max(d.maxLeft, d.left > 0 ? d.left : 0.0));
    scrollbar(c, area, d.top, static_cast<double>(rows), static_cast<double>(shown.size()));
}

void drawGrid(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    if (p.sheets.empty()) return;
    d.sheet = std::min(d.sheet, p.sheets.size() - 1);
    // les onglets (un classeur)
    if (p.kind == fv::Kind::Workbook) {
        float x = area.x;
        float rowH = 0;
        for (std::size_t i = 0; i < p.sheets.size(); ++i) {
            const std::string label = p.sheets[i].name + (p.sheets[i].hidden ? " (masqu\xC3\xA9)" : "");
            const float w = width(c, label, c.f.small) + 18.f;
            if (x + w > area.right() && x > area.x) {
                x = area.x;
                area.y += rowH + 4;
                area.h -= rowH + 4;
            }
            const auto b = button(c, x, area.y, label, "sheet:" + std::to_string(i), true, i == d.sheet);
            rowH = b.h;
            x += b.w + 4;
        }
        area.y += rowH + 6;
        area.h -= rowH + 6;
    }
    const auto& sh = p.sheets[d.sheet];
    const gfx::FontId f = c.f.small;
    const float rh = lineH(c, f) + 6.f;
    // les largeurs des colonnes : leur contenu (les 100 premieres lignes), une fois par onglet
    if (d.widthsFor != d.sheet || d.colWidths.size() != sh.columns) {
        d.colWidths.assign(sh.columns, 0.f);
        for (std::size_t r = 0; r < sh.rows.size() && r < 100; ++r)
            for (std::size_t k = 0; k < sh.rows[r].size() && k < sh.columns; ++k)
                d.colWidths[k] = std::max(d.colWidths[k], width(c, sh.rows[r][k], f));
        for (auto& w : d.colWidths) w = std::clamp(w + 14.f, 48.f, 280.f);
        d.widthsFor = d.sheet;
    }
    const std::string lastNo = std::to_string(sh.rows.size());
    const float rowHead = width(c, lastNo, f) + 16.f;
    float total = rowHead;
    for (const float w : d.colWidths) total += w;
    const float footer = sh.truncated ? rh : 0.f;
    const gfx::Rect grid{area.x, area.y, area.w, std::max(0.f, area.h - footer)};
    const std::size_t rows = static_cast<std::size_t>(std::max(1.f, std::floor((grid.h - rh) / rh)));
    d.maxTop = static_cast<double>(sh.rows.size() > rows ? sh.rows.size() - rows : 0);
    d.top = std::clamp(std::floor(d.top), 0.0, d.maxTop);
    d.maxLeft = std::max(0.0, static_cast<double>(total - grid.w));
    d.left = std::clamp(d.left, 0.0, d.maxLeft);
    c.r.pushClip(grid);
    const gfx::Color headBg = mix(c.pal.back, c.pal.text, 0.08f), alt = mix(c.pal.back, c.pal.text, 0.03f), line = mix(c.pal.back, c.pal.text, 0.16f);
    c.r.fillRect({grid.x, grid.y, grid.w, rh}, headBg);
    // les titres A, B, C...
    float x = grid.x + rowHead - static_cast<float>(d.left);
    for (std::size_t k = 0; k < d.colWidths.size(); ++k) {
        const float w = d.colWidths[k];
        if (x + w >= grid.x + rowHead && x <= grid.right()) {
            const std::string n = colName(k);
            text(c, x + (w - width(c, n, f)) / 2.f, grid.y + 3, n, f, c.pal.muted);
            c.r.line({x + w, grid.y}, {x + w, grid.bottom()}, line, 1.f);
        }
        x += w;
    }
    for (std::size_t k = 0; k < rows && static_cast<std::size_t>(d.top) + k < sh.rows.size(); ++k) {
        const std::size_t r = static_cast<std::size_t>(d.top) + k;
        const float y = grid.y + rh + static_cast<float>(k) * rh;
        if (r % 2 == 1) c.r.fillRect({grid.x, y, grid.w, rh}, alt);
        c.r.pushClip({grid.x + rowHead, grid.y, std::max(0.f, grid.w - rowHead), grid.h});
        float cx = grid.x + rowHead - static_cast<float>(d.left);
        for (std::size_t col = 0; col < d.colWidths.size(); ++col) {
            const float w = d.colWidths[col];
            if (col < sh.rows[r].size() && cx + w >= grid.x + rowHead && cx <= grid.right())
                text(c, cx + 6, y + 3, fit(c, sh.rows[r][col], f, w - 10.f), f, r == 0 ? c.pal.text : c.pal.text);
            cx += w;
        }
        c.r.popClip();
        c.r.fillRect({grid.x, y, rowHead, rh}, headBg);
        const std::string no = std::to_string(r + 1);
        text(c, grid.x + rowHead - 8.f - width(c, no, f), y + 3, no, f, c.pal.muted);
        c.r.line({grid.x, y + rh}, {grid.right(), y + rh}, line, 1.f);
    }
    c.r.popClip();
    scrollbar(c, grid, d.top, static_cast<double>(rows), static_cast<double>(sh.rows.size()));
    if (sh.truncated)
        text(c, area.x, grid.bottom() + 3,
             "Les " + std::to_string(sh.rows.size()) + " premi\xC3\xA8res lignes" + (sh.totalRows > sh.rows.size() ? " sur " + std::to_string(sh.totalRows) : std::string{}),
             f, c.pal.muted);
}

void drawZip(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    const gfx::FontId f = c.f.small;
    const float rh = lineH(c, f) + 6.f;
    const float sizeW = width(c, "000,0 Mo", f) + 24.f;
    const std::size_t rows = static_cast<std::size_t>(std::max(1.f, std::floor((area.h - rh) / rh)));
    d.maxTop = static_cast<double>(p.entries.size() > rows ? p.entries.size() - rows : 0);
    d.top = std::clamp(std::floor(d.top), 0.0, d.maxTop);
    c.r.pushClip(area);
    const gfx::Color headBg = mix(c.pal.back, c.pal.text, 0.08f), alt = mix(c.pal.back, c.pal.text, 0.03f);
    c.r.fillRect({area.x, area.y, area.w, rh}, headBg);
    const float nameW = std::max(60.f, area.w - 2 * sizeW);
    text(c, area.x + 6, area.y + 3, "Nom", f, c.pal.muted);
    text(c, area.x + nameW + sizeW - 8 - width(c, "Taille", f), area.y + 3, "Taille", f, c.pal.muted);
    text(c, area.x + nameW + 2 * sizeW - 8 - width(c, "Compress\xC3\xA9", f), area.y + 3, "Compress\xC3\xA9", f, c.pal.muted);
    for (std::size_t k = 0; k < rows && static_cast<std::size_t>(d.top) + k < p.entries.size(); ++k) {
        const auto& e = p.entries[static_cast<std::size_t>(d.top) + k];
        const float y = area.y + rh + static_cast<float>(k) * rh;
        if (k % 2 == 1) c.r.fillRect({area.x, y, area.w, rh}, alt);
        const std::string name = e.name + (e.encrypted ? "  (chiffr\xC3\xA9)" : "");
        text(c, area.x + 6, y + 3, fit(c, name, f, nameW - 12), f, e.folder ? c.pal.muted : c.pal.text);
        if (!e.folder) {
            const std::string s = hmi::explorer::sizeText(e.size), z = hmi::explorer::sizeText(e.packed);
            text(c, area.x + nameW + sizeW - 8 - width(c, s, f), y + 3, s, f, c.pal.text);
            text(c, area.x + nameW + 2 * sizeW - 8 - width(c, z, f), y + 3, z, f, c.pal.muted);
        }
    }
    c.r.popClip();
    scrollbar(c, area, d.top, static_cast<double>(rows), static_cast<double>(p.entries.size()));
}

void drawDocx(const Ctx& c, const fv::Preview& p, gfx::Rect area) {
    auto& d = c.d;
    const float w = area.w - 14.f;
    if (std::abs(d.wrappedW - w) > 0.5f) {
        d.wrapped.clear();
        float y = 0;
        for (const auto& para : p.paragraphs) {
            const gfx::FontId f = para.level == 1 ? c.f.title : para.level == 2 ? c.f.ui : para.table ? c.f.small : c.f.ui;
            const std::string body = (para.item ? "\xE2\x80\xA2 " : "") + para.text;
            if (para.level > 0) y += lineH(c, f) * 0.4f;
            for (const auto& l : wrapText(c, body, f, w - (para.item ? 14.f : 0.f))) {
                FileViewer::Impl::WLine wl;
                wl.text = l;
                wl.level = para.level;
                wl.table = para.table;
                wl.y = y;
                wl.h = lineH(c, f) + 2.f;
                y += wl.h;
                d.wrapped.push_back(std::move(wl));
            }
            if (para.text.empty()) y += lineH(c, c.f.ui) * 0.5f;
            y += para.table ? 2.f : lineH(c, c.f.ui) * 0.35f;
        }
        d.wrappedW = w;
        d.wrappedH = y;
    }
    d.maxTop = std::max(0.0, static_cast<double>(d.wrappedH - area.h));
    d.top = std::clamp(d.top, 0.0, d.maxTop);
    c.r.pushClip(area);
    for (const auto& l : d.wrapped) {
        const float y = area.y + l.y - static_cast<float>(d.top);
        if (y + l.h < area.y) continue;
        if (y > area.bottom()) break;
        const gfx::FontId f = l.level == 1 ? c.f.title : l.level == 2 ? c.f.ui : l.table ? c.f.small : c.f.ui;
        const gfx::Color col = l.level > 0 ? c.pal.accent : c.pal.text;
        if (l.table) c.r.fillRect({area.x, y, w, l.h}, mix(c.pal.back, c.pal.text, 0.04f));
        text(c, area.x + 4, y, l.text, f, col);
        if (l.level > 0 && l.level <= 2) text(c, area.x + 4.5f, y, l.text, f, col);   // le gras simule (l'appli n'a pas de face grasse)
    }
    c.r.popClip();
    scrollbar(c, area, d.top, area.h, d.wrappedH);
}

} // namespace

void FileViewer::paint(gfx::IRenderer& r, const gfx::Rect& area, const Palette& pal, const Fonts& fonts) {
    auto& d = *impl_;
    d.poll();
    d.hits.clear();
    d.animating = false;
    // Les textures : celles du renderer de la surface (sous un zoom de page) ; un autre renderer : on repart de zero.
    gfx::IRenderer& base = ui::unzoomed(r);
    if (d.owner != &base) {
        d.textures.clear();
        d.retired.clear();
        d.owner = &base;
    }
    for (const auto id : d.retired)
        if (id.v) base.releaseImage(id);
    d.retired.clear();
    {
        std::lock_guard<std::mutex> lk(g_orphansMutex);
        for (auto it = g_orphans.begin(); it != g_orphans.end();) {
            if (it->owner == &base) {
                if (it->id.v) base.releaseImage(it->id);
                it = g_orphans.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (area.w < 8 || area.h < 8) return;
    const Ctx c{r, pal, fonts, d};
    r.pushClip(area);
    r.fillRect(area, pal.back);
    const float pad = std::clamp(area.w * 0.02f, 6.f, 12.f);
    float x = area.x + pad, y = area.y + pad;
    const float w = area.w - 2 * pad;
    const fv::Preview* p = d.preview.get();
    if (d.source.empty() && !p) {
        float yy = y;
        for (const auto& l : wrapText(c, d.emptyText, fonts.ui, w)) {
            text(c, x, yy, l, fonts.ui, pal.muted);
            yy += lineH(c, fonts.ui) + 2;
        }
        r.popClip();
        return;
    }
    // ---- l'en-tete : le nom, le type, la taille, la date
    if (d.showHeader) {
        const std::string name = p ? p->name : (d.source.name.empty() ? d.source.path : d.source.name);
        text(c, x, y, fit(c, name, fonts.title, w), fonts.title, pal.text);
        y += lineH(c, fonts.title) + 2;
        std::string sub;
        if (p) {
            sub = p->typeLabel;
            if (p->kind != fv::Kind::Folder && p->kind != fv::Kind::Missing) sub += "  \xC2\xB7  " + hmi::explorer::sizeText(p->size);
            if (p->modified > 0) sub += "  \xC2\xB7  " + hmi::explorer::dateText(p->modified);
            if (!p->origin.empty()) sub += "  \xC2\xB7  " + p->origin;
        } else {
            sub = "Chargement\xE2\x80\xA6";
        }
        text(c, x, y, fit(c, sub, fonts.small, w), fonts.small, pal.muted);
        y += lineH(c, fonts.small) + 2;
        const std::string where = p ? p->shownPath : d.source.path;
        if (!where.empty() && where != name) {
            text(c, x, y, fit(c, where, fonts.small, w), fonts.small, pal.muted);
            y += lineH(c, fonts.small) + 2;
        }
        y += 4;
    }
    // ---- les boutons : Ouvrir avec le programme du systeme, Ouvrir le dossier
    if (p && (d.allowSystem || d.allowFolder)) {
        float bx = x;
        float bh = 0;
        const bool canOpen = p->kind != fv::Kind::Missing && p->kind != fv::Kind::Folder && (p->onDisk || p->bytes);
        if (d.allowSystem && canOpen) {
            const auto b = button(c, bx, y, "Ouvrir avec le programme du syst\xC3\xA8me", "open");
            bx += b.w + 6;
            bh = b.h;
        }
        const bool hasFolder = !d.source.openFolder.empty() || (!d.source.path.empty() && p->kind != fv::Kind::Missing);
        if (d.allowFolder && hasFolder) {
            const auto b = button(c, bx, y, p->kind == fv::Kind::Folder ? "Ouvrir ce dossier" : "Ouvrir le dossier", "folder");
            bh = b.h;
        }
        if (bh > 0) y += bh + 6;
    }
    if (!d.message.empty()) {
        text(c, x, y, fit(c, d.message, fonts.small, w), fonts.small, d.messageError ? pal.error : pal.accent);
        y += lineH(c, fonts.small) + 4;
    }
    if (!p) {
        text(c, x, y + 8, "Chargement\xE2\x80\xA6", fonts.ui, pal.muted);
        d.animating = true;
        r.popClip();
        return;
    }
    // ---- les faits ("Encodage : UTF-8"), en lignes ; les notes ; l'erreur
    {
        float fx = x;
        int rowsUsed = 0;
        const float fh = lineH(c, fonts.small) + 3;
        for (std::size_t i = 0; i < p->facts.size() && rowsUsed < 3; ++i) {
            const std::string one = p->facts[i].first + " : " + p->facts[i].second;
            const float fw = width(c, one, fonts.small);
            if (fx > x && fx + fw > x + w) {
                fx = x;
                y += fh;
                ++rowsUsed;
                if (rowsUsed >= 3) break;
            }
            text(c, fx, y, fit(c, one, fonts.small, x + w - fx), fonts.small, pal.text);
            fx += fw + 18.f;
        }
        if (!p->facts.empty()) y += fh + 2;
        for (std::size_t i = 0; i < p->notes.size() && i < 3; ++i) {
            text(c, x, y, fit(c, p->notes[i], fonts.small, w), fonts.small, pal.warning);
            y += fh;
        }
        if (!p->error.empty()) {
            for (const auto& l : wrapText(c, p->error, fonts.ui, w)) {
                text(c, x, y, l, fonts.ui, pal.error);
                y += lineH(c, fonts.ui) + 2;
            }
        }
        y += 4;
    }
    // ---- le contenu
    const gfx::Rect content{x, y, w, std::max(0.f, area.bottom() - pad - y)};
    if (content.h > 12) {
        switch (p->kind) {
            case fv::Kind::Image: drawImage(c, *p, content); break;
            case fv::Kind::Video: drawVideo(c, *p, content); break;
            case fv::Kind::Sound: drawSound(c, *p, content); break;
            case fv::Kind::Pdf: drawPdf(c, *p, content); break;
            case fv::Kind::Font: drawFont(c, *p, content); break;
            case fv::Kind::Csv:
            case fv::Kind::Workbook: drawGrid(c, *p, content); break;
            case fv::Kind::Zip: drawZip(c, *p, content); break;
            case fv::Kind::Docx: drawDocx(c, *p, content); break;
            case fv::Kind::Text:
            case fv::Kind::Json:
            case fv::Kind::Xml:
            case fv::Kind::ControlExpert:
            case fv::Kind::Binary: drawLines(c, *p, content); break;
            case fv::Kind::Folder:
            case fv::Kind::Missing: break;
        }
    }
    r.popClip();
}

bool FileViewer::event(const ui::InputEvent& ev, const gfx::Rect& area) {
    auto& d = *impl_;
    const fv::Preview* p = current();
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        if (!area.contains(m->pos)) return false;
        if (m->button != ui::MouseButton::Left) return true;
        for (auto it = d.hits.rbegin(); it != d.hits.rend(); ++it) {
            if (!it->r.contains(m->pos)) continue;
            const std::string& a = it->action;
            if (a == "open") (void)openWithSystem();
            else if (a == "folder") (void)openFolder();
            else if (a == "zoom+") zoomBy(1.25);
            else if (a == "zoom-") zoomBy(1 / 1.25);
            else if (a == "fit") zoomFit();
            else if (a == "actual") zoomActual();
            else if (a == "page-") (void)goPage(-1);
            else if (a == "page+") (void)goPage(1);
            else if (a == "play") (void)togglePlay();
            else if (a == "foldall" || a == "unfoldall") {
                if (p) {
                    d.folded.assign(p->lines.size(), false);
                    if (a == "foldall")
                        for (std::size_t i = 0; i < p->depth.size(); ++i) d.folded[i] = p->depth[i] >= 1 && fv::opensBlock(p->depth, i);
                    d.top = 0;
                }
            } else if (a.rfind("sheet:", 0) == 0) (void)setSheet(static_cast<std::size_t>(std::strtoul(a.c_str() + 6, nullptr, 10)));
            else if (a.rfind("fold:", 0) == 0) (void)toggleFold(static_cast<std::size_t>(std::strtoul(a.c_str() + 5, nullptr, 10)));
            return true;
        }
        // Un double-clic sur l'image : ajuste <-> 100 %.
        if (p && m->clickCount >= 2 && (p->kind == fv::Kind::Image || p->kind == fv::Kind::Pdf)) {
            if (d.fit) zoomActual();
            else zoomFit();
        }
        return true;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!area.contains(w->pos) || !p) return false;
        const bool zoomable = p->kind == fv::Kind::Image || p->kind == fv::Kind::Pdf;
        if (w->mods.ctrl && zoomable) {
            zoomBy(std::pow(1.15, std::clamp(static_cast<double>(w->dy), -4.0, 4.0)));
            return true;
        }
        const double dy = -std::clamp(static_cast<double>(w->dy), -6.0, 6.0);
        const double dx = -std::clamp(static_cast<double>(w->dx), -6.0, 6.0);
        if (zoomable || p->kind == fv::Kind::Video) {
            if (w->mods.shift) scrollBy(0, dy * 60.0);
            else scrollBy(dy * 3.0, dx * 60.0);
        } else if (p->kind == fv::Kind::Docx) {
            scrollBy(dy * 3.0);
        } else if (w->mods.shift || (w->dy == 0 && w->dx != 0)) {
            scrollBy(0, (w->dy != 0 ? dy : dx) * 60.0);
        } else {
            scrollBy(dy * 3.0);
        }
        return true;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (!p) return false;
        const double page = 20;
        switch (k->key) {
            case ui::Key::Down: scrollBy(1); return true;
            case ui::Key::Up: scrollBy(-1); return true;
            case ui::Key::PageDown:
                if (p->kind == fv::Kind::Pdf && !k->mods.ctrl && goPage(1)) return true;
                scrollBy(page);
                return true;
            case ui::Key::PageUp:
                if (p->kind == fv::Kind::Pdf && !k->mods.ctrl && goPage(-1)) return true;
                scrollBy(-page);
                return true;
            case ui::Key::Home: scrollBy(-1e9, -1e9); return true;
            case ui::Key::End: scrollBy(1e9); return true;
            case ui::Key::Left: scrollBy(0, -40); return true;
            case ui::Key::Right: scrollBy(0, 40); return true;
            case ui::Key::Space:
                if (p->kind == fv::Kind::Sound || p->kind == fv::Kind::Video) return togglePlay();
                return false;
            default: return false;
        }
    }
    return false;
}

// ==================================================================== le volet ==
FileViewPane::FileViewPane(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }

void FileViewPane::show(fileview::Source source) {
    viewer_.show(std::move(source));
    invalidate();
}

void FileViewPane::onPaint(const ui::PaintContext& ctx) {
    viewer_.paint(ctx.r, bounds(), FileViewer::paletteOf(ctx.theme), FileViewer::fontsOf(ctx.theme));
    if (focused()) ctx.r.strokeRect(bounds(), ctx.theme.brand.focusRing, 1.f);
    if (viewer_.animating()) invalidate();
}

ui::EventResult FileViewPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && bounds().contains(m->pos)) grabFocus();
    if (std::holds_alternative<ui::KeyDown>(ev) && !focused()) return ui::EventResult::Ignored;
    if (viewer_.event(ev, bounds())) {
        invalidate();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ============================================ l'Explorateur de fichiers de l'IHM ==
namespace {
struct Held {
    std::unique_ptr<FileViewer> viewer;
    gfx::Rect                   rect{};
    double                      used{0};
};
std::map<std::pair<const void*, std::uint32_t>, Held>& heldViewers() {
    static std::map<std::pair<const void*, std::uint32_t>, Held> m;
    return m;
}
} // namespace

FileViewer& explorerViewer(const void* runtime, std::uint32_t object) {
    auto& m = heldViewers();
    const double now = clockNow();
    for (auto it = m.begin(); it != m.end();) {
        if (now - it->second.used > 30.0 && it->first != std::make_pair(runtime, object)) it = m.erase(it);
        else ++it;
    }
    auto& h = m[{runtime, object}];
    if (!h.viewer) {
        h.viewer = std::make_unique<FileViewer>();
        h.viewer->setEmptyText("Aper\xC3\xA7u : choisis un fichier dans la liste.");
    }
    h.used = now;
    return *h.viewer;
}

FileViewer* findExplorerViewer(const void* runtime, std::uint32_t object) {
    auto& m = heldViewers();
    const auto it = m.find({runtime, object});
    return it == m.end() ? nullptr : it->second.viewer.get();
}

void setExplorerViewerRect(const void* runtime, std::uint32_t object, const gfx::Rect& screen) {
    auto& m = heldViewers();
    if (const auto it = m.find({runtime, object}); it != m.end()) it->second.rect = screen;
}

bool explorerViewerRect(const void* runtime, std::uint32_t object, gfx::Rect& out) {
    auto& m = heldViewers();
    const auto it = m.find({runtime, object});
    if (it == m.end() || it->second.rect.w <= 0) return false;
    out = it->second.rect;
    return true;
}

bool explorerViewersAnimating() {
    for (const auto& [k, h] : heldViewers())
        if (h.viewer && h.viewer->animating()) return true;
    return false;
}

std::string explorerSelectionHint(const hmi::Runtime& rt, std::uint32_t object) {
    const auto* fe = rt.fileExplorer(object);
    if (!fe) return {};
    return fe->selectedPath + "\n" + std::to_string(fe->selected >= 0) + "\n" + std::to_string(fe->loads);
}

fv::Source explorerSelection(const hmi::Runtime& rt, std::uint32_t object, const hmi::Project* project) {
    const auto* fe = rt.fileExplorer(object);
    if (!fe || fe->selected < 0 || static_cast<std::size_t>(fe->selected) >= fe->shown.size()) return {};
    const auto& e = fe->shown[static_cast<std::size_t>(fe->selected)];
    if (fe->resources) {
        // Les ressources du projet, en memoire (le projet n'est pas encore enregistre).
        const hmi::Resource* r = project ? project->resourceByName(e.name) : nullptr;
        if (!r || !r->data) return {};
        return fv::fromBytes(r->name, r->data, "Ressource du projet");
    }
    if (e.inZip) {
        if (e.folder) return {};
        fv::Source s;
        s.name = e.name;
        s.shownPath = e.path;
        s.modified = e.modified;
        const fs::path zip = pathOf(fe->loc.root);
        s.origin = "Dans " + textOf(zip.filename());
        s.openFolder = textOf(zip.parent_path());
        s.fetch = [loc = fe->loc, entry = e](hmi::Bytes& out, std::string& why) {
            std::vector<std::uint8_t> bytes;
            if (!hmi::explorer::readEntry(loc, entry, 128ull << 20, bytes, why)) return false;
            out = std::move(bytes);
            return true;
        };
        return s;
    }
    return fv::fromPath(e.path);   // un fichier ; un dossier : son resume
}

} // namespace app
