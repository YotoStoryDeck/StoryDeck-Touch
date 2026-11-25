#pragma once
// Grown-Ups Settings Screen and Parent PIN Keypad Modal (Full Web Kiosk Parity)
#include <Arduino.h>
#include <lvgl.h>
#include "app_config.h"
#include "core_api.h"
#include "hal_display.h"
#include "app_state.h"

extern int         s_sleep_sec;
extern int         s_vol;
extern lv_obj_t*   s_scr_home;
extern lv_obj_t*   s_scr_settings;
extern lv_obj_t*   s_scr_pin;
extern lv_obj_t*   s_scr_prev;
extern lv_obj_t*   s_scr_error;
extern int         s_card_count;
extern String      get_active_player_name();
extern lv_obj_t*   s_set_sleep_lbl;

extern void fmt_sleep(char* b, size_t n);
extern void np_volume(int v);
extern void go_home();
extern void build_grid();
extern void start_hotspot_cb(lv_event_t* e);

void open_settings();             // fwd
void open_pin_modal(bool for_settings_change); // fwd

static void sync_settings_if_core(){
  if(cfg::op_mode == "core" && cfg::core_url.length() > 0){
    core::sync_device_settings(cfg::core_url);
  }
}


#include "ui_grownups_pin.h"
#include "ui_grownups_tabs.h"

void open_settings(){
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xFBF7F0), 0);

  // Header Back Button
  lv_obj_t* back = lv_btn_create(scr);
  lv_obj_set_size(back, 50, 50);
  lv_obj_set_pos(back, 14, 12);
  lv_obj_set_style_radius(back, 25, 0);
  lv_obj_set_style_bg_color(back, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_shadow_width(back, 6, 0);
  lv_obj_set_style_shadow_opa(back, LV_OPA_30, 0);
  lv_obj_add_event_cb(back, [](lv_event_t*){
    if(!s_lib_ok){
      s_set_sleep_lbl = nullptr;
      if(s_scr_settings){ lv_obj_del_async(s_scr_settings); s_scr_settings=nullptr; }
      s_scr_prev = nullptr;
      net_init_and_fetch();
      return;
    }
    go_home();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* bk = lv_label_create(back);
  lv_label_set_text(bk, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(bk, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(bk, lv_color_hex(0x23303A), 0);
  lv_obj_center(bk);

  // Title
  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "Grown-ups Settings");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5352B), 0);
  lv_obj_set_pos(title, 76, 22);

  // Change PIN button
  lv_obj_t* chg_pin = pill_btn(scr, "Change PIN", 0, [](lv_event_t*){ open_pin_modal(true); });
  lv_obj_set_size(chg_pin, 140, 42);
  lv_obj_set_pos(chg_pin, LCD_H_RES - 156, 16);
  lv_obj_set_style_text_font(lv_obj_get_child(chg_pin, 0), &lv_font_montserrat_18, 0);

  // 4-Tab Navigation Bar (grownups.js parity)
  lv_obj_t* nav = lv_obj_create(scr);
  lv_obj_set_size(nav, LCD_H_RES - 28, 48);
  lv_obj_set_pos(nav, 14, 68);
  lv_obj_set_style_bg_color(nav, lv_color_hex(0xF4EDE0), 0);
  lv_obj_set_style_border_width(nav, 0, 0);
  lv_obj_set_style_pad_all(nav, 4, 0);
  lv_obj_set_style_pad_column(nav, 8, 0);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

  static const char* tab_titles[4] = {"Sleep / Bed", "Audio", "Display / Dim", "Network"};
  for(int t = 0; t < 4; t++){
    lv_obj_t* tb = lv_btn_create(nav);
    lv_obj_set_size(tb, 180, 40);
    lv_obj_set_style_radius(tb, 20, 0);
    bool sel = (t == (int)s_gu_active_tab);
    lv_obj_set_style_bg_color(tb, sel ? lv_color_hex(0xE5352B) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(tb, 1, 0);
    lv_obj_set_style_border_color(tb, lv_color_hex(0xE7E0D2), 0);
    lv_obj_set_style_shadow_width(tb, 0, 0);
    lv_obj_add_event_cb(tb, gu_tab_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t);

    lv_obj_t* tl = lv_label_create(tb);
    lv_label_set_text(tl, tab_titles[t]);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(tl, sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x23303A), 0);
    lv_obj_center(tl);

    s_gu_tab_btns[t] = tb;
  }

  // Common content area below tab bar
  const int cont_y = 122;
  const int cont_h = LCD_V_RES - cont_y - 10;

  for(int i = 0; i < 4; i++){
    lv_obj_t* panel = lv_obj_create(scr);
    lv_obj_set_size(panel, LCD_H_RES - 28, cont_h);
    lv_obj_set_pos(panel, 14, cont_y);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    s_gu_tab_panels[i] = panel;
  }

  build_gu_tab_sleep(s_gu_tab_panels[0]);
  build_gu_tab_audio(s_gu_tab_panels[1]);
  build_gu_tab_display(s_gu_tab_panels[2]);
  build_gu_tab_network(s_gu_tab_panels[3]);

  // Set initial active tab visibility
  gu_tab_switch(s_gu_active_tab);

  lv_obj_t* old_s = s_scr_settings;
  s_scr_settings = scr;
  lv_scr_load(scr);
  if(old_s && old_s != scr) lv_obj_del_async(old_s);
}
