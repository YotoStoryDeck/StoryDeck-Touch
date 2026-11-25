#pragma once
// Home Catalog and Detail screen persistent bottom Now Playing mini-bar.

static void minibar_open_cb(lv_event_t*){
  if(!s_playing && s_play_title.length() == 0 && s_play_card < 0) return;
  s_cur_card = s_play_card;
  if(s_play_card >= 0 && s_play_card < s_card_count && s_cards[s_play_card].daily){
    s_cur_card = s_play_card; s_detail_title = "Yoto Daily";
    s_ch_count = 1; s_cur_ch = 0;
    s_ch[0].key = "daily"; s_ch[0].title = s_play_chapter; s_ch[0].dur = s_play_dur;
    open_now();
    return;
  }
  if(s_play_card >= 0 && s_play_card < s_card_count){
    if(s_cur_card != s_play_card){
      s_cur_card = s_play_card;
      if(!sd_chap_read(s_cards[s_play_card].id.c_str())) fetch_card_detail(s_play_card);
    }
    s_detail_title = s_cards[s_play_card].title.c_str();
  } else {
    s_cur_card = -1;
    s_detail_title = s_play_title;
    s_ch_count = 0;
  }
  s_cur_ch = s_play_ch;
  open_now();
}
static void minibar_toggle_cb(lv_event_t*){ np_toggle(); }


// Reusable Nowbar builder (100% kiosk PlayerScreen.renderNowbar parity; shared by Catalog & Detail screens)
static void populate_nowbar_obj(lv_obj_t* bar, bool is_home_bar){
  if(!bar) return;
  lv_obj_clean(bar);

  bool has_active = (s_playing || s_play_title.length() > 0 || (s_play_card >= 0 && s_play_card < s_card_count));
  if(!has_active){
    // Idle state: musical note icon badge + "Nothing playing — tap a book to listen"
    lv_obj_t* mb = lv_obj_create(bar);
    lv_obj_set_size(mb, 38, 38);
    lv_obj_set_style_radius(mb, 10, 0);
    lv_obj_set_style_bg_color(mb, lv_color_hex(0xF3ECE0), 0);
    lv_obj_set_style_border_width(mb, 0, 0);
    lv_obj_clear_flag(mb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(mb, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_t* mbi = lv_label_create(mb);
    lv_label_set_text(mbi, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(mbi, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(mbi, lv_color_hex(0x8A8276), 0);
    lv_obj_center(mbi);

    lv_obj_t* lbl = lv_label_create(bar);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x9A9384), 0);
    lv_label_set_text(lbl, is_home_bar ? "Nothing playing  -  tap a book to listen" : "Nothing playing  -  tap a chapter to listen");
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 68, 0);
    if(is_home_bar) s_barlabel = lbl;
    return;
  }
  if(is_home_bar) s_barlabel = nullptr;

  // Far right: circular red Play/Pause button
  lv_obj_t* pp=lv_btn_create(bar);
  lv_obj_set_size(pp, 54, 54);
  lv_obj_align(pp, LV_ALIGN_RIGHT_MID, -16, 0);
  lv_obj_set_style_radius(pp, 27, 0);
  lv_obj_set_style_bg_color(pp, lv_color_hex(0xE5352B), 0);
  lv_obj_set_style_shadow_width(pp, 0, 0);
  lv_obj_add_event_cb(pp, minibar_toggle_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* ppi=lv_label_create(pp);
  lv_label_set_text(ppi, s_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  lv_obj_set_style_text_font(ppi, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(ppi, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(ppi);

  // Tappable title/chapter block -> opens full Now Playing screen (expanded to full width)
  lv_obj_t* tap=lv_obj_create(bar);
  lv_obj_set_size(tap, 680, 64);
  lv_obj_align(tap, LV_ALIGN_LEFT_MID, 18, 0);
  lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tap, 0, 0);
  lv_obj_set_style_pad_all(tap, 0, 0);
  lv_obj_clear_flag(tap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tap, minibar_open_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* tt=lv_label_create(tap);
  lv_label_set_long_mode(tt, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tt, 670);
  lv_label_set_text_fmt(tt, LV_SYMBOL_AUDIO "  %s", s_play_title.c_str());
  lv_obj_set_style_text_font(tt, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(tt, lv_color_hex(0x23303A), 0);
  lv_obj_align(tt, LV_ALIGN_LEFT_MID, 0, -11);

  lv_obj_t* tc=lv_label_create(tap);
  lv_label_set_long_mode(tc, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tc, 670);
  if(s_play_author.length() > 0 && s_play_chapter.length() > 0){
    lv_label_set_text_fmt(tc, "%s  \xE2\x80\xA2  %s", s_play_author.c_str(), s_play_chapter.c_str());
  } else {
    lv_label_set_text(tc, s_play_chapter.c_str());
  }
  lv_obj_set_style_text_font(tc, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(tc, lv_color_hex(0x9A9384), 0);
  lv_obj_align(tc, LV_ALIGN_LEFT_MID, 28, 14);
}

// Rebuild the home bottom bar (and Detail screen bottom bar if visible)
static void update_home_bar(){
  if(s_top_dev_dot) lv_obj_set_style_bg_color(s_top_dev_dot, get_player_dot_color(cfg::device_id), 0);
  if(s_bar) populate_nowbar_obj(s_bar, true);
  if(s_detail_bar && s_scr_detail) populate_nowbar_obj(s_detail_bar, false);
}
