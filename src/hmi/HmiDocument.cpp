// =============================================================================
//  hmi/HmiDocument.cpp - 1.12.30 : les documents de la visionneuse.
//                        Voir HmiDocument.hpp.
// =============================================================================
#include "HmiDocument.hpp"

#include "HmiAssets.hpp"      // resolveExternalPath : un fichier externe du projet
#include "HmiPdf.hpp"
#include "../export/DocKit.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <tuple>
#include <utility>

namespace hmi::doc {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// Un chemin en UTF-8 <-> std::filesystem (C++20 : u8path est deprecie, u8string rend un std::u8string).
fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8Of(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::atomic<std::uint64_t> gSerial{0};
std::atomic<std::size_t> gBudget{160u * 1024u * 1024u};
constexpr std::size_t kThumbBudget = 32u * 1024u * 1024u;
constexpr double kMaxPixels = 16.0e6;     // une page rendue : 16 millions de pixels au plus
constexpr double kThumbWidth = 120.0;     // la largeur d'une vignette, en pixels
constexpr double kPdfToView = 96.0 / 72.0;

std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

std::string trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

// ---- le fil des documents : un seul, pour tout PDFium ---------------------------------------
//  Une file a priorites (0 : ouvrir, fermer ; 1 : une page visible ; 3 : une vignette ; 100 + p : chercher dans la
//  page p) ; a priorite egale, la plus recente d'abord (ce qu'on regarde maintenant). Jamais detruit (un singleton laisse
//  a la sortie) : a la sortie du programme, atexit attend la fin du travail en cours, puis le fil s'arrete.
struct Worker {
    std::mutex              m;
    std::condition_variable cv, idle;
    struct Job {
        int                   prio{0};
        std::uint64_t         seq{0};
        std::function<void()> run;
    };
    std::vector<Job> jobs;
    std::uint64_t    seq{0};
    bool             stop{false}, busy{false};

    Worker() {
        std::thread([this] { loop(); }).detach();
    }
    void post(int prio, std::function<void()> f) {
        {
            const std::lock_guard<std::mutex> lock(m);
            if (stop) return;
            jobs.push_back({prio, ++seq, std::move(f)});
        }
        cv.notify_one();
    }
    void loop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&] { return stop || !jobs.empty(); });
                if (stop) {
                    busy = false;
                    idle.notify_all();
                    return;
                }
                std::size_t best = 0;
                for (std::size_t i = 1; i < jobs.size(); ++i)
                    if (jobs[i].prio < jobs[best].prio || (jobs[i].prio == jobs[best].prio && jobs[i].seq > jobs[best].seq)) best = i;
                job = std::move(jobs[best]);
                jobs.erase(jobs.begin() + static_cast<std::ptrdiff_t>(best));
                busy = true;
            }
            try {
                job.run();
            } catch (...) {
                // un travail qui echoue n'arrete pas le fil (la page restera sans image)
            }
            {
                const std::lock_guard<std::mutex> lock(m);
                busy = false;
            }
            idle.notify_all();
            gSerial.fetch_add(1, std::memory_order_relaxed);
        }
    }
    void shutdown() {
        std::unique_lock<std::mutex> lock(m);
        stop = true;
        jobs.clear();
        cv.notify_all();
        idle.wait_for(lock, std::chrono::seconds(3), [&] { return !busy; });
    }
};

Worker& worker() {
    static Worker* w = [] {
        auto* created = new Worker();
        std::atexit([] { worker().shutdown(); });
        return created;
    }();
    return *w;
}

// ---- l'UTF-8 ----------------------------------------------------------------------------------
bool validUtf8(const Bytes& b) {
    std::size_t i = 0;
    while (i < b.size()) {
        const unsigned char c = b[i];
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || i + static_cast<std::size_t>(n) >= b.size() + (n == 0 ? 1u : 0u)) return false;
        for (int k = 1; k <= n; ++k)
            if ((b[i + static_cast<std::size_t>(k)] & 0xC0) != 0x80) return false;
        i += static_cast<std::size_t>(n) + 1;
    }
    return true;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::vector<char32_t> codePoints(std::string_view s) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        char32_t cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k <= n && i + static_cast<std::size_t>(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
        out.push_back(cp);
        i += static_cast<std::size_t>(n) + 1;
    }
    return out;
}

// Une lettre sans casse ni accent (la recherche dans un texte).
char32_t folded(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    switch (c) {
        case 0xE0: case 0xE1: case 0xE2: case 0xE4: case 0xC0: case 0xC1: case 0xC2: case 0xC4: return 'a';
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'e';
        case 0xEC: case 0xED: case 0xEE: case 0xEF: case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'i';
        case 0xF2: case 0xF3: case 0xF4: case 0xF6: case 0xD2: case 0xD3: case 0xD4: case 0xD6: return 'o';
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'u';
        case 0xE7: case 0xC7: return 'c';
        default: return c;
    }
}

std::u16string utf16(std::string_view s) {
    std::u16string out;
    for (const char32_t cp : codePoints(s)) {
        if (cp < 0x10000) out += static_cast<char16_t>(cp);
        else {
            const char32_t v = cp - 0x10000;
            out += static_cast<char16_t>(0xD800 + (v >> 10));
            out += static_cast<char16_t>(0xDC00 + (v & 0x3FF));
        }
    }
    return out;
}

bool readFile(const std::string& path, Bytes& out, std::string& why) {
    std::error_code ec;
    const fs::path p = pathOf(path);
    const auto size = fs::file_size(p, ec);
    if (ec) { why = "fichier introuvable : " + path; return false; }
    if (size > 512ull * 1024 * 1024) { why = "fichier trop lourd (plus de 512 Mo) : " + path; return false; }
    std::ifstream in(p, std::ios::binary);
    if (!in) { why = "lecture impossible : " + path; return false; }
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    if (!in && size > 0) { why = "lecture interrompue : " + path; return false; }
    return true;
}

int scaleKey(double scale) {
    const double s = std::max(1e-3, scale);
    return static_cast<int>(std::ceil(std::log2(s) * 6.0 - 1e-6));
}

} // namespace

// ================================================================== les genres ===
Type typeOfFormat(std::string_view f) noexcept {
    std::string u;
    for (const char c : f)
        if (c != '.') u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u == "PDF") return Type::Pdf;
    if (u == "PNG" || u == "JPEG" || u == "JPG" || u == "BMP" || u == "GIF") return Type::Image;
    if (u == "SVG") return Type::Svg;
    if (u == "TXT" || u == "TEXT" || u == "LOG" || u == "MD" || u == "INI" || u == "CSV") return Type::Text;
    return Type::None;
}

Type typeOfName(std::string_view name) noexcept {
    const auto dot = name.find_last_of('.');
    const auto slash = name.find_last_of("/\\");
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return Type::None;
    return typeOfFormat(name.substr(dot + 1));
}

std::string_view typeLabel(Type t) noexcept {
    switch (t) {
        case Type::Pdf: return "PDF";
        case Type::Image: case Type::Svg: return "image";
        case Type::Text: return "texte";
        case Type::None: break;
    }
    return "inconnu";
}

std::vector<std::vector<std::string>> paginateText(const Bytes& data) {
    std::string text;
    if (validUtf8(data)) {
        text.assign(data.begin(), data.end());
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF)
            text.erase(0, 3);   // la marque d'ordre des octets
    } else {
        for (const auto b : data) appendUtf8(text, b < 0x80 ? b : static_cast<char32_t>(b));   // Windows-1252 ~ Latin-1
    }
    std::vector<std::string> lines;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto nl = text.find('\n', from);
        std::string raw = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        // Les tabulations : 4 espaces ; les lignes longues : coupees aux mots, a kTextChars caracteres.
        std::string line;
        for (const char c : raw) {
            if (c == '\t') line += "    ";
            else line += c;
        }
        auto cps = codePoints(line);
        while (static_cast<int>(cps.size()) > kTextChars) {
            int cut = kTextChars;
            for (int k = kTextChars; k > kTextChars / 2; --k)
                if (cps[static_cast<std::size_t>(k)] == ' ') { cut = k; break; }
            std::string head;
            for (int k = 0; k < cut; ++k) appendUtf8(head, cps[static_cast<std::size_t>(k)]);
            lines.push_back(head);
            std::size_t rest = static_cast<std::size_t>(cut);
            while (rest < cps.size() && cps[rest] == ' ') ++rest;
            cps.erase(cps.begin(), cps.begin() + static_cast<std::ptrdiff_t>(rest));
        }
        std::string tail;
        for (const auto cp : cps) appendUtf8(tail, cp);
        lines.push_back(tail);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    while (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    std::vector<std::vector<std::string>> pages;
    for (std::size_t i = 0; i < lines.size(); i += static_cast<std::size_t>(kTextLines))
        pages.emplace_back(lines.begin() + static_cast<std::ptrdiff_t>(i),
                           lines.begin() + static_cast<std::ptrdiff_t>(std::min(lines.size(), i + static_cast<std::size_t>(kTextLines))));
    if (pages.empty()) pages.emplace_back();
    return pages;
}

// ================================================================= le document ===
struct Document::Impl {
    mutable std::mutex m;
    Type               type{Type::None};
    std::string        key, name, path, format;
    BlobPtr            bytes;                 // le contenu (garde tant que PDFium s'en sert)
    State              state{State::Loading};
    std::string        error;
    bool               needsEngine{false};
    pdf::DocHandle     handle{nullptr};       // le fil des documents seulement
    std::vector<Size>  sizes;                 // a 100 %
    std::vector<std::vector<std::string>> text;
    std::shared_ptr<const Rgba> image;        // une image (PNG, JPEG...) decodee
    struct Entry {
        std::shared_ptr<const Rgba> image;
        std::uint64_t               used{0};
    };
    std::map<std::pair<int, int>, Entry> pages;   // (page, palier d'echelle)
    std::map<int, Entry>                 thumbs;  // page
    std::set<std::pair<int, int>>        pending;
    std::set<int>                        pendingThumbs;
    std::map<std::pair<int, int>, Clock::time_point> wantedAt;
    std::size_t   bytesUsed{0}, thumbBytes{0}, renders{0};
    std::uint64_t tick{0};
    Search        search;
    std::uint64_t searchGeneration{0};
};

Document::Document(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {
    type_ = impl_->type;
    name_ = impl_->name;
    key_ = impl_->key;
}

Document::~Document() {
    // PDFium se ferme sur son fil ; le travail garde le contenu (et l'etat) jusque-la.
    std::shared_ptr<Impl> keep = std::move(impl_);
    if (!keep) return;
    worker().post(0, [keep] {
        const std::lock_guard<std::mutex> lock(keep->m);
        if (keep->handle) {
            pdf::close(keep->handle);
            keep->handle = nullptr;
        }
    });
}

Document::State Document::state() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->state;
}

std::string Document::error() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->error;
}

bool Document::needsEngine() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->needsEngine;
}

int Document::pageCount() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return static_cast<int>(impl_->sizes.size());
}

Size Document::pageSize(int index) const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    if (index < 0 || static_cast<std::size_t>(index) >= impl_->sizes.size()) return {};
    return impl_->sizes[static_cast<std::size_t>(index)];
}

std::vector<Size> Document::pageSizes() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->sizes;
}

std::vector<std::string> Document::textLines(int index) const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    if (index < 0 || static_cast<std::size_t>(index) >= impl_->text.size()) return {};
    return impl_->text[static_cast<std::size_t>(index)];
}

double Document::renderScale(double scale) noexcept { return std::exp2(scaleKey(scale) / 6.0); }

namespace {

// Les octets en trop : les images les moins recemment demandees partent (pas celle qu'on vient de poser).
void evict(Document::Impl& d, std::pair<int, int> keep) {
    const std::size_t budget = gBudget.load();
    while (d.bytesUsed > budget && d.pages.size() > 1) {
        auto oldest = d.pages.end();
        for (auto it = d.pages.begin(); it != d.pages.end(); ++it)
            if (it->first != keep && (oldest == d.pages.end() || it->second.used < oldest->second.used)) oldest = it;
        if (oldest == d.pages.end()) break;
        d.bytesUsed -= oldest->second.image ? oldest->second.image->pixels.size() : 0;
        d.pages.erase(oldest);
    }
    while (d.thumbBytes > kThumbBudget && d.thumbs.size() > 1) {
        auto oldest = d.thumbs.begin();
        for (auto it = d.thumbs.begin(); it != d.thumbs.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        d.thumbBytes -= oldest->second.image ? oldest->second.image->pixels.size() : 0;
        d.thumbs.erase(oldest);
    }
}

// Rendre la page `index` a `scale` (pixels par pixel a 100 %) : un PDF par PDFium, un SVG par nanosvg. Sur le fil.
bool renderOn(Document::Impl& d, int index, double scale, Rgba& out) {
    Size s;
    Type type;
    pdf::DocHandle handle = nullptr;
    BlobPtr bytes;
    {
        const std::lock_guard<std::mutex> lock(d.m);
        if (index < 0 || static_cast<std::size_t>(index) >= d.sizes.size()) return false;
        s = d.sizes[static_cast<std::size_t>(index)];
        type = d.type;
        handle = d.handle;
        bytes = d.bytes;
    }
    double k = scale;
    if (s.w * s.h * k * k > kMaxPixels) k = std::sqrt(kMaxPixels / std::max(1.0, s.w * s.h));
    const int w = std::max(1, static_cast<int>(std::lround(s.w * k))), h = std::max(1, static_cast<int>(std::lround(s.h * k)));
    if (type == Type::Pdf) return handle && pdf::render(handle, index, w, h, 0, out);
    if (type == Type::Svg) return bytes && rasterizeSvg(*bytes, w, h, out);
    return false;
}

} // namespace

Document::Picture Document::page(int index, double scale, bool wanted) {
    Picture pic;
    auto& d = *impl_;
    std::unique_lock<std::mutex> lock(d.m);
    if (d.state != State::Ready || index < 0 || static_cast<std::size_t>(index) >= d.sizes.size()) return pic;
    if (d.type == Type::Image) {
        pic.image = d.image;
        pic.exact = true;
        return pic;
    }
    if (d.type != Type::Pdf && d.type != Type::Svg) return pic;
    const int key = scaleKey(scale);
    const auto exact = d.pages.find({index, key});
    if (exact != d.pages.end()) {
        exact->second.used = ++d.tick;
        pic.image = exact->second.image;
        pic.exact = true;
        return pic;
    }
    // En attendant : la plus proche deja rendue.
    int bestGap = 1 << 20;
    for (auto it = d.pages.lower_bound({index, -(1 << 20)}); it != d.pages.end() && it->first.first == index; ++it)
        if (const int gap = std::abs(it->first.second - key); gap < bestGap) {
            bestGap = gap;
            pic.image = it->second.image;
        }
    if (!pic.image)
        if (const auto t = d.thumbs.find(index); t != d.thumbs.end()) pic.image = t->second.image;
    if (!wanted) return pic;
    d.wantedAt[{index, key}] = Clock::now();
    if (!d.pending.insert({index, key}).second) return pic;
    const std::weak_ptr<Impl> weak = impl_;
    lock.unlock();
    worker().post(1, [weak, index, key] {
        const auto self = weak.lock();
        if (!self) return;
        {
            const std::lock_guard<std::mutex> guard(self->m);
            // Perimee : plus demandee depuis 1,5 s (on a zoome plus loin, ou defile ailleurs).
            const auto at = self->wantedAt.find({index, key});
            if (at == self->wantedAt.end() || Clock::now() - at->second > std::chrono::milliseconds(1500)) {
                self->pending.erase({index, key});
                return;
            }
        }
        Rgba out;
        const bool ok = renderOn(*self, index, std::exp2(key / 6.0), out);
        const std::lock_guard<std::mutex> guard(self->m);
        self->pending.erase({index, key});
        if (!ok) return;
        auto img = std::make_shared<const Rgba>(std::move(out));
        self->bytesUsed += img->pixels.size();
        self->pages[{index, key}] = Impl::Entry{std::move(img), ++self->tick};
        ++self->renders;
        evict(*self, {index, key});
    });
    return pic;
}

Document::Picture Document::thumbnail(int index) {
    Picture pic;
    auto& d = *impl_;
    std::unique_lock<std::mutex> lock(d.m);
    if (d.state != State::Ready || index < 0 || static_cast<std::size_t>(index) >= d.sizes.size()) return pic;
    if (d.type == Type::Image) {
        pic.image = d.image;
        pic.exact = true;
        return pic;
    }
    if (d.type != Type::Pdf && d.type != Type::Svg) return pic;
    if (const auto t = d.thumbs.find(index); t != d.thumbs.end()) {
        t->second.used = ++d.tick;
        pic.image = t->second.image;
        pic.exact = true;
        return pic;
    }
    if (!d.pendingThumbs.insert(index).second) return pic;
    const double scale = kThumbWidth / std::max(1.0, d.sizes[static_cast<std::size_t>(index)].w);
    const std::weak_ptr<Impl> weak = impl_;
    lock.unlock();
    worker().post(3, [weak, index, scale] {
        const auto self = weak.lock();
        if (!self) return;
        Rgba out;
        const bool ok = renderOn(*self, index, scale, out);
        const std::lock_guard<std::mutex> guard(self->m);
        self->pendingThumbs.erase(index);
        if (!ok) return;
        auto img = std::make_shared<const Rgba>(std::move(out));
        self->thumbBytes += img->pixels.size();
        self->thumbs[index] = Impl::Entry{std::move(img), ++self->tick};
        evict(*self, {-1, 0});
    });
    return pic;
}

namespace {
template <class F>
bool waitFor(int ms, F done) {
    const auto end = Clock::now() + std::chrono::milliseconds(std::max(0, ms));
    for (;;) {
        if (done()) return true;
        if (Clock::now() >= end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
} // namespace

bool Document::waitReady(int ms) const {
    return waitFor(ms, [&] { return state() != State::Loading; }) && state() == State::Ready;
}

bool Document::waitPage(int index, double scale, int ms) {
    if (!waitReady(ms)) return false;
    if (type_ == Type::Text) return true;
    return waitFor(ms, [&] { return page(index, scale).exact; });
}

bool Document::waitThumbnail(int index, int ms) {
    if (!waitReady(ms)) return false;
    if (type_ == Type::Text) return true;
    return waitFor(ms, [&] { return thumbnail(index).exact; });
}

std::size_t Document::cachedPages() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->pages.size();
}

std::size_t Document::cachedBytes() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->bytesUsed;
}

std::size_t Document::renderCount() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->renders;
}

// ---- la recherche ----------------------------------------------------------------------------
void Document::search(const std::string& queryIn) {
    const std::string query = trimmed(queryIn);
    auto& d = *impl_;
    std::unique_lock<std::mutex> lock(d.m);
    if (query == d.search.query && (d.search.running || !d.search.hits.empty() || query.empty())) return;
    ++d.searchGeneration;
    d.search = Search{};
    d.search.query = query;
    if (query.empty() || d.state != State::Ready) return;
    if (d.type == Type::Text) {
        // Un texte : tout de suite, sans casse ni accent.
        std::vector<char32_t> q;
        for (const auto cp : codePoints(query)) q.push_back(folded(cp));
        for (std::size_t p = 0; p < d.text.size(); ++p)
            for (std::size_t l = 0; l < d.text[p].size(); ++l) {
                std::vector<char32_t> line;
                for (const auto cp : codePoints(d.text[p][l])) line.push_back(folded(cp));
                for (std::size_t at = 0; q.size() <= line.size() && at + q.size() <= line.size(); ++at)
                    if (std::equal(q.begin(), q.end(), line.begin() + static_cast<std::ptrdiff_t>(at))) {
                        Hit h;
                        h.page = static_cast<int>(p);
                        h.line = static_cast<int>(l);
                        h.col = static_cast<int>(at);
                        h.len = static_cast<int>(q.size());
                        const double cw = kTextFont * 0.55;
                        h.rects.push_back({kTextMargin + cw * h.col, kTextMargin + kTextLine * static_cast<double>(l), cw * h.len, kTextLine});
                        d.search.hits.push_back(std::move(h));
                        at += q.size() - 1;
                    }
            }
        return;
    }
    if (d.type != Type::Pdf || !d.handle) return;
    d.search.running = true;
    const std::uint64_t generation = d.searchGeneration;
    const int pages = static_cast<int>(d.sizes.size());
    const std::weak_ptr<Impl> weak = impl_;
    lock.unlock();
    // Page par page, chacune son travail, dans l'ordre des pages (la priorite 100 + page) : une page a rendre (1) ou
    // une vignette (3) passe entre deux. Le dernier travail (100 + pages) dit la recherche finie.
    const auto q16 = std::make_shared<const std::u16string>(utf16(query));
    for (int p = 0; p < pages; ++p)
        worker().post(100 + p, [weak, generation, p, q16] {
            const auto self = weak.lock();
            if (!self) return;
            pdf::DocHandle h = nullptr;
            {
                const std::lock_guard<std::mutex> guard(self->m);
                if (self->searchGeneration != generation) return;
                h = self->handle;
            }
            const auto found = pdf::search(h, p, *q16);
            const std::lock_guard<std::mutex> guard(self->m);
            if (self->searchGeneration != generation) return;
            for (const auto& f : found) {
                Hit hit;
                hit.page = p;
                for (const auto& r : f.rects)
                    hit.rects.push_back({r[0] * kPdfToView, r[1] * kPdfToView, (r[2] - r[0]) * kPdfToView, (r[3] - r[1]) * kPdfToView});
                self->search.hits.push_back(std::move(hit));
            }
        });
    worker().post(100 + pages, [weak, generation] {
        const auto self = weak.lock();
        if (!self) return;
        const std::lock_guard<std::mutex> guard(self->m);
        if (self->searchGeneration == generation) self->search.running = false;
    });
}

Document::Search Document::searchState() const {
    const std::lock_guard<std::mutex> lock(impl_->m);
    return impl_->search;
}

bool Document::waitSearch(int ms) const {
    return waitFor(ms, [&] { return !searchState().running; });
}

// ================================================================== ouvrir ===
namespace {

std::mutex& registryLock() {
    static auto* m = new std::mutex();
    return *m;
}
std::map<std::string, std::weak_ptr<Document>>& registry() {
    static auto* r = new std::map<std::string, std::weak_ptr<Document>>();
    return *r;
}

// Sur le fil : lire le contenu (un fichier), le decoder, compter les pages et leurs tailles.
void load(Document::Impl& d) {
    BlobPtr bytes;
    std::string path, format;
    Type type;
    {
        const std::lock_guard<std::mutex> lock(d.m);
        bytes = d.bytes;
        path = d.path;
        format = d.format;
        type = d.type;
    }
    std::string why;
    if (!bytes) {
        auto owned = std::make_shared<Bytes>();
        if (!readFile(path, *owned, why)) {
            const std::lock_guard<std::mutex> lock(d.m);
            d.state = Document::State::Failed;
            d.error = why;
            return;
        }
        bytes = std::move(owned);
    }
    std::vector<Size> sizes;
    std::vector<std::vector<std::string>> text;
    std::shared_ptr<const Rgba> image;
    pdf::DocHandle handle = nullptr;
    bool engine = false;
    switch (type) {
        case Type::Pdf: {
            if (!pdf::library().ok) {
                engine = true;
                why = pdf::missingAdvice();
                break;
            }
            handle = pdf::open(bytes->data(), bytes->size(), &why);
            if (!handle) break;
            const int n = pdf::pageCount(handle);
            for (int i = 0; i < n; ++i) {
                double w = 0, h = 0;
                if (!pdf::pageSize(handle, i, w, h)) { w = 595; h = 842; }
                sizes.push_back({w * kPdfToView, h * kPdfToView});
            }
            if (sizes.empty()) why = "PDF sans page";
            break;
        }
        case Type::Image: {
            // Une image enorme (une photo de 100 millions de pixels) ne se decode pas : sa taille d'abord.
            const MediaInfo mi = inspectMedia(*bytes, "image." + format);
            if (static_cast<double>(mi.width) * static_cast<double>(mi.height) > 100.0e6) {
                why = "image trop grande (" + std::to_string(mi.width) + " x " + std::to_string(mi.height) + " pixels ; 100 millions au plus)";
                break;
            }
            Rgba rgba;
            std::string err;
            if (!decodeImage(*bytes, format, rgba, 8192, &err) || rgba.width <= 0) {
                why = "image illisible" + (err.empty() ? std::string{} : " (" + err + ")");
                break;
            }
            sizes.push_back({static_cast<double>(rgba.width), static_cast<double>(rgba.height)});
            // Au-dela de 16 millions de pixels, l'ecran la recoit reduite (une moyenne par carre de k x k).
            const double px = static_cast<double>(rgba.width) * rgba.height;
            if (px > kMaxPixels) {
                const int k = static_cast<int>(std::ceil(std::sqrt(px / kMaxPixels)));
                Rgba small;
                small.width = std::max(1, rgba.width / k);
                small.height = std::max(1, rgba.height / k);
                small.pixels.assign(static_cast<std::size_t>(small.width) * static_cast<std::size_t>(small.height) * 4u, 0);
                for (int y = 0; y < small.height; ++y)
                    for (int x = 0; x < small.width; ++x) {
                        unsigned sum[4] = {0, 0, 0, 0};
                        for (int dy = 0; dy < k; ++dy)
                            for (int dx = 0; dx < k; ++dx) {
                                const std::size_t at = (static_cast<std::size_t>(y * k + dy) * static_cast<std::size_t>(rgba.width)
                                                        + static_cast<std::size_t>(x * k + dx)) * 4u;
                                for (int c = 0; c < 4; ++c) sum[c] += rgba.pixels[at + static_cast<std::size_t>(c)];
                            }
                        const std::size_t to = (static_cast<std::size_t>(y) * static_cast<std::size_t>(small.width) + static_cast<std::size_t>(x)) * 4u;
                        for (int c = 0; c < 4; ++c) small.pixels[to + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(sum[c] / static_cast<unsigned>(k * k));
                    }
                rgba = std::move(small);
            }
            image = std::make_shared<const Rgba>(std::move(rgba));
            break;
        }
        case Type::Svg: {
            const MediaInfo mi = inspectMedia(*bytes, "image.svg");
            double w = mi.width > 0 ? mi.width : 800, h = mi.height > 0 ? mi.height : 600;
            Rgba probe;
            if (!rasterizeSvg(*bytes, 8, 8, probe)) {
                why = "SVG illisible";
                break;
            }
            sizes.push_back({w, h});
            break;
        }
        case Type::Text:
            text = paginateText(*bytes);
            for (std::size_t i = 0; i < text.size(); ++i) sizes.push_back({kTextPageW, kTextPageH});
            break;
        case Type::None:
            why = "format inconnu : la visionneuse lit les PDF, les images (PNG, JPEG, BMP, GIF, SVG) et le texte (TXT)";
            break;
    }
    const std::lock_guard<std::mutex> lock(d.m);
    d.bytes = bytes;
    d.handle = handle;
    d.sizes = std::move(sizes);
    d.text = std::move(text);
    d.image = std::move(image);
    d.needsEngine = engine;
    if (d.sizes.empty()) {
        d.state = Document::State::Failed;
        d.error = why.empty() ? std::string("document illisible") : why;
    } else {
        d.state = Document::State::Ready;
    }
}

std::shared_ptr<Document> make(const std::string& key, Type type, std::string name, std::string path, BlobPtr bytes, std::string format) {
    const std::lock_guard<std::mutex> lock(registryLock());
    auto& reg = registry();
    if (auto it = reg.find(key); it != reg.end())
        if (auto alive = it->second.lock()) return alive;
    // Les entrees mortes partent (les documents fermes).
    for (auto it = reg.begin(); it != reg.end();) it = it->second.expired() ? reg.erase(it) : std::next(it);
    auto impl = std::make_shared<Document::Impl>();
    impl->key = key;
    impl->type = type;
    impl->name = std::move(name);
    impl->path = std::move(path);
    impl->bytes = std::move(bytes);
    impl->format = std::move(format);
    auto docPtr = std::make_shared<Document>(impl);
    reg[key] = docPtr;
    const std::weak_ptr<Document::Impl> weak = impl;
    worker().post(0, [weak] {
        if (const auto self = weak.lock()) load(*self);
    });
    return docPtr;
}

} // namespace

Resolved resolve(const Assets& assets, std::string_view specIn, const std::string& folder) {
    Resolved r;
    const std::string spec = trimmed(specIn);
    if (spec.empty()) {
        r.error = "aucun document";
        return r;
    }
    const Resource* res = nullptr;
    for (const auto& candidate : assets.resources)
        if (candidate.name == spec) res = &candidate;
    // Un fichier externe du projet (Ressources > Fichiers externes) : son chemin, relatif au dossier du projet.
    if (!res)
        for (const auto& f : assets.files)
            if (f.name == spec) {
                const std::string at = resolveExternalPath(f, folder);
                if (!at.empty()) return resolve(Assets{}, at, folder);
            }
    if (res) {
        r.resource = res;
        r.shown = res->name;
        r.type = typeOfFormat(res->format);
        if (r.type == Type::None) r.type = typeOfName(res->name);
        if (!res->data) r.error = "le fichier de la ressource " + res->name + " manque";
        else if (r.type == Type::None) r.error = res->name + " (" + res->format + ") : la visionneuse lit les PDF, les images (PNG, JPEG, BMP, GIF, SVG) et le texte (TXT)";
        return r;
    }
    fs::path path = pathOf(spec);
    std::error_code ec;
    if (path.is_relative()) {
        if (folder.empty()) {
            r.shown = utf8Of(pathOf(spec).filename());
            r.type = typeOfName(spec);
            r.error = "document introuvable : " + spec + " (ni une ressource du projet ; un chemin relatif part du dossier du projet, "
                      "qui n'est pas encore enregistr\xC3\xA9)";
            return r;
        }
        path = pathOf(folder) / path;
    }
    r.path = utf8Of(path.lexically_normal());
    r.shown = utf8Of(pathOf(spec).filename());
    r.type = typeOfName(spec);
    if (!fs::is_regular_file(path, ec)) {
        r.error = "document introuvable : " + spec + " (ni une ressource du projet, ni un fichier)";
        return r;
    }
    if (r.type == Type::None) r.error = r.shown + " : la visionneuse lit les PDF, les images (PNG, JPEG, BMP, GIF, SVG) et le texte (TXT)";
    return r;
}

std::shared_ptr<Document> open(const Assets& assets, std::string_view spec, const std::string& folder, std::string* why) {
    const Resolved r = resolve(assets, spec, folder);
    if (!r.error.empty()) {
        if (why) *why = r.error;
        return nullptr;
    }
    if (r.resource) {
        char ptr[32];
        std::snprintf(ptr, sizeof ptr, "%p", static_cast<const void*>(r.resource->data.get()));
        return make("ressource:" + r.resource->name + "|" + ptr, r.type, r.resource->name, {}, r.resource->data, r.resource->format);
    }
    std::error_code ec;
    const fs::path path = pathOf(r.path);
    const auto size = fs::file_size(path, ec);
    const auto when = fs::last_write_time(path, ec);
    const auto stamp = static_cast<long long>(when.time_since_epoch().count());
    std::string ext;
    if (const auto dot = r.path.find_last_of('.'); dot != std::string::npos) ext = upper(r.path.substr(dot + 1));
    return make("fichier:" + r.path + "|" + std::to_string(static_cast<unsigned long long>(size)) + "|" + std::to_string(stamp), r.type, r.shown,
                r.path, nullptr, ext == "JPG" ? std::string("JPEG") : ext);
}

std::shared_ptr<Document> openBytes(BlobPtr data, Type type, std::string name, std::string key) {
    return make("octets:" + key, type, std::move(name), {}, std::move(data), type == Type::Pdf ? "PDF" : type == Type::Svg ? "SVG" : "PNG");
}

void setCacheBudget(std::size_t bytes) { gBudget = std::max<std::size_t>(bytes, 1u << 20); }

std::uint64_t serial() noexcept { return gSerial.load(std::memory_order_relaxed); }

// ============================================================ la notice d'exemple ===
namespace {

namespace dk = exporter::doc;

// Une page de PDF : son flux (le texte en Windows-1252, les traces).
struct PdfPage {
    std::string ops;
    void color(double r, double g, double b, bool stroke = false) {
        ops += dk::pdfNumber(r) + " " + dk::pdfNumber(g) + " " + dk::pdfNumber(b) + (stroke ? " RG\n" : " rg\n");
    }
    void rect(double x, double y, double w, double h, bool fillIt = true) {
        ops += dk::pdfNumber(x) + " " + dk::pdfNumber(842 - y - h) + " " + dk::pdfNumber(w) + " " + dk::pdfNumber(h) + (fillIt ? " re f\n" : " re S\n");
    }
    void width(double w) { ops += dk::pdfNumber(w) + " w\n"; }
    void line(double x0, double y0, double x1, double y1) {
        ops += dk::pdfNumber(x0) + " " + dk::pdfNumber(842 - y0) + " m " + dk::pdfNumber(x1) + " " + dk::pdfNumber(842 - y1) + " l S\n";
    }
    // Un cercle (quatre arcs de Bezier) ; plein ou trace.
    void circle(double cx, double cy, double r, bool fillIt) {
        const double k = 0.5523 * r, y = 842 - cy;
        const auto n = [](double v) { return dk::pdfNumber(v); };
        ops += n(cx + r) + " " + n(y) + " m\n";
        ops += n(cx + r) + " " + n(y + k) + " " + n(cx + k) + " " + n(y + r) + " " + n(cx) + " " + n(y + r) + " c\n";
        ops += n(cx - k) + " " + n(y + r) + " " + n(cx - r) + " " + n(y + k) + " " + n(cx - r) + " " + n(y) + " c\n";
        ops += n(cx - r) + " " + n(y - k) + " " + n(cx - k) + " " + n(y - r) + " " + n(cx) + " " + n(y - r) + " c\n";
        ops += n(cx + k) + " " + n(y - r) + " " + n(cx + r) + " " + n(y - k) + " " + n(cx + r) + " " + n(y) + " c\n";
        ops += fillIt ? "f\n" : "S\n";
    }
    // Une onde (une periode et demie), centree en (cx, cy).
    void wave(double cx, double cy, double w, double a) {
        const int n = 18;
        for (int i = 0; i <= n; ++i) {
            const double u = static_cast<double>(i) / n;
            const double x = cx - w / 2 + w * u, y = cy - a * std::sin(u * 3.0 * 3.14159265358979);
            ops += dk::pdfNumber(x) + " " + dk::pdfNumber(842 - y) + (i == 0 ? " m\n" : " l\n");
        }
        ops += "S\n";
    }
    void text(double x, double y, double size, std::string_view utf8, bool bold = false) {
        ops += "BT /" + std::string(bold ? "F2 " : "F1 ") + dk::pdfNumber(size) + " Tf " + dk::pdfNumber(x) + " " + dk::pdfNumber(842 - y) + " Td "
             + dk::pdfLiteral(dk::toWinAnsi(utf8)) + " Tj ET\n";
    }
    void footer(int page, int pages) {
        color(0.45, 0.48, 0.53);
        text(40, 815, 9, "VE-101 \xC2\xB7 notice de conduite \xC2\xB7 page " + std::to_string(page) + " / " + std::to_string(pages));
        color(0.75, 0.77, 0.8, true);
        width(0.6);
        line(40, 800, 555, 800);
    }
};

Bytes writePdf(const std::vector<PdfPage>& pages) {
    std::string out = "%PDF-1.4\n";
    // Le commentaire binaire (des octets au-dela de 127 : les outils de transfert gardent le fichier tel quel).
    for (const int b : {0x25, 0xE2, 0xE3, 0xCF, 0xD3, 0x0A}) out += static_cast<char>(b);
    std::vector<std::size_t> offsets;
    const auto object = [&](const std::string& body) {
        offsets.push_back(out.size());
        out += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    const std::size_t n = pages.size();
    // 1 le catalogue, 2 l'arbre des pages, 3 et 4 les polices, puis pour chaque page : la page et son flux.
    object("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (std::size_t i = 0; i < n; ++i) kids += std::to_string(5 + 2 * i) + " 0 R ";
    object("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(n) + " >>");
    object("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    object("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    for (std::size_t i = 0; i < n; ++i) {
        object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents "
               + std::to_string(6 + 2 * i) + " 0 R >>");
        object("<< /Length " + std::to_string(pages[i].ops.size()) + " >>\nstream\n" + pages[i].ops + "endstream");
    }
    const std::size_t xref = out.size();
    char line[32];
    out += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto off : offsets) {
        std::snprintf(line, sizeof line, "%010zu 00000 n \n", off);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return Bytes(out.begin(), out.end());
}

} // namespace

Bytes sampleNotice() {
    std::vector<PdfPage> pages(4);
    // ---- 1 : le sommaire
    {
        auto& pg = pages[0];
        pg.color(0.184, 0.435, 0.839);
        pg.rect(0, 0, 595, 70);
        pg.color(1, 1, 1);
        pg.text(40, 44, 24, "Notice de conduite", true);
        pg.color(0.898, 0.325, 0.294);
        pg.rect(500, 90, 40, 40);
        pg.color(0.12, 0.14, 0.18);
        pg.text(40, 112, 15, "Ventilateur d'extraction VE-101 et son variateur VAR-101", true);
        pg.color(0.3, 0.33, 0.38);
        pg.text(40, 136, 11, "Atelier de peinture \xC2\xB7 armoire CVC-2 \xC2\xB7 r\xC3\xA9vision C");
        pg.color(0.12, 0.14, 0.18);
        pg.text(40, 200, 16, "Sommaire", true);
        const char* entries[] = {"1. Le sch\xC3\xA9ma \xC3\xA9lectrique et a\xC3\xA9raulique", "2. Mettre en route le ventilateur",
                                 "3. D\xC3\xA9" "fauts du variateur et rem\xC3\xA8" "des"};
        for (int i = 0; i < 3; ++i) {
            pg.text(56, 232 + 24.0 * i, 12, entries[i]);
            pg.text(480, 232 + 24.0 * i, 12, "page " + std::to_string(i + 2));
        }
        pg.color(0.93, 0.95, 0.98);
        pg.rect(40, 330, 515, 92);
        pg.color(0.184, 0.435, 0.839, true);
        pg.width(1);
        pg.rect(40, 330, 515, 92, false);
        pg.color(0.12, 0.14, 0.18);
        pg.text(56, 356, 12, "\xC3\x80 lire avant toute intervention", true);
        pg.text(56, 378, 11, "Consigner le sectionneur Q1 avant d'ouvrir l'armoire ou le caisson du ventilateur.");
        pg.text(56, 396, 11, "Le variateur garde une tension dangereuse 5 minutes apr\xC3\xA8s la coupure.");
        pg.footer(1, 4);
    }
    // ---- 2 : le schema
    {
        auto& pg = pages[1];
        pg.color(0.12, 0.14, 0.18);
        pg.text(40, 60, 20, "1. Le sch\xC3\xA9ma", true);
        pg.text(40, 90, 11, "Le r\xC3\xA9seau 400 V alimente le variateur VAR-101 par le disjoncteur Q1 ; le variateur");
        pg.text(40, 106, 11, "fait tourner le moteur M-101 du ventilateur VE-101, qui extrait l'air de la cabine.");
        pg.color(0.2, 0.22, 0.26, true);
        pg.width(2);
        pg.line(50, 240, 110, 240);                      // le reseau
        pg.color(0.12, 0.14, 0.18);
        pg.text(50, 230, 9, "400 V");
        pg.color(0.2, 0.22, 0.26, true);
        pg.rect(110, 225, 30, 30, false);               // Q1
        pg.line(140, 240, 190, 240);
        pg.color(0.184, 0.435, 0.839, true);
        pg.width(2.5);
        pg.rect(190, 195, 90, 90, false);               // le variateur : son cadre, sa diagonale, ~ et les ondes
        pg.line(190, 285, 280, 195);
        pg.width(1.6);
        pg.wave(214, 219, 26, 4);
        pg.wave(256, 256, 26, 4);
        pg.wave(256, 266, 26, 4);
        pg.color(0.2, 0.22, 0.26, true);
        pg.width(2);
        pg.line(280, 240, 330, 240);
        pg.circle(360, 240, 30, false);                 // le moteur
        pg.color(0.12, 0.14, 0.18);
        pg.text(352, 246, 16, "M", true);
        pg.color(0.2, 0.22, 0.26, true);
        pg.line(390, 240, 420, 240);
        pg.circle(455, 240, 35, false);                  // le ventilateur et ses pales
        pg.color(0.18, 0.8, 0.44);
        for (int k = 0; k < 4; ++k) {
            const double a = k * 3.14159265358979 / 2 + 0.4;
            pg.circle(455 + 18 * std::cos(a), 240 + 18 * std::sin(a), 9, true);
        }
        pg.color(0.2, 0.22, 0.26, true);
        pg.line(490, 222, 555, 222);                     // la gaine d'extraction
        pg.line(490, 258, 555, 258);
        pg.color(0.12, 0.14, 0.18);
        pg.text(108, 280, 10, "Q1", true);
        pg.text(205, 305, 10, "VAR-101", true);
        pg.text(340, 290, 10, "M-101", true);
        pg.text(436, 295, 10, "VE-101", true);
        pg.text(500, 245, 9, "air extrait");
        pg.color(0.93, 0.95, 0.98);
        pg.rect(40, 360, 515, 120);
        pg.color(0.12, 0.14, 0.18);
        pg.text(56, 386, 12, "Les deux vitesses du ventilateur", true);
        pg.text(56, 410, 11, "Petite vitesse (inoccupation) : 35 Hz, environ 70 % du d\xC3\xA9" "bit nominal.");
        pg.text(56, 428, 11, "Grande vitesse (cabine en peinture) : 50 Hz, le d\xC3\xA9" "bit nominal de 12 000 m3/h.");
        pg.text(56, 446, 11, "Le variateur monte en 10 s et descend en 15 s (rampes r\xC3\xA9gl\xC3\xA9" "es en usine).");
        pg.footer(2, 4);
    }
    // ---- 3 : mettre en route
    {
        auto& pg = pages[2];
        pg.color(0.12, 0.14, 0.18);
        pg.text(40, 60, 20, "2. Mettre en route le ventilateur", true);
        const char* steps[] = {
            "V\xC3\xA9rifier que le registre RG-3212 est ouvert (voyant O allum\xC3\xA9).",
            "Mettre le s\xC3\xA9lecteur de l'armoire sur DISTANCE : le variateur passe en DIST.",
            "Sur la vue CVC, v\xC3\xA9rifier que VAR-101 est PR\xC3\x8AT (bleu).",
            "Choisir la vitesse : petite (35 Hz) ou grande (50 Hz).",
            "Appuyer sur MARCHE : la fr\xC3\xA9quence monte jusqu'\xC3\xA0 sa consigne.",
            "Contr\xC3\xB4ler le courant : 18 A au plus en grande vitesse.",
            "En cas de d\xC3\xA9" "faut, voir la page 4 avant de r\xC3\xA9" "armer.",
        };
        for (int i = 0; i < 7; ++i) {
            pg.color(0.184, 0.435, 0.839);
            pg.circle(52, 118 + 34.0 * i, 9, true);
            pg.color(1, 1, 1);
            pg.text(i + 1 < 10 ? 48.5 : 46, 122 + 34.0 * i, 10, std::to_string(i + 1), true);
            pg.color(0.12, 0.14, 0.18);
            pg.text(72, 122 + 34.0 * i, 12, steps[i]);
        }
        pg.footer(3, 4);
    }
    // ---- 4 : defauts et remedes
    {
        auto& pg = pages[3];
        pg.color(0.12, 0.14, 0.18);
        pg.text(40, 60, 20, "3. D\xC3\xA9" "fauts du variateur et rem\xC3\xA8" "des", true);
        const char* rows[][3] = {
            {"Code", "Cause probable", "Rem\xC3\xA8" "de"},
            {"OHF", "Variateur trop chaud", "Nettoyer le filtre de l'armoire"},
            {"OLF", "Surcharge du moteur", "Contr\xC3\xB4ler le registre et la turbine"},
            {"SLF", "Liaison Modbus coup\xC3\xA9" "e", "V\xC3\xA9rifier le c\xC3\xA2" "ble vers l'automate"},
            {"USF", "Sous-tension du r\xC3\xA9seau", "Attendre le retour du r\xC3\xA9seau"},
            {"OCF", "Surintensit\xC3\xA9", "Appeler la maintenance"},
        };
        const double x0 = 40, cols[] = {70, 190, 255}, top = 100, rh = 28;
        for (int r = 0; r < 6; ++r) {
            if (r == 0) {
                pg.color(0.184, 0.435, 0.839);
                pg.rect(x0, top, 515, rh);
            }
            double x = x0;
            for (int c = 0; c < 3; ++c) {
                if (r == 0) pg.color(1, 1, 1);
                else pg.color(0.12, 0.14, 0.18);
                pg.text(x + 8, top + rh * r + 18, 11, rows[r][c], r == 0 || c == 0);
                x += cols[c];
            }
        }
        pg.color(0.6, 0.63, 0.68, true);
        pg.width(0.8);
        for (int r = 0; r <= 6; ++r) pg.line(x0, top + rh * r, x0 + 515, top + rh * r);
        double x = x0;
        pg.line(x, top, x, top + rh * 6);
        for (const double cw : cols) {
            x += cw;
            pg.line(x, top, x, top + rh * 6);
        }
        pg.color(0.12, 0.14, 0.18);
        pg.text(40, 300, 11, "Apr\xC3\xA8s un d\xC3\xA9" "faut : corriger la cause, puis r\xC3\xA9" "armer le variateur depuis la vue CVC.");
        pg.footer(4, 4);
    }
    return writePdf(pages);
}

} // namespace hmi::doc
