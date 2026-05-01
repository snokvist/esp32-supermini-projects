#include "config.h"
#include "logging.h"

#if defined(TARGET_TX_BACKPACK)

void
TxBackpackConfig::Load()
{
    m_eeprom->Get(0, m_config);
    m_modified = 0;

    // Check if version number matches
    if (m_config.version != (uint32_t)(TX_BACKPACK_CONFIG_VERSION | TX_BACKPACK_CONFIG_MAGIC))
    {
        // If not, revert to defaults for this version
        DBGLN("EEPROM version mismatch! Resetting to defaults...");
        SetDefaults();
    }
}

void
TxBackpackConfig::Commit()
{
    if (!m_modified)
    {
        // No changes
        return;
    }
    // Write the struct to eeprom
    m_eeprom->Put(0, m_config);
    m_eeprom->Commit();

    m_modified = false;
}

// Setters
void
TxBackpackConfig::SetStorageProvider(ELRS_EEPROM *eeprom)
{
    if (eeprom)
    {
        m_eeprom = eeprom;
    }
}

void
TxBackpackConfig::SetDefaults()
{
    m_config.version = TX_BACKPACK_CONFIG_VERSION | TX_BACKPACK_CONFIG_MAGIC;
    m_config.startWiFi = false;
    m_config.ssid[0] = 0;
    m_config.password[0] = 0;
    memset(m_config.address, 0, 6);
    m_config.wifiService = WIFI_SERVICE_UPDATE;
    m_config.mavlinkListenPort = 14555;  // Default MavLink listen port
    m_config.mavlinkSendPort = 14550;    // Default MavLink send port
    m_modified = true;
    Commit();
}

void
TxBackpackConfig::SetStartWiFiOnBoot(bool startWifi)
{
    m_config.startWiFi = startWifi;
    m_modified = true;
}

void
TxBackpackConfig::SetSSID(const char *ssid)
{
    strcpy(m_config.ssid, ssid);
    m_modified = true;
}

void
TxBackpackConfig::SetPassword(const char *password)
{
    strcpy(m_config.password, password);
    m_modified = true;
}

void
TxBackpackConfig::SetGroupAddress(const uint8_t address[6])
{
    memcpy(m_config.address, address, 6);
    m_modified = true;
}

void
TxBackpackConfig::SetWiFiService(wifi_service_t service)
{
    m_config.wifiService = service;
    m_modified = true;
}

void
TxBackpackConfig::SetTelemMode(telem_mode_t mode)
{
    m_config.telemMode = mode;
    m_modified = true;
}
void
TxBackpackConfig::SetMavlinkListenPort(uint16_t port)
{
    m_config.mavlinkListenPort = port;
    m_modified = true;
}

void
TxBackpackConfig::SetMavlinkSendPort(uint16_t port)
{
    m_config.mavlinkSendPort = port;
    m_modified = true;
}
#endif
