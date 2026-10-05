#include "HmiPrompt.hpp"

#include <algorithm>

namespace hmi {

PromptLayout promptLayout(double w, double h) {
    PromptLayout l;
    // Titre, champ, limites, message, boutons : cinq lignes et demie.
    l.rowH = std::clamp((std::min(h, 520.0) - 24) / 6.4, 22.0, 44.0);
    l.fontSize = std::clamp(l.rowH * 0.46, 11.0, 20.0);
    const double pw = std::max(220.0, std::min(w - 24, 520.0)), ph = std::min(h - 12, l.rowH * 6.4 + 12);
    const double px = (w - pw) / 2, py = std::max(6.0, (h - ph) / 2);
    l.panel = {px, py, pw, ph};
    const double titleH = l.rowH * 1.2;
    l.title = {px, py, pw, titleH};
    l.close = {px + pw - titleH, py, titleH, titleH};
    const double left = px + 18, inner = pw - 36;
    double y = py + titleH + l.rowH * 0.45;
    l.field = {left, y, inner, l.rowH * 1.15};
    y += l.rowH * 1.35;
    l.limits = {left, y, inner, l.rowH * 0.8};
    y += l.rowH * 0.85;
    l.message = {left, y, inner, l.rowH * 0.8};
    y += l.rowH * 0.95;
    const double bw = std::min(170.0, (inner - 12) / 2);
    l.cancel = {left + inner - bw, y, bw, l.rowH};
    l.ok = {l.cancel.x - 12 - bw, y, bw, l.rowH};
    return l;
}

std::string promptHit(const PromptLayout& l, double x, double y) {
    if (!l.panel.contains(x, y)) return "dehors";
    if (l.close.contains(x, y)) return "fermer";
    if (l.ok.contains(x, y)) return "bouton:valider";
    if (l.cancel.contains(x, y)) return "bouton:annuler";
    if (l.field.contains(x, y)) return "champ";
    return {};
}

bool promptPartBox(const PromptLayout& l, std::string_view part, Box& out) {
    if (part == "fermer") out = l.close;
    else if (part == "bouton:valider") out = l.ok;
    else if (part == "bouton:annuler") out = l.cancel;
    else if (part == "champ") out = l.field;
    else return false;
    return true;
}

} // namespace hmi
