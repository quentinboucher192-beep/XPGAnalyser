// =============================================================================
//  ui/widgets/Controls.hpp — leaf input widgets
// =============================================================================
#pragma once

#include "../../core/Command.hpp"
#include "../Icons.hpp"
#include "../Syntax.hpp"
#include "../Widget.hpp"
#include "KindBadge.hpp"   // 1.11.2 (API-V, decision 161) : la pastille d'une proposition

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // ---------------------------------------------------------------- Button ---
    class Button : public Widget {
    public:
        enum class Style : std::uint8_t { Default, Primary, Flat, Danger };

        explicit Button(std::string text, std::string id = {});

        void setText(std::string t);
        void setIcon(Icon icon) { icon_ = icon; invalidateLayout(); }
        // Icon only, label carried by the tooltip. Used by ToolBar when the labels
        // no longer fit; meaningless without an icon, so it is ignored then.
        void setCompact(bool compact);
        [[nodiscard]] bool compact() const noexcept { return compact_ && icon_ != Icon::None; }
        void setStyle(Style s) { style_ = s; invalidate(); }
        void setAction(core::ActionId a) { action_ = a; }   // toolbar buttons bind to the action table
        [[nodiscard]] const std::string& text() const noexcept { return text_; }
        [[nodiscard]] core::ActionId action() const noexcept { return action_; }
        [[nodiscard]] SizeHint sizeHint() const override;

        const core::SignalPtr<> clicked = core::Signal<>::create();

    protected:
        void        onPaint(const PaintContext&) override;
        EventResult onEvent(const InputEvent&) override;

    private:
        std::string                  text_;
        Icon                         icon_{ Icon::None };
        bool                         compact_{ false };
        Style                        style_{ Style::Default };
        core::ActionId               action_{};
        bool                         pressed_{ false };
    };

    // ------------------------------------------------------------- InputText ---
    class InputText : public Widget {
    public:
        explicit InputText(std::string id = {});

        void setText(std::string t);
        void setPlaceholder(std::string p) { placeholder_ = std::move(p); invalidate(); }
        void setReadOnly(bool ro) { readOnly_ = ro; }
        void setMaxLength(std::size_t n) { maxLength_ = n; }
        // Rejects keystrokes that would make the text invalid (used by the address
        // filter box: only %M, %MW, %I, %Q, %S… patterns).
        void setValidator(std::function<bool(std::string_view)> v) { validator_ = std::move(v); }
        // Un mot de passe : chaque caractere est montre comme un point, et le
        // texte ne part pas dans le presse-papiers (Ctrl+C, Ctrl+X).
        void setMasked(bool m) { masked_ = m; invalidate(); }
        [[nodiscard]] bool masked() const noexcept { return masked_; }
        [[nodiscard]] const std::string& text() const noexcept { return text_; }
        [[nodiscard]] SizeHint sizeHint() const override;

        // L'AIDE A LA SAISIE D'UN CHAMP : une liste sous le champ, comme dans
        // l'editeur de code. Le fournisseur recoit le texte jusqu'au curseur,
        // remplit `out` et dit dans `from` ou commence ce qu'un choix remplace
        // (un octet de `before`). Fleches, Entree ou Tab pour choisir, Echap
        // pour fermer la liste ; Ctrl+Espace l'ouvre sans rien taper.
        struct Suggestion {
            std::string text;
            std::string detail;
            Icon        icon{ Icon::None };
            std::string insert;        // vide : `text`
            bool        chain{ false };  // rouvrir la liste apres (un chemin a.b.)
            // 1.11.2 (API-V, decision 161) : la nature et la provenance, tracees
            // devant le nom a la place de `icon` ; sa legende au pied de la liste.
            KindBadge   badge{};
        };
        using Assist = std::function<void(std::string_view before, std::size_t& from, std::vector<Suggestion>& out)>;
        void setAssist(Assist a) { assist_ = std::move(a); }
        [[nodiscard]] bool hasAssist() const noexcept { return static_cast<bool>(assist_); }
        [[nodiscard]] bool suggestionsOpen() const noexcept { return !suggestions_.empty(); }
        [[nodiscard]] const std::vector<Suggestion>& suggestions() const noexcept { return suggestions_; }
        [[nodiscard]] std::size_t suggestionIndex() const noexcept { return suggestionIndex_; }
        void openSuggestions();                      // Ctrl+Espace
        void closeSuggestions();
        bool acceptSuggestion(std::size_t index);
        // La liste est dessinee au-dessus de tout et doit recevoir le clic.
        [[nodiscard]] bool      requestsOverlayPass() const override { return !suggestions_.empty(); }
        [[nodiscard]] gfx::Rect eventBounds() const override;

        const core::SignalPtr<const std::string&> textChanged = core::Signal<const std::string&>::create();
        const core::SignalPtr<const std::string&> editingDone = core::Signal<const std::string&>::create();

        // ---- Lot API 8 : finitions (Ctrl+F) ----
        //  Le curseur dans le champ, tout son texte choisi (Widget::grabFocus
        //  est protege) : Ctrl+F dans un volet. Faux : le champ ne le prend pas
        //  (cache, grise).
        bool focusAndSelectAll();
        //  Un champ de recherche : Echap l'efface d'abord (le curseur y reste) ;
        //  vide, Echap le quitte, comme les autres.
        void setEscapeClears(bool on) noexcept { escapeClears_ = on; }
        [[nodiscard]] bool escapeClears() const noexcept { return escapeClears_; }
        // ---- fin Lot API 8 : finitions ----

    protected:
        void        onPaint(const PaintContext&) override;
        void        onPaintOverlay(const PaintContext&) override;
        EventResult onEvent(const InputEvent&) override;
        void        onFocusChanged(bool) override;

    private:
        void updateSuggestions(bool forced);
        void insert(std::string_view utf8);
        void eraseSelection();
        void copySelection() const;
        [[nodiscard]] std::size_t caretFromLocalX(float localX) const;
        // Ce qui est dessine des `upto` premiers octets : le texte, ou un point
        // par caractere quand le champ est masque.
        [[nodiscard]] std::string shown(std::size_t upto) const;

        std::string  text_, placeholder_;
        // Lot 19 : Ctrl+Z dans un champ en cours de saisie revient au texte
        // qu'il avait en recevant le focus (ou au dernier Entree) ; sinon
        // Ctrl+Z passe a l'historique du projet.
        std::string  undoText_;
        std::size_t  caret_{ 0 }, selAnchor_{ 0 };
        float        scrollX_{ 0.f };
        std::size_t  maxLength_{ 4096 };
        bool         readOnly_{ false };
        bool         masked_{ false };
        bool         escapeClears_{ false };     // lot API 8 : finitions (Echap efface une recherche)
        std::function<bool(std::string_view)> validator_;
        Assist                  assist_;
        std::vector<Suggestion> suggestions_;
        std::size_t             suggestionIndex_{ 0 };
        std::size_t             suggestionFrom_{ 0 };
        std::size_t             suggestionFirst_{ 0 };     // la premiere ligne montree
        mutable gfx::Rect       suggestionBox_{};
        mutable float           suggestionLegendH_{ 0.f };  // 1.11.2 : le pied de la liste (la legende)
    };

    // -------------------------------------------------------- MultiLineText ---
    //  The source view, and now the source editor.
    //
    //  EDITING MODEL. One std::string holds the text; a line index and the folding
    //  structure are rebuilt after each change. That is O(size) per keystroke, which
    //  sounds wrong until you measure it: rebuilding the index for the largest
    //  section in the reference project - 400 kB, 10 000 lines - is well under a
    //  millisecond, because it is a single linear scan with no allocation per line.
    //  A piece table would be faster and would also mean every other part of this
    //  class stops being able to say `buffer_.substr(...)`. Not worth it until a
    //  measurement says so.
    //
    //  COMPLETION. The widget knows nothing about PLCs. It collects the identifier
    //  being typed and asks a provider, which the screen fills from the project's
    //  symbol table. The list is filtered, ranked and drawn here; what goes in it is
    //  someone else's business.
    class MultiLineText : public Widget {
    public:
        struct Completion {
            std::string text;      // what is shown, and what is inserted by default
            std::string detail;    // type, address, or what kind of thing it is
            Icon        icon{ Icon::None };
            int         rank{ 0 };   // lower sorts first; a prefix match beats a substring

            // A snippet: what actually gets inserted when it differs from the label,
            // with newlines expanded to the current indentation. `caret` is a byte
            // offset into `insert` and says where the cursor lands afterwards -
            // typing IF should leave you between IF and THEN, not after END_IF.
            std::string insert;
            std::size_t caret{ std::string::npos };
            // Ce que le choix remplace AVANT le mot en cours : le debut d'un nom
            // qui n'est pas un identifiant ('alarme.wav', 'Instantan\xC3\xA9e') et
            // que le mot en cours (lettres, chiffres, _) ne couvre pas en entier.
            std::size_t extend{ 0 };
            // Rouvrir la liste apres l'insertion : IHM_NAVIGUER('|') laisse le
            // curseur entre les guillemets, la ou l'on choisit la vue.
            bool        chain{ false };
            // 1.11.2 (API-V, decision 161) : what it is and where it comes from,
            // drawn in front of the name instead of `icon`; its legend at the foot.
            KindBadge   badge{};
        };

        // What a call looks like, shown while the arguments are being typed.
        struct Signature {
            std::string              name;
            std::vector<std::string> parameters;   // "IN  start : BOOL"
            std::string              returns;
        };
        using SignatureProvider = std::function<bool(std::string_view name, Signature& out)>;
        using CompletionProvider =
            std::function<void(std::string_view prefix, std::vector<Completion>& out)>;

        explicit MultiLineText(std::string id = {});

        void setText(std::string t);
        void setReadOnly(bool ro) { readOnly_ = ro; invalidate(); }
        [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }

        // ---- editing ---------------------------------------------------------
        // Focus is normally taken by a click; a container that reveals an editor
        // needs to hand it over without one.
        void takeFocus() { grabFocus(); }

        void insertText(std::string_view utf8);
        void deleteSelection();
        void deleteBefore();                    // Backspace
        void deleteAfter();                     // Delete
        void insertNewline();                   // keeps the current indentation
        // What the Tab key inserts. A real tab by default, because that is what the
        // reference project uses: 3880 of its indented lines are tabs against 706
        // with spaces.
        void setTabInsertsSpaces(int count) noexcept { tabSpaces_ = count; }
        [[nodiscard]] bool modified() const noexcept { return modified_; }
        void clearModified() noexcept { modified_ = false; }

        const core::SignalPtr<const std::string&> textChanged =
            core::Signal<const std::string&>::create();

        // ---- completion ------------------------------------------------------
        void setCompletionProvider(CompletionProvider provider);
        void setSignatureProvider(SignatureProvider provider);

        // While a simulation runs, hovering a symbol should say what it holds right
        // now. The widget finds the identifier under the pointer - it already does
        // that for completion - and asks; what a value means is not its business.
        using ValueProvider = std::function<bool(std::string_view symbol, std::string& text)>;
        void setValueProvider(ValueProvider provider);
        [[nodiscard]] std::string symbolAt(gfx::Point global) const;
        // Lot 7 : la valeur du nom survole est redemandee tant que l'infobulle
        // est ouverte - elle suit la simulation au lieu de rester figee.
        [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

        // The identifier the reader is looking at: the selection when it is a single
        // name or dotted path, otherwise the word the caret sits in. Selection wins
        // because double-clicking a name is how people ask "what is this?", and it
        // is what the screenshot showed someone doing.
        [[nodiscard]] std::string symbolAtCaret() const;
        // Emitted when that changes, and only then - a status bar rebuilt on every
        // keystroke flickers and costs a project-wide lookup per character.
        const core::SignalPtr<const std::string&> caretSymbolChanged =
            core::Signal<const std::string&>::create();
        // Double-clicking a name asks to go to it. The editor does not know what
        // "go to" means for any given symbol, so it says which name was opened and
        // lets the screen decide - a chart for a GRAFCET instance, nothing at all
        // for a plain BOOL.
        const core::SignalPtr<const std::string&> symbolActivated =
            core::Signal<const std::string&>::create();
        [[nodiscard]] bool signatureOpen() const noexcept { return signatureActive_; }
        [[nodiscard]] const Signature& signature() const noexcept { return signature_; }
        [[nodiscard]] std::size_t activeParameter() const noexcept { return activeParameter_; }
        void requestCompletion();               // Ctrl+Space
        void dismissCompletion();
        [[nodiscard]] bool completionOpen() const noexcept { return !completions_.empty(); }
        [[nodiscard]] std::size_t completionCount() const noexcept { return completions_.size(); }
        [[nodiscard]] std::string completionPrefix() const;
        void acceptCompletion();
        // Le curseur, en octets depuis le debut du texte : un fournisseur de
        // completion y lit ce qui precede le mot en cours (une chaine, un
        // commentaire, un chemin a.b.).
        [[nodiscard]] std::size_t caretOffset() const { return offsetOf(caret_); }
        // Ouvrir la liste des qu'un point suit un nom (les membres d'une
        // structure). Desactive par defaut : l'editeur des sections ne le fait pas.
        void setCompleteAfterDot(bool on) noexcept { completeAfterDot_ = on; }
        void setWordWrap(bool w) { wordWrap_ = w; invalidateLayout(); }
        void setShowLineNumbers(bool s) { lineNumbers_ = s; invalidateLayout(); }
        void setLanguage(Language l);
        [[nodiscard]] Language language() const noexcept { return language_; }
        void scrollToLine(std::size_t line);
        [[nodiscard]] std::size_t firstVisibleLine() const noexcept { return firstVisible_; }
        // Aller a une ligne (0 = la premiere) : le curseur a son debut, la ligne
        // en vue avec un peu de contexte au-dessus. C'est ce que fait "aller a la
        // source" depuis un rapport de compilation.
        void goToLine(std::size_t line);
        // Des lignes marquees (une erreur, un avertissement) : un fond teinte et un
        // repere dans la marge. Vide : aucune.
        void setMarkedLines(std::vector<std::pair<std::size_t, gfx::Color>> lines);
        // ---- Lot API 8 : les points d'arret dans la marge, la ligne d'arret ----
        //  setBreakpointGutter(true) : une colonne de plus, a gauche des numeros.
        //  Un clic dedans (ou F9, le curseur sur la ligne, l'editeur ayant le
        //  focus) DEMANDE de poser ou d'enlever un point d'arret sur cette ligne
        //  (breakpointToggled) ; Maj+clic (ou Ctrl+F9) : l'activer ou le
        //  desactiver (breakpointEnableToggled). L'editeur ne decide rien : il
        //  montre ce qu'on lui donne - des ronds rouges (setBreakpoints ; un
        //  point desactive : un anneau ; a condition : un point blanc au milieu),
        //  la ligne ou la simulation est en pause (setExecutionLine : une fleche
        //  dans la marge, la ligne surlignee ; npos : aucune), un texte a droite
        //  de certaines lignes (setLineNotes : les valeurs au passage). Les
        //  lignes : 0 = la premiere. Sans la colonne (le defaut), rien ne change.
        struct BreakMark {
            std::size_t line{ 0 };
            bool        enabled{ true };
            bool        conditional{ false };
        };
        struct LineNote {
            std::size_t line{ 0 };
            std::string text;
            Tone        tone{ Tone::None };     // None : le gris des commentaires
        };
        void setBreakpointGutter(bool on);
        [[nodiscard]] bool breakpointGutter() const noexcept { return breakGutter_; }
        void setBreakpoints(std::vector<BreakMark> marks);
        [[nodiscard]] const std::vector<BreakMark>& breakpoints() const noexcept { return breaks_; }
        void setExecutionLine(std::size_t line);
        [[nodiscard]] std::size_t executionLine() const noexcept { return execLine_; }
        void setLineNotes(std::vector<LineNote> notes);
        [[nodiscard]] const std::vector<LineNote>& lineNotes() const noexcept { return notes_; }
        // ---- 1.10 : les fautes soulignees ----
        //  Une faute a sa place : un trait ondule sous ses caracteres (rouge :
        //  erreur, orange : avertissement), le numero de sa ligne sur fond
        //  teinte, son message dans l'infobulle quand la souris passe dessus.
        //  L'editeur ne juge rien : il montre ce qu'on lui donne. Ligne : 0 = la
        //  premiere ; colonne : en octets depuis le debut de la ligne.
        struct Squiggle {
            std::size_t   line{ 0 };
            std::uint32_t column{ 0 };
            std::uint32_t length{ 1 };
            Tone          tone{ Tone::Error };
            std::string   message;
        };
        void setSquiggles(std::vector<Squiggle> squiggles);
        [[nodiscard]] const std::vector<Squiggle>& squiggles() const noexcept { return squiggles_; }
        // La faute sous ce point (coordonnees de l'ecran, apres un dessin) ; nullptr : aucune.
        [[nodiscard]] const Squiggle* squiggleAt(gfx::Point global) const;
        // Le milieu du soulignement de la faute `index` a l'ecran ; faux : pas en vue.
        [[nodiscard]] bool squigglePoint(std::size_t index, gfx::Point& out) const;
        // Aller a une faute : sa ligne en vue, ses caracteres selectionnes.
        void selectRange(std::size_t line, std::uint32_t column, std::uint32_t length);
        // ---- fin 1.10 ----
        [[nodiscard]] std::size_t caretLine() const noexcept { return caret_.line; }
        // Ou cliquer dans la marge pour cette ligne (coordonnees de l'ecran,
        // apres un dessin) ; faux : pas de colonne, ou la ligne n'est pas montree.
        [[nodiscard]] bool breakpointMarginPoint(std::size_t line, gfx::Point& out) const;
        // 1.11 (T1) : le rectangle du caractere (ligne, colonne en octets) a l'ecran,
        // d'apres le dernier dessin (sa ligne, sa hauteur, la largeur mesuree du debut
        // de ligne) ; apres la fin de la ligne, une demi-ligne de large. Faux : la
        // ligne n'est pas montree. Lecture seule : les cibles des tutoriels.
        [[nodiscard]] bool positionRect(std::size_t line, std::uint32_t column, gfx::Rect& out) const;
        // 1.11 (T1) : le rectangle de la ligne de la liste de completion qui montre `text`
        // (le meme texte, sans tenir compte de la casse ; sinon la premiere qui commence par
        // lui), d'apres le dernier dessin ; `text` vide : toute la liste. Faux : liste fermee,
        // pas encore dessinee, ou rien ne correspond. Lecture seule : les cibles des tutoriels.
        [[nodiscard]] bool completionRect(std::string_view text, gfx::Rect& out) const;
        [[nodiscard]] bool hasTooltip() const override;
        const core::SignalPtr<std::size_t> breakpointToggled = core::Signal<std::size_t>::create();
        const core::SignalPtr<std::size_t> breakpointEnableToggled = core::Signal<std::size_t>::create();
        // Lot API 8 (2e partie) : un clic droit dans la colonne (sur un point ou
        // sur une ligne sans point) DEMANDE la condition de cette ligne.
        const core::SignalPtr<std::size_t> breakpointConditionRequested = core::Signal<std::size_t>::create();
        // ---- fin Lot API 8 ----
        // The completion list is drawn above everything and must receive the click.
        [[nodiscard]] bool      requestsOverlayPass() const override {
            return completionOpen() || signatureActive_;
        }
        [[nodiscard]] gfx::Rect eventBounds() const override;

        // Font scale for this view only, so one section can be enlarged without
        // changing the whole application's theme. Ctrl+wheel drives it.
        void setZoom(float zoom);
        [[nodiscard]] float zoom() const noexcept { return zoom_; }

        void selectAll();                       // Ctrl+A
        void copySelection() const;             // Ctrl+C; whole buffer when nothing is selected
        [[nodiscard]] bool hasSelection() const noexcept;
        void clearSelection();
        [[nodiscard]] std::string selectedText() const;

        // ---- folding -------------------------------------------------------
        // Regions come from indentation, not from a grammar. That is deliberate:
        // indentation is the one structural signal every language in scope shares,
        // it survives code that does not parse, and it needs no per-language block
        // table. A line is a fold header when the next non-blank line is indented
        // further; the region ends at the last line still indented past it.
        void toggleFold(std::size_t line);
        void foldAll();
        void unfoldAll();
        void foldToDepth(int depth);            // 0 = everything folded, -1 = show all
        [[nodiscard]] int  maxFoldDepth() const noexcept { return maxDepth_; }
        [[nodiscard]] bool isFoldable(std::size_t line) const;
        [[nodiscard]] bool isFolded(std::size_t line) const;

        // Read-only seams the caret tests need. The geometry ones report what the
        // last paint actually used, which is the whole point of the fix they pin.
        [[nodiscard]] std::uint32_t caretLineForTest() const noexcept { return caret_.line; }
        [[nodiscard]] std::uint32_t caretColumnForTest() const noexcept { return caret_.column; }
        [[nodiscard]] float         gutterForTest() const noexcept { return paintGutter_; }
        // Ou le dernier dessin a pose le curseur, en pixels depuis le debut du
        // texte (hors marge) : la largeur mesuree du debut de la ligne.
        [[nodiscard]] float         caretXForTest() const noexcept { return paintCaretX_; }
        [[nodiscard]] gfx::Rect     contentRectForTest() const noexcept { return contentRect(); }
        [[nodiscard]] std::string text() const;
        [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }
        [[nodiscard]] SizeHint sizeHint() const override;

    protected:
        void        onPaint(const PaintContext&) override;        // visible lines only
        void        onPaintOverlay(const PaintContext&) override;  // the completion list
        EventResult onEvent(const InputEvent&) override;

    private:
        // `inBlockComment` records whether a block comment is open when the line
        // starts. Storing it per line is what lets the painter begin tokenising at
        // the first *visible* line instead of rescanning the file on every scroll.
        // `indent` and `foldEnd` carry the folding structure, computed once.
        struct Line {
            std::uint32_t begin{ 0 }, end{ 0 };
            bool          inBlockComment{ false };
            std::uint16_t indent{ 0 };        // leading columns, tabs expanded
            std::uint16_t depth{ 0 };         // nesting level, for fold-to-depth
            std::uint32_t foldEnd{ 0 };       // last line of the region; == index when not foldable
            bool          blank{ false };
        };
        struct Caret { std::uint32_t line{ 0 }; std::uint32_t column{ 0 }; };
        EventResult handleEvent(const InputEvent&);   // the body; onEvent wraps it
        [[nodiscard]] std::string symbolAtCaretPosition(Caret) const;
        void        notifyCaretSymbol();
        std::string lastCaretSymbol_;

        void rebuildLines();
        void rebuildVisible();
        void applyEdit(std::size_t from, std::size_t to, std::string_view replacement);
        [[nodiscard]] std::size_t offsetOf(Caret) const;
        [[nodiscard]] Caret       caretFromOffset(std::size_t offset) const;
        void updateCompletion();
        void updateSignature();
        void drawCompletion(const PaintContext&) const;
        void drawSignature(const PaintContext&) const;
        [[nodiscard]] gfx::FontId scaledFont(gfx::FontId base) const;
        [[nodiscard]] std::size_t lineAt(float globalY) const;          // model line
        [[nodiscard]] Caret       caretAt(gfx::Point global) const;
        // Une colonne (un octet de la ligne) et sa position a l'ecran. La face
        // n'est PAS a chasse fixe : le renderer ne charge qu'une police
        // proportionnelle, pour `mono` comme pour `ui`. "colonne x largeur de M"
        // posait donc le curseur trop loin apres chaque espace, et au milieu d'un
        // caractere accentue. On mesure le debut de ligne, comme InputText.
        [[nodiscard]] float         columnX(std::size_t line, std::uint32_t column) const;
        [[nodiscard]] std::uint32_t columnAtX(std::size_t line, float x) const;   // frontiere la plus proche
        [[nodiscard]] float       gutterWidth(const gfx::IRenderer&, gfx::FontId) const;
        [[nodiscard]] std::pair<Caret, Caret> orderedSelection() const;
        [[nodiscard]] std::size_t caretRow() const;   // caret line as a visible row
        void ensureCaretVisible();                    // scrolls only when it has to

        std::string                buffer_;
        std::vector<Line>          lines_;
        mutable std::vector<Token> tokens_;      // scratch, one line at a time
        // Geometry the hit tests need but cannot recompute safely: the painter is
        // the only place that knows the final font and gutter width, so it records
        // them. The previous version recomputed a *different* gutter in caretAt and
        // the caret landed one line-number-column too far to the right.
        mutable float              paintGutter_{ 18.f };
        mutable gfx::FontId        paintFont_{ 16 };     // la fonte (zoom compris) du dernier dessin
        mutable float              paintCaretX_{ 0.f };
        // Le clignotement repart a chaque frappe, clic ou deplacement : le
        // curseur est TOUJOURS visible juste apres avoir bouge, sinon on ne sait
        // pas ou il est tombe une fois sur deux.
        double                     blinkOrigin_{ 0.0 };
        bool                       blinkRestart_{ true };
        bool                       completeAfterDot_{ false };
        mutable float              paintLineHeight_{ 16.f };
        mutable float              paintNumbers_{ 0.f };

        std::vector<std::uint32_t> visible_;      // line indices currently on screen
        std::vector<bool>          folded_;
        std::vector<std::pair<std::size_t, gfx::Color>> marked_;
        std::size_t                firstVisible_{ 0 };   // index into visible_
        float                      scrollX_{ 0.f };
        float                      zoom_{ 1.f };
        int                        maxDepth_{ 0 };
        Caret                      caret_{}, anchor_{};
        bool                       selecting_{ false };
        bool                       modified_{ false };
        int                        tabSpaces_{ 0 };     // 0 = insert a real tab

        ValueProvider              valueProvider_;
        std::string                hoverSymbol_;
        SignatureProvider          signatureProvider_;
        Signature                  signature_;
        std::size_t                activeParameter_{ 0 };
        bool                       signatureActive_{ false };

        CompletionProvider         completionProvider_;
        std::vector<Completion>    completions_;
        std::size_t                completionIndex_{ 0 };
        std::size_t                completionStart_{ 0 };   // buffer offset of the prefix
        mutable gfx::Rect          completionBox_{};
        mutable float              completionLegendH_{ 0.f };   // 1.11.2 : the legend at the foot of the list
        Language                   language_{ Language::PlainText };
        bool                       readOnly_{ true }, wordWrap_{ false }, lineNumbers_{ true };
        // ---- Lot API 8 : la marge des points d'arret ----
        [[nodiscard]] bool inBreakColumn(gfx::Point global) const;
        bool                       breakGutter_{ false };
        std::vector<BreakMark>     breaks_;
        std::size_t                execLine_{ std::string::npos };
        std::vector<LineNote>      notes_;
        std::vector<Squiggle>      squiggles_;                         // 1.10 : les fautes soulignees
        std::size_t                breakHover_{ std::string::npos };   // la ligne survolee dans la marge
        mutable float              paintBreak_{ 0.f };                 // la largeur de la colonne au dernier dessin
        // ---- fin Lot API 8 ----
    };

    // -------------------------------------------------------------- Checkbox ---
    class Checkbox : public Widget {
    public:
        enum class State : std::uint8_t { Unchecked, Checked, Mixed };
        explicit Checkbox(std::string label, std::string id = {});

        void setState(State s);
        void setTristate(bool t) { tristate_ = t; }
        [[nodiscard]] State state() const noexcept { return state_; }
        [[nodiscard]] bool  isChecked() const noexcept { return state_ == State::Checked; }
        [[nodiscard]] const std::string& label() const noexcept { return label_; }
        // 1.8.0 : le libelle suit ce qu'il dit (le comparateur : "Rapprocher A <-> B").
        void setLabel(std::string label) {
            if (label == label_) return;
            label_ = std::move(label);
            invalidateLayout();
        }
        [[nodiscard]] SizeHint sizeHint() const override;

        const core::SignalPtr<State> stateChanged = core::Signal<State>::create();

    protected:
        void        onPaint(const PaintContext&) override;
        EventResult onEvent(const InputEvent&) override;
    private:
        std::string label_;
        State       state_{ State::Unchecked };
        bool        tristate_{ false };
    };

    // ----------------------------------------------------------- RadioButton ---
    // Group membership is by shared_ptr to a RadioGroup, so exclusivity survives
    // widgets being re-parented or destroyed.
    class RadioGroup;

    class RadioButton : public Widget {
    public:
        RadioButton(std::string label, std::shared_ptr<RadioGroup> group, int value, std::string id = {});
        ~RadioButton() override;
        [[nodiscard]] bool selected() const noexcept;
        void select();
        // Lot 17 : son libelle (les scripts le designent par lui).
        [[nodiscard]] const std::string& label() const noexcept { return label_; }
    protected:
        void        onPaint(const PaintContext&) override;
        EventResult onEvent(const InputEvent&) override;
    private:
        std::string                 label_;
        std::shared_ptr<RadioGroup> group_;
        int                         value_;
    };

    class RadioGroup {
    public:
        void setValue(int v);
        [[nodiscard]] int value() const noexcept { return value_; }
        const core::SignalPtr<int> valueChanged = core::Signal<int>::create();
    private:
        friend class RadioButton;
        void attach(RadioButton*);
        void detach(RadioButton*);
        int                       value_{ -1 };
        std::vector<RadioButton*> members_;   // non-owning: buttons are owned by the tree
    };

    // ---------------------------------------------------------- ToggleButton ---
    class ToggleButton : public Widget {
    public:
        explicit ToggleButton(std::string text, std::string id = {});
        void setChecked(bool c);
        [[nodiscard]] bool checked() const noexcept { return checked_; }
        [[nodiscard]] const std::string& text() const noexcept { return text_; }   // lot 19 : les scripts le cliquent
        [[nodiscard]] SizeHint sizeHint() const override;
        const core::SignalPtr<bool> toggled = core::Signal<bool>::create();
    protected:
        void        onPaint(const PaintContext&) override;
        EventResult onEvent(const InputEvent&) override;
    private:
        std::string text_;
        bool        checked_{ false };
    };

    // -------------------------------------------------------------- DropDown ---
    class DropDown : public Widget {
    public:
        struct Item { std::string label; std::string value; gfx::TextureId icon{}; bool enabled{ true }; };

        explicit DropDown(std::string id = {});
        void setItems(std::vector<Item> items);
        void setSelectedIndex(int i);
        void setEditable(bool e) { editable_ = e; }          // combo box behaviour
        [[nodiscard]] int selectedIndex() const noexcept { return selected_; }
        [[nodiscard]] const Item* selectedItem() const;
        [[nodiscard]] const std::vector<Item>& items() const noexcept { return items_; }
        [[nodiscard]] SizeHint sizeHint() const override;
        [[nodiscard]] bool      requestsOverlayPass() const override { return popupOpen_; }
        [[nodiscard]] gfx::Rect eventBounds() const override;   // the surface, while open

        // How many rows the list shows before it starts scrolling. Twelve is enough
        // to scan without becoming a wall; a project with sixty derived types has to
        // scroll whatever the number is.
        void setMaxVisibleRows(int rows);

        // Exposed for tests: a list that runs off the bottom of the screen is a
        // defect that only appears on a smaller window than the one it was built on.
        [[nodiscard]] gfx::Rect popupRect() const;
        [[nodiscard]] bool      isOpen() const noexcept { return popupOpen_; }

        const core::SignalPtr<int> selectionChanged = core::Signal<int>::create();

    protected:
        void        onPaint(const PaintContext&) override;
        void        onPaintOverlay(const PaintContext&) override;   // the popup list
        EventResult onEvent(const InputEvent&) override;
    private:
        // The popup is not a child widget. It is drawn in the top-most pass so it
        // can extend past the parent's clip rectangle.
        void openPopup();
        void closePopup();
        [[nodiscard]] int   visibleRows() const;
        [[nodiscard]] float maxScroll() const;
        [[nodiscard]] int   rowAt(gfx::Point global) const;
        void                scrollTo(int row);
        void                moveHighlight(int delta);
        [[nodiscard]] gfx::Rect scrollbarRect() const;

        std::vector<Item> items_;
        int               selected_{ -1 };
        int               highlighted_{ -1 };      // where the keyboard or pointer is
        int               maxVisibleRows_{ 12 };
        float             scroll_{ 0.f };          // pixels, 0 .. maxScroll()
        float             rowHeight_{ 24.f };      // cached from the theme at paint time
        bool              editable_{ false }, popupOpen_{ false };
        mutable bool      flipped_{ false };       // the list sits above, not below
    };

    // ------------------------------------------------------------- PopupMenu ---
    // ---------------------------------------------------------------------------
    //  The right-click menu. Built on the same mechanism as DropDown's list - not a
    //  child widget, painted in the top-most pass, dispatched through
    //  findOverlayOwner() - because that mechanism already solves "drawn outside the
    //  parent's clip rectangle" and solving it twice would mean two behaviours.
    //
    //  Three things it does that DropDown does not:
    //
    //   * IT OPENS AT A POINT, not under a widget, and clamps itself to the surface.
    //     A menu opened near the bottom edge flips above the cursor rather than
    //     hanging off the screen where its last entries cannot be reached.
    //
    //   * IT GRABS THE POINTER WHILE OPEN. eventBounds() covers the whole surface,
    //     so a click anywhere reaches onEvent() and dismisses the menu. Without
    //     that, Widget::dispatch rejects the click before the menu sees it and the
    //     menu stays open over a window that is already reacting to the click.
    //     (DropDown has exactly this defect; see the note in Controls.cpp.)
    //
    //   * IT SAYS WHY AN ENTRY IS GREYED. "Delete" that is simply dead tells you
    //     nothing. disabledReason is shown next to the entry, so "Delete - still
    //     used by Gaz_1.Step" answers the question instead of raising it.
    // ---------------------------------------------------------------------------
    class PopupMenu : public Widget {
    public:
        struct Item {
            std::string label;
            std::string shortcut;          // right-aligned hint, e.g. "Del"
            std::string disabledReason;    // shown when enabled == false
            Icon        icon{ Icon::None };
            bool        enabled{ true };
            bool        separator{ false };  // a rule; label and the rest are ignored
            int         id{ -1 };            // the caller's own tag, echoed back
            // Lot API 6 : une ligne de texte en tete du menu (ce que l'on modifie,
            // depuis quand) - ni survolee, ni choisie ; `shortcut` s'y lit a droite.
            bool        heading{ false };
            // Lot 7 : UN SOUS-MENU. Non vide : l'entree porte une fleche a droite,
            // et la survoler (ou la cliquer, ou fleche droite, ou Entree) ouvre
            // ces entrees a cote d'elle. Elle-meme ne se choisit pas : itemChosen
            // rend l'id de l'entree choisie dans le sous-menu. Un seul niveau (les
            // enfants d'un enfant sont ignores).
            std::vector<Item> children{};
            // 1.11.3 : une icone dessinee par l'appelant (le carre de legende d'une
            // case) a la place de `icon`, dans la meme gouttiere.
            std::function<void(const PaintContext&, gfx::Rect)> paintIcon{};
        };

        explicit PopupMenu(std::string id = {});

        void setItems(std::vector<Item> items);
        // `surface` is the window size, used to keep the menu on screen.
        void openAt(gfx::Point where, gfx::Size surface);
        void close();
        [[nodiscard]] bool isOpen() const noexcept { return open_; }

        // Lot 7 : le sous-menu ouvert - le rang de son entree (-1 : aucun). Pour
        // les scripts et les tests : l'ouvrir par programme (faux : pas une entree
        // a sous-menu, ou grisee), son cadre, et ou est dessinee chacune de ses
        // entrees (comme itemRect).
        [[nodiscard]] int openedSubmenu() const noexcept { return submenu_; }
        bool openSubmenu(std::size_t index);
        [[nodiscard]] gfx::Rect submenuRect() const;
        [[nodiscard]] gfx::Rect subItemRect(std::size_t index) const;

        [[nodiscard]] bool      requestsOverlayPass() const override { return open_; }
        [[nodiscard]] gfx::Rect eventBounds() const override;
        [[nodiscard]] SizeHint  sizeHint() const override;

        // Geometry, exposed for tests: a menu that runs off the screen is a defect
        // that only shows up on one screen size, so it is asserted rather than
        // eyeballed.
        [[nodiscard]] gfx::Rect popupRect() const;
        // The entries and where each one is drawn: the scripts click an entry by
        // its label, like a pointer would (lot 18).
        [[nodiscard]] const std::vector<Item>& items() const noexcept { return items_; }
        [[nodiscard]] gfx::Rect itemRect(std::size_t index) const;

        // Carries the Item::id, not the row: inserting a separator must not
        // renumber the actions the caller connected.
        const core::SignalPtr<int> itemChosen = core::Signal<int>::create();
        const core::SignalPtr<>    dismissed = core::Signal<>::create();

    protected:
        void        onPaintOverlay(const PaintContext&) override;
        EventResult onEvent(const InputEvent&) override;

    private:
        [[nodiscard]] int  itemAt(gfx::Point global) const;
        void               moveHighlight(int delta);
        [[nodiscard]] float itemHeight(std::size_t i) const;
        // Lot 7 : le sous-menu.
        [[nodiscard]] int  subItemAt(gfx::Point global) const;
        void               moveSubHighlight(int delta);
        void               closeSubmenu();
        [[nodiscard]] bool hasSubmenu(int index) const noexcept;

        std::vector<Item> items_;
        gfx::Point        anchor_{};
        gfx::Size         surface_{};
        int               highlighted_{ -1 };
        bool              open_{ false };
        mutable float     rowHeight_{ 24.f };   // cached from the theme at paint time
        int               submenu_{ -1 };        // lot 7 : l'entree dont le sous-menu est ouvert
        int               subHighlighted_{ -1 }; // l'entree survolee du sous-menu
        bool              subKeyboard_{ false }; // les fleches haut / bas vont au sous-menu
    };

} // namespace ui