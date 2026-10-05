// =============================================================================
//  app/hmi/HmiIcons.hpp - les pictogrammes de l'editeur de vues
// -----------------------------------------------------------------------------
//  Dessines en primitives, comme ui/Icons : pas d'image a livrer, net a toutes
//  les tailles, et la couleur suit le theme. A part de ui::Icon parce que ce
//  sont les outils d'un editeur de dessin (aligner, retourner, calques...),
//  que le reste de l'application n'utilise pas.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"
#include "../../platform/Renderer.hpp"

#include <cstdint>

namespace app {

enum class HmiGlyph : std::uint8_t {
    None,
    Select, Pan,
    // les objets
    Text, Image, Button, Rectangle, Ellipse, Line, Polygon, Indicator, ProgressBar, Gauge,
    Table, History, Trend, List, Video, Container, Group,
    // l'edition
    Ungroup, Front, Back, Forward, Backward,
    AlignLeft, AlignCenterH, AlignRight, AlignTop, AlignCenterV, AlignBottom,
    DistributeH, DistributeV, RotateLeft, RotateRight, MirrorH, MirrorV,
    Lock, Unlock, Eye, EyeOff, Grid, Magnet, Guide,
    ZoomIn, ZoomOut, ZoomFit, Copy, Paste, Duplicate, Delete, StyleCopy, StylePaste,
    Layer, LayerAdd, LayerRemove, Up, Down, Star, StarFilled, Search, View, Plus,
    Undo, Redo, Code,
    // les ressources
    Play, Stop, Link,
    // lot 4 : alarmes, utilisateurs, recettes, echange
    Bell, User, Key, Import, Export, Compare, Check, Refresh,
    // lot 6 : gestion de recettes, image animee, modeles de vue, ressources
    RecipeManager, AnimatedImage, Template, Sound, System,
    // lot 8 : saisie, utilisateurs, popups, aide
    InputField, Login, Logout, UserInfo, Password, Users, Keyboard, Popup, Help,
    // lot 9 : commandes et afficheurs ; les variables systeme et d'instances
    PushButton, Switch, IlluminatedButton, Selector, Slider, Knob, ComboBox, CheckBox, RadioGroup,
    DateTimePicker, WeeklySchedule, NumericDisplay, MultiStateIndicator, MultiStateText, Bargraph,
    Thermometer, Dial, Clock, HourMeter, SevenSegment, TrendArrow, Marquee, QrCode,
    SystemVars, InstanceVars,
    // lot 10 : le menu natif Parametres systeme (et son objet) ; les symboles de synoptique
    Gear,
    Valve, Pump, Motor, Pipe, Tank, GasBottle, Fan, Compressor, HeatExchanger, Filter, Boiler, Conveyor,
    Cylinder, IsaInstrument, CircuitBreaker, Disconnector, Contactor, Lamp, Transformer, Silo, Hopper, Mixer, CheckValve, FlowArrow,
    // lot 10 : les symboles reutilisables (l'instance, Creer un symbole, la section de la bibliotheque)
    Symbol,
    // lot 11 : graphiques, objets des alarmes et de la production
    BarChart, XYChart, StateChart, PieChart, RadarChart, Histogram,
    AlarmBanner, AlarmCounter, AlarmSummary, AlarmInstruction, AlarmStats,
    ProductionCounter, VariableTable, RecipeEditor, Shelve, Mute,
    // lot 12 : navigation et structure ; les styles nommes
    NavBar, Breadcrumb, TabContainer, Frame, ScrollPanel, CollapsiblePanel, ZoneMap, Style,
    // lot 12 : le menu natif de connexion (et son objet)
    LoginMenu,
    // lot 13 : la signature electronique, le badge, le journal d'audit ; les langues
    Signature, Badge, Audit, Language, Theme,
    // lot 14 : la communication, le poste d'exploitation, les notifications, les rapports, le web
    Network, Diagnostic, Station, Mail, Report, Web,
    // lot 16 : le GIF anime ; les types IHM (structures)
    Gif, Structure,
    // 1.9 : la lecture cyclique (Figer l'affichage)
    Pause,
    // 1.10.4 : la vanne 3 voies
    ThreeWayValve,
};

[[nodiscard]] HmiGlyph glyphFor(hmi::Kind) noexcept;
void drawHmiGlyph(gfx::IRenderer&, HmiGlyph, const gfx::Rect& box, gfx::Color);

} // namespace app
