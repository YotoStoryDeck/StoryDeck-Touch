#pragma once
// Web settings portal HTML builders, auth helper, timezone & WiFi pickers.
#include <WebServer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "app_config.h"
#include "yoto_api.h"
#include "core_api.h"
#include "standalone_curation.h"

extern bool fetch_library();
extern void build_catalog_from_library();
extern void build_grid();
extern bool fetch_library_core();
extern void np_update_vol_ui();


namespace portal {

static bool auth_ok(WebServer& w){
  if(sizeof(OTA_PASSWORD) <= 1) return true;          // empty define => open (trusted LAN)
  if(w.authenticate("admin", OTA_PASSWORD)) return true;
  w.requestAuthentication();
  return false;
}

// Minimal HTML-attribute escape so an SSID with quotes can't break the form.
static String esc(const String& s){
  String o; o.reserve(s.length() + 8);
  for(char c : s){
    if(c=='&') o += "&amp;"; else if(c=='<') o += "&lt;";
    else if(c=='>') o += "&gt;"; else if(c=='"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String text_row(const char* label, const char* name, const String& val, const char* hint=nullptr, const char* list=nullptr){
  String h = "<label>" + String(label) + "<input name='" + name + "' id='" + name + "' value=\"" + esc(val) + "\"";
  if(list){ h += " list='"; h += list; h += "'"; }
  h += ">";
  if(hint){ h += "<small>"; h += hint; h += "</small>"; }
  h += "</label>";
  return h;
}
static String secret_row(const char* label, const char* name, const char* hint){
  return "<label>" + String(label) + "<input name='" + name + "' id='" + name +
         "' placeholder='(unchanged — leave blank to keep)'><small>" + hint + "</small></label>";
}

static String mode_select(){
  String h = "<label>Operational Mode<select name='mode' id='mode_sel' onchange='toggleModeFields()' required>";
  if(cfg::op_mode.length() == 0){
    h += "<option value='' disabled selected>-- Select Initial Operational Mode --</option>";
  }
  h += String("<option value='core'") + (cfg::op_mode == "core" ? " selected" : "") + ">StoryDeck Core Managed (Hub + Profiles)</option>";
  h += String("<option value='standalone'") + (cfg::op_mode == "standalone" ? " selected" : "") + ">Standalone Cloud (Direct Yoto API)</option>";
  h += "</select><small>Choose whether this screen connects to your local StoryDeck Core server or directly to Yoto Cloud.</small></label>";
  return h;
}

inline String parse_tz_input(const String& raw){
  String s = raw;
  s.trim();
  if(s.length() == 0) return "EST5EDT,M3.2.0,M11.1.0";

  String up = s;
  up.toUpperCase();
  if(up == "UTC" || up == "GMT" || up == "Z" || up == "UTC0" || up == "GMT0" || up == "0" || up == "+0" || up == "-0"){
    return "UTC0";
  }

  String rem = s;
  if(rem.startsWith("UTC") || rem.startsWith("utc")) rem = rem.substring(3);
  else if(rem.startsWith("GMT") || rem.startsWith("gmt")) rem = rem.substring(3);
  rem.trim();

  bool has_sign = (rem.startsWith("+") || rem.startsWith("-"));
  bool is_all_digits = true;
  for(size_t i = (has_sign ? 1 : 0); i < rem.length(); i++){
    char c = rem[i];
    if(c != ':' && (c < '0' || c > '9')){
      is_all_digits = false;
      break;
    }
  }

  if(rem.length() > 0 && is_all_digits){
    bool is_positive = !rem.startsWith("-");
    if(rem.startsWith("+") || rem.startsWith("-")) rem = rem.substring(1);
    int colon = rem.indexOf(':');
    int hours = 0;
    int mins = 0;
    if(colon > 0){
      hours = rem.substring(0, colon).toInt();
      mins = rem.substring(colon + 1).toInt();
    } else {
      hours = rem.toInt();
    }
    if(hours == 0 && mins == 0) return "UTC0";
    if(hours < 0) hours = -hours;
    if(mins < 0) mins = -mins;
    if(hours > 14) hours = 14;
    if(mins > 59) mins = 59;

    char label_buf[32];
    char offset_buf[32];
    char sign_char = is_positive ? '+' : '-';
    if(mins > 0){
      snprintf(label_buf, sizeof(label_buf), "<UTC%c%d:%02d>", sign_char, hours, mins);
      // In POSIX: east (+) is negative, west (-) is positive
      snprintf(offset_buf, sizeof(offset_buf), "%s%d:%02d", is_positive ? "-" : "", hours, mins);
    } else {
      snprintf(label_buf, sizeof(label_buf), "<UTC%c%d>", sign_char, hours);
      snprintf(offset_buf, sizeof(offset_buf), "%s%d", is_positive ? "-" : "", hours);
    }
    return String(label_buf) + offset_buf;
  }

  return s;
}

static String tz_select(){
  struct TzEntry { const char* grp; const char* label; const char* val; };
  static const TzEntry zones[] = {
    // North America
    {"North America", "US Eastern (New York, Miami, Atlanta)", "EST5EDT,M3.2.0,M11.1.0"},
    {"North America", "US Central (Chicago, Dallas)", "CST6CDT,M3.2.0,M11.1.0"},
    {"North America", "US Mountain (Denver, Salt Lake City)", "MST7MDT,M3.2.0,M11.1.0"},
    {"North America", "US Arizona (Phoenix - no DST)", "MST7"},
    {"North America", "US Pacific (Los Angeles, Seattle)", "PST8PDT,M3.2.0,M11.1.0"},
    {"North America", "US Alaska (Anchorage)", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"North America", "US Hawaii (Honolulu - no DST)", "HST10"},
    {"North America", "Canada Atlantic (Halifax)", "AST4ADT,M3.2.0,M11.1.0"},
    {"North America", "Canada Newfoundland (St. John's)", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"North America", "Mexico (Mexico City)", "CST6"},

    // Central & South America
    {"Central & South America", "Colombia, Peru, Ecuador (Bogota, Lima)", "COT5"},
    {"Central & South America", "Venezuela (Caracas)", "VET4"},
    {"Central & South America", "Brazil (Sao Paulo, Rio de Janeiro)", "BRT3"},
    {"Central & South America", "Argentina (Buenos Aires)", "ART3"},
    {"Central & South America", "Chile (Santiago)", "CLT4CLST,M9.1.0/24,M4.1.0/24"},

    // Europe & UK
    {"Europe & UK", "UK & Ireland (London, Dublin)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe & UK", "Western Europe (Lisbon)", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe & UK", "Central Europe (Berlin, Paris, Rome, Madrid, Amsterdam)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe & UK", "Eastern Europe (Athens, Helsinki, Kyiv, Bucharest)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe & UK", "Turkey (Istanbul)", "TRT-3"},

    // Africa & Middle East
    {"Africa & Middle East", "Egypt (Cairo)", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Africa & Middle East", "South Africa (Johannesburg, Cape Town)", "SAST-2"},
    {"Africa & Middle East", "East Africa (Nairobi, Addis Ababa)", "EAT-3"},
    {"Africa & Middle East", "West Africa (Lagos, Accra)", "WAT-1"},
    {"Africa & Middle East", "Gulf / UAE (Dubai, Abu Dhabi)", "GST-4"},
    {"Africa & Middle East", "Israel (Jerusalem)", "IST-2IDT,M3.4.4/26,M10.5.0"},

    // Asia
    {"Asia", "Pakistan (Karachi, Islamabad)", "PKT-5"},
    {"Asia", "India & Sri Lanka (New Delhi, Mumbai, Colombo)", "IST-5:30"},
    {"Asia", "Nepal (Kathmandu)", "NPT-5:45"},
    {"Asia", "Bangladesh (Dhaka)", "BST-6"},
    {"Asia", "Thailand, Vietnam, Indonesia (Bangkok, Hanoi, Jakarta)", "ICT-7"},
    {"Asia", "China, Hong Kong, Singapore, Taiwan (Beijing, Singapore)", "CST-8"},
    {"Asia", "Japan & Korea (Tokyo, Seoul)", "JST-9"},

    // Australia & Pacific
    {"Australia & Pacific", "Australia Western (Perth)", "AWST-8"},
    {"Australia & Pacific", "Australia Northern (Darwin)", "ACST-9:30"},
    {"Australia & Pacific", "Australia South (Adelaide)", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia & Pacific", "Australia Queensland (Brisbane)", "AEST-10"},
    {"Australia & Pacific", "Australia Eastern (Sydney, Melbourne, Hobart)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia & Pacific", "New Zealand (Auckland, Wellington)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Australia & Pacific", "Fiji", "FJT-12"},

    // Standard Fixed UTC Offsets
    {"Standard UTC Offsets", "UTC-12:00 (Baker Island)", "<UTC-12>12"},
    {"Standard UTC Offsets", "UTC-11:00 (Samoa, Niue)", "<UTC-11>11"},
    {"Standard UTC Offsets", "UTC-10:00 (Hawaii Std)", "<UTC-10>10"},
    {"Standard UTC Offsets", "UTC-09:00 (Alaska Std)", "<UTC-9>9"},
    {"Standard UTC Offsets", "UTC-08:00 (Pacific Std)", "<UTC-8>8"},
    {"Standard UTC Offsets", "UTC-07:00 (Mountain Std)", "<UTC-7>7"},
    {"Standard UTC Offsets", "UTC-06:00 (Central Std)", "<UTC-6>6"},
    {"Standard UTC Offsets", "UTC-05:00 (Eastern Std)", "<UTC-5>5"},
    {"Standard UTC Offsets", "UTC-04:00 (Atlantic Std)", "<UTC-4>4"},
    {"Standard UTC Offsets", "UTC-03:30 (Newfoundland Std)", "<UTC-3:30>3:30"},
    {"Standard UTC Offsets", "UTC-03:00 (Buenos Aires / Brasilia)", "<UTC-3>3"},
    {"Standard UTC Offsets", "UTC-02:00 (South Georgia)", "<UTC-2>2"},
    {"Standard UTC Offsets", "UTC-01:00 (Azores Std)", "<UTC-1>1"},
    {"Standard UTC Offsets", "UTC±00:00 (Greenwich / UTC)", "UTC0"},
    {"Standard UTC Offsets", "UTC+01:00 (Central European Std)", "<UTC+1>-1"},
    {"Standard UTC Offsets", "UTC+02:00 (Eastern European Std)", "<UTC+2>-2"},
    {"Standard UTC Offsets", "UTC+03:00 (Moscow / Arabia Std)", "<UTC+3>-3"},
    {"Standard UTC Offsets", "UTC+03:30 (Iran Std)", "<UTC+3:30>-3:30"},
    {"Standard UTC Offsets", "UTC+04:00 (Gulf Std)", "<UTC+4>-4"},
    {"Standard UTC Offsets", "UTC+04:30 (Afghanistan)", "<UTC+4:30>-4:30"},
    {"Standard UTC Offsets", "UTC+05:00 (Pakistan Std)", "<UTC+5>-5"},
    {"Standard UTC Offsets", "UTC+05:30 (India Standard Time)", "<UTC+5:30>-5:30"},
    {"Standard UTC Offsets", "UTC+05:45 (Nepal)", "<UTC+5:45>-5:45"},
    {"Standard UTC Offsets", "UTC+06:00 (Bangladesh Std)", "<UTC+6>-6"},
    {"Standard UTC Offsets", "UTC+06:30 (Myanmar)", "<UTC+6:30>-6:30"},
    {"Standard UTC Offsets", "UTC+07:00 (Indochina Time)", "<UTC+7>-7"},
    {"Standard UTC Offsets", "UTC+08:00 (China / Singapore / WA)", "<UTC+8>-8"},
    {"Standard UTC Offsets", "UTC+09:00 (Japan / Korea Std)", "<UTC+9>-9"},
    {"Standard UTC Offsets", "UTC+09:30 (Darwin Std)", "<UTC+9:30>-9:30"},
    {"Standard UTC Offsets", "UTC+10:00 (Australia Eastern Std)", "<UTC+10>-10"},
    {"Standard UTC Offsets", "UTC+11:00 (Solomon Islands)", "<UTC+11>-11"},
    {"Standard UTC Offsets", "UTC+12:00 (New Zealand Std)", "<UTC+12>-12"},
    {"Standard UTC Offsets", "UTC+13:00 (Tonga / Tokelau)", "<UTC+13>-13"},
    {"Standard UTC Offsets", "UTC+14:00 (Line Islands / Kiritimati)", "<UTC+14>-14"},
  };

  String h = "<label>Timezone<select name='tz' id='tz_sel'>";
  bool found = false;
  const char* cur_grp = nullptr;

  for(size_t i = 0; i < sizeof(zones)/sizeof(zones[0]); i++){
    auto& z = zones[i];
    if(!cur_grp || strcmp(cur_grp, z.grp) != 0){
      if(cur_grp) h += "</optgroup>";
      cur_grp = z.grp;
      h += String("<optgroup label=\"") + cur_grp + "\">";
    }
    bool sel = (cfg::tz == z.val);
    found |= sel;
    h += String("<option value=\"") + z.val + "\"" + (sel ? " selected" : "") + ">" + z.label + "</option>";
  }
  if(cur_grp) h += "</optgroup>";

  if(!found){
    h += "<optgroup label=\"Current Custom Setting\">";
    h += "<option value=\"" + esc(cfg::tz) + "\" selected>Custom (" + esc(cfg::tz) + ")</option>";
    h += "</optgroup>";
  }
  h += "</select></label>";

  h += "<label style='margin-top:8px'>Manual UTC Offset or Custom POSIX String"
       "<input type='text' id='tz_custom' name='tz_custom' value='' "
       "placeholder='e.g. UTC-5, UTC+2, +05:30, or custom POSIX' style='margin-top:4px'>"
       "<small>Entering a value here overrides the dropdown above. Supports friendly UTC offsets (e.g. UTC+1, -5, +5:30) or full POSIX strings.</small></label>";

  return h;
}

// Scan nearby 2.4 GHz WiFi networks and build a select picker + input combo
static String wifi_picker(){
  String h = "<label>Network name (SSID)<div style='display:flex;gap:8px;margin-top:4px'>";
  h += "<select id='wf_sel' onchange='var el=document.getElementById(\"wf_in\");if(this.value)el.value=this.value' style='width:50%;margin-top:0'>";
  h += "<option value=''>-- Select nearby WiFi --</option>";

  if(WiFi.getMode() & WIFI_MODE_AP){
    WiFi.disconnect(false);
    delay(50);
  }

  int n = WiFi.scanNetworks(false, false, false, 200);
  if(n > 0){
    for(int i = 0; i < n; i++){
      String ssid = WiFi.SSID(i);
      if(ssid.length() == 0) continue;
      int32_t rssi = WiFi.RSSI(i);
      h += "<option value=\"" + esc(ssid) + "\">" + esc(ssid) + " (" + String(rssi) + " dBm)</option>";
    }
    WiFi.scanDelete();
  }
  h += "</select>";
  h += "<input id='wf_in' name='ssid' value=\"" + esc(cfg::wifi_ssid) + "\" placeholder='or type SSID' style='width:50%;margin-top:0'>";
  h += "</div><small>Pick a nearby 2.4 GHz network from the dropdown or type manually.</small></label>";
  return h;
}

// Profile & Target Device Selection Section (Grown-ups & Admin parity)
static String profile_device_picker(){
  String h = "<div id='prof_device_sec'>";
  h += "<h2>Profile &amp; Device Setup</h2>";

  // Child Profile (Core Managed Mode only — hidden in Standalone)
  h += "<div id='prof_group'>";
  h += "<label>Assigned Child Profile (Core Managed Mode)";
  h += "<select id='prof_sel' name='prof' onchange='onProfileSelectChange()'>";
  h += "<option value=''>-- Select Profile or type below --</option>";
  for(int i = 0; i < core::s_profile_count; i++){
    bool sel = (cfg::assigned_profile == core::s_profiles[i].id);
    h += "<option value=\"" + esc(core::s_profiles[i].id) + "\" data-player=\"" + esc(core::s_profiles[i].default_player_id) + "\"" + (sel ? " selected" : "") + ">" + esc(core::s_profiles[i].display_name) + " (" + esc(core::s_profiles[i].id) + ")</option>";
  }
  h += "</select>";
  h += "<input id='prof_in' name='prof_custom' value=\"" + esc(cfg::assigned_profile) + "\" placeholder='or type Profile ID (e.g. child-id)' style='margin-top:6px;'>";
  h += "<small>Select from discovered Core profiles or type profile ID. The controller boots directly into this child's library.</small></label>";
  h += "</div>";

  // Target Player (Audio Output) — Required for all controllers
  h += "<div id='player_group'>";
  h += "<label style='margin-top:14px;'>Target Yoto Player (Required Audio Destination)";
  h += "<select id='dev_sel' name='devid' onchange='onPlayerSelectChange()'>";
  
  bool is_standalone = (cfg::op_mode == "standalone");
  bool yoto_authed = yoto::is_authenticated();

  if(is_standalone && !yoto_authed){
    h += "<option value=''>-- Sign in to Yoto above to discover players --</option>";
  } else {
    h += "<option value=''>-- Select Target Player --</option>";
  }
  
  // Aggregate unique players across Core profiles & standalone list
  String seen_ids[32]; int seen_cnt = 0;
  auto add_player_opt = [&](const String& pid, const String& pname, bool online){
    if(pid.length() == 0 || seen_cnt >= 32) return;
    for(int s = 0; s < seen_cnt; s++){ if(seen_ids[s] == pid) return; }
    seen_ids[seen_cnt++] = pid;
    bool sel = (cfg::device_id == pid || cfg::locked_dev_id == pid);
    h += "<option value=\"" + esc(pid) + "\"" + (sel ? " selected" : "") + ">" + esc(pname) + " (" + esc(pid) + ") &bull; " + (online ? "Online" : "Offline") + "</option>";
  };

  for(int i = 0; i < core::s_profile_count; i++){
    for(int p = 0; p < core::s_profiles[i].player_count; p++){
      add_player_opt(core::s_profiles[i].players[p].id, core::s_profiles[i].players[p].name, core::s_profiles[i].players[p].online);
    }
  }
  for(int p = 0; p < core::s_standalone_player_count; p++){
    add_player_opt(core::s_standalone_players[p].id, core::s_standalone_players[p].name, core::s_standalone_players[p].online);
  }

  h += "</select>";
  h += "<input id='dev_in' name='devid_custom' value=\"" + esc(cfg::device_id) + "\" placeholder='or enter Yoto Player Device ID' style='margin-top:6px;'>";
  h += "<small style='display:block;margin-top:6px;color:#7a7060;'>🔒 All StoryDeck Touch controllers are dedicated single-user appliances and are permanently locked to their target player.</small>";
  h += "<div id='dev_standalone_notice' style='display:none;color:#e5352b;font-weight:600;font-size:0.9em;margin-top:6px;'>⚠️ Authenticate with Yoto above to discover and select your players.</div>";
  h += "</label>";
  h += "</div>";

  // Discovery Action Buttons
  h += "<div style='margin-top:12px;'>";
  h += "<button type='button' id='disc_btn_core' class='test-btn' onclick='fetchDiscovery()'>&#128260; Fetch Profiles &amp; Players from Core</button>";
  h += "<button type='button' id='disc_btn_yoto' class='test-btn' style='display:none;' onclick='fetchYotoPlayers()'>&#128260; Refresh Yoto Players</button>";
  h += "<span id='disc_badge'></span></div>";
  h += "</div>";
  return h;
}

static String portal_toolbar(const String& active_tab = "settings"){
  String tb = "<div class='nav-toolbar'>";
  auto btn = [&](const char* label, const char* url, bool active, const char* id = "", const char* extra = ""){
    String s = "<a href='";
    s += url;
    s += "' class='nav-btn";
    if(active) s += " active";
    s += "'";
    if(id && id[0]){ s += " id='"; s += id; s += "'"; }
    if(extra && extra[0]){ s += " "; s += extra; }
    s += ">";
    s += label;
    s += "</a>";
    return s;
  };
  tb += btn("⚙️ Settings", "/settings", active_tab == "settings");
  tb += btn("🎴 Card Curation", "/settings?tab=curation", active_tab == "curation", "nav_curation");
  tb += btn("📜 Console Logs", "/logs", false, "", "target='_blank'");
  tb += btn("📺 Live Display", "/screen", false, "", "target='_blank'");
  tb += btn("⚡ Firmware Update", "/update", false);
  tb += btn("🔄 Reboot", "/reboot", false, "", "onclick=\"return confirm('Reboot StoryDeck Touch now?');\"");
  tb += "</div>";
  return tb;
}

} // namespace portal
