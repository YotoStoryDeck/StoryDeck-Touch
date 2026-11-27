#pragma once
// Web settings portal HTTP API & form POST handlers (settings POST, curation, ping, discovery, reboot).
#include "web_portal_html.h"

namespace portal {

static void handle_post(WebServer& w){
  if(!auth_ok(w)) return;
  // Blank secret fields mean "keep what's stored"; visible fields update when non-empty.
  String v;
  v = w.arg("kname"); if(v.length()) cfg::kiosk_name    = v;
  v = w.arg("ssid");  if(v.length()) cfg::wifi_ssid     = v;
  v = w.arg("pass");  if(v.length()) cfg::wifi_pass     = v;
  v = w.arg("cid");   if(v.length()) cfg::client_id     = v;
  v = w.arg("csec");  if(v.length()) cfg::client_secret = v;
  v = w.arg("mode");  if(v.length()) cfg::op_mode       = v;
  v = w.arg("curl");  cfg::core_url = v;
  String custom_tz = w.arg("tz_custom");
  custom_tz.trim();
  String chosen_tz = (custom_tz.length() > 0) ? custom_tz : w.arg("tz");
  chosen_tz.trim();
  if(chosen_tz.length() > 0){
    String parsed = parse_tz_input(chosen_tz);
    if(cfg::tz != parsed){
      cfg::tz = parsed;
      configTzTime(cfg::tz.c_str(), "pool.ntp.org", "time.nist.gov");
      log_msg("[NTP] Clock reconfigured with TZ '%s'", cfg::tz.c_str());
    }
  }
  v = w.arg("pin");   if(v.length() == 4) cfg::parent_pin = v;
  cfg::auto_update_check = (w.arg("otachk") == "1");

  // Profile assignment (Core mode only)
  if(cfg::op_mode == "standalone"){
    cfg::assigned_profile = "";
    core::s_assigned_profile = "";
  } else {
    String prof = w.arg("prof_custom");
    if(prof.length() == 0) prof = w.arg("prof");
    prof.trim();
    cfg::assigned_profile = prof;
    core::s_assigned_profile = prof;
  }

  // Target Player assignment: Permanently locked
  String dev = w.arg("devid_custom");
  if(dev.length() == 0) dev = w.arg("devid");
  dev.trim();
  if(dev.length() > 0){
    cfg::device_id = dev;
    cfg::locked_dev_id = dev;
    cfg::lock_player = true;
  }

    v = w.arg("catlim"); if(v.length()){ int n = v.toInt(); if(n >= 10 && n <= 300) cfg::catalog_card_limit = n; }
  v = w.arg("rothr");  if(v.length()){ int h = v.toInt(); if(h >= 0 && h <= 23) cfg::rotation_hour = h; }
  v = w.arg("vollim"); if(v.length()){ int vl = v.toInt(); if(vl >= 10 && vl <= 100) cfg::vol_limit = vl; }
  cfg::save();
  if(cfg::op_mode == "core" && cfg::core_url.length() > 0 && WiFi.status() == WL_CONNECTED){
    core::push_device_settings(cfg::core_url);
  }
  v = w.arg("rt");
  if(v.length()) yoto::set_refresh(v);                  // also clears the cached access token

  // Instantly apply volume limit and catalog card count / curation changes
  np_update_vol_ui();
  reload_catalog_and_thumbs(false);

  w.send(200, "text/html; charset=utf-8",
    "<!doctype html><body style='font-family:system-ui;background:#fbf7f0;padding:24px'>"
    "<h2 style='color:#3a6b50'>Saved.</h2>"
    "<p>Settings saved to device flash. Changes take effect immediately or upon reboot.</p>"
    "<p><a href='/reboot'>Reboot now</a> &middot; <a href='/settings'>Back to settings</a></p></body>");
}

static void handle_reboot(WebServer& w){
  if(!auth_ok(w)) return;
  w.send(200, "text/html; charset=utf-8",
    "<!doctype html><body style='font-family:system-ui;background:#fbf7f0;padding:24px'>"
    "<h2>Rebooting&hellip;</h2><p>Give it ~15 seconds, then <a href='/settings'>reload</a>.</p></body>");
  delay(300);                                           // let the response flush
  ESP.restart();
}

// Test ping for WiFi credentials
static void handle_ping_wifi(WebServer& w){
  if(!auth_ok(w)) return;
  String s = w.arg("ssid");
  String p = w.arg("pass");
  if(s.length() == 0) s = cfg::wifi_ssid;
  if(p.length() == 0) p = cfg::wifi_pass;

  if(s.length() == 0){
    w.send(200, "application/json", "{\"ok\":false,\"error\":\"SSID empty\"}");
    return;
  }

  // If already connected to this SSID, return current IP directly
  if(WiFi.status() == WL_CONNECTED && WiFi.SSID() == s){
    String resp = "{\"ok\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    w.send(200, "application/json", resp);
    return;
  }

  // Attempt connection with timeout
  WiFi.begin(s.c_str(), p.c_str());
  uint32_t t0 = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - t0 < 8000){
    delay(200);
  }

  if(WiFi.status() == WL_CONNECTED){
    String resp = "{\"ok\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    w.send(200, "application/json", resp);
  } else {
    // Restore configured network
    if(cfg::wifi_ssid.length() > 0){
      WiFi.begin(cfg::wifi_ssid.c_str(), cfg::wifi_pass.c_str());
    }
    w.send(200, "application/json", "{\"ok\":false,\"error\":\"Connection timed out\"}");
  }
}

// Test ping for StoryDeck Core URL
static void handle_ping_core(WebServer& w){
  if(!auth_ok(w)) return;
  String u = w.arg("url");
  if(u.length() == 0) u = cfg::core_url;

  if(u.length() == 0){
    w.send(200, "application/json", "{\"ok\":false,\"error\":\"Core URL empty\"}");
    return;
  }

  bool ok = core::ping(u);
  if(ok){
    w.send(200, "application/json", "{\"ok\":true}");
  } else {
    w.send(200, "application/json", "{\"ok\":false,\"error\":\"HTTP status != 200 or connection failed\"}");
  }
}

// Live Discovery of Profiles & Players from StoryDeck Core
static void handle_portal_discovery(WebServer& w){
  if(!auth_ok(w)) return;
  String u = w.arg("url");
  if(u.length() == 0) u = cfg::core_url;
  if(u.length() == 0){
    w.send(200, "application/json", "{\"ok\":false,\"error\":\"Core URL empty\"}");
    return;
  }

  bool ok = core::fetch_config_and_profiles(u, false);
  if(!ok && core::s_profile_count == 0){
    w.send(200, "application/json", "{\"ok\":false,\"error\":\"Could not load profiles from Core\"}");
    return;
  }

  String out = "{\"ok\":true,\"profiles\":[";
  for(int i = 0; i < core::s_profile_count; i++){
    if(i > 0) out += ",";
    out += "{\"id\":\"" + esc(core::s_profiles[i].id) + "\",";
    out += "\"name\":\"" + esc(core::s_profiles[i].display_name) + "\",";
    out += "\"default_player_id\":\"" + esc(core::s_profiles[i].default_player_id) + "\"}";
  }
  out += "],\"players\":[";
  int pcount = 0;
  String seen[32]; int seen_cnt = 0;
  auto append_json_player = [&](const String& pid, const String& pname, bool online){
    if(pid.length() == 0 || seen_cnt >= 32) return;
    for(int s = 0; s < seen_cnt; s++){ if(seen[s] == pid) return; }
    seen[seen_cnt++] = pid;
    if(pcount > 0) out += ",";
    out += "{\"id\":\"" + esc(pid) + "\",";
    out += "\"name\":\"" + esc(pname) + "\",";
    out += "\"online\":" + String(online ? "true" : "false") + "}";
    pcount++;
  };

  for(int i = 0; i < core::s_profile_count; i++){
    for(int p = 0; p < core::s_profiles[i].player_count; p++){
      append_json_player(core::s_profiles[i].players[p].id, core::s_profiles[i].players[p].name, core::s_profiles[i].players[p].online);
    }
  }
  for(int p = 0; p < core::s_standalone_player_count; p++){
    append_json_player(core::s_standalone_players[p].id, core::s_standalone_players[p].name, core::s_standalone_players[p].online);
  }
  out += "]}";

  w.send(200, "application/json", out);
}

static void handle_curation_post(WebServer& w){
  if(!auth_ok(w)) return;
  if(!s_sd_ok) sd_probe_and_mount();
  curation::load();
  curation::set_hide_physical(w.hasArg("hide_phys"));

  std::vector<Card, PsramVectorAlloc<Card>>& card_list = !s_library_cards.empty() ? s_library_cards : s_cards;
  for(size_t i = 0; i < card_list.size(); i++){
    if(card_list[i].daily) continue;
    String id = card_list[i].id.c_str();
    bool pin = w.hasArg("pin_" + id);
    bool phys = w.hasArg("phys_" + id);
    bool hide = w.hasArg("hide_" + id);
    curation::toggle_card_pinned(id, pin);
    curation::toggle_card_physical(id, phys);
    curation::toggle_card_hidden(id, hide);
  }
  curation::save();

  if(cfg::op_mode != "core"){
    reload_catalog_and_thumbs(false);
  }

  String storage_notice = s_sd_ok ? "Saved to MicroSD (/config/curation.json)." : "Saved to Internal Flash NVS (no MicroSD card inserted).";

  w.send(200, "text/html; charset=utf-8",
    "<!doctype html><body style='font-family:system-ui;background:#fbf7f0;padding:24px;text-align:center'>"
    "<h2 style='color:#3a6b50'>&#10004; Curation Saved</h2>"
    "<p>" + storage_notice + " Active catalog and touchscreen display updated.</p>"
    "<p><a href='/settings?tab=curation' style='color:#e5352b;font-weight:700;'>Back to Card Curation</a> &middot; <a href='/settings'>Settings</a></p></body>");
}

} // namespace portal
