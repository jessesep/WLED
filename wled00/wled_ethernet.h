#ifndef WLED_ETHERNET_H
#define WLED_ETHERNET_H

#include "pin_manager.h"

#ifdef WLED_USE_ETHERNET

// For ESP32, the remaining five pins are at least somewhat configurable.
// eth_address  is in range [0..31], indicates which PHY (MAC?) address should be allocated to the interface
// eth_power    is an output GPIO pin used to enable/disable the ethernet port (and/or external oscillator)
// eth_mdc      is an output GPIO pin used to provide the clock for the management data
// eth_mdio     is an input/output GPIO pin used to transfer management data
// eth_type     is the physical ethernet module's type (ETH_PHY_LAN8720, ETH_PHY_TLK110)
// eth_clk_mode defines the GPIO pin and GPIO mode for the clock signal
//              However, there are really only four configurable options on ESP32:
//              ETH_CLOCK_GPIO0_IN    == External oscillator, clock input  via GPIO0
//              ETH_CLOCK_GPIO0_OUT   == ESP32 provides 50MHz clock output via GPIO0
//              ETH_CLOCK_GPIO16_OUT  == ESP32 provides 50MHz clock output via GPIO16
//              ETH_CLOCK_GPIO17_OUT  == ESP32 provides 50MHz clock output via GPIO17
typedef struct EthernetSettings {
  uint8_t        eth_address;
  int            eth_power;
  int            eth_mdc;
  int            eth_mdio;
  eth_phy_type_t eth_type;
  eth_clock_mode_t eth_clk_mode;
} ethernet_settings;

extern const ethernet_settings ethernetBoards[];

#define WLED_ETH_RSVD_PINS_COUNT 6
extern const managed_pin_type esp32_nonconfigurable_ethernet_pins[WLED_ETH_RSVD_PINS_COUNT];

// W5500 SPI Ethernet support
// Uses the ESP-IDF SPI ethernet driver (not the Arduino ETH class RMII path)
// Configurable via build flags:
//   -D WLED_ETH_W5500_MOSI=23
//   -D WLED_ETH_W5500_MISO=19
//   -D WLED_ETH_W5500_SCK=18
//   -D WLED_ETH_W5500_CS=5
//   -D WLED_ETH_W5500_INT=4
//   -D WLED_ETH_W5500_RST=-1   (set to -1 if RST is tied high on PCB)
#ifdef WLED_ETH_W5500

#ifndef WLED_ETH_W5500_MOSI
  #define WLED_ETH_W5500_MOSI 23
#endif
#ifndef WLED_ETH_W5500_MISO
  #define WLED_ETH_W5500_MISO 19
#endif
#ifndef WLED_ETH_W5500_SCK
  #define WLED_ETH_W5500_SCK 18
#endif
#ifndef WLED_ETH_W5500_CS
  #define WLED_ETH_W5500_CS 5
#endif
#ifndef WLED_ETH_W5500_INT
  #define WLED_ETH_W5500_INT 4
#endif
#ifndef WLED_ETH_W5500_RST
  #define WLED_ETH_W5500_RST -1
#endif
#ifndef WLED_ETH_W5500_SPI_CLOCK_MHZ
  #define WLED_ETH_W5500_SPI_CLOCK_MHZ 25
#endif

bool initW5500Ethernet();

// W5500 SPI pins reserved count: MOSI, MISO, SCK, CS, INT (+ RST if >= 0)
#if WLED_ETH_W5500_RST >= 0
  #define WLED_ETH_W5500_RSVD_PINS_COUNT 6
#else
  #define WLED_ETH_W5500_RSVD_PINS_COUNT 5
#endif

#endif // WLED_ETH_W5500

#endif // WLED_USE_ETHERNET

#endif // WLED_ETHERNET_H
