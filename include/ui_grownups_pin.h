#pragma once
// Grown-Ups Parent PIN keypad modal (verify, create, confirm, unlock flows).

// ---------------- Parent PIN Keypad Modal (pin.js 3-step parity) ----------------
enum PinFlowMode {
  PIN_MODE_UNLOCK = 0,
  PIN_MODE_VERIFY_OLD = 1,
  PIN_MODE_NEW_SETUP = 2,
  PIN_MODE_NEW_CONFIRM = 3
};
static PinFlowMode s_pin_mode = PIN_MODE_UNLOCK;
static String s_pin_entered = "";
static String s_pin_new_temp = "";
static int    s_pin_fails = 0;
static uint32_t s_pin_lockout_until = 0;
static lv_obj_t* s_pin_dots[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t* s_pin_title_lbl = nullptr;
static lv_obj_t* s_pin_msg_lbl = nullptr;
static lv_obj_t* s_scr_prev_pin = nullptr;

static const char* pin_mode_title(PinFlowMode m){
  switch(m){
    case PIN_MODE_VERIFY_OLD:  return "Verify Current PIN";
    case PIN_MODE_NEW_SETUP:   return "Create Parent PIN";
    case PIN_MODE_NEW_CONFIRM: return "Confirm Parent PIN";
    default:                   return "Parent PIN Required";
  }
}
static const char* pin_mode_prompt(PinFlowMode m){
  switch(m){
    case PIN_MODE_VERIFY_OLD:  return "Enter current 4-digit PIN";
    case PIN_MODE_NEW_SETUP:   return "Choose a 4-digit PIN for Grown-ups settings";
    case PIN_MODE_NEW_CONFIRM: return "Re-enter your 4-digit PIN to confirm";
    default:                   return "Enter 4-digit PIN to access Grown-ups settings";
  }
}

static void update_pin_dots(){
  int len = s_pin_entered.length();
  for(int i = 0; i < 4; i++){
    if(s_pin_dots[i]){
      if(i < len){
        lv_obj_set_style_bg_color(s_pin_dots[i], lv_color_hex(0xE5352B), 0);
      } else {
        lv_obj_set_style_bg_color(s_pin_dots[i], lv_color_hex(0xE2DCD2), 0);
      }
    }
  }
}

static void close_pin_modal(){
  s_pin_entered = "";
  s_pin_new_temp = "";
  if(s_scr_pin){
    lv_obj_t* p = s_scr_pin;
    s_scr_pin = nullptr;
    if(s_scr_prev_pin){
      lv_scr_load(s_scr_prev_pin);
      s_scr_prev_pin = nullptr;
    } else if(s_scr_prev){
      lv_scr_load(s_scr_prev);
    } else if(s_scr_home){
      lv_scr_load(s_scr_home);
    } else if(s_scr_error){
      lv_scr_load(s_scr_error);
    }
    lv_obj_del_async(p);
  }
}

static void pin_digit_cb(lv_event_t* e){
  if(millis() < s_pin_lockout_until) return; // locked out
  const char* val = (const char*)lv_event_get_user_data(e);
  if(!val) return;

  if(strcmp(val, "C") == 0){
    s_pin_entered = "";
    update_pin_dots();
    if(s_pin_msg_lbl) lv_label_set_text(s_pin_msg_lbl, pin_mode_prompt(s_pin_mode));
    return;
  }
  if(strcmp(val, "<") == 0){
    if(s_pin_entered.length() > 0){
      s_pin_entered.remove(s_pin_entered.length() - 1);
      update_pin_dots();
    }
    return;
  }

  if(s_pin_entered.length() < 4){
    s_pin_entered += val[0];
    update_pin_dots();
  }

  // Auto-validate once 4 digits entered
  if(s_pin_entered.length() == 4){
    // Step 1 of Change PIN: Verify current PIN
    if(s_pin_mode == PIN_MODE_VERIFY_OLD){
      if(s_pin_entered == cfg::parent_pin){
        s_pin_fails = 0;
        s_pin_entered = "";
        s_pin_mode = PIN_MODE_NEW_SETUP;
        update_pin_dots();
        if(s_pin_title_lbl) lv_label_set_text(s_pin_title_lbl, pin_mode_title(s_pin_mode));
        if(s_pin_msg_lbl)   lv_label_set_text(s_pin_msg_lbl, pin_mode_prompt(s_pin_mode));
      } else {
        s_pin_fails++;
        s_pin_entered = "";
        update_pin_dots();
        if(s_pin_msg_lbl) lv_label_set_text(s_pin_msg_lbl, "Incorrect current PIN. Try again.");
      }
      return;
    }

    // Step 2 of Change PIN: Enter new 4-digit PIN
    if(s_pin_mode == PIN_MODE_NEW_SETUP){
      s_pin_new_temp = s_pin_entered;
      s_pin_entered = "";
      s_pin_mode = PIN_MODE_NEW_CONFIRM;
      update_pin_dots();
      if(s_pin_title_lbl) lv_label_set_text(s_pin_title_lbl, pin_mode_title(s_pin_mode));
      if(s_pin_msg_lbl)   lv_label_set_text(s_pin_msg_lbl, pin_mode_prompt(s_pin_mode));
      return;
    }

    // Step 3 of Change PIN: Confirm new 4-digit PIN
    if(s_pin_mode == PIN_MODE_NEW_CONFIRM){
      if(s_pin_entered == s_pin_new_temp){
        cfg::parent_pin = s_pin_entered;
        cfg::save();
        sync_settings_if_core();
        if(s_pin_msg_lbl) lv_label_set_text(s_pin_msg_lbl, "PIN changed successfully!");
        lv_refr_now(lv_disp_get_default());
        delay(500);
        close_pin_modal();
      } else {
        s_pin_entered = "";
        s_pin_new_temp = "";
        s_pin_mode = PIN_MODE_NEW_SETUP;
        update_pin_dots();
        if(s_pin_title_lbl) lv_label_set_text(s_pin_title_lbl, pin_mode_title(s_pin_mode));
        if(s_pin_msg_lbl)   lv_label_set_text(s_pin_msg_lbl, "PINs did not match. Enter new PIN again.");
      }
      return;
    }

    // Normal Unlock Mode
    if(s_pin_entered == cfg::parent_pin){
      s_pin_fails = 0;
      s_pin_entered = "";
      s_scr_prev_pin = nullptr;
      if(s_scr_pin){
        lv_obj_t* p = s_scr_pin;
        s_scr_pin = nullptr;
        open_settings();
        lv_obj_del_async(p);
      }
    } else {
      s_pin_fails++;
      s_pin_entered = "";
      update_pin_dots();
      if(s_pin_fails >= 3){
        s_pin_lockout_until = millis() + 10000;
        if(s_pin_msg_lbl) lv_label_set_text(s_pin_msg_lbl, "Too many attempts! Locked for 10s");
      } else {
        if(s_pin_msg_lbl) lv_label_set_text(s_pin_msg_lbl, "Incorrect PIN. Try again.");
      }
    }
  }
}

void open_pin_modal(bool for_settings_change){
  if(for_settings_change){
    s_scr_prev_pin = lv_scr_act();
    s_pin_mode = (cfg::parent_pin.length() > 0) ? PIN_MODE_VERIFY_OLD : PIN_MODE_NEW_SETUP;
  } else {
    s_scr_prev = lv_scr_act();
    s_scr_prev_pin = s_scr_prev;
    s_pin_mode = PIN_MODE_UNLOCK;
  }
  s_pin_entered = "";
  s_pin_new_temp = "";

  lv_obj_t* scr = lv_obj_create(NULL);
  s_scr_pin = scr;
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xFBF7F0), 0);

  // Close / Cancel button (top-left)
  lv_obj_t* back = lv_btn_create(scr);
  lv_obj_set_size(back, 52, 52);
  lv_obj_set_pos(back, 14, 14);
  lv_obj_set_style_radius(back, 26, 0);
  lv_obj_set_style_bg_color(back, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_shadow_width(back, 8, 0);
  lv_obj_set_style_shadow_opa(back, LV_OPA_30, 0);
  lv_obj_add_event_cb(back, [](lv_event_t*){ close_pin_modal(); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* bk = lv_label_create(back);
  lv_label_set_text(bk, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(bk, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(bk, lv_color_hex(0x23303A), 0);
  lv_obj_center(bk);

  // Modal Title
  s_pin_title_lbl = lv_label_create(scr);
  lv_label_set_text(s_pin_title_lbl, pin_mode_title(s_pin_mode));
  lv_obj_set_style_text_font(s_pin_title_lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(s_pin_title_lbl, lv_color_hex(0xE5352B), 0);
  lv_obj_align(s_pin_title_lbl, LV_ALIGN_TOP_MID, 0, 20);

  // Subtitle / message
  s_pin_msg_lbl = lv_label_create(scr);
  lv_label_set_text(s_pin_msg_lbl, pin_mode_prompt(s_pin_mode));
  lv_obj_set_style_text_font(s_pin_msg_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(s_pin_msg_lbl, lv_color_hex(0x7A7060), 0);
  lv_obj_align(s_pin_msg_lbl, LV_ALIGN_TOP_MID, 0, 60);

  // 4 PIN Dots indicator container
  lv_obj_t* dcont = lv_obj_create(scr);
  lv_obj_set_size(dcont, 160, 36);
  lv_obj_align(dcont, LV_ALIGN_TOP_MID, 0, 95);
  lv_obj_set_style_bg_opa(dcont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dcont, 0, 0);
  lv_obj_set_style_pad_all(dcont, 0, 0);
  lv_obj_set_flex_flow(dcont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dcont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(dcont, LV_OBJ_FLAG_SCROLLABLE);

  for(int i = 0; i < 4; i++){
    s_pin_dots[i] = lv_obj_create(dcont);
    lv_obj_set_size(s_pin_dots[i], 20, 20);
    lv_obj_set_style_radius(s_pin_dots[i], 10, 0);
    lv_obj_set_style_bg_color(s_pin_dots[i], lv_color_hex(0xE2DCD2), 0);
    lv_obj_set_style_border_width(s_pin_dots[i], 0, 0);
  }

  // 3x4 Keypad Container
  lv_obj_t* kpad = lv_obj_create(scr);
  lv_obj_set_size(kpad, 360, 310);
  lv_obj_align(kpad, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_bg_opa(kpad, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(kpad, 0, 0);
  lv_obj_set_style_pad_all(kpad, 0, 0);
  lv_obj_set_style_pad_row(kpad, 10, 0);
  lv_obj_set_style_pad_column(kpad, 16, 0);
  lv_obj_set_flex_flow(kpad, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(kpad, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(kpad, LV_OBJ_FLAG_SCROLLABLE);

  static const char* keys[12] = {
    "1", "2", "3",
    "4", "5", "6",
    "7", "8", "9",
    "C", "0", "<"
  };

  for(int i = 0; i < 12; i++){
    lv_obj_t* btn = lv_btn_create(kpad);
    lv_obj_set_size(btn, 98, 62);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xF3E9D8), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xE7E0D2), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, pin_digit_cb, LV_EVENT_CLICKED, (void*)keys[i]);

    lv_obj_t* l = lv_label_create(btn);
    lv_label_set_text(l, keys[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x23303A), 0);
    lv_obj_center(l);
  }

  lv_scr_load(scr);
  lv_refr_now(lv_disp_get_default());
}

