#pragma once
// Home Catalog tile badges, PSRAM LRU thumbnail pool, and idle WiFi cover streaming.

static void tile_cb(lv_event_t* e){
  int i=(int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
  if(i<0 || i>=s_card_count) return;
  if(s_cards[i].daily) play_yoto_daily(i);   // one tap → today's episode (no chapter list)
  else                 open_detail(i);
}



// Add Kiosk badges (ABS / YOTO source badge, ★ highlight badge, #sequence badge, and optional title fallback)
// Optimized to reuse existing child objects and avoid lv_obj_clean churn / memory fragmentation
static void decorate_tile(lv_obj_t* tile, int card_idx, bool show_seq, int seq_pos){
  if(!tile || card_idx < 0 || card_idx >= s_card_count) return;
  const Card& c = s_cards[card_idx];
  bool has_cover = (c.pool_slot >= 0 && s_thumb_pool[c.pool_slot].buf != nullptr);

  lv_obj_t* title_lbl = nullptr;
  lv_obj_t* star_lbl  = nullptr;
  lv_obj_t* seq_lbl   = nullptr;
  uint32_t cnt = lv_obj_get_child_cnt(tile);
  for(uint32_t idx = 0; idx < cnt; idx++){
    lv_obj_t* ch = lv_obj_get_child(tile, idx);
    void* ud = lv_obj_get_user_data(ch);
    if(ud == (void*)1001) title_lbl = ch;
    else if(ud == (void*)1002) star_lbl = ch;
    else if(ud == (void*)1003) seq_lbl = ch;
  }

  // Fallback title label
  if(!has_cover){
    if(!title_lbl){
      title_lbl = lv_label_create(tile);
      lv_obj_set_user_data(title_lbl, (void*)1001);
      lv_obj_set_width(title_lbl, THUMB_W - 20);
      lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFFFFFF), 0);
      lv_obj_clear_flag(title_lbl, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_center(title_lbl);
    }
    lv_label_set_text(title_lbl, c.title.c_str());
    lv_obj_clear_flag(title_lbl, LV_OBJ_FLAG_HIDDEN);
  } else if(title_lbl){
    lv_obj_add_flag(title_lbl, LV_OBJ_FLAG_HIDDEN);
  }

  // 1. Highlight Star Badge (top-left) - ONLY show in main Catalog tab, NOT in Series or Recently Played
  bool show_star = c.is_highlight && (s_active_tab == TAB_ALL);
  if(show_star){
    if(!star_lbl){
      star_lbl = lv_label_create(tile);
      lv_obj_set_user_data(star_lbl, (void*)1002);
      lv_label_set_text(star_lbl, " * ");
      lv_obj_set_style_text_font(star_lbl, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(star_lbl, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_bg_color(star_lbl, lv_color_hex(0xF59E0B), 0);
      lv_obj_set_style_bg_opa(star_lbl, LV_OPA_90, 0);
      lv_obj_set_style_radius(star_lbl, 6, 0);
      lv_obj_set_style_pad_left(star_lbl, 4, 0);
      lv_obj_set_style_pad_right(star_lbl, 4, 0);
      lv_obj_set_style_pad_top(star_lbl, 2, 0);
      lv_obj_set_style_pad_bottom(star_lbl, 0, 0);
      lv_obj_clear_flag(star_lbl, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_align(star_lbl, LV_ALIGN_TOP_LEFT, 6, 6);
    }
    lv_obj_clear_flag(star_lbl, LV_OBJ_FLAG_HIDDEN);
  } else if(star_lbl){
    lv_obj_add_flag(star_lbl, LV_OBJ_FLAG_HIDDEN);
  }

  // 2. Sequence Badge in Series Level 2 view (top-right)
  if(show_seq && s_active_tab == TAB_SERIES && s_selected_series_filter.length() > 0){
    float cur_seq = -1.0f;
    String cur_disp = "";
    get_card_seq_info(c, s_selected_series_filter, cur_seq, cur_disp);

    String s_txt = "";
    if(cur_disp.length() > 0){
      s_txt = cur_disp.startsWith("#") ? cur_disp : (String("#") + cur_disp);
    } else if(cur_seq >= 0.0f){
      int iv = (int)cur_seq;
      if(fabsf(cur_seq - (float)iv) < 0.01f) s_txt = String("#") + String(iv);
      else s_txt = String("#") + String(cur_seq, 1);
    } else if(seq_pos > 0){
      s_txt = String("#") + String(seq_pos);
    } else if(seq_lbl && !lv_obj_has_flag(seq_lbl, LV_OBJ_FLAG_HIDDEN)){
      s_txt = lv_label_get_text(seq_lbl);
    }

    if(s_txt.length() > 0){
      if(!seq_lbl){
        seq_lbl = lv_label_create(tile);
        lv_obj_set_user_data(seq_lbl, (void*)1003);
        lv_obj_set_style_text_font(seq_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(seq_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_color(seq_lbl, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_bg_opa(seq_lbl, LV_OPA_90, 0);
        lv_obj_set_style_radius(seq_lbl, 6, 0);
        lv_obj_set_style_pad_left(seq_lbl, 6, 0);
        lv_obj_set_style_pad_right(seq_lbl, 6, 0);
        lv_obj_set_style_pad_top(seq_lbl, 2, 0);
        lv_obj_set_style_pad_bottom(seq_lbl, 2, 0);
        lv_obj_clear_flag(seq_lbl, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(seq_lbl, LV_ALIGN_TOP_RIGHT, -6, 6);
      }
      lv_label_set_text(seq_lbl, s_txt.c_str());
      lv_obj_clear_flag(seq_lbl, LV_OBJ_FLAG_HIDDEN);
    } else if(seq_lbl){
      lv_obj_add_flag(seq_lbl, LV_OBJ_FLAG_HIDDEN);
    }
  } else if(seq_lbl){
    lv_obj_add_flag(seq_lbl, LV_OBJ_FLAG_HIDDEN);
  }
}

static bool is_tile_visible(lv_obj_t* tile){
  if(!tile || !lv_obj_is_valid(tile)) return false;
  lv_area_t a;
  lv_obj_get_coords(tile, &a);
  // Ignore tiles that have not yet undergone initial layout
  if(lv_area_get_width(&a) <= 0 || lv_area_get_height(&a) <= 0) return false;
  return (a.y2 >= -40 && a.y1 <= (LCD_V_RES + 40));
}

static int acquire_thumb_slot(int card_idx){
  if(!s_thumb_pool[0].buf) init_thumb_pool();
  for(int p = 0; p < THUMB_POOL_SIZE; p++){
    if(s_thumb_pool[p].card_idx < 0){
      return p;
    }
  }

  int best_slot = -1;
  uint32_t oldest_time = 0xFFFFFFFF;
  for(int p = 0; p < THUMB_POOL_SIZE; p++){
    int c_idx = s_thumb_pool[p].card_idx;
    bool visible = false;
    if(c_idx >= 0 && c_idx < s_card_count && s_cards[c_idx].tile){
      visible = is_tile_visible(s_cards[c_idx].tile);
    }
    if(!visible){
      if(s_thumb_pool[p].last_use < oldest_time){
        oldest_time = s_thumb_pool[p].last_use;
        best_slot = p;
      }
    }
  }

  if(best_slot < 0){
    for(int p = 0; p < THUMB_POOL_SIZE; p++){
      if(s_thumb_pool[p].last_use < oldest_time){
        oldest_time = s_thumb_pool[p].last_use;
        best_slot = p;
      }
    }
  }

  if(best_slot >= 0){
    int old_idx = s_thumb_pool[best_slot].card_idx;
    if(old_idx >= 0 && old_idx < s_card_count){
      s_cards[old_idx].pool_slot = -1;
      if(s_cards[old_idx].tile && lv_obj_is_valid(s_cards[old_idx].tile)){
        lv_obj_set_style_bg_img_src(s_cards[old_idx].tile, NULL, 0);
        decorate_tile(s_cards[old_idx].tile, old_idx, s_active_tab == TAB_SERIES && s_selected_series_filter.length() > 0);
        lv_obj_invalidate(s_cards[old_idx].tile);
      }
    }
    s_thumb_pool[best_slot].card_idx = -1;
  }
  return best_slot;
}

bool load_thumb_for_card(int card_idx, bool force_refr){
  if(card_idx < 0 || card_idx >= s_card_count) return false;
  Card& c = s_cards[card_idx];

  if(c.pool_slot >= 0 && c.pool_slot < THUMB_POOL_SIZE){
    s_thumb_pool[c.pool_slot].last_use = millis();
    return true;
  }

  bool have_sd = s_sd_ok && sd_cache_exists(c.id.c_str(), THUMB_W, c.cover.c_str());
  if(!have_sd && !force_refr && s_sd_ok){
    return false;
  }

  int slot = acquire_thumb_slot(card_idx);
  if(slot < 0 || !s_thumb_pool[slot].buf) return false;

  bool ok = false;
  if(have_sd){
    ok = sd_cache_read(c.id.c_str(), THUMB_W, s_thumb_pool[slot].buf, THUMB_SZ, c.cover.c_str());
  } else if(c.cover.length() > 0 && WiFi.status() == WL_CONNECTED){
    ok = load_cover_cached(c.id.c_str(), c.cover.c_str(), THUMB_W, THUMB_H, THUMB_SZ, s_thumb_pool[slot].buf);
  }

  if(!ok){
    s_thumb_pool[slot].card_idx = -1;
    return false;
  }

  s_thumb_pool[slot].card_idx = card_idx;
  s_thumb_pool[slot].last_use = millis();
  c.pool_slot = slot;

  if(c.tile && lv_obj_is_valid(c.tile)){
    lv_obj_set_style_bg_img_src(c.tile, &s_thumb_pool[slot].dsc, 0);
    if(s_active_tab != TAB_SERIES || s_selected_series_filter.length() > 0) decorate_tile(c.tile, card_idx, s_active_tab == TAB_SERIES && s_selected_series_filter.length() > 0);
    lv_obj_invalidate(c.tile);
  }

  return true;
}

void update_visible_thumbs(bool force_refr){
  if(!s_grid) return;
  int loaded_pass = 0;
  for(int i = 0; i < s_card_count; i++){
    if(s_cards[i].tile && is_tile_visible(s_cards[i].tile)){
      if(s_cards[i].pool_slot < 0){
        if(!force_refr && loaded_pass >= 2) continue;
        if(load_thumb_for_card(i, force_refr)){
          loaded_pass++;
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      } else {
        s_thumb_pool[s_cards[i].pool_slot].last_use = millis();
      }
    }
  }
}

// User interaction detector: Returns true if user touched recently or grid is in motion
inline bool is_ui_interacting(){
  if(s_grid && lv_obj_is_scrolling(s_grid)) return true;
  if(lv_disp_get_inactive_time(NULL) < 1500) return true;
  return false;
}

// On-demand visible cover streaming:
// When scrolling lands on tiles that are not yet cached on MicroSD, stream their 160x254
// thumbnail covers over WiFi peacefully when idle so placeholders turn into covers without UI lag.
inline bool stream_visible_uncached_thumb(){
  if(is_ui_interacting()) return false;
  if(!s_grid || s_card_count <= 0) return false;
  lv_obj_t* act = lv_scr_act();
  if(act != s_scr_home) return false;

  for(int i = 0; i < s_card_count; i++){
    if(s_cards[i].tile && is_tile_visible(s_cards[i].tile)){
      if(s_cards[i].pool_slot >= 0) continue;

      // If it exists on SD, load it immediately into an LRU PSRAM slot
      if(sd_cache_exists(s_cards[i].id.c_str(), THUMB_W, s_cards[i].cover.c_str())){
        if(load_thumb_for_card(i, true)) return true;
      }

      // Check failure backoff (skip cards that failed within 20 seconds)
      if(s_cards[i].stream_fail_t > 0 && (millis() - s_cards[i].stream_fail_t < 20000)){
        continue;
      }

      // Visible tile is missing its cover: load via SD or direct WiFi streaming
      if(s_cards[i].cover.length() > 0){
        if(load_thumb_for_card(i, true)){
          log_msg("[STREAM] Loaded cover for card %d: '%s' (sd_ok=%d)", i, s_cards[i].title.c_str(), s_sd_ok ? 1 : 0);
          return true;
        } else {
          s_cards[i].stream_fail_t = millis();
        }
      }
    }
  }
  return false;
}

static lv_obj_t* create_card_tile(lv_obj_t* parent, int i, bool show_seq, int seq_pos = -1){
  Card& c = s_cards[i];
  lv_obj_t* tile = lv_obj_create(parent);
  lv_obj_set_size(tile, THUMB_W, THUMB_H);
  lv_obj_set_style_bg_color(tile, c.is_abs() ? lv_color_hex(0x312E81) : lv_color_hex(0xE5352B), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(tile, 14, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_translate_y(tile, 3, LV_STATE_PRESSED);
  lv_obj_set_style_opa(tile, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_set_user_data(tile, (void*)(intptr_t)i);
  lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, NULL);
  c.tile = tile;
  if(c.pool_slot >= 0 && c.pool_slot < THUMB_POOL_SIZE && s_thumb_pool[c.pool_slot].buf && s_thumb_pool[c.pool_slot].card_idx == i){
    lv_obj_set_style_bg_img_src(tile, &s_thumb_pool[c.pool_slot].dsc, 0);
  } else {
    c.pool_slot = -1;
    lv_obj_set_style_bg_img_src(tile, NULL, 0);
  }
  decorate_tile(tile, i, show_seq, seq_pos);
  return tile;
}

