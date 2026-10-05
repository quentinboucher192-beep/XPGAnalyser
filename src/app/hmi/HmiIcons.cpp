#include "HmiIcons.hpp"

#include "../../ui/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace app {

namespace shapes = ui::shapes;

HmiGlyph glyphFor(hmi::Kind k) noexcept {
    switch (k) {
        case hmi::Kind::Text:        return HmiGlyph::Text;
        case hmi::Kind::Image:       return HmiGlyph::Image;
        case hmi::Kind::Button:      return HmiGlyph::Button;
        case hmi::Kind::Rectangle:   return HmiGlyph::Rectangle;
        case hmi::Kind::Ellipse:     return HmiGlyph::Ellipse;
        case hmi::Kind::Line:        return HmiGlyph::Line;
        case hmi::Kind::Polygon:     return HmiGlyph::Polygon;
        case hmi::Kind::Indicator:   return HmiGlyph::Indicator;
        case hmi::Kind::ProgressBar: return HmiGlyph::ProgressBar;
        case hmi::Kind::Gauge:       return HmiGlyph::Gauge;
        case hmi::Kind::Table:       return HmiGlyph::Table;
        case hmi::Kind::History:     return HmiGlyph::History;
        case hmi::Kind::Trend:       return HmiGlyph::Trend;
        case hmi::Kind::List:        return HmiGlyph::List;
        case hmi::Kind::Video:       return HmiGlyph::Video;
        case hmi::Kind::Container:   return HmiGlyph::Container;
        case hmi::Kind::Group:       return HmiGlyph::Group;
        case hmi::Kind::RecipeManager: return HmiGlyph::RecipeManager;
        case hmi::Kind::AnimatedImage: return HmiGlyph::AnimatedImage;
        case hmi::Kind::InputField:    return HmiGlyph::InputField;
        case hmi::Kind::LoginPanel:    return HmiGlyph::Login;
        case hmi::Kind::LogoutButton:  return HmiGlyph::Logout;
        case hmi::Kind::UserInfo:      return HmiGlyph::UserInfo;
        case hmi::Kind::PasswordChange: return HmiGlyph::Password;
        case hmi::Kind::UserManager:   return HmiGlyph::Users;
        // lot 9
        case hmi::Kind::PushButton:          return HmiGlyph::PushButton;
        case hmi::Kind::Switch:              return HmiGlyph::Switch;
        case hmi::Kind::IlluminatedButton:   return HmiGlyph::IlluminatedButton;
        case hmi::Kind::Selector:            return HmiGlyph::Selector;
        case hmi::Kind::Slider:              return HmiGlyph::Slider;
        case hmi::Kind::Knob:                return HmiGlyph::Knob;
        case hmi::Kind::ComboBox:            return HmiGlyph::ComboBox;
        case hmi::Kind::CheckBox:            return HmiGlyph::CheckBox;
        case hmi::Kind::RadioGroup:          return HmiGlyph::RadioGroup;
        case hmi::Kind::DateTimePicker:      return HmiGlyph::DateTimePicker;
        case hmi::Kind::WeeklySchedule:      return HmiGlyph::WeeklySchedule;
        case hmi::Kind::NumericDisplay:      return HmiGlyph::NumericDisplay;
        case hmi::Kind::MultiStateIndicator: return HmiGlyph::MultiStateIndicator;
        case hmi::Kind::MultiStateText:      return HmiGlyph::MultiStateText;
        case hmi::Kind::Bargraph:            return HmiGlyph::Bargraph;
        case hmi::Kind::Thermometer:         return HmiGlyph::Thermometer;
        case hmi::Kind::Dial:                return HmiGlyph::Dial;
        case hmi::Kind::Clock:               return HmiGlyph::Clock;
        case hmi::Kind::HourMeter:           return HmiGlyph::HourMeter;
        case hmi::Kind::SevenSegment:        return HmiGlyph::SevenSegment;
        case hmi::Kind::TrendArrow:          return HmiGlyph::TrendArrow;
        case hmi::Kind::Marquee:             return HmiGlyph::Marquee;
        case hmi::Kind::QrCode:              return HmiGlyph::QrCode;
        case hmi::Kind::SystemButton:        return HmiGlyph::Gear;
        case hmi::Kind::Valve:                return HmiGlyph::Valve;
        case hmi::Kind::Pump:                 return HmiGlyph::Pump;
        case hmi::Kind::Motor:                return HmiGlyph::Motor;
        case hmi::Kind::Pipe:                 return HmiGlyph::Pipe;
        case hmi::Kind::Tank:                 return HmiGlyph::Tank;
        case hmi::Kind::GasBottle:            return HmiGlyph::GasBottle;
        case hmi::Kind::Fan:                  return HmiGlyph::Fan;
        case hmi::Kind::Compressor:           return HmiGlyph::Compressor;
        case hmi::Kind::HeatExchanger:        return HmiGlyph::HeatExchanger;
        case hmi::Kind::Filter:               return HmiGlyph::Filter;
        case hmi::Kind::Boiler:               return HmiGlyph::Boiler;
        case hmi::Kind::Conveyor:             return HmiGlyph::Conveyor;
        case hmi::Kind::Cylinder:             return HmiGlyph::Cylinder;
        case hmi::Kind::IsaInstrument:        return HmiGlyph::IsaInstrument;
        case hmi::Kind::CircuitBreaker:       return HmiGlyph::CircuitBreaker;
        case hmi::Kind::Disconnector:         return HmiGlyph::Disconnector;
        case hmi::Kind::Contactor:            return HmiGlyph::Contactor;
        case hmi::Kind::Lamp:                 return HmiGlyph::Lamp;
        case hmi::Kind::Transformer:          return HmiGlyph::Transformer;
        case hmi::Kind::Silo:                 return HmiGlyph::Silo;
        case hmi::Kind::Hopper:               return HmiGlyph::Hopper;
        case hmi::Kind::Mixer:                return HmiGlyph::Mixer;
        case hmi::Kind::CheckValve:           return HmiGlyph::CheckValve;
        case hmi::Kind::FlowArrow:            return HmiGlyph::FlowArrow;
        case hmi::Kind::SymbolInstance:      return HmiGlyph::Symbol;
        // lot 11
        case hmi::Kind::BarChart:             return HmiGlyph::BarChart;
        case hmi::Kind::XYChart:              return HmiGlyph::XYChart;
        case hmi::Kind::StateChart:           return HmiGlyph::StateChart;
        case hmi::Kind::PieChart:             return HmiGlyph::PieChart;
        case hmi::Kind::RadarChart:           return HmiGlyph::RadarChart;
        case hmi::Kind::Histogram:            return HmiGlyph::Histogram;
        case hmi::Kind::AlarmBanner:          return HmiGlyph::AlarmBanner;
        case hmi::Kind::AlarmCounter:         return HmiGlyph::AlarmCounter;
        case hmi::Kind::AlarmSummary:         return HmiGlyph::AlarmSummary;
        case hmi::Kind::AlarmInstruction:     return HmiGlyph::AlarmInstruction;
        case hmi::Kind::AlarmStats:           return HmiGlyph::AlarmStats;
        case hmi::Kind::ProductionCounter:    return HmiGlyph::ProductionCounter;
        case hmi::Kind::VariableTable:        return HmiGlyph::VariableTable;
        case hmi::Kind::RecipeEditor:         return HmiGlyph::RecipeEditor;
        case hmi::Kind::ExportButton:         return HmiGlyph::Export;
        // lot 12
        case hmi::Kind::NavBar:               return HmiGlyph::NavBar;
        case hmi::Kind::Breadcrumb:           return HmiGlyph::Breadcrumb;
        case hmi::Kind::TabContainer:         return HmiGlyph::TabContainer;
        case hmi::Kind::Frame:                return HmiGlyph::Frame;
        case hmi::Kind::ScrollPanel:          return HmiGlyph::ScrollPanel;
        case hmi::Kind::CollapsiblePanel:     return HmiGlyph::CollapsiblePanel;
        case hmi::Kind::ZoneMap:              return HmiGlyph::ZoneMap;
        case hmi::Kind::LoginMenuButton:      return HmiGlyph::LoginMenu;
        case hmi::Kind::LanguageSelector:     return HmiGlyph::Language;      // lot 13
        case hmi::Kind::ThemeSelector:        return HmiGlyph::Theme;
        case hmi::Kind::CommStatus:           return HmiGlyph::Network;       // lot 14
        case hmi::Kind::PlcDiagnostic:        return HmiGlyph::Diagnostic;
        case hmi::Kind::AnimatedGif:          return HmiGlyph::Gif;           // lot 16
        case hmi::Kind::ThreeWayValve:        return HmiGlyph::ThreeWayValve; // 1.10.4
    }
    return HmiGlyph::None;
}

void drawHmiGlyph(gfx::IRenderer& r, HmiGlyph g, const gfx::Rect& b, gfx::Color c) {
    // Tout est trace dans un carre de 16 unites, mis a l'echelle de la boite.
    const float s = std::min(b.w, b.h) / 16.f;
    const float ox = b.x + (b.w - 16.f * s) / 2, oy = b.y + (b.h - 16.f * s) / 2;
    auto P = [&](float x, float y) { return gfx::Point{ox + x * s, oy + y * s}; };
    auto L = [&](float x0, float y0, float x1, float y1, float t = 1.3f) { r.line(P(x0, y0), P(x1, y1), c, std::max(1.f, t * s)); };
    auto R = [&](float x, float y, float w, float h) { r.strokeRect({ox + x * s, oy + y * s, w * s, h * s}, c, std::max(1.f, 1.2f * s)); };
    auto F = [&](float x, float y, float w, float h, gfx::Color col) { r.fillRect({ox + x * s, oy + y * s, w * s, h * s}, col); };
    auto poly = [&](std::initializer_list<gfx::Point> pts, bool fill) {
        std::vector<gfx::Point> v;
        for (auto p : pts) v.push_back(P(p.x, p.y));
        if (fill) shapes::fillPolygon(r, v, c);
        else shapes::strokePolyline(r, v, true, c, std::max(1.f, 1.2f * s));
    };
    auto circle = [&](float cx, float cy, float rad, bool fill) {
        auto v = shapes::ellipse(P(cx, cy), rad * s, rad * s, 24);
        if (fill) shapes::fillPolygon(r, v, c);
        else shapes::strokePolyline(r, v, true, c, std::max(1.f, 1.2f * s));
    };
    const gfx::Color soft = c.withAlpha(static_cast<std::uint8_t>(c.a * 0.45f));

    switch (g) {
        case HmiGlyph::None: break;
        case HmiGlyph::Select:      poly({{3, 2}, {12, 9}, {8, 9.5f}, {10, 14}, {8.5f, 14.6f}, {6.5f, 10.2f}, {3, 13}}, true); break;
        case HmiGlyph::Pan:         L(8, 2, 8, 14); L(2, 8, 14, 8); poly({{8, 1}, {6, 3.5f}, {10, 3.5f}}, true); poly({{8, 15}, {6, 12.5f}, {10, 12.5f}}, true); break;
        case HmiGlyph::Text:        L(3, 3, 13, 3, 1.8f); L(8, 3, 8, 13, 1.8f); L(6, 13, 10, 13); break;
        case HmiGlyph::Image:       R(2, 3, 12, 10); poly({{3, 12}, {7, 7}, {10, 10}, {11.5f, 8.5f}, {13, 12}}, true); circle(11, 5.5f, 1.2f, true); break;
        case HmiGlyph::Button:      R(1.5f, 4.5f, 13, 7); L(5, 8, 11, 8); break;
        case HmiGlyph::Rectangle:   R(2, 4, 12, 8); break;
        case HmiGlyph::Ellipse:     circle(8, 8, 5.5f, false); break;
        case HmiGlyph::Line:        L(2, 13, 14, 3, 1.6f); break;
        case HmiGlyph::Polygon:     poly({{8, 2}, {14, 7}, {11.5f, 14}, {4.5f, 14}, {2, 7}}, false); break;
        case HmiGlyph::Indicator:   circle(8, 8, 5.5f, false); circle(8, 8, 3, true); break;
        case HmiGlyph::ProgressBar: R(1.5f, 5.5f, 13, 5); F(2.5f, 6.5f, 7, 3, c); break;
        case HmiGlyph::Gauge: {
            auto a = shapes::arc(P(8, 9.5f), 6 * s, 150, 390, 20);
            shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.4f * s));
            L(8, 9.5f, 11, 5.5f, 1.4f);
            break;
        }
        case HmiGlyph::Table:       R(2, 3, 12, 10); L(2, 6, 14, 6); L(2, 9.5f, 14, 9.5f); L(6, 3, 6, 13); break;
        case HmiGlyph::History:     circle(8, 8, 5.5f, false); L(8, 8, 8, 4.5f); L(8, 8, 10.5f, 9.5f); break;
        case HmiGlyph::Trend:       L(2, 14, 14, 14); L(2, 2, 2, 14); poly({{3, 11}, {6, 7}, {9, 9}, {13, 3}}, false); break;
        case HmiGlyph::List:        for (int k = 0; k < 3; ++k) { F(2, 3.5f + k * 4, 2, 2, c); L(5.5f, 4.5f + k * 4, 14, 4.5f + k * 4); } break;
        case HmiGlyph::Video:       R(1.5f, 3.5f, 13, 9); poly({{6.5f, 5.5f}, {10.5f, 8}, {6.5f, 10.5f}}, true); break;
        case HmiGlyph::Container:   R(2, 2, 12, 12); L(2, 5, 14, 5); break;
        case HmiGlyph::Group:       R(1.5f, 1.5f, 8, 7); R(6.5f, 7.5f, 8, 7); break;
        case HmiGlyph::Ungroup:     R(1.5f, 1.5f, 6, 6); R(8.5f, 8.5f, 6, 6); L(9, 3, 13, 3); L(3, 13, 7, 13); break;
        case HmiGlyph::Front:       F(6, 6, 8, 8, c); R(2, 2, 8, 8); break;
        case HmiGlyph::Back:        R(6, 6, 8, 8); F(2, 2, 8, 8, soft); R(2, 2, 8, 8); break;
        case HmiGlyph::Forward:     R(2, 7, 7, 7); poly({{11, 2}, {14.5f, 6}, {7.5f, 6}}, true); L(11, 6, 11, 11); break;
        case HmiGlyph::Backward:    R(2, 2, 7, 7); poly({{11, 14}, {14.5f, 10}, {7.5f, 10}}, true); L(11, 10, 11, 5); break;
        case HmiGlyph::AlignLeft:   L(2, 1.5f, 2, 14.5f); F(3.5f, 3, 9, 3.5f, c); F(3.5f, 9.5f, 6, 3.5f, c); break;
        case HmiGlyph::AlignCenterH:L(8, 1.5f, 8, 14.5f); F(3, 3, 10, 3.5f, c); F(4.5f, 9.5f, 7, 3.5f, c); break;
        case HmiGlyph::AlignRight:  L(14, 1.5f, 14, 14.5f); F(3.5f, 3, 9, 3.5f, c); F(6.5f, 9.5f, 6, 3.5f, c); break;
        case HmiGlyph::AlignTop:    L(1.5f, 2, 14.5f, 2); F(3, 3.5f, 3.5f, 9, c); F(9.5f, 3.5f, 3.5f, 6, c); break;
        case HmiGlyph::AlignCenterV:L(1.5f, 8, 14.5f, 8); F(3, 3, 3.5f, 10, c); F(9.5f, 4.5f, 3.5f, 7, c); break;
        case HmiGlyph::AlignBottom: L(1.5f, 14, 14.5f, 14); F(3, 3.5f, 3.5f, 9, c); F(9.5f, 6.5f, 3.5f, 6, c); break;
        case HmiGlyph::DistributeH: L(1.5f, 2, 1.5f, 14); L(14.5f, 2, 14.5f, 14); F(3.5f, 5, 3, 6, c); F(9.5f, 5, 3, 6, c); break;
        case HmiGlyph::DistributeV: L(2, 1.5f, 14, 1.5f); L(2, 14.5f, 14, 14.5f); F(5, 3.5f, 6, 3, c); F(5, 9.5f, 6, 3, c); break;
        case HmiGlyph::RotateLeft: {
            auto a = shapes::arc(P(8, 8.5f), 5.5f * s, 200, 470, 20);
            shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.4f * s));
            poly({{1.5f, 5}, {5.5f, 5.5f}, {3, 9}}, true);
            break;
        }
        case HmiGlyph::RotateRight: {
            auto a = shapes::arc(P(8, 8.5f), 5.5f * s, 70, 340, 20);
            shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.4f * s));
            poly({{14.5f, 5}, {10.5f, 5.5f}, {13, 9}}, true);
            break;
        }
        case HmiGlyph::MirrorH:     L(8, 1, 8, 15, 1.0f); poly({{6.5f, 3}, {6.5f, 13}, {1.5f, 13}}, true); poly({{9.5f, 3}, {9.5f, 13}, {14.5f, 13}}, false); break;
        case HmiGlyph::MirrorV:     L(1, 8, 15, 8, 1.0f); poly({{3, 6.5f}, {13, 6.5f}, {13, 1.5f}}, true); poly({{3, 9.5f}, {13, 9.5f}, {13, 14.5f}}, false); break;
        case HmiGlyph::Lock:        R(3, 7.5f, 10, 7); { auto a = shapes::arc(P(8, 7.5f), 3.2f * s, 180, 360, 12); shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.3f * s)); } F(7.3f, 10, 1.4f, 2.5f, c); break;
        case HmiGlyph::Unlock:      R(3, 7.5f, 10, 7); { auto a = shapes::arc(P(11, 5.5f), 3.2f * s, 180, 330, 12); shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.3f * s)); } break;
        case HmiGlyph::Eye: {
            poly({{1, 8}, {4.5f, 4.5f}, {8, 3.5f}, {11.5f, 4.5f}, {15, 8}, {11.5f, 11.5f}, {8, 12.5f}, {4.5f, 11.5f}}, false);
            circle(8, 8, 2.2f, true);
            break;
        }
        case HmiGlyph::EyeOff:
            poly({{1, 8}, {4.5f, 4.5f}, {8, 3.5f}, {11.5f, 4.5f}, {15, 8}, {11.5f, 11.5f}, {8, 12.5f}, {4.5f, 11.5f}}, false);
            L(2.5f, 13.5f, 13.5f, 2.5f, 1.6f);
            break;
        case HmiGlyph::Grid:
            for (int k = 0; k < 4; ++k) {
                const float o = 2.f + 4.f * static_cast<float>(k);
                L(o, 2, o, 14, 0.9f);
                L(2, o, 14, o, 0.9f);
            }
            break;
        case HmiGlyph::Magnet: {
            auto a = shapes::arc(P(8, 8), 5 * s, 0, 180, 14);
            shapes::strokePolyline(r, a, false, c, std::max(1.f, 2.4f * s));
            L(3, 8, 3, 3, 2.4f); L(13, 8, 13, 3, 2.4f);
            F(1.8f, 2, 2.4f, 2, soft); F(11.8f, 2, 2.4f, 2, soft);
            break;
        }
        case HmiGlyph::Guide:       L(5, 1, 5, 15, 1.0f); L(1, 10, 15, 10, 1.0f); break;
        case HmiGlyph::ZoomIn:      circle(7, 7, 4.5f, false); L(10.3f, 10.3f, 14, 14, 1.8f); L(5, 7, 9, 7); L(7, 5, 7, 9); break;
        case HmiGlyph::ZoomOut:     circle(7, 7, 4.5f, false); L(10.3f, 10.3f, 14, 14, 1.8f); L(5, 7, 9, 7); break;
        case HmiGlyph::ZoomFit:     L(2, 5, 2, 2); L(2, 2, 5, 2); L(11, 2, 14, 2); L(14, 2, 14, 5); L(14, 11, 14, 14); L(14, 14, 11, 14); L(5, 14, 2, 14); L(2, 14, 2, 11); R(5, 5, 6, 6); break;
        case HmiGlyph::Copy:        R(2, 4, 8, 10); R(6, 2, 8, 10); break;
        case HmiGlyph::Paste:       R(3, 3, 10, 12); F(5.5f, 1.5f, 5, 3, c); break;
        case HmiGlyph::Duplicate:   R(2, 2, 8, 8); F(6, 6, 8, 8, soft); R(6, 6, 8, 8); L(10, 8, 10, 12); L(8, 10, 12, 10); break;
        case HmiGlyph::Delete:      L(3, 4, 13, 4); R(4.5f, 4, 7, 10); L(6.5f, 2.5f, 9.5f, 2.5f); L(7, 6.5f, 7, 11.5f, 1.0f); L(9, 6.5f, 9, 11.5f, 1.0f); break;
        case HmiGlyph::StyleCopy:   R(2, 2, 9, 5); L(11, 4.5f, 13.5f, 4.5f); L(13.5f, 4.5f, 13.5f, 9); L(13.5f, 9, 7.5f, 9); F(6.5f, 9, 2, 5.5f, c); break;
        case HmiGlyph::StylePaste:  R(2, 2, 9, 5); F(3, 3, 7, 3, soft); L(11, 4.5f, 13.5f, 4.5f); L(13.5f, 4.5f, 13.5f, 9); L(13.5f, 9, 7.5f, 9); F(6.5f, 9, 2, 5.5f, c); break;
        case HmiGlyph::Layer:       poly({{8, 2}, {14.5f, 5.5f}, {8, 9}, {1.5f, 5.5f}}, false); L(1.5f, 9, 8, 12.5f); L(8, 12.5f, 14.5f, 9); break;
        case HmiGlyph::LayerAdd:    poly({{7, 2}, {12.5f, 5}, {7, 8}, {1.5f, 5}}, false); L(12, 10, 12, 15); L(9.5f, 12.5f, 14.5f, 12.5f); break;
        case HmiGlyph::LayerRemove: poly({{7, 2}, {12.5f, 5}, {7, 8}, {1.5f, 5}}, false); L(9.5f, 12.5f, 14.5f, 12.5f); break;
        case HmiGlyph::Up:          poly({{8, 3}, {13, 9}, {3, 9}}, true); L(8, 9, 8, 13, 1.8f); break;
        case HmiGlyph::Down:        poly({{8, 13}, {13, 7}, {3, 7}}, true); L(8, 7, 8, 3, 1.8f); break;
        case HmiGlyph::Star:
        case HmiGlyph::StarFilled: {
            std::vector<gfx::Point> v;
            for (int k = 0; k < 10; ++k) {
                const float a = (-90.f + 36.f * static_cast<float>(k)) * 3.14159265f / 180.f;
                const float rad = (k % 2 == 0) ? 6.5f : 2.8f;
                v.push_back(P(8 + rad * std::cos(a), 8.5f + rad * std::sin(a)));
            }
            if (g == HmiGlyph::StarFilled) shapes::fillPolygon(r, v, c);
            else shapes::strokePolyline(r, v, true, c, std::max(1.f, 1.1f * s));
            break;
        }
        case HmiGlyph::Search:      circle(7, 7, 4.5f, false); L(10.3f, 10.3f, 14, 14, 1.8f); break;
        case HmiGlyph::View:        R(1.5f, 2.5f, 13, 11); L(1.5f, 5, 14.5f, 5); F(3, 7, 4, 5, soft); break;
        case HmiGlyph::Plus:        L(8, 3, 8, 13, 1.8f); L(3, 8, 13, 8, 1.8f); break;
        case HmiGlyph::Code:        // </> : compiler
            L(5, 4, 1.5f, 8, 1.6f); L(1.5f, 8, 5, 12, 1.6f);
            L(11, 4, 14.5f, 8, 1.6f); L(14.5f, 8, 11, 12, 1.6f);
            L(9.5f, 2.5f, 6.5f, 13.5f, 1.4f);
            break;
        case HmiGlyph::Play:        poly({{4, 2.5f}, {13.5f, 8}, {4, 13.5f}}, true); break;
        case HmiGlyph::Stop:        F(3.5f, 3.5f, 9, 9, c); break;
        case HmiGlyph::Link:        // deux maillons
            R(1.5f, 5.5f, 8, 5); R(6.5f, 5.5f, 8, 5); break;
        case HmiGlyph::Bell:        // une cloche : le corps, le bord, le battant
            poly({{8, 2}, {11, 3.5f}, {12, 8}, {13.5f, 11.5f}, {2.5f, 11.5f}, {4, 8}, {5, 3.5f}}, false);
            circle(8, 13.3f, 1.3f, true);
            break;
        case HmiGlyph::User:        // une tete, des epaules
            circle(8, 5, 2.8f, false);
            poly({{2.5f, 14.5f}, {3.5f, 11}, {6, 9.5f}, {10, 9.5f}, {12.5f, 11}, {13.5f, 14.5f}}, false);
            break;
        case HmiGlyph::Key:         // une cle : l'anneau, la tige, deux dents
            circle(4.5f, 8, 2.8f, false);
            L(7.3f, 8, 14.5f, 8, 1.5f); L(12, 8, 12, 11, 1.4f); L(14.3f, 8, 14.3f, 10.5f, 1.4f);
            break;
        case HmiGlyph::Import:      // une fleche qui entre dans le bac
            L(8, 1.5f, 8, 9.5f, 1.6f); poly({{8, 11}, {5, 7.5f}, {11, 7.5f}}, true);
            L(2, 10, 2, 14); L(2, 14, 14, 14); L(14, 14, 14, 10);
            break;
        case HmiGlyph::Export:      // une fleche qui sort du bac
            L(8, 11, 8, 3.5f, 1.6f); poly({{8, 1.5f}, {5, 5}, {11, 5}}, true);
            L(2, 10, 2, 14); L(2, 14, 14, 14); L(14, 14, 14, 10);
            break;
        case HmiGlyph::Compare:     // deux colonnes, des ecarts
            R(1.5f, 2.5f, 5.5f, 11); R(9, 2.5f, 5.5f, 11);
            L(3, 6, 5.5f, 6); L(10.5f, 6, 13, 6); F(10.2f, 8.6f, 3.2f, 1.8f, c);
            break;
        case HmiGlyph::Check:       // une coche
            L(2.5f, 8.5f, 6.5f, 12.5f, 1.8f); L(6.5f, 12.5f, 13.5f, 3.5f, 1.8f);
            break;
        case HmiGlyph::Refresh: {   // un cercle ouvert et sa pointe
            std::vector<gfx::Point> v;
            for (int k = 0; k <= 20; ++k) {
                const float a = (40.f + 280.f * static_cast<float>(k) / 20.f) * 3.14159265f / 180.f;
                v.push_back(P(8 + 5.2f * std::cos(a), 8 + 5.2f * std::sin(a)));
            }
            shapes::strokePolyline(r, v, false, c, std::max(1.f, 1.4f * s));
            poly({{13.5f, 1.5f}, {13.8f, 6.5f}, {9, 5}}, true);
            break;
        }
        case HmiGlyph::Undo:
        case HmiGlyph::Redo: {
            // Un crochet : la fleche repart d'ou l'on vient. Redo est le miroir.
            const bool back = g == HmiGlyph::Undo;
            auto X = [&](float x) { return back ? x : 16.f - x; };
            std::vector<gfx::Point> v{P(X(4.5f), 6), P(X(10), 6), P(X(12.5f), 7.2f), P(X(13.5f), 9),
                                      P(X(12.5f), 10.8f), P(X(10), 12), P(X(6), 12)};
            shapes::strokePolyline(r, v, false, c, std::max(1.f, 1.4f * s));
            poly({{X(1.5f), 6}, {X(5.5f), 3}, {X(5.5f), 9}}, true);
            break;
        }
        case HmiGlyph::RecipeManager:   // un tableau et sa barre de boutons
            R(1.5f, 5, 13, 9); L(1.5f, 8, 14.5f, 8); L(6, 5, 6, 14);
            F(1.5f, 1.5f, 3.5f, 2.2f, c); F(6.2f, 1.5f, 3.5f, 2.2f, c); F(10.9f, 1.5f, 3.5f, 2.2f, soft);
            break;
        case HmiGlyph::AnimatedImage:   // deux images decalees et une fleche de defilement
            R(4.5f, 2, 10, 8); R(1.5f, 5.5f, 10, 8);
            poly({{2.5f, 12.5f}, {5.5f, 9}, {8, 11.5f}, {10.5f, 12.5f}}, true);
            poly({{12, 11.5f}, {15, 13.5f}, {12, 15.5f}}, true);
            break;
        case HmiGlyph::Template:        // une page et le cadre qui la porte
            R(1.5f, 1.5f, 13, 13); F(1.5f, 1.5f, 13, 3, soft); F(1.5f, 11.5f, 13, 3, soft);
            R(4.5f, 6, 7, 4);
            break;
        case HmiGlyph::Sound:           // un haut-parleur et ses ondes
            poly({{2, 6}, {5, 6}, {9, 2.5f}, {9, 13.5f}, {5, 10}, {2, 10}}, true);
            L(11, 5.5f, 12, 8); L(12, 8, 11, 10.5f); L(13, 3.5f, 14.8f, 8); L(14.8f, 8, 13, 12.5f);
            break;
        case HmiGlyph::System:          // un ecran et un engrenage
            R(1.5f, 2, 13, 9); L(6, 14, 10, 14); L(8, 11, 8, 14);
            circle(8, 6.5f, 2.2f, false); L(8, 3.2f, 8, 4.2f); L(8, 8.8f, 8, 9.8f); L(4.7f, 6.5f, 5.7f, 6.5f); L(10.3f, 6.5f, 11.3f, 6.5f);
            break;
        // ---- lot 8
        case HmiGlyph::InputField:      // un champ, du texte, le curseur
            R(1, 4.5f, 14, 7); L(3.5f, 8, 8, 8, 1.2f); L(10, 6, 10, 10, 1.4f); L(9, 6, 11, 6, 1); L(9, 10, 11, 10, 1);
            break;
        case HmiGlyph::Login:           // une porte, et la fleche qui entre
            L(9, 2, 14, 2); L(14, 2, 14, 14); L(14, 14, 9, 14);
            L(1.5f, 8, 9.5f, 8, 1.6f); poly({{11, 8}, {7.5f, 5}, {7.5f, 11}}, true);
            break;
        case HmiGlyph::Logout:          // une porte, et la fleche qui sort
            L(7, 2, 2, 2); L(2, 2, 2, 14); L(2, 14, 7, 14);
            L(5.5f, 8, 13, 8, 1.6f); poly({{14.8f, 8}, {11.2f, 5}, {11.2f, 11}}, true);
            break;
        case HmiGlyph::UserInfo:        // une carte, une tete, des lignes
            R(1, 3, 14, 10); circle(5, 7, 1.8f, false); L(2.8f, 11.5f, 7.2f, 11.5f); L(9, 6.5f, 13, 6.5f); L(9, 9.5f, 12, 9.5f);
            break;
        case HmiGlyph::Password:        // un cadenas et des points
            R(3, 7.5f, 10, 7); { auto a = shapes::arc(P(8, 7.5f), 3.2f * s, 180, 360, 12); shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.3f * s)); }
            circle(5.5f, 11, 0.9f, true); circle(8, 11, 0.9f, true); circle(10.5f, 11, 0.9f, true);
            break;
        case HmiGlyph::Users:           // deux tetes
            circle(5.5f, 5.5f, 2.3f, false); poly({{1, 14}, {2, 10.5f}, {4, 9.5f}, {7, 9.5f}, {9, 10.5f}, {10, 14}}, false);
            circle(11.5f, 4.5f, 2, false); L(10.5f, 8.5f, 13.5f, 8.5f); L(13.5f, 8.5f, 15, 12.5f);
            break;
        case HmiGlyph::Keyboard:        // un clavier
            R(1, 4, 14, 8);
            for (int k = 0; k < 4; ++k) { F(2.5f + k * 3, 5.5f, 1.6f, 1.4f, c); F(2.5f + k * 3, 8, 1.6f, 1.4f, c); }
            L(4.5f, 10.8f, 11.5f, 10.8f);
            break;
        case HmiGlyph::Popup:           // une fenetre au-dessus d'une autre
            R(1, 5, 10, 9); F(4, 2, 11, 3, soft); R(4, 2, 11, 9); L(12.5f, 3.2f, 13.8f, 4.3f); L(13.8f, 3.2f, 12.5f, 4.3f);
            break;
        case HmiGlyph::Help:            // un rond et un point d'interrogation
            circle(8, 8, 6.5f, false);
            { auto a = shapes::arc(P(8, 6.2f), 2.3f * s, 200, 450, 14); shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.4f * s)); }
            L(8, 8.5f, 8, 10, 1.4f); circle(8, 12, 0.9f, true);
            break;
        // ---- lot 9
        case HmiGlyph::PushButton:      // un bouton et son creneau
            R(1.5f, 3.5f, 13, 9);
            { std::vector<gfx::Point> w = {P(3, 10), P(5.5f, 10), P(5.5f, 6), P(10.5f, 6), P(10.5f, 10), P(13, 10)};
              shapes::strokePolyline(r, w, false, c, std::max(1.f, 1.3f * s)); }
            break;
        case HmiGlyph::Switch:          // une glissiere et son bouton
            poly({{4, 5}, {12, 5}, {14.5f, 8}, {12, 11}, {4, 11}, {1.5f, 8}}, false); circle(11, 8, 2.3f, true);
            break;
        case HmiGlyph::IlluminatedButton:   // un bouton qui porte sa lampe
            R(1.5f, 3.5f, 13, 9); circle(8, 8, 2.8f, true); circle(8, 8, 4.3f, false);
            break;
        case HmiGlyph::Selector:        // un bouton rotatif et ses reperes
            circle(8, 9.5f, 4.5f, false); L(8, 9.5f, 5.2f, 6.2f, 1.8f);
            L(2, 5, 3.2f, 6); L(8, 2, 8, 3.5f); L(14, 5, 12.8f, 6);
            break;
        case HmiGlyph::Slider:          // une piste et sa poignee
            L(1.5f, 8, 14.5f, 8, 1.4f); F(1.5f, 7.3f, 7, 1.4f, c); circle(9, 8, 2.6f, true);
            L(2, 12, 2, 13.5f); L(8, 12, 8, 13.5f); L(14, 12, 14, 13.5f);
            break;
        case HmiGlyph::Knob: {          // un bouton tournant, l'arc de sa course
            auto a = shapes::arc(P(8, 8.5f), 6.2f * s, 135, 405, 24);
            shapes::strokePolyline(r, a, false, soft, std::max(1.f, 1.4f * s));
            auto run = shapes::arc(P(8, 8.5f), 6.2f * s, 135, 300, 18);
            shapes::strokePolyline(r, run, false, c, std::max(1.f, 1.6f * s));
            circle(8, 8.5f, 3.2f, false); L(8, 8.5f, 9.8f, 6.2f, 1.4f);
            break;
        }
        case HmiGlyph::ComboBox:        // un champ et sa fleche vers le bas
            R(1, 4.5f, 14, 7); L(10.5f, 5, 10.5f, 11); poly({{11.8f, 7}, {14, 7}, {12.9f, 9.2f}}, true); L(3, 8, 8.5f, 8);
            break;
        case HmiGlyph::CheckBox:        // une case cochee
            R(2.5f, 2.5f, 11, 11);
            { std::vector<gfx::Point> v = {P(4.8f, 8.2f), P(7.2f, 10.8f), P(11.6f, 5.4f)};
              shapes::strokePolyline(r, v, false, c, std::max(1.f, 1.6f * s)); }
            break;
        case HmiGlyph::RadioGroup:      // deux options, la premiere choisie
            circle(4, 5, 2.4f, false); circle(4, 5, 1.1f, true); circle(4, 11.5f, 2.4f, false);
            L(8, 5, 14.5f, 5); L(8, 11.5f, 14.5f, 11.5f);
            break;
        case HmiGlyph::DateTimePicker:  // un calendrier
            R(1.5f, 3, 13, 11.5f); F(1.5f, 3, 13, 2.5f, soft); L(4.5f, 1.5f, 4.5f, 4); L(11.5f, 1.5f, 11.5f, 4);
            for (int k = 0; k < 3; ++k) { F(3.5f + k * 3.5f, 7.5f, 2, 1.8f, c); F(3.5f + k * 3.5f, 10.8f, 2, 1.8f, c); }
            break;
        case HmiGlyph::WeeklySchedule:  // une grille de la semaine, des plages
            R(1, 3, 14, 10);
            for (int k = 1; k < 4; ++k) L(1 + k * 3.5f, 3, 1 + k * 3.5f, 13, 0.8f);
            F(2, 4.5f, 5.5f, 1.8f, c); F(5.5f, 7.6f, 7, 1.8f, c); F(2, 10.6f, 9, 1.8f, c);
            break;
        case HmiGlyph::NumericDisplay:  // un afficheur et sa valeur
            R(1, 4, 14, 8); L(6, 6, 6, 10, 1.4f); L(9, 6, 11, 6, 1.4f); L(11, 6, 11, 10, 1.4f); L(9, 10, 11, 10, 1.4f);
            L(9, 8, 11, 8, 1.4f); L(3, 10, 3.5f, 10, 1.4f);
            break;
        case HmiGlyph::MultiStateIndicator:   // trois voyants, un allume
            circle(3.5f, 8, 2.2f, false); circle(8, 8, 2.4f, true); circle(12.5f, 8, 2.2f, false);
            break;
        case HmiGlyph::MultiStateText:  // des libelles, et le choisi
            L(2, 4.5f, 14, 4.5f, 0.9f); L(2, 8, 14, 8, 2); L(2, 11.5f, 14, 11.5f, 0.9f);
            break;
        case HmiGlyph::Bargraph:        // une barre graduee, ses zones
            R(4, 1.5f, 5, 13); F(4, 8, 5, 6.5f, c); L(11, 2, 11, 14, 1.4f); L(11, 5, 13, 5); L(11, 8, 13, 8); L(11, 11, 13, 11);
            break;
        case HmiGlyph::Thermometer:     // un tube et son bulbe
            R(6.5f, 1.5f, 3, 9.5f); F(7.3f, 6, 1.4f, 5.5f, c); circle(8, 12.5f, 2.6f, true);
            L(10.5f, 3.5f, 12, 3.5f); L(10.5f, 6.5f, 12, 6.5f); L(10.5f, 9.5f, 12, 9.5f);
            break;
        case HmiGlyph::Dial: {          // un cadran, ses zones, son aiguille
            circle(8, 8.5f, 6.5f, false);
            auto a = shapes::arc(P(8, 8.5f), 5 * s, 300, 405, 12);
            shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.8f * s));
            L(8, 8.5f, 5, 5.5f, 1.4f); circle(8, 8.5f, 1, true);
            break;
        }
        case HmiGlyph::Clock:           // une horloge
            circle(8, 8, 6.5f, false); L(8, 8, 8, 4, 1.4f); L(8, 8, 11, 9.5f, 1.4f);
            break;
        case HmiGlyph::HourMeter:       // un sablier
            L(3.5f, 2, 12.5f, 2, 1.4f); L(3.5f, 14, 12.5f, 14, 1.4f);
            poly({{4.5f, 2.5f}, {11.5f, 2.5f}, {8, 8}}, false); poly({{4.5f, 13.5f}, {11.5f, 13.5f}, {8, 8}}, true);
            break;
        case HmiGlyph::SevenSegment:    // un 8 en segments
            L(5, 2.5f, 11, 2.5f, 1.6f); L(5, 8, 11, 8, 1.6f); L(5, 13.5f, 11, 13.5f, 1.6f);
            L(4, 3.5f, 4, 7, 1.6f); L(12, 3.5f, 12, 7, 1.6f); L(4, 9, 4, 12.5f, 1.6f); L(12, 9, 12, 12.5f, 1.6f);
            circle(14, 13.5f, 0.9f, true);
            break;
        case HmiGlyph::TrendArrow:      // une fleche qui monte
            L(3, 13, 12, 4, 1.8f); poly({{13.5f, 2.5f}, {13.5f, 8}, {8, 2.5f}}, true);
            break;
        case HmiGlyph::Marquee:         // un bandeau, un texte qui passe
            R(1, 4.5f, 14, 7); L(5, 8, 13, 8, 1.2f); poly({{2.2f, 8}, {4.5f, 6.3f}, {4.5f, 9.7f}}, true);
            break;
        case HmiGlyph::QrCode:          // trois reperes et des modules
            R(1.5f, 1.5f, 5, 5); R(9.5f, 1.5f, 5, 5); R(1.5f, 9.5f, 5, 5);
            F(3.2f, 3.2f, 1.6f, 1.6f, c); F(11.2f, 3.2f, 1.6f, 1.6f, c); F(3.2f, 11.2f, 1.6f, 1.6f, c);
            F(9.5f, 9.5f, 2, 2, c); F(12.5f, 12.5f, 2, 2, c); F(12.5f, 9.5f, 2, 2, c);
            break;
        case HmiGlyph::SystemVars:      // un engrenage et "SYS"
            circle(8, 8, 3, false);
            for (int k = 0; k < 8; ++k) {
                const float a = static_cast<float>(k) * 0.785398f;
                L(8 + 4.2f * std::cos(a), 8 + 4.2f * std::sin(a), 8 + 6.2f * std::cos(a), 8 + 6.2f * std::sin(a), 1.8f);
            }
            break;
        case HmiGlyph::InstanceVars:    // une vue, un objet, ses proprietes
            R(1, 2, 9, 7); F(2.5f, 3.5f, 3, 2, c); L(9, 9, 12, 12); L(11, 12, 15, 12); L(11, 14.5f, 15, 14.5f);
            break;
        case HmiGlyph::Gear:            // un engrenage plein, son moyeu
            for (int k = 0; k < 8; ++k) {
                const float a = static_cast<float>(k) * 0.785398f + 0.3927f;
                L(8 + 3.8f * std::cos(a), 8 + 3.8f * std::sin(a), 8 + 6.6f * std::cos(a), 8 + 6.6f * std::sin(a), 2.6f);
            }
            circle(8, 8, 4.6f, true);
            {
                const auto hub = shapes::ellipse(P(8, 8), 1.9f * s, 1.9f * s, 16);
                shapes::fillPolygon(r, hub, gfx::Color{20, 24, 30, c.a});
            }
            break;
        // ---- lot 10 : les symboles de synoptique
        case HmiGlyph::Valve:           // deux triangles, la tige, le volant
            poly({{2, 5}, {2, 11}, {8, 8}}, true); poly({{14, 5}, {14, 11}, {8, 8}}, true); L(8, 8, 8, 3); L(5.5f, 3, 10.5f, 3);
            break;
        case HmiGlyph::Pump:            // le corps, la roue, le refoulement
            circle(7, 9.5f, 5, false); poly({{5, 7}, {5, 12}, {9.5f, 9.5f}}, true); L(9, 4.5f, 14.5f, 4.5f); L(14.5f, 4.5f, 14.5f, 7);
            break;
        case HmiGlyph::Motor:           // un cercle, un M
            circle(8, 8, 5.5f, false); L(5.5f, 10.5f, 5.5f, 5.5f); L(5.5f, 5.5f, 8, 8.5f); L(8, 8.5f, 10.5f, 5.5f); L(10.5f, 5.5f, 10.5f, 10.5f);
            break;
        case HmiGlyph::Pipe:            // un tube, le fluide
            R(1, 6, 14, 4); F(3, 7.4f, 3, 1.2f, c); F(8, 7.4f, 3, 1.2f, c); F(13, 7.4f, 1.5f, 1.2f, c);
            break;
        case HmiGlyph::Tank:            // une cuve, son niveau
            R(3, 2, 10, 12); F(4, 8, 8, 5.4f, soft);
            break;
        case HmiGlyph::GasBottle: {     // le corps, l'ogive, le col
            L(5, 6, 5, 14.5f); L(11, 6, 11, 14.5f); L(5, 14.5f, 11, 14.5f);
            auto a = shapes::arc(P(8, 6), 3 * s, 180, 360, 12);
            shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.2f * s));
            F(7, 1.2f, 2, 1.8f, c);
            break;
        }
        case HmiGlyph::Fan:             // le carter, trois pales
            circle(8, 8, 6.2f, false); poly({{8, 8}, {7, 2.8f}, {10.2f, 3.8f}}, true); poly({{8, 8}, {13, 10.5f}, {10.6f, 12.6f}}, true);
            poly({{8, 8}, {3.2f, 10.6f}, {3.2f, 7.4f}}, true);
            break;
        case HmiGlyph::Compressor:      // un cercle, deux lignes qui se resserrent
            circle(8, 8, 6, false); L(3.6f, 4.6f, 12.6f, 6.6f); L(3.6f, 11.4f, 12.6f, 9.4f);
            break;
        case HmiGlyph::HeatExchanger:   // un cercle, un zigzag
            circle(8, 8, 5.8f, false); L(2.8f, 9, 5, 6); L(5, 6, 7, 10); L(7, 10, 9, 6); L(9, 6, 11, 10); L(11, 10, 13.2f, 7);
            break;
        case HmiGlyph::Filter:          // un losange, le media tirete
            poly({{8, 2}, {14, 8}, {8, 14}, {2, 8}}, false); L(8, 3.8f, 8, 5.6f); L(8, 7.1f, 8, 8.9f); L(8, 10.4f, 8, 12.2f);
            break;
        case HmiGlyph::Boiler:          // le corps, la cheminee, la flamme
            R(3, 4, 9, 11); R(9.5f, 1, 2, 3); poly({{7.5f, 13.5f}, {5.8f, 11}, {7.5f, 7.5f}, {9.2f, 11}}, true);
            break;
        case HmiGlyph::Conveyor:        // la bande, deux rouleaux, les pieds
            R(1, 5, 14, 5); circle(3.6f, 7.5f, 1.3f, false); circle(12.4f, 7.5f, 1.3f, false); L(3.5f, 10, 3.5f, 14); L(12.5f, 10, 12.5f, 14);
            break;
        case HmiGlyph::Cylinder:        // le corps, le piston, la tige
            R(1, 5, 9, 6); F(5, 5.6f, 1.6f, 4.8f, c); L(6.6f, 8, 15, 8, 1.6f);
            break;
        case HmiGlyph::IsaInstrument:   // la bulle ISA, un trait de montage
            circle(8, 7, 5.5f, false); L(2.5f, 7, 13.5f, 7, 0.9f); L(8, 12.5f, 8, 15);
            break;
        case HmiGlyph::CircuitBreaker:  // la ligne, le carre
            L(8, 1, 8, 5); R(5, 5, 6, 6); F(6.2f, 6.2f, 3.6f, 3.6f, soft); L(8, 11, 8, 15);
            break;
        case HmiGlyph::Disconnector:    // la ligne, la lame ouverte
            L(8, 1, 8, 5); L(5.5f, 5, 10.5f, 5); L(8, 15, 8, 11); L(8, 11, 4.8f, 5.8f);
            break;
        case HmiGlyph::Contactor: {     // la lame, la marque, la bobine
            L(10, 1, 10, 4.6f); L(10, 15, 10, 11); L(10, 11, 7.4f, 5.6f);
            auto a = shapes::arc(P(10, 5.6f), 1.2f * s, 180, 360, 8);
            shapes::strokePolyline(r, a, false, c, std::max(1.f, 1.1f * s));
            R(1.5f, 7, 3.8f, 3); L(5.6f, 8.5f, 6.6f, 8.5f, 0.9f); L(7.6f, 8.5f, 8.4f, 8.5f, 0.9f);
            break;
        }
        case HmiGlyph::Lamp:            // un cercle barre
            circle(8, 8, 5.5f, false); L(4.1f, 4.1f, 11.9f, 11.9f); L(11.9f, 4.1f, 4.1f, 11.9f);
            break;
        case HmiGlyph::Transformer:     // deux cercles
            circle(8, 5.8f, 3.8f, false); circle(8, 10.2f, 3.8f, false);
            break;
        case HmiGlyph::Silo:            // le toit, le corps, le cone
            poly({{3, 3.2f}, {8, 1}, {13, 3.2f}, {13, 10}, {9, 14.5f}, {7, 14.5f}, {3, 10}}, false);
            break;
        case HmiGlyph::Hopper:          // la tremie, sa goulotte
            poly({{2, 2}, {14, 2}, {14, 5}, {9.5f, 11}, {6.5f, 11}, {2, 5}}, false); R(6.5f, 11, 3, 3.5f);
            break;
        case HmiGlyph::Mixer:           // la cuve, le moteur, l'arbre, les pales
            R(3, 5, 10, 10); R(6, 1, 4, 3); L(8, 4, 8, 12); L(5, 12, 11, 12, 1.6f);
            break;
        case HmiGlyph::CheckValve:      // le triangle, le siege
            L(1, 8, 15, 8); poly({{4, 4}, {4, 12}, {10, 8}}, true); L(11.5f, 4, 11.5f, 12, 1.6f);
            break;
        case HmiGlyph::FlowArrow:       // une fleche pleine
            poly({{1, 6.5f}, {9, 6.5f}, {9, 3}, {15, 8}, {9, 13}, {9, 9.5f}, {1, 9.5f}}, true);
            break;
        // ---- lot 10 : les symboles reutilisables (un losange, et son coeur)
        case HmiGlyph::Symbol:
            poly({{8, 1}, {15, 8}, {8, 15}, {1, 8}}, false);
            poly({{8, 5}, {11, 8}, {8, 11}, {5, 8}}, true);
            break;
        // ---- lot 11 : les graphiques
        case HmiGlyph::BarChart:        // trois barres sur un axe
            L(1.5f, 14, 14.5f, 14); F(3, 8, 2.6f, 6, c); F(6.8f, 4, 2.6f, 10, c); F(10.6f, 10, 2.6f, 4, c);
            break;
        case HmiGlyph::XYChart:         // deux axes, une courbe et ses points
            L(2, 14, 14, 14); L(2, 2, 2, 14); poly({{3.5f, 4}, {7, 6}, {10, 9.5f}, {13, 12.5f}}, false);
            circle(7, 6, 1.1f, true); circle(10, 9.5f, 1.1f, true);
            break;
        case HmiGlyph::StateChart:      // deux lignes d'etats dans le temps
            F(2, 3.5f, 5, 3, c); F(8, 3.5f, 6, 3, soft); F(2, 9.5f, 3, 3, soft); F(6, 9.5f, 8, 3, c); L(1.5f, 15, 14.5f, 15, 0.8f);
            break;
        case HmiGlyph::PieChart: {      // un disque, une part detachee
            circle(7.2f, 8.8f, 5.6f, false);
            auto a = shapes::arc(P(8.8f, 7.2f), 5.6f * s, -90, 0, 10);
            std::vector<gfx::Point> v{P(8.8f, 7.2f)};
            v.insert(v.end(), a.begin(), a.end());
            shapes::fillPolygon(r, v, c);
            break;
        }
        case HmiGlyph::RadarChart:      // une toile, un polygone
            poly({{8, 1.5f}, {14.2f, 6}, {11.8f, 13.8f}, {4.2f, 13.8f}, {1.8f, 6}}, false);
            poly({{8, 4.5f}, {12, 7}, {10, 11.5f}, {5.5f, 12}, {4.5f, 7.2f}}, true);
            break;
        case HmiGlyph::Histogram:       // une cloche de barres
            L(1.5f, 14, 14.5f, 14); F(2, 11, 2, 3, c); F(4.4f, 7.5f, 2, 6.5f, c); F(6.8f, 3.5f, 2, 10.5f, c); F(9.2f, 6, 2, 8, c);
            F(11.6f, 10, 2, 4, c);
            break;
        // ---- lot 11 : les alarmes
        case HmiGlyph::AlarmBanner:     // un bandeau, sa bande de couleur, un texte
            R(1, 5, 14, 6); F(1.6f, 5.6f, 2, 4.8f, c); L(5, 8, 12.5f, 8);
            break;
        case HmiGlyph::AlarmCounter:    // une cloche et sa pastille
            poly({{7, 3}, {9.5f, 4.2f}, {10.3f, 8}, {11.6f, 11}, {2.4f, 11}, {3.7f, 8}, {4.5f, 4.2f}}, false);
            circle(7, 13, 1.1f, true);
            circle(12.2f, 3.8f, 2.6f, true);
            break;
        case HmiGlyph::AlarmSummary:    // quatre tuiles, dont une allumee
            R(1.5f, 1.5f, 5.8f, 5.8f); F(8.7f, 1.5f, 5.8f, 5.8f, c); R(1.5f, 8.7f, 5.8f, 5.8f); R(8.7f, 8.7f, 5.8f, 5.8f);
            break;
        case HmiGlyph::AlarmInstruction: // une fiche, un point d'exclamation
            R(3, 1.5f, 10, 13); L(8, 4.5f, 8, 9.5f, 1.8f); circle(8, 12, 1.1f, true);
            break;
        case HmiGlyph::AlarmStats:      // des barres rangees de la plus longue a la plus courte
            F(2, 2.5f, 12, 2.2f, c); F(2, 6.2f, 8.5f, 2.2f, c); F(2, 9.9f, 5.5f, 2.2f, c); F(2, 13.4f, 3, 1.6f, soft);
            break;
        // ---- lot 11 : la production
        case HmiGlyph::ProductionCounter: // trois tuiles, une jauge
            R(1.5f, 2, 3.8f, 5); R(6.1f, 2, 3.8f, 5); F(10.7f, 2, 3.8f, 5, c); R(1.5f, 10, 13, 3); F(2.3f, 10.8f, 8, 1.4f, c);
            break;
        case HmiGlyph::VariableTable:   // un tableau, une colonne de valeurs allumee
            R(1.5f, 2.5f, 13, 11); L(1.5f, 6, 14.5f, 6); L(1.5f, 9.8f, 14.5f, 9.8f); L(8, 2.5f, 8, 13.5f); F(8.6f, 6.6f, 5.4f, 2.6f, soft);
            break;
        case HmiGlyph::RecipeEditor:    // un tableau et un crayon
            R(1.5f, 2.5f, 10, 11); L(1.5f, 6, 11.5f, 6); L(1.5f, 9.8f, 11.5f, 9.8f);
            L(9.5f, 14, 14.5f, 9, 1.8f); poly({{9.5f, 14}, {9, 15.2f}, {10.4f, 14.8f}}, true);
            break;
        case HmiGlyph::Shelve:          // une cloche et une horloge (mise de cote temporisee)
            poly({{6, 2}, {8.4f, 3.2f}, {9.2f, 7}, {10.4f, 10}, {1.6f, 10}, {2.8f, 7}, {3.6f, 3.2f}}, false);
            circle(11.5f, 11.5f, 3.4f, false); L(11.5f, 11.5f, 11.5f, 9.4f, 1.f); L(11.5f, 11.5f, 13, 12.3f, 1.f);
            break;
        case HmiGlyph::Mute:            // un haut-parleur barre
            poly({{2, 6}, {5, 6}, {9, 2.5f}, {9, 13.5f}, {5, 10}, {2, 10}}, false); L(10.5f, 5, 14.5f, 11); L(14.5f, 5, 10.5f, 11);
            break;
        // ---- lot 12 : la navigation et la structure
        case HmiGlyph::NavBar:          // une barre de trois boutons, le deuxieme allume
            R(1, 5, 14, 6); L(5.7f, 5, 5.7f, 11); L(10.3f, 5, 10.3f, 11); F(6.3f, 5.6f, 3.4f, 4.8f, c);
            break;
        case HmiGlyph::Breadcrumb:      // trois etapes et deux chevrons
            F(1, 7, 3, 2, c); L(5, 6, 6.5f, 8); L(6.5f, 8, 5, 10); F(7.5f, 7, 3, 2, c); L(11.5f, 6, 13, 8); L(13, 8, 11.5f, 10);
            circle(14.6f, 8, 0.9f, true);
            break;
        case HmiGlyph::TabContainer:    // deux onglets sur une page
            R(1, 5, 14, 9.5f); poly({{1, 5}, {1, 2}, {6, 2}, {6.8f, 5}}, true); poly({{7.2f, 5}, {8, 2.5f}, {12, 2.5f}, {12.8f, 5}}, false);
            break;
        case HmiGlyph::Frame:           // un cadre, son titre dans la bordure
            L(1.5f, 4, 3.5f, 4); L(9.5f, 4, 14.5f, 4); L(14.5f, 4, 14.5f, 14); L(14.5f, 14, 1.5f, 14); L(1.5f, 14, 1.5f, 4);
            L(4.5f, 4, 8.5f, 4, 2.2f);
            break;
        case HmiGlyph::ScrollPanel:     // un panneau et sa barre de defilement
            R(1.5f, 1.5f, 13, 13); L(11.5f, 1.5f, 11.5f, 14.5f); F(12.2f, 4, 1.6f, 4.5f, c); L(3.5f, 5, 9.5f, 5); L(3.5f, 8, 9.5f, 8);
            L(3.5f, 11, 8, 11);
            break;
        case HmiGlyph::CollapsiblePanel: // un bandeau et son chevron, le contenu dessous
            F(1.5f, 1.5f, 13, 4, soft); poly({{3, 2.7f}, {6, 2.7f}, {4.5f, 4.6f}}, true); R(1.5f, 1.5f, 13, 13); L(3.5f, 8.5f, 12, 8.5f);
            L(3.5f, 11.5f, 10, 11.5f);
            break;
        case HmiGlyph::ZoneMap:         // un plan, deux zones dont une allumee
            R(1, 2, 14, 12); poly({{2.5f, 3.5f}, {7.5f, 3.5f}, {7.5f, 8.5f}, {2.5f, 8.5f}}, true);
            poly({{9, 6}, {13.5f, 4}, {13.5f, 12.5f}, {8.5f, 12.5f}}, false);
            break;
        case HmiGlyph::LoginMenu:       // une fenetre, sa barre de titre, une silhouette
            R(1, 1.5f, 14, 13); L(1, 4.5f, 15, 4.5f); circle(8, 7.8f, 1.9f, false);
            poly({{4.5f, 13.5f}, {5.3f, 11.4f}, {6.8f, 10.6f}, {9.2f, 10.6f}, {10.7f, 11.4f}, {11.5f, 13.5f}}, false);
            break;
        case HmiGlyph::Style:           // un pinceau
            poly({{9.5f, 1.5f}, {14.5f, 6.5f}, {8.5f, 12.5f}, {3.5f, 7.5f}}, false); L(3.5f, 7.5f, 1.5f, 14.5f, 1.8f); F(9.8f, 3.5f, 2.5f, 2.5f, c);
            break;
        // ---- lot 13
        case HmiGlyph::Signature:       // un stylo qui trace un paraphe sur la ligne
            poly({{10.5f, 1.2f}, {14.3f, 5}, {7.2f, 12.1f}, {3.4f, 8.3f}}, false); poly({{3.4f, 8.3f}, {7.2f, 12.1f}, {2.2f, 13.3f}}, true);
            L(1, 15, 15, 15, 1.1f);
            break;
        case HmiGlyph::Badge:           // une carte, sa photo, deux lignes
            R(1, 3, 14, 10); F(2.8f, 5, 4.2f, 5.2f, soft); circle(4.9f, 6.8f, 1.1f, false); L(8.5f, 6, 13, 6); L(8.5f, 8.5f, 12, 8.5f);
            L(6, 1.5f, 10, 1.5f, 1.4f);
            break;
        case HmiGlyph::Audit:           // un registre et un maillon de chaine
            R(2, 1.5f, 9, 12.5f); L(4, 4.5f, 9, 4.5f); L(4, 7, 9, 7); L(4, 9.5f, 7, 9.5f);
            circle(11.5f, 11, 2.3f, false); circle(13.2f, 13.2f, 1.7f, false);
            break;
        case HmiGlyph::Theme: {         // un soleil (a gauche) et un croissant de lune (a droite)
            circle(5, 8, 2.2f, true);
            for (int k = 0; k < 8; ++k) {
                const float a = 3.14159265f * static_cast<float>(k) / 4.f;
                L(5 + 3.1f * std::cos(a), 8 + 3.1f * std::sin(a), 5 + 4.2f * std::cos(a), 8 + 4.2f * std::sin(a), 1.0f);
            }
            circle(12, 8, 3.4f, false);
            poly({{12.6f, 4.8f}, {14.2f, 6.2f}, {14.6f, 8.2f}, {13.8f, 10.4f}, {12.4f, 11.3f}, {13.2f, 9.6f}, {13.4f, 7.8f}}, true);
            break;
        }
        case HmiGlyph::Network:         // l'automate et le poste, relies
            R(1, 1.5f, 6, 5); L(2.5f, 3.2f, 5.5f, 3.2f, 0.9f);
            R(9, 9.5f, 6, 5); L(10.5f, 11.2f, 13.5f, 11.2f, 0.9f);
            L(4, 6.5f, 4, 12, 1.2f); L(4, 12, 9, 12, 1.2f);
            circle(4, 12, 1.1f, true);
            break;
        case HmiGlyph::Diagnostic:      // un cadre et une ligne de pouls
            R(1, 2.5f, 14, 11);
            poly({{2.5f, 8.5f}, {5, 8.5f}, {6.5f, 5}, {8.5f, 12}, {10.2f, 7}, {11.4f, 8.5f}, {13.5f, 8.5f}}, false);
            break;
        case HmiGlyph::Station:         // un ecran sur son pied, la vue en plein ecran
            R(1.5f, 2, 13, 9); F(3, 3.5f, 10, 6, soft); L(8, 11, 8, 13.5f); L(5, 14, 11, 14, 1.5f);
            break;
        case HmiGlyph::Mail:            // une enveloppe
            R(1.5f, 3.5f, 13, 9); L(1.8f, 3.8f, 8, 9); L(8, 9, 14.2f, 3.8f);
            break;
        case HmiGlyph::Report:          // une feuille et trois barres
            R(3, 1.5f, 10, 13); F(5, 9, 1.6f, 3.5f, c); F(7.4f, 6.5f, 1.6f, 6, c); F(9.8f, 8, 1.6f, 4.5f, c); L(5, 4, 11, 4, 1.0f);
            break;
        case HmiGlyph::Web:             // une fenetre de navigateur
            R(1.5f, 2.5f, 13, 11); L(1.5f, 5.2f, 14.5f, 5.2f, 1.0f);
            circle(3.4f, 3.9f, 0.6f, true); circle(5.2f, 3.9f, 0.6f, true);
            circle(8, 9.4f, 2.6f, false); L(5.4f, 9.4f, 10.6f, 9.4f, 0.8f);
            break;
        case HmiGlyph::Language: {      // un globe : l'equateur, deux paralleles, un meridien
            circle(8, 8, 6.5f, false);
            L(1.5f, 8, 14.5f, 8, 1.1f);
            L(2.6f, 4.6f, 13.4f, 4.6f, 0.9f);
            L(2.6f, 11.4f, 13.4f, 11.4f, 0.9f);
            auto m = shapes::ellipse(P(8, 8), 2.9f * s, 6.5f * s, 24);
            shapes::strokePolyline(r, m, true, c, std::max(1.f, 1.1f * s));
            break;
        }
        // ---- lot 16
        case HmiGlyph::Gif:             // une pellicule (trois images) et le triangle de lecture
            R(1.5f, 3, 13, 10);
            L(5.8f, 3, 5.8f, 13, 1.0f); L(10.2f, 3, 10.2f, 13, 1.0f);
            F(1.5f, 1.2f, 1.6f, 1.2f, soft); F(4.5f, 1.2f, 1.6f, 1.2f, soft); F(7.5f, 1.2f, 1.6f, 1.2f, soft); F(10.5f, 1.2f, 1.6f, 1.2f, soft);
            F(1.5f, 13.6f, 1.6f, 1.2f, soft); F(4.5f, 13.6f, 1.6f, 1.2f, soft); F(7.5f, 13.6f, 1.6f, 1.2f, soft); F(10.5f, 13.6f, 1.6f, 1.2f, soft);
            poly({{6.8f, 5.6f}, {10.4f, 8}, {6.8f, 10.4f}}, true);
            break;
        case HmiGlyph::Structure:       // des accolades et trois membres
            L(4.5f, 2, 3, 3.2f); L(3, 3.2f, 3, 6.8f); L(3, 6.8f, 1.8f, 8); L(1.8f, 8, 3, 9.2f); L(3, 9.2f, 3, 12.8f); L(3, 12.8f, 4.5f, 14);
            L(11.5f, 2, 13, 3.2f); L(13, 3.2f, 13, 6.8f); L(13, 6.8f, 14.2f, 8); L(14.2f, 8, 13, 9.2f); L(13, 9.2f, 13, 12.8f); L(13, 12.8f, 11.5f, 14);
            F(5.5f, 4.5f, 5, 1.4f, c); F(5.5f, 7.3f, 5, 1.4f, soft); F(5.5f, 10.1f, 5, 1.4f, c);
            break;
        case HmiGlyph::ThreeWayValve:   // 1.10.4 : trois triangles, la tige, le moteur
            poly({{1.5f, 6}, {1.5f, 11}, {7, 8.5f}}, true); poly({{14.5f, 6}, {14.5f, 11}, {9, 8.5f}}, true);
            poly({{5.5f, 15}, {10.5f, 15}, {8, 9.5f}}, false); L(8, 8.5f, 8, 4); circle(8, 3, 2, false);
            break;
        case HmiGlyph::Pause:           // 1.9 : deux barres (Figer l'affichage)
            F(4, 3, 2.6f, 10, c); F(9.4f, 3, 2.6f, 10, c);
            break;
    }
}

} // namespace app
