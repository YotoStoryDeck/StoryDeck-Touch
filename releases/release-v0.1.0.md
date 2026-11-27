<p align="center">
  <img src="https://raw.githubusercontent.com/YotoStoryDeck/StoryDeck-Touch/main/assets/icon.svg" alt="StoryDeck Logo" width="88" height="88">
</p>

# StoryDeck Touch v0.1.0 — First Full Stable Release

**Release Tag:** `v0.1.0`
**Release Channel:** Full Production Release
**Target Hardware:** Waveshare ESP32-S3-Touch-LCD-4.3B (4.3" `800×480` Capacitive Touchscreen)
**Release Date:** November 27, 2026

---

## 📻 Overview

Welcome to **StoryDeck Touch `v0.1.0`** — the **first full stable release** of the dedicated bedside touchscreen companion controller for [Yoto](https://my.yotoplay.com/) players!

Engineered for the **Waveshare ESP32-S3-Touch-LCD-4.3B**, StoryDeck Touch lets listeners browse their curated audiobook and music library with full-color cover art, pick individual chapters, and start playback on a Yoto Player with a single tap—without needing a phone or tablet nearby.

---

## 🌟 What You Can Do with StoryDeck Touch

### 1. 🎨 Browse & Play Independently
* **Catalog Grid:** Scroll through crisp, full-color book and card covers for the device's assigned **Listener Profile**, complete with favorites (`★`) and subtle source badges.
* **Series Collections:** Automatically groups multi-book series under one cover card and lets listeners browse every book in numbered order.
* **Recently Played Shelf:** Jump right back into stories from the past 7 days with a single tap.
* **Chapter Selection & Playback Controls:** View track/chapter lists with custom 16×16 pixel icons, skip ±30 seconds, switch chapters, and adjust volume while automatically honoring your Yoto Player's volume limit.
* **Persistent Mini-Player:** Keeps active playback one tap away across browsing screens.

---

### 2. 🌐 Two Flexible Operating Modes
* **StoryDeck Core Mode (Recommended):** Connects to a self-hosted **[StoryDeck Core](https://github.com/YotoStoryDeck/StoryDeck-Core) (`v0.2.0+`)** hub with one-click Admin pairing approval. Locks the bedside unit to an assigned **Listener Profile**, combines **Audiobookshelf** and **Yoto** libraries, and supports remote Admin firmware updates and rollbacks.
* **Standalone Mode (Direct Yoto Cloud):** Works with no home server at all. Authenticates directly with your Yoto account over Wi-Fi and includes a built-in web **Card Curation** portal (`http://storydeck-touch.local/settings?tab=curation`) to pin favorites, flag physically owned cards, or hide cards from the display.

---

### 3. 🌙 Night Clock, Sleep Timers & PIN-Protected Settings
* **Bedtime & OK-to-Wake Night Clock:** Switches to a low-glare digital clock when idle, displaying red during bedtime hours, bright green during wake windows, and subtle pixel shifting to protect the screen.
* **PIN-Protected Settings Menu:** Long-press the gear icon and enter your 4-digit **Settings PIN** to adjust Wi-Fi, brightness, sleep timers, player lock, travel protection, and operating mode right on the display—or manage everything from the local web portal (`http://storydeck-touch.local/settings`).

---

### 4. 📡 Wireless Firmware Updates (OTA) & Built-In Update Checker
Once `v0.1.0+` is flashed onto your board once over USB-C, you never need to unplug it for updates:
* **Web Installer Wireless Update Tab:** Switch to **Wireless Update (OTA)** on the **[StoryDeck Touch Web Flasher](https://yotostorydeck.github.io/StoryDeck-Touch/)**, enter your device IP or `storydeck-touch.local`, and confirm with your 4-digit **Settings PIN**.
* **Automatic Boot & Daily Update Checker:** StoryDeck Touch automatically checks for new releases after boot and once every 24 hours. Open `http://storydeck-touch.local/settings` and click **Check for Updates** → **Install** anytime (PIN-protected, with SHA-256 verification and all Wi-Fi/Yoto settings preserved).
* **StoryDeck Core & PlatformIO OTA:** Also supports 1-click fleet updates/rollbacks from Core Admin and wireless CLI uploads via PlatformIO.

---

### 5. 💾 Optional MicroSD Caching & Safe Mode Recovery
* **Fast MicroSD Art Cache:** Adding a FAT32 MicroSD card caches book covers and chapter icons locally so libraries appear quickly on boot.
* **Automatic Recovery Hotspot:** If Wi-Fi fails to connect or repeated startup interruptions occur, StoryDeck Touch automatically launches a setup hotspot (`StoryDeck-Setup`) so you can adjust settings from any phone or browser without USB tools.

---

## 🛠️ Hardware & Minimum Requirements for Wireless OTA

| Component | Requirement | Notes |
| :--- | :--- | :--- |
| **Target Hardware** | **Waveshare ESP32-S3-Touch-LCD-4.3B** | 4.3" `800×480` capacitive touchscreen, 16MB Flash, 8MB PSRAM, USB-C power |
| **Minimum Version for Web OTA** | **StoryDeck Touch `v0.1.0+`** (installed once via USB-C) | Required for the Web Installer **Wireless Update (OTA)** tab and automatic update checks |
| **MicroSD Card** | Optional FAT32 card (`4GB–64GB`) | Speeds up cover loading; works without SD using flash NVS |
| **3D Enclosure** | Desktop & wall-mount STLs included (`3d-models/`) | Open-source 3D-printable stand and faceplate |

---

## 🚀 First-Time Install (USB-C Required)

1. Connect your **Waveshare ESP32-S3-Touch-LCD-4.3B** to your computer using a USB-C data cable.
2. Open Chrome or Edge to the **[StoryDeck Touch Web Flasher](https://yotostorydeck.github.io/StoryDeck-Touch/)**.
3. On the **First-Time USB Install** tab, click **Connect & Install StoryDeck Touch (`v0.1.0`)**, choose **Erase device**, and wait ~60 seconds.
4. Once rebooted, join the **`StoryDeck-Setup`** Wi-Fi hotspot or tap **Open Settings** on-screen to choose **StoryDeck Core Mode** or **Standalone Mode**.

*Prefer CLI flashing?* Use `esptool.py` with `firmware-merged.bin`, or build with PlatformIO:

```bash
git clone https://github.com/YotoStoryDeck/StoryDeck-Touch.git
cd StoryDeck-Touch
git checkout v0.1.0
pio run -e waveshare-s3-43b -t upload
```

---

## 📋 System Compatibility

| Component | Minimum / Recommended | Notes |
| :--- | :--- | :--- |
| **StoryDeck Core Hub** | `v0.2.0+` | Optional — required only for StoryDeck Core Mode and Tablet Kiosk |
| **Yoto Player** | Yoto Gen 2, Gen 3, Mini | Target playback speaker |
| **Web Flasher Browser** | Chrome or Microsoft Edge (Desktop) | Required for initial USB-C Web Serial install |

---

## 🤝 Ecosystem Links

* **StoryDeck Touch Repository:** [github.com/YotoStoryDeck/StoryDeck-Touch](https://github.com/YotoStoryDeck/StoryDeck-Touch)
* **Touch Web Flasher & OTA Tool:** [yotostorydeck.github.io/StoryDeck-Touch/](https://yotostorydeck.github.io/StoryDeck-Touch/)
* **StoryDeck Core Hub:** [github.com/YotoStoryDeck/StoryDeck-Core](https://github.com/YotoStoryDeck/StoryDeck-Core)
