#pragma once
// Home Catalog Screen facade: navigation bar, grid layout, series, thumbs, and nowbar.
#include <Arduino.h>
#include <lvgl.h>
#include "app_state.h"
#include "storage_sd.h"

static lv_obj_t* s_grid = nullptr;
static lv_obj_t* s_tab_btns[4] = {nullptr, nullptr, nullptr, nullptr};
static void populate_grid(); // fwd
static lv_obj_t* s_top_dev_lbl = nullptr;
static lv_obj_t* s_top_dev_dot = nullptr;
static void populate_grid(); // fwd
static void update_home_bar(); // fwd

#include "ui_catalog_series.h"
#include "ui_catalog_thumbs.h"
#include "ui_catalog_nowbar.h"

static bool is_played_past_7_days(const PsramString& lp){
  if(lp.empty()) return false;
  time_t now = time(nullptr);
  if(now > 1700000000){
    time_t cutoff = now - (7 * 86400);
    if(lp.length() >= 10 && lp[4] == '-' && lp[7] == '-'){
      struct tm tm_lp = {};
      tm_lp.tm_year = atoi(lp.substr(0, 4).c_str()) - 1900;
      tm_lp.tm_mon  = atoi(lp.substr(5, 2).c_str()) - 1;
      tm_lp.tm_mday = atoi(lp.substr(8, 2).c_str());
      if(lp.length() >= 19 && (lp[10] == 'T' || lp[10] == ' ')){
        tm_lp.tm_hour = atoi(lp.substr(11, 2).c_str());
        tm_lp.tm_min  = atoi(lp.substr(14, 2).c_str());
        tm_lp.tm_sec  = atoi(lp.substr(17, 2).c_str());
      }
      time_t lp_epoch = mktime(&tm_lp);
      return (lp_epoch >= cutoff);
    }
    struct tm tm_cut;
    gmtime_r(&cutoff, &tm_cut);
    char cutoff_date[12];
    strftime(cutoff_date, sizeof(cutoff_date), "%Y-%m-%d", &tm_cut);
    return (lp.substr(0, 10) >= cutoff_date);
  }
  return true;
}

static bool card_matches_active_tab(const Card& c){
  if(s_active_tab == TAB_ALL) return true;
  if(s_active_tab == TAB_SERIES) return (c.series.length() > 0);
  if(s_active_tab == TAB_RECENT){
    if(c.daily) return true;
    return is_played_past_7_days(c.lp);
  }
  if(s_active_tab == TAB_YOTO) return (!c.is_abs() || c.daily);
  if(s_active_tab == TAB_ABS)  return c.is_abs();
  return true;
}



static void populate_grid(){

  if(!s_grid) return;
  lv_obj_clean(s_grid);
  unbind_grid_tiles();

  if(s_active_tab == TAB_SERIES){
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    static std::vector<int, PsramVectorAlloc<int>> group_counts;
    static std::vector<int, PsramVectorAlloc<int>> elig_idx;
    static std::vector<int, PsramVectorAlloc<int>> rep_idx;
    static std::vector<int, PsramVectorAlloc<int>> m_idx;
    s_series_group_names.clear();
    group_counts.clear();
    elig_idx.clear();
    rep_idx.clear();
    m_idx.clear();

    auto card_seq_in_group = [&](const Card& c, const PsramString& grp) -> float {
      float seq = -1.0f;
      String disp;
      get_card_seq_info(c, grp, seq, disp);
      return (seq >= 0.0f) ? seq : 999999.0f;
    };

    auto add_to_group = [&](const PsramString& raw_name, int card_index){
      PsramString gname = raw_name;
      gname.trim();
      if(gname.length() == 0) gname = "Standalone & Singles";
      int found = -1;
      for(size_t g = 0; g < s_series_group_names.size(); g++){
        if(s_series_group_names[g].equalsIgnoreCase(gname)){ found = (int)g; break; }
      }
      if(found >= 0){
        group_counts[found]++;
        int cur_rep = rep_idx[found];
        float cur_seq = (cur_rep >= 0) ? card_seq_in_group(s_cards[cur_rep], gname) : 999999.0f;
        float new_seq = card_seq_in_group(s_cards[card_index], gname);
        if(new_seq < cur_seq) rep_idx[found] = card_index;
      } else {
        s_series_group_names.push_back(gname);
        group_counts.push_back(1);
        rep_idx.push_back(card_index);
      }
    };

    for(int i = 0; i < s_card_count; i++){
      add_to_group(s_cards[i].series, i);
      for(int ex = 0; ex < s_cards[i].extra_series_cnt; ex++){
        if(s_cards[i].extra_series[ex].length() > 0){
          add_to_group(s_cards[i].extra_series[ex], i);
        }
      }
    }

    // LEVEL 2: Selected Series Tracklist View
    if(s_selected_series_filter.length() > 0){
      const PsramString& sel_grp = s_selected_series_filter;
      for(int i = 0; i < s_card_count; i++){
        String cs = s_cards[i].series.c_str();
        cs.trim();
        if(cs.length() == 0) cs = "Standalone & Singles";
        bool match = cs.equalsIgnoreCase(sel_grp.c_str());
        if(!match){
          for(int ex = 0; ex < s_cards[i].extra_series_cnt; ex++){
            String es = s_cards[i].extra_series[ex].c_str();
            es.trim();
            if(es.equalsIgnoreCase(sel_grp.c_str())){ match = true; break; }
          }
        }
        if(match) m_idx.push_back(i);
      }
      std::sort(m_idx.begin(), m_idx.end(), [&](int idx_a, int idx_b){
        const Card& ca = s_cards[idx_a];
        const Card& cb = s_cards[idx_b];
        float sa = card_seq_in_group(ca, sel_grp);
        float sb = card_seq_in_group(cb, sel_grp);
        if(sa != sb) return sa < sb;
        return ca.sort_title < cb.sort_title;
      });
      int m_cnt = (int)m_idx.size();

      // Level 2 Header Row with Back Button
      lv_obj_t* hdr = lv_obj_create(s_grid);
      lv_obj_set_size(hdr, LCD_H_RES - 36, 44);
      lv_obj_set_style_bg_color(hdr, lv_color_hex(0xF3ECE0), 0);
      lv_obj_set_style_radius(hdr, 12, 0);
      lv_obj_set_style_border_width(hdr, 1, 0);
      lv_obj_set_style_border_color(hdr, lv_color_hex(0xE7E0D2), 0);
      lv_obj_set_style_pad_left(hdr, 10, 0);
      lv_obj_set_style_pad_right(hdr, 14, 0);
      lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(hdr, 12, 0);
      lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t* bk_btn = lv_btn_create(hdr);
      lv_obj_set_size(bk_btn, 170, 34);
      lv_obj_set_style_radius(bk_btn, 17, 0);
      lv_obj_set_style_bg_color(bk_btn, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_border_width(bk_btn, 1, 0);
      lv_obj_set_style_border_color(bk_btn, lv_color_hex(0xE7E0D2), 0);
      lv_obj_set_style_shadow_width(bk_btn, 0, 0);
      lv_obj_add_event_cb(bk_btn, series_back_cb, LV_EVENT_CLICKED, NULL);

      lv_obj_t* bkl = lv_label_create(bk_btn);
      lv_label_set_text(bkl, LV_SYMBOL_LEFT " Back to Series");
      lv_obj_set_style_text_font(bkl, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(bkl, lv_color_hex(0x23303A), 0);
      lv_obj_center(bkl);

      lv_obj_t* htitle = lv_label_create(hdr);
      lv_label_set_text(htitle, sel_grp.c_str());
      lv_obj_set_style_text_font(htitle, &lv_font_montserrat_20, 0);
      lv_obj_set_style_text_color(htitle, lv_color_hex(0x23303A), 0);
      lv_label_set_long_mode(htitle, LV_LABEL_LONG_DOT);
      lv_obj_set_flex_grow(htitle, 1);

      lv_obj_t* hcnt = lv_label_create(hdr);
      lv_label_set_text_fmt(hcnt, "(%d)", m_cnt);
      lv_obj_set_style_text_font(hcnt, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(hcnt, lv_color_hex(0x8A8276), 0);

      for(int k = 0; k < m_cnt; k++){
        create_card_tile(s_grid, m_idx[k], true, k + 1);
      }
      lv_obj_update_layout(s_grid);
      update_visible_thumbs(true);
      return;
    }

    // LEVEL 1: Series Collection Browser (Instantaneous via precomputed series groups)
    if(s_series_groups.empty()){
      precompute_series_metadata();
    }
    if(s_series_groups.empty()){
      lv_obj_t* empty = lv_label_create(s_grid);
      lv_label_set_text(empty, "No multi-book series found in this library.");
      lv_obj_set_width(empty, LCD_H_RES - 40);
      lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_font(empty, &lv_font_montserrat_20, 0);
      lv_obj_set_style_text_color(empty, lv_color_hex(0x8A8276), 0);
      lv_obj_set_style_pad_top(empty, 60, 0);
      return;
    }

    for(size_t e = 0; e < s_series_groups.size(); e++){
      create_series_tile(s_grid, (int)e, s_series_groups[e].name, s_series_groups[e].rep_card_idx, s_series_groups[e].count);
    }
    lv_obj_update_layout(s_grid);
    update_visible_thumbs(true);
    return;
  }

  // Flat Grid View (TAB_ALL / Catalog, TAB_RECENT, TAB_ABS, TAB_YOTO)
  lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  static std::vector<int, PsramVectorAlloc<int>> grid_indices;
  grid_indices.clear();
  for(int i = 0; i < s_card_count; i++){
    if(card_matches_active_tab(s_cards[i])) grid_indices.push_back(i);
  }

  if(s_active_tab == TAB_RECENT){
    std::sort(grid_indices.begin(), grid_indices.end(), [](int ia, int ib){
      const Card& a = s_cards[ia];
      const Card& b = s_cards[ib];
      if(a.daily != b.daily) return a.daily;
      return a.lp > b.lp; // newest played first
    });
  }

  int shown = 0;
  for(size_t gi = 0; gi < grid_indices.size(); gi++){
    int i = grid_indices[gi];
    create_card_tile(s_grid, i, false);
    shown++;
    if((shown & 31) == 0) yield();
  }
  if(shown == 0){
    lv_obj_t* empty = lv_label_create(s_grid);
    if(s_active_tab == TAB_RECENT){
      lv_label_set_text(empty, "No audiobooks played in the past 7 days.");
    } else {
      lv_label_set_text(empty, "No audiobooks found in this category.");
    }
    lv_obj_set_width(empty, LCD_H_RES - 40);
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(0x8A8276), 0);
    lv_obj_set_style_pad_top(empty, 60, 0);
  }
  lv_obj_update_layout(s_grid);
  update_visible_thumbs(true);
}

void build_grid(){
  status_hide();
  s_top_dev_lbl = nullptr;
  s_top_dev_dot = nullptr;
  for(int t = 0; t < 4; t++) s_tab_btns[t] = nullptr;
  if(!s_scr_home){
    s_scr_home = lv_obj_create(NULL);
  } else {
    lv_obj_clean(s_scr_home);
  }
  lv_obj_t* scr=s_scr_home;
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xFBF7F0), 0);

  // Top Tab Navigation Bar (Direct flat children of nav for 100% reliable LVGL touch hit-testing)
  // Left: [Profile Pill + Target Player Pill], Right: [All, Series, Gear]
  if((int)s_active_tab > 2) s_active_tab = TAB_ALL;

  lv_obj_t* nav = lv_obj_create(scr);
  lv_obj_set_size(nav, LCD_H_RES, 48);
  lv_obj_align(nav, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(nav, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(nav, 1, 0);
  lv_obj_set_style_border_color(nav, lv_color_hex(0xE7E0D2), 0);
  lv_obj_set_style_radius(nav, 0, 0);
  lv_obj_set_style_pad_all(nav, 0, 0);
  lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

  // Top Left: Dedicated Player Status Pill (showing active player & connection status dot)
  lv_obj_t* dev_btn = lv_obj_create(nav);
  lv_obj_set_size(dev_btn, 240, 38);
  lv_obj_set_pos(dev_btn, 12, 5);
  lv_obj_set_style_radius(dev_btn, 19, 0);
  lv_obj_set_style_bg_color(dev_btn, lv_color_hex(0xF4EDE0), 0);
  lv_obj_set_style_border_width(dev_btn, 1, 0);
  lv_obj_set_style_border_color(dev_btn, lv_color_hex(0xE7E0D2), 0);
  lv_obj_set_style_pad_all(dev_btn, 0, 0);
  lv_obj_set_style_shadow_width(dev_btn, 0, 0);
  lv_obj_clear_flag(dev_btn, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* dot = lv_obj_create(dev_btn);
  s_top_dev_dot = dot;
  lv_obj_set_size(dot, 12, 12);
  lv_obj_set_style_radius(dot, 6, 0);
  lv_obj_set_style_bg_color(dot, get_player_dot_color(cfg::device_id), 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(dot, LV_ALIGN_LEFT_MID, 12, 0);

  lv_obj_t* dname = lv_label_create(dev_btn);
  s_top_dev_lbl = dname;
  lv_label_set_text(dname, get_active_player_name().c_str());
  lv_obj_set_style_text_font(dname, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(dname, lv_color_hex(0x23303A), 0);
  lv_label_set_long_mode(dname, LV_LABEL_LONG_DOT);
  lv_obj_set_width(dname, 195);
  lv_obj_clear_flag(dname, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(dname, LV_ALIGN_LEFT_MID, 32, 0);

  // 3. Right: Category Pills (Catalog + Series + Recently Played) + Grown-Ups Settings Gear Button
  static const char* tab_names[3]  = {"Catalog", "Series", "Recently Played"};
  static const int   tab_widths[3] = {96, 82, 164};
  static const int   tab_xs[3]     = {386, 490, 580};
  for(int t = 0; t < 3; t++){
    lv_obj_t* tb = lv_btn_create(nav);
    s_tab_btns[t] = tb;
    lv_obj_set_size(tb, tab_widths[t], 38);
    lv_obj_set_pos(tb, tab_xs[t], 5);
    lv_obj_set_style_radius(tb, 19, 0);
    bool sel = (t == (int)s_active_tab);
    lv_obj_set_style_bg_color(tb, sel ? lv_color_hex(0xE5352B) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(tb, 1, 0);
    lv_obj_set_style_border_color(tb, sel ? lv_color_hex(0xE5352B) : lv_color_hex(0xE7E0D2), 0);
    lv_obj_set_style_pad_all(tb, 0, 0);
    lv_obj_set_style_shadow_width(tb, 0, 0);
    lv_obj_add_event_cb(tb, tab_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t);

    lv_obj_t* tl = lv_label_create(tb);
    lv_label_set_text(tl, tab_names[t]);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tl, sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x23303A), 0);
    lv_obj_clear_flag(tl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(tl);
  }

  // 4. Far Right: Grown-Ups Settings Gear Button
  lv_obj_t* top_gear = lv_btn_create(nav);
  lv_obj_set_size(top_gear, 40, 38);
  lv_obj_set_pos(top_gear, 752, 5);
  lv_obj_set_style_radius(top_gear, 19, 0);
  lv_obj_set_style_bg_color(top_gear, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(top_gear, lv_color_hex(0xE5352B), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(top_gear, 1, 0);
  lv_obj_set_style_border_color(top_gear, lv_color_hex(0xE7E0D2), 0);
  lv_obj_set_style_shadow_width(top_gear, 0, 0);
  lv_obj_add_event_cb(top_gear, [](lv_event_t*){
      s_scr_prev = s_scr_home;
    open_pin_modal(false);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* tgi = lv_label_create(top_gear);
  lv_label_set_text(tgi, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_font(tgi, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(tgi, lv_color_hex(0x7A7060), 0);
  lv_obj_clear_flag(tgi, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(tgi);

  // Scrollable grid container (offset 48px for the top nav tab bar)
  lv_obj_t* cont=lv_obj_create(scr);
  s_grid=cont;
  lv_obj_set_size(cont, LCD_H_RES, 362);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_bg_color(cont, lv_color_hex(0xFBF7F0), 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 18, 0);
  lv_obj_set_style_pad_row(cont, 20, 0);
  lv_obj_set_style_pad_column(cont, 20, 0);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_event_cb(cont, [](lv_event_t* e){
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SCROLL_END){
      update_visible_thumbs(true);
    }
  }, LV_EVENT_ALL, NULL);

  populate_grid();

  // bottom status bar (Now Playing bar mirroring kiosk PlayerScreen.renderNowbar)
  s_bar=lv_obj_create(scr);
  lv_obj_set_size(s_bar, LCD_H_RES, 70);
  lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_side(s_bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(s_bar, 1, 0);
  lv_obj_set_style_border_color(s_bar, lv_color_hex(0xE7E0D2), 0);
  lv_obj_set_style_radius(s_bar, 0, 0);
  lv_obj_set_style_pad_all(s_bar, 0, 0);
  lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
  update_home_bar();
}

void reload_catalog_and_thumbs(bool refetch_standalone){
  unbind_grid_tiles();
  for(size_t i = 0; i < s_cards.size(); i++){
    s_cards[i].stream_fail_t = 0;
  }
  s_pc_cursor = 0;
  s_pc_pass = 0;
  s_pc_fail = 0;
  s_pc_done = false;

  if(cfg::op_mode == "core" && cfg::core_url.length() > 0){
    fetch_library_core();
  } else if(refetch_standalone){
    fetch_library();
  } else {
    build_catalog_from_library();
  }

  core::s_catalog_dirty = false;
  if(s_scr_home){
    build_grid();
    load_thumbs();
  }
}

