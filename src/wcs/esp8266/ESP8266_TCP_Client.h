/**
 * 
 * The Network Upgradable ESP8266 Secure TCP Client Class, ESP8266_TCP_Client.h v1.0.0
 * 
 * The MIT License (MIT)
 * Copyright (c) 2021 K. Suwatchai (Mobizt)
 * 
 * 
 * Permission is hereby granted, free of charge, to any person returning a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef ESP8266_TCP_Client_H
#define ESP8266_TCP_Client_H

#ifdef ESP8266

//ARDUINO_ESP8266_GIT_VER
//2.6.2 0xbc204a9b
//2.6.1 0x482516e3
//2.6.0 0x643ec203
//2.5.2 0x8b899c12
//2.5.1 0xac02aff5
//2.5.0 0x951aeffa
//2.5.0-beta3 0x21db8fc9
//2.5.0-beta2 0x0fd86a07
//2.5.0-beta1 0x9c1e03a1
//2.4.2 0xbb28d4a3
//2.4.1 0x614f7c32
//2.4.0 0x4ceabea9
//2.4.0-rc2 0x0c897c37
//2.4.0-rc1 0xf6d232f1

#include <Arduino.h>
#include <core_version.h>
#include <time.h>
#include <string>

#include "extras/SDK_Version_Common.h"

#ifndef ARDUINO_ESP8266_GIT_VER
#error Your ESP8266 Arduino Core SDK is outdated, please update. From Arduino IDE go to Boards Manager and search 'esp8266' then select the latest version.
#endif

#include <WiFiClient.h>

#if ARDUINO_ESP8266_GIT_VER != 0xf6d232f1 && ARDUINO_ESP8266_GIT_VER != 0x0c897c37 && ARDUINO_ESP8266_GIT_VER != 0x4ceabea9 && ARDUINO_ESP8266_GIT_VER != 0x614f7c32 && ARDUINO_ESP8266_GIT_VER != 0xbb28d4a3
#include "ESP8266_WCS.h"
#define ESP_MAIL_SSL_CLIENT ESP8266_WCS
#else
#error Please update the ESP8266 Arduino Core SDK to latest version.
#endif

#if defined __has_include

#if __has_include(<LwipIntfDev.h>)
#include <LwipIntfDev.h>
#endif

#if __has_include(<ENC28J60lwIP.h>)
#define INC_ENC28J60_LWIP
#include <ENC28J60lwIP.h>
#endif

#if __has_include(<W5100lwIP.h>)
#define INC_W5100_LWIP
#include <W5100lwIP.h>
#endif

#if __has_include(<W5500lwIP.h>)
#define INC_W5500_LWIP
#include <W5500lwIP.h>
#endif

#endif

#define FS_NO_GLOBALS
#include <FS.h>
#include <SD.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define ESP_MAIL_FLASH_FS ESP_Mail_DEFAULT_FLASH_FS
#define ESP_MAIL_SD_FS ESP_Mail_DEFAULT_SD_FS

#define TCP_CLIENT_ERROR_CONNECTION_REFUSED (-1)
#define TCP_CLIENT_ERROR_SEND_DATA_FAILED (-2)
#define TCP_CLIENT_DEFAULT_TCP_TIMEOUT_SEC 30

enum esp_mail_file_storage_type
{
  esp_mail_file_storage_type_none,
  esp_mail_file_storage_type_flash,
  esp_mail_file_storage_type_sd
};

class ESP8266_TCP_Client
{

public:
  ESP8266_TCP_Client();
  ~ESP8266_TCP_Client();

  bool begin(const char *host, uint16_t port);

  bool connected(void);

  int send(const char *data);

  ESP8266_WCS *stream(void);

  void setCACert(const char *caCert);
  void setCertFile(const char *caCertFile, esp_mail_file_storage_type storageType, uint8_t sdPin);
  bool connect(bool secured, bool verify);

  int _certType = -1;
  MBSTRING _caCertFile = "";
  esp_mail_file_storage_type _caCertFileStoreageType = esp_mail_file_storage_type::esp_mail_file_storage_type_none;
  uint16_t tcpTimeout = 40000;

  uint8_t _sdPin = 15;
  bool _clockReady = false;
  uint16_t _bsslRxSize = 1024;
  uint16_t _bsslTxSize = 1024;
  bool fragmentable = false;
  int chunkSize = 1024;
  int maxRXBufSize = 16384; //SSL full supported 16 kB
  int maxTXBufSize = 16384;
  bool mflnChecked = false;
  int rxBufDivider = maxRXBufSize / chunkSize;
  int txBufDivider = maxRXBufSize / chunkSize;


private:
  std::unique_ptr<ESP_MAIL_SSL_CLIENT> _wcs = std::unique_ptr<ESP_MAIL_SSL_CLIENT>(new ESP_MAIL_SSL_CLIENT());
  std::unique_ptr<char> _cacert;
  MBSTRING _host = "";
  uint16_t _port = 0;
#ifndef USING_AXTLS
  X509List *x509 = nullptr;
#endif
};

#endif /* ESP8266 */

#endif /* ESP8266_TCP_Client_H */