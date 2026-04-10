#include "wled.h"
#include "fcn_declare.h"
#include "wled_ethernet.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET) && defined(WLED_ETH_W5500)
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

// W5500 SPI Ethernet globals
static esp_netif_t *w5500_netif = nullptr;
static esp_eth_handle_t w5500_eth_handle = nullptr;
static bool w5500_link_up = false;
static bool w5500_got_ip = false;

// Event handler for W5500 ethernet events
static void w5500_eth_event_handler(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data)
{
  if (event_base == ETH_EVENT) {
    switch (event_id) {
      case ETHERNET_EVENT_CONNECTED:
        DEBUG_PRINTLN(F("W5500-E: Link Up"));
        w5500_link_up = true;
        if (!apActive) {
          WiFi.disconnect(true);
        }
        showWelcomePage = false;
        break;
      case ETHERNET_EVENT_DISCONNECTED:
        DEBUG_PRINTLN(F("W5500-E: Link Down"));
        w5500_link_up = false;
        w5500_got_ip = false;
        forceReconnect = true;
        break;
      case ETHERNET_EVENT_START:
        DEBUG_PRINTLN(F("W5500-E: Started"));
        break;
      case ETHERNET_EVENT_STOP:
        DEBUG_PRINTLN(F("W5500-E: Stopped"));
        w5500_link_up = false;
        w5500_got_ip = false;
        break;
      default:
        break;
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    DEBUG_PRINTF_P(PSTR("W5500-E: Got IP: " IPSTR "\n"), IP2STR(&event->ip_info.ip));
    w5500_got_ip = true;
  }
}

// Accessors for W5500 state - used by Network class
bool isW5500Connected() {
  return w5500_link_up && w5500_got_ip;
}

IPAddress getW5500LocalIP() {
  if (!w5500_netif) return IPAddress((uint32_t)0);
  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(w5500_netif, &ip_info) == ESP_OK) {
    return IPAddress(ip_info.ip.addr);
  }
  return IPAddress((uint32_t)0);
}

IPAddress getW5500SubnetMask() {
  if (!w5500_netif) return IPAddress(255, 255, 255, 0);
  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(w5500_netif, &ip_info) == ESP_OK) {
    return IPAddress(ip_info.netmask.addr);
  }
  return IPAddress(255, 255, 255, 0);
}

IPAddress getW5500GatewayIP() {
  if (!w5500_netif) return INADDR_NONE;
  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(w5500_netif, &ip_info) == ESP_OK) {
    return IPAddress(ip_info.gw.addr);
  }
  return INADDR_NONE;
}

void getW5500MAC(uint8_t *mac) {
  if (w5500_netif) {
    esp_netif_get_mac(w5500_netif, mac);
  } else {
    memset(mac, 0, 6);
  }
}

bool initW5500Ethernet()
{
  static bool w5500_initialized = false;
  if (w5500_initialized) return false;

  DEBUG_PRINTLN(F("W5500: Initializing SPI Ethernet..."));

  // Ensure the TCP/IP stack and event loop are initialized
  // (normally done by WiFi, but might not be if WiFi is disabled)
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { // ESP_ERR_INVALID_STATE means already initialized
    DEBUG_PRINTF_P(PSTR("W5500: esp_netif_init failed: %d\n"), err);
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { // already created is fine
    DEBUG_PRINTF_P(PSTR("W5500: event loop create failed: %d\n"), err);
    return false;
  }

  // Allocate SPI pins via WLED pin manager
  // RST pin is only included if it is a valid GPIO (>= 0)
  managed_pin_type w5500_pins[WLED_ETH_W5500_RSVD_PINS_COUNT] = {
    { (int8_t)WLED_ETH_W5500_MOSI, true  },
    { (int8_t)WLED_ETH_W5500_MISO, false },
    { (int8_t)WLED_ETH_W5500_SCK,  true  },
    { (int8_t)WLED_ETH_W5500_CS,   true  },
    { (int8_t)WLED_ETH_W5500_INT,  false },
#if WLED_ETH_W5500_RST >= 0
    { (int8_t)WLED_ETH_W5500_RST,  true  },
#endif
  };

  if (!PinManager::allocateMultiplePins(w5500_pins, WLED_ETH_W5500_RSVD_PINS_COUNT, PinOwner::Ethernet)) {
    DEBUG_PRINTLN(F("W5500: Failed to allocate SPI pins"));
    return false;
  }

  // Hardware reset the W5500 if RST pin is valid (tied high on PCB = no reset needed)
#if WLED_ETH_W5500_RST >= 0
  gpio_set_direction((gpio_num_t)WLED_ETH_W5500_RST, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)WLED_ETH_W5500_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level((gpio_num_t)WLED_ETH_W5500_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
#endif

  // Initialize SPI bus
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = WLED_ETH_W5500_MOSI;
  buscfg.miso_io_num = WLED_ETH_W5500_MISO;
  buscfg.sclk_io_num = WLED_ETH_W5500_SCK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;

  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    DEBUG_PRINTF_P(PSTR("W5500: SPI bus init failed: %d\n"), ret);
    for (auto &mpt : w5500_pins) PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    return false;
  }

  // Add W5500 SPI device
  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits = 16;  // W5500 address phase
  devcfg.address_bits = 8;   // W5500 control phase
  devcfg.mode = 0;
  devcfg.clock_speed_hz = WLED_ETH_W5500_SPI_CLOCK_MHZ * 1000 * 1000;
  devcfg.spics_io_num = WLED_ETH_W5500_CS;
  devcfg.queue_size = 20;

  spi_device_handle_t spi_handle = nullptr;
  ret = spi_bus_add_device(SPI3_HOST, &devcfg, &spi_handle);
  if (ret != ESP_OK) {
    DEBUG_PRINTF_P(PSTR("W5500: SPI device add failed: %d\n"), ret);
    spi_bus_free(SPI3_HOST);
    for (auto &mpt : w5500_pins) PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    return false;
  }

  // W5500 MAC driver config
  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
  w5500_config.int_gpio_num = (gpio_num_t)WLED_ETH_W5500_INT;

  // MAC config
  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  if (!mac) {
    DEBUG_PRINTLN(F("W5500: MAC creation failed"));
    spi_bus_remove_device(spi_handle);
    spi_bus_free(SPI3_HOST);
    for (auto &mpt : w5500_pins) PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    return false;
  }

  // PHY config
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;
  phy_config.reset_gpio_num = -1; // already reset via GPIO above
  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (!phy) {
    DEBUG_PRINTLN(F("W5500: PHY creation failed"));
    mac->del(mac);
    spi_bus_remove_device(spi_handle);
    spi_bus_free(SPI3_HOST);
    for (auto &mpt : w5500_pins) PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    return false;
  }

  // Install Ethernet driver
  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  ret = esp_eth_driver_install(&eth_config, &w5500_eth_handle);
  if (ret != ESP_OK) {
    DEBUG_PRINTF_P(PSTR("W5500: Driver install failed: %d\n"), ret);
    phy->del(phy);
    mac->del(mac);
    spi_bus_remove_device(spi_handle);
    spi_bus_free(SPI3_HOST);
    for (auto &mpt : w5500_pins) PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    return false;
  }

  // Create netif for W5500
  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
  w5500_netif = esp_netif_new(&netif_config);
  if (!w5500_netif) {
    DEBUG_PRINTLN(F("W5500: netif creation failed"));
    esp_eth_driver_uninstall(w5500_eth_handle);
    spi_bus_remove_device(spi_handle);
    spi_bus_free(SPI3_HOST);
    for (auto &mpt : w5500_pins) PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    return false;
  }

  // Set hostname
  char hostname[64] = {'\0'};
  getWLEDhostname(hostname, sizeof(hostname), true);
  esp_netif_set_hostname(w5500_netif, hostname);

  // Attach driver to netif
  esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(w5500_eth_handle);
  ret = esp_netif_attach(w5500_netif, glue);
  if (ret != ESP_OK) {
    DEBUG_PRINTF_P(PSTR("W5500: netif attach failed: %d\n"), ret);
    esp_netif_destroy(w5500_netif);
    w5500_netif = nullptr;
    esp_eth_driver_uninstall(w5500_eth_handle);
    spi_bus_remove_device(spi_handle);
    spi_bus_free(SPI3_HOST);
    for (auto &mpt : w5500_pins) PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    return false;
  }

  // Register event handlers
  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &w5500_eth_event_handler, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &w5500_eth_event_handler, NULL);

  // Configure static IP if set, otherwise DHCP (default)
  if (multiWiFi[0].staticIP != (uint32_t)0x00000000 && multiWiFi[0].staticGW != (uint32_t)0x00000000) {
    esp_netif_dhcpc_stop(w5500_netif);
    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = (uint32_t)multiWiFi[0].staticIP;
    ip_info.gw.addr = (uint32_t)multiWiFi[0].staticGW;
    ip_info.netmask.addr = (uint32_t)multiWiFi[0].staticSN;
    esp_netif_set_ip_info(w5500_netif, &ip_info);

    // Set DNS
    esp_netif_dns_info_t dns;
    dns.ip.u_addr.ip4.addr = (uint32_t)dnsAddress;
    dns.ip.type = IPADDR_TYPE_V4;
    esp_netif_set_dns_info(w5500_netif, ESP_NETIF_DNS_MAIN, &dns);
  }

  // Start Ethernet
  ret = esp_eth_start(w5500_eth_handle);
  if (ret != ESP_OK) {
    DEBUG_PRINTF_P(PSTR("W5500: Start failed: %d\n"), ret);
    return false;
  }

  w5500_initialized = true;
  DEBUG_PRINTLN(F("W5500: *** SPI Ethernet successfully initialized! ***"));
  return true;
}

#endif // WLED_ETH_W5500

#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
// The following six pins are neither configurable nor
// can they be re-assigned through IOMUX / GPIO matrix.
// See https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-ethernet-kit-v1.1.html#ip101gri-phy-interface
const managed_pin_type esp32_nonconfigurable_ethernet_pins[WLED_ETH_RSVD_PINS_COUNT] = {
    { 21, true  }, // RMII EMAC TX EN  == When high, clocks the data on TXD0 and TXD1 to transmitter
    { 19, true  }, // RMII EMAC TXD0   == First bit of transmitted data
    { 22, true  }, // RMII EMAC TXD1   == Second bit of transmitted data
    { 25, false }, // RMII EMAC RXD0   == First bit of received data
    { 26, false }, // RMII EMAC RXD1   == Second bit of received data
    { 27, true  }, // RMII EMAC CRS_DV == Carrier Sense and RX Data Valid
};

const ethernet_settings ethernetBoards[] = {
  // None
  {
  },

  // WT32-EHT01
  // Please note, from my testing only these pins work for LED outputs:
  //   IO2, IO4, IO12, IO14, IO15
  // These pins do not appear to work from my testing:
  //   IO35, IO36, IO39
  {
    1,                    // eth_address,
    16,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_IN    // eth_clk_mode
  },

  // ESP32-POE
  {
     0,                   // eth_address,
    12,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT  // eth_clk_mode
  },

   // WESP32
  {
    0,			              // eth_address,
    -1,			              // eth_power,
    16,			              // eth_mdc,
    17,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_IN	  // eth_clk_mode
  },

  // QuinLed-ESP32-Ethernet
  {
    0,			              // eth_address,
    5,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // TwilightLord-ESP32 Ethernet Shield
  {
    0,			              // eth_address,
    5,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // ESP3DEUXQuattro
  {
    1,                    // eth_address,
    -1,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT  // eth_clk_mode
  },

  // ESP32-ETHERNET-KIT-VE
  {
    0,                    // eth_address,
    5,                    // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_IP101,        // eth_type,
    ETH_CLOCK_GPIO0_IN    // eth_clk_mode
  },

  // QuinLed-Dig-Octa Brainboard-32-8L and LilyGO-T-ETH-POE
  {
    0,			              // eth_address,
    -1,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // ABC! WLED Controller V43 + Ethernet Shield & compatible
  {
    1,                    // eth_address, 
    5,                    // eth_power, 
    23,                   // eth_mdc, 
    33,                   // eth_mdio, 
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT	// eth_clk_mode
  },

  // Serg74-ESP32 Ethernet Shield
  {
    1,                    // eth_address,
    5,                    // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO17_OUT  // eth_clk_mode
  },

  // ESP32-POE-WROVER
  {
    0,                    // eth_address,
    12,                   // eth_power,
    23,                   // eth_mdc,
    18,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_OUT   // eth_clk_mode
  },
  
  // LILYGO T-POE Pro
  // https://github.com/Xinyuan-LilyGO/LilyGO-T-ETH-Series/blob/master/schematic/T-POE-PRO.pdf
  {
    0,			              // eth_address,
    5,			              // eth_power,
    23,			              // eth_mdc,
    18,			              // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_OUT	// eth_clk_mode
  },

 // Gledopto Series With Ethernet
 {
    1,                    // eth_address,
    5,                    // eth_power,
    23,                   // eth_mdc,
    33,                   // eth_mdio,
    ETH_PHY_LAN8720,      // eth_type,
    ETH_CLOCK_GPIO0_IN	 // eth_clk_mode
  },

  // W5500 SPI Ethernet (placeholder - actual init uses separate SPI path)
  // Pins are configured via WLED_ETH_W5500_* build flags, not via this struct
  {
  },
};

bool initEthernet()
{
  static bool successfullyConfiguredEthernet = false;

  if (successfullyConfiguredEthernet) {
    // DEBUG_PRINTLN(F("initE: ETH already successfully configured, ignoring"));
    return false;
  }
  if (ethernetType == WLED_ETH_NONE) {
    return false;
  }
  if (ethernetType >= WLED_NUM_ETH_TYPES) {
    DEBUG_PRINTF_P(PSTR("initE: Ignoring attempt for invalid ethernetType (%d)\n"), ethernetType);
    return false;
  }

  DEBUG_PRINTF_P(PSTR("initE: Attempting ETH config: %d\n"), ethernetType);

  // W5500 SPI Ethernet - separate init path (not RMII)
  #ifdef WLED_ETH_W5500
  if (ethernetType == WLED_ETH_W5500_SPI) {
    successfullyConfiguredEthernet = initW5500Ethernet();
    return successfullyConfiguredEthernet;
  }
  #endif

  // Ethernet initialization should only succeed once -- else reboot required
  ethernet_settings es = ethernetBoards[ethernetType];
  managed_pin_type pinsToAllocate[10] = {
    // first six pins are non-configurable
    esp32_nonconfigurable_ethernet_pins[0],
    esp32_nonconfigurable_ethernet_pins[1],
    esp32_nonconfigurable_ethernet_pins[2],
    esp32_nonconfigurable_ethernet_pins[3],
    esp32_nonconfigurable_ethernet_pins[4],
    esp32_nonconfigurable_ethernet_pins[5],
    { (int8_t)es.eth_mdc,   true },  // [6] = MDC  is output and mandatory
    { (int8_t)es.eth_mdio,  true },  // [7] = MDIO is bidirectional and mandatory
    { (int8_t)es.eth_power, true },  // [8] = optional pin, not all boards use
    { ((int8_t)0xFE),       false }, // [9] = replaced with eth_clk_mode, mandatory
  };
  // update the clock pin....
  if (es.eth_clk_mode == ETH_CLOCK_GPIO0_IN) {
    pinsToAllocate[9].pin = 0;
    pinsToAllocate[9].isOutput = false;
  } else if (es.eth_clk_mode == ETH_CLOCK_GPIO0_OUT) {
    pinsToAllocate[9].pin = 0;
    pinsToAllocate[9].isOutput = true;
  } else if (es.eth_clk_mode == ETH_CLOCK_GPIO16_OUT) {
    pinsToAllocate[9].pin = 16;
    pinsToAllocate[9].isOutput = true;
  } else if (es.eth_clk_mode == ETH_CLOCK_GPIO17_OUT) {
    pinsToAllocate[9].pin = 17;
    pinsToAllocate[9].isOutput = true;
  } else {
    DEBUG_PRINTF_P(PSTR("initE: Failing due to invalid eth_clk_mode (%d)\n"), es.eth_clk_mode);
    return false;
  }

  if (!PinManager::allocateMultiplePins(pinsToAllocate, 10, PinOwner::Ethernet)) {
    DEBUG_PRINTLN(F("initE: Failed to allocate ethernet pins"));
    return false;
  }

  /*
  For LAN8720 the most correct way is to perform clean reset each time before init
  applying LOW to power or nRST pin for at least 100 us (please refer to datasheet, page 59)
  ESP_IDF > V4 implements it (150 us, lan87xx_reset_hw(esp_eth_phy_t *phy) function in 
  /components/esp_eth/src/esp_eth_phy_lan87xx.c, line 280)
  but ESP_IDF < V4 does not. Lets do it:
  [not always needed, might be relevant in some EMI situations at startup and for hot resets]
  */
  #if ESP_IDF_VERSION_MAJOR==3
  if(es.eth_power>0 && es.eth_type==ETH_PHY_LAN8720) {
    pinMode(es.eth_power, OUTPUT);
    digitalWrite(es.eth_power, 0);
    delayMicroseconds(150);
    digitalWrite(es.eth_power, 1);
    delayMicroseconds(10);
  }
  #endif

  if (!ETH.begin(
                (uint8_t) es.eth_address,
                (int)     es.eth_power,
                (int)     es.eth_mdc,
                (int)     es.eth_mdio,
                (eth_phy_type_t)   es.eth_type,
                (eth_clock_mode_t) es.eth_clk_mode
                )) {
    DEBUG_PRINTLN(F("initE: ETH.begin() failed"));
    // de-allocate the allocated pins
    for (managed_pin_type mpt : pinsToAllocate) {
      PinManager::deallocatePin(mpt.pin, PinOwner::Ethernet);
    }
    return false;
  }

  // https://github.com/wled/WLED/issues/5247
  if (multiWiFi[0].staticIP != (uint32_t)0x00000000 && multiWiFi[0].staticGW != (uint32_t)0x00000000) {
    ETH.config(multiWiFi[0].staticIP, multiWiFi[0].staticGW, multiWiFi[0].staticSN, dnsAddress);
  } else {
    ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  successfullyConfiguredEthernet = true;
  DEBUG_PRINTLN(F("initE: *** Ethernet successfully configured! ***"));
  return true;
}
#endif


//by https://github.com/tzapu/WiFiManager/blob/master/WiFiManager.cpp
int getSignalQuality(int rssi)
{
    int quality = 0;

    if (rssi <= -100)
    {
        quality = 0;
    }
    else if (rssi >= -50)
    {
        quality = 100;
    }
    else
    {
        quality = 2 * (rssi + 100);
    }
    return quality;
}


void fillMAC2Str(char *str, const uint8_t *mac) {
  sprintf_P(str, PSTR("%02x%02x%02x%02x%02x%02x"), MAC2STR(mac));
  byte nul = 0;
  for (int i = 0; i < 6; i++) nul |= *mac++;  // do we have 0
  if (!nul) str[0] = '\0';                    // empty string
}

void fillStr2MAC(uint8_t *mac, const char *str) {
  for (int i = 0; i < 6; i++) *mac++ = 0;     // clear
  if (!str) return;                           // null string
  uint64_t MAC = strtoull(str, nullptr, 16);
  for (int i = 0; i < 6; i++) { *--mac = MAC & 0xFF; MAC >>= 8; }
}


// performs asynchronous scan for available networks (which may take couple of seconds to finish)
// returns configured WiFi ID with the strongest signal (or default if no configured networks available)
int findWiFi(bool doScan) {
  if (multiWiFi.size() <= 1) {
    DEBUG_PRINTF_P(PSTR("WiFi: Default SSID (%s) used.\n"), multiWiFi[0].clientSSID);
    return 0;
  }

  int status = WiFi.scanComplete(); // complete scan may take as much as several seconds (usually <6s with not very crowded air)

  if (doScan || status == WIFI_SCAN_FAILED) {
    DEBUG_PRINTF_P(PSTR("WiFi: Scan started. @ %lus\n"), millis()/1000);
    WiFi.scanNetworks(true);  // start scanning in asynchronous mode (will delete old scan)
  } else if (status >= 0) {   // status contains number of found networks (including duplicate SSIDs with different BSSID)
    DEBUG_PRINTF_P(PSTR("WiFi: Found %d SSIDs. @ %lus\n"), status, millis()/1000);
    int rssi = -9999;
    int selected = selectedWiFi;
    for (int o = 0; o < status; o++) {
      DEBUG_PRINTF_P(PSTR(" SSID: %s (BSSID: %s) RSSI: %ddB\n"), WiFi.SSID(o).c_str(), WiFi.BSSIDstr(o).c_str(), WiFi.RSSI(o));
      for (unsigned n = 0; n < multiWiFi.size(); n++)
        if (!strcmp(WiFi.SSID(o).c_str(), multiWiFi[n].clientSSID)) {
          bool foundBSSID = memcmp(multiWiFi[n].bssid, WiFi.BSSID(o), 6) == 0;
          // find the WiFi with the strongest signal (but keep priority of entry if signal difference is not big)
          if (foundBSSID || (n < selected && WiFi.RSSI(o) > rssi-10) || WiFi.RSSI(o) > rssi) {
            rssi = foundBSSID ? 0 : WiFi.RSSI(o); // RSSI is only ever negative
            selected = n;
          }
          break;
        }
    }
    DEBUG_PRINTF_P(PSTR("WiFi: Selected SSID: %s RSSI: %ddB\n"), multiWiFi[selected].clientSSID, rssi);
    return selected;
  }
  //DEBUG_PRINT(F("WiFi scan running."));
  return status; // scan is still running or there was an error
}


bool isWiFiConfigured() {
  return multiWiFi.size() > 1 || (strlen(multiWiFi[0].clientSSID) >= 1 && strcmp_P(multiWiFi[0].clientSSID, PSTR(DEFAULT_CLIENT_SSID)) != 0);
}

#if defined(ESP8266)
  #define ARDUINO_EVENT_WIFI_AP_STADISCONNECTED WIFI_EVENT_SOFTAPMODE_STADISCONNECTED
  #define ARDUINO_EVENT_WIFI_AP_STACONNECTED    WIFI_EVENT_SOFTAPMODE_STACONNECTED
  #define ARDUINO_EVENT_WIFI_STA_GOT_IP         WIFI_EVENT_STAMODE_GOT_IP
  #define ARDUINO_EVENT_WIFI_STA_CONNECTED      WIFI_EVENT_STAMODE_CONNECTED
  #define ARDUINO_EVENT_WIFI_STA_DISCONNECTED   WIFI_EVENT_STAMODE_DISCONNECTED
#elif defined(ARDUINO_ARCH_ESP32) && !defined(ESP_ARDUINO_VERSION_MAJOR) //ESP_IDF_VERSION_MAJOR==3
  // not strictly IDF v3 but Arduino core related
  #define ARDUINO_EVENT_WIFI_AP_STADISCONNECTED SYSTEM_EVENT_AP_STADISCONNECTED
  #define ARDUINO_EVENT_WIFI_AP_STACONNECTED    SYSTEM_EVENT_AP_STACONNECTED
  #define ARDUINO_EVENT_WIFI_STA_GOT_IP         SYSTEM_EVENT_STA_GOT_IP
  #define ARDUINO_EVENT_WIFI_STA_CONNECTED      SYSTEM_EVENT_STA_CONNECTED
  #define ARDUINO_EVENT_WIFI_STA_DISCONNECTED   SYSTEM_EVENT_STA_DISCONNECTED
  #define ARDUINO_EVENT_WIFI_AP_START           SYSTEM_EVENT_AP_START
  #define ARDUINO_EVENT_WIFI_AP_STOP            SYSTEM_EVENT_AP_STOP
  #define ARDUINO_EVENT_WIFI_SCAN_DONE          SYSTEM_EVENT_SCAN_DONE
  #define ARDUINO_EVENT_ETH_START               SYSTEM_EVENT_ETH_START
  #define ARDUINO_EVENT_ETH_CONNECTED           SYSTEM_EVENT_ETH_CONNECTED
  #define ARDUINO_EVENT_ETH_DISCONNECTED        SYSTEM_EVENT_ETH_DISCONNECTED
#endif

#if defined(ARDUINO_ARCH_ESP32) && defined(LWIP_IPV6)
#include "lwip/raw.h"
#include "lwip/icmp6.h"
// This is a terrible workaround for a terrible bug: on ESP32 platforms, unsolicited IPv6 router
// advertisements will cause LwIP to overwrite the IPv4 DNS servers with the IPv6 DNS servers
// mentioned in the RA packet.  As a workaround, we just blackhole those packets using the raw
// callback, since we don't yet support IPv6.
//
// This may have been improved in IDF v5 -- Espressif has added a feature to store DNS servers
// on a per interface basis in their LwIP fork.
//
// References:
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/lwip.html (see the "Note" block under "Adapted APIs" -- though it very much undersells the problem.)
// https://github.com/espressif/arduino-esp32/discussions/9988 - links to older discussions
static u8_t blockRouterAdvertisements(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
  // ICMPv6 type is the first byte of the payload, so we skip the header
  if (p->len > 0 && (pbuf_get_at(p, sizeof(struct ip6_hdr)) == ICMP6_TYPE_RA)) {
    pbuf_free(p);
    return 1; // claim the packet — lwIP will not pass it further
  }
  return 0; // not consumed, pass it on
}

void installIPv6RABlocker() {
  struct raw_pcb* ra_blocker = raw_new_ip_type(IPADDR_TYPE_V6, IP6_NEXTH_ICMP6);
  raw_recv(ra_blocker, blockRouterAdvertisements, NULL);
}
#endif

//handle Ethernet connection event
void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      // AP client disconnected
      if (--apClients == 0 && isWiFiConfigured()) forceReconnect = true; // no clients reconnect WiFi if awailable
      DEBUG_PRINTF_P(PSTR("WiFi-E: AP Client Disconnected (%d) @ %lus.\n"), (int)apClients, millis()/1000);
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      // AP client connected
      apClients++;
      DEBUG_PRINTF_P(PSTR("WiFi-E: AP Client Connected (%d) @ %lus.\n"), (int)apClients, millis()/1000);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      DEBUG_PRINT(F("WiFi-E: IP address: ")); DEBUG_PRINTLN(Network.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      // followed by IDLE and SCAN_DONE
      DEBUG_PRINTF_P(PSTR("WiFi-E: Connected! @ %lus\n"), millis()/1000);
      wasConnected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (wasConnected && interfacesInited) {
        DEBUG_PRINTF_P(PSTR("WiFi-E: Disconnected! @ %lus\n"), millis()/1000);
        if (interfacesInited && multiWiFi.size() > 1 && WiFi.scanComplete() >= 0) {
          findWiFi(true); // reinit WiFi scan
          forceReconnect = true;
        }
        interfacesInited = false;
      }
      break;
  #ifdef ARDUINO_ARCH_ESP32
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      // also triggered when connected to selected SSID
      DEBUG_PRINTLN(F("WiFi-E: SSID scan completed."));
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      DEBUG_PRINTLN(F("WiFi-E: AP Started"));
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      DEBUG_PRINTLN(F("WiFi-E: AP Stopped"));
      break;
    #if defined(WLED_USE_ETHERNET)
    case ARDUINO_EVENT_ETH_START:
      DEBUG_PRINTLN(F("ETH-E: Started"));
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      {
      DEBUG_PRINTLN(F("ETH-E: Connected"));
      if (!apActive) {
        WiFi.disconnect(true); // disable WiFi entirely
      }
      #ifdef WLED_ETH_W5500
      // W5500 hostname is set during initW5500Ethernet() via esp_netif_set_hostname()
      // Only call ETH.setHostname() for RMII ethernet boards
      if (ethernetType != WLED_ETH_W5500_SPI)
      #endif
      {
        char hostname[64] = {'\0'}; // any "hostname" within a Fully Qualified Domain Name (FQDN) must not exceed 63 characters
        getWLEDhostname(hostname, sizeof(hostname), true); // create DNS name based on mDNS name if set, or fall back to standard WLED server name
        ETH.setHostname(hostname);
      }
      showWelcomePage = false;
      break;
      }
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      DEBUG_PRINTLN(F("ETH-E: Disconnected"));
      // This doesn't really affect ethernet per se,
      // as it's only configured once.  Rather, it
      // may be necessary to reconnect the WiFi when
      // ethernet disconnects, as a way to provide
      // alternative access to the device.
      if (interfacesInited && WiFi.scanComplete() >= 0) findWiFi(true); // reinit WiFi scan
      forceReconnect = true;
      break;
    #endif
  #endif
    default:
      DEBUG_PRINTF_P(PSTR("WiFi-E: Event %d\n"), (int)event);
      break;
  }
}

