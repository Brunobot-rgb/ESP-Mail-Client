/**
 * Mail Client Arduino Library for Espressif's ESP32 and ESP8266 and SAMD21 with u-blox NINA-W102 WiFi/Bluetooth module
 * 
 *   Version:   1.5.7
 *   Released:  November 11, 2021
 *
 *   Updates:
 * - Fix IMAP folder open issue and add the timing guard for opening the same folder with the same mode.
 * 
 * 
 * This library allows Espressif's ESP32, ESP8266 and SAMD devices to send and read Email through the SMTP and IMAP servers.
 * 
 * The MIT License (MIT)
 * Copyright (c) 2021 K. Suwatchai (Mobizt)
 * 
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
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

#ifndef ESP_Mail_Client_CPP
#define ESP_Mail_Client_CPP

#include "ESP_Mail_Client.h"

#if defined(ESP32)
extern "C"
{
#include <esp_err.h>
#include <esp_wifi.h>
}
#endif

bool ESP_Mail_Client::sdBegin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss)
{
  _sck = sck;
  _miso = miso;
  _mosi = mosi;
  _ss = ss;
  _sdConfigSet = true;
#if defined(ESP32)
#if defined(CARD_TYPE_SD)
  SPI.begin(_sck, _miso, _mosi, _ss);
  return ESP_MAIL_SD_FS.begin(_ss, SPI);
#endif
#elif defined(ESP8266)
  return ESP_MAIL_SD_FS.begin(_ss);
#endif
  return false;
}

bool ESP_Mail_Client::sdBegin(void)
{
  _sdConfigSet = false;
#if defined(ESP32)
  return ESP_MAIL_SD_FS.begin();
#elif defined(ESP8266)
  return ESP_MAIL_SD_FS.begin(SD_CS_PIN);
#endif
  return false;
}

bool ESP_Mail_Client::sdMMCBegin(const char *mountpoint, bool mode1bit, bool format_if_mount_failed)
{
#if defined(ESP32)
#if defined(CARD_TYPE_SD_MMC)
  _sdConfigSet = true;
  sd_mmc_mountpoint = mountpoint;
  sd_mmc_mode1bit = mode1bit;
  sd_mmc_format_if_mount_failed = format_if_mount_failed;
  return ESP_MAIL_SD_FS.begin(mountpoint, mode1bit, format_if_mount_failed);
#endif
#endif
  return false;
}

int ESP_Mail_Client::getFreeHeap()
{
#if defined(ESP32) || defined(ESP8266)
  return ESP.getFreeHeap();
#elif defined(ARDUINO_ARCH_SAMD)
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char *>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif // __arm__
#else
  return 0;
#endif
}

#if defined(ENABLE_SMTP) || defined(ENABLE_IMAP)

bool ESP_Mail_Client::validEmail(const char *s)
{
  MBSTRING str(s);
  size_t at = str.find('@');
  size_t dot = str.find('.', at);
  return (at != MBSTRING::npos) && (dot != MBSTRING::npos);
}

void ESP_Mail_Client::debugInfoP(PGM_P info)
{
  char *tmp = strP(info);
  esp_mail_debug(tmp);
  delP(&tmp);
}

char *ESP_Mail_Client::getRandomUID()
{
  char *tmp = (char *)newP(36);
  itoa(random(10000000, 20000000), tmp, 10);
  return tmp;
}

/* Safe string splitter to avoid strsep bugs*/
void ESP_Mail_Client::splitTk(MBSTRING &str, std::vector<MBSTRING> &tk, const char *delim)
{
  std::size_t current, previous = 0;
  current = str.find(delim, previous);
  MBSTRING s;
  while (current != MBSTRING::npos)
  {
    s = str.substr(previous, current - previous);
    tk.push_back(s);
    previous = current + strlen(delim);
    current = str.find(delim, previous);
  }
  s = str.substr(previous, current - previous);
  tk.push_back(s);
  MBSTRING().swap(s);
}

void ESP_Mail_Client::strcat_c(char *str, char c)
{
  for (; *str; str++)
    ;
  *str++ = c;
  *str++ = 0;
}

int ESP_Mail_Client::strpos(const char *haystack, const char *needle, int offset, bool caseSensitive)
{
  if (!haystack || !needle)
    return -1;

  int hlen = strlen(haystack);
  int nlen = strlen(needle);

  if (hlen == 0 || nlen == 0)
    return -1;

  int hidx = offset, nidx = 0;
  while ((*(haystack + hidx) != '\0') && (*(needle + nidx) != '\0') && hidx < hlen)
  {
    bool nm = caseSensitive ? *(needle + nidx) != *(haystack + hidx) : tolower(*(needle + nidx)) != tolower(*(haystack + hidx));

    if (nm)
    {
      hidx++;
      nidx = 0;
    }
    else
    {
      nidx++;
      hidx++;
      if (nidx == nlen)
        return hidx - nidx;
    }
  }

  return -1;
}

int ESP_Mail_Client::readLine(WiFiClient *stream, char *buf, int bufLen, bool crlf, int &count)
{
  int ret = -1;
  char c = 0;
  char _c = 0;
  int idx = 0;
  if (!stream)
    return idx;
  while (stream->available() && idx < bufLen)
  {
    ret = stream->read();
    if (ret > -1)
    {
      if (idx >= bufLen - 1)
        return idx;

      c = (char)ret;
      strcat_c(buf, c);
      idx++;
      count++;
      if (_c == '\r' && c == '\n')
      {
        if (!crlf)
        {
          buf[idx - 2] = 0;
          idx -= 2;
        }
        return idx;
      }
      _c = c;
    }
    if (!stream)
      return idx;
  }
  return idx;
}

char *ESP_Mail_Client::subStr(const char *buf, PGM_P beginH, PGM_P endH, int beginPos, int endPos, bool caseSensitive)
{
  char *tmp = nullptr;
  int p1 = strposP(buf, beginH, beginPos, caseSensitive);
  if (p1 != -1)
  {

    while (buf[p1 + strlen_P(beginH)] == ' ' || buf[p1 + strlen_P(beginH)] == '\r' || buf[p1 + strlen_P(beginH)] == '\n')
    {
      p1++;
      if (strlen(buf) <= p1 + strlen_P(beginH))
      {
        p1--;
        break;
      }
    }

    int p2 = -1;
    if (endPos == 0)
      p2 = strposP(buf, endH, p1 + strlen_P(beginH), caseSensitive);

    if (p2 == -1)
      p2 = strlen(buf);

    int len = p2 - p1 - strlen_P(beginH);
    tmp = (char *)newP(len + 1);
    memcpy(tmp, &buf[p1 + strlen_P(beginH)], len);
    return tmp;
  }

  return nullptr;
}

#if defined(ESP32) || defined(ESP8266)
void ESP_Mail_Client::setSecure(TCP_CLIENT &tcpClient, ESP_Mail_Session *session, std::shared_ptr<const char> caCert)
{
  MailClient.Time.setClock(0, 0);

#if defined(ESP32)
  if (tcpClient._certType == -1)
  {
    if (strlen(session->certificate.cert_file) == 0)
    {
      if (caCert != nullptr)
        tcpClient.setCACert(caCert.get());
      else
        tcpClient.setCACert(nullptr);
    }
    else
    {
      tcpClient.setCertFile(session->certificate.cert_file, session->certificate.cert_file_storage_type);
    }
  }
#elif defined(ESP8266)

  if (tcpClient._certType == -1)
  {

    if (!MailClient._clockReady && (strlen(session->certificate.cert_file) > 0 || caCert != nullptr))
    {
      tcpClient._clockReady = MailClient._clockReady;
    }

    if (strlen(session->certificate.cert_file) == 0)
    {
      if (caCert != nullptr)
        tcpClient.setCACert(caCert.get());
      else
        tcpClient.setCACert(nullptr);
    }
    else
    {
      tcpClient.setCertFile(session->certificate.cert_file, session->certificate.cert_file_storage_type, MailClient._sdPin);
    }
  }
#endif
}
#endif

void ESP_Mail_Client::ethDNSWorkAround(ESP_Mail_Session *session)
{

#if defined(ESP8266) && defined(ESP8266_CORE_SDK_V3_X_X)

#if defined(INC_ENC28J60_LWIP)
  if (session->spi_ethernet_module.enc28j60)
    goto ex;
#endif
#if defined(INC_W5100_LWIP)
  if (session->spi_ethernet_module.w5100)
    goto ex;
#endif
#if defined(INC_W5100_LWIP)
  if (session->spi_ethernet_module.w5500)
    goto ex;
#endif

  return;

ex:
  WiFiClient client;
  client.connect(session->server.host_name, session->server.port);
  client.stop();
#endif
}

bool ESP_Mail_Client::ethLinkUp(ESP_Mail_Session *session)
{

  bool ret = false;
#if defined(ESP32)
  char *ip = strP(esp_mail_str_328);
  if (strcmp(ETH.localIP().toString().c_str(), ip) != 0)
  {
    ret = true;
    ETH.linkUp();
  }
  delP(&ip);
#elif defined(ESP8266) && defined(ESP8266_CORE_SDK_V3_X_X)

#if defined(INC_ENC28J60_LWIP)
  if (session->spi_ethernet_module.enc28j60)
  {
    ret = session->spi_ethernet_module.enc28j60->status() == WL_CONNECTED;
    goto ex;
  }
#endif
#if defined(INC_W5100_LWIP)
  if (session->spi_ethernet_module.w5100)
  {
    ret = session->spi_ethernet_module.w5100->status() == WL_CONNECTED;
    goto ex;
  }
#endif
#if defined(INC_W5100_LWIP)
  if (session->spi_ethernet_module.w5500)
  {
    ret = session->spi_ethernet_module.w5500->status() == WL_CONNECTED;
    goto ex;
  }
#endif

  return ret;

ex:
  //workaround for ESP8266 Ethernet
  delayMicroseconds(0);
#endif

  return ret;
}

void ESP_Mail_Client::delP(void *ptr)
{
  void **p = (void **)ptr;
  if (*p)
  {
    free(*p);
    *p = 0;
  }
}

size_t ESP_Mail_Client::getReservedLen(size_t len)
{
  int blen = len + 1;

  int newlen = (blen / 4) * 4;

  if (newlen < blen)
    newlen += 4;

  return (size_t)newlen;
}

void *ESP_Mail_Client::newP(size_t len)
{
  void *p;
#if defined(BOARD_HAS_PSRAM) && defined(ESP_MAIL_USE_PSRAM)

  if ((p = (void *)ps_malloc(getReservedLen(len))) == 0)
    return NULL;

#else

  if ((p = (void *)malloc(getReservedLen(len))) == 0)
    return NULL;

#endif
  memset(p, 0, len);
  return p;
}

char *ESP_Mail_Client::newS(char *p, size_t len)
{
  delP(&p);
  p = (char *)newP(len);
  return p;
}

char *ESP_Mail_Client::newS(char *p, size_t len, char *d)
{
  delP(&p);
  p = (char *)newP(len);
  strcpy(p, d);
  return p;
}

bool ESP_Mail_Client::strcmpP(const char *buf, int ofs, PGM_P beginH, bool caseSensitive)
{
  char *tmp = nullptr;
  if (ofs < 0)
  {
    int p = strposP(buf, beginH, 0, caseSensitive);
    if (p == -1)
      return false;
    ofs = p;
  }
  tmp = strP(beginH);
  char *tmp2 = (char *)newP(strlen_P(beginH) + 1);
  memcpy(tmp2, &buf[ofs], strlen_P(beginH));
  tmp2[strlen_P(beginH)] = 0;
  bool ret = (strcasecmp(tmp, tmp2) == 0);
  delP(&tmp);
  delP(&tmp2);
  return ret;
}

int ESP_Mail_Client::strposP(const char *buf, PGM_P beginH, int ofs, bool caseSensitive)
{
  char *tmp = strP(beginH);
  int p = strpos(buf, tmp, ofs, caseSensitive);
  delP(&tmp);
  return p;
}

char *ESP_Mail_Client::strP(PGM_P pgm)
{
  size_t len = strlen_P(pgm) + 1;
  char *buf = (char *)newP(len);
  strcpy_P(buf, pgm);
  buf[len - 1] = 0;
  return buf;
}

void ESP_Mail_Client::appendP(MBSTRING &buf, PGM_P p, bool empty)
{
  if (empty)
    buf.clear();
  char *b = strP(p);
  buf += b;
  delP(&b);
}

char *ESP_Mail_Client::intStr(int value)
{
  char *buf = (char *)newP(36);
  memset(buf, 0, 36);
  itoa(value, buf, 10);
  return buf;
}

char *ESP_Mail_Client::strReplace(char *orig, char *rep, char *with)
{
  char *result = nullptr;
  char *ins = nullptr;
  char *tmp = nullptr;
  int len_rep;
  int len_with;
  int len_front;
  int count;

  len_with = strlen(with);
  len_rep = strlen(rep);

  ins = orig;
  for (count = 0; (tmp = strstr(ins, rep)); ++count)
    ins = tmp + len_rep;

  tmp = result = (char *)newP(strlen(orig) + (len_with - len_rep) * count + 1);
  while (count--)
  {
    ins = strstr(orig, rep);
    len_front = ins - orig;
    tmp = strncpy(tmp, orig, len_front) + len_front;
    tmp = strcpy(tmp, with) + len_with;
    orig += len_front + len_rep;
  }
  strcpy(tmp, orig);
  return result;
}

char *ESP_Mail_Client::strReplaceP(char *buf, PGM_P name, PGM_P value)
{
  char *n = strP(name);
  char *v = strP(value);
  char *out = strReplace(buf, n, v);
  delP(&n);
  delP(&v);
  return out;
}

bool ESP_Mail_Client::authFailed(char *buf, int bufLen, int &chunkIdx, int ofs)
{
  bool ret = false;
  if (chunkIdx == 0)
  {
    size_t olen;
    unsigned char *decoded = decodeBase64((const unsigned char *)(buf + ofs), bufLen - ofs, &olen);
    if (decoded)
    {
      ret = strposP((char *)decoded, esp_mail_str_294, 0) > -1;
      delP(&decoded);
    }
    chunkIdx++;
  }
  return ret;
}

unsigned char *ESP_Mail_Client::decodeBase64(const unsigned char *src, size_t len, size_t *out_len)
{
  unsigned char *out, *pos, block[4], tmp;
  size_t i, count, olen;
  int pad = 0;
  size_t extra_pad;

  unsigned char *dtable = (unsigned char *)newP(256);

  memset(dtable, 0x80, 256);

  for (i = 0; i < sizeof(b64_index_table) - 1; i++)
    dtable[b64_index_table[i]] = (unsigned char)i;
  dtable['='] = 0;

  count = 0;
  for (i = 0; i < len; i++)
  {
    if (dtable[src[i]] != 0x80)
      count++;
  }

  if (count == 0)
    goto exit;
  extra_pad = (4 - count % 4) % 4;

  olen = (count + extra_pad) / 4 * 3;
  pos = out = (unsigned char *)malloc(olen);
  if (out == NULL)
    goto exit;

  count = 0;
  for (i = 0; i < len + extra_pad; i++)
  {
    unsigned char val;

    if (i >= len)
      val = '=';
    else
      val = src[i];
    tmp = dtable[val];
    if (tmp == 0x80)
      continue;

    if (val == '=')
      pad++;
    block[count] = tmp;
    count++;
    if (count == 4)
    {
      *pos++ = (block[0] << 2) | (block[1] >> 4);
      *pos++ = (block[1] << 4) | (block[2] >> 2);
      *pos++ = (block[2] << 6) | block[3];
      count = 0;
      if (pad)
      {
        if (pad == 1)
          pos--;
        else if (pad == 2)
          pos -= 2;
        else
        {
          free(out);
          goto exit;
        }
        break;
      }
    }
  }

  *out_len = pos - out;
  delP(&dtable);
  return out;
exit:
  delP(&dtable);
  return nullptr;
}

MBSTRING ESP_Mail_Client::encodeBase64Str(const unsigned char *src, size_t len)
{
  return encodeBase64Str((uint8_t *)src, len);
}

MBSTRING ESP_Mail_Client::encodeBase64Str(uint8_t *src, size_t len)
{
  MBSTRING outStr;
  unsigned char *out, *pos;
  const unsigned char *end, *in;
  size_t olen = 4 * ((len + 2) / 3);
  if (olen < len)
    return outStr;

  outStr.resize(olen);
  out = (unsigned char *)&outStr[0];

  end = src + len;
  in = src;
  pos = out;

  while (end - in >= 3)
  {
    *pos++ = b64_index_table[in[0] >> 2];
    *pos++ = b64_index_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
    *pos++ = b64_index_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
    *pos++ = b64_index_table[in[2] & 0x3f];
    in += 3;
  }

  if (end - in)
  {
    *pos++ = b64_index_table[in[0] >> 2];
    if (end - in == 1)
    {
      *pos++ = b64_index_table[(in[0] & 0x03) << 4];
      *pos++ = '=';
    }
    else
    {
      *pos++ = b64_index_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
      *pos++ = b64_index_table[(in[1] & 0x0f) << 2];
    }
    *pos++ = '=';
  }

  return outStr;
}

void ESP_Mail_Client::createDirs(MBSTRING dirs)
{
  MBSTRING dir = "";
  int count = 0;
  for (size_t i = 0; i < dirs.length(); i++)
  {
    dir.append(1, dirs[i]);
    count++;
    if (dirs[i] == '/')
    {
      if (dir.length() > 0)
        ESP_MAIL_SD_FS.mkdir(dir.substr(0, dir.length() - 1).c_str());
      count = 0;
    }
  }
  if (count > 0)
    ESP_MAIL_SD_FS.mkdir(dir.c_str());
  MBSTRING().swap(dir);
}

bool ESP_Mail_Client::sdTest()
{
#if defined(CARD_TYPE_SD)
  if (_sdConfigSet)
    sdBegin(_sck, _miso, _mosi, _ss);
  else
    sdBegin();
#endif

#if defined(ESP32)
#if defined(CARD_TYPE_SD_MMC)
  if (_sdConfigSet)
    sdMMCBegin(sd_mmc_mountpoint, sd_mmc_mode1bit, sd_mmc_format_if_mount_failed);
#endif
#endif

  file = ESP_MAIL_SD_FS.open(esp_mail_str_204, FILE_WRITE);
  if (!file)
    return false;

  if (!file.write(32))
    return false;
  file.close();

  file = ESP_MAIL_SD_FS.open(esp_mail_str_204);
  if (!file)
    return false;

  while (file.available())
  {
    if (file.read() != 32)
      return false;
  }
  file.close();

  ESP_MAIL_SD_FS.remove(esp_mail_str_204);

  return true;
}

#endif

#if defined(ENABLE_IMAP)

int ESP_Mail_Client::decodeChar(const char *s)
{
  return 16 * hexval(*(s + 1)) + hexval(*(s + 2));
}

void ESP_Mail_Client::decodeQP(const char *buf, char *out)
{
  char *tmp = strP(esp_mail_str_295);
  while (*buf)
  {
    if (*buf != '=')
      strcat_c(out, *buf++);
    else if (*(buf + 1) == '\r' && *(buf + 2) == '\n')
      buf += 3;
    else if (*(buf + 1) == '\n')
      buf += 2;
    else if (!strchr(tmp, *(buf + 1)))
      strcat_c(out, *buf++);
    else if (!strchr(tmp, *(buf + 2)))
      strcat_c(out, *buf++);
    else
    {
      strcat_c(out, decodeChar(buf));
      buf += 3;
    }
  }
  delP(&tmp);
}

char *ESP_Mail_Client::decode7Bit(char *buf)
{
  char *out = strReplaceP(buf, imap_7bit_key1, imap_7bit_val1);
  char *tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key2, imap_7bit_val2);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key3, imap_7bit_val3);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key4, imap_7bit_val4);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key5, imap_7bit_val5);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key6, imap_7bit_val6);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key7, imap_7bit_val7);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key8, imap_7bit_val8);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key9, imap_7bit_val9);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key10, imap_7bit_val10);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key11, imap_7bit_val11);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key12, imap_7bit_val12);

  delP(&tmp);
  tmp = (char *)newP(strlen(out) + 10);
  strcpy(tmp, out);
  delP(&out);
  out = strReplaceP(tmp, imap_7bit_key13, imap_7bit_val13);
  delP(&tmp);
  return out;
}

void ESP_Mail_Client::decodeHeader(MBSTRING &headerField)
{

  size_t p1 = 0, p2 = 0;
  MBSTRING headerEnc;

  while (headerField[p1] == ' ' && p1 < headerField.length() - 1)
    p1++;

  if (headerField[p1] == '=' && headerField[p1 + 1] == '?')
  {
    p2 = headerField.find("?", p1 + 2);
    if (p2 != MBSTRING::npos)
      headerEnc = headerField.substr(p1 + 2, p2 - p1 - 2);
  }

  int bufSize = 512;
  char *buf = (char *)newP(bufSize);

  RFC2047Decoder.rfc2047Decode(buf, headerField.c_str(), bufSize);

  if (getEncodingFromCharset(headerEnc.c_str()) == esp_mail_char_decoding_scheme_iso8859_1)
  {
    int len = strlen(buf);
    int olen = (len + 1) * 2;
    unsigned char *out = (unsigned char *)newP(olen);
    decodeLatin1_UTF8(out, &olen, (unsigned char *)buf, &len);
    delP(&buf);
    buf = (char *)out;
  }
  else if (getEncodingFromCharset(headerEnc.c_str()) == esp_mail_char_decoding_scheme_tis620)
  {
    size_t len2 = strlen(buf);
    char *tmp = (char *)newP((len2 + 1) * 3);
    decodeTIS620_UTF8(tmp, buf, len2);
    delP(&buf);
    buf = tmp;
  }

  headerField = buf;
  delP(&buf);
}

esp_mail_char_decoding_scheme ESP_Mail_Client::getEncodingFromCharset(const char *enc)
{
  esp_mail_char_decoding_scheme scheme = esp_mail_char_decoding_scheme_default;

  if (strposP(enc, esp_mail_str_237, 0) > -1 || strposP(enc, esp_mail_str_231, 0) > -1 || strposP(enc, esp_mail_str_226, 0) > -1)
    scheme = esp_mail_char_decoding_scheme_tis620;
  else if (strposP(enc, esp_mail_str_227, 0) > -1)
    scheme = esp_mail_char_decoding_scheme_iso8859_1;

  return scheme;
}

void ESP_Mail_Client::decodeTIS620_UTF8(char *out, const char *in, size_t len)
{
  //output is the 3-byte value UTF-8
  int j = 0;
  for (size_t i = 0; i < len; i++)
  {
    if (in[i] < 0x80)
      out[j++] = in[i];
    else if ((in[i] >= 0xa0 && in[i] < 0xdb) || (in[i] > 0xde && in[i] < 0xfc))
    {
      int unicode = 0x0e00 + in[i] - 0xa0;
      out[j++] = 0xe0 | ((unicode >> 12) & 0xf);
      out[j++] = 0x80 | ((unicode >> 6) & 0x3f);
      out[j++] = 0x80 | (unicode & 0x3f);
    }
  }
}

int ESP_Mail_Client::decodeLatin1_UTF8(unsigned char *out, int *outlen, const unsigned char *in, int *inlen)
{
  unsigned char *outstart = out;
  const unsigned char *base = in;
  const unsigned char *processed = in;
  unsigned char *outend = out + *outlen;
  const unsigned char *inend;
  unsigned int c;
  int bits;

  inend = in + (*inlen);
  while ((in < inend) && (out - outstart + 5 < *outlen))
  {
    c = *in++;

    /* assertion: c is a single UTF-4 value */
    if (out >= outend)
      break;
    if (c < 0x80)
    {
      *out++ = c;
      bits = -6;
    }
    else
    {
      *out++ = ((c >> 6) & 0x1F) | 0xC0;
      bits = 0;
    }

    for (; bits >= 0; bits -= 6)
    {
      if (out >= outend)
        break;
      *out++ = ((c >> bits) & 0x3F) | 0x80;
    }
    processed = (const unsigned char *)in;
  }
  *outlen = out - outstart;
  *inlen = processed - base;
  return (0);
}

bool ESP_Mail_Client::sendIMAPCommand(IMAPSession *imap, int msgIndex, int cmdCase)
{

  MBSTRING cmd;
  if (imap->_uidSearch || strlen(imap->_config->fetch.uid) > 0)
    appendP(cmd, esp_mail_str_142, true);
  else
    appendP(cmd, esp_mail_str_143, true);

  char *tmp = intStr(imap->_msgUID[msgIndex]);
  cmd += tmp;
  delP(&tmp);
  appendP(cmd, esp_mail_str_147, false);
  if (!imap->_config->fetch.set_seen)
  {
    appendP(cmd, esp_mail_str_152, false);
    appendP(cmd, esp_mail_str_214, false);
  }
  appendP(cmd, esp_mail_str_218, false);

  switch (cmdCase)
  {
  case 1:

    appendP(cmd, esp_mail_str_269, false);
    break;

  case 2:

    if (cPart(imap)->partNumFetchStr.length() > 0)
      cmd += cPart(imap)->partNumFetchStr;
    else
      appendP(cmd, esp_mail_str_215, false);
    appendP(cmd, esp_mail_str_219, false);
    break;

  case 3:

    cmd += cPart(imap)->partNumFetchStr;
    appendP(cmd, esp_mail_str_219, false);
    break;

  default:
    break;
  }

  if (imapSend(imap, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;
  return true;
}

bool ESP_Mail_Client::readMail(IMAPSession *imap, bool closeSession)
{

  imap->checkUID();
  imap->checkPath();

  if (!imap->_tcpConnected)
    imap->_mailboxOpened = false;

  MBSTRING buf;
  MBSTRING command;
  MBSTRING _uid;
  appendP(command, esp_mail_str_27, true);
  char *tmp = nullptr;
  size_t readCount = 0;
  imap->_multipart_levels.clear();

  if (!reconnect(imap))
    return false;

  int cmem = MailClient.getFreeHeap();

  if (cmem < ESP_MAIL_MIN_MEM)
  {
    if (imap->_debug)
    {
      esp_mail_debug("");
      errorStatusCB(imap, MAIL_CLIENT_ERROR_OUT_OF_MEMORY);
    }
    goto out;
  }

  //new session
  if (!imap->_tcpConnected)
  {
    //authenticate new
    if (!imapAuth(imap))
    {
      closeTCPSession(imap);
      return false;
    }
  }
  else
  {
    //reuse session
    for (size_t i = 0; i < imap->_headers.size(); i++)
      imap->_headers[i].part_headers.clear();
    imap->_headers.clear();

    if (strlen(imap->_config->fetch.uid) > 0)
      imap->_headerOnly = false;
    else
      imap->_headerOnly = true;
  }
  imap->_rfc822_part_count = 0;
  imap->_mbif._availableItems = 0;
  imap->_msgUID.clear();
  imap->_uidSearch = false;
  imap->_mbif._searchCount = 0;

  if (imap->_currentFolder.length() == 0)
    return handleIMAPError(imap, IMAP_STATUS_NO_MAILBOX_FOLDER_OPENED, false);

  if (!imap->_mailboxOpened || (imap->_config->fetch.set_seen && !imap->_headerOnly && imap->_readOnlyMode))
  {
    if (!imap->openFolder(imap->_currentFolder.c_str(), imap->_readOnlyMode && !imap->_config->fetch.set_seen))
      return handleIMAPError(imap, IMAP_STATUS_OPEN_MAILBOX_FAILED, false);
  }

  if (imap->_headerOnly)
  {
    if (strlen(imap->_config->search.criteria) > 0)
    {

      if (imap->_readCallback)
      {
        imapCB(imap, "", false);
        imapCBP(imap, esp_mail_str_66, false);
      }

      if (imap->_debug)
        debugInfoP(esp_mail_str_232);

      if (strposP(imap->_config->search.criteria, esp_mail_str_137, 0) != -1)
      {
        imap->_uidSearch = true;
        appendP(command, esp_mail_str_138, false);
      }

      appendP(command, esp_mail_str_139, false);

      for (size_t i = 0; i < strlen(imap->_config->search.criteria); i++)
      {
        if (imap->_config->search.criteria[i] != ' ' && imap->_config->search.criteria[i] != '\r' && imap->_config->search.criteria[i] != '\n' && imap->_config->search.criteria[i] != '$')
          buf.append(1, imap->_config->search.criteria[i]);

        if (imap->_config->search.criteria[i] == ' ')
        {
          tmp = strP(esp_mail_str_140);
          char *tmp2 = strP(esp_mail_str_224);

          if ((imap->_uidSearch && strcmp(buf.c_str(), tmp) == 0) || (imap->_unseen && buf.find(tmp2) != MBSTRING::npos))
            buf.clear();
          delP(&tmp);
          delP(&tmp2);

          tmp = strP(esp_mail_str_141);
          if (strcmp(buf.c_str(), tmp) != 0 && buf.length() > 0)
          {
            appendP(command, esp_mail_str_131, false);
            command += buf;
          }
          delP(&tmp);
          buf.clear();
        }
      }

      tmp = strP(esp_mail_str_223);
      if (imap->_unseen && strpos(imap->_config->search.criteria, tmp, 0) == -1)
        appendP(command, esp_mail_str_223, false);
      delP(&tmp);

      if (buf.length() > 0)
      {
        appendP(command, esp_mail_str_131, false);
        command += buf;
      }

      if (imapSend(imap, command.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      MBSTRING().swap(command);

      imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_search;

      if (!handleIMAPResponse(imap, IMAP_STATUS_BAD_COMMAND, closeSession))
        return false;

      if (imap->_readCallback)
      {
        MBSTRING s;
        appendP(s, esp_mail_str_34, true);
        appendP(s, esp_mail_str_68, false);
        char *tmp = intStr(imap->_config->limit.search);
        s += tmp;
        delP(&tmp);
        imapCB(imap, s.c_str(), false);

        if (imap->_msgUID.size() > 0)
        {

          appendP(s, esp_mail_str_69, true);
          tmp = intStr(imap->_mbif._searchCount);
          s += tmp;
          delP(&tmp);
          appendP(s, esp_mail_str_70, false);
          imapCB(imap, s.c_str(), false);

          appendP(s, esp_mail_str_71, true);
          tmp = intStr(imap->_msgUID.size());
          s += tmp;
          delP(&tmp);
          appendP(s, esp_mail_str_70, false);
          imapCB(imap, s.c_str(), false);
        }
        else
          imapCBP(imap, esp_mail_str_72, false);
      }
    }
    else
    {
      if (imap->_readCallback)
        imapCBP(imap, esp_mail_str_73, false);

      imap->_mbif._availableItems++;
      int uid = imap->getUID(imap->_mbif._msgCount);
      imap->_msgUID.push_back(uid);
      imap->_headerOnly = false;
      char *tmp = intStr(uid);
      _uid = tmp;
      delP(&tmp);
      imap->_config->fetch.uid = _uid.c_str();
    }
  }
  else
  {
    imap->_mbif._availableItems++;
    imap->_msgUID.push_back(atoi(imap->_config->fetch.uid));
  }

  for (size_t i = 0; i < imap->_msgUID.size(); i++)
  {
    imap->_cMsgIdx = i;
    imap->_totalRead++;

    if (MailClient.getFreeHeap() - (imap->_config->limit.msg_size * (i + 1)) < ESP_MAIL_MIN_MEM)
    {
      if (imap->_debug)
        errorStatusCB(imap, MAIL_CLIENT_ERROR_OUT_OF_MEMORY);
      goto out;
    }

    if (imap->_readCallback)
    {
      readCount++;

      MBSTRING s;
      appendP(s, esp_mail_str_74, true);
      char *tmp = intStr(imap->_totalRead);
      s += tmp;
      delP(&tmp);

      if (imap->_uidSearch || strlen(imap->_config->fetch.uid) > 0)
        appendP(s, esp_mail_str_75, false);
      else
        appendP(s, esp_mail_str_76, false);

      tmp = intStr(imap->_msgUID[i]);
      s += tmp;
      delP(&tmp);
      imapCB(imap, "", false);
      imapCB(imap, s.c_str(), false);
    }

    if (imap->_debug)
      debugInfoP(esp_mail_str_233);

    MBSTRING cmd;
    if (imap->_uidSearch || strlen(imap->_config->fetch.uid) > 0)
      appendP(cmd, esp_mail_str_142, true);
    else
      appendP(cmd, esp_mail_str_143, true);

    if (imap->_debug)
      debugInfoP(esp_mail_str_77);

    char *tmp = intStr(imap->_msgUID[i]);
    cmd += tmp;
    delP(&tmp);

    appendP(cmd, esp_mail_str_147, false);
    if (!imap->_config->fetch.set_seen)
    {
      appendP(cmd, esp_mail_str_152, false);
      appendP(cmd, esp_mail_str_214, false);
    }
    appendP(cmd, esp_mail_str_218, false);

    appendP(cmd, esp_mail_str_144, false);
    appendP(cmd, esp_mail_str_219, false);
    if (imapSend(imap, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_fetch_body_header;

    int err = IMAP_STATUS_BAD_COMMAND;
    if (imap->_headerOnly)
      err = IMAP_STATUS_IMAP_RESPONSE_FAILED;

    if (!handleIMAPResponse(imap, err, closeSession))
      return false;

    cHeader(imap)->flags = imap->getFlags(cHeader(imap)->message_no);

    if (!imap->_headerOnly)
    {
      imap->_cPartIdx = 0;

      //multipart
      if (cHeader(imap)->multipart)
      {
        struct esp_mail_imap_multipart_level_t mlevel;
        mlevel.level = 1;
        mlevel.fetch_rfc822_header = false;
        mlevel.append_body_text = false;
        imap->_multipart_levels.push_back(mlevel);

        if (!fetchMultipartBodyHeader(imap, i))
          return false;
      }
      else
      {
        //singlepart
        if (imap->_debug)
        {
          MBSTRING s;
          appendP(s, esp_mail_str_81, true);
          s += '1';
          esp_mail_debug(s.c_str());
        }

        cHeader(imap)->partNumStr.clear();
        if (!sendIMAPCommand(imap, i, 1))
          return false;

        imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_fetch_body_mime;
        if (!handleIMAPResponse(imap, IMAP_STATUS_BAD_COMMAND, closeSession))
          return false;
      }

      if (imap->_config->download.text || imap->_config->download.html || imap->_config->download.attachment || imap->_config->download.inlineImg)
      {
        if (!_sdOk && imap->_config->storage.type == esp_mail_file_storage_type_sd)
        {
          _sdOk = sdTest();
          if (_sdOk)
            if (!ESP_MAIL_SD_FS.exists(imap->_config->storage.saved_path))
              createDirs(imap->_config->storage.saved_path);
        }
        else if (!_flashOk && imap->_config->storage.type == esp_mail_file_storage_type_flash)
#if defined(ESP32)
          _flashOk = ESP_MAIL_FLASH_FS.begin(FORMAT_FLASH);
#elif defined(ESP8266)
          _flashOk = ESP_MAIL_FLASH_FS.begin();
#else
        {
        }
#endif
      }

      if (cHeader(imap)->part_headers.size() > 0)
      {

        cHeader(imap)->hasAttachment = cHeader(imap)->attachment_count > 0;

        if (cHeader(imap)->attachment_count > 0 && imap->_readCallback)
        {
          MBSTRING s;
          appendP(s, esp_mail_str_34, true);
          appendP(s, esp_mail_str_78, false);

          char *tmp = intStr(cHeader(imap)->attachment_count);
          s += tmp;
          delP(&tmp);
          appendP(s, esp_mail_str_79, false);
          imapCB(imap, s.c_str(), false);

          for (size_t j = 0; j < cHeader(imap)->part_headers.size(); j++)
          {
            imap->_cPartIdx = j;
            if (!cPart(imap)->rfc822_part && cPart(imap)->attach_type != esp_mail_att_type_none)
              imapCB(imap, cPart(imap)->filename.c_str(), false);
          }
        }

        MBSTRING s1, s2;
        int _idx1 = 0;
        for (size_t j = 0; j < cHeader(imap)->part_headers.size(); j++)
        {
          imap->_cPartIdx = j;
          if (cPart(imap)->rfc822_part)
          {
            s1 = cPart(imap)->partNumStr;
            _idx1 = cPart(imap)->rfc822_msg_Idx;
          }
          else if (s1.length() > 0)
          {
            if (multipartMember(s1, cPart(imap)->partNumStr))
            {
              cPart(imap)->message_sub_type = esp_mail_imap_message_sub_type_rfc822;
              cPart(imap)->rfc822_msg_Idx = _idx1;
            }
          }

          if (cPart(imap)->multipart_sub_type == esp_mail_imap_multipart_sub_type_parallel)
            s2 = cPart(imap)->partNumStr;
          else if (s2.length() > 0)
          {
            if (multipartMember(s2, cPart(imap)->partNumStr))
            {
              cPart(imap)->attach_type = esp_mail_att_type_attachment;
              if (cPart(imap)->filename.length() == 0)
              {
                if (cPart(imap)->name.length() > 0)
                  cPart(imap)->filename = cPart(imap)->name;
                else
                {
                  char *tmp = getRandomUID();
                  cPart(imap)->filename = tmp;
                  appendP(cPart(imap)->filename, esp_mail_str_40, false);
                  delP(&tmp);
                }
              }
            }
          }
        }

        int acnt = 0;
        int ccnt = 0;

        for (size_t j = 0; j < cHeader(imap)->part_headers.size(); j++)
        {
          imap->_cPartIdx = j;

          if (cPart(imap)->rfc822_part || cPart(imap)->multipart_sub_type != esp_mail_imap_multipart_sub_type_none)
            continue;

          bool rfc822_body_subtype = cPart(imap)->message_sub_type == esp_mail_imap_message_sub_type_rfc822;

          if (cPart(imap)->attach_type == esp_mail_att_type_none || cPart(imap)->msg_type == esp_mail_msg_type_html || cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched)
          {

            bool ret = ((imap->_config->enable.rfc822 || imap->_config->download.rfc822) && rfc822_body_subtype) || (!rfc822_body_subtype && ((imap->_config->enable.text && (cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched)) || (imap->_config->enable.html && cPart(imap)->msg_type == esp_mail_msg_type_html) || (cPart(imap)->msg_type == esp_mail_msg_type_html && imap->_config->download.html) || ((cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched) && imap->_config->download.text)));
            if (!ret)
              continue;

            if ((imap->_config->download.rfc822 && rfc822_body_subtype) || (!rfc822_body_subtype && ((cPart(imap)->msg_type == esp_mail_msg_type_html && imap->_config->download.html) || ((cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched) && imap->_config->download.text))))
            {

              if (ccnt == 0)
              {
                imapCB(imap, "", false);
                imapCBP(imap, esp_mail_str_57, false);
              }

              if (imap->_debug)
              {
                if (cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched)
                  debugInfoP(esp_mail_str_59);
                else if (cPart(imap)->msg_type == esp_mail_msg_type_html)
                  debugInfoP(esp_mail_str_60);
              }
            }
            else
            {
              if (ccnt == 0)
              {
                imapCB(imap, "", false);
                imapCBP(imap, esp_mail_str_307, false);
              }

              if (imap->_debug)
              {
                if (cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched)
                  debugInfoP(esp_mail_str_308);
                else if (cPart(imap)->msg_type == esp_mail_msg_type_html)
                  debugInfoP(esp_mail_str_309);
              }
            }

            ccnt++;

            if (!sendIMAPCommand(imap, i, 2))
              return false;

            imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_fetch_body_text;
            if (!handleIMAPResponse(imap, IMAP_STATUS_IMAP_RESPONSE_FAILED, closeSession))
              return false;
          }
          else if (cPart(imap)->attach_type != esp_mail_att_type_none && (_sdOk || _flashOk))
          {

            if (imap->_config->download.attachment || imap->_config->download.inlineImg)
            {
              if (acnt == 0)
              {
                imapCB(imap, "", false);
                imapCBP(imap, esp_mail_str_80, false);
              }

              if (imap->_debug)
                debugInfoP(esp_mail_str_55);

              acnt++;
              if (cPart(imap)->octetLen <= (int)imap->_config->limit.attachment_size)
              {

                if (_sdOk || _flashOk)
                {

                  if ((int)j < (int)cHeader(imap)->part_headers.size() - 1)
                    if (cHeader(imap)->part_headers[j + 1].octetLen > (int)imap->_config->limit.attachment_size)
                      cHeader(imap)->downloaded_bytes += cHeader(imap)->part_headers[j + 1].octetLen;

                  if (!sendIMAPCommand(imap, i, 3))
                    return false;

                  imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_fetch_body_attachment;
                  if (!handleIMAPResponse(imap, IMAP_STATUS_IMAP_RESPONSE_FAILED, closeSession))
                    return false;
                  delay(0);
                }
              }
              else
              {
                if ((int)j == (int)cHeader(imap)->part_headers.size() - 1)
                  cHeader(imap)->downloaded_bytes += cPart(imap)->octetLen;
              }
            }
          }
        }
      }

      if (imap->_config->download.header && !imap->_headerSaved)
      {
        if (imap->_readCallback)
        {
          imapCB(imap, "", false);
          imapCBP(imap, esp_mail_str_124, false);
        }
        saveHeader(imap);
      }

      if (closeSession)
      {
        if (imap->_config->storage.type == esp_mail_file_storage_type_sd)
        {
#if defined(ESP_MAIL_SD_FS)
          if (_sdOk)
            ESP_MAIL_SD_FS.end();
#endif
          _sdOk = false;
        }
        else if (imap->_config->storage.type == esp_mail_file_storage_type_flash)
        {

#if defined(ESP_MAIL_FLASH_FS)
          if (_flashOk)
            ESP_MAIL_FLASH_FS.end();
#endif
          _flashOk = false;
        }
      }

      imap->_cMsgIdx++;
    }

    if (imap->_debug)
    {
      MBSTRING s;
      appendP(s, esp_mail_str_261, true);
      appendP(s, esp_mail_str_84, false);
      char *tmp = intStr(MailClient.getFreeHeap());
      s += tmp;
      delP(&tmp);
      esp_mail_debug(s.c_str());
    }
  }

out:

  if (readCount < imap->_msgUID.size())
  {
    imap->_mbif._availableItems = readCount;
    imap->_msgUID.erase(imap->_msgUID.begin() + readCount, imap->_msgUID.end());
  }

  if (closeSession)
  {
    if (!imap->closeSession())
      return false;
  }
  else
  {
    if (imap->_readCallback)
    {
      imapCB(imap, "", false);
      imapCBP(imap, esp_mail_str_87, false);
    }

    if (imap->_debug)
      debugInfoP(esp_mail_str_88);
  }

  if (imap->_readCallback)
    imapCB(imap, "", true);

  return true;
}
bool ESP_Mail_Client::getMultipartFechCmd(IMAPSession *imap, int msgIdx, MBSTRING &partText)
{
  if (imap->_multipart_levels.size() == 0)
    return false;

  int cLevel = imap->_multipart_levels.size() - 1;

  cHeader(imap)->partNumStr.clear();

  if (imap->_uidSearch || strlen(imap->_config->fetch.uid) > 0)
    appendP(partText, esp_mail_str_142, true);
  else
    appendP(partText, esp_mail_str_143, true);

  char *tmp = intStr(imap->_msgUID[msgIdx]);
  partText += tmp;
  delP(&tmp);

  appendP(partText, esp_mail_str_147, false);
  if (!imap->_config->fetch.set_seen)
  {
    appendP(partText, esp_mail_str_152, false);
    appendP(partText, esp_mail_str_214, false);
  }
  appendP(partText, esp_mail_str_218, false);

  for (size_t i = 0; i < imap->_multipart_levels.size(); i++)
  {
    if (i > 0)
    {
      appendP(partText, esp_mail_str_152, false);
      appendP(cHeader(imap)->partNumStr, esp_mail_str_152, false);
    }

    tmp = intStr(imap->_multipart_levels[i].level);
    partText += tmp;
    cHeader(imap)->partNumStr += tmp;
    delP(&tmp);
  }

  if (imap->_multipart_levels[cLevel].fetch_rfc822_header)
  {
    appendP(partText, esp_mail_str_152, false);
    appendP(partText, esp_mail_str_144, false);
    appendP(partText, esp_mail_str_219, false);
    imap->_multipart_levels[cLevel].append_body_text = true;
  }
  else
    appendP(partText, esp_mail_str_148, false);

  imap->_multipart_levels[cLevel].fetch_rfc822_header = false;

  return true;
}

bool ESP_Mail_Client::multipartMember(const MBSTRING &part, const MBSTRING &check)
{
  if (part.length() > check.length())
    return false;

  for (size_t i = 0; i < part.length(); i++)
    if (part[i] != check[i])
      return false;

  return true;
}

bool ESP_Mail_Client::fetchMultipartBodyHeader(IMAPSession *imap, int msgIdx)
{
  bool ret = true;

  if (!connected(imap))
  {
    closeTCPSession(imap);
    return false;
  }
  int cLevel = 0;

  do
  {

    struct esp_mail_message_part_info_t *_cpart = &cHeader(imap)->part_headers[cHeader(imap)->message_data_count - 1];
    bool rfc822_body_subtype = _cpart->message_sub_type == esp_mail_imap_message_sub_type_rfc822;

    MBSTRING cmd;

    if (!getMultipartFechCmd(imap, msgIdx, cmd))
      return true;

    if (imap->_debug)
    {
      MBSTRING s;
      if (imap->_multipart_levels.size() > 1)
        appendP(s, esp_mail_str_86, true);
      else
        appendP(s, esp_mail_str_81, true);
      s += cHeader(imap)->partNumStr;
      esp_mail_debug(s.c_str());
    }

    if (imapSend(imap, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    imap->_imap_cmd = esp_mail_imap_cmd_fetch_body_mime;

    ret = handleIMAPResponse(imap, IMAP_STATUS_IMAP_RESPONSE_FAILED, false);

    _cpart = &cHeader(imap)->part_headers[cHeader(imap)->message_data_count - 1];
    rfc822_body_subtype = _cpart->message_sub_type == esp_mail_imap_message_sub_type_rfc822;
    cLevel = imap->_multipart_levels.size() - 1;

    if (ret)
    {

      if (_cpart->multipart)
      {
        if (_cpart->multipart_sub_type == esp_mail_imap_multipart_sub_type_parallel || _cpart->multipart_sub_type == esp_mail_imap_multipart_sub_type_alternative || _cpart->multipart_sub_type == esp_mail_imap_multipart_sub_type_related || _cpart->multipart_sub_type == esp_mail_imap_multipart_sub_type_mixed)
        {
          struct esp_mail_imap_multipart_level_t mlevel;
          mlevel.level = 1;
          mlevel.fetch_rfc822_header = false;
          mlevel.append_body_text = false;
          imap->_multipart_levels.push_back(mlevel);
          fetchMultipartBodyHeader(imap, msgIdx);
        }
        else
          imap->_multipart_levels[cLevel].level++;
      }
      else
      {
        if (rfc822_body_subtype)
        {
          //to get additional rfc822 message header
          imap->_multipart_levels[cLevel].fetch_rfc822_header = true;
          fetchMultipartBodyHeader(imap, msgIdx);
        }
        else
        {
          if (imap->_multipart_levels[cLevel].append_body_text)
          {
            //single part rfc822 message body, append TEXT to the body fetch command
            appendP(_cpart->partNumFetchStr, esp_mail_str_152, false);
            appendP(_cpart->partNumFetchStr, esp_mail_str_215, false);
            imap->_multipart_levels[cLevel].append_body_text = false;
          }
          imap->_multipart_levels[cLevel].level++;
        }
      }
    }

  } while (ret);

  imap->_multipart_levels.pop_back();

  if (imap->_multipart_levels.size() > 0)
  {
    cLevel = imap->_multipart_levels.size() - 1;
    imap->_multipart_levels[cLevel].level++;
  }

  return true;
}

bool ESP_Mail_Client::connected(IMAPSession *imap)
{
  if (!imap->tcpClient.stream())
    return false;
  return imap->tcpClient.stream()->connected();
}

bool ESP_Mail_Client::imapAuth(IMAPSession *imap)
{

  bool ssl = false;
  MBSTRING buf;
#if defined(ESP32)
  imap->tcpClient.setDebugCallback(NULL);
#elif defined(ESP8266)

#endif

  if (imap->_config != nullptr)
  {
    if (strlen(imap->_config->fetch.uid) > 0)
      imap->_headerOnly = false;
    else
      imap->_headerOnly = true;
  }

  imap->_totalRead = 0;
  imap->_secure = true;
  bool secureMode = true;

#if defined(ESP32)
  if (imap->_debug)
    imap->tcpClient.setDebugCallback(esp_mail_debug);
#elif defined(ESP8266)
  imap->tcpClient.txBufDivider = 16; //minimum, tx buffer size for ssl data and request command data
  imap->tcpClient.rxBufDivider = 1;
  if (imap->_config != nullptr)
  {
    if (!imap->_headerOnly && !imap->_config->enable.html && !imap->_config->enable.text && !imap->_config->download.attachment && !imap->_config->download.inlineImg && !imap->_config->download.html && !imap->_config->download.text)
      imap->tcpClient.rxBufDivider = 16; // minimum rx buffer size for only message header
  }
#endif

  if (imap->_sesson_cfg->server.port == esp_mail_imap_port_143)
  {
    imap->_secure = false;
    secureMode = false;
  }
  else
    secureMode = !imap->_sesson_cfg->secure.startTLS;
#if defined(ESP32) || defined(ESP8266)
  setSecure(imap->tcpClient, imap->_sesson_cfg, imap->_caCert);
#endif
  if (imap->_readCallback)
    imapCBP(imap, esp_mail_str_50, false);

  if (imap->_debug)
  {
    MBSTRING s;
    appendP(s, esp_mail_str_314, true);
    s += ESP_MAIL_VERSION;
#if defined(ARDUINO_ARCH_SAMD)
    MBSTRING ninaFw = WiFi.firmwareVersion();
    appendP(s, esp_mail_str_329, false);
    s += ninaFw;
    imap->tcpClient.firmwareBuildNumber();
    if (imap->tcpClient._fwBuild > 0)
    {
      String build = String(imap->tcpClient._fwBuild);
      appendP(s, esp_mail_str_330, false);
      s += String(imap->tcpClient._fwBuild).c_str();
    }
#endif
    esp_mail_debug(s.c_str());

    debugInfoP(esp_mail_str_225);
    appendP(s, esp_mail_str_261, true);
    appendP(s, esp_mail_str_211, false);
    s += imap->_sesson_cfg->server.host_name;
    esp_mail_debug(s.c_str());
    char *tmp = intStr(imap->_sesson_cfg->server.port);
    appendP(s, esp_mail_str_261, true);
    appendP(s, esp_mail_str_201, false);
    s += tmp;
    delP(&tmp);
    esp_mail_debug(s.c_str());
  }

  imap->tcpClient.begin(imap->_sesson_cfg->server.host_name, imap->_sesson_cfg->server.port);

  ethDNSWorkAround(imap->_sesson_cfg);

  if (!imap->tcpClient.connect(secureMode, imap->_sesson_cfg->certificate.verify))
    return handleIMAPError(imap, IMAP_STATUS_SERVER_CONNECT_FAILED, false);

  imap->_tcpConnected = true;

#if defined(ESP32)
  WiFiClient *stream = imap->tcpClient.stream();
  stream->setTimeout(TCP_CLIENT_DEFAULT_TCP_TIMEOUT_SEC);
#elif defined(ESP8266)
  WiFiClient *stream = imap->tcpClient.stream();
  stream->setTimeout(TCP_CLIENT_DEFAULT_TCP_TIMEOUT_SEC * 1000);
#endif
  imap->tcpClient.tcpTimeout = TCP_CLIENT_DEFAULT_TCP_TIMEOUT_SEC * 1000;

  if (imap->_readCallback)
    imapCBP(imap, esp_mail_str_54, false);

  if (imap->_debug)
    debugInfoP(esp_mail_str_228);

init:

  if (!imap->checkCapability())
    return false;

  //start TLS when needed or the server issue
  if ((imap->_auth_capability.start_tls || imap->_sesson_cfg->secure.startTLS) && !ssl)
  {
    MBSTRING s;
    if (imap->_readCallback)
    {
      appendP(s, esp_mail_str_34, true);
      appendP(s, esp_mail_str_209, false);
      esp_mail_debug(s.c_str());
    }

    if (imap->_debug)
    {
      appendP(s, esp_mail_str_196, true);
      esp_mail_debug(s.c_str());
    }

    imapSendP(imap, esp_mail_str_311, false);
    imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_starttls;
    if (!handleIMAPResponse(imap, IMAP_STATUS_BAD_COMMAND, false))
      return false;

    if (imap->_debug)
    {
      debugInfoP(esp_mail_str_310);
    }

    //connect in secure mode
    //do ssl handshake
#if defined(ARDUINO_ARCH_SAMD)
    if (!imap->tcpClient.connectSSL(imap->_sesson_cfg->certificate.verify))
      return handleIMAPError(imap, MAIL_CLIENT_ERROR_SSL_TLS_STRUCTURE_SETUP, false);
#else
    if (!imap->tcpClient.stream()->connectSSL(imap->_sesson_cfg->certificate.verify))
      return handleIMAPError(imap, MAIL_CLIENT_ERROR_SSL_TLS_STRUCTURE_SETUP, false);
#endif

    //set the secure mode
    imap->_sesson_cfg->secure.startTLS = false;
    ssl = true;
    imap->_secure = true;

    //check the capabilitiy again
    goto init;
  }

  imap->clearMessageData();
  imap->_mailboxOpened = false;

  bool creds = strlen(imap->_sesson_cfg->login.email) > 0 && strlen(imap->_sesson_cfg->login.password) > 0;
  bool xoauth_auth = strlen(imap->_sesson_cfg->login.accessToken) > 0 && imap->_auth_capability.xoauth2;
  bool login_auth = creds;
  bool plain_auth = imap->_auth_capability.plain && creds;

  bool supported_auth = xoauth_auth || login_auth || plain_auth;

  if (!supported_auth)
    return handleIMAPError(imap, IMAP_STATUS_NO_SUPPORTED_AUTH, false);

  //rfc4959
  if (supported_auth)
  {
    if (imap->_readCallback)
    {
      imapCB(imap, "", false);
      imapCBP(imap, esp_mail_str_56, false);
    }
  }

  if (xoauth_auth)
  {
    if (!imap->_auth_capability.xoauth2)
      return handleIMAPError(imap, IMAP_STATUS_SERVER_OAUTH2_LOGIN_DISABLED, false);

    if (imap->_debug)
      debugInfoP(esp_mail_str_291);

    MBSTRING cmd;
    appendP(cmd, esp_mail_str_292, true);
    cmd += getEncodedToken(imap);
    if (imapSend(imap, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_auth;
    if (!handleIMAPResponse(imap, IMAP_STATUS_LOGIN_FAILED, false))
      return false;
  }
  else if (login_auth)
  {

    if (imap->_debug)
      debugInfoP(esp_mail_str_229);

    MBSTRING cmd;

    appendP(cmd, esp_mail_str_130, true);
    cmd += imap->_sesson_cfg->login.email;
    appendP(cmd, esp_mail_str_131, false);
    cmd += imap->_sesson_cfg->login.password;

    if (imapSend(imap, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_login;
    if (!handleIMAPResponse(imap, IMAP_STATUS_LOGIN_FAILED, true))
      return false;
  }
  else if (plain_auth)
  {
    if (imap->_debug)
      debugInfoP(esp_mail_str_290);

    const char *usr = imap->_sesson_cfg->login.email;
    const char *psw = imap->_sesson_cfg->login.password;
    int len = strlen(usr) + strlen(psw) + 2;
    uint8_t *tmp = (uint8_t *)newP(len);
    memset(tmp, 0, len);
    int p = 1;
    memcpy(tmp + p, usr, strlen(usr));
    p += strlen(usr) + 1;
    memcpy(tmp + p, psw, strlen(psw));
    p += strlen(psw);

    MBSTRING s;
    appendP(s, esp_mail_str_41, true);
    s += encodeBase64Str(tmp, p);
    delP(&tmp);

    if (imapSend(imap, s.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    imap->_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_login;
    if (!handleIMAPResponse(imap, IMAP_STATUS_LOGIN_FAILED, true))
      return false;
  }

  return true;
}

MBSTRING ESP_Mail_Client::getEncodedToken(IMAPSession *imap)
{
  MBSTRING raw;
  appendP(raw, esp_mail_str_285, true);
  raw += imap->_sesson_cfg->login.email;
  appendP(raw, esp_mail_str_286, false);
  raw += imap->_sesson_cfg->login.accessToken;
  appendP(raw, esp_mail_str_287, false);
  MBSTRING s = encodeBase64Str((const unsigned char *)raw.c_str(), raw.length());
  return s;
}

bool ESP_Mail_Client::imapLogout(IMAPSession *imap)
{
  if (imap->_readCallback)
  {
    imapCBP(imap, esp_mail_str_85, false);
  }

  if (imap->_debug)
    debugInfoP(esp_mail_str_234);

  if (imapSendP(imap, esp_mail_str_146, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  imap->_imap_cmd = esp_mail_imap_cmd_logout;
  if (!handleIMAPResponse(imap, IMAP_STATUS_BAD_COMMAND, false))
    return false;

  if (imap->_readCallback)
  {
    imapCB(imap, "", false);
    imapCBP(imap, esp_mail_str_187, false);
  }

  if (imap->_debug)
    debugInfoP(esp_mail_str_235);

  return true;
}

void ESP_Mail_Client::errorStatusCB(IMAPSession *imap, int error)
{
  imap->_imapStatus.statusCode = error;
  MBSTRING s;
  if (imap->_readCallback)
  {
    appendP(s, esp_mail_str_53, true);
    s += imap->errorReason().c_str();
    imapCB(imap, s.c_str(), false);
  }

  if (imap->_debug)
  {
    appendP(s, esp_mail_str_185, true);
    s += imap->errorReason().c_str();
    esp_mail_debug(s.c_str());
  }
}

size_t ESP_Mail_Client::imapSendP(IMAPSession *imap, PGM_P v, bool newline)
{
  if (!reconnect(imap))
  {
    closeTCPSession(imap);
    return 0;
  }

  if (!connected(imap))
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
    return 0;
  }

  if (!imap->_tcpConnected)
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    return 0;
  }

  char *tmp = strP(v);
  size_t len = 0;

  if (newline)
  {
    if (imap->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug(tmp);
    len = imap->tcpClient.stream()->println(tmp);
  }
  else
  {
    if (imap->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug_line(tmp, false);
    len = imap->tcpClient.stream()->print(tmp);
  }

  if (len != strlen(tmp) && len != strlen(tmp) + 2)
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    len = 0;
  }

  delP(&tmp);

  return len;
}

size_t ESP_Mail_Client::imapSend(IMAPSession *imap, const char *data, bool newline)
{
  if (!reconnect(imap))
  {
    closeTCPSession(imap);
    return 0;
  }

  if (!connected(imap))
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
    return 0;
  }

  if (!imap->_tcpConnected)
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    return 0;
  }

  size_t len = 0;

  if (newline)
  {
    if (imap->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug(data);
    len = imap->tcpClient.stream()->println(data);
  }
  else
  {
    if (imap->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug_line(data, false);
    len = imap->tcpClient.stream()->print(data);
  }

  if (len != strlen(data) && len != strlen(data) + 2)
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    len = 0;
  }
  return len;
}

size_t ESP_Mail_Client::imapSend(IMAPSession *imap, int data, bool newline)
{
  if (!reconnect(imap))
  {
    closeTCPSession(imap);
    return 0;
  }

  if (!connected(imap))
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
    return 0;
  }

  if (!imap->_tcpConnected)
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    return 0;
  }

  char *tmp = intStr(data);
  size_t len = 0;

  if (newline)
  {
    if (imap->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug(tmp);
    len = imap->tcpClient.stream()->println(tmp);
  }
  else
  {
    if (imap->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug_line(tmp, false);
    len = imap->tcpClient.stream()->print(tmp);
  }

  if (len != strlen(tmp) && len != strlen(tmp) + 2)
  {
    errorStatusCB(imap, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    len = 0;
  }

  delP(&tmp);

  return len;
}

bool ESP_Mail_Client::setFlag(IMAPSession *imap, int msgUID, const char *flag, bool closeSession)
{
  return mSetFlag(imap, msgUID, flag, 0, closeSession);
}

bool ESP_Mail_Client::addFlag(IMAPSession *imap, int msgUID, const char *flag, bool closeSession)
{
  return mSetFlag(imap, msgUID, flag, 1, closeSession);
}

bool ESP_Mail_Client::removeFlag(IMAPSession *imap, int msgUID, const char *flag, bool closeSession)
{
  return mSetFlag(imap, msgUID, flag, 2, closeSession);
}

bool ESP_Mail_Client::mSetFlag(IMAPSession *imap, int msgUID, const char *flag, uint8_t action, bool closeSession)
{
  if (!reconnect(imap))
    return false;

  if (!imap->_tcpConnected)
  {
    imap->_mailboxOpened = false;
    return false;
  }

  if (imap->_currentFolder.length() == 0)
  {
    if (imap->_readCallback)
      debugInfoP(esp_mail_str_153);

    if (imap->_debug)
    {
      MBSTRING e;
      appendP(e, esp_mail_str_185, true);
      appendP(e, esp_mail_str_151, false);
      esp_mail_debug(e.c_str());
    }
  }
  else
  {
    if (imap->_readOnlyMode || !imap->_mailboxOpened)
    {
      if (!imap->selectFolder(imap->_currentFolder.c_str(), false))
        return false;
    }
  }

  if (imap->_readCallback)
  {
    imapCB(imap, "", false);
    if (action == 0)
      debugInfoP(esp_mail_str_157);
    else if (action == 1)
      debugInfoP(esp_mail_str_155);
    else
      debugInfoP(esp_mail_str_154);
  }

  if (imap->_debug)
  {
    if (action == 0)
      debugInfoP(esp_mail_str_253);
    else if (action == 1)
      debugInfoP(esp_mail_str_254);
    else
      debugInfoP(esp_mail_str_255);
  }

  MBSTRING cmd;
  appendP(cmd, esp_mail_str_249, true);
  char *tmp = intStr(msgUID);
  cmd += tmp;
  delP(&tmp);
  if (action == 0)
    appendP(cmd, esp_mail_str_250, false);
  else if (action == 1)
    appendP(cmd, esp_mail_str_251, false);
  else
    appendP(cmd, esp_mail_str_252, false);
  cmd += flag;
  appendP(cmd, esp_mail_str_192, false);

  if (imapSend(imap, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  imap->_imap_cmd = esp_mail_imap_cmd_store;

  if (!handleIMAPResponse(imap, IMAP_STATUS_PARSE_FLAG_FAILED, false))
    return false;

  if (closeSession)
    imap->closeSession();

  return true;
}

void ESP_Mail_Client::imapCBP(IMAPSession *imap, PGM_P info, bool success)
{
  char *tmp = strP(info);
  imap->_cbData._info = tmp;
  imap->_cbData._success = success;
  imap->_readCallback(imap->_cbData);
  delP(&tmp);
}

void ESP_Mail_Client::imapCB(IMAPSession *imap, const char *info, bool success)
{
  imap->_cbData._info = info;
  imap->_cbData._success = success;
  imap->_readCallback(imap->_cbData);
}

int ESP_Mail_Client::getMSGNUM(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, bool &endSearch, int &nump, const char *key, const char *pc)
{
  int ret = -1;
  char c = 0;
  int idx = 0;
  int num = 0;
  while (available(imap) > 0 && idx < bufLen)
  {
    delay(0);

    ret = imap->tcpClient.stream()->read();

    if (ret > -1)
    {

      if (idx >= bufLen - 1)
        return idx;

      c = (char)ret;

      if (c == '\n')
        c = ' ';

      strcat_c(buf, c);
      idx++;

      if (chunkIdx == 0)
      {
        if (strcmp(buf, key) == 0)
        {
          chunkIdx++;
          return 0;
        }

        if (strposP(buf, esp_mail_imap_response_1, 0) > -1)
          goto end_search;
      }
      else
      {
        if (c == ' ')
        {
          imap->_mbif._searchCount++;
          if (imap->_config->enable.recent_sort)
          {
            imap->_msgUID.push_back(atoi(buf));
            if (imap->_msgUID.size() > imap->_config->limit.search)
              imap->_msgUID.erase(imap->_msgUID.begin());
          }
          else
          {
            if (imap->_msgUID.size() < imap->_config->limit.search)
              imap->_msgUID.push_back(atoi(buf));
          }

          if (imap->_debug)
          {
            num = (float)(100.0f * imap->_mbif._searchCount / imap->_mbif._msgCount);
            if (nump != num)
            {
              nump = num;
              searchReport(num, pc);
            }
          }

          chunkIdx++;
          return idx;
        }
        else if (c == '$')
        {
          if (imap->_config->enable.recent_sort)
            std::sort(imap->_msgUID.begin(), imap->_msgUID.end(), compFunc);

          goto end_search;
        }
      }
    }
  }

  return idx;

end_search:

  endSearch = true;
  int read = available(imap);

  idx = imap->tcpClient.stream()->readBytes(buf + idx, read);

  return idx;
}

struct esp_mail_message_part_info_t *ESP_Mail_Client::cPart(IMAPSession *imap)
{
  return &cHeader(imap)->part_headers[imap->_cPartIdx];
}

struct esp_mail_message_header_t *ESP_Mail_Client::cHeader(IMAPSession *imap)
{
  return &imap->_headers[cIdx(imap)];
}

void ESP_Mail_Client::handleHeader(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, struct esp_mail_message_header_t &header, int &headerState, int &octetCount, bool caseSensitive)
{
  char *tmp = nullptr;
  if (chunkIdx == 0)
  {
    if (strposP(buf, esp_mail_str_324, 0, caseSensitive) != -1 && buf[0] == '*')
      chunkIdx++;

    tmp = subStr(buf, esp_mail_str_193, esp_mail_str_194, 0);
    if (tmp)
    {
      octetCount = 2;
      header.header_data_len = atoi(tmp);
      delP(&tmp);
    }

    tmp = subStr(buf, esp_mail_str_51, esp_mail_imap_response_7, 0);
    if (tmp)
    {
      header.message_no = atoi(tmp);
      delP(&tmp);
    }
  }
  else
  {
    if (octetCount > header.header_data_len + 2)
      return;

    if (strcmpP(buf, 0, esp_mail_str_10, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_from;
      tmp = subStr(buf, esp_mail_str_10, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_150, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_sender;
      tmp = subStr(buf, esp_mail_str_150, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_11, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_to;
      tmp = subStr(buf, esp_mail_str_11, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_12, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_cc;
      tmp = subStr(buf, esp_mail_str_12, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_24, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_subject;
      tmp = subStr(buf, esp_mail_str_24, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_46, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_return_path;
      tmp = subStr(buf, esp_mail_str_46, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_184, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_reply_to;
      tmp = subStr(buf, esp_mail_str_184, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_109, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_in_reply_to;
      tmp = subStr(buf, esp_mail_str_109, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_107, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_references;
      tmp = subStr(buf, esp_mail_str_107, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_134, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_comments;
      tmp = subStr(buf, esp_mail_str_134, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_145, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_keywords;
      tmp = subStr(buf, esp_mail_str_145, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_25, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_content_type;
      tmp = subStr(buf, esp_mail_str_25, esp_mail_str_97, 0, caseSensitive);
      if (tmp)
      {
        if (strpos(tmp, esp_mail_imap_multipart_sub_type_t::mixed, 0, caseSensitive) != -1)
          header.hasAttachment = true;

        setHeader(imap, buf, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_172, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_content_transfer_encoding;
      tmp = subStr(buf, esp_mail_str_172, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_190, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_accept_language;
      tmp = subStr(buf, esp_mail_str_190, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_191, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_content_language;
      tmp = subStr(buf, esp_mail_str_191, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_99, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_date;
      tmp = subStr(buf, esp_mail_str_99, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_101, caseSensitive))
    {
      headerState = esp_mail_imap_header_state::esp_mail_imap_state_msg_id;
      tmp = subStr(buf, esp_mail_str_101, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);
      }
    }
    chunkIdx++;
  }
}

void ESP_Mail_Client::setHeader(IMAPSession *imap, char *buf, struct esp_mail_message_header_t &header, int state)
{
  size_t i = 0;
  while (buf[i] == ' ')
  {
    i++;
    if (strlen(buf) <= i)
      return;
  }

  switch (state)
  {
  case esp_mail_imap_header_state::esp_mail_imap_state_from:
    header.header_fields.from += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_sender:
    header.header_fields.sender += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_to:
    header.header_fields.to += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_cc:
    header.header_fields.cc += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_subject:
    header.header_fields.subject += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_content_type:
    header.content_type += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_content_transfer_encoding:
    header.content_transfer_encoding += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_accept_language:
    header.accept_language += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_content_language:
    header.content_language += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_date:
    header.header_fields.date += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_msg_id:
    header.header_fields.messageID += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_return_path:
    header.header_fields.return_path += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_reply_to:
    header.header_fields.reply_to += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_in_reply_to:
    header.header_fields.in_reply_to += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_references:
    header.header_fields.references += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_comments:
    header.header_fields.comments += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_keywords:
    header.header_fields.keywords += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_char_set:
    header.char_set += &buf[i];
    break;
  case esp_mail_imap_header_state::esp_mail_imap_state_boundary:
    header.boundary += &buf[i];
    break;
  default:
    break;
  }
}

void ESP_Mail_Client::handlePartHeader(IMAPSession *imap, char *buf, int &chunkIdx, struct esp_mail_message_part_info_t &part, bool caseSensitive)
{
  char *tmp = nullptr;
  if (chunkIdx == 0)
  {
    tmp = subStr(buf, esp_mail_imap_response_7, NULL, 0, -1);
    if (tmp)
    {
      delP(&tmp);
      tmp = subStr(buf, esp_mail_str_193, esp_mail_str_194, 0);
      if (tmp)
      {
        chunkIdx++;
        part.octetLen = atoi(tmp);
        delP(&tmp);
      }
    }
  }
  else
  {
    if (strcmpP(buf, 0, esp_mail_str_25, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_25, esp_mail_str_97, 0, 0, caseSensitive);
      bool con_type = false;
      if (tmp)
      {
        con_type = true;
        part.content_type = tmp;
        delP(&tmp);
        int p1 = strposP(part.content_type.c_str(), esp_mail_imap_composite_media_type_t::multipart, 0, caseSensitive);
        if (p1 != -1)
        {
          p1 += strlen(esp_mail_imap_composite_media_type_t::multipart) + 1;
          part.multipart = true;
          //inline or embedded images
          if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::related, p1, caseSensitive) != -1)
            part.multipart_sub_type = esp_mail_imap_multipart_sub_type_related;
          //multiple text formats e.g. plain, html, enriched
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::alternative, p1, caseSensitive) != -1)
            part.multipart_sub_type = esp_mail_imap_multipart_sub_type_alternative;
          //medias
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::parallel, p1, caseSensitive) != -1)
            part.multipart_sub_type = esp_mail_imap_multipart_sub_type_parallel;
          //rfc822 encapsulated
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::digest, p1, caseSensitive) != -1)
            part.multipart_sub_type = esp_mail_imap_multipart_sub_type_digest;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::report, p1, caseSensitive) != -1)
            part.multipart_sub_type = esp_mail_imap_multipart_sub_type_report;
          //others can be attachments
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::mixed, p1, caseSensitive) != -1)
            part.multipart_sub_type = esp_mail_imap_multipart_sub_type_mixed;
        }

        p1 = strposP(part.content_type.c_str(), esp_mail_imap_composite_media_type_t::message, 0, caseSensitive);
        if (p1 != -1)
        {
          p1 += strlen(esp_mail_imap_composite_media_type_t::message) + 1;
          if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::rfc822, p1, caseSensitive) != -1)
            part.message_sub_type = esp_mail_imap_message_sub_type_rfc822;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::Partial, p1, caseSensitive) != -1)
            part.message_sub_type = esp_mail_imap_message_sub_type_partial;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::External_Body, p1, caseSensitive) != -1)
            part.message_sub_type = esp_mail_imap_message_sub_type_external_body;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::delivery_status, p1, caseSensitive) != -1)
            part.message_sub_type = esp_mail_imap_message_sub_type_delivery_status;
        }

        p1 = strpos(part.content_type.c_str(), esp_mail_imap_descrete_media_type_t::text, 0, caseSensitive);
        if (p1 != -1)
        {
          p1 += strlen(esp_mail_imap_descrete_media_type_t::text) + 1;
          if (strpos(part.content_type.c_str(), esp_mail_imap_media_text_sub_type_t::plain, p1, caseSensitive) != -1)
            part.msg_type = esp_mail_msg_type_plain;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_media_text_sub_type_t::enriched, p1, caseSensitive) != -1)
            part.msg_type = esp_mail_msg_type_enriched;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_media_text_sub_type_t::html, p1, caseSensitive) != -1)
            part.msg_type = esp_mail_msg_type_html;
          else
            part.msg_type = esp_mail_msg_type_plain;
        }
      }

      if (con_type)
      {
        if (part.msg_type == esp_mail_msg_type_plain || part.msg_type == esp_mail_msg_type_enriched)
        {
          tmp = subStr(buf, esp_mail_str_168, esp_mail_str_136, 0, 0, caseSensitive);
          if (tmp)
          {
            part.charset = tmp;
            delP(&tmp);
          }
          else
          {
            tmp = subStr(buf, esp_mail_str_169, NULL, 0, -1, caseSensitive);
            if (tmp)
            {
              part.charset = tmp;
              delP(&tmp);
            }
          }

          if (strposP(buf, esp_mail_str_275, 0, caseSensitive) > -1 || strposP(buf, esp_mail_str_270, 0, caseSensitive) > -1)
            part.plain_flowed = true;
          if (strposP(buf, esp_mail_str_259, 0, caseSensitive) > -1 || strposP(buf, esp_mail_str_257, 0, caseSensitive) > -1)
            part.plain_delsp = true;
        }

        if (part.charset.length() == 0)
        {
          tmp = subStr(buf, esp_mail_str_168, esp_mail_str_136, 0, 0, caseSensitive);
          if (tmp)
          {
            part.charset = tmp;
            delP(&tmp);
          }
          else
          {
            tmp = subStr(buf, esp_mail_str_169, NULL, 0, -1, caseSensitive);
            if (tmp)
            {
              part.charset = tmp;
              delP(&tmp);
            }
          }
        }

        tmp = subStr(buf, esp_mail_str_170, esp_mail_str_136, 0, 0, caseSensitive);
        if (tmp)
        {
          part.name = tmp;
          delP(&tmp);
        }
        else
        {
          tmp = subStr(buf, esp_mail_str_171, NULL, 0, -1, caseSensitive);
          if (tmp)
          {
            part.name = tmp;
            delP(&tmp);
          }
        }
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_172, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_172, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.content_transfer_encoding = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_174, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_174, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.descr = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_175, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_175, esp_mail_str_97, 0, 0, caseSensitive);
      if (tmp)
      {

        //don't count altenative part text and html as embedded contents
        if (cHeader(imap)->multipart_sub_type != esp_mail_imap_multipart_sub_type_alternative)
        {
          part.content_disposition = tmp;
          if (caseSensitive)
          {
            if (strcmp(tmp, esp_mail_imap_content_disposition_type_t::attachment) == 0)
              part.attach_type = esp_mail_att_type_attachment;
            else if (strcmp(tmp, esp_mail_imap_content_disposition_type_t::inline_) == 0)
              part.attach_type = esp_mail_att_type_inline;
          }
          else
          {
            if (strcasecmp(tmp, esp_mail_imap_content_disposition_type_t::attachment) == 0)
              part.attach_type = esp_mail_att_type_attachment;
            else if (strcasecmp(tmp, esp_mail_imap_content_disposition_type_t::inline_) == 0)
              part.attach_type = esp_mail_att_type_inline;
          }
        }
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_150, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_150, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.sender = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_10, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_10, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.from = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_11, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_11, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.to = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_12, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_12, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.cc = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_184, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_184, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.reply_to = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_134, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_134, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.comments = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_24, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_24, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.subject = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_101, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_101, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.messageID = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_46, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_46, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.return_path = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_99, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_99, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.date = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_145, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_145, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.keywords = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_109, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_109, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.in_reply_to = tmp;
        delP(&tmp);
      }
    }
    else if (strcmpP(buf, 0, esp_mail_str_107, caseSensitive))
    {
      tmp = subStr(buf, esp_mail_str_107, NULL, 0, -1, caseSensitive);
      if (tmp)
      {
        part.rfc822_header.references = tmp;
        delP(&tmp);
      }
    }

    if (part.content_disposition.length() > 0)
    {
      tmp = subStr(buf, esp_mail_str_176, esp_mail_str_136, 0, caseSensitive);
      if (tmp)
      {
        MBSTRING s = tmp;
        decodeHeader(s);
        part.filename = s;
        delP(&tmp);
      }
      else
      {
        tmp = subStr(buf, esp_mail_str_177, NULL, 0, -1, caseSensitive);
        if (tmp)
        {
          MBSTRING s = tmp;
          decodeHeader(s);
          part.filename = s;
          delP(&tmp);
        }
      }

      tmp = subStr(buf, esp_mail_str_178, esp_mail_str_97, 0, 0, caseSensitive);
      if (tmp)
      {
        part.attach_data_size = atoi(tmp);
        delP(&tmp);
        cHeader(imap)->total_attach_data_size += part.attach_data_size;
        part.sizeProp = true;
      }
      else
      {
        tmp = subStr(buf, esp_mail_str_178, NULL, 0, -1, caseSensitive);
        if (tmp)
        {
          part.attach_data_size = atoi(tmp);
          delP(&tmp);
          cHeader(imap)->total_attach_data_size += part.attach_data_size;
          part.sizeProp = true;
        }
      }

      tmp = subStr(buf, esp_mail_str_179, esp_mail_str_136, 0, 0, caseSensitive);
      if (tmp)
      {
        part.creation_date = tmp;
        delP(&tmp);
      }
      else
      {
        tmp = subStr(buf, esp_mail_str_180, NULL, 0, -1, caseSensitive);
        if (tmp)
        {
          part.creation_date = tmp;
          delP(&tmp);
        }
      }

      tmp = subStr(buf, esp_mail_str_181, esp_mail_str_136, 0, 0, caseSensitive);
      if (tmp)
      {
        part.modification_date = tmp;
        delP(&tmp);
      }
      else
      {
        tmp = subStr(buf, esp_mail_str_182, NULL, 0, -1, caseSensitive);
        if (tmp)
        {
          part.modification_date = tmp;
          delP(&tmp);
        }
      }
    }

    chunkIdx++;
  }
}

void ESP_Mail_Client::closeTCPSession(IMAPSession *imap)
{

  if (imap->_tcpConnected)
  {
    if (imap->tcpClient.stream())
    {
      if (connected(imap))
      {
        imap->tcpClient.stream()->stop();
      }
    }
    _lastReconnectMillis = millis();
  }
  imap->_tcpConnected = false;
}

bool ESP_Mail_Client::reconnect(IMAPSession *imap, unsigned long dataTime, bool downloadRequest)
{

  bool status = WiFi.status() == WL_CONNECTED || ethLinkUp(imap->_sesson_cfg);

  if (dataTime > 0)
  {
    if (millis() - dataTime > imap->tcpClient.tcpTimeout)
    {

      closeTCPSession(imap);

      if (imap->_headers.size() > 0)
      {
        if (downloadRequest)
        {
          errorStatusCB(imap, IMAP_STATUS_ERROR_DOWNLAD_TIMEOUT);
          if (cHeader(imap)->part_headers.size() > 0)
            cPart(imap)->download_error = imap->errorReason().c_str();
        }
        else
        {
          errorStatusCB(imap, MAIL_CLIENT_ERROR_READ_TIMEOUT);
          cHeader(imap)->error_msg = imap->errorReason().c_str();
        }
      }
      return false;
    }
  }

  if (!status)
  {

    if (imap->_tcpConnected)
      closeTCPSession(imap);

    //defer the polling error report
    if (imap->_mbif._idleTimeMs > 0)
    {
      if (millis() - imap->_last_polling_error > 10000 && !imap->_tcpConnected)
      {
        imap->_last_polling_error = millis();
        // errorStatusCB(imap, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
      }
    }
    else
      errorStatusCB(imap, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);

    if (imap->_headers.size() > 0)
    {
      if (downloadRequest)
        cPart(imap)->download_error = imap->errorReason().c_str();
      else
        cHeader(imap)->error_msg = imap->errorReason().c_str();
    }

    if (millis() - _lastReconnectMillis > _reconnectTimeout && !imap->_tcpConnected)
    {
#if defined(ESP32)
      esp_wifi_connect();
#elif defined(ESP8266)
      WiFi.reconnect();
#endif
      _lastReconnectMillis = millis();
    }

    status = WiFi.status() == WL_CONNECTED || ethLinkUp(imap->_sesson_cfg);
  }

  return status;
}

int ESP_Mail_Client::available(IMAPSession *imap)
{
  int sz = 0;
  if (imap->tcpClient.stream())
    sz = imap->tcpClient.stream()->available();
  return sz;
}

bool ESP_Mail_Client::handleIMAPResponse(IMAPSession *imap, int errCode, bool closeSession)
{

  if (!reconnect(imap))
    return false;

  esp_mail_imap_response_status imapResp = esp_mail_imap_response_status::esp_mail_imap_resp_unknown;
  char *response = nullptr;
  int readLen = 0;
  long dataTime = millis();
  int chunkBufSize = available(imap);
  int chunkIdx = 0;
  MBSTRING s;
  bool completedResponse = false;
  bool endSearch = false;
  struct esp_mail_message_header_t header;
  struct esp_mail_message_part_info_t part;

  MBSTRING filePath = "";
  bool downloadRequest = false;
  int reportState = 0;
  int octetCount = 0;
  int octetLength = 0;
  int oCount = 0;
  bool tmo = false;
  int headerState = 0;
  int scnt = 0;
  int dcnt = -1;
  char *skey = nullptr;
  char *spc = nullptr;
  char *lastBuf = nullptr;
  char *tmp = nullptr;
  bool crLF = imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_text && strcmpP(cPart(imap)->content_transfer_encoding.c_str(), 0, esp_mail_str_31);

  while (imap->_tcpConnected && chunkBufSize <= 0)
  {
    if (!reconnect(imap, dataTime))
      return false;
    if (!connected(imap))
    {
      errorStatusCB(imap, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
      return false;
    }
    chunkBufSize = available(imap);
    delay(0);
  }

  dataTime = millis();

  if (chunkBufSize > 1)
  {
    if (imap->_imap_cmd == esp_mail_imap_cmd_examine)
    {
      imap->_mbif.clear();
      imap->_mbif._msgCount = 0;
      MBSTRING().swap(imap->_mbif._polling_status.argument);
      imap->_mbif._polling_status.messageNum = 0;
      imap->_mbif._polling_status.type = imap_polling_status_type_undefined;
      imap->_mbif._idleTimeMs = 0;
      imap->_nextUID = "";
    }

    if (imap->_imap_cmd == esp_mail_imap_cmd_search)
    {
      imap->_mbif._searchCount = 0;
      imap->_msgUID.clear();
    }

    chunkBufSize = 512;
    response = (char *)newP(chunkBufSize + 1);

    if (imap->_imap_cmd == esp_mail_imap_command::esp_mail_imap_cmd_search)
    {
      skey = strP(esp_mail_imap_response_6);
      spc = strP(esp_mail_str_92);
    }

    if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_attachment || imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_inline)
      lastBuf = (char *)newP(BASE64_CHUNKED_LEN + 1);

    while (!completedResponse)
    {
      delay(0);
      if (!reconnect(imap, dataTime) || !connected(imap))
      {
        if (imap->_imap_cmd == esp_mail_imap_command::esp_mail_imap_cmd_search)
        {
          delP(&skey);
          delP(&spc);
        }

        if (!connected(imap))
        {
          errorStatusCB(imap, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
          return false;
        }
        return false;
      }
      chunkBufSize = available(imap);

      if (chunkBufSize > 0)
      {
        chunkBufSize = 512;

        if (imap->_imap_cmd == esp_mail_imap_command::esp_mail_imap_cmd_search)
        {
          readLen = getMSGNUM(imap, response, chunkBufSize, chunkIdx, endSearch, scnt, skey, spc);
          imap->_mbif._availableItems = imap->_msgUID.size();
        }
        else
        {
          readLen = readLine(imap->tcpClient.stream(), response, chunkBufSize, crLF, octetCount);
        }

        if (readLen)
        {

          if (imap->_debugLevel > esp_mail_debug_level_1)
          {
            if (imap->_imap_cmd != esp_mail_imap_cmd_search && imap->_imap_cmd != esp_mail_imap_cmd_fetch_body_text && imap->_imap_cmd != esp_mail_imap_cmd_fetch_body_attachment && imap->_imap_cmd != esp_mail_imap_cmd_fetch_body_inline)
              esp_mail_debug((const char *)response);
          }

          if (imap->_imap_cmd != esp_mail_imap_cmd_search || (imap->_imap_cmd == esp_mail_imap_cmd_search && endSearch))
            imapResp = imapResponseStatus(imap, response);

          if (imapResp != esp_mail_imap_response_status::esp_mail_imap_resp_unknown)
          {

            if (imap->_debugLevel > esp_mail_debug_level_1)
            {
              if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_text || imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_attachment || imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_inline)
                esp_mail_debug((const char *)response);
            }

            if (imap->_imap_cmd == esp_mail_imap_cmd_custom && imap->_customCmdResCallback)
              imap->_customCmdResCallback((const char *)response);

            if (imap->_imap_cmd == esp_mail_imap_cmd_close)
              completedResponse = true;
            else
            {
              //some IMAP servers advertise CAPABILITY in their responses
              //try to read the next available response
              memset(response, 0, chunkBufSize);

              readLen = readLine(imap->tcpClient.stream(), response, chunkBufSize, true, octetCount);
              if (readLen)
              {
                completedResponse = false;
                imapResp = imapResponseStatus(imap, response);
                if (imapResp > esp_mail_imap_response_status::esp_mail_imap_resp_unknown)
                  completedResponse = true;
              }
              else
                completedResponse = true;
            }
          }
          else
          {
            if (imap->_imap_cmd == esp_mail_imap_cmd_auth)
            {
              if (authFailed(response, readLen, chunkIdx, 2))
                completedResponse = true;
            }
            else if (imap->_imap_cmd == esp_mail_imap_cmd_capability)
              handleCapability(imap, response, chunkIdx);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_list)
              handleFolders(imap, response);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_select || imap->_imap_cmd == esp_mail_imap_cmd_examine)
              handleExamine(imap, response);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_get_uid)
              handleGetUID(imap, response);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_get_flags)
              handleGetFlags(imap, response);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_custom && imap->_customCmdResCallback)
              imap->_customCmdResCallback((const char *)response);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_idle)
            {
              completedResponse = response[0] == '+';
              imapResp = esp_mail_imap_response_status::esp_mail_imap_resp_ok;
            }
            else if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_header)
            {
              if (headerState == 0)
              {
                char *tmp = intStr(cMSG(imap));
                header.message_uid = atoi(tmp);
                delP(&tmp);
              }
              int _st = headerState;
              handleHeader(imap, response, readLen, chunkIdx, header, headerState, octetCount, imap->_config->enable.header_case_sensitive);
              if (_st == headerState && headerState > 0 && octetCount <= header.header_data_len)
                setHeader(imap, response, header, headerState);
            }
            else if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_mime)
              handlePartHeader(imap, response, chunkIdx, part, imap->_config->enable.header_case_sensitive);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_text)
              decodeText(imap, response, readLen, chunkIdx, file, filePath, downloadRequest, octetLength, octetCount, dcnt);
            else if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_attachment || imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_inline)
            {

              if (strcmpP(cPart(imap)->content_transfer_encoding.c_str(), 0, esp_mail_str_31))
              {
                //multi-line chunked base64 string attachment handle
                if (octetCount < octetLength && readLen < BASE64_CHUNKED_LEN)
                {
                  if (strlen(lastBuf) > 0)
                  {
                    tmp = (char *)newP(readLen + strlen(lastBuf) + 2);
                    strcpy(tmp, lastBuf);
                    strcat(tmp, response);
                    readLen = strlen(tmp);
                    tmo = handleAttachment(imap, tmp, readLen, chunkIdx, file, filePath, downloadRequest, octetCount, octetLength, oCount, reportState, dcnt);
                    delP(&tmp);
                    memset(lastBuf, 0, BASE64_CHUNKED_LEN + 1);
                    if (!tmo)
                      break;
                  }
                  else if (readLen < BASE64_CHUNKED_LEN + 1)
                    strcpy(lastBuf, response);
                }
                else
                {
                  tmo = handleAttachment(imap, response, readLen, chunkIdx, file, filePath, downloadRequest, octetCount, octetLength, oCount, reportState, dcnt);
                  if (!tmo)
                    break;
                }
              }
              else
                tmo = handleAttachment(imap, response, readLen, chunkIdx, file, filePath, downloadRequest, octetCount, octetLength, oCount, reportState, dcnt);
            }
            dataTime = millis();
          }
        }
        memset(response, 0, chunkBufSize);
      }
    }

    delP(&response);
    if (imap->_imap_cmd == esp_mail_imap_command::esp_mail_imap_cmd_search)
    {
      if (imap->_debug && scnt > 0 && scnt < 100)
        searchReport(100, spc);
      delP(&skey);
      delP(&spc);
    }
    if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_attachment)
      delP(&lastBuf);
  }

  if ((imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_header && header.header_data_len == 0) || imapResp == esp_mail_imap_response_status::esp_mail_imap_resp_no)
  {
    if (imapResp == esp_mail_imap_response_status::esp_mail_imap_resp_no)
      imap->_imapStatus.statusCode = IMAP_STATUS_IMAP_RESPONSE_FAILED;
    else
      imap->_imapStatus.statusCode = IMAP_STATUS_NO_MESSAGE;

    if (imap->_readCallback)
    {
      MBSTRING s;
      appendP(s, esp_mail_str_53, true);
      s += imap->errorReason().c_str();
      imapCB(imap, s.c_str(), false);
    }

    if (imap->_debug)
    {
      MBSTRING s;
      appendP(s, esp_mail_str_185, true);
      s += imap->errorReason().c_str();
      esp_mail_debug_line(s.c_str(), true);
    }

    return false;
  }

  if (imapResp == esp_mail_imap_response_status::esp_mail_imap_resp_ok)
  {
    if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_header)
    {
      char *buf = (char *)newP(header.content_type.length() + 1);
      strcpy(buf, header.content_type.c_str());
      header.content_type.clear();

      tmp = subStr(buf, esp_mail_str_25, esp_mail_str_97, 0, 0, false);
      if (tmp)
      {
        headerState = esp_mail_imap_header_state::esp_mail_imap_state_content_type;
        setHeader(imap, tmp, header, headerState);
        delP(&tmp);

        int p1 = strposP(header.content_type.c_str(), esp_mail_imap_composite_media_type_t::multipart, 0);
        if (p1 != -1)
        {
          p1 += strlen(esp_mail_imap_composite_media_type_t::multipart) + 1;
          header.multipart = true;
          //inline or embedded images
          if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::related, p1) != -1)
            header.multipart_sub_type = esp_mail_imap_multipart_sub_type_related;
          //multiple text formats e.g. plain, html, enriched
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::alternative, p1) != -1)
            header.multipart_sub_type = esp_mail_imap_multipart_sub_type_alternative;
          //medias
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::parallel, p1) != -1)
            header.multipart_sub_type = esp_mail_imap_multipart_sub_type_parallel;
          //rfc822 encapsulated
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::digest, p1) != -1)
            header.multipart_sub_type = esp_mail_imap_multipart_sub_type_digest;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::report, p1) != -1)
            header.multipart_sub_type = esp_mail_imap_multipart_sub_type_report;
          //others can be attachments
          else if (strpos(part.content_type.c_str(), esp_mail_imap_multipart_sub_type_t::mixed, p1) != -1)
            header.multipart_sub_type = esp_mail_imap_multipart_sub_type_mixed;
        }

        p1 = strposP(header.content_type.c_str(), esp_mail_imap_composite_media_type_t::message, 0);
        if (p1 != -1)
        {
          p1 += strlen(esp_mail_imap_composite_media_type_t::message) + 1;
          if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::rfc822, p1) != -1)
          {
            header.rfc822_part = true;
            header.message_sub_type = esp_mail_imap_message_sub_type_rfc822;
          }
          else if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::Partial, p1) != -1)
            header.message_sub_type = esp_mail_imap_message_sub_type_partial;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::External_Body, p1) != -1)
            header.message_sub_type = esp_mail_imap_message_sub_type_external_body;
          else if (strpos(part.content_type.c_str(), esp_mail_imap_message_sub_type_t::delivery_status, p1) != -1)
            header.message_sub_type = esp_mail_imap_message_sub_type_delivery_status;
        }

        tmp = subStr(buf, esp_mail_str_169, NULL, 0, -1, false);
        if (tmp)
        {
          headerState = esp_mail_imap_header_state::esp_mail_imap_state_char_set;
          setHeader(imap, tmp, header, headerState);
          delP(&tmp);
        }

        if (header.multipart)
        {
          if (strcmpP(buf, 0, esp_mail_str_277))
          {
            tmp = subStr(buf, esp_mail_str_277, esp_mail_str_136, 0, 0, false);
            if (tmp)
            {
              headerState = esp_mail_imap_header_state::esp_mail_imap_state_boundary;
              setHeader(imap, tmp, header, headerState);
              delP(&tmp);
            }
          }
        }
      }

      delP(&buf);

      decodeHeader(header.header_fields.messageID);
      decodeHeader(header.header_fields.from);
      decodeHeader(header.header_fields.sender);
      decodeHeader(header.header_fields.to);
      decodeHeader(header.header_fields.cc);
      decodeHeader(header.header_fields.bcc);
      decodeHeader(header.header_fields.subject);
      decodeHeader(header.header_fields.date);
      decodeHeader(header.header_fields.return_path);
      decodeHeader(header.header_fields.reply_to);
      decodeHeader(header.header_fields.in_reply_to);
      decodeHeader(header.header_fields.references);
      decodeHeader(header.header_fields.comments);
      decodeHeader(header.header_fields.keywords);
      imap->_headers.push_back(header);
    }

    if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_mime)
    {
      //expect the octet length in the response for the existent part
      if (part.octetLen > 0)
      {

        part.partNumStr = cHeader(imap)->partNumStr;
        part.partNumFetchStr = cHeader(imap)->partNumStr;
        if (cHeader(imap)->part_headers.size() > 0)
        {

          struct esp_mail_message_part_info_t *_part = &cHeader(imap)->part_headers[cHeader(imap)->part_headers.size() - 1];
          bool rfc822_body_subtype = _part->message_sub_type == esp_mail_imap_message_sub_type_rfc822;

          if (rfc822_body_subtype)
          {
            if (!_part->rfc822_part)
            {
              //additional rfc822 message header, store it to the rfc822 part header
              _part->rfc822_part = true;
              _part->rfc822_header = part.rfc822_header;
              imap->_rfc822_part_count++;
              _part->rfc822_msg_Idx = imap->_rfc822_part_count;
            }
          }
        }

        cHeader(imap)->part_headers.push_back(part);
        cHeader(imap)->message_data_count = cHeader(imap)->part_headers.size();

        if (part.msg_type == esp_mail_msg_type_plain || part.msg_type == esp_mail_msg_type_enriched || part.msg_type == esp_mail_msg_type_html || part.attach_type == esp_mail_att_type_none || (part.attach_type == esp_mail_att_type_attachment && imap->_config->download.attachment) || (part.attach_type == esp_mail_att_type_inline && imap->_config->download.inlineImg))
        {
          if (part.message_sub_type != esp_mail_imap_message_sub_type_rfc822)
          {
            if (part.attach_type != esp_mail_att_type_none && cHeader(imap)->multipart_sub_type != esp_mail_imap_multipart_sub_type_alternative)
              cHeader(imap)->attachment_count++;
          }
        }
      }
      else
      {
        //nonexistent part
        //return false to exit the loop without closing the connection
        if (closeSession)
          imap->closeSession();
        return false;
      }
    }

    if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_attachment || imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_text || imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_inline)
    {
      if (cPart(imap)->file_open_write)
        file.close();
    }

    if (imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_text)
      cPart(imap)->text[cPart(imap)->textLen] = 0;
  }
  else
  {
    //some server responses NO and should exit (false) from MIME feching loop without
    //closing the session
    if (imap->_imap_cmd != esp_mail_imap_cmd_fetch_body_mime)
      return handleIMAPError(imap, errCode, false);

    if (closeSession)
      imap->closeSession();
    return false;
  }

  return true;
}

void ESP_Mail_Client::saveHeader(IMAPSession *imap)
{

  MBSTRING headerFilePath;
  prepareFilePath(imap, headerFilePath, true);
  if (imap->_config->storage.type == esp_mail_file_storage_type_sd && !_sdOk)
    _sdOk = sdTest();
  else if (imap->_config->storage.type == esp_mail_file_storage_type_flash && !_flashOk)
#if defined(ESP32)
    _flashOk = ESP_MAIL_FLASH_FS.begin(FORMAT_FLASH);
#elif defined(ESP8266)
    _flashOk = ESP_MAIL_FLASH_FS.begin();
#endif

  if (_sdOk || _flashOk)
  {
    if (file)
      file.close();

    if (imap->_config->storage.type == esp_mail_file_storage_type_sd)
      file = ESP_MAIL_SD_FS.open(headerFilePath.c_str(), FILE_WRITE);
    else if (imap->_config->storage.type == esp_mail_file_storage_type_flash)
#if defined(ESP32)
      file = ESP_MAIL_FLASH_FS.open(headerFilePath.c_str(), FILE_WRITE);
#elif defined(ESP8266)
      file = ESP_MAIL_FLASH_FS.open(headerFilePath.c_str(), "w");
#endif

    if (file)
    {
      MBSTRING s;

      appendP(s, esp_mail_str_276, true);
      appendP(s, esp_mail_str_131, false);
      file.print(s.c_str());
      file.println(cHeader(imap)->message_no);

      appendP(s, esp_mail_str_100, true);
      appendP(s, esp_mail_str_131, false);
      file.print(s.c_str());
      file.println(cHeader(imap)->message_uid);

      if (cHeader(imap)->header_fields.messageID.length() > 0)
      {
        appendP(s, esp_mail_str_101, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.messageID.c_str());
      }

      if (cHeader(imap)->accept_language.length() > 0)
      {
        appendP(s, esp_mail_str_102, true);
        file.print(s.c_str());
        file.println(cHeader(imap)->accept_language.c_str());
      }

      if (cHeader(imap)->content_language.length() > 0)
      {
        appendP(s, esp_mail_str_103, true);
        file.print(s.c_str());
        file.println(cHeader(imap)->content_language.c_str());
      }

      if (cHeader(imap)->header_fields.from.length() > 0)
      {
        appendP(s, esp_mail_str_10, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.from.c_str());
      }

      if (cHeader(imap)->header_fields.sender.length() > 0)
      {
        appendP(s, esp_mail_str_150, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.sender.c_str());
      }

      if (cHeader(imap)->header_fields.to.length() > 0)
      {
        appendP(s, esp_mail_str_11, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.to.c_str());
      }

      if (cHeader(imap)->header_fields.cc.length() > 0)
      {
        appendP(s, esp_mail_str_108, true);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.cc.c_str());
      }

      if (cHeader(imap)->header_fields.date.length() > 0)
      {
        appendP(s, esp_mail_str_99, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.date.c_str());
      }

      if (cHeader(imap)->header_fields.subject.length() > 0)
      {
        appendP(s, esp_mail_str_24, true);
        appendP(s, esp_mail_str_131, true);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.subject.c_str());
      }

      if (cHeader(imap)->header_fields.reply_to.length() > 0)
      {
        appendP(s, esp_mail_str_184, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.reply_to.c_str());
      }

      if (cHeader(imap)->header_fields.return_path.length() > 0)
      {
        appendP(s, esp_mail_str_46, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.return_path.c_str());
      }

      if (cHeader(imap)->header_fields.in_reply_to.length() > 0)
      {
        appendP(s, esp_mail_str_109, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.in_reply_to.c_str());
      }

      if (cHeader(imap)->header_fields.references.length() > 0)
      {
        appendP(s, esp_mail_str_107, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.references.c_str());
      }

      if (cHeader(imap)->header_fields.comments.length() > 0)
      {
        appendP(s, esp_mail_str_134, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.comments.c_str());
      }

      if (cHeader(imap)->header_fields.keywords.length() > 0)
      {
        appendP(s, esp_mail_str_145, true);
        appendP(s, esp_mail_str_131, false);
        file.print(s.c_str());
        file.println(cHeader(imap)->header_fields.keywords.c_str());
      }

      if (cHeader(imap)->attachment_count > 0)
      {

        appendP(s, esp_mail_str_113, true);
        file.print(s.c_str());
        file.println(cHeader(imap)->attachment_count);

        for (int j = 0; j < cHeader(imap)->attachment_count; j++)
        {
          if (imap->_headers[cIdx(imap)].part_headers[j].attach_type == esp_mail_att_type_none || imap->_headers[cIdx(imap)].part_headers[j].rfc822_part)
            continue;
          struct esp_mail_attachment_info_t att;
          att.filename = imap->_headers[cIdx(imap)].part_headers[j].filename.c_str();
          att.mime = imap->_headers[cIdx(imap)].part_headers[j].content_type.c_str();
          att.name = imap->_headers[cIdx(imap)].part_headers[j].name.c_str();
          att.size = imap->_headers[cIdx(imap)].part_headers[j].attach_data_size;
          att.creationDate = imap->_headers[cIdx(imap)].part_headers[j].creation_date.c_str();
          att.type = imap->_headers[cIdx(imap)].part_headers[j].attach_type;

          appendP(s, esp_mail_str_114, true);
          file.print(s.c_str());
          file.println(j + 1);

          appendP(s, esp_mail_str_115, true);
          file.print(s.c_str());
          file.println(att.filename);

          appendP(s, esp_mail_str_116, true);
          file.print(s.c_str());
          file.println(att.name);

          appendP(s, esp_mail_str_117, true);
          file.print(s.c_str());
          file.println(att.size);

          appendP(s, esp_mail_str_118, true);
          file.print(s.c_str());
          file.println(att.mime);

          appendP(s, esp_mail_str_119, true);
          file.print(s.c_str());
          file.println(att.creationDate);
        }
      }

      file.close();
    }
    imap->_headerSaved = true;
  }
}

esp_mail_imap_response_status ESP_Mail_Client::imapResponseStatus(IMAPSession *imap, char *response)
{
  imap->_imapStatus.text.clear();
  if (strposP(response, esp_mail_imap_response_1, 0) > -1)
    return esp_mail_imap_response_status::esp_mail_imap_resp_ok;
  else if (strposP(response, esp_mail_imap_response_2, 0) > -1)
  {
    imap->_imapStatus.text = response;
    imap->_imapStatus.text = imap->_imapStatus.text.substr(strlen_P(esp_mail_imap_response_2));
    return esp_mail_imap_response_status::esp_mail_imap_resp_no;
  }
  else if (strposP(response, esp_mail_imap_response_3, 0) > -1)
  {
    imap->_imapStatus.text = response;
    imap->_imapStatus.text = imap->_imapStatus.text.substr(strlen_P(esp_mail_imap_response_3));
    return esp_mail_imap_response_status::esp_mail_imap_resp_bad;
  }
  return esp_mail_imap_response_status::esp_mail_imap_resp_unknown;
}

void ESP_Mail_Client::handleCapability(IMAPSession *imap, char *buf, int &chunkIdx)
{
  if (chunkIdx == 0)
  {
    if (strposP(buf, esp_mail_imap_response_10, 0) > -1)
    {
      if (strposP(buf, esp_mail_imap_response_11, 0) > -1)
        imap->_auth_capability.login = true;
      if (strposP(buf, esp_mail_imap_response_12, 0) > -1)
        imap->_auth_capability.plain = true;
      if (strposP(buf, esp_mail_imap_response_13, 0) > -1)
        imap->_auth_capability.xoauth2 = true;
      if (strposP(buf, esp_mail_imap_response_14, 0) > -1)
        imap->_auth_capability.start_tls = true;
      if (strposP(buf, esp_mail_imap_response_15, 0) > -1)
        imap->_auth_capability.cram_md5 = true;
      if (strposP(buf, esp_mail_imap_response_16, 0) > -1)
        imap->_auth_capability.digest_md5 = true;
      if (strposP(buf, esp_mail_imap_response_17, 0) > -1)
        imap->_read_capability.idle = true;
      if (strposP(buf, esp_mail_imap_response_18, 0) > -1)
        imap->_read_capability.imap4 = true;
      if (strposP(buf, esp_mail_imap_response_19, 0) > -1)
        imap->_read_capability.imap4rev1 = true;
    }
  }
}

void ESP_Mail_Client::handleFolders(IMAPSession *imap, char *buf)
{
  struct esp_mail_folder_info_t fd;
  char *tmp = nullptr;
  int p1 = strposP(buf, esp_mail_imap_response_4, 0);
  int p2 = 0;
  if (p1 != -1)
  {
    p1 = strposP(buf, esp_mail_str_198, 0);
    if (p1 != -1)
    {
      p2 = strposP(buf, esp_mail_str_192, p1 + 1);
      if (p2 != -1)
      {
        tmp = (char *)newP(p2 - p1);
        strncpy(tmp, buf + p1 + 1, p2 - p1 - 1);
        if (tmp[p2 - p1 - 2] == '\r')
          tmp[p2 - p1 - 2] = 0;
        fd.attributes = tmp;
        delP(&tmp);
      }
    }

    p1 = strposP(buf, esp_mail_str_136, 0);
    if (p1 != -1)
    {
      p2 = strposP(buf, esp_mail_str_136, p1 + 1);
      if (p2 != -1)
      {
        tmp = (char *)newP(p2 - p1);
        strncpy(tmp, buf + p1 + 1, p2 - p1 - 1);
        if (tmp[p2 - p1 - 2] == '\r')
          tmp[p2 - p1 - 2] = 0;
        fd.delimiter = tmp;
        delP(&tmp);
      }
    }

    p1 = strposP(buf, esp_mail_str_131, p2);
    if (p1 != -1)
    {
      p2 = strlen(buf);
      tmp = (char *)newP(p2 - p1);
      if (buf[p1 + 1] == '"')
        p1++;
      strncpy(tmp, buf + p1 + 1, p2 - p1 - 1);
      if (tmp[p2 - p1 - 2] == '\r')
        tmp[p2 - p1 - 2] = 0;
      if (tmp[strlen(tmp) - 1] == '"')
        tmp[strlen(tmp) - 1] = 0;
      fd.name = tmp;
      delP(&tmp);
    }
    imap->_folders.add(fd);
  }
}

bool ESP_Mail_Client::handleIdle(IMAPSession *imap)
{

  int chunkBufSize = 0;

  if (!reconnect(imap))
    return false;

  if (imap->tcpClient.stream())
    chunkBufSize = imap->tcpClient.stream()->available();
  else
    return false;

  if (chunkBufSize > 0)
  {
    chunkBufSize = 512;

    char *buf = (char *)newP(chunkBufSize + 1);

    int octetCount = 0;

    int readLen = MailClient.readLine(imap->tcpClient.stream(), buf, chunkBufSize, false, octetCount);

    if (readLen > 0)
    {

      if (imap->_debugLevel > esp_mail_debug_level_1)
        esp_mail_debug((const char *)buf);

      char *tmp = nullptr;
      int p1 = -1;
      bool exists = false;

      p1 = strposP(buf, esp_mail_str_199, 0);
      if (p1 != -1)
      {
        int numMsg = imap->_mbif._msgCount;
        tmp = (char *)newP(p1);
        strncpy(tmp, buf + 2, p1 - 1);
        imap->_mbif._msgCount = atoi(tmp);
        delP(&tmp);
        exists = true;
        imap->_mbif._folderChanged |= (int)imap->_mbif._msgCount != numMsg;
        if ((int)imap->_mbif._msgCount > numMsg)
        {
          imap->_mbif._polling_status.type = imap_polling_status_type_new_message;
          imap->_mbif._polling_status.messageNum = imap->_mbif._msgCount;
        }
        goto ex;
      }

      p1 = strposP(buf, esp_mail_str_333, 0);
      if (p1 != -1)
      {
        imap->_mbif._polling_status.type = imap_polling_status_type_remove_message;
        tmp = (char *)newP(p1);
        strncpy(tmp, buf + 2, p1 - 1);
        imap->_mbif._polling_status.messageNum = atoi(tmp);

        if (imap->_mbif._polling_status.messageNum == imap->_mbif._msgCount && imap->_mbif._nextUID > 0)
          imap->_mbif._nextUID--;

        delP(&tmp);
        imap->_mbif._folderChanged = true;
        goto ex;
      }

      p1 = strposP(buf, esp_mail_str_334, 0);
      if (p1 != -1)
      {
        tmp = (char *)newP(p1);
        strncpy(tmp, buf + 2, p1 - 1);
        imap->_mbif._recentCount = atoi(tmp);
        delP(&tmp);
        goto ex;
      }

      p1 = strposP(buf, esp_mail_imap_response_7, 0);
      if (p1 != -1)
      {
        tmp = (char *)newP(p1);
        strncpy(tmp, buf + 2, p1 - 1);
        imap->_mbif._polling_status.messageNum = atoi(tmp);
        delP(&tmp);

        imap->_mbif._polling_status.argument = buf;
        imap->_mbif._polling_status.argument.erase(0, p1 + 8);
        imap->_mbif._polling_status.argument.pop_back();
        imap->_mbif._folderChanged = true;
        imap->_mbif._polling_status.type = imap_polling_status_type_fetch_message;
        goto ex;
      }

    ex:

      imap->_mbif._floderChangedState = (imap->_mbif._folderChanged && exists) || imap->_mbif._polling_status.type == imap_polling_status_type_fetch_message;
    }

    delP(&buf);
  }

  if (millis() - imap->_mbif._idleTimeMs > imap->_config->limit.imap_idle_timeout)
  {
    if (imap->mStopListen(true))
      return imap->mListen(true);
    return false;
  }

  return true;
}

void ESP_Mail_Client::handleGetUID(IMAPSession *imap, char *buf)
{
  char *tmp = nullptr;
  int p1 = strposP(buf, esp_mail_str_342, 0);
  imap->_uid_tmp = 0;
  if (p1 != -1)
  {
    tmp = (char *)newP(20);
    strncpy(tmp, buf + p1 + strlen_P(esp_mail_str_342), strlen(buf) - p1 - strlen_P(esp_mail_str_342) - 1);
    imap->_uid_tmp = atoi(tmp);
    delP(&tmp);
    return;
  }
}

void ESP_Mail_Client::handleGetFlags(IMAPSession *imap, char *buf)
{
  char *tmp = nullptr;
  int p1 = strposP(buf, esp_mail_str_112, 0);
  if (p1 != -1)
  {
    int len = strlen(buf) - p1 - strlen_P(esp_mail_str_112);
    tmp = (char *)newP(len);
    strncpy(tmp, buf + p1 + strlen_P(esp_mail_str_112), strlen(buf) - p1 - strlen_P(esp_mail_str_112) - 2);
    imap->_flags_tmp = tmp;
    delP(&tmp);
    return;
  }
}

void ESP_Mail_Client::handleExamine(IMAPSession *imap, char *buf)
{
  char *tmp = nullptr;
  int p1, p2;

  p1 = strposP(buf, esp_mail_str_199, 0);
  if (p1 != -1)
  {
    tmp = (char *)newP(p1);
    strncpy(tmp, buf + 2, p1 - 1);
    imap->_mbif._msgCount = atoi(tmp);
    delP(&tmp);
    return;
  }

  p1 = strposP(buf, esp_mail_str_334, 0);
  if (p1 != -1)
  {
    tmp = (char *)newP(p1);
    strncpy(tmp, buf + 2, p1 - 1);
    imap->_mbif._recentCount = atoi(tmp);
    delP(&tmp);
    return;
  }

  if (imap->_mbif._flags.size() == 0)
  {
    p1 = strposP(buf, esp_mail_imap_response_5, 0);
    if (p1 != -1)
    {
      p1 = strposP(buf, esp_mail_str_198, 0);
      if (p1 != -1)
      {
        p2 = strposP(buf, esp_mail_str_192, p1 + 1);
        if (p2 != -1)
        {
          tmp = (char *)newP(p2 - p1);
          strncpy(tmp, buf + p1 + 1, p2 - p1 - 1);
          char *stk = strP(esp_mail_str_131);
#if defined(ARDUINO_ARCH_SAMD)
          MBSTRING content = tmp;
          std::vector<MBSTRING> tokens = std::vector<MBSTRING>();
          splitTk(content, tokens, stk);
          for (size_t i = 0; i < tokens.size(); i++)
            imap->_mbif.addFlag(tokens[i].c_str());
#else

          char *end_token;
          char *token = strtok_r(tmp, stk, &end_token);
          while (token != NULL)
          {
            imap->_mbif.addFlag(token);
            token = strtok_r(NULL, stk, &end_token);
          }
          if (token)
            delP(&token);
#endif
          delP(&tmp);
          delP(&stk);
        }
      }
      return;
    }
  }

  if (imap->_nextUID.length() == 0)
  {
    p1 = strposP(buf, esp_mail_str_200, 0);
    if (p1 != -1)
    {
      p2 = strposP(buf, esp_mail_str_219, p1 + strlen_P(esp_mail_str_200));
      if (p2 != -1)
      {
        tmp = (char *)newP(p2 - p1 - strlen_P(esp_mail_str_200) + 1);
        strncpy(tmp, buf + p1 + strlen_P(esp_mail_str_200), p2 - p1 - strlen_P(esp_mail_str_200));
        imap->_nextUID = tmp;
        imap->_mbif._nextUID = atoi(tmp);
        delP(&tmp);
      }
      return;
    }
  }
}

bool ESP_Mail_Client::handleIMAPError(IMAPSession *imap, int err, bool ret)
{
  if (err < 0)
  {
    errorStatusCB(imap, err);

    if (imap->_headers.size() > 0)
    {
      if ((imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_attachment || imap->_imap_cmd == esp_mail_imap_cmd_fetch_body_inline) && (imap->_config->download.attachment || imap->_config->download.inlineImg))
      {
        if (cHeader(imap)->part_headers.size() > 0)
          cPart(imap)->download_error = imap->errorReason().c_str();
      }
      else
        cHeader(imap)->error_msg = imap->errorReason().c_str();

      cHeader(imap)->error = true;
    }
  }

  if (imap->_tcpConnected)
    closeTCPSession(imap);

  imap->_cbData.empty();

  return ret;
}

bool ESP_Mail_Client::handleAttachment(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, File &file, MBSTRING &filePath, bool &downloadRequest, int &octetCount, int &octetLength, int &oCount, int &reportState, int &downloadCount)
{
  if (chunkIdx == 0)
  {
    char *tmp = subStr(buf, esp_mail_str_193, esp_mail_str_194, 0);
    if (tmp)
    {
      octetCount = 2; //CRLF counted from first line
      octetLength = atoi(tmp);
      delP(&tmp);
      chunkIdx++;
      cHeader(imap)->total_download_size += octetLength;
    }
    return true;
  }

  if (octetLength == 0)
    return true;

  chunkIdx++;

  delay(0);

  if (!cPart(imap)->file_open_write)
  {

    cPart(imap)->file_open_write = true;

    if (imap->_config->storage.type == esp_mail_file_storage_type_sd && !_sdOk)
      _sdOk = sdTest();
    else if (imap->_config->storage.type == esp_mail_file_storage_type_flash && !_flashOk)
#if defined(ESP32)
      _flashOk = ESP_MAIL_FLASH_FS.begin(FORMAT_FLASH);
#elif defined(ESP8266)
      _flashOk = ESP_MAIL_FLASH_FS.begin();
#endif

    if (_sdOk || _flashOk)
    {

      downloadRequest = true;

      filePath.clear();
      filePath += imap->_config->storage.saved_path;
      appendP(filePath, esp_mail_str_202, false);

      char *tmp = intStr(cMSG(imap));
      filePath += tmp;
      delP(&tmp);

#if defined(ESP_MAIL_SD_FS)
      if (imap->_config->storage.type == esp_mail_file_storage_type_sd)
        if (!ESP_MAIL_SD_FS.exists(filePath.c_str()))
          createDirs(filePath);
#endif

      appendP(filePath, esp_mail_str_202, false);

      filePath += cPart(imap)->filename;

      if (imap->_config->storage.type == esp_mail_file_storage_type_sd)
#if defined(ESP_MAIL_SD_FS)
        file = ESP_MAIL_SD_FS.open(filePath.c_str(), FILE_WRITE);
#endif
      else if (imap->_config->storage.type == esp_mail_file_storage_type_flash)
      {
#if defined(ESP_MAIL_FLASH_FS)
#if defined(ESP32)
        file = ESP_MAIL_FLASH_FS.open(filePath.c_str(), FILE_WRITE);
#elif defined(ESP8266)
        file = ESP_MAIL_FLASH_FS.open(filePath.c_str(), "w");
#endif
#endif
      }
    }
  }

  if (_sdOk || _flashOk)
  {
    int nOctet = oCount + bufLen + 2;
    if (nOctet > octetLength)
    {
      if (imap->_readCallback)
        downloadReport(imap, 100);

      if (oCount < octetLength)
      {
        int dLen = nOctet - 2 - octetLength;
        bufLen -= dLen;
        buf[bufLen] = 0;
      }
      else
        return true;
    }

    oCount += bufLen + 2;

    if (strcmpP(cPart(imap)->content_transfer_encoding.c_str(), 0, esp_mail_str_31))
    {

      size_t olen = 0;
      unsigned char *decoded = decodeBase64((const unsigned char *)buf, bufLen, &olen);

      if (decoded)
      {

        if (!cPart(imap)->sizeProp)
        {
          cPart(imap)->attach_data_size += olen;
          cHeader(imap)->total_attach_data_size += cPart(imap)->attach_data_size;
        }

        file.write((const uint8_t *)decoded, olen);
        delay(0);
        delP(&decoded);

        if (imap->_config->enable.download_status)
        {
          int p = 0;
          if (cHeader(imap)->total_download_size > 0)
            p = 100 * octetCount / cHeader(imap)->total_download_size;

          if ((p != downloadCount) && (p <= 100))
          {
            downloadCount = p;
            if (imap->_readCallback && reportState != -1)
              downloadReport(imap, p);
            reportState = -1;
          }
          else
            reportState = 0;
        }
      }

      if (!reconnect(imap))
        return false;
    }
    else
    {
      //binary content
      if (!cPart(imap)->sizeProp)
      {
        cPart(imap)->attach_data_size += bufLen;
        cHeader(imap)->total_attach_data_size += cPart(imap)->attach_data_size;
      }

      file.write((const uint8_t *)buf, bufLen);
      delay(0);

      if (imap->_config->enable.download_status)
      {
        int p = 0;
        if (cHeader(imap)->total_download_size > 0)
          p = 100 * octetCount / cHeader(imap)->total_download_size;

        if ((p != downloadCount) && (p <= 100))
        {
          downloadCount = p;
          if (imap->_readCallback && reportState != -1)
            downloadReport(imap, p);
          reportState = -1;
        }
        else
          reportState = 0;
      }

      if (!reconnect(imap))
        return false;
    }
  }
  return true;
}

void ESP_Mail_Client::downloadReport(IMAPSession *imap, int progress)
{
  if (imap->_readCallback && progress % ESP_MAIL_PROGRESS_REPORT_STEP == 0)
  {
    MBSTRING s;
    char *tmp = intStr(progress);
    appendP(s, esp_mail_str_90, true);
    appendP(s, esp_mail_str_131, false);
    s += cPart(imap)->filename;
    appendP(s, esp_mail_str_91, false);
    s += tmp;
    delP(&tmp);
    appendP(s, esp_mail_str_92, false);
    appendP(s, esp_mail_str_34, false);
    esp_mail_debug_line(s.c_str(), false);
    MBSTRING().swap(s);
  }
}

void ESP_Mail_Client::fetchReport(IMAPSession *imap, int progress, bool download)
{
  if (imap->_readCallback && progress % ESP_MAIL_PROGRESS_REPORT_STEP == 0)
  {
    MBSTRING s;
    char *tmp = intStr(progress);
    if (download)
      appendP(s, esp_mail_str_90, true);
    else
      appendP(s, esp_mail_str_83, true);
    appendP(s, esp_mail_str_131, false);
    if (cPart(imap)->filename.length() > 0)
    {
      s += cPart(imap)->filename;
      appendP(s, esp_mail_str_91, false);
    }
    s += tmp;
    delP(&tmp);
    appendP(s, esp_mail_str_92, false);
    appendP(s, esp_mail_str_34, false);
    esp_mail_debug_line(s.c_str(), false);
    MBSTRING().swap(s);
  }
}

void ESP_Mail_Client::searchReport(int progress, const char *percent)
{
  if (progress % ESP_MAIL_PROGRESS_REPORT_STEP == 0)
  {
    char *tmp = intStr(progress);
    MBSTRING s;
    appendP(s, esp_mail_str_261, true);
    s += tmp;
    s += percent;
    appendP(s, esp_mail_str_34, false);
    esp_mail_debug_line(s.c_str(), false);
    delP(&tmp);
  }
}

int ESP_Mail_Client::cMSG(IMAPSession *imap)
{
  return imap->_msgUID[cIdx(imap)];
}

int ESP_Mail_Client::cIdx(IMAPSession *imap)
{
  return imap->_cMsgIdx;
}

void ESP_Mail_Client::decodeText(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, File &file, MBSTRING &filePath, bool &downloadRequest, int &octetLength, int &octetCount, int &readCount)
{
  bool rfc822_body_subtype = cPart(imap)->message_sub_type == esp_mail_imap_message_sub_type_rfc822;
  if (chunkIdx == 0)
  {
    char *tmp = subStr(buf, esp_mail_str_193, esp_mail_str_194, 0);
    if (tmp)
    {
      octetCount = 2;
      octetLength = atoi(tmp);
      delP(&tmp);
      chunkIdx++;
      cPart(imap)->octetLen = octetLength;

      if ((rfc822_body_subtype && imap->_config->download.rfc822) || (!rfc822_body_subtype && ((cPart(imap)->msg_type == esp_mail_msg_type_html && imap->_config->download.html) || ((cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched) && imap->_config->download.text))))
        prepareFilePath(imap, filePath, false);

      if (filePath.length() == 0)
      {
        if (!rfc822_body_subtype)
          appendP(filePath, esp_mail_str_67, false);
        else
        {
          appendP(filePath, esp_mail_str_82, false);
          appendP(filePath, esp_mail_str_131, false);
          appendP(filePath, esp_mail_str_67, false);
        }
      }
      cPart(imap)->filename = filePath;

      return;
    }
    else
    {
      if (imap->_debug)
      {
        char *tmp = strP(esp_mail_str_280);
        esp_mail_debug_line(tmp, false);
        delP(&tmp);
      }
    }
  }

  delay(0);

  if (octetLength == 0)
    return;

  if (imap->_config->download.rfc822 || imap->_config->download.html || imap->_config->download.text || (rfc822_body_subtype && imap->_config->enable.rfc822) || (!rfc822_body_subtype && ((cPart(imap)->msg_type == esp_mail_msg_type_html && imap->_config->enable.html) || ((cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched) && imap->_config->enable.text))))
  {
    if (imap->_readCallback && octetCount > octetLength + 2 && readCount < 100)
      fetchReport(imap, 100, (imap->_config->download.rfc822 && rfc822_body_subtype) || (!rfc822_body_subtype && ((cPart(imap)->msg_type == esp_mail_msg_type_html && imap->_config->download.html) || ((cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched) && imap->_config->download.text))));

    if (octetCount <= octetLength + 2)
    {
      size_t olen = 0;
      char *decoded = nullptr;
      bool newC = true;
      if (strcmpP(cPart(imap)->content_transfer_encoding.c_str(), 0, esp_mail_str_31))
      {
        decoded = (char *)decodeBase64((const unsigned char *)buf, bufLen, &olen);
      }
      else if (strcmpP(cPart(imap)->content_transfer_encoding.c_str(), 0, esp_mail_str_278))
      {
        decoded = (char *)newP(bufLen + 10);
        decodeQP(buf, decoded);
        olen = strlen(decoded);
      }
      else if (strcmpP(cPart(imap)->content_transfer_encoding.c_str(), 0, esp_mail_str_29))
      {
        decoded = decode7Bit(buf);
        olen = strlen(decoded);
      }
      else
      {
        //8bit and binary
        newC = false;
        decoded = buf;
        olen = bufLen;
      }

      if (decoded)
      {

        if ((rfc822_body_subtype && imap->_config->enable.rfc822) || (!rfc822_body_subtype && ((cPart(imap)->msg_type == esp_mail_msg_type_html && imap->_config->enable.html) || ((cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched) && imap->_config->enable.text))))
        {

          if (getEncodingFromCharset(cPart(imap)->charset.c_str()) == esp_mail_char_decoding_scheme_iso8859_1)
          {
            int ilen = olen;
            int olen2 = (ilen + 1) * 2;
            unsigned char *tmp = (unsigned char *)newP(olen2);
            decodeLatin1_UTF8(tmp, &olen2, (unsigned char *)decoded, &ilen);
            delP(&decoded);
            olen = olen2;
            decoded = (char *)tmp;
          }
          else if (getEncodingFromCharset(cPart(imap)->charset.c_str()) == esp_mail_char_decoding_scheme_tis620)
          {
            char *out = (char *)newP((olen + 1) * 3);
            delP(&decoded);
            decodeTIS620_UTF8(out, decoded, olen);
            olen = strlen(out);
            decoded = out;
          }

          int p = 0;

          if (octetLength > 0)
            p = 100 * octetCount / octetLength;

          if ((p != readCount) && (p <= 100))
          {
            readCount = p;
            if (imap->_readCallback)
              fetchReport(imap, p, (imap->_config->download.rfc822 && rfc822_body_subtype) || (!rfc822_body_subtype && ((cPart(imap)->msg_type == esp_mail_msg_type_html && imap->_config->download.html) || ((cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched) && imap->_config->download.text))));
          }

          if (cPart(imap)->text.length() < imap->_config->limit.msg_size)
          {

            if (cPart(imap)->text.length() + olen < imap->_config->limit.msg_size)
            {
              cPart(imap)->textLen += olen;
              cPart(imap)->text.append(decoded, olen);
            }
            else
            {
              int d = imap->_config->limit.msg_size - cPart(imap)->text.length();
              cPart(imap)->textLen += d;
              if (d > 0)
                cPart(imap)->text.append(decoded, d);
            }
          }
        }

        if (filePath.length() > 0)
        {
          if (!cPart(imap)->file_open_write)
          {
            cPart(imap)->file_open_write = true;

            if (_sdOk || _flashOk)
            {
              downloadRequest = true;

              if (imap->_config->storage.type == esp_mail_file_storage_type_sd)
              {
#if defined(ESP_MAIL_SD_FS)
                file = ESP_MAIL_SD_FS.open(filePath.c_str(), FILE_WRITE);
#endif
              }
              else if (imap->_config->storage.type == esp_mail_file_storage_type_flash)
              {
#if defined(ESP_MAIL_FLASH_FS)
#if defined(ESP32)
                file = ESP_MAIL_FLASH_FS.open(filePath.c_str(), FILE_WRITE);
#elif defined(ESP8266)
                file = ESP_MAIL_FLASH_FS.open(filePath.c_str(), "w");
#endif
#endif
              }
            }
          }

          if (_sdOk || _flashOk)
            file.write((const uint8_t *)decoded, olen);
        }

        if (newC)
          delP(&decoded);
      }
    }
  }
}
void ESP_Mail_Client::prepareFilePath(IMAPSession *imap, MBSTRING &filePath, bool header)
{
  bool rfc822_body_subtype = cPart(imap)->message_sub_type == esp_mail_imap_message_sub_type_rfc822;
  MBSTRING fpath = imap->_config->storage.saved_path;
  appendP(fpath, esp_mail_str_202, false);
  char *tmp = intStr(cMSG(imap));
  fpath += tmp;
  delP(&tmp);

  if (imap->_config->storage.type == esp_mail_file_storage_type_sd)
    if (!ESP_MAIL_SD_FS.exists(fpath.c_str()))
      createDirs(fpath);

  if (header)
  {
    appendP(fpath, esp_mail_str_203, false);
  }
  else
  {
    if (!rfc822_body_subtype)
    {
      appendP(fpath, esp_mail_str_161, false);
      if (cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched)
        appendP(fpath, esp_mail_str_95, false);
      else if (cPart(imap)->msg_type == esp_mail_msg_type_html)
        appendP(fpath, esp_mail_str_94, false);
    }
    else
    {
      appendP(fpath, esp_mail_str_163, false);

      if (cPart(imap)->rfc822_msg_Idx > 0)
      {
        char *tmp = intStr(cPart(imap)->rfc822_msg_Idx);
        fpath += tmp;
        delP(&tmp);
      }

      if (cPart(imap)->msg_type == esp_mail_msg_type_plain || cPart(imap)->msg_type == esp_mail_msg_type_enriched)
        appendP(fpath, esp_mail_str_95, false);
      else if (cPart(imap)->msg_type == esp_mail_msg_type_html)
        appendP(fpath, esp_mail_str_94, false);
      else
        //possible rfc822 encapsulated message which cannot fetch its header
        appendP(fpath, esp_mail_str_40, false);
    }
  }

  filePath = fpath;
}

IMAPSession::IMAPSession()
{
}
IMAPSession::~IMAPSession()
{
  empty();
#if defined(ESP32) || defined(ESP8266)
  _caCert.reset();
  _caCert = nullptr;
#endif
}

bool IMAPSession::closeSession()
{
  if (!_tcpConnected)
    return false;

  if (_mbif._idleTimeMs > 0)
    mStopListen(false);

#if defined(ESP32)
  /** 
   * The strange behavior in ESP8266 SSL client, BearSSLWiFiClientSecure
   * The client disposed without memory released after the server close 
   * the connection due to LOGOUT command, which caused the memory leaks.
  */
  if (!MailClient.imapLogout(this))
    return false;
#endif
  return MailClient.handleIMAPError(this, 0, true);
}

bool IMAPSession::connect(ESP_Mail_Session *sesssion, IMAP_Config *config)
{
  if (_tcpConnected)
    MailClient.closeTCPSession(this);

  _sesson_cfg = sesssion;
  _config = config;

#if defined(ESP32) || defined(ESP8266)

  _caCert = nullptr;

  if (strlen(_sesson_cfg->certificate.cert_data) > 0)
    _caCert = std::shared_ptr<const char>(_sesson_cfg->certificate.cert_data);

  if (strlen(_sesson_cfg->certificate.cert_file) > 0)
  {
    if (_sesson_cfg->certificate.cert_file_storage_type == esp_mail_file_storage_type::esp_mail_file_storage_type_sd && !MailClient._sdOk)
      MailClient._sdOk = MailClient.sdTest();
    if (_sesson_cfg->certificate.cert_file_storage_type == esp_mail_file_storage_type::esp_mail_file_storage_type_flash && !MailClient._flashOk)
#if defined(ESP32)
      MailClient._flashOk = ESP_MAIL_FLASH_FS.begin(FORMAT_FLASH);
#elif defined(ESP8266)
      MailClient._flashOk = ESP_MAIL_FLASH_FS.begin();
#endif
  }

#endif

  return MailClient.imapAuth(this);
}

void IMAPSession::debug(int level)
{
  if (level > esp_mail_debug_level_0)
  {
    if (level > esp_mail_debug_level_3)
      level = esp_mail_debug_level_1;
    _debugLevel = level;
    _debug = true;
  }
  else
  {
    _debugLevel = esp_mail_debug_level_0;
    _debug = false;
  }
}

String IMAPSession::errorReason()
{
  MBSTRING ret;

  if (_imapStatus.text.length() > 0)
    return _imapStatus.text.c_str();

  switch (_imapStatus.statusCode)
  {
  case IMAP_STATUS_SERVER_CONNECT_FAILED:
    MailClient.appendP(ret, esp_mail_str_38, true);
    break;
  case MAIL_CLIENT_ERROR_CONNECTION_CLOSED:
    MailClient.appendP(ret, esp_mail_str_221, true);
    break;
  case MAIL_CLIENT_ERROR_READ_TIMEOUT:
    MailClient.appendP(ret, esp_mail_str_258, true);
    break;
  case MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED:
    MailClient.appendP(ret, esp_mail_str_305, true);
    break;
  case IMAP_STATUS_NO_MESSAGE:
    MailClient.appendP(ret, esp_mail_str_306, true);
    break;
  case IMAP_STATUS_ERROR_DOWNLAD_TIMEOUT:
    MailClient.appendP(ret, esp_mail_str_93, true);
    break;
  case MAIL_CLIENT_ERROR_SSL_TLS_STRUCTURE_SETUP:
    MailClient.appendP(ret, esp_mail_str_132, true);
    break;
  case IMAP_STATUS_CLOSE_MAILBOX_FAILED:
    MailClient.appendP(ret, esp_mail_str_188, true);
    break;
  case IMAP_STATUS_OPEN_MAILBOX_FAILED:
    MailClient.appendP(ret, esp_mail_str_281, true);
    break;
  case IMAP_STATUS_LIST_MAILBOXS_FAILED:
    MailClient.appendP(ret, esp_mail_str_62, true);
    break;
  case IMAP_STATUS_NO_SUPPORTED_AUTH:
    MailClient.appendP(ret, esp_mail_str_42, true);
    break;
  case IMAP_STATUS_CHECK_CAPABILITIES_FAILED:
    MailClient.appendP(ret, esp_mail_str_63, true);
    break;
  case MAIL_CLIENT_ERROR_OUT_OF_MEMORY:
    MailClient.appendP(ret, esp_mail_str_186, true);
    break;
  case IMAP_STATUS_NO_MAILBOX_FOLDER_OPENED:
    MailClient.appendP(ret, esp_mail_str_153, true);
    break;

  default:
    break;
  }
  return ret.c_str();
}

bool IMAPSession::selectFolder(const char *folderName, bool readOnly)
{
  if (_tcpConnected)
  {
    if (!openFolder(folderName, readOnly))
      return false;
  }
  else
  {
    _currentFolder = folderName;
  }
  return true;
}

bool IMAPSession::openFolder(const char *folderName, bool readOnly)
{
  if (!_tcpConnected)
    return false;
  if (readOnly)
    return openMailbox(folderName, esp_mail_imap_auth_mode::esp_mail_imap_mode_examine, true);
  else
    return openMailbox(folderName, esp_mail_imap_auth_mode::esp_mail_imap_mode_select, true);
}

bool IMAPSession::getFolders(FoldersCollection &folders)
{
  if (!_tcpConnected)
    return false;
  return getMailboxes(folders);
}

bool IMAPSession::closeFolder(const char *folderName)
{
  if (!_tcpConnected)
    return false;
  return closeMailbox();
}

bool IMAPSession::mListen(bool recon)
{
  //no folder opened or IDLE was not supported
  if (_currentFolder.length() == 0 || !_read_capability.idle)
  {
    _mbif._floderChangedState = false;
    _mbif._folderChanged = false;
    return false;
  }

  if (!MailClient.reconnect(this))
    return false;

  if (!_tcpConnected)
  {
    if (!_sesson_cfg || !_config)
      return false;

    //re-authenticate after session closed
    if (!MailClient.imapAuth(this))
    {
      MailClient.closeTCPSession(this);
      return false;
    }

    //re-open folder
    if (!selectFolder(_currentFolder.c_str()))
      return false;
  }

  if (_mbif._idleTimeMs == 0)
  {
    _mbif._polling_status.messageNum = 0;
    _mbif._polling_status.type = imap_polling_status_type_undefined;
    MBSTRING().swap(_mbif._polling_status.argument);
    _mbif._recentCount = 0;
    _mbif._folderChanged = false;

    MBSTRING s;

    if (!recon)
    {

      if (_readCallback)
      {
        MailClient.appendP(s, esp_mail_str_336, true);
        s += _currentFolder;
        MailClient.appendP(s, esp_mail_str_337, false);
        MailClient.imapCB(this, "", false);
        MailClient.imapCB(this, s.c_str(), false);
      }

      if (_debug)
        MailClient.debugInfoP(esp_mail_str_335);
    }

    if (MailClient.imapSendP(this, esp_mail_str_331, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_idle;
    if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
      return false;

    if (!recon)
    {

      if (_readCallback)
      {
        MailClient.appendP(s, esp_mail_str_338, true);
        MailClient.imapCB(this, s.c_str(), false);
      }

      if (_debug)
        MailClient.debugInfoP(esp_mail_str_339);
    }

    _mbif._idleTimeMs = millis();
  }
  else
  {

    if (_mbif._floderChangedState)
    {
      _mbif._floderChangedState = false;
      _mbif._folderChanged = false;
      _mbif._polling_status.messageNum = 0;
      _mbif._polling_status.type = imap_polling_status_type_undefined;
      MBSTRING().swap(_mbif._polling_status.argument);
      _mbif._recentCount = 0;
    }

    return MailClient.handleIdle(this);
  }

  return true;
}

bool IMAPSession::mStopListen(bool recon)
{
  _mbif._idleTimeMs = 0;
  _mbif._floderChangedState = false;
  _mbif._folderChanged = false;
  _mbif._polling_status.messageNum = 0;
  _mbif._polling_status.type = imap_polling_status_type_undefined;
  MBSTRING().swap(_mbif._polling_status.argument);
  _mbif._recentCount = 0;

  if (!_tcpConnected || _currentFolder.length() == 0 || !_read_capability.idle)
    return false;

  if (MailClient.imapSendP(this, esp_mail_str_332, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_done;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
    return false;

  if (!recon)
  {
    if (_readCallback)
    {
      MBSTRING s;
      MailClient.appendP(s, esp_mail_str_340, true);
      MailClient.imapCB(this, s.c_str(), false);
    }

    if (_debug)
      MailClient.debugInfoP(esp_mail_str_341);
  }

  return true;
}

bool IMAPSession::folderChanged()
{
  return _mbif._floderChangedState;
}

void IMAPSession::checkUID()
{
  if (MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_140) || MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_212) ||
      MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_213) || MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_214) || MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_215) ||
      MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_216) || MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_217) || MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_218) ||
      MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_219) || MailClient.strcmpP(_config->fetch.uid, 0, esp_mail_str_220))
    _config->fetch.uid = "*";
}

void IMAPSession::checkPath()
{
  MBSTRING path = _config->storage.saved_path;
  if (path[0] != '/')
  {
    path = "/";
    path += _config->storage.saved_path;
    path = path.c_str();
  }
}

bool IMAPSession::headerOnly()
{
  return _headerOnly;
}

struct esp_mail_imap_msg_list_t IMAPSession::data()
{
  struct esp_mail_imap_msg_list_t ret;

  for (size_t i = 0; i < _headers.size(); i++)
  {
    if (MailClient.getFreeHeap() < ESP_MAIL_MIN_MEM)
      continue;

    struct esp_mail_imap_msg_item_t itm;

    itm.UID = _headers[i].message_uid;
    itm.msgNo = _headers[i].message_no;
    itm.ID = _headers[i].header_fields.messageID.c_str();
    itm.from = _headers[i].header_fields.from.c_str();
    itm.sender = _headers[i].header_fields.sender.c_str();
    itm.to = _headers[i].header_fields.to.c_str();
    itm.cc = _headers[i].header_fields.cc.c_str();
    itm.subject = _headers[i].header_fields.subject.c_str();
    itm.date = _headers[i].header_fields.date.c_str();
    itm.return_path = _headers[i].header_fields.return_path.c_str();
    itm.reply_to = _headers[i].header_fields.reply_to.c_str();
    itm.in_reply_to = _headers[i].header_fields.in_reply_to.c_str();
    itm.references = _headers[i].header_fields.references.c_str();
    itm.comments = _headers[i].header_fields.comments.c_str();
    itm.keywords = _headers[i].header_fields.keywords.c_str();
    itm.flags = _headers[i].flags.c_str();
    itm.acceptLang = _headers[i].accept_language.c_str();
    itm.contentLang = _headers[i].content_language.c_str();
    itm.hasAttachment = _headers[i].hasAttachment;
    itm.fetchError = _headers[i].error_msg.c_str();

    getMessages(i, itm);

    getRFC822Messages(i, itm);

    ret.msgItems.push_back(itm);
  }

  return ret;
}

SelectedFolderInfo IMAPSession::selectedFolder()
{
  return _mbif;
}

void IMAPSession::callback(imapStatusCallback imapCallback)
{
  _readCallback = std::move(imapCallback);
}

void IMAPSession::getMessages(uint16_t messageIndex, struct esp_mail_imap_msg_item_t &msg)
{
  msg.text.content = "";
  msg.text.charSet = "";
  msg.text.content_type = "";
  msg.text.transfer_encoding = "";
  msg.html.content = "";
  msg.html.charSet = "";
  msg.html.content_type = "";
  msg.html.transfer_encoding = "";

  if (messageIndex < _headers.size())
  {
    int sz = _headers[messageIndex].part_headers.size();
    if (sz > 0)
    {
      for (int i = 0; i < sz; i++)
      {
        if (!_headers[messageIndex].part_headers[i].rfc822_part && _headers[messageIndex].part_headers[i].message_sub_type != esp_mail_imap_message_sub_type_rfc822)
        {
          if (_headers[messageIndex].part_headers[i].attach_type == esp_mail_att_type_none)
          {
            if (_headers[messageIndex].part_headers[i].msg_type == esp_mail_msg_type_plain || _headers[messageIndex].part_headers[i].msg_type == esp_mail_msg_type_enriched)
            {
              msg.text.content = _headers[messageIndex].part_headers[i].text.c_str();
              msg.text.charSet = _headers[messageIndex].part_headers[i].charset.c_str();
              msg.text.content_type = _headers[messageIndex].part_headers[i].content_type.c_str();
              msg.text.transfer_encoding = _headers[messageIndex].part_headers[i].content_transfer_encoding.c_str();
            }

            if (_headers[messageIndex].part_headers[i].msg_type == esp_mail_msg_type_html)
            {
              msg.html.content = _headers[messageIndex].part_headers[i].text.c_str();
              msg.html.charSet = _headers[messageIndex].part_headers[i].charset.c_str();
              msg.html.content_type = _headers[messageIndex].part_headers[i].content_type.c_str();
              msg.html.transfer_encoding = _headers[messageIndex].part_headers[i].content_transfer_encoding.c_str();
            }
          }
          else
          {
            struct esp_mail_attachment_info_t att;
            att.filename = _headers[messageIndex].part_headers[i].filename.c_str();
            att.mime = _headers[messageIndex].part_headers[i].content_type.c_str();
            att.name = _headers[messageIndex].part_headers[i].name.c_str();
            att.size = _headers[messageIndex].part_headers[i].attach_data_size;
            att.creationDate = _headers[messageIndex].part_headers[i].creation_date.c_str();
            att.type = _headers[messageIndex].part_headers[i].attach_type;
            msg.attachments.push_back(att);
          }
        }
      }
    }
  }
}

void IMAPSession::getRFC822Messages(uint16_t messageIndex, struct esp_mail_imap_msg_item_t &msg)
{
  if (messageIndex < _headers.size())
  {
    int sz = _headers[messageIndex].part_headers.size();
    int partIdx = 0;
    int cIdx = 0;
    IMAP_MSG_Item *_rfc822 = nullptr;
    if (sz > 0)
    {
      for (int i = 0; i < sz; i++)
      {
        if (_headers[messageIndex].part_headers[i].message_sub_type == esp_mail_imap_message_sub_type_rfc822)
        {
          if (_headers[messageIndex].part_headers[i].rfc822_part)
          {
            if (partIdx > 0)
              msg.rfc822.push_back(*_rfc822);
            cIdx = i;
            partIdx++;
            _rfc822 = new IMAP_MSG_Item();

            _rfc822->from = _headers[messageIndex].part_headers[i].rfc822_header.from.c_str();
            _rfc822->sender = _headers[messageIndex].part_headers[i].rfc822_header.sender.c_str();
            _rfc822->to = _headers[messageIndex].part_headers[i].rfc822_header.to.c_str();
            _rfc822->cc = _headers[messageIndex].part_headers[i].rfc822_header.cc.c_str();
            _rfc822->return_path = _headers[messageIndex].part_headers[i].rfc822_header.return_path.c_str();
            _rfc822->reply_to = _headers[messageIndex].part_headers[i].rfc822_header.reply_to.c_str();
            _rfc822->subject = _headers[messageIndex].part_headers[i].rfc822_header.subject.c_str();
            _rfc822->comments = _headers[messageIndex].part_headers[i].rfc822_header.comments.c_str();
            _rfc822->keywords = _headers[messageIndex].part_headers[i].rfc822_header.keywords.c_str();
            _rfc822->in_reply_to = _headers[messageIndex].part_headers[i].rfc822_header.in_reply_to.c_str();
            _rfc822->references = _headers[messageIndex].part_headers[i].rfc822_header.references.c_str();
            _rfc822->date = _headers[messageIndex].part_headers[i].rfc822_header.date.c_str();
            _rfc822->ID = _headers[messageIndex].part_headers[i].rfc822_header.messageID.c_str();
            _rfc822->flags = _headers[messageIndex].part_headers[i].rfc822_header.flags.c_str();
            _rfc822->text.charSet = "";
            _rfc822->text.content_type = "";
            _rfc822->text.transfer_encoding = "";
            _rfc822->html.charSet = "";
            _rfc822->html.content_type = "";
            _rfc822->html.transfer_encoding = "";
          }
          else
          {
            if (MailClient.multipartMember(_headers[messageIndex].part_headers[cIdx].partNumStr, _headers[messageIndex].part_headers[i].partNumStr))
            {
              if (_headers[messageIndex].part_headers[i].attach_type == esp_mail_att_type_none)
              {
                if (_headers[messageIndex].part_headers[i].msg_type == esp_mail_msg_type_plain || _headers[messageIndex].part_headers[i].msg_type == esp_mail_msg_type_enriched)
                {
                  _rfc822->text.charSet = _headers[messageIndex].part_headers[i].charset.c_str();
                  _rfc822->text.content_type = _headers[messageIndex].part_headers[i].content_type.c_str();
                  _rfc822->text.content = _headers[messageIndex].part_headers[i].text.c_str();
                  _rfc822->text.transfer_encoding = _headers[messageIndex].part_headers[i].content_transfer_encoding.c_str();
                }
                if (_headers[messageIndex].part_headers[i].msg_type == esp_mail_msg_type_html)
                {
                  _rfc822->html.charSet = _headers[messageIndex].part_headers[i].charset.c_str();
                  _rfc822->html.content_type = _headers[messageIndex].part_headers[i].content_type.c_str();
                  _rfc822->html.content = _headers[messageIndex].part_headers[i].text.c_str();
                  _rfc822->html.transfer_encoding = _headers[messageIndex].part_headers[i].content_transfer_encoding.c_str();
                }
              }
              else
              {
                struct esp_mail_attachment_info_t att;
                att.filename = _headers[messageIndex].part_headers[i].filename.c_str();
                att.mime = _headers[messageIndex].part_headers[i].content_type.c_str();
                att.name = _headers[messageIndex].part_headers[i].name.c_str();
                att.size = _headers[messageIndex].part_headers[i].attach_data_size;
                att.creationDate = _headers[messageIndex].part_headers[i].creation_date.c_str();
                att.type = _headers[messageIndex].part_headers[i].attach_type;
                _rfc822->attachments.push_back(att);
              }
            }
          }
        }
      }

      if ((int)msg.rfc822.size() < partIdx && _rfc822 != nullptr)
        msg.rfc822.push_back(*_rfc822);
    }
  }
}

bool IMAPSession::closeMailbox()
{

  if (!MailClient.reconnect(this))
    return false;

  MBSTRING s;

  if (_readCallback)
  {
    MailClient.appendP(s, esp_mail_str_210, true);
    s += _currentFolder;
    MailClient.appendP(s, esp_mail_str_96, false);
    MailClient.imapCB(this, "", false);
    MailClient.imapCB(this, s.c_str(), false);
  }

  if (_debug)
    MailClient.debugInfoP(esp_mail_str_197);

  if (MailClient.imapSendP(this, esp_mail_str_195, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;
  _imap_cmd = esp_mail_imap_cmd_close;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_CLOSE_MAILBOX_FAILED, false))
    return false;

  _currentFolder.clear();
  _mailboxOpened = false;

  return true;
}

bool IMAPSession::openMailbox(const char *folder, esp_mail_imap_auth_mode mode, bool waitResponse)
{

  if (!MailClient.reconnect(this) || !folder)
    return false;

  if (strlen(folder) == 0)
    return false;

  //The SELECT/EXAMINE command automatically deselects any currently selected mailbox
  //before attempting the new selection (RFC3501 p.33)

  //folder should not close for re-selection otherwise the server returned * BAD Command Argument Error. 12

  bool sameFolder = strcmp(_currentFolder.c_str(), folder) == 0;

  //guards 5 seconds to prevent accidently frequently select the same folder with the same mode
  if (_mailboxOpened && sameFolder && millis() - _lastSameFolderOpenMillis < 5000)
  {
    if ((_readOnlyMode && mode == esp_mail_imap_mode_examine) || (!_readOnlyMode && mode == esp_mail_imap_mode_select))
      return true;
  }

  if (!sameFolder)
    _currentFolder = folder;

  MBSTRING s;
  if (_readCallback)
  {
    MailClient.appendP(s, esp_mail_str_61, true);
    s += _currentFolder;
    MailClient.appendP(s, esp_mail_str_96, false);
    MailClient.imapCB(this, "", false);
    MailClient.imapCB(this, s.c_str(), false);
  }

  if (_debug)
    MailClient.debugInfoP(esp_mail_str_248);

  if (mode == esp_mail_imap_mode_examine)
  {
    MailClient.appendP(s, esp_mail_str_135, true);
    _imap_cmd = esp_mail_imap_cmd_examine;
  }
  else if (mode == esp_mail_imap_mode_select)
  {
    MailClient.appendP(s, esp_mail_str_247, true);
    _imap_cmd = esp_mail_imap_cmd_select;
  }
  s += _currentFolder;
  MailClient.appendP(s, esp_mail_str_136, false);
  if (MailClient.imapSend(this, s.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  _lastSameFolderOpenMillis = millis();

  if (waitResponse)
  {

    if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_OPEN_MAILBOX_FAILED, false))
      return false;
  }

  if (mode == esp_mail_imap_mode_examine)
    _readOnlyMode = true;
  else if (mode == esp_mail_imap_mode_select)
    _readOnlyMode = false;

  _mailboxOpened = true;

  return true;
}

bool IMAPSession::getMailboxes(FoldersCollection &folders)
{
  _folders.clear();

  if (_readCallback)
  {
    MailClient.imapCB(this, "", false);
    MailClient.imapCBP(this, esp_mail_str_58, false);
  }

  if (_debug)
    MailClient.debugInfoP(esp_mail_str_230);

  if (MailClient.imapSendP(this, esp_mail_str_133, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;
  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_list;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_LIST_MAILBOXS_FAILED, false))
    return false;

  folders = _folders;
  return true;
}

bool IMAPSession::checkCapability()
{
  if (_readCallback)
  {
    MailClient.imapCB(this, "", false);
    MailClient.imapCBP(this, esp_mail_str_64, false);
  }

  if (_debug)
    MailClient.debugInfoP(esp_mail_str_65);

  if (MailClient.imapSendP(this, esp_mail_str_2, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_capability;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_CHECK_CAPABILITIES_FAILED, false))
    return false;

  return true;
}

bool IMAPSession::createFolder(const char *folderName)
{
  if (_debug)
  {
    esp_mail_debug("");
    MailClient.debugInfoP(esp_mail_str_320);
  }

  MBSTRING cmd;
  MailClient.appendP(cmd, esp_mail_str_322, true);
  cmd += folderName;

  if (MailClient.imapSend(this, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_create;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
    return false;

  return true;
}

int IMAPSession::getUID(int msgNum)
{
  if (_currentFolder.length() == 0)
    return 0;

  MBSTRING cmd;
  MailClient.appendP(cmd, esp_mail_str_143, true);
  char *tmp = MailClient.intStr(msgNum);
  cmd += tmp;
  MailClient.delP(&tmp);
  MailClient.appendP(cmd, esp_mail_str_138, false);

  MBSTRING s;

  if (_readCallback)
  {
    MailClient.appendP(s, esp_mail_str_156, true);
    MailClient.imapCB(this, s.c_str(), false);
  }

  if (_debug)
    MailClient.debugInfoP(esp_mail_str_189);

  if (MailClient.imapSend(this, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return 0;

  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_get_uid;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
    return 0;

  if (_readCallback || _debug)
  {

    char *num = MailClient.intStr(_uid_tmp);

    if (_readCallback)
    {
      MailClient.appendP(s, esp_mail_str_274, true);
      s += num;
      MailClient.imapCB(this, s.c_str(), false);
    }

    if (_debug)
    {
      MailClient.appendP(s, esp_mail_str_111, true);
      s += num;
      esp_mail_debug(s.c_str());
    }

    MailClient.delP(&num);
  }

  return _uid_tmp;
}

const char *IMAPSession::getFlags(int msgNum)
{
  MBSTRING().swap(_flags_tmp);
  if (_currentFolder.length() == 0)
    return _flags_tmp.c_str();

  MBSTRING cmd;
  MailClient.appendP(cmd, esp_mail_str_143, true);
  char *tmp = MailClient.intStr(msgNum);
  cmd += tmp;
  MailClient.delP(&tmp);
  MailClient.appendP(cmd, esp_mail_str_273, false);

  MBSTRING s;

  if (_readCallback)
  {
    MailClient.appendP(s, esp_mail_str_279, true);
    MailClient.imapCB(this, s.c_str(), false);
  }

  if (_debug)
    MailClient.debugInfoP(esp_mail_str_105);

  if (MailClient.imapSend(this, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return _flags_tmp.c_str();

  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_get_flags;
  MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false);

  return _flags_tmp.c_str();
}

bool IMAPSession::sendCustomCommand(const char *cmd, imapResponseCallback callback)
{
  if (_currentFolder.length() == 0)
    return false;

  _customCmdResCallback = std::move(callback);

  if (MailClient.imapSend(this, cmd, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_custom;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
    return false;

  return true;
}

bool IMAPSession::deleteFolder(const char *folderName)
{
  if (_debug)
  {
    esp_mail_debug("");
    MailClient.debugInfoP(esp_mail_str_321);
  }

  MBSTRING cmd;
  MailClient.appendP(cmd, esp_mail_str_323, true);
  cmd += folderName;

  if (MailClient.imapSend(this, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_delete;
  if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
    return false;

  return true;
}

bool IMAPSession::deleteMessages(MessageList *toDelete, bool expunge)
{
  if (toDelete->_list.size() > 0)
  {

    if (!selectFolder(_currentFolder.c_str(), false))
      return false;

    if (_debug)
    {
      esp_mail_debug("");
      MailClient.debugInfoP(esp_mail_str_316);
    }

    MBSTRING cmd;
    char *tmp = NULL;
    MailClient.appendP(cmd, esp_mail_str_249, true);
    for (size_t i = 0; i < toDelete->_list.size(); i++)
    {
      if (i > 0)
        MailClient.appendP(cmd, esp_mail_str_263, false);
      tmp = MailClient.intStr(toDelete->_list[i]);
      cmd += tmp;
      MailClient.delP(&tmp);
    }
    MailClient.appendP(cmd, esp_mail_str_315, false);

    if (MailClient.imapSend(this, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_store;
    if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
      return false;

    if (expunge)
    {
      if (MailClient.imapSendP(this, esp_mail_str_317, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_expunge;
      if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
        return false;
    }
  }

  return true;
}

bool IMAPSession::copyMessages(MessageList *toCopy, const char *dest)
{
  if (toCopy->_list.size() > 0)
  {

    if (!selectFolder(_currentFolder.c_str(), false))
      return false;

    if (_debug)
    {
      MBSTRING s;
      MailClient.appendP(s, esp_mail_str_318, true);
      s += dest;
      esp_mail_debug(s.c_str());
    }

    MBSTRING cmd;
    char *tmp = nullptr;
    MailClient.appendP(cmd, esp_mail_str_319, true);
    for (size_t i = 0; i < toCopy->_list.size(); i++)
    {
      if (i > 0)
        MailClient.appendP(cmd, esp_mail_str_263, false);
      tmp = MailClient.intStr(toCopy->_list[i]);
      cmd += tmp;
      MailClient.delP(&tmp);
    }
    MailClient.appendP(cmd, esp_mail_str_131, false);
    cmd += dest;

    if (MailClient.imapSend(this, cmd.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_store;
    if (!MailClient.handleIMAPResponse(this, IMAP_STATUS_BAD_COMMAND, false))
      return false;
  }

  return true;
}

void IMAPSession::empty()
{
  MBSTRING().swap(_nextUID);
  clearMessageData();
}

void IMAPSession::clearMessageData()
{
  for (size_t i = 0; i < _headers.size(); i++)
  {
    _headers[i].part_headers.clear();
    std::vector<struct esp_mail_message_part_info_t>().swap(_headers[i].part_headers);
  }
  std::vector<struct esp_mail_message_header_t>().swap(_headers);
  std::vector<uint32_t>().swap(_msgUID);
  _folders.clear();
  _mbif._flags.clear();
  _mbif._searchCount = 0;
  MBSTRING().swap(_flags_tmp);
}

IMAP_Status::IMAP_Status()
{
}
IMAP_Status::~IMAP_Status()
{
  empty();
}

const char *IMAP_Status::info()
{
  return _info.c_str();
}

bool IMAP_Status::success()
{
  return _success;
}

void IMAP_Status::empty()
{
  MBSTRING().swap(_info);
}

#endif

#if defined(ENABLE_SMTP)

void ESP_Mail_Client::mimeFromFile(const char *name, MBSTRING &mime)
{
  MBSTRING ext = name;
  size_t p = ext.find_last_of(".");
  if (p != MBSTRING::npos)
  {
    ext = ext.substr(p, ext.length() - p);
    if (ext.length() > 0)
      getMIME(ext.c_str(), mime);
  }
}

void ESP_Mail_Client::getMIME(const char *ext, MBSTRING &mime)
{
  mime = "";
  for (int i = 0; i < esp_mail_file_extension_maxType; i++)
  {
    if (strcmp_P(ext, mimeinfo[i].endsWith) == 0)
    {
      char *tmp = strP(mimeinfo[i].mimeType);
      mime = tmp;
      delP(&tmp);
      break;
    }
  }
}

MBSTRING ESP_Mail_Client::getEncodedToken(SMTPSession *smtp)
{
  MBSTRING raw;
  appendP(raw, esp_mail_str_285, true);
  raw += smtp->_sesson_cfg->login.email;
  appendP(raw, esp_mail_str_286, false);
  raw += smtp->_sesson_cfg->login.accessToken;
  appendP(raw, esp_mail_str_287, false);
  return encodeBase64Str((const unsigned char *)raw.c_str(), raw.length());
}

bool ESP_Mail_Client::smtpAuth(SMTPSession *smtp)
{

  if (!reconnect(smtp))
    return false;

  bool ssl = false;
  smtp->_secure = true;
  bool secureMode = true;

  MBSTRING s;

#if defined(ESP32)
  smtp->tcpClient.setDebugCallback(NULL);
#elif defined(ESP8266)
  smtp->tcpClient.rxBufDivider = 16; // minimum rx buffer for smtp status response
  smtp->tcpClient.txBufDivider = 8;  // medium tx buffer for faster attachment/inline data transfer
#endif

  if (smtp->_sesson_cfg->server.port == esp_mail_smtp_port_25)
  {
    smtp->_secure = false;
    secureMode = false;
  }
  else
  {
    if (smtp->_sesson_cfg->server.port == esp_mail_smtp_port_587)
      smtp->_sesson_cfg->secure.startTLS = true;

    secureMode = !smtp->_sesson_cfg->secure.startTLS;

    //to prevent to send the connection upgrade command when some server promotes
    //the starttls capability even the current connection was already secured.
    if (smtp->_sesson_cfg->server.port == esp_mail_smtp_port_465)
      ssl = true;
  }

#if defined(ESP32) || defined(ESP8266)
  setSecure(smtp->tcpClient, smtp->_sesson_cfg, smtp->_caCert);
#endif

  //Server connection attempt: no status code
  if (smtp->_sendCallback)
    smtpCBP(smtp, esp_mail_str_120);

  if (smtp->_debug)
  {
    appendP(s, esp_mail_str_314, true);
    s += ESP_MAIL_VERSION;
#if defined(ARDUINO_ARCH_SAMD)
    MBSTRING ninaFw = WiFi.firmwareVersion();
    appendP(s, esp_mail_str_329, false);
    s += ninaFw;
    smtp->tcpClient.firmwareBuildNumber();
    if (smtp->tcpClient._fwBuild > 0)
    {
      String build = String(smtp->tcpClient._fwBuild);
      appendP(s, esp_mail_str_330, false);
      s += String(smtp->tcpClient._fwBuild).c_str();
    }
#endif
    esp_mail_debug(s.c_str());

    debugInfoP(esp_mail_str_236);
    appendP(s, esp_mail_str_261, true);
    appendP(s, esp_mail_str_211, false);
    s += smtp->_sesson_cfg->server.host_name;
    esp_mail_debug(s.c_str());
    char *tmp = intStr(smtp->_sesson_cfg->server.port);
    appendP(s, esp_mail_str_261, true);
    appendP(s, esp_mail_str_201, false);
    s += tmp;
    delP(&tmp);
    esp_mail_debug(s.c_str());
  }
#if defined(ESP32)
  if (smtp->_debug)
    smtp->tcpClient.setDebugCallback(esp_mail_debug);
#endif

  smtp->tcpClient.begin(smtp->_sesson_cfg->server.host_name, smtp->_sesson_cfg->server.port);

  ethDNSWorkAround(smtp->_sesson_cfg);

  if (!smtp->tcpClient.connect(secureMode, smtp->_sesson_cfg->certificate.verify))
    return handleSMTPError(smtp, SMTP_STATUS_SERVER_CONNECT_FAILED);

  //server connected
  smtp->_tcpConnected = true;

  if (smtp->_debug)
    debugInfoP(esp_mail_str_238);

  if (smtp->_sendCallback)
  {
    smtpCB(smtp, "");
    smtpCBP(smtp, esp_mail_str_121);
  }

#if defined(ESP32)
  smtp->tcpClient.stream()->setTimeout(TCP_CLIENT_DEFAULT_TCP_TIMEOUT_SEC);
#elif defined(ESP8266)
  smtp->tcpClient.stream()->setTimeout(TCP_CLIENT_DEFAULT_TCP_TIMEOUT_SEC * 1000);
#endif
  smtp->tcpClient.tcpTimeout = TCP_CLIENT_DEFAULT_TCP_TIMEOUT_SEC * 1000;

  smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_initial_state;

  //expected status code 220 for ready to service
  if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_220, SMTP_STATUS_SMTP_GREETING_GET_RESPONSE_FAILED))
    return false;

init:

  //Sending greeting hello response
  if (smtp->_sendCallback)
  {
    smtpCB(smtp, "");
    smtpCBP(smtp, esp_mail_str_122);
  }

  if (smtp->_debug)
    debugInfoP(esp_mail_str_239);

  appendP(s, esp_mail_str_6, true);
  if (strlen(smtp->_sesson_cfg->login.user_domain) > 0)
    s += smtp->_sesson_cfg->login.user_domain;
  else
    appendP(s, esp_mail_str_44, false);

  if (smtpSendP(smtp, s.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_greeting;

  if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, 0))
  {
    appendP(s, esp_mail_str_5, true);
    if (strlen(smtp->_sesson_cfg->login.user_domain) > 0)
      s += smtp->_sesson_cfg->login.user_domain;
    else
      appendP(s, esp_mail_str_44, false);

    if (smtpSend(smtp, s.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;
    if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SMTP_GREETING_SEND_ACK_FAILED))
      return false;
    smtp->_send_capability.esmtp = false;
    smtp->_auth_capability.login = true;
  }
  else
    smtp->_send_capability.esmtp = true;

  //start TLS when needed
  if ((smtp->_auth_capability.start_tls || smtp->_sesson_cfg->secure.startTLS) && !ssl)
  {
    //send starttls command
    if (smtp->_sendCallback)
    {
      smtpCB(smtp, "");
      smtpCBP(smtp, esp_mail_str_209);
    }

    if (smtp->_debug)
    {
      appendP(s, esp_mail_str_196, true);
      esp_mail_debug(s.c_str());
    }

    //expected status code 250 for complete the request
    //some server returns 220 to restart to initial state
    smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_start_tls;
    smtpSendP(smtp, esp_mail_str_311, false);
    if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SMTP_GREETING_SEND_ACK_FAILED))
      return false;

    if (smtp->_debug)
    {
      debugInfoP(esp_mail_str_310);
    }

    //connect in secure mode
    //do ssl handshake
#if defined(ARDUINO_ARCH_SAMD)
    if (!smtp->tcpClient.connectSSL(smtp->_sesson_cfg->certificate.verify))
      return handleSMTPError(smtp, MAIL_CLIENT_ERROR_SSL_TLS_STRUCTURE_SETUP);
#else
    if (!smtp->tcpClient.stream()->connectSSL(smtp->_sesson_cfg->certificate.verify))
      return handleSMTPError(smtp, MAIL_CLIENT_ERROR_SSL_TLS_STRUCTURE_SETUP);
#endif

    //set the secure mode
    smtp->_sesson_cfg->secure.startTLS = false;
    ssl = true;
    smtp->_secure = true;

    //return to initial state if the response status is 220.
    if (smtp->_smtpStatus.respCode == esp_mail_smtp_status_code_220)
      goto init;
  }

  bool creds = strlen(smtp->_sesson_cfg->login.email) > 0 && strlen(smtp->_sesson_cfg->login.password) > 0;
  bool xoauth_auth = strlen(smtp->_sesson_cfg->login.accessToken) > 0 && smtp->_auth_capability.xoauth2;
  bool login_auth = smtp->_auth_capability.login && creds;
  bool plain_auth = smtp->_auth_capability.plain && creds;

  if (xoauth_auth || login_auth || plain_auth)
  {
    if (smtp->_sendCallback)
    {
      smtpCB(smtp, "", false);
      smtpCBP(smtp, esp_mail_str_56, false);
    }

    //log in
    if (xoauth_auth)
    {
      if (smtp->_debug)
        debugInfoP(esp_mail_str_288);

      if (!smtp->_auth_capability.xoauth2)
        return handleSMTPError(smtp, SMTP_STATUS_SERVER_OAUTH2_LOGIN_DISABLED, false);

      if (smtpSendP(smtp, esp_mail_str_289, false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      if (smtpSend(smtp, getEncodedToken(smtp).c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_auth;
      if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_235, SMTP_STATUS_AUTHEN_FAILED))
        return false;

      return true;
    }
    else if (plain_auth)
    {

      if (smtp->_debug)
        debugInfoP(esp_mail_str_241);

      if (smtp->_debug)
      {
        appendP(s, esp_mail_str_261, true);
        s += smtp->_sesson_cfg->login.email;
        esp_mail_debug(s.c_str());

        appendP(s, esp_mail_str_131, false);
        for (size_t i = 0; i < strlen(smtp->_sesson_cfg->login.password); i++)
          appendP(s, esp_mail_str_183, false);
        esp_mail_debug(s.c_str());
      }

      //rfc4616
      const char *usr = smtp->_sesson_cfg->login.email;
      const char *psw = smtp->_sesson_cfg->login.password;
      int len = strlen(usr) + strlen(psw) + 2;
      uint8_t *tmp = (uint8_t *)newP(len);
      memset(tmp, 0, len);
      int p = 1;
      memcpy(tmp + p, usr, strlen(usr));
      p += strlen(usr) + 1;
      memcpy(tmp + p, psw, strlen(psw));
      p += strlen(psw);

      MBSTRING s;
      appendP(s, esp_mail_str_45, true);
      appendP(s, esp_mail_str_131, false);
      s += encodeBase64Str(tmp, p);
      delP(&tmp);

      if (smtpSend(smtp, s.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_auth_plain;
      if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_235, SMTP_STATUS_USER_LOGIN_FAILED))
        return false;

      return true;
    }
    else if (login_auth)
    {
      if (smtp->_debug)
        debugInfoP(esp_mail_str_240);

      if (smtpSendP(smtp, esp_mail_str_4, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_334, SMTP_STATUS_AUTHEN_FAILED))
        return false;

      if (smtp->_debug)
      {
        appendP(s, esp_mail_str_261, true);
        s += smtp->_sesson_cfg->login.email;
        esp_mail_debug(s.c_str());
      }

      if (smtpSend(smtp, encodeBase64Str((const unsigned char *)smtp->_sesson_cfg->login.email, strlen(smtp->_sesson_cfg->login.email)).c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_login_user;
      if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_334, SMTP_STATUS_USER_LOGIN_FAILED))
        return false;

      if (smtp->_debug)
      {
        appendP(s, esp_mail_str_261, true);
        for (size_t i = 0; i < strlen(smtp->_sesson_cfg->login.password); i++)
          appendP(s, esp_mail_str_183, false);
        esp_mail_debug(s.c_str());
      }

      if (smtpSend(smtp, encodeBase64Str((const unsigned char *)smtp->_sesson_cfg->login.password, strlen(smtp->_sesson_cfg->login.password)).c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_login_psw;
      if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_235, SMTP_STATUS_PASSWORD_LOGIN_FAILED))
        return false;

      return true;
    }
  }

  return true;
}

bool ESP_Mail_Client::connected(SMTPSession *smtp)
{
  if (!smtp->tcpClient.stream())
    return false;
  return smtp->tcpClient.stream()->connected();
}

bool ESP_Mail_Client::setSendingResult(SMTPSession *smtp, SMTP_Message *msg, bool result)
{
  if (result)
    smtp->_sentSuccessCount++;
  else
    smtp->_sentFailedCount++;

  if (smtp->_sendCallback)
  {
    SMTP_Result status;
    status.completed = result;
#if defined(ARDUINO_ARCH_SAMD)
    unsigned long ts = WiFi.getTime();
    status.timestamp = ts;
#else
    status.timestamp = time(nullptr);
#endif
    status.subject = msg->subject;
    status.recipients = msg->_rcp[0].email;

    smtp->sendingResult.add(&status);

    smtp->_cbData._sentSuccess = smtp->_sentSuccessCount;
    smtp->_cbData._sentFailed = smtp->_sentFailedCount;
  }

  return result;
}

bool ESP_Mail_Client::sendMail(SMTPSession *smtp, SMTP_Message *msg, bool closeSession)
{

  if (strlen(msg->html.content) > 0 || msg->html.blob.size > 0 || strlen(msg->html.file.name) > 0)
    msg->type |= esp_mail_msg_type_html;

  if (strlen(msg->text.content) > 0 || msg->text.blob.size > 0 || strlen(msg->text.file.name) > 0)
    msg->type |= esp_mail_msg_type_plain;

  for (size_t i = 0; i < msg->_rfc822.size(); i++)
  {
    if (strlen(msg->_rfc822[i].html.content) > 0)
      msg->_rfc822[i].type |= esp_mail_msg_type_html;

    if (strlen(msg->_rfc822[i].text.content) > 0)
      msg->_rfc822[i].type |= esp_mail_msg_type_plain;
  }

  return mSendMail(smtp, msg, closeSession);
}

size_t ESP_Mail_Client::numAtt(SMTPSession *smtp, esp_mail_attach_type type, SMTP_Message *msg)
{
  size_t count = 0;
  for (size_t i = 0; i < msg->_att.size(); i++)
  {
    if (msg->_att[i]._int.att_type == type)
      count++;
  }
  return count;
}

bool ESP_Mail_Client::checkEmail(SMTPSession *smtp, SMTP_Message *msg)
{
  bool validRecipient = false;

  if (!validEmail(msg->sender.email))
  {
    errorStatusCB(smtp, SMTP_STATUS_NO_VALID_SENDER_EXISTED);
    return setSendingResult(smtp, msg, false);
  }

  for (uint8_t i = 0; i < msg->_rcp.size(); i++)
  {
    if (validEmail(msg->_rcp[i].email))
      validRecipient = true;
  }

  if (!validRecipient)
  {
    errorStatusCB(smtp, SMTP_STATUS_NO_VALID_RECIPIENTS_EXISTED);
    return setSendingResult(smtp, msg, false);
  }

  return true;
}

bool ESP_Mail_Client::mSendMail(SMTPSession *smtp, SMTP_Message *msg, bool closeSession)
{

  smtp->_smtpStatus.statusCode = 0;
  smtp->_smtpStatus.respCode = 0;
  smtp->_smtpStatus.text.clear();
  bool rfc822MSG = false;

  if (!checkEmail(smtp, msg))
    return false;

  smtp->_chunkedEnable = false;
  smtp->_chunkCount = 0;

  //new session
  if (!smtp->_tcpConnected)
  {
    if (!smtpAuth(smtp))
    {
      closeTCPSession(smtp);
      return setSendingResult(smtp, msg, false);
    }
    smtp->_sentSuccessCount = 0;
    smtp->_sentFailedCount = 0;
    smtp->sendingResult.clear();
  }
  else
  {
    //reuse session
    if (smtp->_sendCallback)
    {
      smtpCB(smtp, "");
      if (smtp->_sentSuccessCount || smtp->_sentFailedCount)
        smtpCBP(smtp, esp_mail_str_267);
      else
        smtpCBP(smtp, esp_mail_str_208);
    }

    if (smtp->_debug)
    {
      if (smtp->_sentSuccessCount || smtp->_sentFailedCount)
        debugInfoP(esp_mail_str_268);
      else
        debugInfoP(esp_mail_str_207);
    }
  }

  if (smtp->_sendCallback)
  {
    smtpCB(smtp, "");
    smtpCBP(smtp, esp_mail_str_125);
  }

  if (smtp->_debug)
    debugInfoP(esp_mail_str_242);

  MBSTRING buf;
  MBSTRING buf2;
  checkBinaryData(smtp, msg);

  if (msg->priority >= esp_mail_smtp_priority_high && msg->priority <= esp_mail_smtp_priority_low)
  {
    char *tmp = intStr(msg->priority);
    appendP(buf2, esp_mail_str_17, true);
    buf2 += tmp;
    delP(&tmp);
    appendP(buf2, esp_mail_str_34, false);

    if (msg->priority == esp_mail_smtp_priority_high)
    {
      appendP(buf2, esp_mail_str_18, false);
      appendP(buf2, esp_mail_str_21, false);
    }
    else if (msg->priority == esp_mail_smtp_priority_normal)
    {
      appendP(buf2, esp_mail_str_19, false);
      appendP(buf2, esp_mail_str_22, false);
    }
    else if (msg->priority == esp_mail_smtp_priority_low)
    {
      appendP(buf2, esp_mail_str_20, false);
      appendP(buf2, esp_mail_str_23, false);
    }
  }

  appendP(buf2, esp_mail_str_10, false);
  appendP(buf2, esp_mail_str_131, false);

  if (strlen(msg->sender.name) > 0)
    buf2 += msg->sender.name;

  appendP(buf2, esp_mail_str_14, false);
  buf2 += msg->sender.email;
  appendP(buf2, esp_mail_str_15, false);
  appendP(buf2, esp_mail_str_34, false);

  appendP(buf, esp_mail_str_8, true);
  appendP(buf, esp_mail_str_14, false);
  buf += msg->sender.email;
  appendP(buf, esp_mail_str_15, false);

  if (smtp->_send_capability.binaryMIME && smtp->_send_capability.chunking && msg->enable.chunking && (msg->text._int.binary || msg->html._int.binary))
    appendP(buf, esp_mail_str_104, false);

  if (smtpSend(smtp, buf.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return setSendingResult(smtp, msg, false);

  smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_send_header_sender;
  if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SEND_HEADER_SENDER_FAILED))
    return setSendingResult(smtp, msg, false);

  for (uint8_t i = 0; i < msg->_rcp.size(); i++)
  {
    if (i == 0)
    {
      appendP(buf2, esp_mail_str_11, false);
      appendP(buf2, esp_mail_str_131, false);
      if (strlen(msg->_rcp[i].name) > 0)
        buf2 += msg->_rcp[i].name;

      appendP(buf2, esp_mail_str_14, false);
      buf2 += msg->_rcp[i].email;
      appendP(buf2, esp_mail_str_15, false);
    }
    else
    {
      if (strlen(msg->_rcp[i].name) > 0)
      {
        appendP(buf2, esp_mail_str_263, false);
        buf2 += msg->_rcp[i].name;
        appendP(buf2, esp_mail_str_14, false);
      }
      else
        appendP(buf2, esp_mail_str_13, false);
      buf2 += msg->_rcp[i].email;
      appendP(buf2, esp_mail_str_15, false);
    }

    if (i == msg->_rcp.size() - 1)
      appendP(buf2, esp_mail_str_34, false);

    buf.clear();
    //only address
    appendP(buf, esp_mail_str_9, false);
    appendP(buf, esp_mail_str_14, false);
    buf += msg->_rcp[i].email;
    appendP(buf, esp_mail_str_15, false);

    //rfc3461, rfc3464
    if (smtp->_send_capability.dsn)
    {
      if (msg->response.notify != esp_mail_smtp_notify::esp_mail_smtp_notify_never)
      {
        appendP(buf, esp_mail_str_262, false);
        int opcnt = 0;
        if (msg->response.notify == esp_mail_smtp_notify::esp_mail_smtp_notify_success)
        {
          if (opcnt > 0)
            appendP(buf, esp_mail_str_263, false);
          appendP(buf, esp_mail_str_264, false);
          opcnt++;
        }
        if (msg->response.notify == esp_mail_smtp_notify::esp_mail_smtp_notify_failure)
        {
          if (opcnt > 0)
            appendP(buf, esp_mail_str_263, false);
          appendP(buf, esp_mail_str_265, false);
          opcnt++;
        }
        if (msg->response.notify == esp_mail_smtp_notify::esp_mail_smtp_notify_delay)
        {
          if (opcnt > 0)
            appendP(buf, esp_mail_str_263, false);
          appendP(buf, esp_mail_str_266, false);
          opcnt++;
        }
      }
    }

    if (smtpSend(smtp, buf.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return setSendingResult(smtp, msg, false);

    smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_send_header_recipient;
    if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SEND_HEADER_RECIPIENT_FAILED))
      return setSendingResult(smtp, msg, false);
  }

  for (uint8_t i = 0; i < msg->_cc.size(); i++)
  {
    if (i == 0)
    {
      appendP(buf2, esp_mail_str_12, false);
      appendP(buf2, esp_mail_str_131, false);
      appendP(buf2, esp_mail_str_14, false);
      buf2 += msg->_cc[i].email;
      appendP(buf2, esp_mail_str_15, false);
    }
    else
    {
      appendP(buf2, esp_mail_str_13, false);
      buf2 += msg->_cc[i].email;
      appendP(buf2, esp_mail_str_15, false);
    }

    if (i == msg->_cc.size() - 1)
      appendP(buf2, esp_mail_str_34, false);

    buf.clear();

    appendP(buf, esp_mail_str_9, false);
    appendP(buf, esp_mail_str_14, false);
    buf += msg->_cc[i].email;
    appendP(buf, esp_mail_str_15, false);

    if (smtpSend(smtp, buf.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return setSendingResult(smtp, msg, false);

    smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_send_header_recipient;
    if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SEND_HEADER_RECIPIENT_FAILED))
      return setSendingResult(smtp, msg, false);
  }

  for (uint8_t i = 0; i < msg->_bcc.size(); i++)
  {
    appendP(buf, esp_mail_str_9, true);
    appendP(buf, esp_mail_str_14, false);
    buf += msg->_bcc[i].email;
    appendP(buf, esp_mail_str_15, false);
    if (smtpSend(smtp, buf.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return setSendingResult(smtp, msg, false);
    smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_send_header_recipient;
    if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SEND_HEADER_RECIPIENT_FAILED))
      return setSendingResult(smtp, msg, false);
  }

  if (smtp->_sendCallback)
  {
    smtpCB(smtp, "");
    smtpCBP(smtp, esp_mail_str_126);
  }

  if (smtp->_debug)
    debugInfoP(esp_mail_str_243);

  if (smtp->_send_capability.chunking && msg->enable.chunking)
  {
    smtp->_chunkedEnable = true;
    if (!bdat(smtp, msg, buf2.length(), false))
      return false;
  }
  else
  {
    if (smtpSendP(smtp, esp_mail_str_16, true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return setSendingResult(smtp, msg, false);

    smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_send_body;
    if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_354, SMTP_STATUS_SEND_BODY_FAILED))
      return setSendingResult(smtp, msg, false);
  }

  if (smtpSend(smtp, buf2.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return setSendingResult(smtp, msg, false);

  MBSTRING s;
  appendP(s, esp_mail_str_24, true);
  appendP(s, esp_mail_str_131, false);
  s += msg->subject;
  appendP(s, esp_mail_str_34, false);

  if (msg->_hdr.size() > 0)
  {
    for (uint8_t k = 0; k < msg->_hdr.size(); k++)
    {
      s += msg->_hdr[k];
      appendP(s, esp_mail_str_34, false);
    }
  }

  if (strlen(msg->response.reply_to) > 0)
  {
    appendP(s, esp_mail_str_184, false);
    appendP(s, esp_mail_str_131, false);
    appendP(s, esp_mail_str_14, false);
    s += msg->response.reply_to;
    appendP(s, esp_mail_str_15, false);
    appendP(s, esp_mail_str_34, false);
  }

  if (strlen(msg->response.return_path) > 0)
  {
    appendP(s, esp_mail_str_46, false);
    appendP(s, esp_mail_str_131, false);
    appendP(s, esp_mail_str_14, false);
    s += msg->response.return_path;
    appendP(s, esp_mail_str_15, false);
    appendP(s, esp_mail_str_34, false);
  }

  if (strlen(msg->in_reply_to) > 0)
  {
    appendP(s, esp_mail_str_109, false);
    appendP(s, esp_mail_str_131, false);
    s += msg->in_reply_to;
    appendP(s, esp_mail_str_34, false);
  }

  if (strlen(msg->references) > 0)
  {
    appendP(s, esp_mail_str_107, false);
    appendP(s, esp_mail_str_131, false);
    s += msg->references;
    appendP(s, esp_mail_str_34, false);
  }

  if (strlen(msg->comments) > 0)
  {
    appendP(s, esp_mail_str_134, false);
    appendP(s, esp_mail_str_131, false);
    s += msg->comments;
    appendP(s, esp_mail_str_34, false);
  }

  if (strlen(msg->keywords) > 0)
  {
    appendP(s, esp_mail_str_145, false);
    appendP(s, esp_mail_str_131, false);
    s += msg->keywords;
    appendP(s, esp_mail_str_34, false);
  }

  if (strlen(msg->messageID) > 0)
  {
    appendP(s, esp_mail_str_101, false);
    appendP(s, esp_mail_str_131, false);
    appendP(s, esp_mail_str_14, false);
    s += msg->messageID;
    appendP(s, esp_mail_str_15, false);
    appendP(s, esp_mail_str_34, false);
  }

  appendP(s, esp_mail_str_3, false);

  if (!bdat(smtp, msg, s.length(), false))
    return false;

  if (smtpSend(smtp, s.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return setSendingResult(smtp, msg, false);

  return sendMSGData(smtp, msg, closeSession, rfc822MSG);
}

bool ESP_Mail_Client::sendMSGData(SMTPSession *smtp, SMTP_Message *msg, bool closeSession, bool rfc822MSG)
{
  MBSTRING s;
  MBSTRING mixed = getBoundary(15);
  MBSTRING alt = getBoundary(15);

  if (numAtt(smtp, esp_mail_att_type_attachment, msg) == 0 && msg->_parallel.size() == 0 && msg->_rfc822.size() == 0)
  {
    if (msg->type == (esp_mail_msg_type_plain | esp_mail_msg_type_html | esp_mail_msg_type_enriched) || numAtt(smtp, esp_mail_att_type_inline, msg) > 0)
    {
      if (!sendMSG(smtp, msg, alt))
        return setSendingResult(smtp, msg, false);
    }
    else if (msg->type != esp_mail_msg_type_none)
    {
      if (!sendPartText(smtp, msg, msg->type, ""))
        return setSendingResult(smtp, msg, false);
    }
  }
  else
  {
    appendP(s, esp_mail_str_1, true);
    s += mixed;
    appendP(s, esp_mail_str_35, false);

    appendP(s, esp_mail_str_33, false);
    s += mixed;
    appendP(s, esp_mail_str_34, false);

    if (!bdat(smtp, msg, s.length(), false))
      return false;

    if (smtpSend(smtp, s.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return setSendingResult(smtp, msg, false);

    if (!sendMSG(smtp, msg, alt))
      return setSendingResult(smtp, msg, false);

    if (!bdat(smtp, msg, 2, false))
      return false;

    if (smtpSendP(smtp, esp_mail_str_34, false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return setSendingResult(smtp, msg, false);

    if (smtp->_sendCallback)
    {
      smtpCB(smtp, "");
      smtpCBP(smtp, esp_mail_str_127);
    }

    if (smtp->_debug)
      debugInfoP(esp_mail_str_244);

    if (smtp->_sendCallback && numAtt(smtp, esp_mail_att_type_attachment, msg) > 0)
      esp_mail_debug("");

    if (!sendAttachments(smtp, msg, mixed))
      return setSendingResult(smtp, msg, false);

    if (!sendParallelAttachments(smtp, msg, mixed))
      return setSendingResult(smtp, msg, false);

    if (!sendRFC822Msg(smtp, msg, mixed, closeSession, msg->_rfc822.size() > 0))
      return setSendingResult(smtp, msg, false);

    appendP(s, esp_mail_str_33, true);
    s += mixed;
    appendP(s, esp_mail_str_33, false);

    if (!bdat(smtp, msg, s.length(), false))
      return false;

    if (smtpSend(smtp, s.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return setSendingResult(smtp, msg, false);
  }

  if (!rfc822MSG)
  {
    if (smtp->_sendCallback)
    {
      smtpCB(smtp, "");
      smtpCBP(smtp, esp_mail_str_303);
    }

    if (smtp->_debug)
      debugInfoP(esp_mail_str_304);

    if (smtp->_chunkedEnable)
    {

      if (!bdat(smtp, msg, 0, true))
        return false;

      smtp->_smtp_cmd = esp_mail_smtp_cmd_chunk_termination;
      if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SEND_BODY_FAILED))
        return false;
    }
    else
    {
      if (smtpSendP(smtp, esp_mail_str_37, false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return setSendingResult(smtp, msg, false);

      smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_send_body;
      if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SEND_BODY_FAILED))
        return setSendingResult(smtp, msg, false);
    }

    setSendingResult(smtp, msg, true);

    if (closeSession)
      if (!smtp->closeSession())
        return false;
  }

  return true;
}

bool ESP_Mail_Client::sendRFC822Msg(SMTPSession *smtp, SMTP_Message *msg, const MBSTRING &boundary, bool closeSession, bool rfc822MSG)
{
  if (msg->_rfc822.size() == 0)
    return true;
  MBSTRING buf;
  for (uint8_t i = 0; i < msg->_rfc822.size(); i++)
  {
    buf.clear();
    getRFC822PartHeader(smtp, buf, boundary);

    getRFC822MsgEnvelope(smtp, &msg->_rfc822[i], buf);

    if (!bdat(smtp, msg, buf.length(), false))
      return false;

    if (smtpSend(smtp, buf.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    if (!sendMSGData(smtp, &msg->_rfc822[i], closeSession, rfc822MSG))
      return false;
  }

  return true;
}

void ESP_Mail_Client::getRFC822MsgEnvelope(SMTPSession *smtp, SMTP_Message *msg, MBSTRING &buf)
{
  if (strlen(msg->date) > 0)
  {
    appendP(buf, esp_mail_str_99, false);
    buf += msg->date;
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->from.email) > 0)
  {
    appendP(buf, esp_mail_str_10, false);
    appendP(buf, esp_mail_str_131, false);

    if (strlen(msg->from.name) > 0)
      buf += msg->from.name;

    appendP(buf, esp_mail_str_14, false);
    buf += msg->from.email;
    appendP(buf, esp_mail_str_15, false);
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->sender.email) > 0)
  {
    appendP(buf, esp_mail_str_150, false);
    appendP(buf, esp_mail_str_131, false);

    if (strlen(msg->sender.name) > 0)
      buf += msg->sender.name;

    appendP(buf, esp_mail_str_14, false);
    buf += msg->sender.email;
    appendP(buf, esp_mail_str_15, false);
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->response.reply_to) > 0)
  {
    appendP(buf, esp_mail_str_184, false);
    appendP(buf, esp_mail_str_131, false);
    appendP(buf, esp_mail_str_14, false);
    buf += msg->response.reply_to;
    appendP(buf, esp_mail_str_15, false);
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->response.return_path) > 0)
  {
    appendP(buf, esp_mail_str_46, false);
    appendP(buf, esp_mail_str_131, false);
    appendP(buf, esp_mail_str_14, false);
    buf += msg->response.return_path;
    appendP(buf, esp_mail_str_15, false);
    appendP(buf, esp_mail_str_34, false);
  }

  for (uint8_t i = 0; i < msg->_rcp.size(); i++)
  {
    if (i == 0)
    {
      appendP(buf, esp_mail_str_11, false);
      appendP(buf, esp_mail_str_131, false);
      if (strlen(msg->_rcp[i].name) > 0)
        buf += msg->_rcp[i].name;

      appendP(buf, esp_mail_str_14, false);
      buf += msg->_rcp[i].email;
      appendP(buf, esp_mail_str_15, false);
    }
    else
    {
      if (strlen(msg->_rcp[i].name) > 0)
      {
        appendP(buf, esp_mail_str_263, false);
        buf += msg->_rcp[i].name;
        appendP(buf, esp_mail_str_14, false);
      }
      else
        appendP(buf, esp_mail_str_13, false);
      buf += msg->_rcp[i].email;
      appendP(buf, esp_mail_str_15, false);
    }

    if (i == msg->_rcp.size() - 1)
      appendP(buf, esp_mail_str_34, false);
  }

  for (uint8_t i = 0; i < msg->_cc.size(); i++)
  {
    if (i == 0)
    {
      appendP(buf, esp_mail_str_12, false);
      appendP(buf, esp_mail_str_131, false);
      appendP(buf, esp_mail_str_14, false);
      buf += msg->_cc[i].email;
      appendP(buf, esp_mail_str_15, false);
    }
    else
    {
      appendP(buf, esp_mail_str_13, false);
      buf += msg->_cc[i].email;
      appendP(buf, esp_mail_str_15, false);
    }

    if (i == msg->_cc.size() - 1)
      appendP(buf, esp_mail_str_34, false);
  }

  for (uint8_t i = 0; i < msg->_bcc.size(); i++)
  {
    if (i == 0)
    {
      appendP(buf, esp_mail_str_149, false);
      appendP(buf, esp_mail_str_14, false);
      buf += msg->_bcc[i].email;
      appendP(buf, esp_mail_str_15, false);
    }
    else
    {
      appendP(buf, esp_mail_str_13, false);
      buf += msg->_bcc[i].email;
      appendP(buf, esp_mail_str_15, false);
    }

    if (i == msg->_bcc.size() - 1)
      appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->subject) > 0)
  {
    appendP(buf, esp_mail_str_24, false);
    appendP(buf, esp_mail_str_131, false);
    buf += msg->subject;
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->keywords) > 0)
  {
    appendP(buf, esp_mail_str_145, false);
    appendP(buf, esp_mail_str_131, false);
    buf += msg->keywords;
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->comments) > 0)
  {
    appendP(buf, esp_mail_str_134, false);
    appendP(buf, esp_mail_str_131, false);
    buf += msg->comments;
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->in_reply_to) > 0)
  {
    appendP(buf, esp_mail_str_109, false);
    appendP(buf, esp_mail_str_131, false);
    buf += msg->in_reply_to;
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->references) > 0)
  {
    appendP(buf, esp_mail_str_107, false);
    appendP(buf, esp_mail_str_131, false);
    buf += msg->references;
    appendP(buf, esp_mail_str_34, false);
  }

  if (strlen(msg->messageID) > 0)
  {
    appendP(buf, esp_mail_str_101, false);
    appendP(buf, esp_mail_str_131, false);
    appendP(buf, esp_mail_str_14, false);
    buf += msg->messageID;
    appendP(buf, esp_mail_str_15, false);
    appendP(buf, esp_mail_str_34, false);
  }
}

bool ESP_Mail_Client::bdat(SMTPSession *smtp, SMTP_Message *msg, int len, bool last)
{
  if (!smtp->_chunkedEnable || !msg->enable.chunking)
    return true;

  smtp->_chunkCount++;

  MBSTRING bdat;
  appendP(bdat, esp_mail_str_106, true);
  char *tmp = intStr(len);
  bdat += tmp;
  if (last)
    appendP(bdat, esp_mail_str_173, false);
  delP(&tmp);
  if (smtpSend(smtp, bdat.c_str(), true) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return setSendingResult(smtp, msg, false);

  if (!smtp->_send_capability.pipelining)
  {
    smtp->_smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_send_body;
    if (!handleSMTPResponse(smtp, esp_mail_smtp_status_code_250, SMTP_STATUS_SEND_BODY_FAILED))
      return setSendingResult(smtp, msg, false);
    smtp->_chunkCount = 0;
  }
  return true;
}

void ESP_Mail_Client::checkBinaryData(SMTPSession *smtp, SMTP_Message *msg)
{
  if (msg->type & esp_mail_msg_type_plain || msg->type == esp_mail_msg_type_enriched || msg->type & esp_mail_msg_type_html)
  {
    if ((msg->type & esp_mail_msg_type_plain || msg->type == esp_mail_msg_type_enriched) > 0)
    {
      if (strlen(msg->text.transfer_encoding) > 0)
      {
        if (strcmp(msg->text.transfer_encoding, Content_Transfer_Encoding::enc_binary) == 0)
        {
          msg->text._int.binary = true;
        }
      }
    }

    if ((msg->type & esp_mail_msg_type_html) > 0)
    {
      if (strlen(msg->html.transfer_encoding) > 0)
      {
        if (strcmp(msg->html.transfer_encoding, Content_Transfer_Encoding::enc_binary) == 0)
        {
          msg->html._int.binary = true;
        }
      }
    }
  }

  for (size_t i = 0; i < msg->_att.size(); i++)
  {
    if (strcmpP(msg->_att[i].descr.transfer_encoding, 0, esp_mail_str_166))
    {
      msg->_att[i]._int.binary = true;
    }
  }
}

bool ESP_Mail_Client::sendBlob(SMTPSession *smtp, SMTP_Message *msg, SMTP_Attachment *att)
{
  if (strcmp(att->descr.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0 && strcmp(att->descr.transfer_encoding, att->descr.content_encoding) != 0)
  {
    if (!sendBase64(smtp, msg, (const unsigned char *)att->blob.data, att->blob.size, att->_int.flash_blob, att->descr.filename, smtp->_sendCallback != NULL))
      return false;
    return true;
  }
  else
  {
    if (att->blob.size > 0)
    {
      if (strcmp(att->descr.content_encoding, Content_Transfer_Encoding::enc_base64) == 0)
      {
        if (!sendBase64Raw(smtp, msg, att->blob.data, att->blob.size, att->_int.flash_blob, att->descr.filename, smtp->_sendCallback != NULL))
          return false;
        return true;
      }
      else
      {

        size_t chunkSize = ESP_MAIL_CLIENT_STREAM_CHUNK_SIZE;
        size_t writeLen = 0;
        uint8_t *buf = (uint8_t *)newP(chunkSize);
        int pg = 0, _pg = 0;
        while (writeLen < att->blob.size)
        {
          if (writeLen > att->blob.size - chunkSize)
            chunkSize = att->blob.size - writeLen;

          if (!bdat(smtp, msg, chunkSize, false))
            break;
          memcpy_P(buf, att->blob.data, chunkSize);
          if (smtpSend(smtp, buf, chunkSize) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
            break;

          if (smtp->_sendCallback)
          {
            pg = (float)(100.0f * writeLen / att->blob.size);
            if (pg != _pg)
              uploadReport(att->descr.filename, pg);
            _pg = pg;
          }
          writeLen += chunkSize;
        }
        delP(&buf);
        if (smtp->_sendCallback && _pg < 100)
          uploadReport(att->descr.filename, 100);

        return writeLen >= att->blob.size;
      }
    }
  }
  return false;
}

bool ESP_Mail_Client::sendFile(SMTPSession *smtp, SMTP_Message *msg, SMTP_Attachment *att, File &file)
{
  if (strcmp(att->descr.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0 && strcmp(att->descr.transfer_encoding, att->descr.content_encoding) != 0)
  {
    if (!sendBase64Stream(smtp, msg, file, att->descr.filename, smtp->_sendCallback != NULL))
      return false;
    return true;
  }
  else
  {
    if (file.size() > 0)
    {
      if (strcmp(att->descr.content_encoding, Content_Transfer_Encoding::enc_base64) == 0)
      {
        if (!sendBase64StreamRaw(smtp, msg, file, att->descr.filename, smtp->_sendCallback != NULL))
          return false;
        return true;
      }
      else
      {
        size_t chunkSize = ESP_MAIL_CLIENT_STREAM_CHUNK_SIZE;
        size_t writeLen = 0;
        if (file.size() < chunkSize)
          chunkSize = file.size();
        uint8_t *buf = (uint8_t *)newP(chunkSize);
        int pg = 0, _pg = 0;
        while (writeLen < file.size() && file.available())
        {
          if (writeLen > file.size() - chunkSize)
            chunkSize = file.size() - writeLen;
          size_t readLen = file.read(buf, chunkSize);
          if (readLen != chunkSize)
          {
            errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
            break;
          }

          if (!bdat(smtp, msg, chunkSize, false))
            break;

          if (smtpSend(smtp, buf, chunkSize) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
            break;

          if (smtp->_sendCallback)
          {
            pg = (float)(100.0f * writeLen / file.size());
            if (pg != _pg)
              uploadReport(att->descr.filename, pg);
            _pg = pg;
          }
          writeLen += chunkSize;
        }
        delP(&buf);
        if (smtp->_sendCallback && _pg < 100)
          uploadReport(att->descr.filename, 100);
        return writeLen == file.size();
      }
    }
    return false;
  }
  return false;
}

bool ESP_Mail_Client::sendParallelAttachments(SMTPSession *smtp, SMTP_Message *msg, const MBSTRING &boundary)
{
  if (msg->_parallel.size() == 0)
    return true;

  MBSTRING buf;
  MBSTRING parallel = getBoundary(15);
  appendP(buf, esp_mail_str_33, true);
  buf += boundary;
  appendP(buf, esp_mail_str_34, false);

  appendP(buf, esp_mail_str_28, false);
  buf += parallel;
  appendP(buf, esp_mail_str_35, false);

  if (!bdat(smtp, msg, buf.length(), false))
    return false;

  if (smtpSend(smtp, buf.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return setSendingResult(smtp, msg, false);

  if (!sendAttachments(smtp, msg, parallel, true))
    return setSendingResult(smtp, msg, false);

  appendP(buf, esp_mail_str_33, true);
  buf += parallel;
  appendP(buf, esp_mail_str_33, false);

  if (!bdat(smtp, msg, buf.length(), false))
    return false;

  if (smtpSend(smtp, buf.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return setSendingResult(smtp, msg, false);

  return true;
}

bool ESP_Mail_Client::sendAttachments(SMTPSession *smtp, SMTP_Message *msg, const MBSTRING &boundary, bool parallel)
{
  MBSTRING s;
  MBSTRING buf;
  int cnt = 0;

  SMTP_Attachment *att = nullptr;

  size_t sz = msg->_att.size();
  if (parallel)
    sz = msg->_parallel.size();

  for (size_t i = 0; i < sz; i++)
  {
    if (parallel)
      att = &msg->_parallel[i];
    else
      att = &msg->_att[i];

    if (att->_int.att_type == esp_mail_att_type_attachment)
    {
      appendP(s, esp_mail_str_261, true);
      s += att->descr.filename;

      if (smtp->_sendCallback)
      {
        if (cnt > 0)
          smtpCB(smtp, "");
        smtpCB(smtp, att->descr.filename);
      }

      if (smtp->_debug)
        esp_mail_debug(s.c_str());

      cnt++;

      if (att->file.storage_type == esp_mail_file_storage_type_none)
      {
        if (!att->blob.data)
          continue;

        if (smtp->_sendCallback)
          smtpCB(smtp, att->descr.filename);

        if (smtp->_debug)
          esp_mail_debug(s.c_str());

        buf.clear();
        getAttachHeader(buf, boundary, att, att->blob.size);

        if (!bdat(smtp, msg, buf.length(), false))
          return false;

        if (smtpSend(smtp, buf.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
          return false;

        if (!sendBlob(smtp, msg, att))
          return false;

        if (!bdat(smtp, msg, 2, false))
          return false;

        if (smtpSendP(smtp, esp_mail_str_34, false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
          return false;
      }
      else
      {

        if (!_sdOk && att->file.storage_type == esp_mail_file_storage_type_sd)
          _sdOk = sdTest();

        if (!_flashOk && att->file.storage_type == esp_mail_file_storage_type_flash)
#if defined(ESP32)
          _flashOk = ESP_MAIL_FLASH_FS.begin(FORMAT_FLASH);
#elif defined(ESP8266)
          _flashOk = ESP_MAIL_FLASH_FS.begin();
#endif

        if ((!_sdOk && att->file.storage_type == esp_mail_file_storage_type_sd) || (!_flashOk && att->file.storage_type == esp_mail_file_storage_type_flash))
        {

          if (smtp->_sendCallback)
            debugInfoP(esp_mail_str_158);

          if (smtp->_debug)
          {
            MBSTRING e;
            appendP(e, esp_mail_str_185, true);
            appendP(e, esp_mail_str_158, false);
            esp_mail_debug(e.c_str());
          }

          continue;
        }

        if (openFileRead(smtp, msg, att, file, s, buf, boundary, false))
        {
          if (file)
          {

            if (!sendFile(smtp, msg, att, file))
              return false;

            if (!bdat(smtp, msg, 2, false))
              return false;

            if (smtpSendP(smtp, esp_mail_str_34, false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
              return false;
          }
        }
      }
    }
  }
  return true;
}

bool ESP_Mail_Client::openFileRead(SMTPSession *smtp, SMTP_Message *msg, SMTP_Attachment *att, File &file, MBSTRING &s, MBSTRING &buf, const MBSTRING &boundary, bool inlined)
{
  bool file_existed = false;
  MBSTRING filepath;

  if (strlen(att->file.path) > 0)
  {
    if (att->file.path[0] != '/')
      appendP(filepath, esp_mail_str_202, true);
    filepath += att->file.path;
  }

  if (att->file.storage_type == esp_mail_file_storage_type_sd)
  {
#if defined(ESP_MAIL_SD_FS)
    file_existed = ESP_MAIL_SD_FS.exists(filepath.c_str());
#endif
  }
  else if (att->file.storage_type == esp_mail_file_storage_type_flash)
  {
#if defined(ESP_MAIL_FLASH_FS)
    file_existed = ESP_MAIL_FLASH_FS.exists(filepath.c_str());
#endif
  }

  if (!file_existed)
  {

    if (strlen(att->descr.filename) > 0)
    {
      filepath.clear();
      if (att->descr.filename[0] != '/')
        appendP(filepath, esp_mail_str_202, true);
      filepath += att->descr.filename;
    }

    if (att->file.storage_type == esp_mail_file_storage_type_sd)
#if defined(ESP_MAIL_SD_FS)
      file_existed = ESP_MAIL_SD_FS.exists(filepath.c_str());
#endif
  }
  else if (att->file.storage_type == esp_mail_file_storage_type_flash)
  {
#if defined(ESP_MAIL_FLASH_FS)
    file_existed = ESP_MAIL_FLASH_FS.exists(filepath.c_str());
#endif
  }

  if (!file_existed)
  {
    if (smtp->_sendCallback)
      debugInfoP(esp_mail_str_158);

    if (smtp->_debug)
    {
      MBSTRING e;
      appendP(e, esp_mail_str_185, true);
      appendP(e, esp_mail_str_158, false);
      esp_mail_debug(e.c_str());
    }
  }

  if (file_existed)
  {

    buf.clear();

    if (att->file.storage_type == esp_mail_file_storage_type_sd)
      file = ESP_MAIL_SD_FS.open(filepath.c_str(), FILE_READ);
    else if (att->file.storage_type == esp_mail_file_storage_type_flash)
#if defined(ESP32)
      file = ESP_MAIL_FLASH_FS.open(filepath.c_str(), FILE_READ);
#elif defined(ESP8266)
      file = ESP_MAIL_FLASH_FS.open(filepath.c_str(), "r");
#endif

    if (!file)
      return false;

    if (inlined)
      getInlineHeader(buf, boundary, att, file.size());
    else
      getAttachHeader(buf, boundary, att, file.size());

    if (!bdat(smtp, msg, buf.length(), false))
      return false;

    if (smtpSend(smtp, buf.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    return true;
  }

  return false;
}

bool ESP_Mail_Client::openFileRead2(SMTPSession *smtp, SMTP_Message *msg, File &file, const char *path, esp_mail_file_storage_type storageType)
{
  bool file_existed = false;
  MBSTRING filepath;

  if (strlen(path) > 0)
  {
    if (path[0] != '/')
      appendP(filepath, esp_mail_str_202, true);
    filepath += path;
  }

  if (storageType == esp_mail_file_storage_type_sd)
  {
#if defined(ESP_MAIL_SD_FS)
    file_existed = ESP_MAIL_SD_FS.exists(filepath.c_str());
#endif
  }
  else if (storageType == esp_mail_file_storage_type_flash)
  {
#if defined(ESP_MAIL_FLASH_FS)
    file_existed = ESP_MAIL_FLASH_FS.exists(filepath.c_str());
#endif
  }

  if (!file_existed)
  {
    if (smtp->_sendCallback)
      debugInfoP(esp_mail_str_158);

    if (smtp->_debug)
    {
      MBSTRING e;
      appendP(e, esp_mail_str_185, true);
      appendP(e, esp_mail_str_158, false);
      esp_mail_debug(e.c_str());
    }
  }

  if (file_existed)
  {
    if (storageType == esp_mail_file_storage_type_sd)
      file = ESP_MAIL_SD_FS.open(filepath.c_str(), FILE_READ);
    else if (storageType == esp_mail_file_storage_type_flash)
#if defined(ESP32)
      file = ESP_MAIL_FLASH_FS.open(filepath.c_str(), FILE_READ);
#elif defined(ESP8266)
      file = ESP_MAIL_FLASH_FS.open(filepath.c_str(), "r");
#endif
    return true;
  }

  return false;
}

bool ESP_Mail_Client::sendInline(SMTPSession *smtp, SMTP_Message *msg, const MBSTRING &boundary, byte type)
{
  size_t num = numAtt(smtp, esp_mail_att_type_inline, msg) > 0;

  if (num > 0)
  {
    if (smtp->_sendCallback)
    {
      smtpCB(smtp, "");
      smtpCBP(smtp, esp_mail_str_167);
    }

    if (smtp->_debug)
      debugInfoP(esp_mail_str_271);
  }

  MBSTRING s;
  MBSTRING buf;
  MBSTRING related = getBoundary(15);
  int cnt = 0;
  SMTP_Attachment *att = nullptr;

  appendP(s, esp_mail_str_33, true);
  s += boundary;
  appendP(s, esp_mail_str_34, false);

  appendP(s, esp_mail_str_298, false);
  s += related;
  appendP(s, esp_mail_str_35, false);

  if (!bdat(smtp, msg, s.length(), false))
    return false;

  if (smtpSend(smtp, s.c_str()) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  if (!sendPartText(smtp, msg, type, related.c_str()))
    return false;

  if (smtp->_sendCallback && numAtt(smtp, esp_mail_att_type_inline, msg) > 0)
    esp_mail_debug("");

  if (num > 0)
  {
    for (uint8_t i = 0; i < msg->_att.size(); i++)
    {
      att = &msg->_att[i];
      if (att->_int.att_type == esp_mail_att_type_inline)
      {
        appendP(s, esp_mail_str_261, true);
        s += att->descr.filename;

        if (smtp->_sendCallback)
        {
          if (cnt > 0)
            smtpCB(smtp, "");
          smtpCB(smtp, att->descr.filename);
        }

        if (smtp->_debug)
          esp_mail_debug(s.c_str());

        cnt++;

        if (att->file.storage_type == esp_mail_file_storage_type_none)
        {
          if (!att->blob.data)
            continue;

          if (smtp->_sendCallback)
            smtpCB(smtp, att->descr.filename);

          if (smtp->_debug)
            esp_mail_debug(s.c_str());

          buf.clear();
          getInlineHeader(buf, related, att, att->blob.size);

          if (!bdat(smtp, msg, buf.length(), false))
            return false;

          if (smtpSend(smtp, buf.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
            return false;

          if (!sendBlob(smtp, msg, att))
            return false;

          if (!bdat(smtp, msg, 2, false))
            return false;

          if (smtpSendP(smtp, esp_mail_str_34, false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
            return false;
        }
        else
        {
          if (!_sdOk && att->file.storage_type == esp_mail_file_storage_type_sd)
            _sdOk = sdTest();

          if (!_flashOk && att->file.storage_type == esp_mail_file_storage_type_flash)
#if defined(ESP32)
            _flashOk = ESP_MAIL_FLASH_FS.begin(FORMAT_FLASH);
#elif defined(ESP8266)
            _flashOk = ESP_MAIL_FLASH_FS.begin();
#endif

          if ((!_sdOk && att->file.storage_type == esp_mail_file_storage_type_sd) || (!_flashOk && att->file.storage_type == esp_mail_file_storage_type_flash))
          {

            if (smtp->_sendCallback)
              debugInfoP(esp_mail_str_158);

            if (smtp->_debug)
            {
              MBSTRING e;
              appendP(e, esp_mail_str_185, true);
              appendP(e, esp_mail_str_158, false);
              esp_mail_debug(e.c_str());
            }

            continue;
          }

          if (openFileRead(smtp, msg, att, file, s, buf, related, true))
          {
            if (!file)
            {
              errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
              return false;
            }

            if (!sendFile(smtp, msg, att, file))
              return false;

            if (!bdat(smtp, msg, 2, false))
              return false;

            if (smtpSendP(smtp, esp_mail_str_34, false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
              return false;
          }
        }
      }
    }
  }

  appendP(s, esp_mail_str_34, true);
  appendP(s, esp_mail_str_33, false);
  s += related;
  appendP(s, esp_mail_str_33, false);
  appendP(s, esp_mail_str_34, false);

  if (!bdat(smtp, msg, s.length(), false))
    return false;

  if (smtpSend(smtp, s.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  return true;
}

void ESP_Mail_Client::errorStatusCB(SMTPSession *smtp, int error)
{
  smtp->_smtpStatus.statusCode = error;
  MBSTRING s;

  if (smtp->_sendCallback)
  {
    appendP(s, esp_mail_str_53, true);
    s += smtp->errorReason().c_str();
    smtpCB(smtp, s.c_str(), false);
  }

  if (smtp->_debug)
  {
    appendP(s, esp_mail_str_185, true);
    s += smtp->errorReason().c_str();
    esp_mail_debug(s.c_str());
  }
}

size_t ESP_Mail_Client::smtpSendP(SMTPSession *smtp, PGM_P v, bool newline)
{
  if (!reconnect(smtp))
  {
    closeTCPSession(smtp);
    return 0;
  }

  if (!connected(smtp))
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
    return 0;
  }

  if (!smtp->_tcpConnected)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    return 0;
  }

  char *tmp = strP(v);
  size_t len = 0;

  if (newline)
  {
    if (smtp->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug(tmp);
    len = smtp->tcpClient.stream()->println(tmp);
  }
  else
  {
    if (smtp->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug_line(tmp, false);
    len = smtp->tcpClient.stream()->print(tmp);
  }

  if (len != strlen(tmp) && len != strlen(tmp) + 2)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    len = 0;
  }

  delP(&tmp);

  return len;
}

size_t ESP_Mail_Client::smtpSend(SMTPSession *smtp, const char *data, bool newline)
{
  if (!reconnect(smtp))
  {
    closeTCPSession(smtp);
    return 0;
  }

  if (!connected(smtp))
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
    return 0;
  }

  if (!smtp->_tcpConnected)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    return 0;
  }

  size_t len = 0;

  if (newline)
  {
    if (smtp->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug(data);
    len = smtp->tcpClient.stream()->println(data);
  }
  else
  {
    if (smtp->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug_line(data, false);
    len = smtp->tcpClient.stream()->print(data);
  }

  if (len != strlen(data) && len != strlen(data) + 2)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    len = 0;
  }

  return len;
}

size_t ESP_Mail_Client::smtpSend(SMTPSession *smtp, int data, bool newline)
{
  if (!reconnect(smtp))
  {
    closeTCPSession(smtp);
    return 0;
  }

  if (!connected(smtp))
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
    return 0;
  }

  if (!smtp->_tcpConnected)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    return 0;
  }

  char *tmp = intStr(data);
  size_t len = 0;

  if (newline)
  {
    if (smtp->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug(tmp);
    len = smtp->tcpClient.stream()->println(tmp);
  }
  else
  {
    if (smtp->_debugLevel > esp_mail_debug_level_2)
      esp_mail_debug_line(tmp, false);
    len = smtp->tcpClient.stream()->print(tmp);
  }

  if (len != strlen(tmp) && len != strlen(tmp) + 2)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    len = 0;
  }

  delP(&tmp);

  return len;
}

size_t ESP_Mail_Client::smtpSend(SMTPSession *smtp, uint8_t *data, size_t size)
{
  if (!reconnect(smtp))
  {
    closeTCPSession(smtp);
    return 0;
  }

  if (!connected(smtp))
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
    return 0;
  }

  if (!smtp->_tcpConnected)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    return 0;
  }

  size_t len = 0;

  len = smtp->tcpClient.stream()->write(data, size);

  if (len != size)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED);
    len = 0;
  }

  return len;
}

bool ESP_Mail_Client::handleSMTPError(SMTPSession *smtp, int err, bool ret)
{
  if (err < 0)
    errorStatusCB(smtp, err);

  if (smtp->_tcpConnected)
    closeTCPSession(smtp);

  return ret;
}

bool ESP_Mail_Client::sendPartText(SMTPSession *smtp, SMTP_Message *msg, uint8_t type, const char *boundary)
{
  MBSTRING header;

  if (strlen(boundary) > 0)
  {
    appendP(header, esp_mail_str_33, false);
    header += boundary;
    appendP(header, esp_mail_str_34, false);
  }

  if (type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched)
  {
    if (strlen(msg->text.content_type) > 0)
    {
      appendP(header, esp_mail_str_25, false);
      appendP(header, esp_mail_str_131, false);
      header += msg->text.content_type;

      if (strlen(msg->text.charSet) > 0)
      {
        appendP(header, esp_mail_str_97, false);
        appendP(header, esp_mail_str_131, false);
        appendP(header, esp_mail_str_168, false);
        header += msg->text.charSet;
        appendP(header, esp_mail_str_136, false);
      }

      if (msg->text.flowed)
      {
        appendP(header, esp_mail_str_97, false);
        appendP(header, esp_mail_str_131, false);
        appendP(header, esp_mail_str_270, false);

        appendP(header, esp_mail_str_97, false);
        appendP(header, esp_mail_str_131, false);
        appendP(header, esp_mail_str_110, false);
      }

      if (msg->text.embed.enable)
      {
        appendP(header, esp_mail_str_26, false);
        appendP(header, esp_mail_str_164, false);
        appendP(header, esp_mail_str_136, false);
        char *tmp = getRandomUID();
        msg->text._int.cid = tmp;
        delP(&tmp);
      }

      appendP(header, esp_mail_str_34, false);
    }

    if (strlen(msg->text.transfer_encoding) > 0)
    {
      appendP(header, esp_mail_str_272, false);
      header += msg->text.transfer_encoding;
      appendP(header, esp_mail_str_34, false);
    }
  }
  else if (type == esp_mail_msg_type_html)
  {
    if (strlen(msg->text.content_type) > 0)
    {
      appendP(header, esp_mail_str_25, false);
      appendP(header, esp_mail_str_131, false);
      header += msg->html.content_type;

      if (strlen(msg->html.charSet) > 0)
      {
        appendP(header, esp_mail_str_97, false);
        appendP(header, esp_mail_str_131, false);
        appendP(header, esp_mail_str_168, false);
        header += msg->html.charSet;
        appendP(header, esp_mail_str_136, false);
      }
      if (msg->html.embed.enable)
      {
        appendP(header, esp_mail_str_26, false);
        appendP(header, esp_mail_str_159, false);
        appendP(header, esp_mail_str_136, false);
        char *tmp = getRandomUID();
        msg->html._int.cid = tmp;
        delP(&tmp);
      }
      appendP(header, esp_mail_str_34, false);
    }

    if (strlen(msg->html.transfer_encoding) > 0)
    {
      appendP(header, esp_mail_str_272, false);
      header += msg->html.transfer_encoding;
      appendP(header, esp_mail_str_34, false);
    }
  }

  if ((type == esp_mail_msg_type_html && msg->html.embed.enable) || ((type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched) && msg->text.embed.enable))
  {

    if ((type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched) && msg->text.embed.enable)
    {
      if (msg->text.embed.type == esp_mail_smtp_embed_message_type_attachment)
        appendP(header, esp_mail_str_30, false);
      else if (msg->text.embed.type == esp_mail_smtp_embed_message_type_inline)
        appendP(header, esp_mail_str_299, false);

      if (strlen(msg->text.embed.filename) > 0)
        header += msg->text.embed.filename;
      else
        appendP(header, esp_mail_str_164, false);
      appendP(header, esp_mail_str_36, false);

      if (msg->text.embed.type == esp_mail_smtp_embed_message_type_inline)
      {
        appendP(header, esp_mail_str_300, false);
        if (strlen(msg->text.embed.filename) > 0)
          header += msg->text.embed.filename;
        else
          appendP(header, esp_mail_str_159, false);
        appendP(header, esp_mail_str_34, false);

        appendP(header, esp_mail_str_301, false);
        header += msg->text._int.cid;
        appendP(header, esp_mail_str_15, false);
        appendP(header, esp_mail_str_34, false);
      }
    }
    else if (type == esp_mail_msg_type_html && msg->html.embed.enable)
    {
      if (msg->html.embed.type == esp_mail_smtp_embed_message_type_attachment)
        appendP(header, esp_mail_str_30, false);
      else if (msg->html.embed.type == esp_mail_smtp_embed_message_type_inline)
        appendP(header, esp_mail_str_299, false);

      if (strlen(msg->html.embed.filename) > 0)
        header += msg->html.embed.filename;
      else
        appendP(header, esp_mail_str_159, false);
      appendP(header, esp_mail_str_36, false);

      if (msg->html.embed.type == esp_mail_smtp_embed_message_type_inline)
      {
        appendP(header, esp_mail_str_300, false);
        if (strlen(msg->html.embed.filename) > 0)
          header += msg->html.embed.filename;
        else
          appendP(header, esp_mail_str_159, false);
        appendP(header, esp_mail_str_34, false);

        appendP(header, esp_mail_str_301, false);
        header += msg->html._int.cid;
        appendP(header, esp_mail_str_15, false);
        appendP(header, esp_mail_str_34, false);
      }
    }
  }

  appendP(header, esp_mail_str_34, false);

  if ((msg->text.blob.size > 0 && (type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched)) || (msg->html.blob.size > 0 && type == esp_mail_msg_type_html))
  {
    if (!bdat(smtp, msg, header.length(), false))
      return false;

    if (smtpSend(smtp, header.c_str()) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    header.clear();

    if (!sendBlobBody(smtp, msg, type))
      return false;
  }
  else if ((strlen(msg->text.file.name) > 0 && (type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched)) || (strlen(msg->html.file.name) > 0 && type == esp_mail_msg_type_html))
  {
    if (!bdat(smtp, msg, header.length(), false))
      return false;

    if (smtpSend(smtp, header.c_str()) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    header.clear();

    if (!sendFileBody(smtp, msg, type))
      return false;
  }
  else
    encodingText(smtp, msg, type, header);

  appendP(header, esp_mail_str_34, false);

  if (strlen(boundary) > 0)
    appendP(header, esp_mail_str_34, false);

  if (!bdat(smtp, msg, header.length(), false))
    return false;

  if (smtpSend(smtp, header.c_str()) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
    return false;

  return true;
}

bool ESP_Mail_Client::sendBlobBody(SMTPSession *smtp, SMTP_Message *msg, uint8_t type)
{

  if (msg->text.blob.size == 0 && msg->html.blob.size == 0)
    return true;

  bool ret = true;
  int bufLen = 512;
  size_t pos = 0;
  int pg = 0, _pg = 0;

  if (type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched)
  {
    char *tmp = strP(esp_mail_str_325);

    if (strlen(msg->text.transfer_encoding) > 0)
    {
      if (strcmp(msg->text.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0)
      {
        ret = sendBase64(smtp, msg, (const unsigned char *)msg->text.blob.data, msg->text.blob.size, true, tmp, smtp->_sendCallback != NULL);
        delP(&tmp);
        return ret;
      }
    }

    int len = msg->text.blob.size;
    int available = len;
    uint8_t *buf = (uint8_t *)newP(bufLen + 1);
    while (available)
    {
      if (available > bufLen)
        available = bufLen;

      memcpy_P(buf, msg->text.blob.data + pos, available);

      if (!bdat(smtp, msg, available, false))
        break;
      if (smtpSend(smtp, buf, available) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        break;
      pos += available;
      len -= available;
      available = len;
      if (smtp->_sendCallback)
      {
        pg = (float)(100.0f * pos / msg->text.blob.size);
        if (pg != _pg)
          uploadReport(tmp, pg);
        _pg = pg;
      }
    }
    delP(&buf);
    delP(&tmp);
  }
  else if (type == esp_mail_message_type::esp_mail_msg_type_html)
  {
    char *tmp = strP(esp_mail_str_325);

    if (strlen(msg->html.transfer_encoding) > 0)
    {
      if (strcmp(msg->html.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0)
      {
        ret = sendBase64(smtp, msg, (const unsigned char *)msg->html.blob.data, msg->html.blob.size, true, tmp, smtp->_sendCallback != NULL);
        delP(&tmp);
        return ret;
      }
    }
    int len = msg->html.blob.size;
    int available = len;
    uint8_t *buf = (uint8_t *)newP(bufLen + 1);
    while (available)
    {

      if (available > bufLen)
        available = bufLen;

      memcpy_P(buf, msg->html.blob.data + pos, available);

      if (!bdat(smtp, msg, available, false))
      {
        ret = false;
        break;
      }

      if (smtpSend(smtp, buf, available) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      {
        ret = false;
        break;
      }
      pos += available;
      len -= available;
      available = len;
      if (smtp->_sendCallback)
      {
        pg = (float)(100.0f * pos / msg->html.blob.size);
        if (pg != _pg)
          uploadReport(tmp, pg);
        _pg = pg;
      }
    }
    delP(&buf);
    delP(&tmp);
  }
  return ret;
}

bool ESP_Mail_Client::sendFileBody(SMTPSession *smtp, SMTP_Message *msg, uint8_t type)
{

  if (strlen(msg->text.file.name) == 0 && strlen(msg->html.file.name) == 0)
    return true;

  bool ret = true;
  size_t chunkSize = ESP_MAIL_CLIENT_STREAM_CHUNK_SIZE;
  size_t writeLen = 0;
  int pg = 0, _pg = 0;

  if (type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched)
  {

    if (!openFileRead2(smtp, msg, file, msg->text.file.name, msg->text.file.type))
      return false;

    char *tmp = strP(esp_mail_str_326);

    if (strlen(msg->text.transfer_encoding) > 0)
    {
      if (strcmp(msg->text.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0)
      {
        ret = sendBase64Stream(smtp, msg, file, tmp, smtp->_sendCallback != NULL);
        delP(&tmp);
        return ret;
      }
    }

    if (file.size() > 0)
    {

      if (file.size() < chunkSize)
        chunkSize = file.size();
      uint8_t *buf = (uint8_t *)newP(chunkSize);
      while (writeLen < file.size() && file.available())
      {
        if (writeLen > file.size() - chunkSize)
          chunkSize = file.size() - writeLen;
        size_t readLen = file.read(buf, chunkSize);

        if (readLen != chunkSize)
        {
          errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
          break;
        }

        if (!bdat(smtp, msg, chunkSize, false))
        {
          ret = false;
          break;
        }

        if (smtpSend(smtp, buf, chunkSize) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        {
          ret = false;
          break;
        }

        if (smtp->_sendCallback)
        {
          pg = (float)(100.0f * writeLen / file.size());
          if (pg != _pg)
            uploadReport(tmp, pg);
          _pg = pg;
        }
        writeLen += chunkSize;
      }
      delP(&buf);
      if (smtp->_sendCallback && _pg < 100)
        uploadReport(tmp, 100);

      delP(&tmp);
      return ret && writeLen == file.size();
    }
  }
  else if (type == esp_mail_message_type::esp_mail_msg_type_html)
  {

    if (!openFileRead2(smtp, msg, file, msg->html.file.name, msg->html.file.type))
      return false;

    char *tmp = strP(esp_mail_str_326);

    if (strlen(msg->html.transfer_encoding) > 0)
    {
      if (strcmp(msg->html.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0)
      {
        ret = sendBase64Stream(smtp, msg, file, tmp, smtp->_sendCallback != NULL);
        delP(&tmp);
        return ret;
      }
    }

    if (file.size() > 0)
    {

      if (file.size() < chunkSize)
        chunkSize = file.size();
      uint8_t *buf = (uint8_t *)newP(chunkSize);
      while (writeLen < file.size() && file.available())
      {
        if (writeLen > file.size() - chunkSize)
          chunkSize = file.size() - writeLen;
        size_t readLen = file.read(buf, chunkSize);

        if (readLen != chunkSize)
        {
          errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
          break;
        }

        if (!bdat(smtp, msg, chunkSize, false))
        {
          ret = false;
          break;
        }

        if (smtpSend(smtp, buf, chunkSize) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        {
          ret = false;
          break;
        }

        if (smtp->_sendCallback)
        {
          pg = (float)(100.0f * writeLen / file.size());
          if (pg != _pg)
            uploadReport(tmp, pg);
          _pg = pg;
        }
        writeLen += chunkSize;
      }
      delP(&buf);
      if (smtp->_sendCallback && _pg < 100)
        uploadReport(tmp, 100);

      delP(&tmp);
      return ret && writeLen == file.size();
    }
  }

  return false;
}

void ESP_Mail_Client::encodingText(SMTPSession *smtp, SMTP_Message *msg, uint8_t type, MBSTRING &content)
{
  if (type == esp_mail_msg_type_plain || type == esp_mail_msg_type_enriched)
  {
    MBSTRING s = msg->text.content;

    if (msg->text.flowed)
      formatFlowedText(s);

    if (strlen(msg->text.transfer_encoding) > 0)
    {
      if (strcmp(msg->text.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0)
        content += encodeBase64Str((const unsigned char *)s.c_str(), s.length());
      else if (strcmp(msg->text.transfer_encoding, Content_Transfer_Encoding::enc_qp) == 0)
      {
        char *out = (char *)newP(s.length() * 3 + 1);
        encodeQP(s.c_str(), out);
        content += out;
        delP(&out);
      }
      else
        content += s;
    }
    else
      content += s;
  }
  else if (type == esp_mail_message_type::esp_mail_msg_type_html)
  {
    char *tmp = nullptr;
    MBSTRING s = msg->html.content;
    MBSTRING fnd, rep;
    SMTP_Attachment *att = nullptr;
    for (uint8_t i = 0; i < msg->_att.size(); i++)
    {
      att = &msg->_att[i];
      if (att->_int.att_type == esp_mail_att_type_inline)
      {
        MBSTRING filename(att->descr.filename);

        size_t found = filename.find_last_of("/\\");
        if (found != MBSTRING::npos)
          filename = filename.substr(found + 1);

        appendP(fnd, esp_mail_str_136, true);
        fnd += filename;
        appendP(fnd, esp_mail_str_136, false);

        appendP(rep, esp_mail_str_136, true);
        appendP(rep, esp_mail_str_302, false);
        if (strlen(att->descr.content_id) > 0)
          rep += att->descr.content_id;
        else
          rep += att->_int.cid;
        appendP(rep, esp_mail_str_136, false);

        tmp = strReplace((char *)s.c_str(), (char *)fnd.c_str(), (char *)rep.c_str());
        s = tmp;
        delP(&tmp);
      }
    }

    if (strlen(msg->html.transfer_encoding) > 0)
    {
      if (strcmp(msg->html.transfer_encoding, Content_Transfer_Encoding::enc_base64) == 0)
        content += encodeBase64Str((const unsigned char *)s.c_str(), s.length());
      else if (strcmp(msg->html.transfer_encoding, Content_Transfer_Encoding::enc_qp) == 0)
      {
        char *out = (char *)newP(strlen(msg->html.content) * 3 + 1);
        encodeQP(msg->html.content, out);
        content += out;
        delP(&out);
      }
      else
        content += s;
    }
    else
      content += s;
    MBSTRING().swap(s);
  }
}

void ESP_Mail_Client::encodeQP(const char *buf, char *out)
{
  int n = 0, p = 0, pos = 0;
  for (n = 0; *buf; buf++)
  {
    if (n >= 73 && *buf != 10 && *buf != 13)
    {
      p = sprintf(out + pos, "=\r\n");
      pos += p;
      n = 0;
    }

    if (*buf == 10 || *buf == 13)
    {
      strcat_c(out, *buf);
      pos++;
      n = 0;
    }
    else if (*buf < 32 || *buf == 61 || *buf > 126)
    {
      p = sprintf(out + pos, "=%02X", (unsigned char)*buf);
      n += p;
      pos += p;
    }
    else if (*buf != 32 || (*(buf + 1) != 10 && *(buf + 1) != 13))
    {
      strcat_c(out, *buf);
      n++;
      pos++;
    }
    else
    {
      p = sprintf(out + pos, "=20");
      n += p;
      pos += p;
    }
  }
}

/** Add the soft line break to the long text line (rfc 3676) 
 * and add Format=flowed parameter in the plain text content-type header.
 * We use the existing white space as a part of this soft line break
 * and set delSp="no" parameter to the header.
 * 
 * Some servers are not rfc 3676 compliant.
 * This causes the text lines are wrapped instead of joined.
 * 
 * Some mail clients trim the space before the line break
 * which makes the soft line break cannot be seen.
*/
void ESP_Mail_Client::formatFlowedText(MBSTRING &content)
{
  int count = 0;
  MBSTRING qms;
  int j = 0;
  std::vector<MBSTRING> tokens = std::vector<MBSTRING>();
  char *stk = strP(esp_mail_str_34);
  char *qm = strP(esp_mail_str_15);
  splitTk(content, tokens, stk);
  content.clear();
  for (size_t i = 0; i < tokens.size(); i++)
  {
    if (tokens[i].length() > 0)
    {
      j = 0;
      qms.clear();
      while (tokens[i][j] == qm[0])
      {
        qms += qm;
        j++;
      }
      softBreak(tokens[i], qms.c_str());
      if (count > 0)
        content += stk;
      content += tokens[i];
    }
    else if (count > 0)
      content += stk;
    count++;
  }

  delP(&stk);
  delP(&qm);
  tokens.clear();
}

void ESP_Mail_Client::softBreak(MBSTRING &content, const char *quoteMarks)
{
  size_t len = 0;
  char *stk = strP(esp_mail_str_131);
  std::vector<MBSTRING> tokens = std::vector<MBSTRING>();
  splitTk(content, tokens, stk);
  content.clear();
  for (size_t i = 0; i < tokens.size(); i++)
  {
    if (tokens[i].length() > 0)
    {
      if (len + tokens[i].length() + 3 > FLOWED_TEXT_LEN)
      {
        /* insert soft crlf */
        content += stk;
        appendP(content, esp_mail_str_34, false);

        /* insert quote marks */
        if (strlen(quoteMarks) > 0)
          content += quoteMarks;
        content += tokens[i];
        len = tokens[i].length();
      }
      else
      {
        if (len > 0)
        {
          content += stk;
          len += strlen(stk);
        }
        content += tokens[i];
        len += tokens[i].length();
      }
    }
  }
  delP(&stk);
  tokens.clear();
}

bool ESP_Mail_Client::sendMSG(SMTPSession *smtp, SMTP_Message *msg, const MBSTRING &boundary)
{
  MBSTRING alt = getBoundary(15);
  MBSTRING s;

  if (numAtt(smtp, esp_mail_att_type_inline, msg) > 0)
  {
    appendP(s, esp_mail_str_297, true);
    s += alt;
    appendP(s, esp_mail_str_35, false);

    if (!bdat(smtp, msg, s.length(), false))
      return false;

    if (smtpSend(smtp, s.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;

    if (msg->type == esp_mail_msg_type_plain || msg->type == esp_mail_msg_type_enriched || msg->type == esp_mail_msg_type_html)
    {
      if (!sendInline(smtp, msg, alt, msg->type))
        return false;
    }
    else if (msg->type == (esp_mail_msg_type_html | esp_mail_msg_type_enriched | esp_mail_msg_type_plain))
    {
      if (!sendPartText(smtp, msg, esp_mail_msg_type_plain, alt.c_str()))
        return false;
      if (!sendInline(smtp, msg, alt, esp_mail_msg_type_html))
        return false;
    }

    appendP(s, esp_mail_str_33, true);
    s += alt;
    appendP(s, esp_mail_str_33, false);
    appendP(s, esp_mail_str_34, false);

    if (!bdat(smtp, msg, s.length(), false))
      return false;

    if (smtpSend(smtp, s.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      return false;
  }
  else
  {
    if (msg->type == esp_mail_msg_type_plain || msg->type == esp_mail_msg_type_enriched || msg->type == esp_mail_msg_type_html)
    {
      if (!sendPartText(smtp, msg, msg->type, ""))
        return false;
    }
    else if (msg->type == (esp_mail_msg_type_html | esp_mail_msg_type_enriched | esp_mail_msg_type_plain))
    {
      appendP(s, esp_mail_str_33, true);
      s += boundary;
      appendP(s, esp_mail_str_34, false);
      appendP(s, esp_mail_str_297, false);
      s += alt;
      appendP(s, esp_mail_str_35, false);

      if (!bdat(smtp, msg, s.length(), false))
        return false;

      if (smtpSend(smtp, s.c_str(), false) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        return false;

      if (!sendPartText(smtp, msg, esp_mail_msg_type_plain, alt.c_str()))
        return false;

      if (!sendPartText(smtp, msg, esp_mail_msg_type_html, alt.c_str()))
        return false;
    }
  }
  return true;
}

void ESP_Mail_Client::getInlineHeader(MBSTRING &header, const MBSTRING &boundary, SMTP_Attachment *inlineAttach, size_t size)
{
  appendP(header, esp_mail_str_33, false);
  header += boundary;
  appendP(header, esp_mail_str_34, false);

  appendP(header, esp_mail_str_25, false);
  appendP(header, esp_mail_str_131, false);

  if (strlen(inlineAttach->descr.mime) == 0)
  {
    MBSTRING mime;
    mimeFromFile(inlineAttach->descr.filename, mime);
    if (mime.length() > 0)
      header += mime;
    else
      appendP(header, esp_mail_str_32, false);
  }
  else
    header += inlineAttach->descr.mime;

  appendP(header, esp_mail_str_26, false);

  MBSTRING filename = inlineAttach->descr.filename;

  size_t found = filename.find_last_of("/\\");

  if (found != MBSTRING::npos)
    filename = filename.substr(found + 1);

  header += filename;
  appendP(header, esp_mail_str_36, false);

  appendP(header, esp_mail_str_299, false);
  header += filename;
  appendP(header, esp_mail_str_327, false);
  char *tmp = intStr(size);
  header += tmp;
  delP(&tmp);
  appendP(header, esp_mail_str_34, false);

  appendP(header, esp_mail_str_300, false);
  header += filename;
  appendP(header, esp_mail_str_34, false);

  appendP(header, esp_mail_str_301, false);
  if (strlen(inlineAttach->descr.content_id) > 0)
    header += inlineAttach->descr.content_id;
  else
    header += inlineAttach->_int.cid;

  appendP(header, esp_mail_str_15, false);

  appendP(header, esp_mail_str_34, false);

  if (strlen(inlineAttach->descr.transfer_encoding) > 0)
  {
    appendP(header, esp_mail_str_272, false);
    header += inlineAttach->descr.transfer_encoding;
    appendP(header, esp_mail_str_34, false);
  }
  appendP(header, esp_mail_str_34, false);

  MBSTRING().swap(filename);
}

void ESP_Mail_Client::getAttachHeader(MBSTRING &header, const MBSTRING &boundary, SMTP_Attachment *attach, size_t size)
{
  appendP(header, esp_mail_str_33, false);
  header += boundary;
  appendP(header, esp_mail_str_34, false);

  appendP(header, esp_mail_str_25, false);
  appendP(header, esp_mail_str_131, false);

  if (strlen(attach->descr.mime) == 0)
  {
    MBSTRING mime;
    mimeFromFile(attach->descr.filename, mime);
    if (mime.length() > 0)
      header += mime;
    else
      appendP(header, esp_mail_str_32, false);
  }
  else
    header += attach->descr.mime;

  appendP(header, esp_mail_str_26, false);

  MBSTRING filename = attach->descr.filename;

  size_t found = filename.find_last_of("/\\");
  if (found != MBSTRING::npos)
    filename = filename.substr(found + 1);

  header += filename;
  appendP(header, esp_mail_str_36, false);

  if (!attach->_int.parallel)
  {
    appendP(header, esp_mail_str_30, false);
    header += filename;
    appendP(header, esp_mail_str_327, false);
    char *tmp = intStr(size);
    header += tmp;
    delP(&tmp);
    appendP(header, esp_mail_str_34, false);
  }

  if (strlen(attach->descr.transfer_encoding) > 0)
  {
    appendP(header, esp_mail_str_272, false);
    header += attach->descr.transfer_encoding;
    appendP(header, esp_mail_str_34, false);
  }

  appendP(header, esp_mail_str_34, false);

  MBSTRING().swap(filename);
}

void ESP_Mail_Client::getRFC822PartHeader(SMTPSession *smtp, MBSTRING &header, const MBSTRING &boundary)
{
  appendP(header, esp_mail_str_33, false);
  header += boundary;
  appendP(header, esp_mail_str_34, false);

  appendP(header, esp_mail_str_25, false);
  appendP(header, esp_mail_str_131, false);

  appendP(header, esp_mail_str_123, false);
  appendP(header, esp_mail_str_34, false);

  appendP(header, esp_mail_str_98, false);

  appendP(header, esp_mail_str_34, false);
}

void ESP_Mail_Client::smtpCBP(SMTPSession *smtp, PGM_P info, bool success)
{
  MBSTRING s;
  appendP(s, info, true);
  smtp->_cbData._info = s;
  smtp->_cbData._success = success;
  smtp->_sendCallback(smtp->_cbData);
  MBSTRING().swap(s);
}

void ESP_Mail_Client::smtpCB(SMTPSession *smtp, const char *info, bool success)
{
  smtp->_cbData._info = info;
  smtp->_cbData._success = success;
  smtp->_sendCallback(smtp->_cbData);
}

bool ESP_Mail_Client::sendBase64Raw(SMTPSession *smtp, SMTP_Message *msg, const uint8_t *data, size_t len, bool flashMem, const char *filename, bool report)
{
  bool ret = false;

  size_t chunkSize = (BASE64_CHUNKED_LEN * UPLOAD_CHUNKS_NUM) + (2 * UPLOAD_CHUNKS_NUM);
  size_t bufIndex = 0;
  size_t dataIndex = 0;

  if (len < chunkSize)
    chunkSize = len;

  uint8_t *buf = (uint8_t *)newP(chunkSize);
  memset(buf, 0, chunkSize);

  int pg = 0, _pg = 0;

  if (report)
    uploadReport(filename, dataIndex);

  while (dataIndex < len - 1)
  {

    size_t size = 4;
    if (dataIndex + size > len - 1)
      size = len - dataIndex;

    if (flashMem)
      memcpy_P(buf + bufIndex, data + dataIndex, size);
    else
      memcpy(buf + bufIndex, data + dataIndex, size);

    dataIndex += size;
    bufIndex += size;

    if (bufIndex + 1 == BASE64_CHUNKED_LEN)
    {
      if (bufIndex + 2 < chunkSize)
      {
        buf[bufIndex++] = 0x0d;
        buf[bufIndex++] = 0x0a;
      }
    }

    if (bufIndex + 1 >= chunkSize - size)
    {
      if (!bdat(smtp, msg, bufIndex + 1, false))
        goto ex;

      if (smtpSend(smtp, buf, bufIndex + 1) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        goto ex;

      bufIndex = 0;
    }

    if (report)
    {
      pg = (float)(100.0f * dataIndex / len);
      if (pg != _pg)
        uploadReport(filename, pg);
      _pg = pg;
    }
  }

  if (report && _pg < 100)
    uploadReport(filename, 100);

  ret = true;
ex:
  if (report)
    esp_mail_debug("");
  delP(&buf);
  return ret;
}

bool ESP_Mail_Client::sendBase64Stream(SMTPSession *smtp, SMTP_Message *msg, File file, const char *filename, bool report)
{
  bool ret = false;

  if (!file)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
    return false;
  }

  size_t chunkSize = (BASE64_CHUNKED_LEN * UPLOAD_CHUNKS_NUM) + (2 * UPLOAD_CHUNKS_NUM);
  size_t byteAdded = 0;
  size_t byteSent = 0;

  unsigned char *buf = (unsigned char *)newP(chunkSize);
  memset(buf, 0, chunkSize);

  size_t len = file.size();
  size_t fbufIndex = 0;
  unsigned char *fbuf = (unsigned char *)newP(3);

  int dByte = 0;

  int bcnt = 0;
  int pg = 0, _pg = 0;

  if (report)
    uploadReport(filename, bcnt);

  while (file.available())
  {
    memset(fbuf, 0, 3);
    if (len - fbufIndex >= 3)
    {
      bcnt += 3;
      size_t readLen = file.read(fbuf, 3);
      if (readLen != 3)
      {
        errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
        break;
      }

      buf[byteAdded++] = b64_index_table[fbuf[0] >> 2];
      buf[byteAdded++] = b64_index_table[((fbuf[0] & 0x03) << 4) | (fbuf[1] >> 4)];
      buf[byteAdded++] = b64_index_table[((fbuf[1] & 0x0f) << 2) | (fbuf[2] >> 6)];
      buf[byteAdded++] = b64_index_table[fbuf[2] & 0x3f];
      dByte += 4;
      if (dByte == BASE64_CHUNKED_LEN)
      {
        if (byteAdded + 1 < chunkSize)
        {
          buf[byteAdded++] = 0x0d;
          buf[byteAdded++] = 0x0a;
        }
        dByte = 0;
      }
      if (byteAdded >= chunkSize - 4)
      {
        byteSent += byteAdded;

        if (!bdat(smtp, msg, byteAdded, false))
          goto ex;

        if (smtpSend(smtp, buf, byteAdded) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
          goto ex;

        memset(buf, 0, chunkSize);
        byteAdded = 0;
      }
      fbufIndex += 3;

      if (report)
      {
        pg = (float)(100.0f * bcnt / len);
        if (pg != _pg)
          uploadReport(filename, pg);
        _pg = pg;
      }
    }
    else
    {
      size_t readLen = file.read(fbuf, len - fbufIndex);
      if (readLen != len - fbufIndex)
      {
        errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
        break;
      }
    }
  }

  file.close();
  if (byteAdded > 0)
  {
    if (!bdat(smtp, msg, byteAdded, false))
      goto ex;

    if (smtpSend(smtp, buf, byteAdded) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      goto ex;
  }

  if (len - fbufIndex > 0)
  {
    memset(buf, 0, chunkSize);
    byteAdded = 0;
    buf[byteAdded++] = b64_index_table[fbuf[0] >> 2];
    if (len - fbufIndex == 1)
    {
      buf[byteAdded++] = b64_index_table[(fbuf[0] & 0x03) << 4];
      buf[byteAdded++] = '=';
    }
    else
    {
      buf[byteAdded++] = b64_index_table[((fbuf[0] & 0x03) << 4) | (fbuf[1] >> 4)];
      buf[byteAdded++] = b64_index_table[(fbuf[1] & 0x0f) << 2];
    }
    buf[byteAdded++] = '=';

    if (!bdat(smtp, msg, byteAdded, false))
      goto ex;

    if (smtpSend(smtp, buf, byteAdded) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      goto ex;
  }
  ret = true;

  if (report && _pg < 100)
    uploadReport(filename, 100);

ex:
  delP(&buf);
  delP(&fbuf);
  file.close();
  return ret;
}

bool ESP_Mail_Client::sendBase64StreamRaw(SMTPSession *smtp, SMTP_Message *msg, File file, const char *filename, bool report)
{
  bool ret = false;

  if (!file)
  {
    errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
    return false;
  }

  size_t chunkSize = (BASE64_CHUNKED_LEN * UPLOAD_CHUNKS_NUM) + (2 * UPLOAD_CHUNKS_NUM);
  size_t bufIndex = 0;
  size_t dataIndex = 0;

  size_t len = file.size();

  if (len < chunkSize)
    chunkSize = len;

  uint8_t *buf = (uint8_t *)newP(chunkSize);
  memset(buf, 0, chunkSize);

  int pg = 0, _pg = 0;

  uint8_t *fbuf = (uint8_t *)newP(4);

  if (report)
    uploadReport(filename, bufIndex);

  while (file.available())
  {

    if (dataIndex < len - 1)
    {
      size_t size = 4;
      if (dataIndex + size > len - 1)
        size = len - dataIndex;

      size_t readLen = file.read(fbuf, size);
      if (readLen != size)
      {
        errorStatusCB(smtp, MAIL_CLIENT_ERROR_FILE_IO_ERROR);
        break;
      }

      memcpy(buf + bufIndex, fbuf, size);

      bufIndex += size;

      if (bufIndex + 1 == BASE64_CHUNKED_LEN)
      {
        if (bufIndex + 2 < chunkSize)
        {
          buf[bufIndex++] = 0x0d;
          buf[bufIndex++] = 0x0a;
        }
      }

      if (bufIndex + 1 >= chunkSize - size)
      {
        if (!bdat(smtp, msg, bufIndex + 1, false))
          goto ex;

        if (smtpSend(smtp, buf, bufIndex + 1) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
          goto ex;

        bufIndex = 0;
      }

      if (report)
      {
        pg = (float)(100.0f * dataIndex / len);
        if (pg != _pg)
          uploadReport(filename, pg);
        _pg = pg;
      }
    }
  }

  file.close();
  ret = true;

  if (report && _pg < 100)
    uploadReport(filename, 100);

ex:
  delP(&buf);
  delP(&fbuf);
  file.close();
  return ret;
}

void ESP_Mail_Client::handleAuth(SMTPSession *smtp, char *buf)
{
  if (strposP(buf, esp_mail_smtp_response_1, 0) > -1)
  {
    if (strposP(buf, esp_mail_smtp_response_2, 0) > -1)
      smtp->_auth_capability.login = true;
    if (strposP(buf, esp_mail_smtp_response_3, 0) > -1)
      smtp->_auth_capability.plain = true;
    if (strposP(buf, esp_mail_smtp_response_4, 0) > -1)
      smtp->_auth_capability.xoauth2 = true;
    if (strposP(buf, esp_mail_smtp_response_11, 0) > -1)
      smtp->_auth_capability.cram_md5 = true;
    if (strposP(buf, esp_mail_smtp_response_12, 0) > -1)
      smtp->_auth_capability.digest_md5 = true;
  }
  else if (strposP(buf, esp_mail_smtp_response_5, 0) > -1)
    smtp->_auth_capability.start_tls = true;
  else if (strposP(buf, esp_mail_smtp_response_6, 0) > -1)
    smtp->_send_capability._8bitMIME = true;
  else if (strposP(buf, esp_mail_smtp_response_7, 0) > -1)
    smtp->_send_capability.binaryMIME = true;
  else if (strposP(buf, esp_mail_smtp_response_8, 0) > -1)
    smtp->_send_capability.chunking = true;
  else if (strposP(buf, esp_mail_smtp_response_9, 0) > -1)
    smtp->_send_capability.utf8 = true;
  else if (strposP(buf, esp_mail_smtp_response_10, 0) > -1)
    smtp->_send_capability.pipelining = true;
  else if (strposP(buf, esp_mail_smtp_response_13, 0) > -1)
    smtp->_send_capability.dsn = true;
}

int ESP_Mail_Client::available(SMTPSession *smtp)
{
  int sz = 0;
  if (smtp->tcpClient.stream())
    sz = smtp->tcpClient.stream()->available();
  return sz;
}

bool ESP_Mail_Client::handleSMTPResponse(SMTPSession *smtp, esp_mail_smtp_status_code respCode, int errCode)
{
  if (!reconnect(smtp))
    return false;

  bool ret = false;
  char *response = nullptr;
  int readLen = 0;
  long dataTime = millis();
  int chunkBufSize = 0;
  MBSTRING s, r;
  int chunkIndex = 0;
  int count = 0;
  bool completedResponse = false;
  smtp->_smtpStatus.statusCode = 0;
  smtp->_smtpStatus.respCode = 0;
  smtp->_smtpStatus.text.clear();
  uint8_t minResLen = 5;
  struct esp_mail_smtp_response_status_t status;

  chunkBufSize = available(smtp);

  while (smtp->_tcpConnected && chunkBufSize <= 0)
  {
    if (!reconnect(smtp, dataTime))
      return false;
    if (!connected(smtp))
    {
      errorStatusCB(smtp, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
      return false;
    }
    chunkBufSize = available(smtp);
    delay(0);
  }

  dataTime = millis();

  if (chunkBufSize > 1)
  {
    while (!completedResponse)
    {
      delay(0);

      if (!reconnect(smtp, dataTime))
        return false;

      if (!connected(smtp))
      {
        errorStatusCB(smtp, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);
        return false;
      }

      chunkBufSize = available(smtp);

      if (chunkBufSize <= 0)
        break;

      if (chunkBufSize > 0)
      {
        chunkBufSize = 512;
        response = (char *)newP(chunkBufSize + 1);

        readLen = readLine(smtp->tcpClient.stream(), response, chunkBufSize, false, count);

        if (readLen)
        {
          if (smtp->_smtp_cmd != esp_mail_smtp_command::esp_mail_smtp_cmd_initial_state)
          {
            //sometimes server sent multiple lines response
            //sometimes rx buffer may not ready for a while
            if (strlen(response) < minResLen)
            {
              r += response;
              chunkBufSize = 0;
              while (chunkBufSize == 0)
              {
                delay(0);
                if (!reconnect(smtp, dataTime))
                  return false;
                chunkBufSize = available(smtp);
              }
            }
            else
            {
              if (r.length() > 0)
              {
                r += response;
                memset(response, 0, chunkBufSize);
                strcpy(response, r.c_str());
              }

              if (smtp->_debugLevel > esp_mail_debug_level_1)
                esp_mail_debug((const char *)response);
            }

            if (smtp->_smtp_cmd == esp_mail_smtp_command::esp_mail_smtp_cmd_greeting)
              handleAuth(smtp, response);
          }

          getResponseStatus(response, respCode, 0, status);

          //get the status code again for unexpected return code
          if (smtp->_smtp_cmd == esp_mail_smtp_command::esp_mail_smtp_cmd_start_tls || status.respCode == 0)
            getResponseStatus(response, esp_mail_smtp_status_code_0, 0, status);

          ret = respCode == status.respCode;
          smtp->_smtpStatus = status;

          if (status.respCode > 0 && (status.respCode < 400 || status.respCode == respCode))
            ret = true;

          if (smtp->_debug && strlen(response) >= minResLen)
          {
            appendP(s, esp_mail_str_260, true);
            if (smtp->_smtpStatus.respCode != esp_mail_smtp_status_code_334)
              s += response;
            else
            {
              //base64 response
              size_t olen;
              char *decoded = (char *)decodeBase64((const unsigned char *)status.text.c_str(), status.text.length(), &olen);
              if (decoded && olen > 0)
              {
                olen += s.length();
                s += decoded;
                s[olen] = 0;
                delP(&decoded);
              }
            }
            esp_mail_debug(s.c_str());
            r.clear();
          }

          completedResponse = smtp->_smtpStatus.respCode > 0 && status.text.length() > minResLen;

          if (smtp->_smtp_cmd == esp_mail_smtp_command::esp_mail_smtp_cmd_auth && smtp->_smtpStatus.respCode == esp_mail_smtp_status_code_334)
          {
            if (authFailed(response, readLen, chunkIndex, 4))
            {
              smtp->_smtpStatus.statusCode = -1;
              ret = false;
            }
          }

          chunkIndex++;

          if (smtp->_chunkedEnable && smtp->_smtp_cmd == esp_mail_smtp_command::esp_mail_smtp_cmd_chunk_termination)
            completedResponse = smtp->_chunkCount == chunkIndex;
        }
        delP(&response);
      }
    }

    if (!ret)
      handleSMTPError(smtp, errCode, false);
  }

  return ret;
}

void ESP_Mail_Client::getResponseStatus(const char *buf, esp_mail_smtp_status_code respCode, int beginPos, struct esp_mail_smtp_response_status_t &status)
{
  MBSTRING s;
  char *tmp = nullptr;
  int p1 = 0;
  if (respCode > esp_mail_smtp_status_code_0)
  {
    tmp = intStr((int)respCode);
    s = tmp;
    appendP(s, esp_mail_str_131, false);
    delP(&tmp);
    p1 = strpos(buf, (const char *)s.c_str(), beginPos);
  }

  if (p1 != -1)
  {
    int ofs = s.length() - 2;
    if (ofs < 0)
      ofs = 1;

    int p2 = strposP(buf, esp_mail_str_131, p1 + ofs);

    if (p2 < 4 && p2 > -1)
    {
      tmp = (char *)newP(p2 + 1);
      memcpy(tmp, &buf[p1], p2);
      status.respCode = atoi(tmp);
      delP(&tmp);

      p1 = p2 + 1;
      p2 = strlen(buf);
      if (p2 > p1)
      {
        tmp = (char *)newP(p2 + 1);
        memcpy(tmp, &buf[p1], p2 - p1);
        status.text = tmp;
        delP(&tmp);
      }
    }
  }
}

void ESP_Mail_Client::closeTCPSession(SMTPSession *smtp)
{
  if (smtp->_tcpConnected)
  {
    if (smtp->tcpClient.stream())
    {
      if (connected(smtp))
      {
        smtp->tcpClient.stream()->stop();
      }
    }
    _lastReconnectMillis = millis();
  }
  smtp->_tcpConnected = false;
}

bool ESP_Mail_Client::reconnect(SMTPSession *smtp, unsigned long dataTime)
{

  bool status = WiFi.status() == WL_CONNECTED || ethLinkUp(smtp->_sesson_cfg);

  if (dataTime > 0)
  {
    if (millis() - dataTime > smtp->tcpClient.tcpTimeout)
    {
      closeTCPSession(smtp);
      errorStatusCB(smtp, MAIL_CLIENT_ERROR_READ_TIMEOUT);
      return false;
    }
  }

  if (!status)
  {
    if (smtp->_tcpConnected)
      closeTCPSession(smtp);

    errorStatusCB(smtp, MAIL_CLIENT_ERROR_CONNECTION_CLOSED);

    if (millis() - _lastReconnectMillis > _reconnectTimeout && !smtp->_tcpConnected)
    {
#if defined(ESP32)
      esp_wifi_connect();
#elif defined(ESP8266)
      WiFi.reconnect();
#endif
      _lastReconnectMillis = millis();
    }

    status = WiFi.status() == WL_CONNECTED || ethLinkUp(smtp->_sesson_cfg);
  }

  return status;
}

void ESP_Mail_Client::uploadReport(const char *filename, int progress)
{
  if (progress % ESP_MAIL_PROGRESS_REPORT_STEP == 0)
  {
    MBSTRING s;
    char *tmp = intStr(progress);
    appendP(s, esp_mail_str_160, true);
    s += filename;
    appendP(s, esp_mail_str_91, false);
    s += tmp;
    delP(&tmp);
    appendP(s, esp_mail_str_92, false);
    appendP(s, esp_mail_str_34, false);
    esp_mail_debug_line(s.c_str(), false);
    MBSTRING().swap(s);
  }
}

MBSTRING ESP_Mail_Client::getBoundary(size_t len)
{
  char *tmp = strP(boundary_table);
  char *buf = (char *)newP(len);
  if (len)
  {
    --len;
    buf[0] = tmp[0];
    buf[1] = tmp[1];
    for (size_t n = 2; n < len; n++)
    {
      int key = rand() % (int)(strlen(tmp) - 1);
      buf[n] = tmp[key];
    }
    buf[len] = '\0';
  }
  MBSTRING s = buf;
  delP(&buf);
  delP(&tmp);
  return s;
}

bool ESP_Mail_Client::sendBase64(SMTPSession *smtp, SMTP_Message *msg, const unsigned char *data, size_t len, bool flashMem, const char *filename, bool report)
{
  bool ret = false;
  const unsigned char *end, *in;

  end = data + len;
  in = data;

  size_t chunkSize = (BASE64_CHUNKED_LEN * UPLOAD_CHUNKS_NUM) + (2 * UPLOAD_CHUNKS_NUM);
  size_t byteAdded = 0;
  size_t byteSent = 0;

  int dByte = 0;
  unsigned char *buf = (unsigned char *)newP(chunkSize);
  memset(buf, 0, chunkSize);

  unsigned char *tmp = (unsigned char *)newP(3);
  int bcnt = 0;
  int pg = 0, _pg = 0;

  if (report)
    uploadReport(filename, bcnt);

  while (end - in >= 3)
  {

    memset(tmp, 0, 3);
    if (flashMem)
      memcpy_P(tmp, in, 3);
    else
      memcpy(tmp, in, 3);
    bcnt += 3;

    buf[byteAdded++] = b64_index_table[tmp[0] >> 2];
    buf[byteAdded++] = b64_index_table[((tmp[0] & 0x03) << 4) | (tmp[1] >> 4)];
    buf[byteAdded++] = b64_index_table[((tmp[1] & 0x0f) << 2) | (tmp[2] >> 6)];
    buf[byteAdded++] = b64_index_table[tmp[2] & 0x3f];
    dByte += 4;
    if (dByte == BASE64_CHUNKED_LEN)
    {
      if (byteAdded + 1 < chunkSize)
      {
        buf[byteAdded++] = 0x0d;
        buf[byteAdded++] = 0x0a;
      }
      dByte = 0;
    }
    if (byteAdded >= chunkSize - 4)
    {
      byteSent += byteAdded;

      if (!bdat(smtp, msg, byteAdded, false))
        goto ex;

      if (smtpSend(smtp, buf, byteAdded) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
        goto ex;
      memset(buf, 0, chunkSize);
      byteAdded = 0;
    }
    in += 3;

    if (report)
    {
      pg = (float)(100.0f * bcnt / len);
      if (pg != _pg)
        uploadReport(filename, pg);
      _pg = pg;
    }
  }

  if (byteAdded > 0)
  {
    if (!bdat(smtp, msg, byteAdded, false))
      goto ex;

    if (smtpSend(smtp, buf, byteAdded) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      goto ex;
  }

  if (end - in)
  {
    memset(buf, 0, chunkSize);
    byteAdded = 0;
    memset(tmp, 0, 3);
    if (flashMem)
    {
      if (end - in == 1)
        memcpy_P(tmp, in, 1);
      else
        memcpy_P(tmp, in, 2);
    }
    else
    {
      if (end - in == 1)
        memcpy(tmp, in, 1);
      else
        memcpy(tmp, in, 2);
    }

    buf[byteAdded++] = b64_index_table[tmp[0] >> 2];
    if (end - in == 1)
    {
      buf[byteAdded++] = b64_index_table[(tmp[0] & 0x03) << 4];
      buf[byteAdded++] = '=';
    }
    else
    {
      buf[byteAdded++] = b64_index_table[((tmp[0] & 0x03) << 4) | (tmp[1] >> 4)];
      buf[byteAdded++] = b64_index_table[(tmp[1] & 0x0f) << 2];
    }
    buf[byteAdded++] = '=';

    if (!bdat(smtp, msg, byteAdded, false))
      goto ex;

    if (smtpSend(smtp, buf, byteAdded) == ESP_MAIL_CLIENT_TRANSFER_DATA_FAILED)
      goto ex;
    memset(buf, 0, chunkSize);
  }

  if (report && _pg < 100)
    uploadReport(filename, 100);

  ret = true;
ex:
  if (report)
    esp_mail_debug("");
  delP(&tmp);
  delP(&buf);
  return ret;
}

SMTPSession::SMTPSession()
{
}

SMTPSession::~SMTPSession()
{
  closeSession();
#if defined(ESP32) || defined(ESP8266)
  _caCert.reset();
  _caCert = nullptr;
#endif
}

bool SMTPSession::connect(ESP_Mail_Session *config)
{
  if (_tcpConnected)
    MailClient.closeTCPSession(this);

  _sesson_cfg = config;
#if defined(ESP32) || defined(ESP8266)
  _caCert = nullptr;
  if (strlen(_sesson_cfg->certificate.cert_data) > 0)
    _caCert = std::shared_ptr<const char>(_sesson_cfg->certificate.cert_data);
#endif

  if (strlen(_sesson_cfg->certificate.cert_file) > 0)
  {
    if (_sesson_cfg->certificate.cert_file_storage_type == esp_mail_file_storage_type::esp_mail_file_storage_type_sd && !MailClient._sdOk)
      MailClient._sdOk = MailClient.sdTest();
    if (_sesson_cfg->certificate.cert_file_storage_type == esp_mail_file_storage_type::esp_mail_file_storage_type_flash && !MailClient._flashOk)
#if defined(ESP32)
      MailClient._flashOk = ESP_MAIL_FLASH_FS.begin(FORMAT_FLASH);
#elif defined(ESP8266)
      MailClient._flashOk = ESP_MAIL_FLASH_FS.begin();
#else
    {
    }
#endif
  }
  return MailClient.smtpAuth(this);
}

void SMTPSession::debug(int level)
{
  if (level > esp_mail_debug_level_0)
  {
    if (level > esp_mail_debug_level_3)
      level = esp_mail_debug_level_1;
    _debugLevel = level;
    _debug = true;
  }
  else
  {
    _debugLevel = esp_mail_debug_level_0;
    _debug = false;
  }
}

String SMTPSession::errorReason()
{
  MBSTRING ret;

  switch (_smtpStatus.statusCode)
  {
  case SMTP_STATUS_SERVER_CONNECT_FAILED:
    MailClient.appendP(ret, esp_mail_str_38, true);
    break;
  case SMTP_STATUS_SMTP_GREETING_GET_RESPONSE_FAILED:
    MailClient.appendP(ret, esp_mail_str_39, true);
    break;
  case SMTP_STATUS_SMTP_GREETING_SEND_ACK_FAILED:
    MailClient.appendP(ret, esp_mail_str_39, true);
    break;
  case SMTP_STATUS_AUTHEN_NOT_SUPPORT:
    MailClient.appendP(ret, esp_mail_str_42, true);
    break;
  case SMTP_STATUS_AUTHEN_FAILED:
    MailClient.appendP(ret, esp_mail_str_43, true);
    break;
  case SMTP_STATUS_USER_LOGIN_FAILED:
    MailClient.appendP(ret, esp_mail_str_43, true);
    break;
  case SMTP_STATUS_PASSWORD_LOGIN_FAILED:
    MailClient.appendP(ret, esp_mail_str_47, true);
    break;
  case SMTP_STATUS_SEND_HEADER_SENDER_FAILED:
    MailClient.appendP(ret, esp_mail_str_48, true);
    break;
  case SMTP_STATUS_SEND_HEADER_RECIPIENT_FAILED:
    MailClient.appendP(ret, esp_mail_str_222, true);
    break;
  case SMTP_STATUS_SEND_BODY_FAILED:
    MailClient.appendP(ret, esp_mail_str_49, true);
    break;
  case MAIL_CLIENT_ERROR_CONNECTION_CLOSED:
    MailClient.appendP(ret, esp_mail_str_221, true);
    break;
  case MAIL_CLIENT_ERROR_READ_TIMEOUT:
    MailClient.appendP(ret, esp_mail_str_258, true);
    break;
  case MAIL_CLIENT_ERROR_FILE_IO_ERROR:
    MailClient.appendP(ret, esp_mail_str_282, true);
    break;
  case SMTP_STATUS_SERVER_OAUTH2_LOGIN_DISABLED:
    MailClient.appendP(ret, esp_mail_str_293, true);
    break;
  case MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED:
    MailClient.appendP(ret, esp_mail_str_305, true);
    break;
  case MAIL_CLIENT_ERROR_SSL_TLS_STRUCTURE_SETUP:
    MailClient.appendP(ret, esp_mail_str_132, true);
    break;
  case SMTP_STATUS_NO_VALID_RECIPIENTS_EXISTED:
    MailClient.appendP(ret, esp_mail_str_206, true);
    break;
  case SMTP_STATUS_NO_VALID_SENDER_EXISTED:
    MailClient.appendP(ret, esp_mail_str_205, true);
    break;
  case MAIL_CLIENT_ERROR_OUT_OF_MEMORY:
    MailClient.appendP(ret, esp_mail_str_186, true);
    break;
  case SMTP_STATUS_NO_SUPPORTED_AUTH:
    MailClient.appendP(ret, esp_mail_str_42, true);
    break;

  default:
    break;
  }

  if (_smtpStatus.text.length() > 0 && ret.length() == 0)
  {
    MailClient.appendP(ret, esp_mail_str_312, true);
    char *code = MailClient.intStr(_smtpStatus.respCode);
    ret += code;
    MailClient.delP(&code);
    MailClient.appendP(ret, esp_mail_str_313, false);
    ret += _smtpStatus.text;
    return ret.c_str();
  }
  return ret.c_str();
}

bool SMTPSession::closeSession()
{
  if (!_tcpConnected)
    return false;

  if (_sendCallback)
  {
    _cbData._sentSuccess = _sentSuccessCount;
    _cbData._sentFailed = _sentFailedCount;
    MailClient.smtpCB(this, "");
    MailClient.smtpCBP(this, esp_mail_str_128);
  }

  if (_debug)
    MailClient.debugInfoP(esp_mail_str_245);

  bool ret = true;

/* Sign out */
#if defined(ESP32)
  /** 
   * The strange behavior in ESP8266 SSL client, BearSSLWiFiClientSecure
   * The client disposed without memory released after the server close 
   * the connection due to QUIT command, which caused the memory leaks.
  */
  MailClient.smtpSendP(this, esp_mail_str_7, true);
  _smtp_cmd = esp_mail_smtp_cmd_logout;
  ret = MailClient.handleSMTPResponse(this, esp_mail_smtp_status_code_221, SMTP_STATUS_SEND_BODY_FAILED);
#endif

  if (ret)
  {

    if (_sendCallback)
    {
      MailClient.smtpCB(this, "");
      MailClient.smtpCBP(this, esp_mail_str_129, false);
    }

    if (_debug)
      MailClient.debugInfoP(esp_mail_str_246);

    if (_sendCallback)
      MailClient.smtpCB(this, "", true);
  }

  return MailClient.handleSMTPError(this, 0, ret);
}

void SMTPSession::callback(smtpStatusCallback smtpCallback)
{
  _sendCallback = std::move(smtpCallback);
}

SMTP_Status::SMTP_Status()
{
}

SMTP_Status::~SMTP_Status()
{
  empty();
}

const char *SMTP_Status::info()
{
  return _info.c_str();
}

bool SMTP_Status::success()
{
  return _success;
}

size_t SMTP_Status::completedCount()
{
  return _sentSuccess;
}

size_t SMTP_Status::failedCount()
{
  return _sentFailed;
}

void SMTP_Status::empty()
{
  MBSTRING().swap(_info);
}

#endif

ESP_Mail_Client MailClient = ESP_Mail_Client();

#endif /* ESP_Mail_Client_CPP */