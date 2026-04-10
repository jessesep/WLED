#ifndef WLED_ETHERNET_H
#define WLED_ETHERNET_H

#include "pin_manager.h"

#ifdef WLED_USE_ETHERNET

// For ESP32, the remaining five pins are at least somewhat configurable.
// eth_address  is in range [0..31], indicates which PHY (MAC?) address should be allocated to the interface
// eth_power    is an output GPIO pin used to enable/disable the ethernet port (and/or external oscillator)
// eth_mdc      is an output GPIO pin used to provide the clock for the management data
// eth_mdio     is an input/output GPIO pin used to transfer management data
// eth_type     is the physical ethernet module's type (ETH_PHY_LAN8720, ETH_PHY_TLK110, ETH_PHY_W5500)
// eth_clk_mode defines the GPIO pin and GPIO mode for the clock signal (RMII only)
//
// For SPI-based ethernet (W5500, DM9051, KSZ8851SNL):
// eth_mosi, eth_miso, eth_sck  = SPI bus pins
// eth_cs                        = chip select
// eth_irq                       = interrupt pin
// eth_rst                       = reset pin (-1 if tied high)
typedef struct EthernetSettings {
  uint8_t        eth_address;
  int            eth_power;
  int            eth_mdc;
  int            eth_mdio;
  eth_phy_type_t eth_type;
  eth_clock_mode_t eth_clk_mode;
  // SPI-based ethernet fields (unused for RMII boards)
  int            eth_mosi;
  int            eth_miso;
  int            eth_sck;
  int            eth_cs;
  int            eth_irq;
  int            eth_rst;
} ethernet_settings;

// Helper to detect SPI-based ethernet types
inline bool isEthernetSPI(eth_phy_type_t type) {
#if CONFIG_ETH_SPI_ETHERNET_W5500
  if (type == ETH_PHY_W5500) return true;
#endif
#if CONFIG_ETH_SPI_ETHERNET_DM9051
  if (type == ETH_PHY_DM9051) return true;
#endif
#if CONFIG_ETH_SPI_ETHERNET_KSZ8851SNL
  if (type == ETH_PHY_KSZ8851) return true;
#endif
  return false;
}

extern const ethernet_settings ethernetBoards[];

#define WLED_ETH_RSVD_PINS_COUNT 6
extern const managed_pin_type esp32_nonconfigurable_ethernet_pins[WLED_ETH_RSVD_PINS_COUNT];
#endif

#endif