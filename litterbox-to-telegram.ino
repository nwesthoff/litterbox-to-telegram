#include "conf.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <LittleFS.h>

const unsigned long BOT_MTBS = 2000;  // mean time between scan messages
const unsigned long LIGHT_MTBR = 50;
const int MAX_DOOR_OPENS = 8;
const int threshold = 25;

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

const int TIMEOUT = 1000 * 60 * 1;  // 1 minute

unsigned long bot_lasttime;                   // last time messages' scan has been done
unsigned long movement_tripped_lasttime = -TIMEOUT;  // last time movement sensor has been tripped
bool movement_tripped = false;
bool notified = false;

uint8_t MOVEMENT_SENSOR_PIN = 14;  // D5
const int lightBufferSize = 25;    // Number of readings to store (adjust as needed)
int lightBuffer[lightBufferSize];  // Buffer to store light sensor values
int lightBufferIndex = 0;


int door_opened_times = 0;

// Keyboard options
String keyboardJson = "[[\"/cleaned\", \"/noclean\"]]";
String botCommandsJson = "[{\"command\":\"start\",\"description\":\"Start\"},{\"command\":\"cleaned\", \"description\":\"Als je de kattenbak hebt schoongemaakt\"},{\"command\":\"status\",\"description\":\"Hoe vaak is de kattenbak gebruikt sinds schoonmaken?\"},{\"command\":\"stop\",\"description\":\"Geen updates meer ontvangen.\"}]";

DynamicJsonDocument usersDoc(1500);
const char *SUBSCRIBED_USERS_FILENAME = "/subscribed_users.json";  // Filename for local storage

JsonObject getSubscribedUsers() {
  File subscribedUsersFile = LittleFS.open(SUBSCRIBED_USERS_FILENAME, "r");
  JsonObject users;

  // no file
  if (!subscribedUsersFile) {
    Serial.println("Failed to open subscribed users file");
    // Create empty file (w+ not working as expect)
    File f = LittleFS.open(SUBSCRIBED_USERS_FILENAME, "w");
    users = usersDoc.to<JsonObject>();
    serializeJson(users, f);
    f.close();
    return users;
  }

  // too large file
  size_t size = subscribedUsersFile.size();
  if (size > 1500) {
    subscribedUsersFile.close();
    Serial.println("Subscribed users file is too large");
    return users;
  }

  String file_content = subscribedUsersFile.readString();
  subscribedUsersFile.close();

  DeserializationError error = deserializeJson(usersDoc, file_content);
  if (error) {
    Serial.println("Failed to parse subscribed users file");
    return users;
  }

  users = usersDoc.as<JsonObject>();

  return users;
}

bool addSubscribedUser(String chat_id, String from_name) {
  JsonObject users = getSubscribedUsers();
  users[chat_id] = from_name;

  Serial.print("Subscribed: ");
  serializeJson(users, Serial);
  Serial.println("");

  File subscribedUsersFile = LittleFS.open(SUBSCRIBED_USERS_FILENAME, "w+");
  // file not available
  if (!subscribedUsersFile) {
    subscribedUsersFile.close();
    Serial.println("Failed to open subscribed users file for writing");
    return false;
  }

  serializeJson(users, subscribedUsersFile);
  subscribedUsersFile.close();
  return true;
}

bool removeSubscribedUser(String chat_id) {
  JsonObject users = getSubscribedUsers();
  users.remove(chat_id);

  File subscribedUsersFile = LittleFS.open(SUBSCRIBED_USERS_FILENAME, "w");
  // file not available
  if (!subscribedUsersFile) {
    subscribedUsersFile.close();
    Serial.println("Failed to open subscribed users file for writing");
    return false;
  }

  serializeJson(users, subscribedUsersFile);
  subscribedUsersFile.close();
  return true;
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;

    if (text == "/start") {
      if (addSubscribedUser(chat_id, from_name)) {
        String welcome = "Hoi, " + from_name + ", welkom bij De Kattenbak.\n";
        welcome += "Je krijgt vanaf nu updates.\n";
        welcome += "/status : als je wil weten hoe vaak de kattenbak is gebruikt\n";
        welcome += "/cleaned : als je de kattenbak hebt schoongemaakt\n";
        welcome += "/stop : als je geen berichten meer wilt ontvangen\n";
        bot.sendMessage(chat_id, welcome, "Markdown");
      } else {
        bot.sendMessage(chat_id, "Er is iets fout gegaan, probeer het later opnieuw?", "");
      }
    }

    if (text == "/stop") {
      if (removeSubscribedUser(chat_id)) {
        bot.sendMessage(chat_id, "Thank you, " + from_name + ", we always waiting you back", "");
      } else {
        bot.sendMessage(chat_id, "Something wrong, please try again (later?)", "");
      }
    }

    if (text == "/cleaned") {
      door_opened_times = 0;
      notified = false;
      notifyClean(chat_id);
    }

    if (text == "/status") {
      String statusMsg = "De kattenbak is " + String(door_opened_times) + " keer gebruikt.";
      bot.sendMessage(chat_id, statusMsg, "");
    }

    if (text == "/noclean") {
      // do nothing
    }
  }
}

void handleNotification() {
  JsonObject users = getSubscribedUsers();
  unsigned int users_processed = 0;

  Serial.println("Sending notification!");

  for (JsonObject::iterator it = users.begin(); it != users.end(); ++it) {
    users_processed++;
    const char *chat_id = it->key().c_str();
    bot.sendMessageWithReplyKeyboard(chat_id, "De kattenbak is vies!", "", keyboardJson, true);
  }
}

void notifyClean(String cleaned_by_chat_id) {
  Serial.println("Kattenbak is schoongemaakt");

  JsonObject users = getSubscribedUsers();
  unsigned int users_processed = 0;

  for (JsonObject::iterator it = users.begin(); it != users.end(); ++it) {
    users_processed++;
    const char *chat_id = it->key().c_str();
    String cleaned_by_name = "Guest";
    if (users[cleaned_by_chat_id]) {
      cleaned_by_name = String(users[cleaned_by_chat_id]);
    }
    Serial.print("Cleaned by: ");
    Serial.println(cleaned_by_name);

    if (cleaned_by_chat_id != chat_id) {
      bot.sendMessageWithReplyKeyboard(chat_id, cleaned_by_name + " heeft de kattenbak schoongemaakt 🧹", "", "");
    } else {
      bot.sendMessageWithReplyKeyboard(chat_id, "Arlo & Remy danken u 🐈🐈‍⬛", "", "");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (!LittleFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  // RESET STORAGE
  // LittleFS.remove(SUBSCRIBED_USERS_FILENAME);

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

  attachInterrupt(MOVEMENT_SENSOR_PIN, IntCallback, RISING);

  bot.setMyCommands(botCommandsJson);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

ICACHE_RAM_ATTR void IntCallback() {
  const unsigned long time = millis();
  Serial.print("Stamp(ms): ");
  Serial.println(time);
  movement_tripped = true;
}

void loop() {
  unsigned long time = millis();

  if (movement_tripped == true) {
    Serial.println(time - movement_tripped_lasttime);
  }

  if (movement_tripped == true && (time - movement_tripped_lasttime > TIMEOUT)) {
    movement_tripped_lasttime = time;
    door_opened_times++;
    Serial.println("Open door detected!!");

    if (door_opened_times >= MAX_DOOR_OPENS && notified == false) {
      handleNotification();
      notified = true;
    }
  }
  movement_tripped = false;


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