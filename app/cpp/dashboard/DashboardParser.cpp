#include "DashboardParser.h"

#include <cjson/cJSON.h>

#include <memory>

namespace app::dashboard {

namespace {

struct CJsonDeleter {
    void operator()(cJSON* p) const { cJSON_Delete(p); }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

std::string print_owned(cJSON* value) {
    if (!value) return {};
    char* txt = cJSON_PrintUnformatted(value);
    if (!txt) return {};
    std::string result = txt;
    cJSON_free(txt);
    return result;
}

DashboardCard parse_card(cJSON* card_json) {
    DashboardCard card;
    if (!card_json || !cJSON_IsObject(card_json)) return card;

    cJSON* type = cJSON_GetObjectItemCaseSensitive(card_json, "type");
    if (type && cJSON_IsString(type)) card.type = type->valuestring;

    cJSON* entity = cJSON_GetObjectItemCaseSensitive(card_json, "entity");
    if (entity && cJSON_IsString(entity)) card.entity_id = entity->valuestring;

    cJSON* grid_options = cJSON_GetObjectItemCaseSensitive(card_json, "grid_options");
    if (grid_options && cJSON_IsObject(grid_options)) {
        cJSON* cols = cJSON_GetObjectItemCaseSensitive(grid_options, "columns");
        if (cols && cJSON_IsNumber(cols)) card.grid_columns = cols->valueint;
        cJSON* rows = cJSON_GetObjectItemCaseSensitive(grid_options, "rows");
        if (rows && cJSON_IsNumber(rows)) card.grid_rows = rows->valueint;
    }

    card.is_background_graph = (card.type == "statistics-graph");
    card.raw_json = print_owned(card_json);
    return card;
}

// Container cards hold other cards: the panel shows their cards one by one
// in the section (vertical-stack, horizontal-stack, the "grid" card) — and
// a conditional card's card, for now without evaluating its conditions.
void append_cards(cJSON* card_json, std::vector<DashboardCard>& out, const std::string& section_title, int depth = 0) {
    if (!card_json || !cJSON_IsObject(card_json)) return;
    cJSON* type = cJSON_GetObjectItemCaseSensitive(card_json, "type");
    const std::string t = cJSON_IsString(type) ? type->valuestring : "";
    if (depth < 8) {
        cJSON* children = cJSON_GetObjectItemCaseSensitive(card_json, "cards");
        if ((t == "vertical-stack" || t == "horizontal-stack" || t == "grid") && cJSON_IsArray(children)) {
            cJSON* child = nullptr;
            cJSON_ArrayForEach(child, children) { append_cards(child, out, section_title, depth + 1); }
            return;
        }
        cJSON* inner = cJSON_GetObjectItemCaseSensitive(card_json, "card");
        if (t == "conditional" && cJSON_IsObject(inner)) {
            append_cards(inner, out, section_title, depth + 1);
            return;
        }
    }
    out.push_back(parse_card(card_json));
    out.back().section_title = section_title;
}

DashboardSection parse_section(cJSON* section_json) {
    DashboardSection section;
    if (!section_json || !cJSON_IsObject(section_json)) return section;

    cJSON* type = cJSON_GetObjectItemCaseSensitive(section_json, "type");
    if (type && cJSON_IsString(type)) section.type = type->valuestring;

    cJSON* title = cJSON_GetObjectItemCaseSensitive(section_json, "title");
    if (title && cJSON_IsString(title)) section.title = title->valuestring;

    cJSON* col_span = cJSON_GetObjectItemCaseSensitive(section_json, "column_span");
    section.column_span = (col_span && cJSON_IsNumber(col_span)) ? col_span->valueint : 1;

    cJSON* cards = cJSON_GetObjectItemCaseSensitive(section_json, "cards");
    if (cards && cJSON_IsArray(cards)) {
        cJSON* card_json = nullptr;
        cJSON_ArrayForEach(card_json, cards) { append_cards(card_json, section.cards, section.title); }
    }
    return section;
}

// "homepane": {"rows": "3"} (a string as in the settings, or a number)
std::string homepane_rows(cJSON* owner) {
    cJSON* homepane = cJSON_GetObjectItemCaseSensitive(owner, "homepane");
    cJSON* rows = cJSON_IsObject(homepane) ? cJSON_GetObjectItemCaseSensitive(homepane, "rows") : nullptr;
    if (cJSON_IsString(rows)) return rows->valuestring;
    if (cJSON_IsNumber(rows)) return std::to_string(rows->valueint);
    return "";
}

DashboardView parse_view(cJSON* view_json) {
    DashboardView view;
    if (!view_json || !cJSON_IsObject(view_json)) return view;

    cJSON* title = cJSON_GetObjectItemCaseSensitive(view_json, "title");
    if (title && cJSON_IsString(title)) view.title = title->valuestring;

    cJSON* type = cJSON_GetObjectItemCaseSensitive(view_json, "type");
    if (type && cJSON_IsString(type)) view.type = type->valuestring;

    cJSON* max_columns = cJSON_GetObjectItemCaseSensitive(view_json, "max_columns");
    if (max_columns && cJSON_IsNumber(max_columns)) view.max_columns = max_columns->valueint;

    view.rows = homepane_rows(view_json);

    cJSON* subview = cJSON_GetObjectItemCaseSensitive(view_json, "subview");
    if (subview && cJSON_IsBool(subview)) view.subview = cJSON_IsTrue(subview);

    cJSON* sections = cJSON_GetObjectItemCaseSensitive(view_json, "sections");
    if (sections && cJSON_IsArray(sections)) {
        cJSON* section_json = nullptr;
        cJSON_ArrayForEach(section_json, sections) { view.sections.push_back(parse_section(section_json)); }
    }

    cJSON* flat_cards = cJSON_GetObjectItemCaseSensitive(view_json, "cards");
    if (flat_cards && cJSON_IsArray(flat_cards) && view.sections.empty()) {
        cJSON* card_json = nullptr;
        cJSON_ArrayForEach(card_json, flat_cards) { append_cards(card_json, view.cards, ""); }
    }

    return view;
}

} // namespace

bool DashboardParser::parse(const std::string& lovelace_result_json, DashboardModel& out_model,
                             ParseError& out_error) {
    out_model = DashboardModel{};

    CJsonPtr root(cJSON_Parse(lovelace_result_json.c_str()));
    if (!root) {
        out_error = {"", "invalid JSON"};
        return false;
    }

    cJSON* views = cJSON_GetObjectItemCaseSensitive(root.get(), "views");
    if (!views || !cJSON_IsArray(views)) {
        out_error = {"views", "missing or not an array"};
        return false;
    }

    cJSON* view_json = nullptr;
    cJSON_ArrayForEach(view_json, views) { out_model.views.push_back(parse_view(view_json)); }
    out_model.rows = homepane_rows(root.get());

    return true;
}

} // namespace app::dashboard
