// =============================================================================
//  app/ai/AiJson.hpp - le JSON de l'Assistant IA : depuis la 1.28.0, core/Json.hpp
// -----------------------------------------------------------------------------
//  Le meme code sert aussi au deploiement (deploy/, qui ne depend pas de app/) :
//  il vit dans core/. Ici, un alias - app::ai::json::Value reste valable partout.
// =============================================================================
#pragma once

#include "../../core/Json.hpp"

namespace app::ai {
namespace json = ::core::json;
} // namespace app::ai
