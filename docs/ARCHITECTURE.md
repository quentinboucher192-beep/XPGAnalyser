# PLC Project Analyzer — Architecture

A C++20/23 desktop framework built on SDL3 for importing and analysing Schneider
Electric EcoStruxure Control Expert projects.

Everything in this document is backed by code in `src/`. The import pipeline,
the navigation stack and the view models compile and are covered by four
executable tests that run headless. Numbers quoted below were measured on the
supplied `MAST.XPG` and on a generated fixture at the target scale.

---

## 0. What the reference file actually contains

Before designing the importer I parsed the supplied file. This changed two
decisions, so it comes first.

`MAST.XPG` is a **Control Expert V15.3 program exchange file**, DTD version 41,
root element `<PGMExchangeFile>`, 522 kB, UTF-8 with CRLF.

| Element | Count | Becomes |
|---|---:|---|
| `DDTSource` | 28 | derived data types (285 member fields) |
| `FBSource` | 8 | DFB types (34 instances across the project) |
| `programUnit` | 3 | multi-section program units |
| `program` / `FBProgram` | 75 | sections, all in ST, 10 336 lines |
| `dataBlock/variables` | 251 | global dictionary, 90 of them located (`%MW…`) |
| `animationTable` | 5 | watch tables |
| **Total variable declarations** | **891** | |

### Two findings that shaped the design

**1. The brief's entry point is a family, not a file.** Control Expert exports a
project as several documents: `.XPG` (program), `.XDB` (variable dictionary),
`.XHW` (hardware), `.XEF`/`.ZEF` (whole project). `.STU` is the binary working
file and is not an exchange format at all. A section export is conventionally
named after its task, which is why the brief says "`.MAST`" — the file is
`MAST.XPG`.

**2. A `.XPG` does not contain the rack.** The only hardware information in the
whole document is one attribute:

```xml
<resource resName="Micro Basic" resIdent="BMX P34 2020 03.30">
```

That identifies the CPU (BMX P34 2020 → Modicon M340 family, OS 03.30) and
nothing else. There is no rack, no slot list, no module reference, no I/O card,
no firmware per module.

The mockup shows a populated rack view with six modules and order numbers. **That
view cannot be filled from a `.XPG`.** Rather than fabricate a plausible rack,
the importer marks `HardwareConfig::inferred = true`, records a
`partialDataNotice`, and the PLC configuration pane shows what it does know plus
an explicit "import the matching `.XHW` or `.XEF`" prompt. `ProjectImporter`
merges several files into one `Project` precisely so that adding the `.XHW`
later fills the gap without re-importing anything.

This is the single most important decision in the document: in an engineering
tool, an invented rack layout is worse than an empty pane.

---

## 1. Folder structure

```
xpg-analyzer/
├── CMakeLists.txt
├── docs/
│   └── ARCHITECTURE.md
├── src/
│   ├── core/                    no dependencies, not even on the platform
│   │   ├── Result.hpp           std::expected + C++20 fallback, ErrorCode, XPG_TRY
│   │   ├── Error.cpp
│   │   ├── Signal.hpp           Observer: Signal<Args…>, RAII Connection, ConnectionScope
│   │   ├── EventBus.hpp         deferred, thread-safe, type-erased pub/sub
│   │   ├── Command.hpp          Command pattern, undo stack, ActionRegistry
│   │   └── ServiceRegistry.hpp  constructor-injection container
│   ├── platform/                the only place SDL3 exists
│   │   ├── Geometry.hpp         Rect, Point, Size, Color, Edges
│   │   ├── InputEvent.hpp       SDL_Event translated into a variant
│   │   ├── Renderer.hpp         gfx::IRenderer + SdlRenderer (pimpl)
│   │   ├── SdlRenderer.cpp
│   │   ├── SdlEventPump.cpp
│   │   └── FontAtlas.cpp
│   ├── ui/
│   │   ├── Theme.hpp/.cpp       design tokens, three themes
│   │   ├── Widget.hpp           retained-mode base + FocusChain
│   │   ├── Layout.hpp           BoxLayout, DockLayout, GridLayout
│   │   └── widgets/
│   │       ├── Controls.hpp     Button, InputText, MultiLineText, Checkbox,
│   │       │                    RadioButton/Group, ToggleButton, DropDown
│   │       ├── Containers.hpp   GroupBox, Splitter, TabControl,
│   │       │                    ScrollablePanel, ToolBar, StatusBar
│   │       ├── DataViews.hpp    ListView, TreeView, TableView, PropertyGrid
│   │       └── FilterChain.cpp
│   ├── menu/
│   │   ├── IMenu.hpp            the six-method contract + WidgetMenu base
│   │   └── MenuManager.hpp/.cpp navigation stack, modals, dialogs, overlays
│   ├── domain/
│   │   └── ProjectModel.hpp/.cpp  Project, Variable, DerivedType, Pou, Section…
│   ├── import/
│   │   ├── XmlReader.hpp/.cpp     zero-copy pull parser
│   │   ├── ProjectParser.hpp      IProjectParser + XPG/XDB/XHW + HardwareCatalog
│   │   ├── XpgParser.cpp
│   │   ├── ProjectAnalyzer.hpp/.cpp
│   │   └── ProjectImporter.hpp/.cpp
│   ├── app/
│   │   ├── App.hpp/.cpp           composition root, frame loop
│   │   ├── ViewModels.hpp/.cpp    the M–V seam
│   │   └── screens/               the six screens + dialogs
│   └── main.cpp                   GUI and --cli modes
└── tests/
    ├── menu_test.cpp        navigation, modality, dialog results
    ├── import_test.cpp      end-to-end against a real export
    └── viewmodel_test.cpp   MVC seam + sort/filter benchmarks
```

### Dependency rule

```
        main ──► app ──► ui ──► core
                  │       │       ▲
                  ├──► menu ──────┤
                  ├──► import ────┤
                  │       │       │
                  └──► domain ────┘
                          ▲
                     platform (SDL3)
```

Arrows only point right. `domain` and `import` never include a UI header;
`ui` and `menu` never include a domain header. That is why
`import_test` and `menu_test` link and run without SDL, a window, or a display
server — which is also how they run in CI.

---

## 2. Part 1 — Menu framework

### 2.1 Class model

```
                       ┌──────────────────────┐
                       │      «interface»     │
                       │        IMenu         │
                       ├──────────────────────┤
                       │ + Initialize(): Status
                       │ + Update(FrameContext)
                       │ + Render(IRenderer, FrameContext)
                       │ + HandleEvent(InputEvent): EventResult
                       │ + OnEnter()
                       │ + OnExit()
                       │ + id(): MenuId
                       │ + traits(): MenuTraits
                       │ + canClose(): bool
                       │ # manager(): MenuManager&
                       └──────────▲───────────┘
                                  │
                       ┌──────────┴───────────┐
                       │      WidgetMenu      │  holds a Widget tree +
                       │  # buildUi(): Status │  a FocusChain, forwards
                       └──────────▲───────────┘  the lifecycle to them
                                  │
   ┌────────────┬────────────┬────┴───────┬─────────────┬──────────────┐
 StartupScreen  MainAnalysis VariableExpl LibraryExpl  Statistics   MessageDialog
                  Screen       Screen       Screen      Dashboard   OpenProjectDialog

  ┌───────────────────────────────────────────────────────────────────┐
  │                           MenuManager                             │
  ├───────────────────────────────────────────────────────────────────┤
  │ - factory_   : MenuFactory                                        │
  │ - stack_     : vector<Entry>      ← screens, modals, dialogs       │
  │ - overlays_  : vector<Entry>      ← always painted on top          │
  │ - pending_   : deque<Request>     ← deferred mutation              │
  │ - history_   : vector<MenuId>     ← PreviousMenu()                 │
  ├───────────────────────────────────────────────────────────────────┤
  │ + PushMenu / PopMenu / PopTo / PopToRoot                           │
  │ + ReplaceMenu / SwitchMenu / PreviousMenu                          │
  │ + ShowModal / ShowDialog / CloseDialog                             │
  │ + ShowOverlay / CloseOverlay / CloseAllOverlays                    │
  │ + applyPending / Update / Render / HandleEvent                     │
  │ + path(): vector<MenuId>          ← breadcrumb                     │
  └───────────────────────────────────────────────────────────────────┘
                  ◇ owns ▼                        ◇ creates ▼
              vector<MenuPtr>                    MenuFactory
```

### 2.2 Where I deviated from the brief, and why

The brief asks for a **`MenuManager` singleton** and, in Part 5, for
**dependency injection**. Those pull against each other. A global mutable
navigation stack makes it impossible to open a second window, to test a
navigation sequence in isolation, or to control destruction order at shutdown.

`MenuManager` is therefore an ordinary class with an ordinary constructor, owned
by `App` and injected. A `MenuManager::instance()` accessor exists and is
populated by `App` through a `ScopedInstance` RAII guard, for call sites where
threading a reference would be gratuitous. **Nothing in the framework itself uses
`instance()`** — delete it and everything still builds. You get the ergonomics
the brief wanted without the testability cost.

### 2.3 Deferred mutation

Every navigation call is queued and applied by `applyPending()` at the top of the
frame:

```cpp
void Button::onClick() { manager().PopMenu(); }   // queued, not immediate
```

Without this, a menu that pops itself in response to its own click destroys the
object whose stack frame is still executing. The test asserts it:

```cpp
m4.PopMenu();                       // queued
assert(m4.top() == top);            // stack unchanged mid-frame
m4.applyPending();
assert(m4.depth() == 0);
```

Requests enqueued *by* a request are handled on the next frame, so each frame's
navigation is deterministic and bounded.

### 2.4 Menu kinds and layer composition

`MenuTraits` decides how a layer interacts with those beneath it:

| Kind | rendersBelow | updatesBelow | blocksInput | dimsBelow | Used for |
|---|---|---|---|---|---|
| `Screen` | ✗ | ✗ | ✓ | ✗ | Startup, Analysis, Settings |
| `Modal` | ✓ | ✗ | ✓ | ✓ | Import progress, confirmations |
| `Dialog` | ✓ | ✗ | ✓ | ✓ | Open project, Save-changes |
| `Overlay` | ✓ | ✓ | ✗ | ✗ | DropDown popups, tooltips, toasts |

`firstRenderedLayer()` walks down from the top while `rendersBelow` is set, so a
dialog over a screen paints two layers and an opaque screen paints one.

### 2.5 Unlimited nesting

Nesting is expressed in **ids**, not in code:

```cpp
factory.add("main.settings.graphics.advanced", [] { … });
```

`MenuFactory::childrenOf("main.settings")` returns direct children only —
`main.settings.audio`, `main.settings.graphics`, `main.settings.theme` but not
`main.settings.graphics.advanced` — which is what a generated submenu list needs.
Depth is unbounded; the test pushes four levels and pops back through them.

### 2.6 Event propagation

```
SDL_Event
   │  SdlEventPump::translate()          ← the only SDL→UI conversion
   ▼
ui::InputEvent (variant)
   │
   ▼  MenuManager::HandleEvent
   ├─ overlays_, newest → oldest         a dropdown popup sees the click first
   │      └─ Consumed? → stop
   ├─ stack_, top → bottom
   │      └─ Consumed? → stop
   │      └─ traits.blocksInput? → stop  ← the modal barrier
   ▼
Widget::dispatch (inside the screen)
   ├─ capture phase   parent → child     (grabs: splitter drag, active editor)
   ├─ target          hitTest(local)
   └─ bubble phase    child → parent     unhandled keys reach the shortcut table
```

Two levels, same `EventResult::{Ignored, Consumed}` contract at both. The test
asserts that an overlay lets an event through to the screen below while a modal
swallows it.

### 2.7 Lifecycle

```
create ─► Initialize() ─► OnEnter() ─┬─► Update()/Render()/HandleEvent()  (per frame)
                            ▲        │
                            │        └─► OnExit()  ──► destroyed
                            └───────── re-entered when the layer above pops
```

`Initialize()` runs once and returns a `Status`, so a screen that cannot build
its UI fails the push instead of appearing broken. `canClose()` lets a screen
veto its own pop — that is where "you have unsaved analysis" lives, without the
manager knowing what analysis is.

---

## 3. Part 2 — Widget framework

### 3.1 Class model

```
                    ┌──────────────────────────────┐
                    │            Widget            │
                    ├──────────────────────────────┤
                    │ - bounds_, padding_          │
                    │ - visibility_, enabled_      │
                    │ - hovered_, focused_         │
                    │ - dirtyPaint_, dirtyLayout_  │
                    │ - parent_ : Widget*          │ non-owning
                    │ - children_ : vector<unique_ptr<Widget>>
                    ├──────────────────────────────┤
                    │ + setBounds / contentRect    │
                    │ + setVisibility / setEnabled │
                    │ + addChild / removeChild     │
                    │ + layout / render / dispatch │
                    │ + invalidate(Layout)         │
                    │ + sizeHint(): SizeHint       │
                    │ ~ focusGained/Lost, hoverChanged : Signal
                    │ # onLayout/onPaint/onEvent/hitTest
                    └──────────────▲───────────────┘
        ┌──────────────┬───────────┼────────────┬──────────────────┐
   layouts        controls      containers    data views
  BoxLayout       Button        GroupBox      ListView   ─► IListModel
  DockLayout      InputText     Splitter      TreeView   ─► ITreeModel
  GridLayout      MultiLineText TabControl    TableView  ─► ITableModel + FilterChain
                  Checkbox      ScrollablePanel PropertyGrid
                  RadioButton   ToolBar
                  ToggleButton  StatusBar
                  DropDown
```

### 3.2 The three rules that make 50 000 rows work

**Model/View separation.** The view never owns rows. `TableView` asks an
`ITableModel` for `cellText(row, column)`. Importing a project costs one
allocation pass over the domain vectors, not one widget per cell.

**Virtualisation.** Only rows intersecting the viewport are measured and
painted: at 22 px rows in a 700 px pane that is ~32 rows, independent of model
size.

**Indirection vector.** Sorting and filtering never touch the model; they rebuild
a `std::vector<RowIndex>` mapping view rows to model rows. Sorting is one
`std::stable_sort` over 4-byte indices; clearing a filter is a vector swap.
Selection is stored as *model* rows, so it survives re-sorting.

Measured on a 59 000-row model (`tests/viewmodel_test.cpp`):

| Operation | 891 rows (real file) | 59 000 rows (fixture) |
|---|---:|---:|
| Sort by Name | 0.17 ms | 7.56 ms |
| Sort by Address (numeric) | 0.03 ms | 4.21 ms |
| Global text filter | 0.24 ms | 11.09 ms |
| Predicate filter ("unused only") | 0.01 ms | 0.44 ms |

All of these are user-initiated, one-off operations; the predicate case fits
inside a single 16 ms frame with room to spare.

### 3.3 TreeView

- Expanded state is flattened into a linear `vector<VisualRow>` on structural
  change; painting and hit-testing are then O(visible rows).
- `+`/`−` expander, per-level indent, nested hierarchy of unbounded depth.
- **Lazy loading contract:** `hasChildren()` may answer before `childCount()` is
  meaningful. The view calls `fetchChildren()` once on first expansion and shows
  a placeholder row until `childrenReady` fires. This is what lets a 10 000-POU
  tree open instantly.
- `setFilter(predicate)` keeps the ancestors of every match so matches stay
  reachable.

### 3.4 TableView

Multiple columns with per-column width/min-width/resizable/sortable/visible/align;
`SelectionMode::{None, Single, Multi, Extended}`; frozen leading columns;
alternating row colours; `autoSizeColumn` samples only visible rows.

Comparison lives in the model, not the view — because only the model knows that
`%MW1174` is an address and not a string:

```cpp
case Address: return va.address < vb.address;   // area, then numeric offset
```

`%MW9` sorts before `%MW10`. A lexicographic sort gets that wrong, and in an I/O
list it is wrong in a way that costs an engineer real time.

`FilterChain` composes three stages, cheapest first: predicates → per-column
terms → global term.

### 3.5 PropertyGrid

Name/Value columns with a draggable divider, arbitrarily nested `Category`
objects, typed editors resolved by value type (`Text, Integer, Real, Boolean,
Enum, Address, Color, ReadOnly`), one live editor at a time, optional description
strip. A property with a null `commit` callback is read-only — which is every
property in an import-only tool today, and the seam through which write-back
would arrive later.

---

## 4. Part 3 — Import system

### 4.1 Pipeline

```
path ──► readFile ──► sniff() ──► IProjectParser (Factory)
                        │                │
                        │                ├─ XpgParser   ✅ implemented
                        │                ├─ XdbParser   declared
                        │                └─ XhwParser   declared
                        ▼                ▼
                 SourceFormat      domain::Project  ◄── merge point for
                                          │              multi-file imports
                                          ▼
                                  ProjectAnalyzer ──► AnalysisReport
                                          │
                                          ▼
                                    EventBus events ──► UI
```

`sniff()` reads the first kilobyte and matches the root element, falling back to
the extension only if that fails. `MAST.XPG`, `MAST.XEF` and a file someone
renamed to `.TXT` all resolve correctly. A `.STU` is rejected with an actionable
message rather than a parse error.

### 4.2 Why a pull parser

A DOM materialises every element and attribute as heap nodes. On a 50 MB export
that is hundreds of megabytes and a visible stall. `XmlReader` is a pull parser
handing out `string_view`s into one buffer, so the importer copies exactly the
bytes it keeps. It never throws, never allocates per element, and reports the
line number, so a malformed file yields *"line 4127: attribute 'typeName'
missing on \<variables\>"* rather than an empty tree.

It handles BOM, comments, CDATA, declarations, self-closing elements, entity
decoding (named, decimal, hex, with UTF-8 re-encoding), plus `readElementText()`
for lifting a 20 kB `<STSource>` body in one call and `skipElement()` for
subtrees the options exclude.

### 4.3 Domain model storage

Flat `std::vector`s owned by `Project`; relationships are 32-bit indices, not
pointers. That gives contiguous iteration for the analyzer, trivial
serialisation of a cached project, and stable identity across reallocation.

Names are interned. A project of this kind repeats `BOOL`, `EBOOL`, `MAST`,
`%MW` tens of thousands of times; interning turns each into a 4-byte id and makes
type resolution an integer compare.

> **A bug worth recording.** `StringPool` first used
> `std::vector<std::string>` for storage with a `unordered_map<string_view,
> SymbolId>` index. Short names like `"BOOL"` live in the small-string buffer
> *inside* the `std::string` object, so a vector reallocation moved them and
> every key in the map dangled. The symptom was subtle: the type histogram
> showed `INT` twice with split counts. Storage is now `std::deque<std::string>`,
> which never moves an element once pushed. The regression is caught by
> comparing the parser's type histogram against an independent count.

### 4.4 What is extracted

| Brief asks for | Source in the file | Where it lands |
|---|---|---|
| PLC family, CPU, firmware | `resource/@resIdent` + `HardwareCatalog` | `HardwareConfig`, PropertyGrid |
| Rack, modules, I/O cards, comms cards | **not in a .XPG** | flagged as a partial-data notice |
| Global variables | `dataBlock/variables` | `Variable{scope=Global}` |
| Local / public / parameters | POU + FB variable groups | `Variable{scope=Local/Public/Input/Output/InOut}` |
| Constants | `variableInit/@value` | `Variable::initValue` |
| Derived types, structures | `DDTSource/structure` | `DerivedType` + member `Variable`s |
| Arrays | `typeName="ARRAY[0..27] OF X"` | `TypeRef{klass=Array, low, high, elementType}` |
| DFB libraries, function libraries | `FBSource` | `Pou{FunctionBlockType}` + `LibraryEntry` |
| Programs, sections, function blocks | `program`, `FBProgram`, `programUnit` | `Section`, `Pou` |
| DFB instances | variables typed by an `FBSource` name | `Pou::instanceCount` (34 found) |
| Language LD/FBD/ST/IL/SFC | `STSource` / `LDSource` / `FBDSource` / `ILSource` / `SFCSource` | `PouLanguage`, one mapping in `languageFromElement()` |

The reference file is 100 % ST. The language mapping is a single function so
adding graphical languages means teaching the *editor* to render them, not
teaching the *parser* a new shape.

### 4.5 Type resolution

Types are resolved in a link step after the streaming pass, because a `.XPG`
declares variables before the DDTs they reference. The link step assigns
`derivedIndex` / `fbTypeIndex`, counts instances, and emits a diagnostic for
every unresolved name instead of silently leaving `TypeClass::Unknown`.

Section-to-task binding happens in the same step: `<taskDesc>` declares the
execution order and closes long before the section bodies appear, so sections are
bound by task name afterwards and re-sorted by `SectionOrder`.

### 4.6 Analysis

`ProjectAnalyzer` is a pure function of the `Project`: same input, same report,
no hidden state, no UI dependency, cancellable through an `atomic_bool`.

The expensive part is cross-referencing. Naively, "is variable V used?" over
50 000 variables and 10 MB of source is 50 000 substring searches. Instead every
section body is tokenised **once** into a case-insensitive identifier multiset
(IEC 61131-3 identifiers are case-insensitive), skipping `(* … *)`, `//` and
`'string'` content. Each variable is then one hash lookup. Complexity is
O(bytes of code + symbols), not the product. Activation conditions and animation
table entries count as references, so a variable that only gates a section is not
reported as dead.

The same `ReferenceIndex` powers the "Usage" column and "find all references", so
the UI never rescans.

Findings produced: unused global/local variables, unused derived types, unused
DFB types, empty sections, over-long sections, duplicate addresses, undefined
types, undocumented located I/O.

### 4.7 Measured results — real file

```
parsed /mnt/project/MAST.XPG: 891 variables, 75 sections in 4.3 ms

product          : Control Expert V15.3 - 230214C
project          : Projet  v0.0.531  (DTD 41)
CPU              : Modicon M340 BMX P34 2020  firmware 03.30  (inferred)

variables        : 891  (global 251, local/param 355, DDT members 285)
located I/O      : 90
derived types    : 28
DFB types        : 8   instances: 34
program units    : 3
sections         : 75   tasks: 1   animation tables: 5
lines of code    : 10336   statements: 6830
doc coverage     : 2.9 %
parse+analyze    : 9.1 ms

most instantiated DFBs: DFB_GRAFCETENGINE ×17, CAPTEUR ×6, BUILDING ×3,
                        TOUTFERMER ×3, WORD_TO_AREBOOL_INV ×2, …

findings: 9 unused locals, 2 long sections, 90 undocumented located I/O,
          0 unused globals, 0 duplicate addresses  (101 total)

! Rack and module layout is not part of a .XPG export. Import the matching
  .XHW or .XEF file to populate the PLC configuration view.
```

The 2.9 % documentation coverage is not a bug: comments live in the `.XDB`
dictionary, and only DDT member comments survive a program export. It is exactly
the kind of thing the tool should say out loud rather than present as a quality
score.

### 4.8 Measured results — target scale

Generated fixture: 50 000 global variables, 10 500 sections, 500 DFB types,
200 DDTs, 6.2 MB.

```
parsed /tmp/SCALE.XPG: 59000 variables, 10500 sections in 52.0 ms
lines of code    : 82500   statements: 41500
parse+analyze    : 93.5 ms
```

Under 100 ms end to end for a project 12× the size of the reference file, well
inside the brief's 50 000-variable / 10 000-POU target.

---

## 5. Part 4 — User interface

### 5.1 Startup screen

`ListView` of recent projects (path, project name, CPU, last opened), an **Open
project** action, and an **Import MAST** entry that runs the same code path with
a file filter defaulted to `*.XPG;*.XEF;*.XDB;*.XHW`. Drag-and-drop is handled
by the same `FileDropped` input event, so the drop target and the button share
one code path.

### 5.2 Main analysis screen

```
┌────────────────────────────── ToolBar ───────────────────────────────┐
│rail│  Project explorer  │  PLC configuration    │  Variables browser │
│    │  TreeView          │  TabControl:          │  search + scope    │
│    │   ├ Configuration  │   IO Mapping          │  TableView         │
│    │   ├ Derived types  │   Task Configuration  │  Name Type Address │
│    │   ├ DFB types      │   Communication       │  Scope Comment     │
│    │   ├ Program units  │   System Settings     │  Usage             │
│    │   ├ Tasks ─ MAST   │  (PropertyGrid each)  │                    │
│    │   └ Animation tbls │                       │                    │
│    ├────────────────────┴───────────────────────┴────────────────────┤
│    │  DFB library │ DB library │ Programming sections                │
│    ├─────────────────────────────────────────────────────────────────┤
│    │  Diagnostics │ Analysis summary │ Project status                │
└──────────────────────────── StatusBar ───────────────────────────────┘
```

Built with one `DockLayout` (toolbar/status/rail/centre) and nested `Splitter`s.
Pane sizes are stored as ratios with per-pane minimums, so a window resize keeps
the engineer's proportions; the ratios go into the workspace file.

Selecting a tree node drives the other panes through `onTreeSelection` — picking
a DDT filters the variable table to that type, picking a task scopes the sections
table. `MainAnalysisScreen.cpp` is written out in full as the worked example the
other screens follow.

### 5.3 Variable explorer

Full-window `TableView` with the six columns from the brief, search box,
scope dropdown, "unused only" and "located only" toggles, multi-select, and CSV
export **in view order** — an export that ignores the current sort and filter is
useless for a review.

### 5.4 Library explorer

`TreeView` over `LibraryTreeModel` with the family folders from the brief
(Standard / Motion / Process / Communication / Safety / User / Custom), each DFB
showing its version and instance count. Selecting one shows its parameters in a
`PropertyGrid` and its body in a read-only `MultiLineText`.

Everything in the reference file lands under **Custom**, because a `.XPG` does
not record library provenance. Classification into Standard/Motion/Process is a
catalog lookup that arrives with the `.XEF` — flagged, not guessed.

### 5.5 Programming units explorer

`TreeView` of program units → sections, with language and line count in the
label and the activation condition in angle brackets, plus a tabbed read-only
source view with line numbers and a syntax-highlighting hook.

### 5.6 Statistics dashboard

`GridLayout` of cards driven by `AnalysisReport`: counters, a language
breakdown, most-declared types, largest sections, most-instantiated DFBs, a
findings donut by severity, and documentation coverage.

### 5.7 Theme

Palette taken from the mockup rather than invented: a desaturated graphite shell
so that the only saturated pixels on screen are *state* — module OK green,
warning amber, error red, selection blue. In a control room that rule matters
more than taste: colour has to mean something. Three themes ship (Dark, Light,
High contrast); high contrast also enlarges row height for larger hit targets.
Metrics are logical pixels multiplied by the display scale, so one theme serves a
96 dpi panel PC and a 4K laptop.

---

## 6. Part 5 — Code quality

### 6.1 C++20 vs `std::expected`

The brief asks for C++20 **and** `std::expected`. `std::expected` is a C++23
library feature (P0323); no conforming C++20 standard library provides it.
Verified:

```
$ g++ -std=c++20 -fsyntax-only expected_test.cpp
error: 'std::expected' is only available from C++23 onwards
$ g++ -std=c++23 -fsyntax-only expected_test.cpp   # OK
```

`core/Result.hpp` aliases to `std::expected` when `__cpp_lib_expected` is
present and otherwise provides an API-compatible fallback. Application code only
ever names `core::Result` / `core::Err`, so moving the fleet to C++23 shrinks
this header and changes nothing else. `tests/` builds and passes under **both**
dialects.

### 6.2 Design patterns, and what each is actually for

| Pattern | Where | Why there |
|---|---|---|
| **Observer** | `core::Signal` | fine-grained, one known publisher: `Button::clicked` |
| **Publish/Subscribe** | `core::EventBus` | publisher and subscriber must not know each other: the importer publishes `ProjectLoaded`, six unrelated views repopulate |
| **Factory** | `MenuFactory`, `IProjectParser` registry | new screen or new file format = a registration, not a `switch` |
| **Command** | `core::ICommand` + `CommandStack` | every user mutation is undoable; toolbar, menu bar and shortcut resolve to one `ActionId`, so they cannot drift apart |
| **Strategy** | `IProjectParser`, `gfx::IRenderer` | swap the format, swap the backend |
| **MVC** | `app/ViewModels` | `domain` knows nothing about widgets, `ui` knows nothing about PLCs |
| **Dependency injection** | `ServiceRegistry` + constructor injection | wiring lives in `App::create()` only |
| **RAII** | `Connection`, `ScopedInstance`, `unique_ptr` everywhere | lifetime is structural, not a convention |
| **Pimpl** | `SdlRenderer` | SDL headers never leak above `platform/` |

### 6.3 Memory ownership

| Relationship | Held as | Rationale |
|---|---|---|
| `Widget` → children | `unique_ptr` | strict tree, single owner |
| `Widget` → parent | raw `Widget*` | back-reference; a child cannot outlive its parent by construction. This is the **only** raw pointer in the code base, and it owns nothing |
| `MenuManager` → menus | `unique_ptr` in stack entries | popped entry is moved out, then destroyed after the stack no longer references it |
| View model → `Project` | `shared_ptr<const Project>` | a background re-import builds a new `Project` while the old one is still on screen; the swap is one atomic pointer exchange at a frame boundary |
| Domain relationships | 32-bit indices | contiguous, serialisable, stable across reallocation |
| Signal subscription | RAII `Connection` token | a widget can never be called back after its own destruction |
| SDL resources | `unique_ptr` + explicit teardown order in `~App` | menus → widgets → textures → renderer → window → `SDL_Quit()` |

The brief says "no raw pointers". The parent back-pointer is the one deliberate
exception; a `weak_ptr` there would cost an atomic load on every hit test for no
safety gain.

### 6.4 Frame pipeline

```
                     ┌─────────────────────────────┐
                     │      App::run() loop        │
                     └──────────────┬──────────────┘
  1. pumpEvents      SDL_PollEvent ──► translate ──► MenuManager::HandleEvent
  2. bus_.drain()    deferred notifications delivered on the UI thread
  3. applyPending()  queued navigation applied — the only place the stack changes
  4. Update(dt)      logic and animation, top layer down to firstUpdatedLayer()
  5. beginFrame()    clear, reset clip stack
  6. Render()        layers back-to-front, then overlays, then tooltips
       ├─ layout()   only subtrees with dirtyLayout_
       └─ onPaint()  only subtrees with dirtyPaint_, clipped to bounds
  7. endFrame()      present (vsync)
```

Fixed order, every frame. Steps 2 and 3 exist so no view is ever mutated in the
middle of another view's paint. Rendering is retained and invalidation-driven:
an idle window submits no draw calls, which matters on a fanless panel PC.

### 6.5 Threading

One UI thread. Import and analysis run on a worker via `std::async`; progress
and completion cross back through `EventBus::publish()`, which is mutex-guarded
and delivered on the UI thread during `drain()`. The `Project` is never mutated
after publication — views hold `shared_ptr<const Project>` — so there is no
shared mutable state and no lock in the render path.

### 6.6 Tests

```
$ ./core_test         # Result under C++20 and C++23, Signal, EventBus, CommandStack
$ ./menu_test         # nesting, pop/replace/switch/previous, canClose veto,
                      # modal vs overlay input, dialog results, deferred mutation
$ ./import_test       # end-to-end on a real export; asserts 28 DDTs, 8 DFB types,
                      # CPU identity, numeric address ordering, partial-data notice
$ ./viewmodel_test    # MVC seam, sort/filter benchmarks, tree shape, property grid
```

All four run headless. `main --cli` returns a non-zero exit code when the
analysis finds an error-severity issue, so the analyzer drops into a build
pipeline as a gate.

---

## 7. Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`-DXPG_WITH_SDL=OFF` builds the import library, the tests and the `--cli` front
end with no SDL dependency at all — useful for CI containers and for running the
analyzer on a build server.

---

## 8. Status and what comes next

**Everything in `src/` compiles and links.** Verified with GCC 13 under both
`-std=c++20` and `-std=c++23` with `-Wall -Wextra`, against the real SDL3 3.2
headers, and linked end to end. All four tests pass, including an import of a
genuine Control Expert export and a generated fixture at 59 000 variables and
10 500 sections. A Visual Studio 2022 solution is included and configured with
`/std:c++20 /permissive- /W4 /utf-8`.

**Two deliberate first-milestone simplifications, both isolated to one file:**

*Text rendering.* `SdlRenderer` draws with `SDL_RenderDebugText`, the 8x8 bitmap
font built into SDL3, scaled to the size carried by `FontId`. That removes the
SDL_ttf dependency entirely — the project builds with nothing but SDL3 on the
include path — and `measure()`, `lineHeight()` and `fitCharacters()` all agree
with what is actually drawn, so layout is honest. Swapping in a real glyph atlas
touches `platform/SdlRenderer.cpp` and nothing else.

*Missing parsers.* `XdbParser` and `XhwParser` return
`ErrorCode::NotImplemented` with a message naming what is missing, rather than
silently producing an empty model.

**Ordered by value:**

1. `.XHW` / `.XEF` parsing — it is the difference between an empty PLC
   configuration pane and the one in the mockup.
2. A real glyph atlas (SDL_ttf or a packed bitmap font). The debug font is
   fixed-pitch and coarse; every metric call already routes through one place,
   so this is a contained change.
3. Graphical language rendering (LD/FBD/SFC). The reference file is all ST, but
   most Control Expert projects are not, and those bodies are structured XML
   graphs rather than text.
4. A cached binary project format. Re-importing 6 MB in 52 ms is fine; a 200 MB
   plant project is not, and the index-based domain model serialises directly.
5. Cross-project comparison — two `Project` objects and a diff of the symbol
   tables is a small step from here, and it is what an integrator actually wants
   when a machine comes back from site with a modified program.
