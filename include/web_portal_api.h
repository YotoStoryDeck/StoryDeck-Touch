#pragma once
// Web settings portal HTTP handlers (settings GET/POST, curation, ping, discovery, reboot).
#include "web_portal_html.h"

namespace portal {

static void handle_get(WebServer& w){
  if(!auth_ok(w)) return;
  if(!s_sd_ok) sd_probe_and_mount();
  String tab = w.arg("tab");
  if(tab == "curation" && cfg::op_mode != "core"){
    curation::load();
    if(w.hasArg("refresh") || (s_library_cards.empty() && yoto::is_authenticated())){
      fetch_library();
    }
    std::vector<Card, PsramVectorAlloc<Card>>& card_list = !s_library_cards.empty() ? s_library_cards : s_cards;
    std::vector<Card, PsramVectorAlloc<Card>> sorted_cards = card_list;
    std::sort(sorted_cards.begin(), sorted_cards.end(), [](const Card& a, const Card& b){
      return a.sort_title < b.sort_title;
    });

    String ch = "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>StoryDeck Standalone Curation</title><style>"
      "body{font-family:-apple-system,system-ui,sans-serif;background:#fbf7f0;color:#23303a;max-width:720px;margin:0 auto;padding:18px}"
      "h1{color:#e5352b;font-size:1.5em;margin:0}h2{font-size:1.05em;margin:1.4em 0 .3em;color:#3a6b50}"
      "table{width:100%;border-collapse:collapse;margin-top:14px;background:#fff;border-radius:10px;overflow:hidden;box-shadow:0 1px 3px rgba(0,0,0,0.1)}"
      "th,td{padding:10px 12px;text-align:left;border-bottom:1px solid #f0eae0}"
      "th{background:#f4ede0;color:#23303a;font-weight:700;font-size:0.9em}"
      "button.save-btn{background:#e5352b;color:#fff;border:0;border-radius:11px;padding:12px 22px;font-size:1.05em;font-weight:700;margin-top:14px;cursor:pointer;width:100%}"
      ".nav-toolbar{display:flex;gap:6px;flex-wrap:wrap;align-items:center;background:#f0e8da;padding:6px;border-radius:10px;margin-bottom:18px;border:1px solid #e5dcce;}.nav-btn{display:inline-flex;align-items:center;gap:5px;padding:7px 12px;border-radius:7px;text-decoration:none;font-weight:600;font-size:0.88em;color:#23303a;background:#fff;border:1px solid #dcd3c4;box-shadow:0 1px 2px rgba(0,0,0,0.03);}.nav-btn.active{background:#e5352b;color:#fff;border-color:#cf2b22;}"
      "</style></head><body>" +
      portal_toolbar("curation") +
      "<div style='display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;margin-bottom:8px;'>"
      "<h1>Card Curation &amp; Ownership</h1>"
      "<a href='/settings?tab=curation&refresh=1' class='nav-btn' style='font-size:0.85em;padding:6px 12px;'>🔄 Refresh from Yoto (" + String((int)sorted_cards.size()) + " cards)</a>"
      "</div>"
      "<div class='info' style='display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;margin-bottom:14px;background:#fff;border:1px solid #e7e0d2;border-radius:10px;padding:10px 14px;'>"
      "<div><strong>Storage Mode:</strong> " + (s_sd_ok ? "<span style='color:#137333;font-weight:700;'>💾 MicroSD Mounted (/config/curation.json)</span>" : "<span style='color:#b06000;font-weight:700;'>⚡ Internal Flash NVS (No MicroSD Card)</span>") + "</div>"
      "<small style='color:#7a7060;'>" + (s_sd_ok ? "Curation saved to SD &bull; Covers cached" : "Curation saved to Flash NVS &bull; Covers stream live") + "</small>"
      "</div>"
      "<p style='color:#7a7060;font-size:0.92em;margin-bottom:12px;'>Manage sticky favorites (⭐), mark physical plastic card copies owned in bedroom stands (🂠), or completely hide cards from appearing in the catalog (🚫).</p>"
      "<form method='POST' action='/settings/curation'>"
      "<label style='display:flex;align-items:center;gap:10px;background:#fff;padding:12px;border-radius:10px;border:1px solid #e7e0d2;cursor:pointer;font-weight:600;'>"
      "<input type='checkbox' name='hide_phys' value='1'" + String(curation::get_hide_physical() ? " checked" : "") + " style='width:auto;margin:0;'>"
      "<span>Hide All Physical Cards from Touchscreen Catalog</span></label>"
      "<table><thead><tr><th style='width:50px;'>Cover</th><th>Title</th><th style='width:65px;text-align:center;'>Sticky &#11088;</th><th style='width:65px;text-align:center;'>Physical &#127188;</th><th style='width:65px;text-align:center;'>Hide &#128683;</th></tr></thead><tbody>";

    if(sorted_cards.empty()){ ch += "<tr><td colspan='5' style='text-align:center;padding:24px;color:#7a7060;'><em>No cards currently in memory. Connect your controller to WiFi and Yoto to load cards.</em></td></tr>"; }

    for(size_t i = 0; i < sorted_cards.size(); i++){
      if(sorted_cards[i].daily) continue;
      bool is_p = curation::is_card_pinned(sorted_cards[i].id.c_str());
      bool is_ph = curation::is_card_physical(sorted_cards[i].id.c_str());
      bool is_h = curation::is_card_hidden(sorted_cards[i].id.c_str());
      ch += "<tr><td>";
      if(sorted_cards[i].cover.length() > 0){
        ch += "<img src='" + esc(sorted_cards[i].cover.c_str()) + "' style='width:36px;height:54px;object-fit:cover;border-radius:4px;'>";
      } else {
        ch += "<div style='width:36px;height:54px;background:#e2dacd;border-radius:4px;display:flex;align-items:center;justify-content:center;color:#8a8070;font-size:18px;'>&#127188;</div>";
      }
      ch += "</td>";
      ch += "<td><strong>" + esc(sorted_cards[i].title.c_str()) + "</strong><br><small style='color:#9a9384;'>" + esc(sorted_cards[i].id.c_str()) + "</small></td>";
      ch += "<td style='text-align:center;'><input type='checkbox' name='pin_" + esc(sorted_cards[i].id.c_str()) + "' value='1'" + (is_p ? " checked" : "") + "></td>";
      ch += "<td style='text-align:center;'><input type='checkbox' name='phys_" + esc(sorted_cards[i].id.c_str()) + "' value='1'" + (is_ph ? " checked" : "") + "></td>";
      ch += "<td style='text-align:center;'><input type='checkbox' name='hide_" + esc(sorted_cards[i].id.c_str()) + "' value='1'" + (is_h ? " checked" : "") + "></td></tr>";
    }

    ch += "</tbody></table>"
      "<button type='submit' class='save-btn'>" + String(s_sd_ok ? "💾 Save Curation to MicroSD" : "⚡ Save Curation to Flash (NVS)") + "</button></form>"
      "</body></html>";
    w.send(200, "text/html; charset=utf-8", ch);
    return;
  }

  String h =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>StoryDeck Touch</title><style>"
    "body{font-family:-apple-system,system-ui,sans-serif;background:#fbf7f0;color:#23303a;max-width:560px;margin:0 auto;padding:18px}"
    "h1{color:#e5352b;font-size:1.5em}h2{font-size:1.05em;margin:1.4em 0 .3em;color:#3a6b50}"
    "label{display:block;margin:.7em 0;font-weight:600;font-size:.92em}"
    "input,select{display:block;width:100%;box-sizing:border-box;padding:9px;margin-top:4px;border:1px solid #d8d0c0;border-radius:9px;font-size:1em;background:#fff}"
    "small{font-weight:400;color:#9a9384}"
    "button.save-btn{background:#e5352b;color:#fff;border:0;border-radius:11px;padding:12px 22px;font-size:1.05em;font-weight:700;margin-top:14px;cursor:pointer;width:100%}"
    "button.test-btn{background:#fff;color:#23303a;border:1px solid #d8d0c0;border-radius:8px;padding:6px 14px;font-size:.88em;font-weight:600;cursor:pointer;margin-top:6px}"
    "button.test-btn:hover{background:#f3e9d8}"
    "a{color:#e5352b}.info{background:#fff;border-radius:11px;padding:10px 14px;font-size:.9em;color:#666}"
    ".status-badge{display:inline-block;padding:3px 8px;border-radius:6px;font-size:0.85em;margin-left:8px;font-weight:600}"
    ".badge-ok{background:#e6f4ea;color:#137333}.badge-err{background:#fce8e6;color:#c5221f}.badge-wait{background:#e8eaed;color:#5f6368}.nav-toolbar{display:flex;gap:6px;flex-wrap:wrap;align-items:center;background:#f0e8da;padding:6px;border-radius:10px;margin-bottom:18px;border:1px solid #e5dcce;}.nav-btn{display:inline-flex;align-items:center;gap:5px;padding:7px 12px;border-radius:7px;text-decoration:none;font-weight:600;font-size:0.88em;color:#23303a;background:#fff;border:1px solid #dcd3c4;box-shadow:0 1px 2px rgba(0,0,0,0.03);}.nav-btn.active{background:#e5352b;color:#fff;border-color:#cf2b22;}"
    "</style><script>\n"
    "var s_yoto_pkce_verifier = '';\n"
    "var s_yoto_redirect_uri = 'http://127.0.0.1:8787/callback';\n"
    "\n"
    "function startYotoOAuth(){\n"
    "  var cidIn = document.getElementById('cid');\n"
    "  var cid = cidIn ? cidIn.value.trim() : '';\n"
    "  if(!cid){\n"
    "    alert('Please enter your Yoto Client ID first.');\n"
    "    if(cidIn) cidIn.focus();\n"
    "    return;\n"
    "  }\n"
    "  var btn = document.getElementById('btn_yoto_oauth');\n"
    "  if(btn){ btn.disabled = true; btn.innerText = 'Connecting...'; }\n"
    "  fetch('/api/yoto/oauth/start', {\n"
    "    method: 'POST',\n"
    "    headers: { 'Content-Type': 'application/json' },\n"
    "    body: JSON.stringify({ client_id: cid })\n"
    "  })\n"
    "  .then(function(r){ return r.json(); })\n"
    "  .then(function(d){\n"
    "    if(btn){ btn.disabled = false; btn.innerText = '🔑 Authenticate with Yoto'; }\n"
    "    if(d.ok && d.auth_url){\n"
    "      var pasteBox = document.getElementById('yoto_oauth_paste_box');\n"
    "      if(pasteBox) pasteBox.style.display = 'block';\n"
    "      var pop = window.open(d.auth_url, 'yotoAuthPopup', 'width=600,height=750,menubar=no,toolbar=no');\n"
    "      if(!pop || pop.closed || typeof pop.closed === 'undefined'){\n"
    "        window.open(d.auth_url, '_blank');\n"
    "      }\n"
    "    } else {\n"
    "      alert('Failed to start Yoto sign-in: ' + (d.error || 'Server error'));\n"
    "    }\n"
    "  })\n"
    "  .catch(function(e){\n"
    "    if(btn){ btn.disabled = false; btn.innerText = '🔑 Authenticate with Yoto'; }\n"
    "    alert('Network error connecting to StoryDeck Touch: ' + e.message);\n"
    "  });\n"
    "}\n"
    "\n"
    "function finishYotoOAuth(){\n"
    "  var rawIn = document.getElementById('yoto_callback_url');\n"
    "  var raw = rawIn ? rawIn.value.trim() : '';\n"
    "  if(!raw){\n"
    "    alert('Please paste the callback URL or authorization code.');\n"
    "    return;\n"
    "  }\n"
    "  var code = raw;\n"
    "  if(raw.indexOf('code=') >= 0){\n"
    "    try {\n"
    "      var u = new URL(raw.startsWith('http') ? raw : ('http://localhost/' + raw));\n"
    "      code = u.searchParams.get('code') || raw;\n"
    "    } catch(e){\n"
    "      var m = raw.match(/code=([^&]+)/);\n"
    "      if(m) code = decodeURIComponent(m[1]);\n"
    "    }\n"
    "  }\n"
    "  var verifier = s_yoto_pkce_verifier || sessionStorage.getItem('yoto_verifier') || '';\n"
    "  var cidEl = document.getElementById('cid');\n"
    "  var cid = (cidEl ? cidEl.value.trim() : '') || sessionStorage.getItem('yoto_cid') || '';\n"
    "  var csecEl = document.getElementById('csec');\n"
    "  var csec = csecEl ? csecEl.value.trim() : '';\n"
    "  var b = document.getElementById('yoto_oauth_badge');\n"
    "  if(b){\n"
    "    b.className = 'status-badge badge-wait';\n"
    "    b.innerText = 'Exchanging code for Yoto token on ESP32...';\n"
    "  }\n"
    "  \n"
    "  fetch('/api/yoto/oauth/exchange', {\n"
    "    method: 'POST',\n"
    "    headers: { 'Content-Type': 'application/json' },\n"
    "    body: JSON.stringify({\n"
    "      code: code,\n"
    "      verifier: verifier,\n"
    "      redirect_uri: s_yoto_redirect_uri,\n"
    "      client_id: cid,\n"
    "      client_secret: csec\n"
    "    })\n"
    "  })\n"
    "  .then(r => r.json())\n"
    "  .then(d => {\n"
    "    if(d.ok){\n"
    "      if(b){\n"
    "        b.className = 'status-badge badge-ok';\n"
    "        b.innerText = 'Authenticated! Discovered ' + (d.players ? d.players.length : 0) + ' player(s)';\n"
    "      }\n"
    "      var st = document.getElementById('yoto_status_text');\n"
    "      if(st) st.innerHTML = '<span style=\"color:#137333;font-weight:700;\">&#10004; Authenticated (Ready)</span>';\n"
    "      var yAuth = document.getElementById('yoto_is_authed');\n"
    "      if(yAuth) yAuth.value = '1';\n"
    "      var pasteBox = document.getElementById('yoto_oauth_paste_box');\n"
    "      if(pasteBox) pasteBox.style.display = 'none';\n"
    "      if(d.players && d.players.length > 0){\n"
    "        populatePlayerDropdown(d.players);\n"
    "      }\n"
    "      toggleModeFields();\n"
    "      var cb = document.getElementById('curation_banner'); if(cb) cb.style.display = 'flex';\n"
    "    } else {\n"
    "      if(b){\n"
    "        b.className = 'status-badge badge-err';\n"
    "        b.innerText = d.error || 'Authentication failed';\n"
    "      }\n"
    "    }\n"
    "  })\n"
    "  .catch(e => {\n"
    "    if(b){\n"
    "      b.className = 'status-badge badge-err';\n"
    "      b.innerText = 'Exchange failed: ' + e.message;\n"
    "    }\n"
    "  });\n"
    "}\n"
    "\n"
    "function fetchYotoPlayers(){\n"
    "  var b = document.getElementById('disc_badge');\n"
    "  if(b){ b.className = 'status-badge badge-wait'; b.innerText = 'Fetching Yoto players...'; }\n"
    "  fetch('/api/yoto/players')\n"
    "    .then(r => r.json())\n"
    "    .then(d => {\n"
    "      if(d.ok && d.players){\n"
    "        if(b){\n"
    "          b.className = 'status-badge badge-ok';\n"
    "          b.innerText = 'Discovered ' + d.players.length + ' Yoto player(s)';\n"
    "        }\n"
    "        populatePlayerDropdown(d.players);\n"
    "      } else {\n"
    "        if(b){\n"
    "          b.className = 'status-badge badge-err';\n"
    "          b.innerText = d.error || 'Failed to fetch players';\n"
    "        }\n"
    "      }\n"
    "    })\n"
    "    .catch(e => {\n"
    "      if(b){\n"
    "        b.className = 'status-badge badge-err';\n"
    "        b.innerText = 'Player fetch error: ' + e.message;\n"
    "      }\n"
    "    });\n"
    "}\n"
    "\n"
    "function populatePlayerDropdown(players){\n"
    "  var dsel = document.getElementById('dev_sel');\n"
    "  var devIn = document.getElementById('dev_in');\n"
    "  var curDev = devIn ? devIn.value : '';\n"
    "  if(!dsel) return;\n"
    "  dsel.innerHTML = '<option value=\"\">-- Select Target Player --</option>';\n"
    "  var matched = false;\n"
    "  players.forEach(function(pl){\n"
    "    var isSel = (pl.id === curDev);\n"
    "    if(isSel) matched = true;\n"
    "    dsel.innerHTML += '<option value=\"' + pl.id + '\"' + (isSel ? ' selected' : '') + '>' + pl.name + ' (' + pl.id + ') &bull; ' + (pl.online ? 'Online' : 'Offline') + '</option>';\n"
    "  });\n"
    "  if(!matched && players.length > 0){\n"
    "    dsel.selectedIndex = 1;\n"
    "    if(devIn) devIn.value = players[0].id;\n"
    "  }\n"
    "}\n"
    "\n"
    "function toggleModeFields(){\n"
    "  var mEl = document.getElementById('mode_sel');\n"
    "  var m = mEl ? mEl.value : '';\n"
    "  var isCore = (m === 'core');\n"
    "  var coreSec = document.getElementById('core_sec');\n"
    "  var ySec = document.getElementById('yoto_sec');\n"
    "  var rotSec = document.getElementById('rot_sec');\n"
    "  var profGrp = document.getElementById('prof_group');\n"
    "  var discBtnCore = document.getElementById('disc_btn_core');\n"
    "  var discBtnYoto = document.getElementById('disc_btn_yoto');\n"
    "  var devNotice = document.getElementById('dev_standalone_notice');\n"
    "  var dsel = document.getElementById('dev_sel');\n"
    "\n"
    "  if(coreSec) coreSec.style.display = isCore ? 'block' : 'none';\n"
    "  if(ySec) ySec.style.display = isCore ? 'none' : 'block';\n"
    "  if(rotSec) rotSec.style.display = isCore ? 'none' : 'block';\n"
    "  if(profGrp) profGrp.style.display = isCore ? 'block' : 'none';\n"
    "  if(discBtnCore) discBtnCore.style.display = isCore ? 'inline-block' : 'none';\n"
    "  var navCur = document.getElementById('nav_curation');\n"
    "  if(navCur) navCur.style.display = isCore ? 'none' : 'inline-flex';\n"
    "  if(discBtnYoto) discBtnYoto.style.display = isCore ? 'none' : 'inline-block';\n"
    "\n"
    "  if(!isCore){\n"
    "    var yAuthEl = document.getElementById('yoto_is_authed');\n"
    "    var yotoAuthOk = (yAuthEl && yAuthEl.value === '1');\n"
    "    if(devNotice) devNotice.style.display = yotoAuthOk ? 'none' : 'block';\n"
    "    if(!yotoAuthOk && dsel && dsel.options.length <= 1){\n"
    "      dsel.innerHTML = '<option value=\"\">-- Sign in to Yoto above to discover players --</option>';\n"
    "    }\n"
    "  } else {\n"
    "    if(devNotice) devNotice.style.display = 'none';\n"
    "  }\n"
    "}\n"
    "\n"
    "function onProfileSelectChange(){\n"
    "  var sel = document.getElementById('prof_sel');\n"
    "  if(!sel) return;\n"
    "  var val = sel.value;\n"
    "  var profIn = document.getElementById('prof_in');\n"
    "  if(val && profIn){ profIn.value = val; }\n"
    "  var opt = sel.options[sel.selectedIndex];\n"
    "  var defPlayer = opt ? opt.getAttribute('data-player') : '';\n"
    "  if(defPlayer){\n"
    "    var dsel = document.getElementById('dev_sel');\n"
    "    if(dsel) dsel.value = defPlayer;\n"
    "    var devIn = document.getElementById('dev_in');\n"
    "    if(devIn) devIn.value = defPlayer;\n"
    "  }\n"
    "}\n"
    "\n"
    "function onPlayerSelectChange(){\n"
    "  var sel = document.getElementById('dev_sel');\n"
    "  if(!sel) return;\n"
    "  var val = sel.value;\n"
    "  var devIn = document.getElementById('dev_in');\n"
    "  if(val && devIn){\n"
    "    devIn.value = val;\n"
    "  }\n"
    "}\n"
    "\n"
    "function testWifi(){\n"
    "  var sEl = document.getElementById('wf_in');\n"
    "  var pEl = document.getElementById('pass');\n"
    "  var s = sEl ? sEl.value : '';\n"
    "  var p = pEl ? pEl.value : '';\n"
    "  var b = document.getElementById('wifi_badge');\n"
    "  if(b){ b.className = 'status-badge badge-wait'; b.innerText = 'Testing connection...'; }\n"
    "  fetch('/api/ping/wifi?ssid=' + encodeURIComponent(s) + '&pass=' + encodeURIComponent(p))\n"
    "    .then(r => r.json())\n"
    "    .then(d => {\n"
    "      if(b){\n"
    "        b.className = 'status-badge ' + (d.ok ? 'badge-ok' : 'badge-err');\n"
    "        b.innerText = d.ok ? ('Connected (IP: ' + d.ip + ')') : ('Failed: ' + (d.error || 'Check credentials'));\n"
    "      }\n"
    "    })\n"
    "    .catch(e => {\n"
    "      if(b){ b.className = 'status-badge badge-err'; b.innerText = 'Ping failed: ' + e.message; }\n"
    "    });\n"
    "}\n"
    "\n"
    "function testCore(){\n"
    "  var uEl = document.getElementById('curl');\n"
    "  var u = uEl ? uEl.value : '';\n"
    "  var b = document.getElementById('core_badge');\n"
    "  if(b){ b.className = 'status-badge badge-wait'; b.innerText = 'Pinging Core...'; }\n"
    "  fetch('/api/ping/core?url=' + encodeURIComponent(u))\n"
    "    .then(r => r.json())\n"
    "    .then(d => {\n"
    "      if(b){\n"
    "        b.className = 'status-badge ' + (d.ok ? 'badge-ok' : 'badge-err');\n"
    "        b.innerText = d.ok ? 'StoryDeck Core reachable (HTTP 200)' : ('Unreachable: ' + (d.error || 'Check URL/DNS'));\n"
    "      }\n"
    "      if(d.ok){ fetchDiscovery(); }\n"
    "    })\n"
    "    .catch(e => {\n"
    "      if(b){ b.className = 'status-badge badge-err'; b.innerText = 'Ping request failed: ' + e.message; }\n"
    "    });\n"
    "}\n"
    "\n"
    "function fetchDiscovery(){\n"
    "  var uEl = document.getElementById('curl');\n"
    "  var u = uEl ? uEl.value : '';\n"
    "  var b = document.getElementById('disc_badge');\n"
    "  if(b){ b.className = 'status-badge badge-wait'; b.innerText = 'Fetching profiles & players...'; }\n"
    "  fetch('/api/portal/discovery?url=' + encodeURIComponent(u))\n"
    "    .then(r => r.json())\n"
    "    .then(d => {\n"
    "      if(d.ok){\n"
    "        if(b){\n"
    "          b.className = 'status-badge badge-ok';\n"
    "          b.innerText = 'Loaded ' + d.profiles.length + ' profiles, ' + d.players.length + ' players';\n"
    "        }\n"
    "        var psel = document.getElementById('prof_sel');\n"
    "        var curProf = document.getElementById('prof_in') ? document.getElementById('prof_in').value : '';\n"
    "        if(psel){\n"
    "          psel.innerHTML = '<option value=\"\">-- Select Profile or type below --</option>';\n"
    "          d.profiles.forEach(function(p){\n"
    "            var isSel = (p.id === curProf) ? ' selected' : '';\n"
    "            psel.innerHTML += '<option value=\"' + p.id + '\" data-player=\"' + (p.default_player_id || '') + '\"' + isSel + '>' + p.name + ' (' + p.id + ')</option>';\n"
    "          });\n"
    "        }\n"
    "        populatePlayerDropdown(d.players);\n"
    "      } else {\n"
    "        if(b){ b.className = 'status-badge badge-err'; b.innerText = d.error || 'Discovery failed'; }\n"
    "      }\n"
    "    })\n"
    "    .catch(e => {\n"
    "      if(b){ b.className = 'status-badge badge-err'; b.innerText = 'Discovery error: ' + e.message; }\n"
    "    });\n"
    "}\n"
    "window.addEventListener('DOMContentLoaded', toggleModeFields);\n"
    "</script></head><body>" +
    portal_toolbar("settings") +
    "<h1>StoryDeck Touch Settings</h1>"
    "<div class=info>IP " + WiFi.localIP().toString() +
    " &middot; Storage: " + (s_sd_ok ? "<span style='color:#137333;font-weight:700;'>💾 MicroSD</span>" : "<span style='color:#b06000;font-weight:700;'>⚡ Flash (NVS)</span>") +
    " &middot; WiFi " + String(WiFi.RSSI()) + " dBm"
    " &middot; free RAM " + String(ESP.getFreeHeap()/1024) + " KB"
    " &middot; v" STORYDECK_VERSION " &middot; <a href='/update'>firmware update</a></div>"
    "<form method='POST' action='/settings'>"
    "<h2>Device Identity</h2>" +
    text_row("Device Friendly Name", "kname", cfg::kiosk_name, "Host and display name for this controller panel.") +
    "<h2>WiFi (2.4 GHz only)</h2>" +
    wifi_picker() +
    secret_row("Password", "pass", "Saved on the board, used at next boot.") +
    "<div><button type='button' class='test-btn' onclick='testWifi()'>Test WiFi Connection</button><span id='wifi_badge'></span></div>"
    "<h2>StoryDeck Integration</h2>" +
    mode_select() +
    "<div id='core_sec'>" +
    text_row("StoryDeck Core URL", "curl", cfg::core_url, "Hub address (e.g. http://<server-ip>:5055 or http://localhost:5055). Required for Core Managed mode.") +
    "<div><button type='button' class='test-btn' onclick='testCore()'>Test StoryDeck Core URL</button><span id='core_badge'></span></div>" +
    "</div>" +
    "<div id='yoto_sec'>" +
    "<h2>Yoto Cloud Account (Standalone Direct)</h2>" +
    "<p style='color:#7a7060;font-size:0.92em;margin-bottom:12px;'>"
      "StoryDeck Touch connects directly to Yoto Cloud without a Core hub. Register a free developer app at <a href='https://dashboard.yoto.dev' target='_blank' rel='noopener' style='color:#e5352b;font-weight:600;'>dashboard.yoto.dev</a> (Public Client, Callback: <code>http://127.0.0.1:8787/callback</code>) and authenticate below."
    "</p>"
    "<input type='hidden' id='yoto_is_authed' value='" + String(yoto::is_authenticated() ? "1" : "0") + "'>" +
    text_row("Yoto Client ID", "cid", cfg::client_id, "From your Yoto Developer Dashboard application.") +
    secret_row("Client Secret (Optional)", "csec", "Optional: Leave blank for standard Public Client PKCE apps.") +
    "<div style='background:#f4ede0;border-radius:12px;padding:14px;margin-top:12px;border:1px solid #e7e0d2;'>"
    "<div style='display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;'>"
    "<div><div style='font-weight:700;color:#23303a;'>Yoto Cloud Connection Status</div>"
    "<div id='yoto_status_text' style='font-size:0.9em;'>" + (yoto::is_authenticated() ? "<span style='color:#137333;font-weight:700;'>&#10004; Authenticated (Ready)</span>" : "<span style='color:#e5352b;font-weight:600;'>&#9888; Not Authenticated</span>") + "</div></div>"
    "<button type='button' class='test-btn' id='btn_yoto_oauth' style='background:#e5352b;color:#fff;border:0;padding:10px 18px;font-weight:700;border-radius:8px;cursor:pointer;' onclick='startYotoOAuth()'>🔑 Authenticate with Yoto</button>"
    "</div>"
    "<div id='curation_banner' style='display:" + String(yoto::is_authenticated() ? "flex" : "none") + ";align-items:center;justify-content:space-between;gap:12px;background:#e6f4ea;border:1px solid #ceead6;border-radius:10px;padding:12px 14px;margin-top:12px;'>"
    "<div><div style='color:#137333;font-weight:700;font-size:0.95em;'>&#127188; Card Curation Ready</div>"
    "<div style='color:#2e7d32;font-size:0.85em;'>Customize favorites, physical card ownership, and catalog rotation.</div></div>"
    "<a href='/settings?tab=curation' style='background:#137333;color:#fff;text-decoration:none;font-weight:700;padding:8px 16px;border-radius:8px;font-size:0.9em;white-space:nowrap;'>Open Curation &rarr;</a>"
    "</div>"
    "<div id='yoto_oauth_paste_box' style='display:none;margin-top:14px;padding-top:14px;border-top:1px dashed #d7cebe;'>"
    "<div style='font-weight:600;font-size:0.92em;color:#23303a;margin-bottom:6px;'>Step 2: Complete Authorization</div>"
    "<div style='font-size:0.88em;color:#5f6368;margin-bottom:8px;'>Sign into Yoto in the opened window. When redirected to <code>http://127.0.0.1:8787/callback?code=...</code>, copy the full URL from your browser address bar and paste it below:</div>"
    "<div style='display:flex;gap:8px;'>"
    "<input type='text' id='yoto_callback_url' placeholder='http://127.0.0.1:8787/callback?code=...' style='margin:0;flex:1;'>"
    "<button type='button' class='test-btn' style='background:#137333;color:#fff;border:0;padding:10px 16px;font-weight:700;border-radius:8px;cursor:pointer;' onclick='finishYotoOAuth()'>Complete Sign-In</button>"
    "</div><div id='yoto_oauth_badge' style='margin-top:8px;'></div></div></div>"
    "</div>" +
    profile_device_picker() +
        "<div id='rot_sec'>" +
    "<h2>Library Rotation &amp; Discovery (Standalone)</h2>" +
    "<label>Maximum Volume Ceiling (All Modes)"
    "<input type='number' name='vollim' id='vollim' min='10' max='100' value='" + String(cfg::vol_limit) + "'>"
    "<small>Parent volume ceiling across Core and Standalone modes (10-100%, default: 100).</small></label>" +
    "<label>Catalog Card Limit"
    "<input type='number' name='catlim' id='catlim' min='10' max='300' value='" + String(cfg::catalog_card_limit) + "'>"
    "<small>Maximum cards loaded into grid memory (default: 60). Lower values optimize RAM and scrolling speed.</small></label>" +
    "<label>Daily Rotation Refresh Hour (0-23)"
    "<input type='number' name='rothr' id='rothr' min='0' max='23' value='" + String(cfg::rotation_hour) + "'>"
    "<small>Local hour when discovery rotation re-rolls (default: 3 for 3:00 AM).</small></label>" +
    "</div>" +

    "<h2>Clock</h2>" +
    tz_select() +
    "<h2>Security &amp; PIN</h2>" +
    text_row("Parent 4-Digit PIN", "pin", cfg::parent_pin, "Protects Grown-ups settings on the device touch screen (default 1234).") +
    "<button type='submit' class='save-btn'>Save Settings</button></form>"
    "<p><small>WiFi and credential changes take effect after a reboot: "
    "<a href='/reboot'>reboot now</a>.</small></p>"
    "</body></html>";
  w.send(200, "text/html; charset=utf-8", h);
}

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
  v = w.arg("rt");
  if(v.length()) yoto::set_refresh(v);                  // also clears the cached access token

  // Instantly apply volume limit and catalog card count changes
  np_update_vol_ui();
  if(cfg::op_mode == "core"){
    fetch_library_core();
  } else {
    build_catalog_from_library();
  }
  build_grid();

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
    build_catalog_from_library();
    if(s_scr_home) build_grid();
  }

  String storage_notice = s_sd_ok ? "Saved to MicroSD (/config/curation.json)." : "Saved to Internal Flash NVS (no MicroSD card inserted).";

  w.send(200, "text/html; charset=utf-8",
    "<!doctype html><body style='font-family:system-ui;background:#fbf7f0;padding:24px;text-align:center'>"
    "<h2 style='color:#3a6b50'>&#10004; Curation Saved</h2>"
    "<p>" + storage_notice + " Active catalog and touchscreen display updated.</p>"
    "<p><a href='/settings?tab=curation' style='color:#e5352b;font-weight:700;'>Back to Card Curation</a> &middot; <a href='/settings'>Settings</a></p></body>");
}

} // namespace portal
