/* ESP32-C3 NVS (Preferences) backend for waymesh_config. Device-only.
 * Maps the wm_store_t KV interface 1:1 onto ESP32 Preferences. */
#include "config_nvs.h"

#include <Preferences.h>

/* esp_random(): declared via the IDF; forward-declare to avoid depending on a
 * specific header path (it moved between IDF versions). */
extern "C" uint32_t esp_random(void);

static Preferences gPrefs;
static bool gOpen = false;

static void ensure_open(void) {
  if (!gOpen) {
    gPrefs.begin("waymesh", /*readOnly=*/false); // RW NVS namespace
    gOpen = true;
  }
}

// C language linkage so these match the wm_store_t function-pointer field types
// (declared inside the header's extern "C"); avoids a C++ linkage mismatch.
extern "C" {

static int nvs_get_u32(void *, const char *key, uint32_t *out) {
  ensure_open();
  if (!gPrefs.isKey(key)) return -1;
  *out = gPrefs.getUInt(key, 0);
  return 0;
}

static int nvs_put_u32(void *, const char *key, uint32_t v) {
  ensure_open();
  return gPrefs.putUInt(key, v) == sizeof(uint32_t) ? 0 : -1;
}

static int nvs_get_blob(void *, const char *key, void *out, size_t cap,
                        size_t *out_len) {
  ensure_open();
  if (!gPrefs.isKey(key)) return -1;
  size_t len = gPrefs.getBytesLength(key);
  if (len == 0 || len > cap) return -1;
  if (gPrefs.getBytes(key, out, cap) != len) return -1;
  *out_len = len;
  return 0;
}

static int nvs_put_blob(void *, const char *key, const void *data, size_t len) {
  ensure_open();
  return gPrefs.putBytes(key, data, len) == len ? 0 : -1;
}

static uint32_t nvs_rng(void *) { return esp_random(); }

} // extern "C"

wm_store_t wm_nvs_store(void) {
  ensure_open();
  wm_store_t s = {nvs_get_u32, nvs_put_u32, nvs_get_blob, nvs_put_blob,
                  nvs_rng, nullptr};
  return s;
}
