#pragma once
// =============================================================================
//  ui/widgets/PropertyGridHooks.hpp - 1.11 (integration I111, lien 5)
// -----------------------------------------------------------------------------
//  "La page des expressions..." a la fin du menu du clic droit d'une case a
//  expression (ui::PropertyGrid, DataViews.cpp) : l'appli (App) branche le
//  crochet et ouvre la page des expressions de T3 sur le type de la case. Sans
//  crochet (les essais) : pas d'entree. A part de DataViews.hpp, que presque
//  tout inclut : le changer ferait tout reconstruire.
// =============================================================================

#include "DataViews.hpp"

#include <functional>
#include <string_view>

namespace ui::propertygrid {

using ExpressionPageHook = std::function<void(const PropertyGrid::Property&)>;
void setExpressionPageHook(ExpressionPageHook hook);
[[nodiscard]] const ExpressionPageHook& expressionPageHook();

// Le libelle de l'entree, que le tutoriel de l'expression (T1, etape 7) vise : "menu:<libelle>".
inline constexpr std::string_view kExpressionPageItem = "La page des expressions\xE2\x80\xA6";

} // namespace ui::propertygrid
