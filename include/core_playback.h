#pragma once
// StoryDeck Core Player Resolution, Settings Sync, Playback & Catalog Fetchers

namespace core {

// Kiosk parity: resolve best active player for a profile (or standalone)
inline String resolve_active_device_id(int prof_idx){
  if(cfg::lock_player && cfg::locked_dev_id.length() > 0){
    return cfg::locked_dev_id;
  }
  if(cfg::op_mode != "core"){
    if(s_standalone_player_count > 0){
      for(int i = 0; i < s_standalone_player_count; i++){
        if(s_standalone_players[i].id == cfg::device_id) return cfg::device_id;
      }
      for(int i = 0; i < s_standalone_player_count; i++){
        if(s_standalone_players[i].online) return s_standalone_players[i].id;
      }
      return s_standalone_players[0].id;
    }
    return cfg::device_id;
  }
  if(prof_idx < 0 || prof_idx >= s_profile_count){
    if(cfg::device_id.length() > 0 && cfg::device_id != "unknown") return cfg::device_id;
    return cfg::device_id;
  }
  const Profile& pr = s_profiles[prof_idx];
  for(int i = 0; i < pr.player_count; i++){
    if(pr.players[i].is_playing) return pr.players[i].id;
  }
  for(int i = 0; i < pr.player_count; i++){
    if(pr.players[i].online) return pr.players[i].id;
  }
  if(pr.default_player_id.length() > 0) return pr.default_player_id;
  if(pr.player_count > 0) return pr.players[0].id;
  for(int p = 0; p < s_profile_count; p++){
    if(s_profiles[p].default_player_id.length() > 0) return s_profiles[p].default_player_id;
    if(s_profiles[p].player_count > 0) return s_profiles[p].players[0].id;
  }
  return cfg::device_id;
}

// Lookup PlayerInfo by device_id across active profile / all profiles / standalone
inline const PlayerInfo* find_player_info(const String& dev_id){
  if(dev_id.length() == 0) return nullptr;
  if(cfg::op_mode == "core"){
    if(s_active_profile_idx >= 0 && s_active_profile_idx < s_profile_count){
      const Profile& pr = s_profiles[s_active_profile_idx];
      for(int i = 0; i < pr.player_count; i++){
        if(pr.players[i].id == dev_id) return &pr.players[i];
      }
    }
    for(int p = 0; p < s_profile_count; p++){
      for(int i = 0; i < s_profiles[p].player_count; i++){
        if(s_profiles[p].players[i].id == dev_id) return &s_profiles[p].players[i];
      }
    }
  } else {
    for(int i = 0; i < s_standalone_player_count; i++){
      if(s_standalone_players[i].id == dev_id) return &s_standalone_players[i];
    }
  }
  return nullptr;
}

// 3b. Push Grown-Ups Settings & PIN back to StoryDeck Core (/api/device/settings)
inline bool sync_device_settings(const String& base_url){
  if(base_url.length() == 0 || cfg::op_mode != "core") return false;
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + "/api/device/settings")) return false;
  http.addHeader("Content-Type", "application/json");
  if(s_access_token.length() > 0){
    http.addHeader("Authorization", String("Bearer ") + s_access_token);
  }
  http.setTimeout(5000);

  JsonDocument doc;
  doc["device_id"]        = get_mac();
  doc["friendly_name"]    = cfg::kiosk_name;
  doc["parent_pin"]       = cfg::parent_pin;
  doc["lock_player"]      = cfg::lock_player;
  doc["locked_player_id"] = cfg::locked_dev_id;
  doc["assigned_profile"] = cfg::assigned_profile;

  JsonObject st = doc["settings"].to<JsonObject>();
  st["sleepTimerMin"]       = cfg::sleep_min;
  st["sleepTimerMode"]      = cfg::sleep_bedtime ? "bedtime" : "always";
  st["screensaverMin"]      = cfg::saver_min;
  st["clockSize"]           = cfg::saver_size;
  st["blockRemotePlayback"] = cfg::block_remote;
  char bbuf[8], wbuf[8];
  snprintf(bbuf, sizeof(bbuf), "%02d:%02d", cfg::night_h, cfg::night_m);
  snprintf(wbuf, sizeof(wbuf), "%02d:%02d", cfg::wake_h, cfg::wake_m);
  st["bedtime"]  = bbuf;
  st["wakeTime"] = wbuf;

  String payload;
  serializeJson(doc, payload);
  int code = http.PUT(payload);
  http.end();
  return (code == 200);
}

// 4. Playback Dispatch via Core Hub (with automatic 401/403 JWT re-auth + diagnostic logging)
inline int play_media(const String& base_url, const String& child_id, const String& card_type, const String& media_id, const String& device_id, int chapter_idx, const String& chapter_key = "", const String& track_key = ""){
  if(base_url.length() == 0) return -1;

  for(int attempt = 0; attempt < 2; attempt++){
    if(s_access_token.length() == 0 || attempt > 0){
      String rerr;
      register_device(base_url, rerr);
    }
    WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
    if(!http_begin_url(http, scli, pcli, base_url + "/api/play")) return -1;
    http.addHeader("Content-Type", "application/json");
    if(s_access_token.length() > 0){
      http.addHeader("Authorization", String("Bearer ") + s_access_token);
    }
    http.setTimeout(10000);

    String eff_dev = device_id;
    if(eff_dev.length() == 0) eff_dev = resolve_active_device_id(s_active_profile_idx);

    auto is_pure_digits = [](const String& s) -> bool {
      if(s.length() == 0) return true;
      for(size_t i = 0; i < s.length(); i++){
        if(s[i] < '0' || s[i] > '9') return false;
      }
      return true;
    };

    s_last_play_resp_ch = -1;
    s_last_play_resp_ch_title = "";
    s_last_play_resp_pos = 0;

    JsonDocument doc;
    doc["child"]     = child_id.length() ? child_id : "default";
    doc["type"]      = card_type.length() ? card_type : "yoto";
    doc["id"]        = media_id;
    doc["device_id"] = eff_dev;
    if(chapter_idx >= 0){
      doc["chapter"] = chapter_idx + 1;
      if(chapter_key.length() > 0 && !is_pure_digits(chapter_key)) doc["chapter_key"] = chapter_key;
      if(track_key.length() > 0   && !is_pure_digits(track_key))   doc["track_key"]   = track_key;
    }

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    String resp = (code > 0) ? http.getString() : "";
    http.end();
    log_msg("[PLAY] POST /api/play att=%d code=%d pl=%s resp=%s", attempt + 1, code, payload.c_str(), resp.c_str());

    if((code == 401 || code == 403) && attempt == 0){
      continue;
    }
    if(code >= 200 && code < 300 && resp.length() > 0){
      JsonDocument rdoc;
      if(!deserializeJson(rdoc, resp)){
        if(!rdoc["chapter"].isNull()){
          int rch = (int)rdoc["chapter"] - 1;
          if(rch >= 0) s_last_play_resp_ch = rch;
        }
        const char* rct = rdoc["chapter_title"];
        if(rct && *rct) s_last_play_resp_ch_title = rct;
        if(!rdoc["position"].isNull()) s_last_play_resp_pos = (int)rdoc["position"];
      }
    }
    return code;
  }
  return -1;
}

// 5. Transport Control (pause, resume, stop, next, prev, volume)
inline int player_control(const String& base_url, const String& device_id, const char* action, int volume = -1){
  if(base_url.length() == 0) return -1;

  for(int attempt = 0; attempt < 2; attempt++){
    if(s_access_token.length() == 0 || attempt > 0){
      String rerr;
      register_device(base_url, rerr);
    }
    WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
    if(!http_begin_url(http, scli, pcli, base_url + "/api/player/control")) return -1;
    http.addHeader("Content-Type", "application/json");
    if(s_access_token.length() > 0){
      http.addHeader("Authorization", String("Bearer ") + s_access_token);
    }
    http.setTimeout(2500);

    String eff_dev = device_id;
    if(eff_dev.length() == 0) eff_dev = resolve_active_device_id(s_active_profile_idx);

    JsonDocument doc;
    doc["device_id"] = eff_dev;
    doc["action"]    = action;
    if(volume >= 0) doc["volume"] = volume;

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    String resp = (code > 0) ? http.getString() : "";
    http.end();
    log_msg("[CTRL] POST /api/player/control att=%d code=%d pl=%s resp=%s", attempt + 1, code, payload.c_str(), resp.c_str());

    if((code == 401 || code == 403) && attempt == 0){
      continue;
    }
    return code;
  }
  return -1;
}

// URL-encode a query string value for safe GET ?name=... requests
inline String url_encode_query(const String& s){
  String out;
  out.reserve(s.length() + 12);
  const char hex[] = "0123456789ABCDEF";
  for(size_t i = 0; i < s.length(); i++){
    unsigned char c = (unsigned char)s[i];
    if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~'){
      out += (char)c;
    } else if(c == ' '){
      out += "%20";
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

typedef void (*CatalogCardCallback)(const String& id, const String& title, const String& author, const String& cover, const String& src, const String& series, float seq, const String& seq_disp, bool is_hl, JsonArray series_list, const String& lp, bool has_phys, const String& s_cover, const String& s_cover_id);

inline bool _fetch_catalog_endpoint(const String& base_url, const String& path, const String& default_src, CatalogCardCallback card_cb){
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + path)) return false;
  if(s_access_token.length() > 0){
    http.addHeader("Authorization", String("Bearer ") + s_access_token);
  }
  http.setTimeout(9000);
  int code = http.GET();
  if(code != 200){ http.end(); return false; }
  size_t blen = 0; char* body = read_body(http, blen);
  http.end();
  if(!body) return false;
  JsonDocument filter;
  JsonObject f0 = filter.add<JsonObject>();
  f0["id"] = true;
  f0["card_id"] = true;
  f0["title"] = true;
  f0["author"] = true;
  f0["cover"] = true;
  f0["thumb"] = true;
  f0["cover_url"] = true;
  f0["source"] = true;
  f0["type"] = true;
  f0["series"] = true;
  f0["sequence"] = true;
  f0["sequence_display"] = true;
  f0["is_highlight"] = true;
  f0["has_physical"] = true;
  f0["last_played_at"] = true;
  f0["lastPlayedAt"] = true;
  f0["series_list"][0]["name"] = true;
  f0["series_list"][0]["sequence"] = true;
  f0["series_list"][0]["sequence_display"] = true;
  f0["series_cover"] = true;
  f0["series_cover_id"] = true;
  PsramAlloc palloc; JsonDocument doc(&palloc);
  DeserializationError jerr = deserializeJson(doc, (const char*)body, blen, DeserializationOption::Filter(filter));
  free(body);
  if(jerr) return false;
  for(JsonObject item : doc.as<JsonArray>()){
    String id = (const char*)(item["id"] | item["card_id"] | "");
    if(id.length() == 0) continue;
    String title = (const char*)(item["title"] | "");
    String author = (const char*)(item["author"] | "");
    String series = (const char*)(item["series"] | "");
    JsonArray sl = item["series_list"].as<JsonArray>();
    if(series.length() == 0 && !sl.isNull() && sl.size() > 0){
      series = (const char*)(sl[0]["name"] | "");
    }
    auto parse_lead_float = [](const String& s) -> float {
      for(size_t i = 0; i < s.length(); i++){
        if(s[i] >= '0' && s[i] <= '9'){
          return s.substring(i).toFloat();
        }
      }
      return -1.0f;
    };
    float seq = item["sequence"].isNull() ? -1.0f : (float)item["sequence"];
    String seq_disp = (const char*)(item["sequence_display"] | "");
    if(seq < 0.0f && seq_disp.length() > 0) seq = parse_lead_float(seq_disp);
    if(seq < 0.0f && !sl.isNull() && sl.size() > 0){
      if(!sl[0]["sequence"].isNull()) seq = (float)sl[0]["sequence"];
      if(seq_disp.length() == 0) seq_disp = (const char*)(sl[0]["sequence_display"] | "");
      if(seq < 0.0f && seq_disp.length() > 0) seq = parse_lead_float(seq_disp);
    }
    bool is_hl = (bool)(item["is_highlight"] | false);
    String item_src = (const char*)(item["source"] | item["type"] | default_src.c_str());
    if(item_src.startsWith("yoto")) item_src = "yoto";
    else if(item_src == "abs") item_src = "abs";
    else item_src = default_src;
    String cover = (const char*)(item["cover"] | item["thumb"] | item["cover_url"] | "");
    if(cover.length() == 0){
      cover = base_url + "/api/thumb?type=" + item_src + "&id=" + id;
    } else if(cover.startsWith("/")){
      cover = base_url + cover;
    }
    String lp = (const char*)(item["last_played_at"] | item["lastPlayedAt"] | "");
    bool has_phys = (bool)(item["has_physical"] | false);
    String s_cover = (const char*)(item["series_cover"] | "");
    String s_cover_id = (const char*)(item["series_cover_id"] | "");
    card_cb(id, title, author, cover, item_src, series, seq, seq_disp, is_hl, sl, lp, has_phys, s_cover, s_cover_id);
  }
  return true;
}

// 6. Fetch Child Catalog (ABS + Yoto merged via Core hub)
inline bool fetch_catalog(const String& base_url, const String& child_id, CatalogCardCallback card_cb){
  if(base_url.length() == 0 || child_id.length() == 0) return false;
  bool ok_a = _fetch_catalog_endpoint(base_url, "/api/kids/" + child_id + "/abs", "abs", card_cb);
  bool ok_y = _fetch_catalog_endpoint(base_url, "/api/kids/" + child_id + "/yoto", "yoto", card_cb);
  return (ok_a || ok_y);
}

// 6b. Fetch Full Series Index for a Profile (all accessible series regardless of 60-card rotation cap)
inline bool fetch_series_index(const String& base_url, const String& child_id,
  void (*series_cb)(const String& name, int count, const String& cover, const String& cover_id, const String& src)){
  if(base_url.length() == 0 || child_id.length() == 0) return false;
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + "/api/kids/" + child_id + "/series")) return false;
  if(s_access_token.length() > 0){
    http.addHeader("Authorization", String("Bearer ") + s_access_token);
  }
  http.setTimeout(9000);
  int code = http.GET();
  if(code != 200){ http.end(); return false; }
  size_t blen = 0; char* body = read_body(http, blen);
  http.end();
  if(!body) return false;
  PsramAlloc palloc; JsonDocument doc(&palloc);
  DeserializationError jerr = deserializeJson(doc, (const char*)body, blen);
  free(body);
  if(jerr || !doc.is<JsonArray>()) return false;
  for(JsonObject s : doc.as<JsonArray>()){
    String name = (const char*)(s["name"] | "");
    if(name.length() == 0) continue;
    int count = (int)(s["count"] | 0);
    String cover_id = (const char*)(s["cover_id"] | "");
    String src = (const char*)(s["source"] | "yoto");
    String cover = (const char*)(s["cover"] | "");
    if(cover.startsWith("/")) cover = base_url + cover;
    series_cb(name, count, cover, cover_id, src);
  }
  return true;
}

// 6c. Lazy-Fetch all books in a single Series for Level 2 view
inline bool fetch_series_items(const String& base_url, const String& child_id, const String& series_name, CatalogCardCallback card_cb){
  if(base_url.length() == 0 || child_id.length() == 0 || series_name.length() == 0) return false;
  String path = "/api/kids/" + child_id + "/series/items?name=" + url_encode_query(series_name);
  return _fetch_catalog_endpoint(base_url, path, "yoto", card_cb);
}

// 7. Fetch Media Tracks / Chapters via Core Hub
inline bool fetch_media_tracks(const String& base_url, const String& media_id, String& title_out, void (*track_cb)(const String& key, const String& trk_key, const String& title, int dur)){
  if(base_url.length() == 0 || media_id.length() == 0) return false;
  WiFiClientSecure scli; WiFiClient pcli; HTTPClient http;
  if(!http_begin_url(http, scli, pcli, base_url + "/api/media/" + media_id + "/tracks")) return false;
  if(s_access_token.length() > 0){
    http.addHeader("Authorization", String("Bearer ") + s_access_token);
  }
  http.setTimeout(9000);
  int code = http.GET();
  if(code != 200){ http.end(); return false; }
  size_t blen = 0; char* body = read_body(http, blen);
  http.end();
  if(!body) return false;
  PsramAlloc palloc; JsonDocument doc(&palloc);
  DeserializationError jerr = deserializeJson(doc, (const char*)body, blen);
  free(body);
  if(jerr) return false;
  const char* t = doc["title"];
  if(t && *t) title_out = t;
  JsonArray tracks = doc["tracks"].as<JsonArray>();
  int idx = 1;
  for(JsonObject tr : tracks){
    String key = (const char*)(tr["key"] | "");
    if(key.length() == 0){
      char kbuf[8];
      snprintf(kbuf, sizeof(kbuf), "%02d", idx);
      key = kbuf;
    }
    String trk_key = (const char*)(tr["track_key"] | key.c_str());
    String tr_title = (const char*)(tr["title"] | "");
    if(tr_title.length() == 0) tr_title = String("Chapter ") + idx;
    int dur = (int)(tr["duration_seconds"] | tr["duration"] | 0);
    track_cb(key, trk_key, tr_title, dur);
    idx++;
  }
  return true;
}

} // namespace core
