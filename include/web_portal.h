#pragma once
// Web settings portal facade — routes registered on WebServer in begin().
#include <WebServer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "app_config.h"
#include "yoto_api.h"
#include "core_api.h"
#include "standalone_curation.h"
#include "ota_pull.h"

extern bool fetch_library();
extern void build_catalog_from_library();
extern void build_grid();
extern bool fetch_library_core();
extern void np_update_vol_ui();
extern void reload_catalog_and_thumbs(bool refetch_standalone = false);

#include "web_portal_html.h"
#include "web_portal_oauth.h"
#include "web_portal_pages.h"
#include "web_portal_api.h"

namespace portal {

static void begin(WebServer& w){

  w.on("/settings", HTTP_GET,  [&w](){ handle_get(w);    });
  w.on("/settings", HTTP_POST, [&w](){ handle_post(w);   });
  w.on("/settings/curation", HTTP_POST, [&w](){ handle_curation_post(w); });
  w.on("/reboot",   HTTP_GET,  [&w](){ handle_reboot(w); });
  w.on("/api/ping/wifi", HTTP_GET, [&w](){ handle_ping_wifi(w); });
  w.on("/api/ping/core", HTTP_GET, [&w](){ handle_ping_core(w); });
  w.on("/api/portal/discovery", HTTP_GET, [&w](){ handle_portal_discovery(w); });
  w.on("/api/yoto/oauth/start", HTTP_POST, [&w](){ handle_yoto_oauth_start(w); });
  w.on("/api/yoto/oauth/exchange", HTTP_POST, [&w](){ handle_yoto_oauth_exchange(w); });
  w.on("/api/yoto/players", HTTP_GET, [&w](){ handle_yoto_players(w); });
  w.on("/callback", HTTP_GET, [&w](){ handle_yoto_callback(w); });
}

} // namespace portal