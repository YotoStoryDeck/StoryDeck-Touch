#pragma once
// Live 800x480 LVGL Framebuffer Mirror (/screen, /screen.raw, /touch) & Live Console Logs (/logs, /api/logs)
#include <Arduino.h>
#include <WebServer.h>
#include <time.h>
#include "hal_display.h"
#include "app_log.h"

inline void ota_diagnostics_register_routes(WebServer& w){
  // Live 1:1 800x480 RGB565 Framebuffer Viewer & Touch Injector (/screen, /screen.raw, /touch)
  w.on("/screen.raw", HTTP_GET, [&w](){
    const uint16_t* fb = hal_get_framebuffer();
    if(!fb){
      w.send(503, "text/plain", "Framebuffer unavailable");
      return;
    }
    WiFiClient client = w.client();
    size_t total_bytes = (size_t)LCD_H_RES * (size_t)LCD_V_RES * 2;
    String hdr = "HTTP/1.1 200 OK\r\n"
                 "Content-Type: application/octet-stream\r\n"
                 "Access-Control-Allow-Origin: *\r\n"
                 "Cache-Control: no-store\r\n"
                 "Content-Length: " + String((unsigned)total_bytes) + "\r\n"
                 "Connection: close\r\n\r\n";
    client.print(hdr);
    const uint8_t* ptr = (const uint8_t*)fb;
    size_t rem = total_bytes;
    while(rem > 0 && client.connected()){
      size_t chunk = (rem > 16384) ? 16384 : rem;
      size_t written = client.write(ptr, chunk);
      if(written == 0) break;
      ptr += written;
      rem -= written;
    }
  });

  w.on("/touch", [&w](){
    int x = w.hasArg("x") ? w.arg("x").toInt() : 0;
    int y = w.hasArg("y") ? w.arg("y").toInt() : 0;
    bool down = w.hasArg("down") ? (w.arg("down").toInt() != 0) : false;
    hal_inject_web_touch(x, y, down);
    w.sendHeader("Access-Control-Allow-Origin", "*");
    w.send(200, "application/json", "{\"ok\":true}");
  });

  w.on("/screen", HTTP_GET, [&w](){
    static const char html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StoryDeck Touch — 800×480 Live LVGL Mirror</title>
<style>
  body { margin:0; background:#141010; color:#FBF7F0; font-family:system-ui,-apple-system,sans-serif; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:100vh; user-select:none; }
  .bar { display:flex; gap:14px; align-items:center; margin-bottom:10px; font-size:13px; color:#A89F94; }
  .badge { background:#E5352B; color:#fff; font-weight:700; padding:3px 10px; border-radius:99px; font-size:12px; }
  .frame { position:relative; border:4px solid #2D2624; border-radius:16px; overflow:hidden; box-shadow:0 16px 48px rgba(0,0,0,0.6); background:#000; width:800px; height:480px; }
  canvas { display:block; width:800px; height:480px; cursor:pointer; touch-action:none; }
  a { color:#FF8A65; text-decoration:none; }
</style>
</head>
<body>
  <div class="bar">
    <span class="badge">LIVE LVGL 800×480</span>
    <span id="st">Connecting...</span>
    <span>• Click / Drag to interact</span>
    <span>• <a href="/settings" target="_blank">Web Settings</a></span>
  </div>
  <div class="frame">
    <canvas id="cv" width="800" height="480"></canvas>
  </div>
<script>
const cv = document.getElementById('cv');
const ctx = cv.getContext('2d');
const st = document.getElementById('st');
const img = ctx.createImageData(800, 480);
const rgba = img.data;
let busy = false, isDown = false;

async function pullFrame(){
  if(busy) return;
  busy = true;
  const t0 = performance.now();
  try {
    const r = await fetch('/screen.raw?t=' + Date.now(), {cache:'no-store'});
    if(r.ok){
      const buf = await r.arrayBuffer();
      const u16 = new Uint16Array(buf);
      for(let i=0, j=0; i<u16.length; i++, j+=4){
        const p = u16[i];
        const r5 = (p >> 11) & 0x1F;
        const g6 = (p >> 5)  & 0x3F;
        const b5 =  p        & 0x1F;
        rgba[j]   = (r5 << 3) | (r5 >> 2);
        rgba[j+1] = (g6 << 2) | (g6 >> 4);
        rgba[j+2] = (b5 << 3) | (b5 >> 2);
        rgba[j+3] = 255;
      }
      ctx.putImageData(img, 0, 0);
      st.textContent = Math.round(performance.now() - t0) + ' ms';
    }
  } catch(e){
    st.textContent = 'Retrying...';
  }
  busy = false;
}
setInterval(pullFrame, 220);
pullFrame();

function coords(e){
  const rect = cv.getBoundingClientRect();
  const cx = (e.touches && e.touches[0]) ? e.touches[0].clientX : e.clientX;
  const cy = (e.touches && e.touches[0]) ? e.touches[0].clientY : e.clientY;
  return {
    x: Math.max(0, Math.min(799, Math.round((cx - rect.left) * 800 / rect.width))),
    y: Math.max(0, Math.min(479, Math.round((cy - rect.top)  * 480 / rect.height)))
  };
}
function sendTouch(x, y, down){
  fetch(`/touch?x=${x}&y=${y}&down=${down?1:0}`, {cache:'no-store'}).then(()=>{ setTimeout(pullFrame, 60); }).catch(()=>{});
}
cv.addEventListener('pointerdown', e => { isDown = true; cv.setPointerCapture(e.pointerId); const p = coords(e); sendTouch(p.x, p.y, true); });
cv.addEventListener('pointermove', e => { if(!isDown) return; const p = coords(e); sendTouch(p.x, p.y, true); });
cv.addEventListener('pointerup',   e => { if(!isDown) return; isDown = false; const p = coords(e); sendTouch(p.x, p.y, false); });
cv.addEventListener('pointercancel', e => { isDown = false; sendTouch(0, 0, false); });
</script>
</body>
</html>)rawliteral";
    w.send_P(200, "text/html", html);
  });

  // Live debug logger: /api/logs (JSON) & /logs (HTML live stream)
  w.on("/api/logs", HTTP_GET, [&w](){
    time_t now_epoch = time(nullptr);
    uint32_t now_ms = millis();
    bool ntp_synced = (now_epoch > 100000000);

    String json;
    json.reserve(256 + s_log_total * 140);
    json = "{\"total\":" + String(s_log_total) + ",\"synced\":" + (ntp_synced ? "true" : "false") + ",\"entries\":[";
    int start = (s_log_total < MAX_LOG_ENTRIES) ? 0 : s_log_head;
    for(int i = 0; i < s_log_total; i++){
      int idx = (start + i) % MAX_LOG_ENTRIES;
      if(i > 0) json += ",";

      time_t ent_epoch = s_log_ring[idx].epoch;
      if(ntp_synced && (ent_epoch <= 100000000)){
        int32_t diff_sec = (int32_t)((now_ms - s_log_ring[idx].ms) / 1000);
        ent_epoch = (now_epoch > diff_sec) ? (now_epoch - diff_sec) : 0;
      }

      char time_str[32] = "";
      if(ent_epoch > 100000000){
        struct tm tm_val;
        localtime_r(&ent_epoch, &tm_val);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_val);
      }

      json += "{\"t\":" + String(s_log_ring[idx].ms) +
              ",\"epoch\":" + String((unsigned long)ent_epoch) +
              ",\"time\":\"" + String(time_str) +
              "\",\"msg\":\"";
      for(const char* p = s_log_ring[idx].msg; *p; p++){
        if(*p == '"') json += "\\\"";
        else if(*p == '\\') json += "\\\\";
        else if(*p == '\n') json += "\\n";
        else if(*p == '\r') continue;
        else json += *p;
      }
      json += "\"}";
    }
    json += "]}";
    w.send(200, "application/json", json);
  });

  w.on("/logs", HTTP_GET, [&w](){
    static const char log_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StoryDeck Touch — Live Console Logs</title>
<style>
  body { margin:0; padding:18px; background:#121214; color:#E4E4E7; font-family:ui-monospace,SFMono-Regular,Consolas,monospace; font-size:13px; line-height:1.5; }
  .header { display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #27272A; padding-bottom:12px; margin-bottom:14px; }
  .title { font-size:16px; font-weight:700; color:#E5352B; display:flex; align-items:center; gap:8px; }
  .badge { background:#27272A; color:#A1A1AA; font-size:11px; padding:2px 8px; border-radius:4px; font-weight:600; }
  .log-box { display:flex; flex-direction:column; gap:4px; }
  .entry { display:flex; gap:12px; border-bottom:1px solid rgba(255,255,255,0.03); padding:4px 0; }
  .time { color:#A1A1AA; font-weight:600; min-width:155px; flex-shrink:0; letter-spacing:-0.2px; }
  .msg { color:#E4E4E7; word-break:break-all; }
  .tag-core { color:#38BDF8; font-weight:700; }
  .tag-sd { color:#FBBF24; font-weight:700; }
  .tag-cache { color:#34D399; font-weight:700; }
  .tag-err { color:#F87171; font-weight:700; }
  .controls { display:flex; gap:10px; align-items:center; }
  button { background:#E5352B; color:#fff; border:none; padding:4px 12px; border-radius:6px; cursor:pointer; font-weight:600; font-family:inherit; }
  button:hover { background:#DC2626; }
</style>
</head>
<body>
  <div class="header">
    <div class="title">⚡ StoryDeck Touch Backend Console <span class="badge" id="count">0 msgs</span></div>
    <div class="controls">
      <label><input type="checkbox" id="autoScroll" checked> Auto-scroll</label>
      <button onclick="fetchLogs()">Refresh</button>
      <a href="/screen" target="_blank" style="color:#A1A1AA; text-decoration:none; font-size:12px;">📺 Screen Mirror</a>
      <a href="/settings" target="_blank" style="color:#A1A1AA; text-decoration:none; font-size:12px;">⚙️ Settings</a>
    </div>
  </div>
  <div class="log-box" id="logBox">Loading backend logs...</div>
<script>
async function fetchLogs(){
  try {
    const res = await fetch('/api/logs?t=' + Date.now());
    if(!res.ok) return;
    const data = await res.json();
    document.getElementById('count').textContent = data.entries.length + ' entries' + (data.synced ? ' (NTP synced)' : '');
    const box = document.getElementById('logBox');
    box.innerHTML = '';
    data.entries.forEach(e => {
      const row = document.createElement('div');
      row.className = 'entry';
      const upSec = (e.t / 1000).toFixed(2) + 's';
      let timeHtml = '';
      if(e.time && e.time.length > 0) {
        timeHtml = `<span class="time" title="Uptime: ${upSec} | Epoch: ${e.epoch}">${e.time}</span>`;
      } else {
        timeHtml = `<span class="time" title="NTP pending (boot +${upSec})">+${upSec}</span>`;
      }
      let formatted = e.msg
        .replace(/\[CORE\]/g, '<span class="tag-core">[CORE]</span>')
        .replace(/\[SD\]/g, '<span class="tag-sd">[SD]</span>')
        .replace(/\[CACHE\]/g, '<span class="tag-cache">[CACHE]</span>')
        .replace(/\[WATCHDOG\]/g, '<span class="tag-cache">[WATCHDOG]</span>')
        .replace(/\[USER\]/g, '<span class="tag-core">[USER]</span>')
        .replace(/\[PLAY\]/g, '<span class="tag-core">[PLAY]</span>')
        .replace(/\[CTRL\]/g, '<span class="tag-core">[CTRL]</span>')
        .replace(/\[WIFI\]/g, '<span class="tag-sd">[WIFI]</span>')
        .replace(/\[NTP\]/g, '<span class="tag-cache">[NTP]</span>')
        .replace(/\[ERR\]|fail|error/gi, '<span class="tag-err">$&</span>');
      row.innerHTML = `${timeHtml}<span class="msg">${formatted}</span>`;
      box.appendChild(row);
    });
    if(document.getElementById('autoScroll').checked){
      window.scrollTo(0, document.body.scrollHeight);
    }
  } catch(err){}
}
setInterval(fetchLogs, 1500);
fetchLogs();
</script>
</body>
</html>)rawliteral";
    w.send_P(200, "text/html", log_html);
  });
}
