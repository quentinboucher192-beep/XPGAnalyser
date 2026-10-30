// =============================================================================
//  app/ai/AiSse.cpp - voir AiSse.hpp
// =============================================================================
#include "AiSse.hpp"

#include <algorithm>

namespace app::ai {

void SseReader::reset() {
    line_.clear();
    bom_.clear();
    lastCr_ = false;
    started_ = false;
    event_.clear();
    data_.clear();
    id_.clear();
    hasData_ = false;
}

void SseReader::feed(std::string_view chunk, std::vector<SseEvent>& out) {
    if (started_) {
        body(chunk, out);
        return;
    }
    // Le BOM UTF-8 (EF BB BF), peut-etre coupe entre deux morceaux : ses trois octets d'abord.
    static constexpr std::string_view kBom = "\xEF\xBB\xBF";
    const std::size_t take = std::min<std::size_t>(3 - bom_.size(), chunk.size());
    bom_.append(chunk.data(), take);
    if (bom_.size() < 3 && kBom.substr(0, bom_.size()) == bom_) return;   // la suite au prochain morceau
    started_ = true;
    std::string head = bom_ == kBom ? std::string{} : bom_;
    bom_.clear();
    head.append(chunk.data() + take, chunk.size() - take);
    body(head, out);
}

void SseReader::body(std::string_view chunk, std::vector<SseEvent>& out) {
    std::size_t i = 0;
    if (lastCr_ && !chunk.empty() && chunk[0] == '\n') i = 1;   // \r | \n coupe entre deux morceaux
    if (!chunk.empty()) lastCr_ = false;
    std::size_t from = i;
    for (; i < chunk.size(); ++i) {
        const char c = chunk[i];
        if (c != '\n' && c != '\r') continue;
        line_.append(chunk.data() + from, i - from);
        line(line_, out);
        line_.clear();
        if (c == '\r') {
            if (i + 1 < chunk.size()) {
                if (chunk[i + 1] == '\n') ++i;
            } else {
                lastCr_ = true;
            }
        }
        from = i + 1;
    }
    if (from < chunk.size()) line_.append(chunk.data() + from, chunk.size() - from);
}

void SseReader::finish(std::vector<SseEvent>& out) {
    if (!started_) {   // moins de trois octets en tout : ce n'etait pas un BOM entier
        started_ = true;
        const std::string head = std::string_view("\xEF\xBB\xBF").substr(0, bom_.size()) == bom_ ? std::string{} : bom_;
        bom_.clear();
        body(head, out);
    }
    if (!line_.empty()) {
        line(line_, out);
        line_.clear();
    }
    // Un evenement sans sa ligne vide n'est pas rendu.
    event_.clear();
    data_.clear();
    hasData_ = false;
}

void SseReader::line(std::string_view l, std::vector<SseEvent>& out) {
    if (l.empty()) {
        // La fin d'un evenement.
        if (hasData_) {
            SseEvent e;
            if (!event_.empty()) e.event = event_;
            e.data = std::move(data_);
            e.id = id_;
            out.push_back(std::move(e));
        }
        event_.clear();
        data_.clear();
        hasData_ = false;
        return;
    }
    if (l.front() == ':') return;   // un commentaire (une veille du serveur)
    std::string_view field = l, value;
    if (const auto colon = l.find(':'); colon != std::string_view::npos) {
        field = l.substr(0, colon);
        value = l.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    }
    if (field == "event") {
        event_ = std::string(value);
    } else if (field == "data") {
        if (hasData_) data_ += '\n';
        data_.append(value.data(), value.size());
        hasData_ = true;
    } else if (field == "id") {
        if (value.find('\0') == std::string_view::npos) id_ = std::string(value);
    }
    // "retry" et les champs inconnus : ignores.
}

} // namespace app::ai
