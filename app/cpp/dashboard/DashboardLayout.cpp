#include "DashboardLayout.h"

#include <algorithm>

namespace app::dashboard {

namespace {
// "heading" card text (raw JSON: "heading": "...").
std::string card_heading(const DashboardCard& card) {
    const std::string key = "\"heading\":\"";
    const size_t p = card.raw_json.find(key);
    if (p == std::string::npos) return "";
    const size_t start = p + key.size();
    const size_t end = card.raw_json.find('"', start);
    return end == std::string::npos ? "" : card.raw_json.substr(start, end - start);
}
} // namespace

int32_t compute_unit_size_x(int32_t container_width_px, int max_columns) {
    int cols = max_columns > 0 ? max_columns : 3; // matches dashboard.c's default
    int32_t units_per_page = cols * 12;
    if (units_per_page <= 0) units_per_page = 36;
    int32_t unit = container_width_px / units_per_page;
    return unit > 0 ? unit : 1;
}

SectionGeometry compute_section_geometry(int32_t unit_size_x, int column_span) {
    int span = column_span > 0 ? column_span : 1;
    SectionGeometry geometry;
    geometry.max_units = span * 12;
    geometry.pixel_width = unit_size_x * geometry.max_units;
    return geometry;
}

namespace {
int32_t card_col_units(const DashboardCard& card, int32_t max_units) {
    // Absent grid_options.columns means "full section width", matching
    // dashboard.c's fallback (card_cols_units_tmp = colSpan * 12 == max_units).
    int32_t units = card.grid_columns > 0 ? card.grid_columns : max_units;
    if (units > max_units) units = max_units;
    if (units < 1) units = 1;
    return units;
}

int32_t card_row_span(const DashboardCard& card) { return card.grid_rows > 0 ? card.grid_rows : 1; }
} // namespace

std::vector<CardPlacement> place_cards(const std::vector<DashboardCard>& cards, int32_t max_units) {
    if (max_units < 1) max_units = 12;

    std::vector<CardPlacement> placements;
    placements.reserve(cards.size());

    int32_t cur_col = 0;
    int32_t cur_row = 0;
    for (const auto& card : cards) {
        int32_t col_units = card_col_units(card, max_units);
        if (cur_col + col_units > max_units) {
            ++cur_row;
            cur_col = 0;
        }

        CardPlacement placement;
        placement.col_start = cur_col;
        placement.col_span = col_units;
        placement.row_start = cur_row;
        placement.row_span = card_row_span(card);
        placements.push_back(placement);

        cur_col += col_units;
    }
    return placements;
}

int32_t required_rows(const std::vector<CardPlacement>& placements) {
    int32_t rows = 1;
    for (const auto& placement : placements) {
        int32_t end_row = placement.row_start + placement.row_span;
        if (end_row > rows) rows = end_row;
    }
    return rows;
}

int view_columns(const DashboardView& view) { return view.max_columns > 0 ? view.max_columns : 4; }

bool is_compact(const DashboardCard& card) {
    if (card.entity_id.empty()) return false; // a compact tile shows one entity
    if (card.grid_rows > 0) return card.grid_rows <= 2;
    // HA's own sizes: tile 6 x 1, entity / sensor 6 x 2, button 3 x 2
    return card.type == "tile" || card.type == "entity" || card.type == "sensor" || card.type == "button";
}

namespace {
// HA's default width (12 units = one column) when grid_options has none.
int default_units(const DashboardCard& card, int section_units) {
    if (card.type == "button") return 3;
    if (card.type == "tile" || card.type == "entity" || card.type == "sensor") return 6;
    return section_units; // the whole section
}

// First free place for a w x h cell in the occupancy grid (rows grow as needed).
void place_dense(std::vector<std::vector<bool>>& used, int width, int w, int h, int& out_x, int& out_y) {
    for (int y = 0;; ++y) {
        while (static_cast<int>(used.size()) < y + h) used.emplace_back(width, false);
        for (int x = 0; x + w <= width; ++x) {
            bool free = true;
            for (int dy = 0; dy < h && free; ++dy)
                for (int dx = 0; dx < w && free; ++dx) free = !used[y + dy][x + dx];
            if (!free) continue;
            for (int dy = 0; dy < h; ++dy)
                for (int dx = 0; dx < w; ++dx) used[y + dy][x + dx] = true;
            out_x = x;
            out_y = y;
            return;
        }
    }
}

struct SectionBox {
    std::vector<GridCell> cells;
    int span = 1;
    int height = 0;
};

SectionBox layout_section(const std::string& title, const std::vector<DashboardCard>& cards, int span) {
    SectionBox box;
    box.span = span;
    const int width = 2 * span;
    std::vector<std::vector<bool>> used;
    auto add = [&](GridCell cell) {
        place_dense(used, width, cell.w, cell.h, cell.x, cell.y);
        box.height = std::max(box.height, cell.y + cell.h);
        box.cells.push_back(std::move(cell));
    };
    auto add_title = [&](const std::string& text) {
        GridCell cell;
        cell.title = text;
        cell.kind = CellKind::Title;
        cell.w = width;
        cell.h = 1;
        add(std::move(cell));
    };
    if (!title.empty()) add_title(title);
    for (const auto& card : cards) {
        if (card.type == "heading") {
            const std::string heading = card_heading(card);
            if (!heading.empty()) add_title(heading);
            continue;
        }
        GridCell cell;
        cell.card = &card;
        cell.kind = is_compact(card) ? CellKind::Compact : CellKind::Tile;
        cell.w = card_halves(card, width);
        cell.h = cell.kind == CellKind::Compact ? 1 : kSlotsPerTile;
        add(std::move(cell));
    }
    return box;
}
} // namespace

int card_halves(const DashboardCard& card, int section_halves) {
    const int section_units = 6 * section_halves;
    const int units = card.grid_columns > 0 ? std::min(card.grid_columns, section_units)
                                            : default_units(card, section_units);
    // compact: half columns (6 units); big: whole columns (12 units)
    const int halves = is_compact(card) ? (units + 3) / 6 : 2 * ((units + 6) / 12);
    const int min = is_compact(card) ? 1 : 2;
    return std::max(std::min(min, section_halves), std::min(section_halves, halves));
}

int rows_on_screen(const std::string& setting, int columns) {
    int rows = 2;
    if (setting == "cols") rows = columns;
    else if (setting == "cols-1") rows = columns - 1;
    else if (setting.size() == 1 && setting[0] >= '1' && setting[0] <= '6') rows = setting[0] - '0';
    return std::max(1, std::min(6, rows));
}

std::vector<GridRow> layout_grid(const DashboardView& view) {
    const int columns = view_columns(view);
    std::vector<SectionBox> boxes;
    for (const auto& section : view.sections) {
        const int span = std::max(1, std::min(columns, section.column_span));
        SectionBox box = layout_section(section.title, section.cards, span);
        if (!box.cells.empty()) boxes.push_back(std::move(box));
    }
    if (!view.cards.empty()) {
        // Masonry views: one section over the whole width, cards one column wide.
        std::vector<DashboardCard> sized = view.cards;
        for (auto& card : sized) {
            if (card.grid_columns <= 0) card.grid_columns = is_compact(card) ? 6 : 12;
        }
        SectionBox box = layout_section("", sized, columns);
        // cells point into `sized`: re-point them at the view's own cards
        for (auto& cell : box.cells) {
            if (cell.card) cell.card = &view.cards[static_cast<size_t>(cell.card - sized.data())];
        }
        boxes.push_back(std::move(box));
    }

    std::vector<GridRow> rows;
    int col = columns; // forces a new row for the first section
    for (auto& box : boxes) {
        if (col + box.span > columns) {
            rows.emplace_back();
            col = 0;
        }
        GridRow& row = rows.back();
        for (auto& cell : box.cells) {
            cell.x += 2 * col;
            row.cells.push_back(std::move(cell));
        }
        row.height = std::max(row.height, box.height);
        col += box.span;
    }
    return rows;
}

} // namespace app::dashboard
