#pragma once
// Web settings portal standalone Yoto PKCE OAuth endpoints & callback.
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

#include "web_portal_html.h"

namespace portal {

static String s_pkce_verifier = "";
static String s_pkce_client_id = "";
static const char* s_pkce_redirect_uri = "http://127.0.0.1:8787/callback";

static String url_encode(const String& str){
  String encoded = "";
  char buf[4];
  for(size_t i = 0; i < str.length(); i++){
    char c = str[i];
    if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~'){
      encoded += c;
    } else if(c == ' '){
      encoded += "%20";
    } else {
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

static String base64url_encode(const uint8_t* src, size_t len){
  size_t olen = 0;
  mbedtls_base64_encode(NULL, 0, &olen, src, len);
  uint8_t* buf = (uint8_t*)malloc(olen + 1);
  if(!buf) return "";
  mbedtls_base64_encode(buf, olen + 1, &olen, src, len);
  buf[olen] = '\0';
  String res = (char*)buf;
  free(buf);
  res.replace("+", "-");
  res.replace("/", "_");
  while(res.endsWith("=")) res.remove(res.length() - 1);
  return res;
}

// Standalone: Generate PKCE challenge on ESP32 (bypasses browser crypto.subtle HTTP block)
static void handle_yoto_oauth_start(WebServer& w){
  if(!auth_ok(w)) return;
  String cid = w.arg("client_id");
  if(cid.length() == 0 && w.hasArg("plain")){
    JsonDocument doc;
    if(!deserializeJson(doc, w.arg("plain"))){
      cid = (const char*)(doc["client_id"] | "");
    }
  }
  if(cid.length() == 0) cid = cfg::client_id;
  if(cid.length() == 0){
    w.send(200, "application/json", "{\"ok\":false,\"error\":\"Client ID empty\"}");
    return;
  }
  s_pkce_client_id = cid;

  // 32 random bytes for verifier via hardware TRNG
  uint8_t v_raw[32];
  esp_fill_random(v_raw, 32);
  s_pkce_verifier = base64url_encode(v_raw, 32);

  // SHA256 challenge via hardware accelerated mbedTLS
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t*)s_pkce_verifier.c_str(), s_pkce_verifier.length(), hash, 0);
  String challenge = base64url_encode(hash, 32);

  // 16 random bytes for state
  uint8_t s_raw[16];
  esp_fill_random(s_raw, 16);
  char state_hex[33];
  for(int i = 0; i < 16; i++) sprintf(state_hex + i*2, "%02x", s_raw[i]);
  state_hex[32] = '\0';

  String scope = "family:library:view user:content:view family:devices:view family:devices:control offline_access openid profile";
  String auth_url = "https://login.yotoplay.com/authorize?"
    "audience=https%3A%2F%2Fapi.yotoplay.com"
    "&scope=" + url_encode(scope) +
    "&response_type=code"
    "&client_id=" + url_encode(cid) +
    "&code_challenge=" + url_encode(challenge) +
    "&code_challenge_method=S256"
    "&redirect_uri=" + url_encode(String(s_pkce_redirect_uri)) +
    "&state=" + String(state_hex);

  JsonDocument outDoc;
  outDoc["ok"] = true;
  outDoc["auth_url"] = auth_url;
  String jsonStr;
  serializeJson(outDoc, jsonStr);
  w.send(200, "application/json", jsonStr);
}

// Standalone: Exchange OAuth code for Yoto token and discover players
static void handle_yoto_oauth_exchange(WebServer& w){
  if(!auth_ok(w)) return;
  if(w.hasArg("plain")){
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, w.arg("plain"));
    if(err){
      w.send(200, "application/json", "{\"ok\":false,\"error\":\"JSON parse error\"}");
      return;
    }
    String code = (const char*)(doc["code"] | "");
    String verifier = (const char*)(doc["verifier"] | "");
    if(verifier.length() == 0) verifier = s_pkce_verifier;
    String redirect_uri = (const char*)(doc["redirect_uri"] | "http://127.0.0.1:8787/callback");
    String client_id = (const char*)(doc["client_id"] | "");
    String client_secret = (const char*)(doc["client_secret"] | "");

    if(code.length() == 0){
      w.send(200, "application/json", "{\"ok\":false,\"error\":\"Authorization code empty\"}");
      return;
    }
    if(client_id.length() == 0) client_id = cfg::client_id;
    if(client_id.length() == 0){
      w.send(200, "application/json", "{\"ok\":false,\"error\":\"Client ID empty\"}");
      return;
    }

    String err_msg;
    bool ok = yoto::exchange_auth_code(code, verifier, redirect_uri, client_id, client_secret, err_msg);
    if(!ok){
      JsonDocument errDoc;
      errDoc["ok"] = false;
      errDoc["error"] = err_msg;
      String jsonErr;
      serializeJson(errDoc, jsonErr);
      w.send(200, "application/json", jsonErr);
      return;
    }

    // Save client_id to config
    cfg::client_id = client_id;
    if(client_secret.length() > 0) cfg::client_secret = client_secret;
    cfg::save();

    // Discover players from Yoto
    core::discover_standalone_players();

    String out = "{\"ok\":true,\"message\":\"Yoto authenticated successfully\",\"players\":[";
    for(int i = 0; i < core::s_standalone_player_count; i++){
      if(i > 0) out += ",";
      out += "{\"id\":\"" + esc(core::s_standalone_players[i].id) + "\",";
      out += "\"name\":\"" + esc(core::s_standalone_players[i].name) + "\",";
      out += "\"online\":" + String(core::s_standalone_players[i].online ? "true" : "false") + "}";
    }
    out += "]}";
    w.send(200, "application/json", out);
    return;
  }
  w.send(400, "application/json", "{\"ok\":false,\"error\":\"Bad request\"}");
}

// Standalone: Discover Yoto players
static void handle_yoto_players(WebServer& w){
  if(!auth_ok(w)) return;
  if(!yoto::is_authenticated()){
    w.send(200, "application/json", "{\"ok\":false,\"authenticated\":false,\"error\":\"Yoto not authenticated\"}");
    return;
  }
  bool ok = core::discover_standalone_players();
  String out = "{\"ok\":true,\"authenticated\":true,\"players\":[";
  for(int i = 0; i < core::s_standalone_player_count; i++){
    if(i > 0) out += ",";
    out += "{\"id\":\"" + esc(core::s_standalone_players[i].id) + "\",";
    out += "\"name\":\"" + esc(core::s_standalone_players[i].name) + "\",";
    out += "\"online\":" + String(core::s_standalone_players[i].online ? "true" : "false") + "}";
  }
  out += "]}";
  w.send(200, "application/json", out);
}

// Direct callback capture
static void handle_yoto_callback(WebServer& w){
  String code = w.arg("code");
  w.send(200, "text/html; charset=utf-8",
    "<!doctype html><body style='font-family:system-ui;background:#fbf7f0;padding:24px;text-align:center'>"
    "<h2 style='color:#137333'>&#10004; Yoto Authorization Code Received!</h2>"
    "<p>You can close this window and paste this authorization code into your settings page:</p>"
    "<pre style='background:#fff;padding:12px;border-radius:8px;border:1px solid #ccc;font-size:1.1em;'>" + esc(code) + "</pre>"
    "<p><a href='/settings' style='color:#e5352b;font-weight:700;'>Return to Settings</a></p></body>");
}

} // namespace portal
