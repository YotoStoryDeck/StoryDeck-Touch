#pragma once
// HTTPS Pull-OTA Update Engine, Version Checker & PIN-Protected /ota-web Portal
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include "app_config.h"
#include "core_api.h"
#include "app_log.h"

static const char* OTA_RELEASES_INDEX_URL = "https://yotostorydeck.github.io/StoryDeck-Touch/releases.json";
static const char* OTA_PAGES_BASE_URL     = "https://yotostorydeck.github.io/StoryDeck-Touch/";
static const char* OTA_DEFAULT_DEVICE_ID  = "waveshare-s3-43b";

inline void ota_splash_and_blank();
inline void ota_restore_display();

struct OtaReleaseInfo {
  bool     checked = false;
  bool     update_available = false;
  String   current_version = STORYDECK_VERSION;
  String   latest_stable = "";
  String   latest_stable_url = "";
  String   latest_stable_sha256 = "";
  String   latest_prerelease = "";
  String   latest_prerelease_url = "";
  String   latest_prerelease_sha256 = "";
  uint32_t last_checked_ms = 0;
  String   last_error = "";
};

static OtaReleaseInfo s_ota_info;

static String ota_html_esc(const String& s){
  String o; o.reserve(s.length() + 16);
  for(size_t i = 0; i < s.length(); i++){
    char c = s[i];
    if(c == '&') o += "&amp;";
    else if(c == '<') o += "&lt;";
    else if(c == '>') o += "&gt;";
    else if(c == '"') o += "&quot;";
    else if(c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

static String ota_json_esc(const String& s){
  String o; o.reserve(s.length() + 8);
  for(size_t i = 0; i < s.length(); i++){
    char c = s[i];
    if(c == '"') o += "\\\"";
    else if(c == '\\') o += "\\\\";
    else if(c == '\n') o += "\\n";
    else if(c == '\r') continue;
    else o += c;
  }
  return o;
}

static String ota_norm_ver_display(const String& v){
  String s = v;
  s.trim();
  if(s.length() == 0) return "unknown";
  if(s[0] == 'v' || s[0] == 'V') return s;
  return String("v") + s;
}

static bool ota_is_allowed_url(const String& url){
  if(url.length() < 16 || url.length() > 512) return false;
  if(url.indexOf("..") >= 0 || url.indexOf(' ') >= 0 || url.indexOf('"') >= 0 || url.indexOf('\'') >= 0) return false;
  return url.startsWith("https://yotostorydeck.github.io/StoryDeck-Touch/") ||
         url.startsWith("https://github.com/YotoStoryDeck/StoryDeck-Touch/releases/");
}

static bool ota_pin_valid(const String& pin){
  String p = pin; p.trim();
  String expected = cfg::parent_pin.length() ? cfg::parent_pin : String("1234");
  expected.trim();
  return p.length() > 0 && p == expected;
}

struct OtaSemver {
  int major = 0, minor = 0, patch = 0;
  bool is_stable = false;
  long rc_date = 0;
  int rc_iter = 0;
  bool valid = false;
};

static OtaSemver ota_parse_semver(const String& raw){
  OtaSemver out;
  String s = raw; s.trim();
  if(s.startsWith("v") || s.startsWith("V")) s = s.substring(1);
  if(s.length() == 0) return out;
  int dash = s.indexOf('-');
  String core = (dash >= 0) ? s.substring(0, dash) : s;
  String pre  = (dash >= 0) ? s.substring(dash + 1) : "";
  int d1 = core.indexOf('.');
  int d2 = (d1 >= 0) ? core.indexOf('.', d1 + 1) : -1;
  if(d1 < 0 || d2 < 0) return out;
  out.major = core.substring(0, d1).toInt();
  out.minor = core.substring(d1 + 1, d2).toInt();
  out.patch = core.substring(d2 + 1).toInt();
  out.valid = true;
  if(pre.length() == 0){
    out.is_stable = true;
  } else {
    out.is_stable = false;
    String digits = "";
    int dot = pre.indexOf('.');
    String datePart = pre;
    if(dot >= 0){
      datePart = pre.substring(0, dot);
      int dot2 = pre.indexOf('.', dot + 1);
      if(dot2 >= 0){
        datePart = pre.substring(dot + 1, dot2);
        out.rc_iter = pre.substring(dot2 + 1).toInt();
      } else {
        datePart = pre.substring(dot + 1);
        out.rc_iter = 1;
      }
    }
    for(size_t i = 0; i < datePart.length(); i++){
      if(isdigit(datePart[i])) digits += datePart[i];
    }
    if(digits.length() > 0) out.rc_date = digits.toInt();
  }
  return out;
}

static bool ota_is_newer(const String& candidate, const String& current){
  OtaSemver a = ota_parse_semver(candidate);
  OtaSemver b = ota_parse_semver(current);
  if(!a.valid || !b.valid) return false;
  if(a.major != b.major) return a.major > b.major;
  if(a.minor != b.minor) return a.minor > b.minor;
  if(a.patch != b.patch) return a.patch > b.patch;
  if(a.is_stable != b.is_stable) return a.is_stable && !b.is_stable;
  if(!a.is_stable && !b.is_stable){
    if(a.rc_date != b.rc_date) return a.rc_date > b.rc_date;
    if(a.rc_iter != b.rc_iter) return a.rc_iter > b.rc_iter;
  }
  return false;
}

static String ota_resolve_fw_url(const String& dev_id, const String& ver, const String& rel_path){
  if(rel_path.startsWith("https://")) return rel_path;
  String path = rel_path;
  if(path.length() == 0) path = String("bin/") + dev_id + "/" + ver + "/firmware.bin";
  while(path.startsWith("/")) path = path.substring(1);
  return String(OTA_PAGES_BASE_URL) + path;
}

static bool ota_check_for_updates(){
  s_ota_info.current_version = STORYDECK_VERSION;
  if(WiFi.status() != WL_CONNECTED){
    s_ota_info.last_error = "WiFi not connected";
    return false;
  }
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 16000){
    s_ota_info.last_error = "Low internal RAM, try again shortly";
    return false;
  }

  char* body = nullptr;
  size_t blen = 0;
  int code = -1;
  {
    tlsarena::Guard tg;
    WiFiClientSecure cli;
    cli.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(8000);
    String url = String(OTA_RELEASES_INDEX_URL) + "?t=" + String(millis());
    if(!http.begin(cli, url)){
      s_ota_info.last_error = "HTTP init failed";
      return false;
    }
    code = http.GET();
    if(code == 200){
      body = core::read_body(http, blen);
    }
    http.end();
  }

  if(code != 200 || !body){
    if(body) free(body);
    s_ota_info.checked = true;
    s_ota_info.last_checked_ms = millis();
    s_ota_info.last_error = String("Release index returned HTTP ") + code;
    log_msg("[OTA] Update check failed: %s", s_ota_info.last_error.c_str());
    return false;
  }

  core::PsramAlloc palloc;
  JsonDocument doc(&palloc);
  DeserializationError jerr = deserializeJson(doc, (const char*)body, blen);
  free(body);
  if(jerr){
    s_ota_info.checked = true;
    s_ota_info.last_checked_ms = millis();
    s_ota_info.last_error = String("Invalid releases.json: ") + jerr.c_str();
    log_msg("[OTA] %s", s_ota_info.last_error.c_str());
    return false;
  }

  String dev_id = (const char*)(doc["default_device"] | OTA_DEFAULT_DEVICE_ID);
  if(!doc["devices"][OTA_DEFAULT_DEVICE_ID].isNull()) dev_id = OTA_DEFAULT_DEVICE_ID;
  JsonObject dev = doc["devices"][dev_id];
  if(dev.isNull()){
    s_ota_info.checked = true;
    s_ota_info.last_checked_ms = millis();
    s_ota_info.last_error = "Device entry missing from releases.json";
    return false;
  }

  s_ota_info.latest_stable = (const char*)(dev["latest_stable"] | "");
  s_ota_info.latest_prerelease = (const char*)(dev["latest_prerelease"] | "");
  s_ota_info.latest_stable_url = "";
  s_ota_info.latest_stable_sha256 = "";
  s_ota_info.latest_prerelease_url = "";
  s_ota_info.latest_prerelease_sha256 = "";

  JsonArray rels = dev["releases"].as<JsonArray>();
  for(JsonObject r : rels){
    String ver = (const char*)(r["version"] | "");
    String fw  = (const char*)(r["firmware"] | "");
    String sha = (const char*)(r["firmware_sha256"] | "");
    if(ver.length() > 0 && ver == s_ota_info.latest_stable){
      s_ota_info.latest_stable_url = ota_resolve_fw_url(dev_id, ver, fw);
      s_ota_info.latest_stable_sha256 = sha;
    }
    if(ver.length() > 0 && ver == s_ota_info.latest_prerelease){
      s_ota_info.latest_prerelease_url = ota_resolve_fw_url(dev_id, ver, fw);
      s_ota_info.latest_prerelease_sha256 = sha;
    }
  }
  if(s_ota_info.latest_stable.length() > 0 && s_ota_info.latest_stable_url.length() == 0){
    s_ota_info.latest_stable_url = ota_resolve_fw_url(dev_id, s_ota_info.latest_stable, "");
  }
  if(s_ota_info.latest_prerelease.length() > 0 && s_ota_info.latest_prerelease_url.length() == 0){
    s_ota_info.latest_prerelease_url = ota_resolve_fw_url(dev_id, s_ota_info.latest_prerelease, "");
  }

  s_ota_info.update_available = ota_is_newer(s_ota_info.latest_stable, s_ota_info.current_version);
  s_ota_info.checked = true;
  s_ota_info.last_checked_ms = millis();
  s_ota_info.last_error = "";
  log_msg("[OTA] Checked updates: current=%s latest_stable=%s available=%s",
          s_ota_info.current_version.c_str(),
          s_ota_info.latest_stable.length() ? s_ota_info.latest_stable.c_str() : "none",
          s_ota_info.update_available ? "YES" : "no");
  return true;
}

static bool ota_pull_from_url(const String& url, const String& expected_sha256, String& err_out){
  if(!ota_is_allowed_url(url)){
    err_out = "Blocked non-official firmware URL";
    return false;
  }
  if(WiFi.status() != WL_CONNECTED){
    err_out = "WiFi not connected";
    return false;
  }

  String exp_sha = expected_sha256;
  exp_sha.trim();
  exp_sha.toLowerCase();

  log_msg("[OTA] Starting HTTPS pull update from %s", url.c_str());

  tlsarena::Guard tg;
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  if(!http.begin(cli, url)){
    err_out = "Failed to open HTTPS connection";
    return false;
  }

  int code = http.GET();
  if(code != 200){
    http.end();
    err_out = String("Firmware download failed (HTTP ") + code + ")";
    return false;
  }

  int content_len = http.getSize();
  if(content_len <= 65536){
    http.end();
    err_out = String("Invalid firmware content length (") + content_len + ")";
    return false;
  }

  ota_splash_and_blank();

  if(!Update.begin((size_t)content_len, U_FLASH)){
    String uerr = Update.errorString();
    http.end();
    ota_restore_display();
    err_out = String("OTA Update.begin failed: ") + uerr;
    return false;
  }

  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0);

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[4096];
  size_t written_total = 0;
  bool magic_checked = false;
  uint32_t last_rx = millis();

  while(written_total < (size_t)content_len){
    size_t avail = stream->available();
    if(avail > 0){
      size_t to_read = avail < sizeof(buf) ? avail : sizeof(buf);
      size_t rem = (size_t)content_len - written_total;
      if(to_read > rem) to_read = rem;
      int n = stream->readBytes(buf, to_read);
      if(n > 0){
        if(!magic_checked){
          magic_checked = true;
          if(buf[0] != 0xE9){
            mbedtls_sha256_free(&sha_ctx);
            Update.abort();
            http.end();
            ota_restore_display();
            err_out = "Downloaded file is not a valid ESP32 firmware image";
            return false;
          }
        }
        mbedtls_sha256_update(&sha_ctx, buf, (size_t)n);
        if(Update.write(buf, (size_t)n) != (size_t)n){
          String uerr = Update.errorString();
          mbedtls_sha256_free(&sha_ctx);
          Update.abort();
          http.end();
          ota_restore_display();
          err_out = String("Flash write error: ") + uerr;
          return false;
        }
        written_total += (size_t)n;
        last_rx = millis();
      }
    } else if(!http.connected()){
      break;
    } else if(millis() - last_rx > 15000){
      break;
    } else {
      delay(2);
    }
  }
  http.end();

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha_ctx, digest);
  mbedtls_sha256_free(&sha_ctx);

  char hex[65];
  for(int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
  hex[64] = '\0';

  if(written_total != (size_t)content_len){
    Update.abort();
    ota_restore_display();
    err_out = String("Incomplete download (") + (unsigned)written_total + "/" + (unsigned)content_len + " bytes)";
    return false;
  }

  if(exp_sha.length() == 64 && exp_sha != String(hex)){
    Update.abort();
    ota_restore_display();
    err_out = "SHA-256 checksum mismatch; update aborted";
    log_msg("[OTA] SHA-256 mismatch: expected %s got %s", exp_sha.c_str(), hex);
    return false;
  }

  if(!Update.end(true)){
    String uerr = Update.errorString();
    ota_restore_display();
    err_out = String("OTA finalize failed: ") + uerr;
    return false;
  }

  log_msg("[OTA] Pull update complete (%u bytes, sha256=%s), rebooting", (unsigned)written_total, hex);
  return true;
}

static String ota_info_to_json(){
  String j = "{";
  j += "\"ok\":true";
  j += ",\"checked\":" + String(s_ota_info.checked ? "true" : "false");
  j += ",\"update_available\":" + String(s_ota_info.update_available ? "true" : "false");
  j += ",\"current_version\":\"" + ota_json_esc(ota_norm_ver_display(s_ota_info.current_version)) + "\"";
  j += ",\"latest_stable\":\"" + ota_json_esc(s_ota_info.latest_stable) + "\"";
  j += ",\"latest_stable_url\":\"" + ota_json_esc(s_ota_info.latest_stable_url) + "\"";
  j += ",\"latest_stable_sha256\":\"" + ota_json_esc(s_ota_info.latest_stable_sha256) + "\"";
  j += ",\"latest_prerelease\":\"" + ota_json_esc(s_ota_info.latest_prerelease) + "\"";
  j += ",\"latest_prerelease_url\":\"" + ota_json_esc(s_ota_info.latest_prerelease_url) + "\"";
  j += ",\"latest_prerelease_sha256\":\"" + ota_json_esc(s_ota_info.latest_prerelease_sha256) + "\"";
  j += ",\"last_checked_ms\":" + String((unsigned long)s_ota_info.last_checked_ms);
  j += ",\"last_error\":\"" + ota_json_esc(s_ota_info.last_error) + "\"";
  j += "}";
  return j;
}

static void ota_pull_register_routes(WebServer& w){
  w.on("/api/ota/status", HTTP_GET, [&w](){
    w.send(200, "application/json", ota_info_to_json());
  });

  auto handle_check = [&w](){
    ota_check_for_updates();
    w.send(200, "application/json", ota_info_to_json());
  };
  w.on("/api/ota/check", HTTP_GET, handle_check);
  w.on("/api/ota/check", HTTP_POST, handle_check);

  w.on("/ota-web", HTTP_GET, [&w](){
    String target_ver = w.arg("version");
    String fw_url     = w.arg("url");
    String sha256     = w.arg("sha256");

    if(fw_url.length() == 0 && s_ota_info.latest_stable_url.length() > 0){
      fw_url = s_ota_info.latest_stable_url;
      if(target_ver.length() == 0) target_ver = s_ota_info.latest_stable;
      if(sha256.length() == 0) sha256 = s_ota_info.latest_stable_sha256;
    }

    if(!ota_is_allowed_url(fw_url)){
      w.send(400, "text/html; charset=utf-8",
        "<!doctype html><html><body style='font-family:sans-serif;padding:24px;max-width:520px;margin:auto;background:#fbf7f0;color:#23303a;'>"
        "<h2 style='color:#e5352b;'>Invalid Firmware Source</h2>"
        "<p>For security, StoryDeck Touch only installs updates hosted on official StoryDeck GitHub URLs.</p>"
        "<p><a href='/settings'>Return to Settings</a></p></body></html>");
      return;
    }

    String cur_disp = ota_norm_ver_display(STORYDECK_VERSION);
    String tgt_disp = target_ver.length() ? ota_norm_ver_display(target_ver) : String("Selected Release");

    String h =
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>StoryDeck Touch — Confirm Wireless Update</title><style>"
      "body{font-family:-apple-system,system-ui,sans-serif;background:#fbf7f0;color:#23303a;max-width:520px;margin:28px auto;padding:18px}"
      ".card{background:#fff;border:1px solid #e7e0d2;border-radius:16px;padding:24px;box-shadow:0 4px 12px rgba(0,0,0,0.06)}"
      "h1{color:#e5352b;font-size:1.4em;margin:0 0 8px}"
      ".ver-box{background:#f4ede0;border-radius:10px;padding:12px 14px;margin:14px 0;font-size:0.95em}"
      ".pin-in{width:100%;box-sizing:border-box;padding:12px;font-size:1.25em;letter-spacing:0.35em;text-align:center;border:1px solid #d8d0c0;border-radius:10px;margin:8px 0 14px}"
      "button{width:100%;background:#e5352b;color:#fff;border:0;border-radius:11px;padding:13px;font-size:1.05em;font-weight:700;cursor:pointer}"
      "button:disabled{opacity:0.6;cursor:wait}"
      ".msg{margin-top:14px;padding:10px 12px;border-radius:8px;font-size:0.92em;display:none}"
      ".err{background:#fce8e6;color:#c5221f;display:block}.ok{background:#e6f4ea;color:#137333;display:block}.wait{background:#e8eaed;color:#3c4043;display:block}"
      "a{color:#e5352b;text-decoration:none;font-weight:600}"
      "</style></head><body><div class='card'>"
      "<h1>📡 Install Wireless Update</h1>"
      "<p style='margin:0;color:#5f6368;font-size:0.93em;'>Confirm you want to update <strong>" + ota_html_esc(cfg::kiosk_name) + "</strong> over Wi-Fi. Saved Wi-Fi and Yoto settings are kept.</p>"
      "<div class='ver-box'>"
      "<div><strong>Current:</strong> " + ota_html_esc(cur_disp) + " &rarr; <strong>New:</strong> " + ota_html_esc(tgt_disp) + "</div>"
      "<div style='margin-top:6px;font-size:0.82em;color:#5f6368;word-break:break-all;'><strong>Source:</strong> " + ota_html_esc(fw_url) + "</div>"
      "</div>"
      "<form id='otaForm' onsubmit='return runOta(event)'>"
      "<label style='font-weight:600;font-size:0.92em;'>Enter 4-Digit Settings PIN</label>"
      "<input id='pin' class='pin-in' type='password' inputmode='numeric' maxlength='8' autocomplete='off' placeholder='••••' required>"
      "<button id='goBtn' type='submit'>⚡ Install Update Now</button>"
      "</form>"
      "<div id='stMsg' class='msg'></div>"
      "<p style='margin:16px 0 0;font-size:0.88em;text-align:center;'><a href='/settings'>&larr; Back to Settings</a></p>"
      "</div><script>\n"
      "var FW_URL = " + String("\"") + ota_json_esc(fw_url) + "\";\n"
      "var FW_SHA = " + String("\"") + ota_json_esc(sha256) + "\";\n"
      "function runOta(e){\n"
      "  e.preventDefault();\n"
      "  var pin = document.getElementById('pin').value.trim();\n"
      "  var btn = document.getElementById('goBtn');\n"
      "  var msg = document.getElementById('stMsg');\n"
      "  btn.disabled = true; btn.innerText = 'Downloading & Flashing... (~30s)';\n"
      "  msg.className = 'msg wait'; msg.innerText = 'Downloading verified firmware and updating device. Do not unplug power...';\n"
      "  fetch('/api/ota/pull', {\n"
      "    method: 'POST',\n"
      "    headers: {'Content-Type': 'application/json'},\n"
      "    body: JSON.stringify({ url: FW_URL, sha256: FW_SHA, pin: pin })\n"
      "  }).then(function(r){ return r.json().then(function(d){ return {status:r.status, data:d}; }); })\n"
      "  .then(function(res){\n"
      "    if(res.data && res.data.ok){\n"
      "      msg.className = 'msg ok';\n"
      "      msg.innerText = 'Update installed! StoryDeck Touch is rebooting now...';\n"
      "      setTimeout(function(){ window.location.href = '/settings'; }, 8000);\n"
      "    } else {\n"
      "      btn.disabled = false; btn.innerText = '⚡ Install Update Now';\n"
      "      msg.className = 'msg err';\n"
      "      msg.innerText = (res.data && res.data.error) ? res.data.error : ('Update failed (HTTP ' + res.status + ')');\n"
      "    }\n"
      "  }).catch(function(){\n"
      "    msg.className = 'msg ok';\n"
      "    msg.innerText = 'Device rebooted to finish installing the update. Redirecting to Settings...';\n"
      "    setTimeout(function(){ window.location.href = '/settings'; }, 8000);\n"
      "  });\n"
      "  return false;\n"
      "}\n"
      "</script></body></html>";
    w.send(200, "text/html; charset=utf-8", h);
  });

  w.on("/api/ota/pull", HTTP_POST, [&w](){
    String url = w.arg("url");
    String sha = w.arg("sha256");
    String pin = w.arg("pin");

    if(w.hasArg("plain") && url.length() == 0){
      JsonDocument doc;
      if(!deserializeJson(doc, w.arg("plain"))){
        url = (const char*)(doc["url"] | "");
        sha = (const char*)(doc["sha256"] | "");
        pin = (const char*)(doc["pin"] | "");
      }
    }

    if(!ota_pin_valid(pin)){
      w.send(403, "application/json", "{\"ok\":false,\"error\":\"Incorrect Settings PIN\"}");
      return;
    }
    if(!ota_is_allowed_url(url)){
      w.send(400, "application/json", "{\"ok\":false,\"error\":\"Firmware URL must be hosted on official StoryDeck GitHub releases\"}");
      return;
    }

    String err;
    if(!ota_pull_from_url(url, sha, err)){
      w.send(500, "application/json", String("{\"ok\":false,\"error\":\"") + ota_json_esc(err) + "\"}");
      return;
    }

    w.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(600);
    ESP.restart();
  });
}
