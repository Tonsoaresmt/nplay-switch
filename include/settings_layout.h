#pragma once
static inline int settings_row_y(int section, int index) { return 322 + index * ((section == 1 || section == 3) ? 60 : 65); }
static inline int settings_row_height(int section) { return (section == 1 || section == 3) ? 52 : 57; }
static inline int settings_row_at(int section, int count, int x, int y) {
    if (section < 0 || section > 3 || count < 1 || count > 5 ||
        x < 316 || x >= 1214 || y < 322) return -1;
    int index = (y - 322) / ((section == 1 || section == 3) ? 60 : 65);
    return index < count && y < settings_row_y(section, index) + settings_row_height(section) ? index : -1;
}
