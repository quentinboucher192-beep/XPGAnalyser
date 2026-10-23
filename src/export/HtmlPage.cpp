// =============================================================================
//  export/HtmlPage.cpp - 1.12.30 : voir HtmlPage.hpp
// =============================================================================
#include "HtmlPage.hpp"

#include "ProgramBook.hpp"

#include <cctype>
#include <cstdio>

namespace exporter::html {

namespace {

constexpr const char* kCss = R"CSS(
:root{--bg:#f4f6f9;--panel:#fff;--ink:#1d2430;--muted:#5d6878;--line:#d9dee6;--accent:#2f6fde;--accent-ink:#fff;
--soft:#e9effb;--code:#f7f8fa;--warn:#b54708;--bad:#c4321f;--ok:#1d7a46}
@media (prefers-color-scheme:dark){:root{--bg:#161a20;--panel:#1f242c;--ink:#e6e9ee;--muted:#a3adbb;--line:#353c47;
--accent:#5b93f0;--accent-ink:#0d1117;--soft:#26324a;--code:#191d23;--warn:#f0a35e;--bad:#ff7b6b;--ok:#5fd18b}}
*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{font:14px/1.45 "Segoe UI",Tahoma,"DejaVu Sans",Arial,sans-serif;background:var(--bg);color:var(--ink)}
a{color:var(--accent)}
header.top{display:flex;flex-wrap:wrap;align-items:center;gap:8px 18px;padding:12px 20px;background:var(--panel);
border-bottom:1px solid var(--line);position:sticky;top:0;z-index:5}
header.top .app{font-weight:600;color:var(--accent);white-space:nowrap}
header.top h1{font-size:18px;margin:0;font-weight:600}
header.top .sub{color:var(--muted);font-size:13px;flex:1 1 280px}
button.btn{font:inherit;border:1px solid var(--accent);background:var(--accent);color:var(--accent-ink);border-radius:6px;
padding:6px 12px;cursor:pointer;white-space:nowrap}
button.btn:hover{filter:brightness(1.08)}
.hint{color:var(--muted);font-size:12px}
.layout{display:flex;align-items:flex-start;min-height:calc(100vh - 60px)}
nav.side{flex:0 0 250px;position:sticky;top:60px;max-height:calc(100vh - 60px);overflow:auto;padding:12px 10px 30px;
border-right:1px solid var(--line);background:var(--panel)}
nav.side input{width:100%;font:inherit;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--ink)}
nav.side h3{font-size:11px;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin:16px 6px 4px}
nav.side a{display:flex;justify-content:space-between;gap:8px;padding:4px 8px;border-radius:5px;color:var(--ink);text-decoration:none}
nav.side a:hover{background:var(--soft)}
nav.side a.on{background:var(--accent);color:var(--accent-ink)}
nav.side a .b{color:var(--muted);font-size:12px;white-space:nowrap}
nav.side a.on .b{color:inherit}
main{flex:1 1 auto;min-width:0;padding:18px 22px 60px}
.sec{display:none}
.sec.on{display:block}
.sec>h2{font-size:20px;margin:0 0 12px;font-weight:600}
.sec>h2 small{font-size:13px;color:var(--muted);font-weight:400;margin-left:8px}
.screen{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:6px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
.screen svg{display:block;width:100%;height:auto}
svg text{white-space:pre}
.hot{cursor:pointer}
.hot rect,.hot polygon{fill:transparent;stroke:transparent}
.hot:hover rect,.hot:hover polygon,.hot:focus rect,.hot:focus polygon{fill:rgba(47,111,222,.14);stroke:#2f6fde;stroke-width:2}
.links{margin:10px 2px;color:var(--muted);display:flex;flex-wrap:wrap;gap:4px 14px}
p.desc{margin:8px 2px;color:var(--muted)}
table.t{border-collapse:collapse;width:100%;background:var(--panel);border:1px solid var(--line)}
table.t th,table.t td{border-bottom:1px solid var(--line);padding:5px 8px;text-align:left;vertical-align:top}
table.t th{background:var(--soft);position:sticky;top:60px;font-weight:600}
table.t td.num{text-align:right;white-space:nowrap}
table.t td.mono,.mono{font-family:Consolas,"DejaVu Sans Mono",monospace;font-size:12.5px}
table.t td.warn{color:var(--warn)}
input.filter{font:inherit;margin:0 0 8px;padding:5px 8px;width:min(360px,100%);border:1px solid var(--line);border-radius:6px;
background:var(--panel);color:var(--ink)}
.card{display:grid;grid-template-columns:minmax(150px,max-content) 1fr;gap:0;background:var(--panel);border:1px solid var(--line);border-radius:6px}
.card div{padding:6px 10px;border-bottom:1px solid var(--line)}
.card div.k{color:var(--muted)}
.tiles{display:flex;flex-wrap:wrap;gap:10px;margin:0 0 16px}
.tiles div{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:8px 14px;min-width:110px}
.tiles b{display:block;font-size:20px}
.tiles span{color:var(--muted);font-size:12px}
pre.code{background:var(--code);border:1px solid var(--line);border-radius:6px;padding:8px 0;overflow:auto;margin:0 0 14px;
font:12.5px/1.5 Consolas,"DejaVu Sans Mono",monospace}
pre.code .l{display:block;padding:0 12px 0 0;white-space:pre}
pre.code .n{display:inline-block;width:4.2em;padding-right:10px;text-align:right;color:var(--muted);user-select:none}
.k1{color:#1f5fbf;font-weight:600}.k2{color:#7a3fb0}.k3{color:#5b8a35;font-style:italic}.k4{color:#b1501f}.k5{color:#0b7d8a}
.k6{color:#a16207}.k7{color:#0f6ab4}
@media (prefers-color-scheme:dark){.k1{color:#7fb0ff}.k2{color:#c9a3f5}.k3{color:#8fc36d}.k4{color:#f0a070}.k5{color:#62c9d6}.k6{color:#e3b55a}.k7{color:#6fb6f2}}
h3.sub{font-size:15px;margin:22px 0 8px}
.ov{position:fixed;inset:0;background:rgba(10,14,20,.45);display:flex;align-items:center;justify-content:center;z-index:20}
.ov[hidden]{display:none}
.ov .win{background:var(--panel);border-radius:8px;box-shadow:0 10px 40px rgba(0,0,0,.4);max-width:94vw;max-height:92vh;overflow:auto}
.ov .wbar{display:flex;align-items:center;gap:10px;padding:8px 10px 8px 14px;border-bottom:1px solid var(--line)}
.ov .wbar b{flex:1}
.ov .wbar button{font:inherit;font-size:18px;line-height:1;border:0;background:transparent;color:var(--ink);cursor:pointer;padding:2px 6px}
.ov .wbody{padding:8px}
.ov .wbody .screen{box-shadow:none;border:0;padding:0}
@media (max-width:760px){.layout{display:block}nav.side{position:static;max-height:none;border-right:0;border-bottom:1px solid var(--line)}
main{padding:12px}header.top{position:static}table.t th{top:0}}
@media print{header.top,nav.side{position:static}nav.side,#dl,.hint,input.filter{display:none}.sec{display:block!important;break-before:page}
.layout{display:block}body{background:#fff}}
)CSS";

constexpr const char* kScript = R"JS(
(function(){
  var first = document.body.getAttribute('data-first');
  function sections(){ return document.querySelectorAll('main > section.sec'); }
  function show(id){
    var s = document.getElementById(id);
    if (!s || !s.classList.contains('sec')) { s = document.getElementById(first); id = first; }
    if (!s) return;
    var all = sections();
    for (var i = 0; i < all.length; i++) all[i].classList.toggle('on', all[i] === s);
    var links = document.querySelectorAll('nav.side a');
    for (var j = 0; j < links.length; j++) links[j].classList.toggle('on', links[j].getAttribute('href') === '#' + id);
    closePopup();
    window.scrollTo(0, 0);
  }
  function route(){ show(decodeURIComponent(location.hash.slice(1)) || first); }
  var ov = document.getElementById('ov');
  function closePopup(){ if (ov) { ov.hidden = true; ov.querySelector('.wbody').innerHTML = ''; } }
  function openPopup(id){
    var s = document.getElementById(id);
    if (!s || !ov) return;
    var screen = s.querySelector('.screen');
    if (!screen) { location.hash = '#' + id; return; }
    var body = ov.querySelector('.wbody');
    body.innerHTML = '';
    body.appendChild(screen.cloneNode(true));
    ov.querySelector('.wbar b').textContent = s.getAttribute('data-name') || id;
    var w = parseFloat(s.getAttribute('data-width') || '600');
    ov.querySelector('.win').style.width = Math.min(w + 20, window.innerWidth * 0.94) + 'px';
    ov.hidden = false;
  }
  document.addEventListener('click', function(e){
    var t = e.target.closest ? e.target.closest('[data-go],[data-popup],[data-back],[data-close]') : null;
    if (!t) return;
    e.preventDefault();
    if (t.hasAttribute('data-close')) { closePopup(); return; }
    if (t.hasAttribute('data-back')) { closePopup(); history.back(); return; }
    if (t.hasAttribute('data-popup')) { openPopup(t.getAttribute('data-popup')); return; }
    location.hash = '#' + t.getAttribute('data-go');
  });
  document.addEventListener('keydown', function(e){
    if (e.key === 'Escape') closePopup();
    if ((e.key === 'Enter' || e.key === ' ') && e.target.closest && e.target.closest('.hot')) { e.preventDefault(); e.target.closest('.hot').dispatchEvent(new MouseEvent('click', {bubbles: true})); }
  });
  if (ov) ov.addEventListener('click', function(e){ if (e.target === ov) closePopup(); });
  window.addEventListener('hashchange', route);
  var find = document.getElementById('find');
  if (find) find.addEventListener('input', function(){
    var q = find.value.toLowerCase();
    var links = document.querySelectorAll('nav.side a');
    for (var i = 0; i < links.length; i++) links[i].style.display = links[i].textContent.toLowerCase().indexOf(q) < 0 ? 'none' : '';
  });
  var filters = document.querySelectorAll('input.filter');
  for (var f = 0; f < filters.length; f++) (function(input){
    input.addEventListener('input', function(){
      var q = input.value.toLowerCase();
      var rows = input.nextElementSibling.querySelectorAll('tbody tr');
      for (var r = 0; r < rows.length; r++) rows[r].style.display = rows[r].textContent.toLowerCase().indexOf(q) < 0 ? 'none' : '';
    });
  })(filters[f]);
  var dl = document.getElementById('dl');
  if (dl) dl.addEventListener('click', function(){
    var s = document.getElementById('xpg-archive');
    if (!s) return;
    var bin = atob(s.textContent.replace(/\s+/g, ''));
    var bytes = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    var url = URL.createObjectURL(new Blob([bytes], {type: 'application/zip'}));
    var a = document.createElement('a');
    a.href = url;
    a.download = s.getAttribute('data-nom') || 'projet.zip';
    document.body.appendChild(a);
    a.click();
    setTimeout(function(){ URL.revokeObjectURL(url); a.remove(); }, 2000);
  });
  route();
})();
)JS";

const char* runClass(book::Run::Kind k) {
    switch (k) {
        case book::Run::Keyword:  return "k1";
        case book::Run::Type:     return "k2";
        case book::Run::Comment:  return "k3";
        case book::Run::String:   return "k4";
        case book::Run::Number:   return "k5";
        case book::Run::Address:  return "k6";
        case book::Run::Function: return "k7";
        case book::Run::Plain:    break;
    }
    return "";
}

} // namespace

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (const char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') out += ' ';
                else out += c;
        }
    }
    return out;
}

std::string anchor(std::string_view s) {
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        out += (std::isalnum(u) && u < 0x80) || c == '-' || c == '_' ? c : '-';
    }
    return out.empty() ? std::string("s") : out;
}

std::string table(const std::vector<std::string>& headers, const std::vector<std::vector<Cell>>& rows, bool filter,
                  std::string_view emptyText) {
    if (rows.empty()) return "<p class=\"desc\">" + escape(emptyText) + "</p>\n";
    std::string out;
    if (filter && rows.size() > 8) out += "<input class=\"filter\" type=\"search\" placeholder=\"Filtrer ces lignes\xE2\x80\xA6\">";
    out += "<table class=\"t\"><thead><tr>";
    for (const auto& h : headers) out += "<th>" + escape(h) + "</th>";
    out += "</tr></thead><tbody>\n";
    for (const auto& r : rows) {
        out += "<tr>";
        for (const auto& c : r) {
            out += c.css.empty() ? "<td>" : "<td class=\"" + escape(c.css) + "\">";
            out += c.html.empty() ? escape(c.text) : c.html;
            out += "</td>";
        }
        out += "</tr>\n";
    }
    out += "</tbody></table>\n";
    return out;
}

std::string card(const std::vector<std::pair<std::string, std::string>>& rows) {
    std::string out = "<div class=\"card\">";
    for (const auto& [k, v] : rows) {
        if (v.empty()) continue;
        out += "<div class=\"k\">" + escape(k) + "</div><div>" + escape(v) + "</div>";
    }
    out += "</div>\n";
    return out;
}

std::string tiles(const std::vector<std::pair<std::string, std::string>>& items) {
    std::string out = "<div class=\"tiles\">";
    for (const auto& [v, label] : items) out += "<div><b>" + escape(v) + "</b><span>" + escape(label) + "</span></div>";
    out += "</div>\n";
    return out;
}

std::string code(const std::vector<std::string>& lines, bool numbers) {
    std::string out = "<pre class=\"code\">";
    bool inComment = false;
    std::size_t n = 0;
    for (const auto& line : lines) {
        ++n;
        out += "<span class=\"l\">";
        if (numbers) out += "<span class=\"n\">" + std::to_string(n) + "</span>";
        for (const auto& run : book::colorize(line, inComment)) {
            const char* css = runClass(run.kind);
            if (*css) out += std::string("<span class=\"") + css + "\">" + escape(run.text) + "</span>";
            else out += escape(run.text);
        }
        out += "</span>";
    }
    out += "</pre>\n";
    return out;
}

std::string render(const Page& p) {
    const std::string kindKey(project::archive::key(p.kind));
    std::string out;
    out.reserve(256 * 1024);
    out += "<!doctype html>\n<html lang=\"fr\">\n<head>\n<meta charset=\"utf-8\">\n";
    out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out += "<meta name=\"generator\" content=\"" + escape(p.application) + "\">\n";
    if (p.archive) out += project::archive::pageHeadMarker(p.kind) + "\n";
    out += "<title>" + escape(p.title) + " \xE2\x80\x94 " + escape(p.application) + "</title>\n";
    out += "<style>" + std::string(kCss) + p.extraCss + "</style>\n</head>\n";
    out += "<body data-first=\"" + escape(p.first) + "\" data-genre=\"" + kindKey + "\">\n";
    out += "<header class=\"top\"><span class=\"app\">" + escape(p.application) + "</span><h1>" + escape(p.title) + "</h1>";
    out += "<span class=\"sub\">" + escape(p.subtitle) + "</span>";
    if (p.archive) {
        const std::string app(project::archive::application(p.kind));
        out += "<span><button class=\"btn\" id=\"dl\" type=\"button\" title=\"Le projet entier, dans un seul fichier, \xC3\xA0 ouvrir avec "
             + escape(app) + "\">T\xC3\xA9l\xC3\xA9" "charger le projet (" + escape(p.archiveName) + ", "
             + escape(project::archive::sizeText(p.archive->size())) + ")</button><br><span class=\"hint\">Le rouvrir : " + escape(app)
             + " \xE2\x80\xBA Projet \xE2\x80\xBA Importer depuis une page web\xE2\x80\xA6</span></span>";
    }
    out += "</header>\n<div class=\"layout\">\n<nav class=\"side\"><input id=\"find\" type=\"search\" placeholder=\"Chercher dans le menu\xE2\x80\xA6\">\n";
    for (const auto& g : p.nav) {
        if (g.links.empty()) continue;
        out += "<h3>" + escape(g.title) + "</h3>\n";
        for (const auto& l : g.links)
            out += "<a href=\"#" + escape(l.target) + "\"><span>" + escape(l.label) + "</span>"
                 + (l.badge.empty() ? std::string{} : "<span class=\"b\">" + escape(l.badge) + "</span>") + "</a>\n";
    }
    out += "</nav>\n<main>\n";
    for (const auto& s : p.sections) {
        out += "<section class=\"sec " + escape(s.css) + "\" id=\"" + escape(s.id) + "\" data-name=\"" + escape(s.title) + "\""
             + (s.attributes.empty() ? std::string{} : " " + s.attributes) + ">\n";
        out += "<h2>" + escape(s.title) + (s.subtitle.empty() ? std::string{} : "<small>" + escape(s.subtitle) + "</small>") + "</h2>\n";
        out += s.html;
        out += "</section>\n";
    }
    out += "</main>\n</div>\n";
    out += "<div id=\"ov\" class=\"ov\" hidden><div class=\"win\"><div class=\"wbar\"><b></b><button type=\"button\" data-close "
           "title=\"Fermer (\xC3\x89" "chap)\">\xC3\x97</button></div><div class=\"wbody\"></div></div></div>\n";
    if (!p.defs.empty())
        out += "<svg width=\"0\" height=\"0\" style=\"position:absolute\" aria-hidden=\"true\"><defs>" + p.defs + "</defs></svg>\n";
    out += "<script>" + std::string(kScript) + "</script>\n";
    if (p.archive) out += project::archive::pageArchiveBlock(*p.archive, p.kind, p.archiveName);
    out += "</body>\n</html>\n";
    return out;
}

} // namespace exporter::html
