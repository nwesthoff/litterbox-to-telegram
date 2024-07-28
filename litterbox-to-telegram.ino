#include "conf.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

const unsigned long BOT_MTBS = 5000;  // mean time between scan messages
const unsigned long LIGHT_MTBR = 50;
const int MAX_DOOR_OPENS = 5;
const int MINUTES_NOTIFICATION_TIMEOUT = 5;
const int threshold = 50;

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
unsigned long bot_lasttime;    // last time messages' scan has been done
unsigned long light_lasttime;  // last time light sensor has been read
unsigned long light_lasttimeouttime;
unsigned long notification_lasttime;  // last time users have been notified

int light_sensor = A0;
const int lightBufferSize = 25;    // Number of readings to store (adjust as needed)
int lightBuffer[lightBufferSize];  // Buffer to store light sensor values
int lightBufferIndex = 0;

const int TIMEOUT = LIGHT_MTBR * (lightBufferSize + 1);

int door_opened_times = 0;

// Keyboard options
String keyboardJson = "[[\"/cleaned\", \"/noclean\"]]";
String botCommandsJson = "[{\"command\":\"reset\", \"description\":\"Zet teller op 0\"},{\"command\":\"start\",\"description\":\"Start\"},{\"command\":\"status\",\"description\":\"Hoe vaak is de kattenbak gebruikt sinds schoonmaken?\"}]";

const int max_registered_chats = 64;
String registered_chat_ids[max_registered_chats];
int registered_chat_ids_length = 0;

void registerChat(String chat_id) {
  if (registered_chat_ids_length < max_registered_chats) {
    Serial.print("Registered user: ");
    Serial.println(chat_id);

    registered_chat_ids[registered_chat_ids_length] = chat_id;  // Add the item to the array
    registered_chat_ids_length++;                               // Increment the current size
  } else {
    Serial.println("Array is full, cannot add more items.");
  }
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String text = bot.messages[i].text;

    if (text == "/start") {
      registerChat(bot.messages[i].chat_id);
    }

    if (text == "/cleaned" || text == "/reset") {
      door_opened_times = 0;
      notifyClean(bot.messages[i].chat_id);
    }

    if (text == "/status") {
      String statusMsg = "Het luik van de kattenbak is " + String(door_opened_times) + " keer geopend.";
      bot.sendMessage(bot.messages[i].chat_id, statusMsg, "");
    }

    if (text == "/noclean") {
      // do nothing
    }
  }
}

void handleNotification() {
  for (int i = 0; i < registered_chat_ids_length; i++) {
    Serial.print("Sending update to: ");
    Serial.println(registered_chat_ids[i]);
    bot.sendMessageWithReplyKeyboard(registered_chat_ids[i], "De kattenbak is vies!", "", keyboardJson, true);
    notification_lasttime = millis();
  }
}

void notifyClean(String chat_id) {
  Serial.println("Kattenbak is schoongemaakt");

  for (int i = 0; i < registered_chat_ids_length; i++) {
    if (chat_id != registered_chat_ids[i]) {
      Serial.print("Sending update to: ");
      Serial.println(registered_chat_ids[i]);
      bot.sendMessage(registered_chat_ids[i], "De kattenbak is schoongemaakt :)", "");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  // attempt to connect to Wifi network:
  Serial.print("Connecting to Wifi SSID ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  secured_client.setTrustAnchors(&cert);  // Add root certificate for api.telegram.org

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nWiFi connected. IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Retrieving time: ");
  configTime(0, 0, "pool.ntp.org");  // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(now);

  // Initialize the light buffer
  for (int i = 0; i < lightBufferSize; i++) {
    lightBuffer[i] = analogRead(light_sensor);
  }

  bot.setMyCommands(botCommandsJson);
}

void loop() {
  unsigned long time = millis();

  if (time - light_lasttime > LIGHT_MTBR) {
    // read light sensor
    int light = analogRead(light_sensor);  // read the raw value from light_sensor pin (A0)

    lightBuffer[lightBufferIndex] = light;                        // Store the light sensor value in the buffer
    lightBufferIndex = (lightBufferIndex + 1) % lightBufferSize;  // Move to the next buffer index

    if (time - light_lasttimeouttime > TIMEOUT) {
      // Find the lowest value in the buffer
      int maxValue = lightBuffer[0];
      for (int i = 1; i < lightBufferSize; i++) {
        if (lightBuffer[i] != 0 && lightBuffer[i] > maxValue) {
          maxValue = lightBuffer[i];
        }
      }

      if (maxValue - threshold > light) {
        Serial.println("Open door detected!! Values: ");
        Serial.print("val: ");
        Serial.println(light);
        Serial.print("max val: ");
        Serial.println(maxValue);

        door_opened_times++;

        if (door_opened_times >= MAX_DOOR_OPENS) {
          handleNotification();
        }
      }

      light_lasttimeouttime = time;
    }

    light_lasttime = time;
  }


  // repeat messages
  if (time - bot_lasttime > BOT_MTBS) {
    Serial.println("Getting message updates");
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    bot_lasttime = time;
    Serial.println("Finished getting message updates");
  }
}