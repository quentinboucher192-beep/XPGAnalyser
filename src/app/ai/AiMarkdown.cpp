// =============================================================================
//  app/ai/AiMarkdown.cpp - voir AiMarkdown.hpp
// =============================================================================
#include "AiMarkdown.hpp"

#include <algorithm>
#include <cctype>

namespace app::ai::markdown {

namespace {

struct Fence {
    char        ch{0};
    std::size_t count{0};
    std::string info;   // la langue (le premier mot apres la balise)
};

std::string_view trimRight(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// Une ligne qui ouvre (ou ferme) un bloc : au plus 3 espaces, puis au moins 3 ` ou ~.
bool fenceOf(std::string_view line, Fence* out) {
    std::size_t i = 0;
    while (i < line.size() && i < 4 && line[i] == ' ') ++i;
    if (i > 3 || i >= line.size() || (line[i] != '`' && line[i] != '~')) return false;
    const char ch = line[i];
    std::size_t n = 0;
    while (i + n < line.size() && line[i + n] == ch) ++n;
    if (n < 3) return false;
    std::string_view rest = trimRight(line.substr(i + n));
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
    if (ch == '`' && rest.find('`') != std::string_view::npos) return false;   // ```du texte``` : pas une balise
    std::size_t w = 0;
    while (w < rest.size() && rest[w] != ' ' && rest[w] != '\t' && rest[w] != '{') ++w;
    out->ch = ch;
    out->count = n;
    out->info = std::string(rest.substr(0, w));
    return true;
}

bool closes(std::string_view line, const Fence& open) {
    Fence f;
    return fenceOf(line, &f) && f.ch == open.ch && f.count >= open.count && f.info.empty()
           && trimRight(line).find_first_not_of(std::string(" ") + open.ch) == std::string_view::npos;
}

} // namespace

std::vector<Segment> split(std::string_view text) {
    std::vector<Segment> out;
    std::string prose;
    const auto flushText = [&] {
        while (!prose.empty() && (prose.back() == '\n' || prose.back() == '\r')) prose.pop_back();
        std::size_t lead = 0;
        while (lead < prose.size() && (prose[lead] == '\n' || prose[lead] == '\r')) ++lead;
        if (lead < prose.size()) {
            Segment s;
            s.text = prose.substr(lead);
            out.push_back(std::move(s));
        }
        prose.clear();
    };
    std::size_t pos = 0;
    bool inCode = false;
    Fence open;
    Segment code;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const bool last = nl == std::string_view::npos;
        const std::string_view line = text.substr(pos, last ? std::string_view::npos : nl - pos);
        if (!inCode) {
            Fence f;
            if (fenceOf(line, &f)) {
                flushText();
                inCode = true;
                open = f;
                code = Segment{};
                code.kind = Segment::Kind::Code;
                code.lang = f.info;
                code.closed = false;
            } else {
                prose.append(line);
                if (!last) prose.push_back('\n');
            }
        } else if (closes(line, open)) {
            if (!code.text.empty() && code.text.back() == '\n') code.text.pop_back();
            code.closed = true;
            out.push_back(std::move(code));
            code = Segment{};
            inCode = false;
        } else {
            std::string_view l = line;
            if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
            code.text.append(l);
            if (!last) code.text.push_back('\n');
        }
        if (last) break;
        pos = nl + 1;
    }
    if (inCode) {   // pas encore ferme (la reponse arrive) : deja un bloc
        if (!code.text.empty() && code.text.back() == '\n') code.text.pop_back();
        out.push_back(std::move(code));
    } else {
        flushText();
    }
    return out;
}

bool hasCode(std::string_view text) {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        Fence f;
        if (fenceOf(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos), &f)) return true;
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    return false;
}

std::string languageLabel(std::string_view lang) {
    std::string l;
    for (const char c : lang) l.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (l.empty()) return "Code";
    if (l == "st" || l == "iecst" || l == "iec" || l == "structured-text") return "ST";
    if (l == "cpp" || l == "c++" || l == "cxx" || l == "hpp") return "C++";
    if (l == "c" || l == "h") return "C";
    if (l == "json") return "JSON";
    if (l == "xml") return "XML";
    if (l == "csv") return "CSV";
    if (l == "sh" || l == "bash" || l == "shell") return "Shell";
    if (l == "ps1" || l == "powershell") return "PowerShell";
    if (l == "bat" || l == "cmd") return "Batch";
    if (l == "py" || l == "python") return "Python";
    if (l == "js" || l == "javascript") return "JavaScript";
    if (l == "sql") return "SQL";
    if (l == "ini") return "INI";
    if (l == "txt" || l == "text" || l == "plaintext") return "Texte";
    std::string up(lang);
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up;
}

} // namespace app::ai::markdown
