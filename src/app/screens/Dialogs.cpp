// =============================================================================
//  app/screens/Dialogs.cpp — modal and dialog layers
// =============================================================================
#include "Screens.hpp"

#include "../../core/FieldCodec.hpp"

#include "../App.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

namespace app {

    using namespace ui;

    namespace {

        menu::MenuTraits dialogTraits() {
            menu::MenuTraits t;
            t.kind = menu::MenuKind::Dialog;
            t.rendersBelow = true;      // the workspace stays visible behind the scrim
            t.updatesBelow = false;
            t.blocksInput = true;
            t.dimsBelow = true;
            return t;
        }

        // A one-line caption. Small enough that duplicating it here is cheaper than a
        // shared header for two classes.
        class Label final : public Widget {
        public:
            explicit Label(std::string text, bool bold = false)
                : text_(std::move(text)), bold_(bold) {}
            [[nodiscard]] SizeHint sizeHint() const override {
                const float line = lineHeight(gfx::FontId{ 16 });
                return SizeHint{ {measureWidth(text_, gfx::FontId{16}) + 8.f, line + 6.f},
                                {40.f, line + 4.f}, 1.f, 0.f };
            }
        protected:
            void onPaint(const PaintContext& ctx) override {
                const auto r = contentRect();
                const auto f = bold_ ? ctx.theme.font.uiBold : ctx.theme.font.ui;
                ctx.r.drawText({ r.x, r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f }, text_, f,
                    ctx.theme.color.text);
            }
        private:
            std::string text_;
            bool        bold_;
        };

        // An invisible elastic gap. Putting one before the button row right-aligns it,
        // which is where a confirm button belongs.
        class Spacer final : public Widget {
        public:
            [[nodiscard]] SizeHint sizeHint() const override {
                SizeHint h;
                h.stretchX = h.stretchY = 1.f;
                return h;
            }
        };

        // Centres a panel in the window and paints the frame; dialogs put their
        // controls inside it.
        class DialogFrame final : public Widget {
        public:
            DialogFrame(std::string title, float w, float h)
                : title_(std::move(title)), width_(w), height_(h) {}

            Widget& body() { return *body_; }

            void attachBody(WidgetPtr b) { body_ = &addChild(std::move(b)); }

        protected:
            void onLayout() override {
                const auto r = bounds();
                // Lot API 8 : dans une fenetre plus petite que lui (un onglet
                // detache), le cadre s'y tient, avec une marge ; sinon sa taille.
                const float w = std::min(width_, std::max(240.f, r.w - 24.f));
                const float h = std::min(height_, std::max(120.f, r.h - 24.f));
                panel_ = { std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h };
                if (body_) body_->setBounds({ panel_.x + 14.f, panel_.y + 40.f,
                                             panel_.w - 28.f, panel_.h - 54.f });
            }
            void onPaint(const PaintContext& ctx) override {
                const auto& c = ctx.theme.color;
                // No scrim here: MenuManager::Render already dims the layer below for
                // any menu whose traits set dimsBelow. Painting a second one made the
                // screen behind twice as dark as intended and the text underneath
                // unreadable.
                ctx.r.fillRect(panel_, c.panelBg);
                ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
                const float titleH = ctx.theme.metric.headerHeight;
                ctx.r.fillRect({ panel_.x, panel_.y, panel_.w, titleH }, c.headerBg);
                ctx.r.drawText({ panel_.x + 12.f,
                                panel_.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f },
                    title_, ctx.theme.font.uiBold, c.text);
            }
        private:
            std::string title_;
            float       width_, height_;
            gfx::Rect   panel_{};
            Widget* body_{ nullptr };
        };

        class ProgressBar final : public Widget {
        public:
            void set(float f, std::string stage) {
                fraction_ = std::clamp(f, 0.f, 1.f);
                stage_ = std::move(stage);
                invalidate();
            }
        protected:
            void onPaint(const PaintContext& ctx) override {
                const auto& c = ctx.theme.color;
                const auto  r = contentRect();
                const gfx::Rect track{ r.x, r.y + r.h * 0.5f - 5.f, r.w, 10.f };
                ctx.r.fillRect(track, c.inputBg);
                ctx.r.strokeRect(track, c.border, 1.f);
                ctx.r.fillRect({ track.x + 1.f, track.y + 1.f,
                                (track.w - 2.f) * fraction_, track.h - 2.f }, c.accent);
                ctx.r.drawText({ r.x, r.y }, stage_, ctx.theme.font.smallUi, c.textMuted);
            }
        private:
            float       fraction_{ 0.f };
            std::string stage_{ "Starting..." };
        };

    } // namespace

    // ============================================================= FormDialog ====
    FormDialog::FormDialog(std::string id, std::string title, std::string explanation,
        std::vector<Field> fields, std::string confirmLabel, bool cancellable)
        : menu::WidgetMenu(std::move(id)),
        title_(std::move(title)),
        explanation_(std::move(explanation)),
        confirmLabel_(std::move(confirmLabel)),
        fields_(std::move(fields)),
        cancellable_(cancellable) {}

    menu::MenuTraits FormDialog::traits() const {
        auto t = dialogTraits();
        // A dialog the user cannot escape from - the master key on first run - must
        // not be closable with Escape either, or the setting would be skipped.
        t.closableWithEscape = cancellable_;
        t.updatesBelow = updatesBelow_;
        return t;
    }

    std::vector<std::string> FormDialog::split(const std::string& payload) {
        // See core/FieldCodec.hpp for why this is not a newline any more.
        return core::splitFields(payload);
    }


    void FormDialog::setRules(Rules rules) {
        rules_ = std::move(rules);
        applyRules();
    }

    std::vector<std::string> FormDialog::currentValues() const {
        std::vector<std::string> out;
        out.reserve(inputs_.size());
        for (auto* w : inputs_) {
            if (const auto* code = dynamic_cast<const MultiLineText*>(w)) out.push_back(code->text());
            else if (const auto* text = dynamic_cast<const InputText*>(w)) out.push_back(text->text());
            else if (const auto* drop = dynamic_cast<const DropDown*>(w))
                out.push_back(drop->selectedItem() ? drop->selectedItem()->value : std::string{});
            else out.emplace_back();
        }
        return out;
    }

    // Rules run against the widgets, not against a rebuilt UI: rebuilding on every
    // keystroke would take the focus away mid-word. Only what actually changed is
    // pushed back - a dropdown whose choices are the same is left alone so it does
    // not lose the row the pointer is over.
    void FormDialog::applyRules() {
        if (!rules_ || applying_ || inputs_.empty()) return;
        applying_ = true;

        auto proposed = state_;
        const auto values = currentValues();
        for (std::size_t i = 0; i < proposed.size() && i < values.size(); ++i)
            proposed[i].value = values[i];
        rules_(values, proposed);

        for (std::size_t i = 0; i < inputs_.size() && i < proposed.size(); ++i) {
            const auto& f = proposed[i];
            if (auto* drop = dynamic_cast<DropDown*>(inputs_[i])) {
                if (f.choices != state_[i].choices) {
                    std::vector<DropDown::Item> items;
                    for (const auto& c : f.choices) items.push_back(DropDown::Item{ c, c, {}, true });
                    drop->setItems(std::move(items));
                }
                // The value is re-seated every time, because a narrowed list may no
                // longer contain what was chosen and leaving the old text showing
                // over a list that cannot produce it is worse than moving it.
                int at = f.choices.empty() ? -1 : 0;
                for (std::size_t k = 0; k < f.choices.size(); ++k)
                    if (f.choices[k] == f.value) at = static_cast<int>(k);
                drop->setSelectedIndex(at);
            }
            else if (auto* text = dynamic_cast<InputText*>(inputs_[i])) {
                if (f.value != values[i]) text->setText(f.value);
                if (f.placeholder != state_[i].placeholder) text->setPlaceholder(f.placeholder);
            }
            inputs_[i]->setEnabled(f.enabled);
            // Le bouton ... d'un champ de chemin suit son champ.
            for (const auto& [index, button] : browseButtons_)
                if (index == i) button->setEnabled(f.enabled);
        }
        state_ = std::move(proposed);
        applying_ = false;
    }

    void FormDialog::setCodeFields(std::vector<std::size_t> indices) {
        codeFields_ = std::move(indices);
    }

    void FormDialog::setCompletionProvider(MultiLineText::CompletionProvider provider) {
        completion_ = std::move(provider);
    }

    void FormDialog::setFieldAssist(std::size_t index, InputText::Assist assist) {
        assists_.emplace_back(index, std::move(assist));
    }

    void FormDialog::setFieldBrowse(std::size_t index, PathBrowse browse) {
        browses_.emplace_back(index, std::move(browse));
    }

    std::unique_ptr<FormDialog> FormDialog::withBrowse(std::unique_ptr<FormDialog> dialog, std::size_t index,
                                                       PathBrowse browse) {
        if (dialog) dialog->setFieldBrowse(index, std::move(browse));
        return dialog;
    }

    namespace {
        // The UI font, spelled the same way Controls.cpp spells it. Wrapping has to
        // measure with the font the labels will actually be drawn in; guessing a
        // character count is what put every explanation off the right edge.
        constexpr gfx::FontId kDialogFont{ 16 };

        // Forces a height on whatever it wraps. A BoxLayout takes each child's preferred
        // size, and MultiLineText asks for one line - which is right for a field and
        // wrong for a program.
        class SizedBox final : public Widget {
        public:
            SizedBox(std::string id, WidgetPtr child, float height)
                : Widget(std::move(id)), height_(height) {
                addChild(std::move(child));
            }
            [[nodiscard]] SizeHint sizeHint() const override {
                SizeHint h;
                h.preferred = { 0.f, height_ };
                h.minimum = { 0.f, height_ };
                h.stretchX = 1.f;
                return h;
            }
        protected:
            void onLayout() override {
                if (!children().empty()) children()[0]->setBounds(contentRect());
            }
        private:
            float height_;
        };
    } // namespace

    core::Status FormDialog::buildUi() {
        const bool hasCode = !codeFields_.empty();
        auto isCode = [this](std::size_t i) {
            return std::find(codeFields_.begin(), codeFields_.end(), i) != codeFields_.end();
            };

        // A code field needs room for a program, not for a value. Twelve lines is
        // enough to see a block without scrolling and short enough that the rest of
        // the form is still on screen; past that the field scrolls.
        constexpr float kCodeHeight = 220.f;
        float width = hasCode ? 1040.f : 860.f;
        // Lot API 8 : la surface ou il s'ouvre est plus etroite (un onglet
        // detache) : le formulaire s'y tient, ses explications coupees a sa largeur.
        if (const gfx::Size s = surfaceSize(); s.w >= 1.f) width = std::min(width, std::max(480.f, s.w - 24.f));

        // The explanation takes room too. It did not, so a long one pushed the
        // fields off the bottom - and "Tableau a lire" has five lines of it, which
        // is exactly how the path and the name ended up unreadable under the text
        // that was supposed to help.
        std::size_t explanationLines = explanation_.empty() ? 0 : 1;
        std::size_t since = 0;
        for (char c : explanation_) {
            ++since;
            if (c == '\n') { ++explanationLines; since = 0; }
        }
        explanationLines += explanation_.size() / 90;

        float height = 130.f + static_cast<float>(explanationLines) * 20.f;
        for (std::size_t i = 0; i < fields_.size(); ++i)
            height += isCode(i) ? kCodeHeight + 34.f : 56.f;
        height = std::min(height, 760.f);   // the label is its own line now
        auto frame = std::make_unique<DialogFrame>(title_, width, height);

        auto body = std::make_unique<BoxLayout>(Orientation::Vertical, id() + ".body");
        body->setSpacing(6.f);
        body->setAlignment(Align::Stretch);

        if (!explanation_.empty()) {
            // Wrapped by MEASURED width, not by a character count.
            //
            // It used to cut at 88 characters, which assumed a width the font does
            // not have: every explanation ran off the right edge of the dialog and
            // the sentence that told you what the form does was the part you could
            // not read. Asking the renderer how wide the text actually is costs one
            // call per line and cannot be wrong about the font.
            const float room = width - 48.f;
            std::size_t from = 0;
            while (from < explanation_.size()) {
                // The newlines an author put in are kept: they are paragraph breaks.
                const auto hard = explanation_.find('\n', from);
                const auto limit = (hard == std::string::npos) ? explanation_.size() : hard;

                std::size_t cut = limit - from;
                while (cut > 0
                    && measureWidth(explanation_.substr(from, cut), kDialogFont) > room) {
                    const auto space = explanation_.rfind(' ', from + cut - 1);
                    if (space == std::string::npos || space <= from) { --cut; continue; }
                    cut = space - from;
                }
                if (cut == 0) cut = limit - from;    // one very long word: let it be

                body->addChild(std::make_unique<Label>(explanation_.substr(from, cut)));
                from += cut;
                while (from < explanation_.size()
                    && (explanation_[from] == ' ' || explanation_[from] == '\n')) ++from;
            }
        }

        inputs_.clear();
        browseButtons_.clear();
        // The declared fields seed the mutable state the rules then work on.
        state_.clear();
        for (const auto& d : fields_)
            state_.push_back(FieldState{ d.value, d.choices, d.placeholder, true, {} });

        for (const auto& f : fields_) {
            auto row = std::make_unique<BoxLayout>(Orientation::Horizontal, id() + ".row");
            row->setSpacing(8.f);
            row->addChild(std::make_unique<Label>(f.label, true));

            if (!f.choices.empty()) {
                auto drop = std::make_unique<DropDown>(id() + ".choice");
                std::vector<DropDown::Item> items;
                for (const auto& c : f.choices) items.push_back(DropDown::Item{ c, c, {}, true });
                drop->setItems(std::move(items));
                drop->setSelectedIndex(0);
                for (std::size_t i = 0; i < f.choices.size(); ++i)
                    if (f.choices[i] == f.value) drop->setSelectedIndex(static_cast<int>(i));
                auto& ref = static_cast<DropDown&>(row->addChild(std::move(drop)));
                inputs_.push_back(&ref);
                if (rules_)
                    links_ += ref.selectionChanged->connect([this](int) { applyRules(); });
            }
            else if (isCode(&f - fields_.data())) {
                // A code field goes on its OWN LINE, under its label, across the
                // whole dialog. Sharing a row with the label left it about half the
                // width, so a line of ST ran out of the box and a horizontal
                // scrollbar did the work the width should have been doing.
                body->addChild(std::move(row));
                // The ST editor, with what the document editor has: colouring,
                // completion, and a scrollbar when the code runs past the box.
                auto editor = std::make_unique<MultiLineText>(id() + ".code");
                editor->setLanguage(Language::StructuredText);
                editor->setText(f.value);
                editor->setReadOnly(false);
                if (completion_) editor->setCompletionProvider(completion_);
                auto* raw = editor.get();
                body->addChild(std::make_unique<SizedBox>(id() + ".codeBox", std::move(editor),
                    kCodeHeight));
                auto& ref = *raw;
                inputs_.push_back(&ref);
                if (rules_)
                    links_ += ref.textChanged->connect([this](const std::string&) { applyRules(); });
                continue;                       // the row is already in
            }
            else {
                auto input = std::make_unique<InputText>(id() + ".field");
                input->setPlaceholder(f.placeholder);
                input->setText(f.value);
                input->setMasked(f.secret);        // un mot de passe : des points
                for (const auto& [index, assist] : assists_)
                    if (index == static_cast<std::size_t>(&f - fields_.data())) input->setAssist(assist);
                auto& ref = static_cast<InputText&>(row->addChild(std::move(input)));
                inputs_.push_back(&ref);
                if (rules_)
                    links_ += ref.textChanged->connect([this](const std::string&) { applyRules(); });
                // Un champ de chemin : le bouton ... a sa droite (l'explorateur).
                // Un voisin du champ, pas un enfant : les scripts comptent
                // toujours les memes champs (champ N).
                const auto index = static_cast<std::size_t>(&f - fields_.data());
                for (const auto& [at, browse] : browses_) {
                    if (at != index || !browse.active()) continue;
                    auto button = std::make_unique<BrowseButton>(ref, browse, id() + ".parcourir." + std::to_string(index));
                    button->setFieldLabel(f.label);
                    // Le chemin choisi : le champ reprend le focus (Entree valide).
                    links_ += button->chosen->connect([this, field = &ref](const std::string&) {
                        focus().clear();                 // la chaine a pu garder le champ pour courant
                        focus().focus(field);
                    });
                    browseButtons_.emplace_back(index, &row->addChild(std::move(button)));
                    break;
                }
            }
            body->addChild(std::move(row));
        }

        auto buttons = std::make_unique<BoxLayout>(Orientation::Horizontal, id() + ".buttons");
        buttons->setSpacing(8.f);
        // La rangee recoit la hauteur en trop (le Spacer s'etire dans les deux
        // sens) : les boutons gardent leur hauteur, en bas du dialogue.
        buttons->setAlignment(Align::End);
        buttons->addChild(std::make_unique<Spacer>());

        if (cancellable_) {
            auto cancel = std::make_unique<Button>("Annuler", id() + ".cancel");
            auto& ref = static_cast<Button&>(buttons->addChild(std::move(cancel)));
            links_ += ref.clicked->connect([this] {
                manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Cancel, {} });
                });
        }
        auto ok = std::make_unique<Button>(confirmLabel_, id() + ".ok");
        ok->setStyle(Button::Style::Primary);
        auto& okRef = static_cast<Button&>(buttons->addChild(std::move(ok)));

        auto collect = [this] {
            // currentValues() already knows about every kind of input, including the
            // code editor. Reading the widgets a second time here is how the code
            // fields came back empty: this loop knew about InputText and DropDown
            // and silently contributed nothing for a MultiLineText.
            manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Ok,
                                                     core::joinFields(currentValues()) });
            };
        links_ += okRef.clicked->connect(collect);
        applyRules();

        // Enter in the last field confirms, which is what everyone expects.
        //
        // ENTREE SEULEMENT. InputText emet editingDone aussi quand il PERD le
        // focus : taper dans le dernier champ puis cliquer "Annuler" validait
        // le dialogue (le clic retire le focus au champ), puis "Annuler" le
        // fermait une seconde fois - et c'est l'ecran du dessous qui partait,
        // laissant une fenetre vide. Le champ a encore le focus sur Entree, plus
        // quand il vient de le perdre.
        //
        // Lot 12 : le dernier CHAMP DE SAISIE, meme suivi de listes (le modele
        // de "Nouvelle vue", la casse de "Dupliquer en remplacant") : sinon
        // Entree dans la description ne validait plus rien.
        // Un champ de code apres lui : pas d'Entree qui valide (comme avant).
        InputText* enterLast = nullptr;
        for (auto it = inputs_.rbegin(); it != inputs_.rend(); ++it) {
            if (dynamic_cast<DropDown*>(*it)) continue;
            if (auto* last = dynamic_cast<InputText*>(*it)) {
                enterLast = last;
                links_ += last->editingDone->connect([collect, last](const std::string&) {
                    if (last->focused()) collect();
                });
            }
            break;
        }
        // Lot API 6 : les champs que l'ecran designe en plus (jamais deux fois le
        // meme : deux validations fermeraient aussi l'ecran du dessous).
        std::vector<InputText*> enterMore;
        for (const std::size_t index : enterFields_) {
            if (index >= inputs_.size()) continue;
            auto* field = dynamic_cast<InputText*>(inputs_[index]);
            if (!field || field == enterLast || std::find(enterMore.begin(), enterMore.end(), field) != enterMore.end()) continue;
            enterMore.push_back(field);
            links_ += field->editingDone->connect([collect, field](const std::string&) {
                if (field->focused()) collect();
            });
        }

        body->addChild(std::move(buttons));
        frame->attachBody(std::move(body));
        setRoot(std::move(frame));
        return core::ok();
    }

    // ========================================================== MessageDialog ====
    MessageDialog::MessageDialog(std::string title, std::string message, Icon icon,
        std::string confirmLabel)
        : menu::WidgetMenu("dialog.message"),
        title_(std::move(title)), message_(std::move(message)),
        confirmLabel_(std::move(confirmLabel)), icon_(icon) {}

    menu::MenuTraits MessageDialog::traits() const { return dialogTraits(); }

    // Lot 15 : le compte a rebours - personne ne repond, le bouton principal l'est d'office.
    void MessageDialog::Update(const menu::FrameContext& f) {
        menu::WidgetMenu::Update(f);
        if (countdown_ <= 0 || answered_ || !okButton_) return;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (deadline_ < 0) deadline_ = now + countdown_;
        const double rest = deadline_ - now;
        const int left = std::max(0, static_cast<int>(std::ceil(rest)));
        okButton_->setText((confirmLabel_.empty() ? std::string("OK") : confirmLabel_) + " (" + std::to_string(left) + " s)");
        if (rest <= 0) {
            answered_ = true;
            manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Ok, {} });
        }
    }

    core::Status MessageDialog::buildUi() {
        // How long is it, really? A fixed 210 pixels was fine for "are you sure" and
        // cut everything else off at whatever line happened to reach the bottom -
        // including the reason a macro refused to run, which is the one message
        // nobody can afford to lose.
        std::size_t lines = 1, longest = 0, since = 0;
        // Lot 15 : la hauteur compte CHAQUE ligne repliee (environ 70 caracteres
        // par ligne), pas seulement la plus longue : trois lignes longues
        // coupaient la fin du message (la question elle-meme).
        std::size_t wrapped = 0;
        for (char c : message_) {
            ++since;
            if (c == '\n') {
                ++lines;
                longest = std::max(longest, since);
                wrapped += std::max<std::size_t>(1, (since + 69) / 70);
                since = 0;
            }
        }
        longest = std::max(longest, since);
        wrapped += std::max<std::size_t>(1, (since + 69) / 70);
        lines += longest / 70;            // roughly, for the wrapping

        // Past a certain size a paragraph is the wrong widget: it cannot scroll, so
        // the part that does not fit is simply not shown. A read-only editor can,
        // and it already honours the newlines.
        const bool longMessage = lines > 8 || message_.size() > 500;
        const float width = longMessage ? 760.f : 560.f;
        const float height = longMessage
            ? 470.f
            : std::min(520.f, 120.f + static_cast<float>(std::max(lines, wrapped)) * 19.f);
        auto frame = std::make_unique<DialogFrame>(title_, width, height);

        auto body = std::make_unique<BoxLayout>(Orientation::Vertical, "dialog.message.body");
        body->setSpacing(10.f);

        // The message widget is a small local type: a wrapped, read-only paragraph.
        class Paragraph final : public Widget {
        public:
            Paragraph(std::string text, Icon icon) : text_(std::move(text)), icon_(icon) {}
            // Lot 15 : le paragraphe prend la hauteur du dialogue (la rangee des
            // boutons garde la sienne, en bas). Sans cela il recevait la moitie
            // du reste - et sa taille d'avant, d'une mise en page a l'autre : un
            // message de six lignes etait coupe a la troisieme.
            [[nodiscard]] SizeHint sizeHint() const override {
                SizeHint h;
                h.stretchX = 1.f;
                h.stretchY = 100.f;
                return h;
            }
        protected:
            void onPaint(const PaintContext& ctx) override {
                const auto& c = ctx.theme.color;
                const auto  r = contentRect();
                const gfx::Color accent = icon_ == Icon::Error ? c.error
                    : icon_ == Icon::Warning ? c.warning : c.info;

                // Naive greedy wrap: enough for a message box, and it keeps the
                // renderer free of a paragraph layout engine.
                //
                // Explicit newlines are honoured. They were not, and a message
                // written as three paragraphs came out as one run-on block - which
                // is how "Ce qui se passerait :" ended up glued to its first action.
                const float maxW = r.w - 14.f;
                float y = r.y;
                std::size_t start = 0;
                while (start < text_.size()) {
                    if (text_[start] == '\n') {
                        y += ctx.r.lineHeight(ctx.theme.font.ui) + 2.f;
                        ++start;
                        continue;
                    }
                    const auto upTo = text_.find('\n', start);
                    const auto available = (upTo == std::string::npos ? text_.size() : upTo) - start;
                    const auto fits = std::min<std::size_t>(
                        available,
                        ctx.r.fitCharacters(std::string_view(text_).substr(start, available),
                            ctx.theme.font.ui, maxW));
                    auto take = std::max<std::size_t>(1, fits);
                    if (take < available) {
                        const auto slice = std::string_view(text_).substr(start, take);
                        const auto space = slice.find_last_of(' ');
                        if (space != std::string_view::npos && space > 0) take = space + 1;
                    }
                    ctx.r.drawText({ r.x + 12.f, y }, std::string_view(text_).substr(start, take),
                        ctx.theme.font.ui, c.text);
                    y += ctx.r.lineHeight(ctx.theme.font.ui) + 2.f;
                    start += take;
                    // Lot 15 : la fin de ligne qui vient d'etre passee ne compte
                    // pas une seconde fois (chaque \n ajoutait une ligne vide).
                    if (take == available && start < text_.size() && text_[start] == '\n') ++start;
                    if (y > r.bottom()) break;
                }
                // Le trait de couleur, a la hauteur du texte.
                const float bar = std::min(y, r.bottom()) - r.y - 4.f;
                if (bar > 0.f) ctx.r.fillRect({ r.x, r.y + 2.f, 3.f, bar }, accent);
            }
        private:
            std::string text_;
            Icon        icon_;
        };

        if (longMessage) {
            // A read-only editor: it scrolls, so nothing is silently lost, and the
            // reader can select the text to paste it into a mail.
            auto view = std::make_unique<MultiLineText>("dialog.message.text");
            view->setText(message_);
            view->setReadOnly(true);
            body->addChild(std::move(view));
        }
        else {
            body->addChild(std::make_unique<Paragraph>(message_, icon_));
        }

        auto buttons = std::make_unique<BoxLayout>(Orientation::Horizontal, "dialog.message.buttons");
        buttons->setSpacing(8.f);
        // La rangee recoit la hauteur en trop (le Spacer s'etire dans les deux
        // sens) : les boutons gardent leur hauteur, en bas du dialogue.
        buttons->setAlignment(Align::End);
        buttons->addChild(std::make_unique<Spacer>());

        // Two buttons when there is a question to answer, one when there is only
        // something to read.
        const bool asks = !confirmLabel_.empty();
        if (asks) {
            auto cancel = std::make_unique<Button>(cancelLabel_, "dialog.message.cancel");
            auto& cancelRef = static_cast<Button&>(buttons->addChild(std::move(cancel)));
            links_ += cancelRef.clicked->connect([this] {
                manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Cancel, {} });
                });
        }

        auto ok = std::make_unique<Button>(asks ? confirmLabel_ : std::string("Fermer"),
            "dialog.message.ok");
        ok->setStyle(Button::Style::Primary);
        auto& okRef = static_cast<Button&>(buttons->addChild(std::move(ok)));
        okButton_ = &okRef;
        links_ += okRef.clicked->connect([this] {
            answered_ = true;
            manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Ok, {} });
            });
        body->addChild(std::move(buttons));

        frame->attachBody(std::move(body));
        setRoot(std::move(frame));
        return core::ok();
    }

    // ====================================================== OpenProjectDialog ====
    OpenProjectDialog::OpenProjectDialog(App& app, bool hardwareOnly)
        : menu::WidgetMenu("dialog.open"), app_(app), hardwareOnly_(hardwareOnly) {}

    menu::MenuTraits OpenProjectDialog::traits() const { return dialogTraits(); }

    core::Status OpenProjectDialog::buildUi() {
        auto frame = std::make_unique<DialogFrame>(
            hardwareOnly_ ? "Importer la configuration mat\xC3\xA9rielle" : "Ouvrir un projet", 720.f, 170.f);

        auto body = std::make_unique<BoxLayout>(Orientation::Vertical, "dialog.open.body");
        body->setSpacing(10.f);

        // Le chemin, et a sa droite l'explorateur de fichiers : ... pour un
        // export (.XPG, .XHW...), Dossier... pour un dossier de projet.
        auto row = std::make_unique<BoxLayout>(Orientation::Horizontal, "dialog.open.row");
        row->setSpacing(6.f);
        auto input = std::make_unique<InputText>("dialog.open.path");
        input->setPlaceholder(hardwareOnly_
            ? "Chemin de l'export .XHW de la configuration  (Ctrl+V pour coller)"
            : "Un dossier de projet, ou un export .XPG / .XHW / .XDB / .XEF  (Ctrl+V pour coller)");
        if (!hardwareOnly_ && !app_.recentPaths().empty()) input->setText(app_.recentPaths().front());
        if (hardwareOnly_ && !app_.sourcePaths().empty()) {
            // Offer the .XHW sitting next to the program export, since that is where
            // Control Expert puts it by default.
            auto guess = app_.sourcePaths().front();
            const auto slash = guess.find_last_of("/\\");
            guess = (slash == std::string::npos ? std::string{} : guess.substr(0, slash + 1)) + "CONFIG.XHW";
            input->setText(std::move(guess));
        }
        path_ = &static_cast<InputText&>(row->addChild(std::move(input)));
        {
            // Sans chemin dans le champ, l'explorateur part du dossier des projets
            // (ou de celui du projet ouvert, pour son .XHW).
            const std::string from = hardwareOnly_ && !app_.projectFolder().empty() ? app_.projectFolder()
                                                                                     : App::projectsRoot().string();
            // Un export, ou le project.xpgproj d'un dossier de projet : le champ
            // prend alors le dossier (c'est lui qui s'ouvre).
            auto file = std::make_unique<BrowseButton>(
                *path_, openFile(hardwareOnly_ ? "Configuration mat\xC3\xA9rielle (.XHW)|*.xhw"
                                               : "Projets et exports|*.xpgproj;*.xpg;*.xhw;*.xdb;*.xef|Projets (project.xpgproj)|*.xpgproj"
                                                 "|Exports Control Expert|*.xpg;*.xhw;*.xdb;*.xef",
                                 from, hardwareOnly_ ? "Importer la configuration mat\xC3\xA9rielle" : "Ouvrir un projet ou un export"),
                "dialog.open.parcourir");
            file->setFieldLabel(hardwareOnly_ ? "L'export .XHW" : "Un projet ou un export .XPG, .XHW, .XDB, .XEF");
            auto& fileRef = static_cast<BrowseButton&>(row->addChild(std::move(file)));
            links_ += fileRef.chosen->connect([this](const std::string& p) {
                const std::string_view project = ".xpgproj";
                if (!hardwareOnly_ && p.size() > project.size()) {
                    std::string tail = p.substr(p.size() - project.size());
                    for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    const auto slash = p.find_last_of("/\\");
                    if (tail == project && slash != std::string::npos) path_->setText(p.substr(0, slash));
                }
                focus().clear();
                focus().focus(path_);                    // Entree ouvre ce qui a ete choisi
            });
            if (!hardwareOnly_) {
                auto folder = std::make_unique<BrowseButton>(*path_, chooseFolder(from, "Ouvrir un dossier de projet"),
                                                             "dialog.open.dossier", "Dossier\xE2\x80\xA6");
                folder->setFieldLabel("Un dossier de projet");
                links_ += folder->chosen->connect([this](const std::string&) {
                    focus().clear();
                    focus().focus(path_);
                });
                row->addChild(std::move(folder));
            }
        }
        body->addChild(std::move(row));

        auto buttons = std::make_unique<BoxLayout>(Orientation::Horizontal, "dialog.open.buttons");
        buttons->setSpacing(8.f);
        // La rangee recoit la hauteur en trop (le Spacer s'etire dans les deux
        // sens) : les boutons gardent leur hauteur, en bas du dialogue.
        buttons->setAlignment(Align::End);
        buttons->addChild(std::make_unique<Spacer>());
        auto cancel = std::make_unique<Button>("Annuler", "dialog.open.cancel");
        auto& cancelRef = static_cast<Button&>(buttons->addChild(std::move(cancel)));
        auto open = std::make_unique<Button>(hardwareOnly_ ? "Importer" : "Ouvrir", "dialog.open.ok");
        open->setStyle(Button::Style::Primary);
        auto& openRef = static_cast<Button&>(buttons->addChild(std::move(open)));

        links_ += cancelRef.clicked->connect([this] {
            manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Cancel, {} });
            });
        links_ += openRef.clicked->connect([this] {
            manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Ok, path_->text() });
            });
        // ENTREE SEULEMENT (comme FormDialog) : editingDone part aussi quand le
        // champ PERD le focus - cliquer ... ou Annuler apres avoir tape ouvrait
        // le chemin tape.
        links_ += path_->editingDone->connect([this](const std::string& p) {
            if (!p.empty() && path_->focused())
                manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Ok, p });
            });

        body->addChild(std::move(buttons));
        frame->attachBody(std::move(body));
        setRoot(std::move(frame));
        return core::ok();
    }

    // =================================================== ImportProgressDialog ====
    ImportProgressDialog::ImportProgressDialog(App& app)
        : menu::WidgetMenu("dialog.importProgress"), app_(app) {}

    menu::MenuTraits ImportProgressDialog::traits() const {
        auto t = dialogTraits();
        t.kind = menu::MenuKind::Modal;
        t.closableWithEscape = false;    // cancelling goes through the Cancel button
        return t;
    }

    core::Status ImportProgressDialog::buildUi() {
        auto frame = std::make_unique<DialogFrame>("Importing", 480.f, 150.f);

        auto body = std::make_unique<BoxLayout>(Orientation::Vertical, "dialog.progress.body");
        body->setSpacing(10.f);

        auto bar = std::make_unique<ProgressBar>();
        bar_ = &body->addChild(std::move(bar));

        auto cancel = std::make_unique<Button>("Annuler", "dialog.progress.cancel");
        auto& cancelRef = static_cast<Button&>(body->addChild(std::move(cancel)));
        links_ += cancelRef.clicked->connect([this] {
            app_.importer().cancel();
            manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Cancel, {} });
            });

        frame->attachBody(std::move(body));
        setRoot(std::move(frame));
        return core::ok();
    }

    void ImportProgressDialog::onEnter() {
        links_ += app_.events().subscribe<importer::ImportProgress>(
            [this](const importer::ImportProgress& p) {
                if (bar_) static_cast<ProgressBar*>(bar_)->set(p.fraction, p.stage);
            });
        links_ += app_.events().subscribe<importer::ImportFinished>(
            [this](const importer::ImportFinished&) {
                manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Ok, {} });
            });
        links_ += app_.events().subscribe<importer::ImportFailed>(
            [this](const importer::ImportFailed&) {
                manager().CloseDialog(menu::DialogResult{ menu::DialogResult::Button::Cancel, {} });
            });
    }

    void ImportProgressDialog::onExit() { links_.clear(); }

} // namespace app