/* Dependencies:
   Radiohead: www.airspayce.com/mikem/arduino/RadioHead/
   Syslog: https://github.com/arcao/Syslog/
*/

extern "C" {
#include "user_interface.h"
}

#define SYSLOG syslog
#include "settings.h"

#include <ESP8266WiFi.h>
#include <stdio.h>
#include <SPI.h>
#include <RH_RF95.h>

#define RFM_CS D2
#define RFM_INT D1
#define RFM_FREQ 433.92

#define REPORT_INTERVAL 60000

// Singleton instance of the radio driver
RH_RF95 radio(RFM_CS, RFM_INT);

#include <Syslog.h> // Needs to be included to get access to LOG_INFO, etc
#ifdef SYSLOG
#include <WiFiUdp.h>
#define SYSLOG_SERVER "stinger.exsolvi.se"
#define SYSLOG_PORT 514
#define DEVICE_HOSTNAME "LoRaGW"

#define APP_NAME "FatLink"
WiFiUDP udpClient;
Syslog syslog(udpClient, SYSLOG_SERVER, SYSLOG_PORT, DEVICE_HOSTNAME, APP_NAME, LOG_KERN);
#else
#define SYSLOG none
class None {
  public:
    void log(...) {}
    void logf(...) {}
};
None none;
#endif

const char* host = "pubsub.pubnub.com";
const char* channel = "position_in";
RH_RF95::ModemConfigChoice modem_config = RH_RF95::Bw125Cr48Sf4096;

#define PAYLOADSIZE 128

void setup()
{
  Serial.begin(115200);
  Serial.println("in setup");

  Serial.print("Connecting to ");
  Serial.println(MY_ESP8266_SSID);

  WiFi.mode(WIFI_STA);
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP);

  WiFi.begin(MY_ESP8266_SSID, MY_ESP8266_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  SYSLOG.log(LOG_INFO, "FatLink GW starting up");
  while (!radio.init()) {
    SYSLOG.log(LOG_INFO, "LoRa radio init failed");
    delay(5000);
  }
  SYSLOG.log(LOG_INFO, "LoRa radio init OK!");

  if (!radio.setFrequency(RFM_FREQ)) {
    SYSLOG.log(LOG_INFO, "setFrequency failed");
    delay(5000);
  }

  radio.setModemConfig(modem_config);
  radio.setTxPower(23);
  SYSLOG.log(LOG_INFO, "Setup finished");
}

unsigned long lastSend = 0;

void loop()
{
  check_incoming();
  if (millis() > lastSend  + REPORT_INTERVAL) {
    SYSLOG.log(LOG_INFO, "MARK");
    send_beacon();
    lastSend = millis();
  }
  delay(100); // Give the ESP chance to enter low power mode
}

void send_beacon() {
#ifndef SEND_BEACON
  return;
#endif
  char data[] = "Beacon";
  unsigned long start_send = millis();
  radio.send((uint8_t *)data, strlen(data));
  SYSLOG.log(LOG_INFO, data);
  radio.waitPacketSent();
  SYSLOG.logf(LOG_INFO, "Packet send took %u ms", millis() - start_send);
}

void set_led(bool state) {
  //TODO: Set global variable, trigger websocket, switch physical led
}

void check_incoming() {
  if (radio.waitAvailableTimeout(10))
  {
    // Should be a message for us now
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);

    if (radio.recv(buf, &len))
    {
      set_led(HIGH);
      RH_RF95::printBuffer("Received: ", buf, len);
      buf[len] = '\0';
      SYSLOG.log(LOG_INFO, "Got: ");
      SYSLOG.log(LOG_INFO, (char*)buf);
      SYSLOG.logf(LOG_INFO, "RSSI: %i", radio.lastRssi());
      SYSLOG.logf(LOG_INFO, "SNR: %i", radio.lastSNR());
      // TODO: Parse message (gps coords, etc)

#ifdef SEND_REPLY
      // Send a reply
      char data[PAYLOADSIZE];
      snprintf(data, PAYLOADSIZE, "RSSI: %i SNR: %i, freqErr: %i", radio.lastRssi(), radio.lastSNR(), radio.frequencyError());

      unsigned long start_send = millis();
      radio.send((uint8_t *)data, strlen(data));
      radio.waitPacketSent();
      SYSLOG.logf(LOG_INFO, "Packet send took %u ms", millis() - start_send);
#endif
      set_led(LOW);
      //TODO: log to pubnub
    }
    else
    {
      SYSLOG.log(LOG_INFO, "Receive failed");
    }
  }
}

/*
  void receive(const MyMessage &message)
  {
  int16_t rssi = _radio.RSSI;

  if (message.type == V_POSITION) {
    char payload[PAYLOADSIZE];

    const char* msgconst = message.getString();
    char* msg = strdup(msgconst);

    char* latBuf;
    char* lngBuf;
    char* altBuf;
    const char s[2] = ";";

    //sscanf(payload, "%s;%s;%s", &latBuf, &lngBuf, &altBuf);
    latBuf = strtok(msg, s);
    lngBuf = strtok(NULL, s);
    altBuf = strtok(NULL, s);

    formatData(payload, PAYLOADSIZE, latBuf, lngBuf, altBuf, rssi);
    Serial.print(" Publishing: ");
    Serial.println(payload);

    publishPositionToPubNub(payload);
  } else if (message.type == V_TEXT) {
    SYSLOG.log(LOG_INFO, message.getString());
  } else if (message.type == V_VOLTAGE) {
    SYSLOG.logf(LOG_INFO, "Voltage: %f", message.getFloat());
  }
  }
*/

void publishPositionToPubNub(String payload) {
  WiFiClient client;
  const int l_httpPort = 80;
  if (!client.connect(host, l_httpPort)) {
    Serial.println("Pubnub Connection Failed");
    return;
  }
  delay(1);

  //DATA FORMAT : http://pubsub.pubnub.com /publish/pub-key/sub-key/signature/channel/callback/message

  String url = "/publish/";
  url += pubkey;
  url += "/";
  url += subkey;
  url += "/0/";
  url += channel;
  url += "/0/";
  url += payload;
  SYSLOG.log(LOG_INFO, url.c_str());
  url += "?auth=";
  url += authkey;
  Serial.println(url);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  delay(10);

  while (client.available()) {
    String line = client.readStringUntil('\r');
    delay(1);
    //Serial.print(line);
  }

  //Serial.println();
  //Serial.println("Pubnub Connection Closed");
}

void formatData(char* message, byte len, char* latStr, char* longStr, char* alt, int16_t rssi) {
  snprintf(message, len, "{\"%s;%s\":{\"latlng\":[%s,%s],\"data\":{\"alt\":%s,\"rssi\":%d}}}", latStr, longStr, latStr, longStr, alt, rssi);
}

void demoData() {
  char payload[PAYLOADSIZE];
  int16_t rssi = random(-120, -20);
  char latStr[15], longStr[15];
  String(random(57600, 57700) / 1000.0).toCharArray(latStr, sizeof(latStr));
  String(random(11850, 12000) / 1000.0).toCharArray(longStr, sizeof(longStr));;
  Serial.print("RSSI: ");
  Serial.println(rssi);
  formatData(payload, PAYLOADSIZE, latStr, longStr, (char*)"3.141592", rssi);
  Serial.print("Testing: ");
  Serial.println(payload);
  publishPositionToPubNub(payload);
}

