#pragma once

#include "elrs_eeprom.h"

// CONFIG_MAGIC is ORed with CONFIG_VERSION in the version field
#define TX_BACKPACK_CONFIG_MAGIC    (0b01U << 30)

#define TX_BACKPACK_CONFIG_VERSION      4


typedef enum {
    WIFI_SERVICE_UPDATE,
    WIFI_SERVICE_MAVLINK_TX,
} wifi_service_t;

typedef enum {
    BACKPACK_TELEM_MODE_OFF,
    BACKPACK_TELEM_MODE_ESPNOW,
    BACKPACK_TELEM_MODE_WIFI,
    BACKPACK_TELEM_MODE_BLUETOOTH,
} telem_mode_t;

#if defined(TARGET_TX_BACKPACK)

typedef struct {
    uint32_t          version;
    bool              startWiFi;
    char              ssid[33];
    char              password[65];
    uint8_t           address[6];
    wifi_service_t    wifiService;
    telem_mode_t      telemMode;
    uint16_t          mavlinkListenPort;
    uint16_t          mavlinkSendPort;
} tx_backpack_config_t;

class TxBackpackConfig
{
public:
    void Load();
    void Commit();

    // Getters
    bool     IsModified() const { return m_modified; }
    bool     GetStartWiFiOnBoot() { return m_config.startWiFi; }
    char    *GetSSID() { return m_config.ssid; }
    char    *GetPassword() { return m_config.password; }
    uint8_t *GetGroupAddress() { return m_config.address; }
    wifi_service_t GetWiFiService() { return m_config.wifiService; }
    telem_mode_t GetTelemMode() { return m_config.telemMode; }
    uint16_t GetMavlinkListenPort() const { return m_config.mavlinkListenPort; }
    uint16_t GetMavlinkSendPort() const { return m_config.mavlinkSendPort; }

    // Setters
    void SetStorageProvider(ELRS_EEPROM *eeprom);
    void SetDefaults();
    void SetStartWiFiOnBoot(bool startWifi);
    void SetSSID(const char *ssid);
    void SetPassword(const char *ssid);
    void SetGroupAddress(const uint8_t address[6]);
    void SetWiFiService(wifi_service_t service);
    void SetTelemMode(telem_mode_t mode);
    void SetMavlinkListenPort(uint16_t port);
    void SetMavlinkSendPort(uint16_t port);

private:
    tx_backpack_config_t    m_config;
    ELRS_EEPROM             *m_eeprom;
    bool                    m_modified;
};

extern TxBackpackConfig config;

#endif