#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define PWM_RANGE 1024
#define PWM_FREQUENCY 40000

#define __RANGE_COEF__ (__RANGE_HIGH__ - __RANGE_LOW__) / 100

// ********************************************************************************
// ********************************************************************************
// Constants
const int ESP_LED = 2;    // ESP's LED
const int OUT_RED = 15;   // Onboard LED Red
const int OUT_GREEN = 12; // Onboard LED Green
const int OUT_BLUE = 13;  // Onboard LED Blue

// ********************************************************************************
// ********************************************************************************
// Debugging

#ifdef __DEBUG__
#define LOG(x)                    \
  Serial.print("[");              \
  Serial.print(String(millis())); \
  Serial.print("] ");             \
  Serial.print(x);                \
  Serial.print("\n");
#else
#define LOG(x)
#endif

// ********************************************************************************
// ********************************************************************************
// region Global vars & structs
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

uint16 convertBrightnessToPwm(uint16 v)
{
  if (v <= 1)
  {
    return __RANGE_LOW__;
  }
  if (v >= 100)
  {
    return __RANGE_HIGH__;
  }

  return ((double)v) * __RANGE_COEF__ + __RANGE_LOW__;
}

uint16 convertPwmToBrightness(uint16 v)
{
  if (v <= __RANGE_LOW__)
  {
    return 1;
  }
  if (v >= __RANGE_HIGH__)
  {
    return 100;
  }

  return (((double)v) - __RANGE_LOW__) / __RANGE_COEF__;
}

struct State
{

  uint16 targetBrightness = 100;
  uint16 targetPwmValue = __RANGE_HIGH__;
  uint16 currentPwmValue = __RANGE_HIGH__;

  void setTargetBrightness(uint16 v)
  {
    if (v > 100)
    {
      this->targetBrightness = 100;
    }
    else if (v < 1)
    {
      this->targetBrightness = 1;
    }
    else
    {
      this->targetBrightness = v;
    }
    this->targetPwmValue = convertBrightnessToPwm(this->targetBrightness);
#ifdef __DEBUG__
    LOG("State set to: targetBrightness=" + String(this->targetBrightness) + ", targetPwmValue=" + String(this->targetPwmValue));
#endif
  }

  void setTargetPwm(uint16 v)
  {
    this->targetPwmValue = v;
    this->targetBrightness = convertPwmToBrightness(this->targetPwmValue);

#ifdef __DEBUG__
    LOG("State set (raw) to: targetBrightness=" + String(this->targetBrightness) + ", targetPwmValue=" + String(this->targetPwmValue));
#endif
  }

  int16_t getNewPwmValue()
  {
    if (this->currentPwmValue < this->targetPwmValue)
    {
      return this->currentPwmValue + 1;
    }

    if (this->currentPwmValue > this->targetPwmValue)
    {
      return this->currentPwmValue - 1;
    }

    return -1;
  }
} state;

// ********************************************************************************
// ********************************************************************************
// WiFi
bool setupWiFi()
{
  // WiFi.mode(WIFI_STA);

  IPAddress networkIp;
  IPAddress networkGateway;
  IPAddress networkSubnet;
  IPAddress networkDns1;
  IPAddress networkDns2;
  if (!networkIp.fromString(__NETWORK_IP__))
  {
#ifdef __DEBUG__
    LOG("ERROR: Invalid IP string (address): ");
    delay(5);
    ESP.reset();
#endif
  }
  if (!networkGateway.fromString(__NETWORK_GATEWAY__))
  {
#ifdef __DEBUG__
    LOG("ERROR: Invalid IP string (gateway): ");
    delay(5);
    ESP.reset();
#endif
  }
  if (!networkSubnet.fromString(__NETWORK_SUBNET__))
  {
#ifdef __DEBUG__
    LOG("ERROR: Invalid IP string (subnet): ");
    delay(5);
    ESP.reset();
#endif
  }
  if (!networkDns1.fromString(__NETWORK_DNS1__))
  {
#ifdef __DEBUG__
    LOG("ERROR: Invalid IP string (dns1): ");
    delay(5);
    ESP.reset();
#endif
  }
  if (!networkDns2.fromString(__NETWORK_DNS2__))
  {
#ifdef __DEBUG__
    LOG("ERROR: Invalid IP string (dns2): ");
    delay(5);
    ESP.reset();
#endif
  }
  if (!WiFi.config(networkIp, networkGateway, networkSubnet, networkDns1, networkDns2))
  {
#ifdef __DEBUG__
    LOG("ERROR: STA Failed to configure");
    delay(5);
    ESP.reset();
#endif
  }

  WiFi.begin(__WIFI_SSID__, __WIFI_PASSPHRASE__);
  uint32_t timeout = millis() + __WIFI_CONNECT_TIMEOUT_MS__; // await connection for X then reboot
  while ((WiFi.status() != WL_CONNECTED) && (millis() < timeout))
  {
    delay(3);
  }
  return (WiFi.status() == WL_CONNECTED);
}

// ********************************************************************************
// ********************************************************************************
// Messaging

void setupLight()
{
  pinMode(ESP_LED, OUTPUT);
  pinMode(OUT_RED, OUTPUT);
  pinMode(OUT_GREEN, OUTPUT);
  pinMode(OUT_BLUE, OUTPUT);
  analogWriteRange(PWM_RANGE);
  analogWriteFreq(PWM_FREQUENCY);
}

void initLight()
{
  state.currentPwmValue = state.targetPwmValue;
  analogWrite(OUT_BLUE, state.currentPwmValue);
}

void loopSyncLightSmooth()
{
  int16_t newPwmValue = state.getNewPwmValue();
  if (newPwmValue >= 0)
  {
    state.currentPwmValue = newPwmValue;
    analogWrite(OUT_BLUE, newPwmValue);
  }
}

void loopSyncLightQuick()
{
  if (state.currentPwmValue != state.targetPwmValue)
  {
    state.currentPwmValue = state.targetPwmValue;
    analogWrite(OUT_BLUE, state.currentPwmValue);
  }
}

void reportState()
{
  String msg = String();
  msg.concat("{");
  msg.concat("\"targetBrightness\":");
  msg.concat(state.targetBrightness);
  msg.concat(",\"targetPwmValue\":");
  msg.concat(state.targetPwmValue);
  msg.concat("}");
  mqttClient.publish(__MQTT_TOPIC_REPORT__, msg.c_str());
}

void enableAlert()
{
  analogWrite(OUT_RED, 255);
}

void disableAlert()
{
  analogWrite(OUT_RED, 0);
}

void onMessage(char *topic, byte *payload, unsigned int length)
{
  char charPayload[length + 1];
  memcpy(charPayload, payload, length);
  charPayload[length] = 0; // Null termination.
  String stringPayload = charPayload;
#ifdef __DEBUG__
  LOG("Message arrived: [" + stringPayload + "] from [" + String(topic) + "]");
#endif

  if (strcmp(topic, __MQTT_TOPIC_SET__) == 0)
  {
    state.setTargetBrightness(stringPayload.toInt());
  }
  else
  {
    state.setTargetPwm(stringPayload.toInt());
  }
  reportState();
}

void setupMqtt()
{
  mqttClient.setServer(__MQTT_HOST__, __MQTT_PORT__);
  mqttClient.setCallback(onMessage);
}

void reconnectMqtt()
{
  while (!mqttClient.connected())
  {
#ifdef __DEBUG__
    LOG("Connecting to MQTT...");
#endif
    // Create a random client ID
    String clientId = __MQTT_CLIENT_ID__;
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str(), __MQTT_USERNAME__, __MQTT_PASSWORD__))
    {
#ifdef __DEBUG__
      LOG("Connecting to MQTT...OK (client_id=" + String(clientId) + ")");
#endif
      // Once connected, publish an announcement...
      mqttClient.publish(__MQTT_TOPIC_HELLO__, clientId.c_str());
      // subscribe
      mqttClient.subscribe(__MQTT_TOPIC_SET__);
      mqttClient.subscribe(__MQTT_TOPIC_SET_RAW__);
    }
    else
    {
#ifdef __DEBUG__
      LOG("Connecting to MQTT...FAILED (rc=" + String(mqttClient.state()) + "). Will retry in 5s...");
#endif
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup()
{

#ifdef __DEBUG__
  Serial.begin(115200);
  Serial.write("Setting up...");
#endif

  setupLight();
  initLight();
  enableAlert();

#ifdef __DEBUG__
  LOG("Connecting to WiFi...");
#endif
  bool wifiConnected = setupWiFi();

  if (wifiConnected)
  {
#ifdef __DEBUG__
    LOG("Connecting to WiFi...OK");
#endif
  }
  else
  {
#ifdef __DEBUG__
    LOG("Connecting to WiFi...FAILED");
    LOG("Restarting in 5 seconds...");
#endif
    delay(5000);
    ESP.restart();
  }

#ifdef __DEBUG__
  LOG("Preparing MQTT...");
#endif

  setupMqtt();

#ifdef __DEBUG__
  LOG("Preparing MQTT...OK");
#endif
  disableAlert();
}

// ********************************************************************************
// ********************************************************************************
// Run loop
#ifdef __SMOOTH_INTERVAL_MS__
static unsigned long lastSmoothUpdate = 0;
#endif

void loop()
{
  if (!mqttClient.connected())
  {
    enableAlert();
    reconnectMqtt();
    disableAlert();
    reportState();
  }

  mqttClient.loop();

#ifdef __SMOOTH_INTERVAL_MS__
  unsigned long currentMillis = millis();
  if (millis() - lastSmoothUpdate >= __SMOOTH_INTERVAL_MS__)
  {
    lastSmoothUpdate = currentMillis;
    loopSyncLightSmooth();
  }
#else
  loopSyncLightQuick();
#endif
}
