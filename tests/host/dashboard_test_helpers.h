#ifndef TESTS_HOST_DASHBOARD_TEST_HELPERS_H
#define TESTS_HOST_DASHBOARD_TEST_HELPERS_H

#include "lvgl/lvgl.h"

#include <cstdint>

namespace dashboard_test {

// Navigates the real object tree DashboardSession::create() builds (the
// design guide's tile grid, see DashboardLayout.h layout_grid()):
//
//   session_root -> page[view] -> row -> cell[tile] -> widget_root
//
// Title rows hold a label instead of cells and are skipped; tiles are
// counted across the rows of a view in reading order (`section_index` is
// ignored: sections flow into one grid).
//
// Production code never walks this by index — widgets are only ever
// referenced by pointer via WidgetFactory/DashboardSession — so this
// lives only in tests, shared because every widget integration test
// needs the same navigation.
inline lv_obj_t* cell_at(lv_obj_t* session_root, uint32_t view_index, uint32_t card_index) {
    lv_obj_t* page = lv_obj_get_child(session_root, static_cast<int32_t>(view_index));
    uint32_t n = 0;
    for (uint32_t r = 0; page && r < lv_obj_get_child_count(page); ++r) {
        lv_obj_t* row = lv_obj_get_child(page, static_cast<int32_t>(r));
        for (uint32_t c = 0; c < lv_obj_get_child_count(row); ++c) {
            lv_obj_t* cell = lv_obj_get_child(row, static_cast<int32_t>(c));
            if (lv_obj_check_type(cell, &lv_label_class)) continue; // title row
            if (n++ == card_index) return cell;
        }
    }
    return nullptr;
}

inline lv_obj_t* widget_root_at(lv_obj_t* session_root, uint32_t view_index, uint32_t section_index,
                                 uint32_t card_index) {
    (void)section_index;
    lv_obj_t* cell = cell_at(session_root, view_index, card_index);
    return cell ? lv_obj_get_child(cell, 0) : nullptr;
}

// Convenience overload for the single-view/single-section dashboards
// most widget integration tests build.
inline lv_obj_t* widget_root_at(lv_obj_t* session_root, uint32_t card_index) {
    return widget_root_at(session_root, 0, 0, card_index);
}

} // namespace dashboard_test

#endif // TESTS_HOST_DASHBOARD_TEST_HELPERS_H
