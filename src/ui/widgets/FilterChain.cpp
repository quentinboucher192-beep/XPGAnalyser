// =============================================================================
//  ui/widgets/FilterChain.cpp — the filtering half of the index-vector strategy
// -----------------------------------------------------------------------------
//  apply() produces a fresh view->model index vector. It never touches the
//  model, so filtering is safe to run while another thread holds a const
//  reference to the same project.
// =============================================================================
#include "DataViews.hpp"

#include <algorithm>
#include <cctype>

namespace ui {

void FilterChain::setGlobalTerm(std::string term) { global_ = std::move(term); }

void FilterChain::setColumnTerm(std::size_t col, std::string term) {
    auto it = std::find_if(columnTerms_.begin(), columnTerms_.end(),
                           [col](const auto& p) { return p.first == col; });
    if (term.empty()) {
        if (it != columnTerms_.end()) columnTerms_.erase(it);
        return;
    }
    if (it == columnTerms_.end()) columnTerms_.emplace_back(col, std::move(term));
    else                          it->second = std::move(term);
}

void FilterChain::addPredicate(std::string key, std::function<bool(RowIndex)> p) {
    removePredicate(key);
    predicates_.push_back(Named{std::move(key), std::move(p)});
}

void FilterChain::removePredicate(std::string_view key) {
    std::erase_if(predicates_, [&](const Named& n) { return n.key == key; });
}

const std::string& FilterChain::globalTerm() const noexcept { return global_; }

void FilterChain::clear() {
    global_.clear();
    columnTerms_.clear();
    predicates_.clear();
}

bool FilterChain::empty() const noexcept {
    return global_.empty() && columnTerms_.empty() && predicates_.empty();
}

std::vector<RowIndex> FilterChain::apply(const ITableModel& model) const {
    const auto rows = static_cast<RowIndex>(model.rowCount());
    std::vector<RowIndex> out;
    out.reserve(rows);

    if (empty()) {
        for (RowIndex r = 0; r < rows; ++r) out.push_back(r);
        return out;
    }

    // Lot recherche : le terme global est une RECHERCHE (ui::SearchQuery) -
    // chaque mot dans l'une des colonnes (commentaires compris), "une phrase",
    // -mot exclu ; sans casse ni accents. Lue une fois, pas a chaque ligne.
    const SearchQuery query(global_);
    std::vector<std::string> foldedTerms;
    for (const auto& [col, term] : columnTerms_) foldedTerms.push_back(foldForSearch(term));
    const auto columns = model.columnCount();
    for (RowIndex r = 0; r < rows; ++r) {
        // Cheapest stage first: predicates are O(1) flags, column terms touch one
        // cell, the global term is the only one that may touch every column.
        bool keep = true;
        for (const auto& p : predicates_)
            if (!p.fn(r)) { keep = false; break; }
        if (!keep) continue;

        for (std::size_t k = 0; k < columnTerms_.size(); ++k)
            if (!containsFolded(model.cellText(r, columnTerms_[k].first), foldedTerms[k])) { keep = false; break; }
        if (!keep) continue;

        if (!query.empty()) {
            // Colonne apres colonne, jusqu'a tout trouver (s'il n'y a rien a
            // exclure) ou a trouver un mot exclu.
            SearchQuery::Progress found;
            for (std::size_t c = 0; c < columns; ++c) {
                query.feed(found, model.cellText(r, c));
                if (query.hopeless(found) || (query.excluded().empty() && query.satisfied(found))) break;
            }
            keep = query.satisfied(found);
        }
        if (keep) out.push_back(r);
    }
    return out;
}

} // namespace ui
