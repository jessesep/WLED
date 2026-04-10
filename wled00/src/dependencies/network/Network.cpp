#include "Network.h"

// W5500 SPI Ethernet accessors (defined in network.cpp)
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET) && defined(WLED_ETH_W5500)
extern bool isW5500Connected();
extern IPAddress getW5500LocalIP();
extern IPAddress getW5500SubnetMask();
extern IPAddress getW5500GatewayIP();
extern void getW5500MAC(uint8_t *mac);
#endif

IPAddress NetworkClass::localIP()
{
  IPAddress localIP;
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  #if defined(WLED_ETH_W5500)
  // Check W5500 SPI ethernet first
  localIP = getW5500LocalIP();
  if (localIP[0] != 0) {
    return localIP;
  }
  #endif
  // Then check RMII ethernet (Arduino ETH class)
  localIP = ETH.localIP();
  if (localIP[0] != 0) {
    return localIP;
  }
#endif
  localIP = WiFi.localIP();
  if (localIP[0] != 0) {
    return localIP;
  }

  return INADDR_NONE;
}

IPAddress NetworkClass::subnetMask()
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  #if defined(WLED_ETH_W5500)
  IPAddress w5500IP = getW5500LocalIP();
  if (w5500IP[0] != 0) {
    return getW5500SubnetMask();
  }
  #endif
  if (ETH.localIP()[0] != 0) {
    return ETH.subnetMask();
  }
#endif
  if (WiFi.localIP()[0] != 0) {
    return WiFi.subnetMask();
  }
  return IPAddress(255, 255, 255, 0);
}

IPAddress NetworkClass::gatewayIP()
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  #if defined(WLED_ETH_W5500)
  IPAddress w5500IP = getW5500LocalIP();
  if (w5500IP[0] != 0) {
      return getW5500GatewayIP();
  }
  #endif
  if (ETH.localIP()[0] != 0) {
      return ETH.gatewayIP();
  }
#endif
  if (WiFi.localIP()[0] != 0) {
      return WiFi.gatewayIP();
  }
  return INADDR_NONE;
}

void NetworkClass::localMAC(uint8_t* MAC)
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  #if defined(WLED_ETH_W5500)
  if (isW5500Connected()) {
    getW5500MAC(MAC);
    for (uint8_t i = 0; i < 6; i++) {
      if (MAC[i] != 0x00) return;
    }
  }
  #endif
  // ETH.macAddress(MAC); // Does not work because of missing ETHClass:: in ETH.ccp

  // Start work around
  String macString = ETH.macAddress();
  char macChar[18];
  char * octetEnd = macChar;

  strlcpy(macChar, macString.c_str(), 18);

  for (uint8_t i = 0; i < 6; i++) {
    MAC[i] = (uint8_t)strtol(octetEnd, &octetEnd, 16);
    octetEnd++;
  }
  // End work around

  for (uint8_t i = 0; i < 6; i++) {
    if (MAC[i] != 0x00) {
      return;
    }
  }
#endif
  WiFi.macAddress(MAC);
  return;
}

bool NetworkClass::isConnected()
{
  return (WiFi.localIP()[0] != 0 && WiFi.status() == WL_CONNECTED) || isEthernet();
}

bool NetworkClass::isEthernet()
{
#if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_ETHERNET)
  #if defined(WLED_ETH_W5500)
  if (isW5500Connected()) return true;
  #endif
  return (ETH.localIP()[0] != 0) && ETH.linkUp();
#endif
  return false;
}

NetworkClass Network;
