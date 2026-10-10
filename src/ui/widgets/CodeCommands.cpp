// =============================================================================
//  ui/widgets/CodeCommands.cpp - 1.12.2 : les raccourcis de Visual Studio dans
//  MultiLineText (voir Controls.hpp et ui/KeyMap.hpp)
// -----------------------------------------------------------------------------
//  Les commandes de l'editeur de code : commenter, dupliquer, deplacer des
//  lignes, mettre en forme, entourer d'un bloc, aller au bout du bloc, chercher
//  et remplacer dans le document, aller a la ligne, les signets, les fautes (F8,
//  Ctrl+.), les plis au clavier. Chacune est UNE modification du texte
//  (applyEdit) et un seul pas d'annulation (beginStep / endStep scellent la pile
//  de l'appli). Celles que l'editeur ne sait pas faire seul (aller a la
//  definition, compiler, demarrer la simulation) sont demandees a son volet,
//  puis a l'appli.
// =============================================================================
#include "Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace ui {

namespace {

constexpr bool identChar(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}
constexpr bool identStart(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

MultiLineText*                gFocusedCode = nullptr;
MultiLineText::CommandHandler gHostCommands;
MultiLineText::NoticeSink     gNoticeSink;

std::string upperAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}
std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
std::size_t leadingSpace(std::string_view l) {
    std::size_t k = 0;
    while (k < l.size() && (l[k] == ' ' || l[k] == '\t')) ++k;
    return k;
}
bool blankLine(std::string_view l) { return leadingSpace(l) == l.size(); }
std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t a = 0;
    for (;;) {
        const auto b = text.find('\n', a);
        if (b == std::string_view::npos) { out.push_back(text.substr(a)); break; }
        out.push_back(text.substr(a, b - a));
        a = b + 1;
    }
    return out;
}
std::size_t countBreaks(std::string_view s) { return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n')); }

// ---- les mots des blocs (ST) ----------------------------------------------------------
enum class Block : std::uint8_t { None, Open, Close, Middle };
Block blockOf(std::string_view upperWord) {
    static constexpr std::string_view opens[] = { "IF", "FOR", "WHILE", "REPEAT", "CASE", "TRY", "VAR", "VAR_INPUT",
                                                  "VAR_OUTPUT", "VAR_IN_OUT", "VAR_TEMP", "VAR_GLOBAL", "VAR_EXTERNAL",
                                                  "FUNCTION", "FUNCTION_BLOCK" };
    static constexpr std::string_view closes[] = { "END_IF", "END_FOR", "END_WHILE", "END_REPEAT", "END_CASE", "END_TRY",
                                                   "END_VAR", "END_FUNCTION", "END_FUNCTION_BLOCK" };
    static constexpr std::string_view middles[] = { "ELSIF", "ELSE", "CATCH", "UNTIL" };
    for (const auto w : opens) if (w == upperWord) return Block::Open;
    for (const auto w : closes) if (w == upperWord) return Block::Close;
    for (const auto w : middles) if (w == upperWord) return Block::Middle;
    return Block::None;
}
struct Word {
    std::size_t at{ 0 };      // dans la ligne
    std::string up;           // en majuscules
    Block       kind{ Block::None };
};
// Les mots d'une ligne hors commentaires et chaines ; `inComment` suit les (* *) d'une
// ligne a l'autre. Un mot apres un point (Moteur.Type) ou avant # (ETAT#Marche) n'est
// pas un mot du langage.
std::vector<Word> lineWords(std::string_view line, bool& inComment) {
    std::vector<Word> out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (inComment) {
            const auto e = line.find("*)", i);
            if (e == std::string_view::npos) return out;
            inComment = false;
            i = e + 2;
            continue;
        }
        const char c = line[i];
        const char n = i + 1 < line.size() ? line[i + 1] : '\0';
        if (c == '(' && n == '*') { inComment = true; i += 2; continue; }
        if (c == '/' && n == '/') break;
        if (c == '\'' || c == '"') {
            std::size_t k = i + 1;
            while (k < line.size() && line[k] != c) k += line[k] == '$' ? 2 : 1;
            i = k + 1;
            continue;
        }
        if (identStart(c)) {
            std::size_t k = i;
            while (k < line.size() && identChar(line[k])) ++k;
            const bool member = i > 0 && line[i - 1] == '.';
            const bool literal = k < line.size() && line[k] == '#';
            if (!member && !literal) {
                Word w;
                w.at = i;
                w.up = upperAscii(line.substr(i, k - i));
                w.kind = blockOf(w.up);
                out.push_back(std::move(w));
            }
            i = k;
            continue;
        }
        ++i;
    }
    return out;
}

// Les blocs a entourer (Ctrl+K, Ctrl+S) et les extraits (Ctrl+K, Ctrl+X) :
// `head` avant le texte, `tail` apres ; `place` : le mot a choisir (vide : le curseur
// dans le corps).
struct Wrap {
    std::string_view label, detail, head, middle, tail, place;
};
constexpr Wrap kWraps[] = {
    { "IF \xE2\x80\xA6 END_IF", "Si une condition est vraie", "IF condition THEN", "", "END_IF", "condition" },
    { "IF \xE2\x80\xA6 ELSE \xE2\x80\xA6 END_IF", "Si, sinon", "IF condition THEN", "ELSE", "END_IF", "condition" },
    { "FOR \xE2\x80\xA6 END_FOR", "Pour i de 0 \xC3\xA0 10", "FOR i := 0 TO 10 DO", "", "END_FOR", "i" },
    { "WHILE \xE2\x80\xA6 END_WHILE", "Tant que", "WHILE condition DO", "", "END_WHILE", "condition" },
    { "REPEAT \xE2\x80\xA6 UNTIL", "R\xC3\xA9p\xC3\xA9ter jusqu'\xC3\xA0", "REPEAT", "", "UNTIL condition\nEND_REPEAT", "condition" },
    { "CASE \xE2\x80\xA6 END_CASE", "Selon la valeur", "CASE valeur OF\n\t1:", "", "END_CASE", "valeur" },
    { "TRY \xE2\x80\xA6 CATCH \xE2\x80\xA6 END_TRY", "Essayer, rattraper l'erreur", "TRY", "CATCH Erreur", "END_TRY", "" },
};

}   // namespace

// ============================================================ le focus, l'appli ===
MultiLineText::~MultiLineText() {
    if (gFocusedCode == this) gFocusedCode = nullptr;
}

void MultiLineText::onFocusChanged(bool focused) {
    if (focused && commandKeys_) gFocusedCode = this;
    else if (!focused && gFocusedCode == this) gFocusedCode = nullptr;
    if (!focused && resolver_.pending()) {
        resolver_.cancel();
        say({});
        invalidate();
    }
}

MultiLineText* MultiLineText::focusedCodeEditor() noexcept {
    return gFocusedCode && gFocusedCode->focused() && gFocusedCode->commandKeys_ ? gFocusedCode : nullptr;
}

void MultiLineText::setHostCommandHandler(CommandHandler h) { gHostCommands = std::move(h); }
void MultiLineText::setNoticeSink(NoticeSink sink) { gNoticeSink = std::move(sink); }

void MultiLineText::say(std::string text, Tone tone) {
    lastNotice_ = text;
    if (gNoticeSink) gNoticeSink(text, tone);
}

bool MultiLineText::requestHost(std::string_view id) {
    if (commandHandler_ && commandHandler_(*this, id)) return true;
    return gHostCommands && gHostCommands(*this, id);
}

void MultiLineText::beginStep() {
    if (auto* s = core::CommandGroupScope::stack()) s->seal();
}
void MultiLineText::endStep() {
    if (auto* s = core::CommandGroupScope::stack()) s->seal();
}

// ============================================================== les touches ===
bool MultiLineText::claimsKey(const KeyDown& k) const {
    if (!commandKeys_ || !focused()) return false;
    if (resolver_.pending()) return true;
    const auto p = keymap::current();
    const auto s = keymap::strokeOf(k);
    if (keymap::startsChord(p, s)) return true;
    const auto cmd = keymap::single(p, s);
    if (cmd.empty()) return false;
    // Une commande que l'editeur demande a l'ecran : elle est a lui quand meme (F12 la
    // definition et non la capture) ; sauf "compiler", que le volet prend a F7.
    return cmd != "compiler";
}

bool MultiLineText::handleCommandKey(const KeyDown& k) {
    const auto p = keymap::current();
    const bool wasPending = resolver_.pending();
    const auto r = resolver_.feed(p, k);
    using O = keymap::Resolver::Outcome;
    switch (r.outcome) {
        case O::None:
            return wasPending;                     // une touche seule (Maj) pendant l'attente
        case O::Pending:
            say(keymap::pendingMessage(r.chord), Tone::Info);
            invalidate();
            return true;
        case O::Unknown:
            say(keymap::unknownMessage(r.chord), Tone::Warning);
            invalidate();
            return true;
        case O::Command: break;
    }
    if (wasPending) { say({}); invalidate(); }
    const auto* c = keymap::command(r.command);
    if (c && c->host) {
        // Personne pour la faire : la touche suit son chemin (le volet compile a F7).
        return requestHost(r.command) || wasPending;
    }
    const bool done = runCommand(r.command);
    // Ctrl+C, Ctrl+X, Ctrl+A... : une commande qui ne s'applique pas (lecture seule) prend
    // quand meme sa touche, sauf les deplacements qui reviennent au comportement de base.
    return done || wasPending || (r.command != "copier" && r.command != "couper");
}

bool MultiLineText::runCommand(std::string_view id) {
    const auto* c = keymap::command(id);
    if (!c) return false;
    if (c->host) return requestHost(id);
    if (id == "commenter") return commentLines(1);
    if (id == "decommenter") return commentLines(-1);
    if (id == "basculerCommentaire") return commentLines(0);
    if (id == "dupliquer") return duplicate();
    if (id == "monterLignes") return moveLines(-1);
    if (id == "descendreLignes") return moveLines(1);
    if (id == "couperLigne") return removeLines(true);
    if (id == "supprimerLigne") return removeLines(false);
    if (id == "copier") return copyOrCut(false);
    if (id == "couper") return copyOrCut(true);
    if (id == "insererAuDessus") return openLine(true);
    if (id == "insererAuDessous") return openLine(false);
    if (id == "majuscules") return changeCase(true);
    if (id == "minuscules") return changeCase(false);
    if (id == "effacerMotAvant") { deleteWord(false); return true; }
    if (id == "effacerMotApres") { deleteWord(true); return true; }
    if (id == "formaterDocument") return formatLines(false);
    if (id == "formaterSelection") return formatLines(true);
    if (id == "entourer") return showPicker(1);
    if (id == "extrait") return showPicker(2);
    if (id == "toutChoisir") { selectAll(); return true; }
    if (id == "choisirMot") return selectWordAtCaret();
    if (id == "accolade") return matchBlock(false);
    if (id == "choisirBloc") return matchBlock(true);
    if (id == "rechercher") { openBar(BarMode::Find); return true; }
    if (id == "remplacer") {
        if (readOnly_) { openBar(BarMode::Find); return true; }
        openBar(BarMode::Replace);
        return true;
    }
    if (id == "suivant") return findNext(false);
    if (id == "precedent") return findNext(true);
    if (id == "rechercherMot") {
        const auto [a, b] = wordAround(offsetOf(caret_));
        if (b <= a) { say("Ctrl+F3 : le curseur n'est pas sur un mot"); return true; }
        setFindText(buffer_.substr(a, b - a));
        if (barMode_ == BarMode::None) barMode_ = BarMode::Find;
        barFocus_ = false;
        caret_ = anchor_ = caretFromOffset(b);
        return findNext(false);
    }
    if (id == "allerLigne") { openBar(BarMode::GoToLine); return true; }
    if (id == "revenir") return popBack();
    if (id == "diagSuivant") return stepSquiggle(false);
    if (id == "diagPrecedent") return stepSquiggle(true);
    if (id == "corriger") return applyFix();
    if (id == "signet") return toggleBookmark();
    if (id == "signetSuivant") return stepBookmark(false);
    if (id == "signetPrecedent") return stepBookmark(true);
    if (id == "completer") { requestCompletion(); return true; }
    if (id == "infoParametres") return parameterInfo();
    if (id == "infoRapide") return quickInfo();
    if (id == "plier") return foldAtCaret();
    if (id == "plierTout") {
        const bool any = std::find(folded_.begin(), folded_.end(), true) != folded_.end();
        if (any) unfoldAll(); else foldAll();
        ensureCaretVisible();
        say(any ? "Tout d\xC3\xA9pli\xC3\xA9 (Ctrl+M, Ctrl+L)" : "Tout repli\xC3\xA9 (Ctrl+M, Ctrl+L)");
        return true;
    }
    if (id == "plierNiveau") {
        foldToDepth(1);
        ensureCaretVisible();
        say("Les blocs du premier niveau sont repli\xC3\xA9s (Ctrl+M, Ctrl+O)");
        return true;
    }
    return false;
}

// ============================================================ les outils de ligne ===
MultiLineText::LineSpan MultiLineText::selectedLines() const {
    LineSpan s;
    if (lines_.empty()) return s;
    const auto [a, b] = orderedSelection();
    s.first = a.line;
    s.last = b.line;
    // Une selection qui finit au debut d'une ligne ne la prend pas (comme Visual Studio).
    if (s.last > s.first && b.column == 0) --s.last;
    s.last = std::min(s.last, lines_.size() - 1);
    return s;
}

std::string MultiLineText::indentUnit() const {
    return tabSpaces_ > 0 ? std::string(static_cast<std::size_t>(tabSpaces_), ' ') : std::string("\t");
}

void MultiLineText::replaceSpan(std::size_t from, std::size_t to, std::string_view text, std::size_t anchorAt,
                                std::size_t caretAt) {
    beginStep();
    applyEdit(from, to, text);
    endStep();
    anchor_ = caretFromOffset(std::min(anchorAt, buffer_.size()));
    caret_ = caretFromOffset(std::min(caretAt, buffer_.size()));
    ensureCaretVisible();
    invalidate();
}

std::pair<std::size_t, std::size_t> MultiLineText::wordAround(std::size_t at) const {
    at = std::min(at, buffer_.size());
    std::size_t a = at, b = at;
    while (a > 0 && identChar(buffer_[a - 1])) --a;
    while (b < buffer_.size() && identChar(buffer_[b])) ++b;
    return { a, b };
}

// Le bord du mot suivant (ou precedent), comme Ctrl+fleche : les espaces, puis un mot
// (lettres, chiffres, _) ou une suite de signes ; une fin de ligne compte pour un pas.
std::size_t MultiLineText::wordBoundary(std::size_t at, bool forward) const {
    const auto kind = [&](std::size_t i) -> int {
        const char c = buffer_[i];
        if (c == '\n') return 3;
        if (c == ' ' || c == '\t' || c == '\r') return 0;
        if (identChar(c) || (static_cast<unsigned char>(c) & 0x80)) return 1;
        return 2;
    };
    if (forward) {
        if (at >= buffer_.size()) return buffer_.size();
        if (buffer_[at] == '\n') return at + 1;
        std::size_t i = at;
        const int k = kind(i);
        if (k != 0) while (i < buffer_.size() && kind(i) == k) ++i;
        while (i < buffer_.size() && kind(i) == 0) ++i;
        return i;
    }
    if (at == 0) return 0;
    std::size_t i = at;
    if (buffer_[i - 1] == '\n') return i - 1;
    while (i > 0 && kind(i - 1) == 0) --i;
    if (i > 0 && buffer_[i - 1] == '\n') return i;
    if (i > 0) {
        const int k = kind(i - 1);
        while (i > 0 && kind(i - 1) == k) --i;
    }
    return i;
}

void MultiLineText::moveWord(bool forward, bool extend) {
    const auto to = wordBoundary(offsetOf(caret_), forward);
    caret_ = caretFromOffset(to);
    if (!extend) anchor_ = caret_;
    ensureCaretVisible();
    invalidate();
}

void MultiLineText::deleteWord(bool forward) {
    if (readOnly_) return;
    if (hasSelection()) { deleteSelection(); return; }
    const auto at = offsetOf(caret_);
    const auto to = wordBoundary(at, forward);
    if (to == at) return;
    applyEdit(std::min(at, to), std::max(at, to), {});
    dismissCompletion();
}

void MultiLineText::smartHome(bool extend) {
    if (lines_.empty()) return;
    const auto& l = lines_[caret_.line];
    const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
    const auto first = static_cast<std::uint32_t>(leadingSpace(text));
    caret_.column = caret_.column == first ? 0u : first;
    if (!extend) anchor_ = caret_;
    ensureCaretVisible();
    invalidate();
}

void MultiLineText::pageMove(int dir, bool extend) {
    if (lines_.empty()) return;
    const float lh = paintLineHeight_ > 0.f ? paintLineHeight_ : 16.f;
    const auto rows = static_cast<std::int64_t>(std::max(1.f, contentRect().h / lh) - 1.f);
    const auto step = std::max<std::int64_t>(1, rows);
    const float wanted = columnX(caret_.line, caret_.column);
    // Les lignes montrees (les plis comptent) : une page de lignes VISIBLES.
    std::size_t row = 0;
    for (std::size_t i = 0; i < visible_.size(); ++i)
        if (visible_[i] == caret_.line) { row = i; break; }
    const auto target = std::clamp<std::int64_t>(static_cast<std::int64_t>(row) + dir * step, 0,
                                                 static_cast<std::int64_t>(visible_.size()) - 1);
    caret_.line = visible_[static_cast<std::size_t>(target)];
    caret_.column = columnAtX(caret_.line, wanted);
    if (!extend) anchor_ = caret_;
    const auto first = static_cast<std::int64_t>(firstVisible_) + dir * step;
    firstVisible_ = static_cast<std::size_t>(std::clamp<std::int64_t>(first, 0, static_cast<std::int64_t>(visible_.size()) - 1));
    ensureCaretVisible();
    invalidate();
}

bool MultiLineText::selectWordAtCaret() {
    const auto [a, b] = wordAround(offsetOf(caret_));
    if (b <= a) return true;
    anchor_ = caretFromOffset(a);
    caret_ = caretFromOffset(b);
    invalidate();
    return true;
}

// ============================================================== les lignes ===
bool MultiLineText::commentLines(int mode) {
    if (readOnly_ || lines_.empty()) return true;
    const auto span = selectedLines();
    const auto from = lines_[span.first].begin;
    const auto to = lines_[span.last].end;
    const auto lines = splitLines(std::string_view(buffer_).substr(from, to - from));
    const bool slashes = commentStyle_ == CommentStyle::Slashes;
    const auto commented = [&](std::string_view l) {
        const auto t = l.substr(leadingSpace(l));
        if (t.substr(0, 2) == "//") return true;
        return t.size() >= 4 && t.substr(0, 2) == "(*" && t.substr(t.size() - 2) == "*)";
    };
    bool all = true, any = false;
    std::size_t minIndent = std::string::npos;
    for (const auto l : lines) {
        if (blankLine(l)) continue;
        any = true;
        all = all && commented(l);
        minIndent = std::min(minIndent, leadingSpace(l));
    }
    if (!any) return true;
    const bool comment = mode > 0 || (mode == 0 && !all);
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto l = lines[i];
        if (i) out += '\n';
        if (blankLine(l)) { out += l; continue; }
        if (comment) {
            const auto cut = std::min(minIndent, l.size());
            out += l.substr(0, cut);
            if (slashes) { out += "// "; out += l.substr(cut); }
            else { out += "(* "; out += l.substr(cut); out += " *)"; }
            continue;
        }
        const auto lead = leadingSpace(l);
        auto t = l.substr(lead);
        out += l.substr(0, lead);
        if (t.substr(0, 2) == "//") {
            t.remove_prefix(2);
            if (!t.empty() && t[0] == ' ') t.remove_prefix(1);
            out += t;
        } else if (t.size() >= 4 && t.substr(0, 2) == "(*" && t.substr(t.size() - 2) == "*)") {
            t = t.substr(2, t.size() - 4);
            if (!t.empty() && t.front() == ' ') t.remove_prefix(1);
            if (!t.empty() && t.back() == ' ') t.remove_suffix(1);
            out += t;
        } else {
            out += t;
        }
    }
    replaceSpan(from, to, out, from, from + out.size());
    if (span.first == span.last) {   // une ligne : le curseur reste ou il etait dans le texte
        anchor_ = caret_ = Caret{ static_cast<std::uint32_t>(span.first), static_cast<std::uint32_t>(lines_[span.first].end - lines_[span.first].begin) };
    }
    say(comment ? (slashes ? "Lignes comment\xC3\xA9" "es (//)" : "Lignes comment\xC3\xA9" "es ((* *))")
                : "Commentaire retir\xC3\xA9");
    return true;
}

bool MultiLineText::duplicate() {
    if (readOnly_ || lines_.empty()) return true;
    if (hasSelection()) {
        const auto [a, b] = orderedSelection();
        const auto from = offsetOf(a), to = offsetOf(b);
        const std::string piece = buffer_.substr(from, to - from);
        replaceSpan(to, to, piece, to, to + piece.size());
        say("S\xC3\xA9lection dupliqu\xC3\xA9" "e (Ctrl+D)");
        return true;
    }
    const auto& l = lines_[caret_.line];
    const std::string piece = buffer_.substr(l.begin, l.end - l.begin);
    const auto column = caret_.column;
    const auto at = l.end;
    replaceSpan(at, at, "\n" + piece, at, at);
    // le curseur sur la copie, a la meme colonne
    const auto copyLine = static_cast<std::uint32_t>(std::min<std::size_t>(caretFromOffset(at + 1).line, lines_.size() - 1));
    caret_ = anchor_ = Caret{ copyLine, std::min<std::uint32_t>(column, lines_[copyLine].end - lines_[copyLine].begin) };
    ensureCaretVisible();
    say("Ligne dupliqu\xC3\xA9" "e (Ctrl+D)");
    return true;
}

bool MultiLineText::moveLines(int dir) {
    if (readOnly_ || lines_.empty()) return true;
    const auto span = selectedLines();
    if (dir < 0 && span.first == 0) return true;
    if (dir > 0 && span.last + 1 >= lines_.size()) return true;
    const auto [a, b] = orderedSelection();
    const bool caretFirst = caret_.line < anchor_.line || (caret_.line == anchor_.line && caret_.column < anchor_.column);
    const auto aCol = a.column, bCol = b.column;
    const std::size_t from = lines_[dir < 0 ? span.first - 1 : span.first].begin;
    const std::size_t to = lines_[dir < 0 ? span.last : span.last + 1].end;
    const std::string block = buffer_.substr(lines_[span.first].begin, lines_[span.last].end - lines_[span.first].begin);
    const std::string other = dir < 0 ? buffer_.substr(lines_[span.first - 1].begin, lines_[span.first - 1].end - lines_[span.first - 1].begin)
                                      : buffer_.substr(lines_[span.last + 1].begin, lines_[span.last + 1].end - lines_[span.last + 1].begin);
    const std::string text = dir < 0 ? block + "\n" + other : other + "\n" + block;
    const auto newFirst = dir < 0 ? span.first - 1 : span.first + 1;
    const auto lineDelta = static_cast<std::uint32_t>(newFirst);
    const auto aLine = a.line - static_cast<std::uint32_t>(span.first) + lineDelta;
    const auto bLine = b.line - static_cast<std::uint32_t>(span.first) + lineDelta;
    beginStep();
    applyEdit(from, to, text);
    endStep();
    const Caret na{ aLine, aCol }, nb{ bLine, bCol };
    if (caretFirst) { caret_ = na; anchor_ = nb; } else { anchor_ = na; caret_ = nb; }
    ensureCaretVisible();
    invalidate();
    say(dir < 0 ? "Lignes mont\xC3\xA9" "es (Alt+\xE2\x86\x91)" : "Lignes descendues (Alt+\xE2\x86\x93)");
    return true;
}

bool MultiLineText::removeLines(bool toClipboard) {
    if (lines_.empty()) return true;
    const auto span = selectedLines();
    std::size_t from = lines_[span.first].begin;
    std::size_t to = lines_[span.last].end;
    std::string text = buffer_.substr(from, to - from) + "\n";
    if (toClipboard) { setClipboardText(text); lineClip_ = text; }
    if (readOnly_) {
        if (toClipboard) say("Ligne copi\xC3\xA9" "e (lecture seule : rien n'est coup\xC3\xA9)");
        return true;
    }
    if (to < buffer_.size()) ++to;                       // la fin de ligne part avec
    else if (from > 0) --from;                           // la derniere ligne : celle d'avant garde sa fin
    const auto column = caret_.column;
    replaceSpan(from, to, {}, from, from);
    const auto line = std::min<std::size_t>(span.first, lines_.size() - 1);
    caret_ = anchor_ = Caret{ static_cast<std::uint32_t>(line), std::min<std::uint32_t>(column, lines_[line].end - lines_[line].begin) };
    ensureCaretVisible();
    say(toClipboard ? "Ligne coup\xC3\xA9" "e dans le presse-papiers (Ctrl+L)" : "Ligne supprim\xC3\xA9" "e (Ctrl+Maj+L)");
    return true;
}

bool MultiLineText::copyOrCut(bool cut) {
    if (hasSelection()) {
        copySelection();
        lineClip_.clear();
        if (cut && !readOnly_) {
            beginStep();
            deleteSelection();
            endStep();
        }
        return true;
    }
    if (lines_.empty()) return true;
    if (cut) return removeLines(true);
    const auto& l = lines_[caret_.line];
    const std::string text = buffer_.substr(l.begin, l.end - l.begin) + "\n";
    setClipboardText(text);
    lineClip_ = text;
    say("Ligne copi\xC3\xA9" "e (rien de choisi : la ligne enti\xC3\xA8re)");
    return true;
}

// Coller : une ligne entiere copiee sans selection (Ctrl+C, Ctrl+X, Ctrl+L) se colle
// AU-DESSUS de la ligne du curseur, comme dans Visual Studio.
bool MultiLineText::pasteText() {
    if (readOnly_) return true;
    auto paste = clipboardText();
    std::erase(paste, '\r');
    if (paste.empty()) return true;
    if (!hasSelection() && !lineClip_.empty() && paste == lineClip_ && !lines_.empty()) {
        const auto at = lines_[caret_.line].begin;
        const auto column = caret_.column;
        const auto line = caret_.line;
        replaceSpan(at, at, paste, at, at);
        const auto next = static_cast<std::uint32_t>(std::min<std::size_t>(line + countBreaks(paste), lines_.size() - 1));
        caret_ = anchor_ = Caret{ next, std::min<std::uint32_t>(column, lines_[next].end - lines_[next].begin) };
        ensureCaretVisible();
        return true;
    }
    beginStep();
    insertText(paste);
    endStep();
    dismissCompletion();
    return true;
}

bool MultiLineText::openLine(bool above) {
    if (readOnly_ || lines_.empty()) return true;
    const auto& l = lines_[caret_.line];
    const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
    const std::string indent(text.substr(0, leadingSpace(text)));
    if (above) {
        const auto at = l.begin;
        replaceSpan(at, at, indent + "\n", at + indent.size(), at + indent.size());
    } else {
        const auto at = l.end;
        replaceSpan(at, at, "\n" + indent, at + 1 + indent.size(), at + 1 + indent.size());
    }
    return true;
}

bool MultiLineText::changeCase(bool upper) {
    if (readOnly_ || lines_.empty()) return true;
    std::size_t from, to;
    bool reselect = true;
    if (hasSelection()) {
        const auto [a, b] = orderedSelection();
        from = offsetOf(a);
        to = offsetOf(b);
    } else {
        std::tie(from, to) = wordAround(offsetOf(caret_));
        reselect = false;
        if (to <= from) return true;
    }
    const std::string piece = upper ? upperAscii(buffer_.substr(from, to - from)) : lowerAscii(buffer_.substr(from, to - from));
    const bool caretFirst = offsetOf(caret_) < offsetOf(anchor_);
    const auto keep = offsetOf(caret_);
    if (reselect) replaceSpan(from, to, piece, caretFirst ? to : from, caretFirst ? from : to);
    else replaceSpan(from, to, piece, keep, keep);
    say(upper ? "En MAJUSCULES (Ctrl+Maj+U)" : "En minuscules (Ctrl+U)");
    return true;
}

bool MultiLineText::indentLines(bool outdent) {
    if (readOnly_ || lines_.empty()) return true;
    const auto span = selectedLines();
    const auto from = lines_[span.first].begin;
    const auto to = lines_[span.last].end;
    const auto lines = splitLines(std::string_view(buffer_).substr(from, to - from));
    const auto unit = indentUnit();
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto l = lines[i];
        if (i) out += '\n';
        if (!outdent) {
            if (!blankLine(l) || lines.size() == 1) out += unit;
            out += l;
            continue;
        }
        if (!l.empty() && l[0] == '\t') l.remove_prefix(1);
        else {
            std::size_t k = 0;
            const std::size_t max = unit == "\t" ? 4 : unit.size();
            while (k < max && k < l.size() && l[k] == ' ') ++k;
            l.remove_prefix(k);
        }
        out += l;
    }
    if (out == std::string_view(buffer_).substr(from, to - from)) return true;
    // La selection garde les memes lignes, d'un bout a l'autre de leur texte ; sans
    // selection, le curseur suit son texte.
    if (!hasSelection()) {
        const auto lineBefore = lines_[span.first].end - lines_[span.first].begin;
        const auto column = caret_.column;
        replaceSpan(from, to, out, from, from);
        const auto lineAfter = lines_[span.first].end - lines_[span.first].begin;
        const auto col = static_cast<std::int64_t>(column) + static_cast<std::int64_t>(lineAfter) - static_cast<std::int64_t>(lineBefore);
        caret_ = anchor_ = Caret{ static_cast<std::uint32_t>(span.first), static_cast<std::uint32_t>(std::clamp<std::int64_t>(col, 0, lineAfter)) };
        invalidate();
        return true;
    }
    replaceSpan(from, to, out, from, from + out.size());
    return true;
}

// Mettre en forme : chaque ligne a la profondeur de ses blocs (IF, FOR, WHILE, REPEAT,
// CASE, TRY, VAR...) ; END_x, ELSIF, ELSE, CATCH, UNTIL un cran a gauche. Une ligne dans
// un commentaire (* *) sur plusieurs lignes ne bouge pas ; les lignes vides se vident.
bool MultiLineText::formatLines(bool selectionOnly) {
    if (readOnly_ || lines_.empty()) return true;
    const auto span = selectionOnly ? selectedLines() : LineSpan{ 0, lines_.size() - 1 };
    const auto unit = indentUnit();
    std::string out;
    int depth = 0;
    bool inComment = false;
    std::size_t changed = 0;
    for (std::size_t i = 0; i <= span.last; ++i) {
        const auto& l = lines_[i];
        const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
        const bool startedInComment = inComment;
        const auto words = lineWords(text, inComment);
        const auto lead = leadingSpace(text);
        int lineDepth = depth;
        if (!words.empty() && words.front().at == lead
            && (words.front().kind == Block::Close || words.front().kind == Block::Middle))
            lineDepth = std::max(0, depth - 1);
        for (const auto& w : words) {
            if (w.kind == Block::Open) ++depth;
            else if (w.kind == Block::Close) depth = std::max(0, depth - 1);
        }
        if (i < span.first) continue;
        std::string line;
        if (startedInComment) line = std::string(text);
        else if (blankLine(text)) line.clear();
        else {
            for (int d = 0; d < lineDepth; ++d) line += unit;
            line += text.substr(lead);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        }
        if (line != text) ++changed;
        if (i > span.first) out += '\n';
        out += line;
    }
    const auto from = lines_[span.first].begin;
    const auto to = lines_[span.last].end;
    if (changed == 0) { say("D\xC3\xA9j\xC3\xA0 en forme"); return true; }
    const auto caretLine = caret_.line;
    replaceSpan(from, to, out, from, from);
    const auto line = std::min<std::size_t>(caretLine, lines_.size() - 1);
    const std::string_view lt(buffer_.data() + lines_[line].begin, lines_[line].end - lines_[line].begin);
    caret_ = anchor_ = Caret{ static_cast<std::uint32_t>(line), static_cast<std::uint32_t>(leadingSpace(lt)) };
    if (selectionOnly) {
        anchor_ = Caret{ static_cast<std::uint32_t>(span.first), 0 };
        caret_ = Caret{ static_cast<std::uint32_t>(span.last), lines_[span.last].end - lines_[span.last].begin };
    }
    ensureCaretVisible();
    say(std::to_string(changed) + (changed > 1 ? " lignes remises en forme" : " ligne remise en forme")
        + (selectionOnly ? " (Ctrl+K, Ctrl+F)" : " (Ctrl+K, Ctrl+D)"));
    return true;
}

// Ctrl+] : d'une parenthese a l'autre ; d'un debut de bloc a sa fin et retour (IF, END_IF) ;
// d'un ELSIF, ELSE, CATCH, UNTIL a la fin de son bloc.
bool MultiLineText::matchBlock(bool select) {
    if (lines_.empty()) return true;
    const auto at = offsetOf(caret_);
    // ---- les parentheses et les crochets, a cote du curseur
    const auto bracket = [&](std::size_t i) { return i < buffer_.size() && std::string_view("()[]").find(buffer_[i]) != std::string_view::npos; };
    std::size_t b = std::string::npos;
    if (bracket(at)) b = at;
    else if (at > 0 && bracket(at - 1)) b = at - 1;
    if (b != std::string::npos) {
        const char open = buffer_[b] == '(' || buffer_[b] == ')' ? '(' : '[';
        const char close = open == '(' ? ')' : ']';
        const bool forward = buffer_[b] == open;
        int depth = 0;
        std::size_t target = std::string::npos;
        if (forward) {
            for (std::size_t i = b; i < buffer_.size(); ++i) {
                if (buffer_[i] == '\'') { const auto e = buffer_.find('\'', i + 1); if (e == std::string::npos) break; i = e; continue; }
                if (buffer_[i] == open) ++depth;
                else if (buffer_[i] == close && --depth == 0) { target = i; break; }
            }
        } else {
            for (std::size_t i = b + 1; i-- > 0;) {
                if (buffer_[i] == close) ++depth;
                else if (buffer_[i] == open && --depth == 0) { target = i; break; }
            }
        }
        if (target == std::string::npos) { say("Ctrl+] : pas de parenth\xC3\xA8se correspondante", Tone::Warning); return true; }
        pushBack();
        if (select) {
            anchor_ = caretFromOffset(std::min(b, target));
            caret_ = caretFromOffset(std::max(b, target) + 1);
        } else {
            caret_ = anchor_ = caretFromOffset(target + (forward ? 1 : 0));
        }
        ensureCaretVisible();
        invalidate();
        return true;
    }
    // ---- les mots des blocs, de tout le document (dans l'ordre)
    struct Mark { std::size_t offset; std::size_t length; Block kind; std::size_t line; };
    std::vector<Mark> marks;
    bool inComment = false;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const auto& l = lines_[i];
        const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
        for (const auto& w : lineWords(text, inComment))
            if (w.kind != Block::None) marks.push_back(Mark{ l.begin + w.at, w.up.size(), w.kind, i });
    }
    // Le mot sous le curseur, sinon le premier mot de bloc de sa ligne.
    std::size_t k = marks.size();
    for (std::size_t i = 0; i < marks.size(); ++i)
        if (at >= marks[i].offset && at <= marks[i].offset + marks[i].length) { k = i; break; }
    if (k == marks.size())
        for (std::size_t i = 0; i < marks.size(); ++i)
            if (marks[i].line == caret_.line) { k = i; break; }
    if (k == marks.size()) {
        say("Ctrl+] : place le curseur sur IF, FOR, CASE, VAR, TRY\xE2\x80\xA6 ou sur leur fin, ou pr\xC3\xA8s d'une parenth\xC3\xA8se");
        return true;
    }
    std::size_t target = marks.size();
    if (marks[k].kind == Block::Close) {
        int depth = 0;
        for (std::size_t i = k + 1; i-- > 0;) {
            if (marks[i].kind == Block::Close) ++depth;
            else if (marks[i].kind == Block::Open && --depth == 0) { target = i; break; }
        }
    } else {
        int depth = marks[k].kind == Block::Middle ? 1 : 0;
        for (std::size_t i = k; i < marks.size(); ++i) {
            if (marks[i].kind == Block::Open) ++depth;
            else if (marks[i].kind == Block::Close && --depth == 0) { target = i; break; }
        }
    }
    if (target == marks.size()) { say("Ctrl+] : ce bloc n'a pas de fin (ou de d\xC3\xA9" "but)", Tone::Warning); return true; }
    pushBack();
    const auto& t = marks[target];
    if (select) {
        const auto lo = std::min(marks[k].offset, t.offset);
        const auto hi = std::max(marks[k].offset + marks[k].length, t.offset + t.length);
        anchor_ = caretFromOffset(lo);
        caret_ = caretFromOffset(hi);
    } else {
        caret_ = anchor_ = caretFromOffset(t.offset);
    }
    ensureCaretVisible();
    invalidate();
    say((select ? "Bloc choisi : lignes " : "Bout du bloc : ligne ") + (select ? std::to_string(std::min(marks[k].line, t.line) + 1) + " \xC3\xA0 " + std::to_string(std::max(marks[k].line, t.line) + 1)
                                                                            : std::to_string(t.line + 1)));
    return true;
}

// ---- Entourer de (Ctrl+K, Ctrl+S), un extrait (Ctrl+K, Ctrl+X) : la liste de la completion
bool MultiLineText::showPicker(int kind) {
    if (readOnly_ || lines_.empty()) return true;
    completions_.clear();
    for (const auto& w : kWraps) {
        Completion c;
        c.text = std::string(w.label);
        c.detail = std::string(w.detail);
        c.icon = Icon::Code;
        completions_.push_back(std::move(c));
    }
    pickKind_ = kind;
    completionIndex_ = 0;
    completionStart_ = offsetOf(caret_);
    say(kind == 1 ? "Entourer de\xE2\x80\xA6 : \xE2\x86\x91 \xE2\x86\x93 puis Entr\xC3\xA9" "e (\xC3\x89" "chap : rien)"
                  : "Ins\xC3\xA9rer un extrait : \xE2\x86\x91 \xE2\x86\x93 puis Entr\xC3\xA9" "e (\xC3\x89" "chap : rien)");
    invalidate();
    return true;
}

void MultiLineText::applyPicked(std::size_t index) {
    const int kind = pickKind_;
    pickKind_ = 0;
    completions_.clear();
    if (index >= std::size(kWraps) || lines_.empty()) { invalidate(); return; }
    const auto& w = kWraps[index];
    const auto unit = indentUnit();
    const auto expand = [&](std::string_view s, const std::string& indent) {
        std::string out;
        for (const char c : s) {
            if (c == '\t') { out += unit; continue; }
            out += c;
            if (c == '\n') out += indent;
        }
        return out;
    };
    std::size_t from, to;
    std::string indent, body;
    if (kind == 1) {
        const auto span = selectedLines();
        from = lines_[span.first].begin;
        to = lines_[span.last].end;
        const auto lines = splitLines(std::string_view(buffer_).substr(from, to - from));
        std::size_t minIndent = std::string::npos;
        for (const auto l : lines)
            if (!blankLine(l)) minIndent = std::min(minIndent, leadingSpace(l));
        if (minIndent == std::string::npos) minIndent = 0;
        indent = std::string(lines.front().substr(0, std::min(minIndent, lines.front().size())));
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i) body += '\n';
            if (!blankLine(lines[i])) body += indent + unit + std::string(lines[i].substr(std::min(minIndent, lines[i].size())));
        }
    } else {
        const auto& l = lines_[caret_.line];
        const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
        indent = std::string(text.substr(0, leadingSpace(text)));
        from = to = offsetOf(caret_);
        if (!blankLine(text)) {           // au milieu d'une ligne : l'extrait sur la ligne d'apres
            from = to = l.end;
            body = indent + unit;
        } else {
            from = l.begin;
            to = l.end;
            body = indent + unit;
        }
    }
    std::string text = (kind == 2 && from == to && from == lines_[caret_.line].end) ? "\n" : "";
    const std::size_t headAt = text.size();
    text += indent + expand(w.head, indent) + "\n" + body;
    const std::size_t bodyEnd = text.size();
    if (!w.middle.empty()) text += "\n" + indent + expand(w.middle, indent) + "\n" + indent + unit;
    text += "\n" + indent + expand(w.tail, indent);
    // Le mot a remplacer (condition, i, valeur) choisi ; sinon le curseur dans le corps.
    std::size_t selA = from + bodyEnd, selB = selA;
    if (!w.place.empty()) {
        const auto p = text.find(w.place, headAt);
        if (p != std::string::npos) { selA = from + p; selB = selA + w.place.size(); }
    }
    replaceSpan(from, to, text, selA, selB);
    say(std::string(kind == 1 ? "Entour\xC3\xA9 de " : "Extrait : ") + std::string(w.label));
}

// ============================================================== les fautes ===
bool MultiLineText::stepSquiggle(bool backwards) {
    if (squiggles_.empty()) { say("Aucune faute dans ce document", Tone::Ok); return true; }
    std::vector<const Squiggle*> sorted;
    for (const auto& s : squiggles_) sorted.push_back(&s);
    std::sort(sorted.begin(), sorted.end(), [](const Squiggle* a, const Squiggle* b) {
        return a->line != b->line ? a->line < b->line : a->column < b->column;
    });
    const auto here = std::make_pair(static_cast<std::size_t>(caret_.line), anchor_.line == caret_.line ? std::min(anchor_.column, caret_.column) : caret_.column);
    const Squiggle* go = nullptr;
    if (!backwards) {
        for (const auto* s : sorted)
            if (std::make_pair(s->line, s->column) > here) { go = s; break; }
        if (!go) go = sorted.front();
    } else {
        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
            if (std::make_pair((*it)->line, (*it)->column) < here) { go = *it; break; }
        if (!go) go = sorted.back();
    }
    pushBack();
    selectRange(go->line, go->column, go->length);
    say((backwards ? "Maj+F8 \xC2\xB7 ligne " : "F8 \xC2\xB7 ligne ") + std::to_string(go->line + 1) + " : " + go->message
            + (go->fix.empty() ? std::string{} : std::string("  (Ctrl+. : ") + go->fix + ")"),
        go->tone == Tone::Error ? Tone::Error : Tone::Warning);
    return true;
}

bool MultiLineText::applyFix() {
    // La faute sous le curseur, sinon la premiere de sa ligne qui a une correction.
    const Squiggle* f = nullptr;
    for (const auto& s : squiggles_)
        if (s.line == caret_.line && !s.fix.empty() && caret_.column >= s.column && caret_.column <= s.column + s.length) { f = &s; break; }
    if (!f)
        for (const auto& s : squiggles_)
            if (s.line == caret_.line && !s.fix.empty()) { f = &s; break; }
    if (!f) {
        bool any = false;
        for (const auto& s : squiggles_) any = any || s.line == caret_.line;
        say(any ? "Ctrl+. : pas de correction toute faite pour cette faute" : "Ctrl+. : aucune faute sur cette ligne (F8 : la suivante)");
        return true;
    }
    if (readOnly_ || f->line >= lines_.size()) return true;
    const auto& l = lines_[f->line];
    const auto len = l.end - l.begin;
    const auto a = l.begin + std::min<std::uint32_t>(f->column, len);
    const auto b = l.begin + std::min<std::uint32_t>(f->column + f->length, len);
    const std::string was = buffer_.substr(a, b - a);
    const std::string fix = f->fix;
    // Elle est corrigee : on ne la montre plus (le volet recompte a la frappe suivante).
    const auto line = f->line;
    squiggles_.erase(squiggles_.begin() + (f - squiggles_.data()));
    replaceSpan(a, b, fix, a + fix.size(), a + fix.size());
    say("Corrig\xC3\xA9 ligne " + std::to_string(line + 1) + " : " + was + " \xE2\x86\x92 " + fix + " (Ctrl+.)", Tone::Ok);
    return true;
}

// ============================================================== les signets ===
bool MultiLineText::toggleBookmark() {
    if (lines_.empty()) return true;
    const std::size_t line = caret_.line;
    const auto it = std::lower_bound(bookmarks_.begin(), bookmarks_.end(), line);
    if (it != bookmarks_.end() && *it == line) {
        bookmarks_.erase(it);
        say("Signet retir\xC3\xA9 ligne " + std::to_string(line + 1));
    } else {
        bookmarks_.insert(it, line);
        say("Signet pos\xC3\xA9 ligne " + std::to_string(line + 1) + " (Ctrl+K, Ctrl+N : le suivant)");
    }
    invalidate();
    return true;
}

bool MultiLineText::stepBookmark(bool backwards) {
    if (bookmarks_.empty()) { say("Aucun signet (Ctrl+K, Ctrl+K en pose un)"); return true; }
    std::size_t target = 0;
    if (!backwards) {
        const auto it = std::upper_bound(bookmarks_.begin(), bookmarks_.end(), static_cast<std::size_t>(caret_.line));
        target = it == bookmarks_.end() ? bookmarks_.front() : *it;
    } else {
        const auto it = std::lower_bound(bookmarks_.begin(), bookmarks_.end(), static_cast<std::size_t>(caret_.line));
        target = it == bookmarks_.begin() ? bookmarks_.back() : *(it - 1);
    }
    pushBack();
    goToLine(std::min(target, lines_.size() - 1));
    say("Signet ligne " + std::to_string(target + 1));
    return true;
}

// ================================================================ les plis ===
bool MultiLineText::foldAtCaret() {
    if (lines_.empty()) return true;
    std::size_t header = caret_.line;
    if (!isFoldable(header)) {
        // le bloc qui contient le curseur : la tete la plus proche au-dessus qui le couvre
        bool found = false;
        for (std::size_t i = caret_.line; i-- > 0;)
            if (isFoldable(i) && lines_[i].foldEnd >= caret_.line) { header = i; found = true; break; }
        if (!found) { say("Ctrl+M, Ctrl+M : pas de bloc ici"); return true; }
    }
    toggleFold(header);
    if (folded_[header]) { caret_ = anchor_ = Caret{ static_cast<std::uint32_t>(header), caret_.line == header ? caret_.column : 0u }; }
    ensureCaretVisible();
    invalidate();
    return true;
}

// ======================================================= revenir, les infos ===
void MultiLineText::pushBack() {
    back_.push_back(caret_);
    if (back_.size() > 50) back_.erase(back_.begin());
}

bool MultiLineText::popBack() {
    if (back_.empty()) { say("Ctrl+- : rien o\xC3\xB9 revenir"); return true; }
    const auto c = back_.back();
    back_.pop_back();
    if (lines_.empty()) return true;
    const auto line = std::min<std::size_t>(c.line, lines_.size() - 1);
    goToLine(line);
    caret_.column = std::min<std::uint32_t>(c.column, lines_[line].end - lines_[line].begin);
    anchor_ = caret_;
    ensureCaretVisible();
    say("Revenu ligne " + std::to_string(line + 1) + " (Ctrl+-)");
    return true;
}

bool MultiLineText::quickInfo() {
    const auto name = symbolAtCaret();
    if (name.empty()) { say("Ctrl+K, Ctrl+I : le curseur n'est pas sur un nom"); return true; }
    if (signatureProvider_) {
        Signature s;
        if (signatureProvider_(name, s)) {
            std::string text = s.name + "(";
            for (std::size_t i = 0; i < s.parameters.size(); ++i) text += (i ? ", " : "") + s.parameters[i];
            text += ")";
            if (!s.returns.empty()) text += " : " + s.returns;
            say(text, Tone::Info);
            return true;
        }
    }
    if (valueProvider_) {
        std::string value;
        if (valueProvider_(name, value)) { say(name + " : " + value, Tone::Info); return true; }
    }
    say(name + " : la barre du nom (sous l'\xC3\xA9" "diteur) dit ce qu'il est ; F1 : sa fiche", Tone::Info);
    return true;
}

bool MultiLineText::parameterInfo() {
    updateSignature();
    if (!signatureActive_) say("Ctrl+Maj+Espace : place le curseur dans les parenth\xC3\xA8ses d'un appel");
    invalidate();
    return true;
}

// ======================================================= la barre (Ctrl+F) ===
void MultiLineText::openBar(BarMode mode) {
    if (mode == BarMode::None) { closeBar(); return; }
    if (mode == BarMode::Replace && readOnly_) mode = BarMode::Find;
    barMode_ = mode;
    barFocus_ = true;
    barField_ = 0;
    barSelectAll_ = true;
    dismissCompletion();
    if (mode == BarMode::GoToLine) {
        gotoText_ = std::to_string(caret_.line + 1);
        say("Aller \xC3\xA0 la ligne (1 - " + std::to_string(lines_.size()) + ") : tape son num\xC3\xA9ro, Entr\xC3\xA9" "e");
    } else {
        // La selection d'une ligne devient le texte cherche (comme Visual Studio).
        if (hasSelection()) {
            const auto t = selectedText();
            if (!t.empty() && t.find('\n') == std::string::npos) findText_ = t;
        } else {
            const auto [a, b] = wordAround(offsetOf(caret_));
            if (b > a && findText_.empty()) findText_ = buffer_.substr(a, b - a);
        }
        const auto n = matchCount();
        say(mode == BarMode::Replace ? "Remplacer : Entr\xC3\xA9" "e la suivante, Alt+R remplacer, Alt+A tout, Tab passe d'un champ \xC3\xA0 l'autre"
                                     : "Rechercher : Entr\xC3\xA9" "e ou F3 la suivante, Maj+F3 la pr\xC3\xA9" "c\xC3\xA9" "dente, \xC3\x89" "chap ferme"
                                           + (findText_.empty() ? std::string{} : " \xC2\xB7 " + std::to_string(n) + " trouv\xC3\xA9" "e(s)"));
    }
    invalidate();
}

void MultiLineText::closeBar() {
    if (barMode_ == BarMode::None) return;
    barMode_ = BarMode::None;
    barFocus_ = false;
    invalidate();
}

void MultiLineText::setFindText(std::string text) {
    findText_ = std::move(text);
    invalidate();
}
void MultiLineText::setReplaceText(std::string text) {
    replaceText_ = std::move(text);
    invalidate();
}
void MultiLineText::setFindOptions(bool matchCase, bool wholeWord) {
    matchCase_ = matchCase;
    wholeWord_ = wholeWord;
    invalidate();
}

std::vector<std::pair<std::size_t, std::size_t>> MultiLineText::matches() const {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    if (findText_.empty() || findText_.size() > buffer_.size()) return out;
    const auto fold = [&](char c) {
        return matchCase_ ? c : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    };
    const std::size_t n = findText_.size();
    for (std::size_t i = 0; i + n <= buffer_.size(); ++i) {
        bool same = true;
        for (std::size_t k = 0; k < n && same; ++k) same = fold(buffer_[i + k]) == fold(findText_[k]);
        if (!same) continue;
        if (wholeWord_ && ((i > 0 && identChar(buffer_[i - 1])) || (i + n < buffer_.size() && identChar(buffer_[i + n])))) continue;
        out.emplace_back(i, i + n);
        i += n - 1;
    }
    return out;
}

std::size_t MultiLineText::matchCount() const { return matches().size(); }

bool MultiLineText::findNext(bool backwards) {
    if (findText_.empty()) { openBar(BarMode::Find); return true; }
    const auto all = matches();
    if (all.empty()) { say("\xC2\xAB " + findText_ + " \xC2\xBB introuvable", Tone::Warning); return true; }
    const auto [a, b] = orderedSelection();
    const auto from = offsetOf(a), to = offsetOf(b);
    std::size_t k = 0;
    if (!backwards) {
        k = all.size();
        for (std::size_t i = 0; i < all.size(); ++i)
            if (all[i].first >= to && !(all[i].first == from && all[i].second == to)) { k = i; break; }
        if (k == all.size()) k = 0;                      // en boucle : la premiere
    } else {
        k = all.size();
        for (std::size_t i = all.size(); i-- > 0;)
            if (all[i].first < from) { k = i; break; }
        if (k == all.size()) k = all.size() - 1;
    }
    anchor_ = caretFromOffset(all[k].first);
    caret_ = caretFromOffset(all[k].second);
    // une trouvaille dans un pli : on le deplie
    bool hidden = true;
    for (const auto v : visible_) if (v == caret_.line) { hidden = false; break; }
    if (hidden) unfoldAll();
    ensureCaretVisible();
    invalidate();
    say("\xC2\xAB " + findText_ + " \xC2\xBB : " + std::to_string(k + 1) + " sur " + std::to_string(all.size())
        + " (F3 la suivante, Maj+F3 la pr\xC3\xA9" "c\xC3\xA9" "dente)");
    return true;
}

bool MultiLineText::replaceCurrent() {
    if (readOnly_ || findText_.empty()) return true;
    const auto [a, b] = orderedSelection();
    const auto from = offsetOf(a), to = offsetOf(b);
    for (const auto& m : matches())
        if (m.first == from && m.second == to) {
            replaceSpan(from, to, replaceText_, from + replaceText_.size(), from + replaceText_.size());
            break;
        }
    return findNext(false);
}

std::size_t MultiLineText::replaceAll() {
    if (readOnly_ || findText_.empty()) return 0;
    const auto all = matches();
    if (all.empty()) { say("\xC2\xAB " + findText_ + " \xC2\xBB introuvable", Tone::Warning); return 0; }
    std::string out;
    std::size_t last = 0;
    for (const auto& m : all) {
        out.append(buffer_, last, m.first - last);
        out += replaceText_;
        last = m.second;
    }
    out.append(buffer_, last, std::string::npos);
    const auto keep = offsetOf(caret_);
    replaceSpan(0, buffer_.size(), out, std::min(keep, out.size()), std::min(keep, out.size()));
    say(std::to_string(all.size()) + " remplacement(s) : \xC2\xAB " + findText_ + " \xC2\xBB \xE2\x86\x92 \xC2\xAB " + replaceText_ + " \xC2\xBB (Ctrl+Z les d\xC3\xA9" "fait)",
        Tone::Ok);
    return all.size();
}

bool MultiLineText::gotoLineFromBar() {
    std::size_t n = 0;
    bool any = false;
    for (const char c : gotoText_) {
        if (c < '0' || c > '9') continue;
        n = n * 10 + static_cast<std::size_t>(c - '0');
        any = true;
    }
    if (!any || n == 0) { say("Aller \xC3\xA0 la ligne : un num\xC3\xA9ro de 1 \xC3\xA0 " + std::to_string(lines_.size()), Tone::Warning); return true; }
    pushBack();
    const auto line = std::min(n, lines_.size()) - 1;
    closeBar();
    goToLine(line);
    // la ligne au tiers de la hauteur, pas collee en haut
    say("Ligne " + std::to_string(line + 1) + " (Ctrl+- : revenir)");
    return true;
}

bool MultiLineText::handleBarText(const TextInput& t) {
    if (!barHasKeyboard()) return false;
    std::string& field = barMode_ == BarMode::GoToLine ? gotoText_ : (barField_ == 1 ? replaceText_ : findText_);
    if (barSelectAll_) { field.clear(); barSelectAll_ = false; }
    field += t.utf8;
    if (barMode_ != BarMode::GoToLine && barField_ == 0) {
        // Chercher en tapant (comme Visual Studio) : la premiere apres le debut de la selection.
        const auto [a, b] = orderedSelection();
        caret_ = anchor_ = a;
        (void)b;
        if (matchCount() > 0) findNext(false);
        else say("\xC2\xAB " + findText_ + " \xC2\xBB introuvable", Tone::Warning);
    }
    invalidate();
    return true;
}

bool MultiLineText::handleBarKey(const KeyDown& k) {
    if (!barHasKeyboard()) return false;
    std::string& field = barMode_ == BarMode::GoToLine ? gotoText_ : (barField_ == 1 ? replaceText_ : findText_);
    switch (k.key) {
        case Key::Escape:
            closeBar();
            say({});
            return true;
        case Key::Return:
            if (barMode_ == BarMode::GoToLine) return gotoLineFromBar();
            if (barField_ == 1 && !k.mods.shift) { replaceCurrent(); return true; }
            findNext(k.mods.shift);
            barSelectAll_ = false;
            return true;
        case Key::F3:
            findNext(k.mods.shift);
            return true;
        case Key::Tab:
            if (barMode_ == BarMode::Replace) { barField_ = 1 - barField_; barSelectAll_ = true; invalidate(); }
            return true;
        case Key::Backspace:
            if (barSelectAll_) { field.clear(); barSelectAll_ = false; }
            else if (k.mods.ctrl) field.clear();
            else if (!field.empty()) {
                std::size_t n = field.size() - 1;
                while (n > 0 && isContinuation(field[n])) --n;
                field.erase(n);
            }
            invalidate();
            return true;
        case Key::Delete:
            if (barSelectAll_) { field.clear(); barSelectAll_ = false; invalidate(); }
            return true;
        case Key::Left: case Key::Right: case Key::Home: case Key::End:
            barSelectAll_ = false;
            invalidate();
            return true;
        case Key::Up: case Key::Down:
            if (barMode_ != BarMode::GoToLine) findNext(k.key == Key::Up);
            return true;
        default: break;
    }
    if (k.mods.alt && !k.mods.ctrl) {
        if (k.key == Key::R && barMode_ == BarMode::Replace) { replaceCurrent(); return true; }
        if (k.key == Key::A && barMode_ == BarMode::Replace) { replaceAll(); return true; }
        if (k.key == Key::C) { matchCase_ = !matchCase_; say(matchCase_ ? "Respecter la casse : oui (Alt+C)" : "Respecter la casse : non (Alt+C)"); invalidate(); return true; }
        if (k.key == Key::W) { wholeWord_ = !wholeWord_; say(wholeWord_ ? "Mot entier : oui (Alt+W)" : "Mot entier : non (Alt+W)"); invalidate(); return true; }
    }
    if (k.mods.ctrl && !k.mods.alt) {
        if (k.key == Key::A) { barSelectAll_ = true; invalidate(); return true; }
        if (k.key == Key::V) {
            auto paste = clipboardText();
            std::erase(paste, '\r');
            const auto nl = paste.find('\n');
            if (nl != std::string::npos) paste.erase(nl);
            if (barSelectAll_) { field.clear(); barSelectAll_ = false; }
            field += paste;
            invalidate();
            return true;
        }
        if (k.key == Key::F) { openBar(BarMode::Find); return true; }
        if (k.key == Key::H) { openBar(BarMode::Replace); return true; }
        if (k.key == Key::G) { openBar(BarMode::GoToLine); return true; }
    }
    // Les autres touches (Ctrl+Z, Ctrl+S...) suivent leur chemin.
    return false;
}

bool MultiLineText::handleBarMouse(const MouseDown& d) {
    if (barMode_ == BarMode::None || !barBox_.contains(d.pos)) return false;
    grabFocus();
    if (barClose_.contains(d.pos)) { closeBar(); return true; }
    if (barPrev_.contains(d.pos)) { findNext(true); return true; }
    if (barNext_.contains(d.pos)) { if (barMode_ == BarMode::GoToLine) gotoLineFromBar(); else findNext(false); return true; }
    if (barCase_.contains(d.pos)) { matchCase_ = !matchCase_; invalidate(); return true; }
    if (barWord_.contains(d.pos)) { wholeWord_ = !wholeWord_; invalidate(); return true; }
    if (barOne_.contains(d.pos)) { replaceCurrent(); return true; }
    if (barAll_.contains(d.pos)) { replaceAll(); return true; }
    barFocus_ = true;
    barSelectAll_ = false;
    if (barField1_.contains(d.pos)) barField_ = 1;
    else if (barField0_.contains(d.pos)) barField_ = 0;
    invalidate();
    return true;
}

void MultiLineText::drawBar(const PaintContext& ctx) const {
    if (barMode_ == BarMode::None) { barBox_ = {}; return; }
    const auto& c = ctx.theme.color;
    const auto f = ctx.theme.font.ui;
    const auto sf = ctx.theme.font.smallUi;
    const auto r = bounds();
    const float lh = ctx.r.lineHeight(f);
    const float pad = 5.f, fieldW = 190.f, btn = lh + 4.f;
    const bool replace = barMode_ == BarMode::Replace;
    const bool gotoLine = barMode_ == BarMode::GoToLine;
    const std::string gotoLabel = "Ligne (1 - " + std::to_string(std::max<std::size_t>(1, lines_.size())) + ")";
    const float labelW = gotoLine ? ctx.r.measure(gotoLabel, sf).width + 8.f : 0.f;
    const float rowW = labelW + fieldW + (gotoLine ? btn * 2 : btn * 5 + 52.f) + pad * 3;
    const float rows = replace ? 2.f : 1.f;
    const float boxW = std::min(rowW + (replace ? 0.f : 0.f), std::max(160.f, r.w - 30.f));
    const float boxH = rows * (btn + pad) + pad;
    barBox_ = { r.right() - boxW - 16.f, r.y + 4.f, boxW, boxH };
    ctx.r.fillRoundedRect({ barBox_.x + 1.f, barBox_.y + 2.f, barBox_.w, barBox_.h }, gfx::Color{ 0, 0, 0, 50 }, 5.f);
    ctx.r.fillRoundedRect(barBox_, c.panelBg, 5.f);
    ctx.r.strokeRect(barBox_, c.border, 1.f);

    float x = barBox_.x + pad, y = barBox_.y + pad;
    const auto drawField = [&](gfx::Rect box, const std::string& text, std::string_view placeholder, bool active) {
        ctx.r.fillRect(box, c.inputBg);
        ctx.r.strokeRect(box, active && barFocus_ ? c.accent : c.border, 1.f);
        const float ty = box.y + (box.h - lh) * 0.5f;
        ctx.r.pushClip({ box.x + 3.f, box.y, box.w - 6.f, box.h });
        if (text.empty()) ctx.r.drawText({ box.x + 5.f, ty }, placeholder, f, c.textDisabled);
        else {
            const float tw = ctx.r.measure(text, f).width;
            const float shift = std::max(0.f, tw - (box.w - 14.f));
            if (active && barFocus_ && barSelectAll_) ctx.r.fillRect({ box.x + 5.f - shift, ty, tw, lh }, c.selectionBg);
            ctx.r.drawText({ box.x + 5.f - shift, ty }, text, f, c.text);
            if (active && barFocus_ && !barSelectAll_ && std::fmod(ctx.time, 1.0) < 0.5)
                ctx.r.fillRect({ box.x + 5.f - shift + tw + 1.f, ty + 1.f, 2.f, lh - 2.f }, c.text);
        }
        ctx.r.popClip();
    };
    const auto drawBtn = [&](gfx::Rect box, std::string_view label, bool on, bool enabled) {
        if (on) ctx.r.fillRoundedRect(box, c.accent.withAlpha(70), 3.f);
        else if (box.contains(barMouse_)) ctx.r.fillRoundedRect(box, c.headerBg, 3.f);
        const auto m = ctx.r.measure(label, sf);
        ctx.r.drawText({ box.x + (box.w - m.width) * 0.5f, box.y + (box.h - m.height) * 0.5f }, label, sf, enabled ? c.text : c.textDisabled);
    };
    if (gotoLine) {
        ctx.r.drawText({ x, y + (btn - ctx.r.lineHeight(sf)) * 0.5f }, gotoLabel, sf, c.textMuted);
        x += labelW;
        barField0_ = { x, y, fieldW, btn };
        drawField(barField0_, gotoText_, "num\xC3\xA9ro", true);
        x += fieldW + pad;
        barNext_ = { x, y, btn, btn };
        drawBtn(barNext_, "\xE2\x86\xB5", false, true);
        x += btn;
        barClose_ = { x, y, btn, btn };
        drawBtn(barClose_, "\xE2\x9C\x95", false, true);
        barPrev_ = barCase_ = barWord_ = barField1_ = barOne_ = barAll_ = {};
        return;
    }
    barField0_ = { x, y, fieldW, btn };
    drawField(barField0_, findText_, "Rechercher", barField_ == 0);
    x += fieldW + pad;
    const auto n = matchCount();
    const std::string count = findText_.empty() ? std::string{} : (n ? std::to_string(n) : std::string("aucune"));
    ctx.r.drawText({ x, y + (btn - ctx.r.lineHeight(sf)) * 0.5f }, count, sf, n || findText_.empty() ? c.textMuted : c.error);
    x += 48.f;
    barPrev_ = { x, y, btn, btn }; drawBtn(barPrev_, "\xE2\x86\x91", false, n > 0); x += btn;
    barNext_ = { x, y, btn, btn }; drawBtn(barNext_, "\xE2\x86\x93", false, n > 0); x += btn;
    barCase_ = { x, y, btn, btn }; drawBtn(barCase_, "Aa", matchCase_, true); x += btn;
    barWord_ = { x, y, btn, btn }; drawBtn(barWord_, "ab|", wholeWord_, true); x += btn;
    barClose_ = { x, y, btn, btn }; drawBtn(barClose_, "\xE2\x9C\x95", false, true);
    if (replace) {
        x = barBox_.x + pad;
        y += btn + pad;
        barField1_ = { x, y, fieldW, btn };
        drawField(barField1_, replaceText_, "Remplacer par", barField_ == 1);
        x += fieldW + pad;
        const float wOne = ctx.r.measure("Remplacer", sf).width + 12.f;
        barOne_ = { x, y, wOne, btn }; drawBtn(barOne_, "Remplacer", false, n > 0); x += wOne + 2.f;
        const float wAll = ctx.r.measure("Tout", sf).width + 14.f;
        barAll_ = { x, y, wAll, btn }; drawBtn(barAll_, "Tout", false, n > 0);
    } else {
        barField1_ = barOne_ = barAll_ = {};
    }
}

void MultiLineText::drawMatches(const PaintContext& ctx, std::size_t line, float y, float x0, gfx::FontId f) const {
    if (barMode_ == BarMode::None || barMode_ == BarMode::GoToLine || findText_.empty() || line >= lines_.size()) return;
    const auto& l = lines_[line];
    const std::string_view text(buffer_.data() + l.begin, l.end - l.begin);
    const float lh = paintLineHeight_;
    // Les trouvailles de cette ligne seulement (le texte entier se cherche une fois par
    // trouvaille dans le dessin des lignes montrees : peu de lignes, de petits textes).
    const auto n = findText_.size();
    const auto fold = [&](char ch) { return matchCase_ ? ch : static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch); };
    for (std::size_t i = 0; i + n <= text.size(); ++i) {
        bool same = true;
        for (std::size_t k = 0; k < n && same; ++k) same = fold(text[i + k]) == fold(findText_[k]);
        if (!same) continue;
        if (wholeWord_ && ((i > 0 && identChar(text[i - 1])) || (i + n < text.size() && identChar(text[i + n])))) continue;
        const float xa = ctx.r.measure(text.substr(0, i), f).width;
        const float xb = ctx.r.measure(text.substr(0, i + n), f).width;
        ctx.r.fillRect({ x0 + xa, y + 1.f, xb - xa, lh - 2.f }, ctx.theme.color.warning.withAlpha(70));
        i += n - 1;
    }
}

// ========================================================= suivre les lignes ===
// Une modification qui retire `removed` fins de ligne apres la ligne `fromLine` et en
// ajoute `added` : les signets plus bas suivent leur texte ; ceux des lignes retirees
// tombent sur la ligne de la modification.
void MultiLineText::shiftMarks(std::size_t fromLine, std::size_t removed, std::size_t added) {
    if (bookmarks_.empty() || removed == added) return;
    for (auto& b : bookmarks_) {
        if (b <= fromLine) continue;
        if (b <= fromLine + removed) b = fromLine;
        else b = b + added - removed;
    }
    std::sort(bookmarks_.begin(), bookmarks_.end());
    bookmarks_.erase(std::unique(bookmarks_.begin(), bookmarks_.end()), bookmarks_.end());
}

void MultiLineText::reloadText(std::string t) {
    if (t == buffer_) return;
    // Le premier caractere qui differe : le curseur y va (la ou l'annulation a joue).
    std::size_t same = 0;
    const auto n = std::min(t.size(), buffer_.size());
    while (same < n && t[same] == buffer_[same]) ++same;
    std::size_t tailOld = buffer_.size(), tailNew = t.size();
    while (tailOld > same && tailNew > same && buffer_[tailOld - 1] == t[tailNew - 1]) { --tailOld; --tailNew; }
    const auto first = firstVisible_;
    const float sx = scrollX_;
    const auto fromLine = caretFromOffset(same).line;
    const auto removed = countBreaks(std::string_view(buffer_).substr(same, tailOld - same));
    const auto added = countBreaks(std::string_view(t).substr(same, tailNew - same));
    std::vector<std::size_t> folds;
    for (std::size_t i = 0; i < folded_.size(); ++i)
        if (folded_[i]) folds.push_back(i);
    buffer_ = std::move(t);
    rebuildLines();
    firstVisible_ = std::min(first, visible_.empty() ? std::size_t{ 0 } : visible_.size() - 1);
    scrollX_ = sx;
    for (auto line : folds) {
        if (line > fromLine + removed) line = line + added - removed;
        else if (line > fromLine) continue;
        if (line < folded_.size() && isFoldable(line)) folded_[line] = true;
    }
    rebuildVisible();
    shiftMarks(fromLine, removed, added);
    caret_ = anchor_ = caretFromOffset(tailNew);
    firstVisible_ = std::min(first, visible_.empty() ? std::size_t{ 0 } : visible_.size() - 1);
    ensureCaretVisible();
    invalidate();
}

}   // namespace ui
