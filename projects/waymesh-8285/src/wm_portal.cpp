// =============================================================================
// wm_portal — see wm_portal.h. SoftAP + DNS captive redirect + a single HTTP
// form that writes the home channel + relay policy into wm_config (doc 13 §8.4).
// Compiled only in -DWAYMESH_WIFI_CONFIG builds.
// =============================================================================
#if defined(WAYMESH_WIFI_CONFIG) && WAYMESH_WIFI_CONFIG

#include "wm_portal.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>

#ifndef WM_PORTAL_PASS
#define WM_PORTAL_PASS "waymesh-setup"     // WPA2, >=8 chars (§8.4: protected AP)
#endif
#ifndef WM_PORTAL_TIMEOUT_MS
#define WM_PORTAL_TIMEOUT_MS 180000UL      // close + resume LoRa after 3 min idle
#endif
#ifndef WM_PORTAL_REBOOT_DELAY_MS
#define WM_PORTAL_REBOOT_DELAY_MS 1500UL   // let the "saved" page reach the browser
#endif

static const byte DNS_PORT = 53;
static DNSServer gDns;
static ESP8266WebServer gServer(80);
static wm_config_t *gCfg = NULL;
static IPAddress gApIp(10, 0, 0, 1);
static unsigned long gLastActivityMs = 0;
static bool gRebootReq = false;
static unsigned long gRebootAtMs = 0;

// --- helpers -----------------------------------------------------------------
static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse a user PSK string into bytes. Accepts: "default"/"01" -> {0x01} (the open
// default key, chanHash 8); ""/"none" -> len 0 (no crypto); 32 or 64 hex chars
// (':' / spaces ignored) -> 16/32 raw bytes (AES-128/256). Returns psk_len (0..32)
// or -1 on a malformed value.
static int parsePsk(const String &in, uint8_t *out) {
    String s = in; s.trim();
    String low = s; low.toLowerCase();
    if (low == "default" || s == "01") { out[0] = 0x01; return 1; }
    if (s.length() == 0 || low == "none") return 0;
    String h;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == ':' || c == ' ') continue;
        h += c;
    }
    if (h.length() != 32 && h.length() != 64) return -1;
    int n = h.length() / 2;
    for (int i = 0; i < n; i++) {
        int hi = hexNibble(h[2 * i]), lo = hexNibble(h[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return n;
}

static String pskDisplay(const wm_channel_t *c) {
    if (!c || c->psk_len == 0) return String("");
    if (c->psk_len == 1 && c->psk[0] == 0x01) return String("default");
    String h;
    for (uint8_t i = 0; i < c->psk_len; i++) {
        char b[3]; snprintf(b, sizeof(b), "%02x", c->psk[i]); h += b;
    }
    return h;
}

// Minimal attribute-safe escape for the channel name echoed into value='...'.
static String htmlEscape(const char *s) {
    String o;
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '&': o += F("&amp;"); break;
            case '<': o += F("&lt;");  break;
            case '>': o += F("&gt;");  break;
            case '"': o += F("&quot;"); break;
            case '\'': o += F("&#39;"); break;
            default: o += *p;
        }
    }
    return o;
}

static String renderPage(const String &banner) {
    const wm_channel_t *home = wm_config_home(gCfg);
    char name[WM_CHAN_NAME_MAX] = {0};
    uint8_t index = 0; int hash = -1;
    bool known = (gCfg->relay_policy == WM_RELAY_KNOWN);
    if (home) { strncpy(name, home->name, sizeof(name) - 1); index = home->index; hash = home->hash; }

    String p;
    p += F("<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>Waymesh setup</title><style>body{font-family:sans-serif;max-width:30em;margin:1em auto;padding:0 1em}"
           "label{display:block;margin:.6em 0 .2em;font-weight:bold}input,select{width:100%;padding:.4em;box-sizing:border-box}"
           ".b{background:#e8f0ff;border:1px solid #88a;padding:.5em;border-radius:4px;margin:.5em 0}"
           "small{color:#555}button{margin-top:1em;padding:.6em 1.2em}</style></head><body>");
    p += F("<h2>Waymesh node setup</h2>");
    if (banner.length()) { p += F("<div class=b>"); p += banner; p += F("</div>"); }
    p += F("<form method=POST action='/save'>");
    p += F("<label>Channel name</label><input name=name maxlength=15 value='"); p += htmlEscape(name); p += F("'>");
    p += F("<label>Channel index (0-7)</label><input name=index type=number min=0 max=7 value='"); p += String(index); p += F("'>");
    p += F("<label>PSK</label><input name=psk value='"); p += pskDisplay(home); p += F("'>");
    p += F("<small>default = open key (chanHash 8) &middot; 32 or 64 hex = AES-128/256 &middot; blank = no crypto</small>");
    p += F("<label>Relay policy</label><select name=relay>");
    p += F("<option value=all"); if (!known) p += F(" selected"); p += F(">relay-all (re-flood every group)</option>");
    p += F("<option value=known"); if (known) p += F(" selected"); p += F(">relay-known (only configured)</option>");
    p += F("</select>");
    p += F("<button type=submit>Save &amp; reboot</button></form>");
    p += F("<p><small>current chanHash: "); p += (hash >= 0 ? String(hash) : String("none")); p += F("</small></p>");
    p += F("</body></html>");
    return p;
}

static void handleRoot() {
    gLastActivityMs = millis();
    gServer.send(200, "text/html", renderPage(String()));
}

static void handleSave() {
    gLastActivityMs = millis();
    String name = gServer.arg("name"); name.trim();
    int index = gServer.arg("index").toInt();
    String pskStr = gServer.arg("psk");
    String relay = gServer.arg("relay");

    if (name.length() == 0 || name.length() >= WM_CHAN_NAME_MAX) {
        gServer.send(200, "text/html", renderPage(F("Error: channel name must be 1-15 chars."))); return;
    }
    if (index < 0 || index > 7) {
        gServer.send(200, "text/html", renderPage(F("Error: index must be 0-7."))); return;
    }
    uint8_t psk[WM_PSK_MAX];
    int psk_len = parsePsk(pskStr, psk);
    if (psk_len < 0) {
        gServer.send(200, "text/html", renderPage(F("Error: PSK must be 'default', blank, or 32/64 hex chars."))); return;
    }
    if (wm_config_add_channel(gCfg, name.c_str(), psk_len ? psk : NULL, (size_t)psk_len, (uint8_t)index) != 0) {
        gServer.send(200, "text/html", renderPage(F("Error: could not store channel (full or invalid)."))); return;
    }
    wm_config_set_home(gCfg, (uint8_t)index);
    wm_config_set_relay_policy(gCfg, relay == "known" ? WM_RELAY_KNOWN : WM_RELAY_ALL);

    const wm_channel_t *home = wm_config_home(gCfg);
    int hash = home ? home->hash : -1;
    String banner = String(F("Saved '")) + htmlEscape(name.c_str()) + F("' (chanHash ") +
                    (hash >= 0 ? String(hash) : String("none")) + F(", ") +
                    (relay == "known" ? F("relay-known") : F("relay-all")) + F("). Rebooting...");
    gServer.send(200, "text/html", renderPage(banner));
    Serial.printf("# portal: saved name='%s' index=%d psk_len=%d hash=%d relay=%s -> reboot\n",
                  name.c_str(), index, psk_len, hash, relay == "known" ? "known" : "all");
    gRebootReq = true;
    gRebootAtMs = millis() + WM_PORTAL_REBOOT_DELAY_MS;
}

// Captive-portal catch-all: redirect any host/path to the form.
static void handleNotFound() {
    gLastActivityMs = millis();
    gServer.sendHeader("Location", "http://10.0.0.1/", true);
    gServer.send(302, "text/plain", "");
}

bool wmPortalBegin(wm_config_t *cfg, uint32_t nodeId) {
    gCfg = cfg;
    gRebootReq = false;
    gRebootAtMs = 0;
    char ssid[20];
    snprintf(ssid, sizeof(ssid), "Waymesh_%04X", (unsigned)(nodeId & 0xFFFF));

    WiFi.persistent(false);  // provisioning AP — don't wear flash with WiFi creds
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(gApIp, gApIp, IPAddress(255, 255, 255, 0));
    bool ok = WiFi.softAP(ssid, WM_PORTAL_PASS);

    gDns.setErrorReplyCode(DNSReplyCode::NoError);
    gDns.start(DNS_PORT, "*", gApIp);

    gServer.on("/", handleRoot);
    gServer.on("/save", HTTP_POST, handleSave);
    gServer.onNotFound(handleNotFound);
    gServer.begin();

    gLastActivityMs = millis();
    Serial.printf("# portal: AP '%s' pass '%s' -> http://10.0.0.1/ (LoRa suspended)\n",
                  ssid, WM_PORTAL_PASS);
    return ok;
}

bool wmPortalService(void) {
    gDns.processNextRequest();
    gServer.handleClient();
    if (gRebootReq && (long)(millis() - gRebootAtMs) >= 0) return false;            // -> caller reboots
    if (!gRebootReq && (millis() - gLastActivityMs) > WM_PORTAL_TIMEOUT_MS) return false;  // idle close
    return true;
}

bool wmPortalRebootRequested(void) { return gRebootReq; }

void wmPortalEnd(void) {
    gServer.stop();
    gDns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("# portal: AP down");
}

#endif  // WAYMESH_WIFI_CONFIG
