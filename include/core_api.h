#pragma once
// StoryDeck Core Client for ESP32
// Interacts with StoryDeck Core hub (Docker container, default port 5055/5060)
// providing device registration, child profiles, and merged catalog.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "app_config.h"
#include "yoto_api.h"
#include "app_log.h"

inline int sd_prune_stale_avatars(const String* valid_keys, int key_count);

namespace core {

struct PlayerInfo {
  String id;
  String name;
  bool   online        = false;
  bool   is_home       = true;
  bool   is_playing    = false;
  bool   has_card      = false;
  String item_id       = "";
  String now_playing   = "";
  String author        = "";
  String chapter_title = "";
  int    position      = 0;
  int    duration      = 0;
  int    volume        = 50;
  int    volume_max    = 100;
  String cover_url     = "";
};

#define MAX_PROF_PLAYERS 8

struct Profile {
  String id;
  String display_name;
  String avatar_url;
  bool   has_custom_avatar = false;
  String avatar_cache_key;
  uint32_t avatar_color = 0xE5352B;
  String avatar_initial;
  String default_player_id;
  String default_player_name;
  PlayerInfo players[MAX_PROF_PLAYERS];
  int    player_count = 0;
  uint8_t* avatar_buf = nullptr;
  lv_img_dsc_t avatar_dsc{};
};

// Populated from POST /api/play response when resuming a book
static int    s_last_play_resp_ch = -1;
static String s_last_play_resp_ch_title = "";
static int    s_last_play_resp_pos = 0;

#define MAX_PROFILES 8
static Profile s_profiles[MAX_PROFILES];
static int     s_profile_count = 0;
static int     s_active_profile_idx = 0;
static String  s_assigned_profile = "";
static bool    s_allow_profile_switch = true;
static bool    s_exclude_single_series = true;
static uint64_t s_catalog_revision = 0;
static bool     s_catalog_dirty = false;

// Standalone mode discovered players (from /device-v2/devices/mine)
static PlayerInfo s_standalone_players[MAX_PROF_PLAYERS];
static int        s_standalone_player_count = 0;

inline bool discover_standalone_players(){
  tlsarena::Guard tg;
  WiFiClientSecure cli; cli.setInsecure();
  HTTPClient http;
  int code = yoto::get(cli, http, "/device-v2/devices/mine");
  if(code == 200){
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if(!err){
      s_standalone_player_count = 0;
      for(JsonObject d : doc["devices"].as<JsonArray>()){
        if(s_standalone_player_count >= MAX_PROF_PLAYERS) break;
        String did = (const char*)(d["deviceId"] | d["id"] | "");
        if(did.length() == 0) continue;
        PlayerInfo& pi = s_standalone_players[s_standalone_player_count++];
        pi.id      = did;
        pi.name    = (const char*)(d["name"] | "Yoto Player");
        pi.online  = (bool)(d["online"] | false);
        pi.is_home = pi.online;
        pi.volume  = (int)(d["volume"] | 50);
        pi.volume_max = (int)(d["volume_max"] | 100);
      }
      return (s_standalone_player_count > 0);
    }
  } else if(code > 0){
    http.end();
  }
  return false;
}

static String s_access_token = "";
static bool   s_is_registered = false;
static bool   s_is_approved = false;

inline String get_mac(){
  return WiFi.macAddress();
}

// Helper to correctly begin HTTP or HTTPS requests without panicking or requiring CA certs
inline bool http_begin_url(HTTPClient& http, WiFiClientSecure& scli, WiFiClient& pcli, const String& url){
  if(url.startsWith("https://")){
    scli.setInsecure();
    return http.begin(scli, url);
  }
  return http.begin(pcli, url);
}

// 1. Health Probe
inline bool ping(const String& base_url){
  if(base_url.length() == 0) return false;
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + "/health")) return false;
  http.setTimeout(4000);
  int code = http.GET();
  http.end();
  return (code == 200);
}

// ---- PSRAM-backed response handling ----
// getString() + a default JsonDocument allocate from INTERNAL RAM. Core's config/catalog
// bodies are tens of KB; together with the LVGL draw buffer they exhaust internal heap and
// the WiFi PHY's tiny esp_timer_create() (modem-sleep wake) aborts with ESP_ERR_NO_MEM.
struct PsramAlloc : ArduinoJson::Allocator {
  void* allocate(size_t n) override           { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void  deallocate(void* p) override          { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override{ return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};

// Read an OPEN response fully into a NUL-terminated PSRAM buffer (caller free()s).
inline char* read_body(HTTPClient& http, size_t& outLen){
  outLen = 0;
  int len = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  size_t cap = (len > 0) ? (size_t)len + 1 : 16384;
  char* buf = (char*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
  if(!buf) return nullptr;
  size_t got = 0; uint32_t t0 = millis();
  while(len < 0 || got < (size_t)len){
    size_t avail = stream->available();
    if(avail){
      if(got + avail + 1 > cap){
        size_t ncap = cap * 2;
        char* n = (char*)heap_caps_realloc(buf, ncap, MALLOC_CAP_SPIRAM);
        if(!n) break;
        buf = n; cap = ncap;
      }
      int n = stream->readBytes(buf + got, min(avail, cap - 1 - got));
      if(n > 0){ got += n; t0 = millis(); }
    } else if(!http.connected()){ break; }
    else if(millis() - t0 > 15000){ break; }
    else delay(1);
  }
  buf[got] = '\0';
  outLen = got;
  return buf;
}

// 2. Kiosk Device Registration
inline bool register_device(const String& base_url, String& err_out){
  if(base_url.length() == 0){
    err_out = "Core URL not set";
    return false;
  }
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + "/api/devices/register")){
    err_out = "Failed to initialize connection to Core";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(1200);

  JsonDocument doc;
  doc["mac"]              = get_mac();
  doc["mac_address"]      = get_mac();
  doc["friendly_name"]    = cfg::kiosk_name.length() > 0 ? cfg::kiosk_name : "StoryDeck Touch (ESP32-S3)";
  doc["ip_address"]       = WiFi.localIP().toString();
  doc["firmware_version"] = STORYDECK_VERSION;
  if(cfg::assigned_profile.length() > 0){
    doc["assigned_profile"] = cfg::assigned_profile;
  }
  if(cfg::lock_player || cfg::locked_dev_id.length() > 0){
    String locked_id = cfg::locked_dev_id.length() > 0 ? cfg::locked_dev_id : cfg::device_id;
    doc["lock_player"] = true;
    if(locked_id.length() > 0) doc["locked_player_id"] = locked_id;
  }
  if(cfg::parent_pin.length() == 4){
    doc["parent_pin"] = cfg::parent_pin;
  }

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  String resp = (code > 0) ? http.getString() : "";
  http.end();

  if(code == 202){
    s_is_registered = true;
    s_is_approved = false;
    err_out = "Pending Admin Approval in StoryDeck Core";
    return false;
  }
  if(code == 200 || code == 201){
    JsonDocument rdoc;
    if(!deserializeJson(rdoc, resp)){
      const char* tok = rdoc["tokens"]["access_token"];
      if(tok && *tok){
        s_access_token = tok;
      }
    }
    s_is_registered = true;
    s_is_approved = true;
    return true;
  }

  err_out = String("Registration failed (HTTP ") + code + ")";
  return false;
}

// 3. Fetch Device Config & Child Profiles
inline bool fetch_config_and_profiles(const String& base_url, bool prune_avatars = false){
  if(base_url.length() == 0) return false;
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + "/api/device/config?device_id=" + get_mac())) return false;
  if(s_access_token.length() > 0){
    http.addHeader("Authorization", String("Bearer ") + s_access_token);
  }
  http.setTimeout(1200);
  int code = http.GET();
  if(code == 401 || code == 403){
    http.end();
    s_is_approved = false;
    s_access_token = "";
    return false;
  }
  if(code != 200){ http.end(); return false; }

  size_t blen = 0; char* body = read_body(http, blen);
  http.end();
  if(!body) return false;
  PsramAlloc palloc; JsonDocument doc(&palloc);
  DeserializationError jerr = deserializeJson(doc, (const char*)body, blen);
  free(body);
  if(jerr) return false;

  if(!doc["is_approved"].isNull()){
    s_is_approved = (bool)doc["is_approved"];
  }
  if(!s_is_approved){
    s_access_token = "";
    return false;
  }

  uint64_t rev = (uint64_t)(doc["catalog_revision"] | 0ULL);
  if(rev > 0){
    if(s_catalog_revision > 0 && rev != s_catalog_revision){
      s_catalog_dirty = true;
      log_msg("[CORE] Catalog revision changed (%llu -> %llu); scheduling live reload",
              (unsigned long long)s_catalog_revision, (unsigned long long)rev);
    }
    s_catalog_revision = rev;
  }

  bool cfg_changed = false;
  const char* pin = doc["parent_pin"];
  if(pin && strlen(pin) == 4 && cfg::parent_pin != pin){
    cfg::parent_pin = pin;
    cfg_changed = true;
  }
  const char* lpid = doc["locked_player_id"];
  if(lpid && *lpid && cfg::locked_dev_id != lpid){
    cfg::locked_dev_id = lpid;
    cfg::device_id = lpid;
    cfg::lock_player = true;
    cfg_changed = true;
  } else if(!doc["lock_player"].isNull()){
    bool lp = (bool)doc["lock_player"];
    if(cfg::lock_player != lp){ cfg::lock_player = lp; cfg_changed = true; }
  }
  if(!doc["settings"].isNull() && doc["settings"].is<JsonObject>()){
    JsonObject st = doc["settings"].as<JsonObject>();
    if(!st["sleepTimerMin"].isNull()){
      int sm = (int)st["sleepTimerMin"];
      if(cfg::sleep_min != sm){ cfg::sleep_min = sm; cfg_changed = true; }
    }
    if(!st["sleepTimerMode"].isNull()){
      bool sb = (String((const char*)st["sleepTimerMode"]) != "always");
      if(cfg::sleep_bedtime != sb){ cfg::sleep_bedtime = sb; cfg_changed = true; }
    }
    if(!st["screensaverMin"].isNull()){
      int smin = (int)st["screensaverMin"];
      if(cfg::saver_min != smin){ cfg::saver_min = smin; cfg_changed = true; }
    }
    if(!st["clockSize"].isNull()){
      int cs = (int)st["clockSize"];
      if(cfg::saver_size != cs){ cfg::saver_size = cs; cfg_changed = true; }
    }
    if(!st["blockRemotePlayback"].isNull()){
      bool br = (bool)st["blockRemotePlayback"];
      if(cfg::block_remote != br){ cfg::block_remote = br; cfg_changed = true; }
    }
    if(!st["bedtime"].isNull()){
      String bt = (const char*)st["bedtime"];
      int colon = bt.indexOf(':');
      if(colon > 0){
        int nh = bt.substring(0, colon).toInt();
        int nm = bt.substring(colon + 1).toInt();
        if(cfg::night_h != nh || cfg::night_m != nm){
          cfg::night_h = nh; cfg::night_m = nm; cfg_changed = true;
        }
      }
    }
    if(!st["wakeTime"].isNull()){
      String wt = (const char*)st["wakeTime"];
      int colon = wt.indexOf(':');
      if(colon > 0){
        int wh = wt.substring(0, colon).toInt();
        int wm = wt.substring(colon + 1).toInt();
        if(cfg::wake_h != wh || cfg::wake_m != wm){
          cfg::wake_h = wh; cfg::wake_m = wm; cfg_changed = true;
        }
      }
    }
  }
  s_assigned_profile = (const char*)(doc["assigned_profile"] | "");
  if(s_assigned_profile.length() > 0 && cfg::assigned_profile != s_assigned_profile){
    cfg::assigned_profile = s_assigned_profile;
    cfg_changed = true;
  } else if(s_assigned_profile.length() == 0 && cfg::assigned_profile.length() > 0){
    s_assigned_profile = cfg::assigned_profile;
  }
  if(cfg_changed) cfg::save();
  if(!doc["allow_profile_switch"].isNull()){
    s_allow_profile_switch = (bool)doc["allow_profile_switch"];
  } else {
    s_allow_profile_switch = (s_assigned_profile.length() == 0);
  }
  if(!doc["exclude_single_card_series"].isNull()){
    s_exclude_single_series = (bool)doc["exclude_single_card_series"];
  } else {
    s_exclude_single_series = true;
  }

  s_profile_count = 0;
  JsonArray profs = doc["profiles"].as<JsonArray>();
  for(JsonObject p : profs){
    if(s_profile_count >= MAX_PROFILES) break;
    Profile& pr = s_profiles[s_profile_count];
    pr.id = (const char*)(p["id"] | "");
    pr.display_name = (const char*)(p["display_name"] | "Child");
    String av = (const char*)(p["avatar_url"] | "");
    if(av.startsWith("/")){
      av = base_url + av;
    }
    if(pr.avatar_url != av && pr.avatar_buf){
      free(pr.avatar_buf);
      pr.avatar_buf = nullptr;
    }
    pr.avatar_url = av;
    pr.has_custom_avatar = !p["has_custom_avatar"].isNull()
                           ? (bool)p["has_custom_avatar"]
                           : ((av.indexOf("?t=") >= 0) || (av.length() > 0 && av.indexOf("/api/avatar/") < 0));
    int t_pos = av.indexOf("?t=");
    String ver_tag = (t_pos >= 0) ? av.substring(t_pos + 3) : String("1");
    int amp_pos = ver_tag.indexOf('&');
    if(amp_pos >= 0) ver_tag = ver_tag.substring(0, amp_pos);
    if(ver_tag.length() > 6) ver_tag = ver_tag.substring(ver_tag.length() - 6);
    pr.avatar_cache_key = String("av_") + pr.id + "_" + ver_tag;
    String col_str = (const char*)(p["avatar_color"] | "");
    if(col_str.startsWith("#") && col_str.length() == 7){
      pr.avatar_color = (uint32_t)strtoul(col_str.c_str() + 1, nullptr, 16);
    } else {
      static const uint32_t PALETTE[10] = {0x3B82F6, 0xEF4444, 0x10B981, 0xF59E0B, 0x8B5CF6, 0xEC4899, 0x06B6D4, 0xF97316, 0x14B8A6, 0x6366F1};
      uint32_t h = 0;
      String key = pr.display_name; key.toLowerCase();
      for(size_t ci = 0; ci < key.length(); ci++) h = (h * 31 + (uint8_t)key[ci]);
      pr.avatar_color = PALETTE[h % 10];
    }
    pr.avatar_initial = (const char*)(p["avatar_initial"] | "");
    if(pr.avatar_initial.length() == 0){
      pr.avatar_initial = pr.display_name.length() > 0 ? pr.display_name.substring(0, 1) : String("C");
      pr.avatar_initial.toUpperCase();
    }
    pr.default_player_id = (const char*)(p["default_player_id"] | "");
    pr.default_player_name = (const char*)(p["default_player_name"] | "Yoto Player");

    pr.player_count = 0;
    JsonArray pls = p["players"].as<JsonArray>();
    for(JsonObject pl : pls){
      if(pr.player_count >= MAX_PROF_PLAYERS) break;
      String pid = (const char*)(pl["id"] | "");
      if(pid.length() == 0) continue;
      PlayerInfo& pi = pr.players[pr.player_count++];
      pi.id         = pid;
      pi.name       = (const char*)(pl["name"] | "Yoto Player");
      pi.online        = (bool)(pl["online"] | false);
      pi.is_home       = pl["is_home"].isNull() ? pi.online : (bool)pl["is_home"];
      pi.is_playing    = (bool)(pl["is_playing"] | false);
      pi.has_card      = (bool)(pl["has_card"] | pi.is_playing);
      pi.item_id       = (const char*)(pl["item_id"] | "");
      pi.now_playing   = (const char*)(pl["now_playing"] | "");
      pi.author        = (const char*)(pl["author"] | "");
      pi.chapter_title = (const char*)(pl["chapter_title"] | "");
      pi.position      = (int)(pl["position"] | 0);
      pi.duration      = (int)(pl["duration"] | 0);
      pi.volume        = (int)(pl["volume"] | 50);
      pi.volume_max    = (int)(pl["volume_max"] | 100);
      String curl      = (const char*)(pl["cover_url"] | "");
      if(curl.startsWith("/")){
        curl = base_url + curl;
      }
      pi.cover_url     = curl;
    }
    if(pr.player_count == 0 && pr.default_player_id.length() > 0){
      PlayerInfo& pi = pr.players[pr.player_count++];
      pi.id   = pr.default_player_id;
      pi.name = pr.default_player_name;
    }
    if(pr.default_player_id.length() == 0 && pr.player_count > 0){
      pr.default_player_id   = pr.players[0].id;
      pr.default_player_name = pr.players[0].name;
    }
    s_profile_count++;
  }

  // Prune any superseded or deleted profile avatar .565 files from MicroSD (only on explicit boot/maintenance)
  if(prune_avatars && s_profile_count > 0){
    String valid_av_keys[MAX_PROFILES];
    for(int i = 0; i < s_profile_count; i++) valid_av_keys[i] = s_profiles[i].avatar_cache_key;
    sd_prune_stale_avatars(valid_av_keys, s_profile_count);
  }

  // If Core assigned a specific profile to this hardware controller, select it
  if(s_assigned_profile.length() > 0){
    for(int i = 0; i < s_profile_count; i++){
      if(s_profiles[i].id == s_assigned_profile){
        s_active_profile_idx = i;
        break;
      }
    }
  }
  return (s_profile_count > 0);
}

// 4. Push local device settings / profile & player lock state to StoryDeck Core
inline bool push_device_settings(const String& base_url){
  if(base_url.length() == 0) return false;
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + "/api/device/settings")) return false;
  http.addHeader("Content-Type", "application/json");
  if(s_access_token.length() > 0){
    http.addHeader("Authorization", String("Bearer ") + s_access_token);
  }
  http.setTimeout(1500);

  JsonDocument doc;
  doc["device_id"]        = get_mac();
  doc["friendly_name"]    = cfg::kiosk_name.length() > 0 ? cfg::kiosk_name : "StoryDeck Touch (ESP32-S3)";
  doc["assigned_profile"] = cfg::assigned_profile;
  doc["lock_player"]      = cfg::lock_player;
  doc["locked_player_id"] = cfg::locked_dev_id;
  if(cfg::parent_pin.length() == 4){
    doc["parent_pin"] = cfg::parent_pin;
  }

  String payload;
  serializeJson(doc, payload);
  int code = http.POST(payload);
  http.end();
  return (code >= 200 && code < 300);
}

} // namespace core

#include "core_playback.h"
