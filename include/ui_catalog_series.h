#pragma once
// Home Catalog series sequence calculation, metadata grouping, and series tiles.
// Expects app_state.h, storage_sd.h, and catalog forward declarations via ui_catalog.h.

inline void open_series_view(const PsramString& sname){
  s_active_tab = TAB_SERIES;
  s_selected_series_filter = sname;
  for(int i = 0; i < 3; i++){
    lv_obj_t* b = s_tab_btns[i];
    if(!b) continue;
    bool sel = (i == 1); // 1 = TAB_SERIES
    lv_obj_set_style_bg_color(b, sel ? lv_color_hex(0xE5352B) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(b, sel ? lv_color_hex(0xE5352B) : lv_color_hex(0xE7E0D2), 0);
    lv_obj_t* l = lv_obj_get_child(b, 0);
    if(l) lv_obj_set_style_text_color(l, sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x23303A), 0);
  }
  go_home();
  populate_grid();
}



static void tab_btn_cb(lv_event_t* e){
  int tab = (int)(intptr_t)lv_event_get_user_data(e);
  if(tab < 0 || tab > 2) return;
  s_selected_series_filter = "";
  const char* tname = (tab == 1) ? "Series" : (tab == 2 ? "Recently Played" : "Catalog");
  Serial.printf("[TAB] Switched catalog tab to %d (%s)\n", tab, tname);
  s_active_tab = (TabFilter)tab;
  for(int i = 0; i < 3; i++){
    lv_obj_t* b = s_tab_btns[i];
    if(!b) continue;
    bool sel = (i == tab);
    lv_obj_set_style_bg_color(b, sel ? lv_color_hex(0xE5352B) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(b, sel ? lv_color_hex(0xE5352B) : lv_color_hex(0xE7E0D2), 0);
    lv_obj_t* lbl = lv_obj_get_child(b, 0);
    if(lbl) lv_obj_set_style_text_color(lbl, sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x23303A), 0);
  }
  populate_grid();
  lv_refr_now(lv_disp_get_default());
}


// Returns the resolved sequence float and display string for a card in the given series context
static void get_card_seq_info(const Card& c, const PsramString& target_grp, float& out_seq, String& out_disp){
  out_seq = -1.0f;
  out_disp = "";

  PsramString cs = c.series; cs.trim();
  if(cs.length() == 0) cs = "Standalone & Singles";

  // 1. Check primary series if target_grp matches or if target_grp is empty
  if(target_grp.length() == 0 || cs.equalsIgnoreCase(target_grp)){
    if(c.sequence >= 0.0f) out_seq = c.sequence;
    if(c.seq_display.length() > 0) out_disp = c.seq_display.c_str();
  }

  // 2. Check extra_series
  for(int ex = 0; ex < c.extra_series_cnt; ex++){
    if(c.extra_series[ex].equalsIgnoreCase(target_grp)){
      if(c.extra_seq[ex] >= 0.0f) out_seq = c.extra_seq[ex];
      if(c.extra_seq_disp[ex].length() > 0) out_disp = c.extra_seq_disp[ex].c_str();
      break;
    }
  }

  // 3. Fallback to primary if target_grp had no sequence
  if(out_seq < 0.0f && out_disp.length() == 0){
    if(c.sequence >= 0.0f) out_seq = c.sequence;
    if(c.seq_display.length() > 0) out_disp = c.seq_display.c_str();
  }

  // 4. Parse from out_disp to float if out_seq is still < 0
  if(out_seq < 0.0f && out_disp.length() > 0){
    for(size_t i = 0; i < out_disp.length(); i++){
      if(out_disp[i] >= '0' && out_disp[i] <= '9'){
        out_seq = out_disp.substring(i).toFloat();
        break;
      }
    }
  }

  // 5. Title fallback for #<num> (e.g. Captain Underpants #01, Star Wars #0-3)
  if(out_seq < 0.0f && out_disp.length() == 0){
    String t = c.title.c_str();
    int hash_pos = t.indexOf('#');
    if(hash_pos >= 0){
      int end_pos = hash_pos + 1;
      while(end_pos < (int)t.length() && (isDigit(t[end_pos]) || t[end_pos] == '.' || t[end_pos] == '-')) end_pos++;
      if(end_pos > hash_pos + 1){
        out_disp = t.substring(hash_pos + 1, end_pos);
        out_seq = out_disp.toFloat();
      }
    }
  }
}


static std::vector<PsramString, PsramVectorAlloc<PsramString>> s_series_group_names;
// Precompute series groups, predetermined covers, and sort order once on catalog load
void precompute_series_metadata(){
  s_series_groups.clear();
  s_series_group_names.clear();

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
    for(size_t g = 0; g < s_series_groups.size(); g++){
      if(s_series_groups[g].name.equalsIgnoreCase(gname)){ found = (int)g; break; }
    }
    if(found >= 0){
      s_series_groups[found].count++;
      const Card& c = s_cards[card_index];
      if(c.series_cover_id.length() > 0 && c.id.equalsIgnoreCase(c.series_cover_id)){
        s_series_groups[found].rep_card_idx = card_index;
      } else {
        int cur_rep = s_series_groups[found].rep_card_idx;
        if(cur_rep >= 0 && s_cards[cur_rep].series_cover_id.length() > 0 && s_cards[cur_rep].id.equalsIgnoreCase(s_cards[cur_rep].series_cover_id)){
          // Already have predetermined lead cover card
        } else {
          float cur_seq = (cur_rep >= 0) ? card_seq_in_group(s_cards[cur_rep], gname) : 999999.0f;
          float new_seq = card_seq_in_group(s_cards[card_index], gname);
          if(new_seq < cur_seq) s_series_groups[found].rep_card_idx = card_index;
        }
      }
    } else {
      SeriesGroup sg;
      sg.name = gname;
      sg.count = 1;
      sg.rep_card_idx = card_index;
      s_series_groups.push_back(sg);
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

  std::vector<SeriesGroup, PsramVectorAlloc<SeriesGroup>> elig;
  for(size_t g = 0; g < s_series_groups.size(); g++){
    if(core::s_exclude_single_series && s_series_groups[g].count <= 1) continue;
    elig.push_back(s_series_groups[g]);
  }

  std::sort(elig.begin(), elig.end(), [](const SeriesGroup& a, const SeriesGroup& b){
    if(a.name == "Standalone & Singles" && b.name != "Standalone & Singles") return false;
    if(b.name == "Standalone & Singles" && a.name != "Standalone & Singles") return true;
    return clean_sort_title(a.name.c_str()) < clean_sort_title(b.name.c_str());
  });

  s_series_groups = elig;
  for(size_t g = 0; g < s_series_groups.size(); g++){
    s_series_group_names.push_back(s_series_groups[g].name);
  }
}

static void series_tile_cb(lv_event_t* e){
  int g_idx = (int)(intptr_t)lv_event_get_user_data(e);
  if(g_idx < 0 || g_idx >= (int)s_series_groups.size()){
    lv_obj_t* target = lv_event_get_current_target(e);
    if(target) g_idx = (int)(intptr_t)lv_obj_get_user_data(target);
  }
  if(g_idx >= 0 && g_idx < (int)s_series_groups.size()){
    s_selected_series_filter = s_series_groups[g_idx].name;
    Serial.printf("[SERIES] Selected series: '%s' (idx %d)\n", s_selected_series_filter.c_str(), g_idx);
    populate_grid();
    lv_refr_now(lv_disp_get_default());
  }
}
static void series_back_cb(lv_event_t*){
  s_selected_series_filter = "";
  populate_grid();
  lv_refr_now(lv_disp_get_default());
}

static lv_obj_t* create_series_tile(lv_obj_t* parent, int elig_g_idx, const PsramString& gname, int rep_card_idx, int count){
  lv_obj_t* tile = lv_obj_create(parent);
  lv_obj_set_size(tile, THUMB_W, THUMB_H);
  lv_obj_set_style_bg_color(tile, lv_color_hex(0x312E81), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(tile, 14, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_translate_y(tile, 3, LV_STATE_PRESSED);
  lv_obj_set_style_opa(tile, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_set_user_data(tile, (void*)(intptr_t)elig_g_idx);
  lv_obj_add_event_cb(tile, series_tile_cb, LV_EVENT_CLICKED, (void*)(intptr_t)elig_g_idx);

  if(rep_card_idx >= 0 && rep_card_idx < s_card_count){
    Card& rc = s_cards[rep_card_idx];
    rc.tile = tile;
    if(rc.pool_slot >= 0 && rc.pool_slot < THUMB_POOL_SIZE && s_thumb_pool[rc.pool_slot].buf && s_thumb_pool[rc.pool_slot].card_idx == rep_card_idx){
      lv_obj_set_style_bg_img_src(tile, &s_thumb_pool[rc.pool_slot].dsc, 0);
    } else {
      lv_obj_set_style_bg_img_src(tile, NULL, 0);
    }
  }

  // Top-right count pill: number-only "(7)"
  lv_obj_t* ql = lv_label_create(tile);
  lv_label_set_text_fmt(ql, "(%d)", count);
  lv_obj_set_style_text_font(ql, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ql, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(ql, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_opa(ql, LV_OPA_90, 0);
  lv_obj_set_style_radius(ql, 6, 0);
  lv_obj_set_style_pad_left(ql, 6, 0);
  lv_obj_set_style_pad_right(ql, 6, 0);
  lv_obj_set_style_pad_top(ql, 2, 0);
  lv_obj_set_style_pad_bottom(ql, 2, 0);
  lv_obj_clear_flag(ql, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(ql, LV_ALIGN_TOP_RIGHT, -6, 6);

  // Bottom translucent banner: Series title
  lv_obj_t* ban = lv_obj_create(tile);
  lv_obj_set_size(ban, THUMB_W, 52);
  lv_obj_align(ban, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(ban, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_opa(ban, LV_OPA_80, 0);
  lv_obj_set_style_radius(ban, 10, 0);
  lv_obj_set_style_pad_left(ban, 6, 0);
  lv_obj_set_style_pad_right(ban, 6, 0);
  lv_obj_set_style_pad_top(ban, 4, 0);
  lv_obj_set_style_pad_bottom(ban, 4, 0);
  lv_obj_set_style_border_width(ban, 0, 0);
  lv_obj_clear_flag(ban, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* bl = lv_label_create(ban);
  lv_label_set_text(bl, gname.c_str());
  lv_obj_set_width(bl, THUMB_W - 16);
  lv_label_set_long_mode(bl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_clear_flag(bl, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(bl);

  return tile;
}

