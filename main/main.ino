#include <BLEDevice.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1 // no reset pin
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define PIN_A 16
#define PIN_B 17
volatile uint32_t lastIsrTime = 0;
int upOrDown = 0;

const unsigned char PROGMEM play_icon[] = {
  0b00011000,
  0b00011100,
  0b00011110,
  0b00011111,
  0b00011111,
  0b00011110,
  0b00011100,
  0b00011000
};

// 8x8 Pause icon (two vertical bars)
const unsigned char PROGMEM pause_icon[] = {
  0b01100110,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01100110,
  0b01100110
};

static BLEAdvertisedDevice* phoneDevice = nullptr;
static bool doConnect = false;
BLEClient* client;

static BLEUUID serviceUUID("0000feed-0000-1000-8000-00805f9b34fb");
static BLEUUID metaCharUUID("0000beef-0000-1000-8000-00805f9b34fb");
static BLEUUID cmdCharUUID("0000c0de-0000-1000-8000-00805f9b34fb");

void IRAM_ATTR handleA() {
  uint32_t now = micros();
  if (now - lastIsrTime < 2000) return;
  lastIsrTime = now;

  bool a = digitalRead(PIN_A);
  bool b = digitalRead(PIN_B);

  if (a == b) {
    upOrDown = 1;   
  } else {
    upOrDown = -1;   
  }
}

void showMessage(const String& line1, const String& line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.setCursor(0, 16);
  display.println(line2);
  display.display();
}

void showMetadata(const String& title, const String& artist, const bool isPlaying) {
  display.clearDisplay();
  if(isPlaying) display.drawBitmap(SCREEN_WIDTH - 8, 0, play_icon, 8, 8, SSD1306_WHITE);
  else display.drawBitmap(SCREEN_WIDTH - 8, 0, pause_icon, 8, 8, SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title.substring(0, 21));  // truncate to fit width
  display.setCursor(0, 16);
  display.setTextSize(1);
  display.println(artist.substring(0, 21));
  display.display();
}

void loadingAnimation() {
  int totalWidth = SCREEN_WIDTH;
  int totalTime = 3000;
  int stepDelay = 10;  
  int steps = totalWidth;

  display.clearDisplay();
  
  for(int i = 0; i <= steps; i++) {
    display.fillRect(0, 0, i, SCREEN_HEIGHT, SSD1306_WHITE);
    display.display();
    delay(totalTime / steps); 
  }

  display.clearDisplay();
  display.setTextSize(3);
  display.setCursor(10, 10);
  display.println("TOYOTA");
  display.display();
  delay(2000);
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        Serial.printf("[BLE] Found: %s\n", advertisedDevice.toString().c_str());
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
            Serial.println("[BLE] Found phone advertising FEED service!");
            BLEDevice::getScan()->stop();
            phoneDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
        }
    }
};

static void notifyCallback(BLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
  String s = "";
  for (size_t i = 0; i < len; i++) s += (char)data[i];
  Serial.printf("[BLE] Notification: %s\n", s.c_str());
  int sep = s.indexOf('.');
  bool isPlaying = false;
  String condition = "1";
  if (sep > 0) {
    if(s.substring(s.length()-2, s.length()-1) == "1") isPlaying = true;
    showMetadata(s.substring(0, sep), s.substring(sep + 1, s.length()-3), isPlaying);
  } else {
    //showMetadata(s, "No music.", false);
  }
}

bool connectToPhone() {
    Serial.println("[BLE] Connecting...");
    client = BLEDevice::createClient();
    if (!client->connect(phoneDevice)) {
        Serial.println("[BLE] Connection failed.");
        delete phoneDevice;
        phoneDevice = nullptr;
        return false;
    }

    Serial.println("[BLE] Connected, discovering service...");
    if (client->getMTU() < 517) {
        uint16_t mtu = client->setMTU(512); // choose a reasonable value
        Serial.printf("[BLE] MTU requested: %d\n", mtu);
    }
    BLERemoteService* pSvc = client->getService(serviceUUID);
    if (pSvc == nullptr) {
        Serial.println("[BLE] Service not found!");
        client->disconnect();
        return false;
    }

    Serial.println("[BLE] Service found.");
    BLERemoteCharacteristic* metaChar = pSvc->getCharacteristic(metaCharUUID);
    if (metaChar && metaChar->canRead()) {
        auto rawVal = metaChar->readValue();
        String val = String(rawVal.c_str()); 
        Serial.printf("Initial metadata: %s\n", val.c_str());
        int sep = val.indexOf('.');
        if (sep != -1)
            showMetadata(val.substring(0, sep), val.substring(sep + 1), true);
        else
            showMetadata(val, "", true);
    }
    if (metaChar && metaChar->canNotify()) {
        metaChar->registerForNotify(notifyCallback);
    }
    return true;
}

void sendCommand(String cmd) {
    if (client && client->isConnected()) {
        BLERemoteService* pSvc = client->getService(serviceUUID);
        if (pSvc) {
            BLERemoteCharacteristic* cmdChar = pSvc->getCharacteristic(cmdCharUUID);
            if (cmdChar && cmdChar->canWrite()) {
                bool success = cmdChar->writeValue(cmd);
                if (success) {
                    Serial.println("[BLE] Sent 'volup' command!");
                } else {
                    Serial.println("[BLE] Failed to send command.");
                }
            } else {
                Serial.println("[BLE] Command characteristic not writable or not found.");
            }
        } else {
            Serial.println("[BLE] Service not found!");
        }
    } else {
        Serial.println("[BLE] Not connected to phone.");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[BLE] Starting classic ESP32 BLE client...");

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    showMessage("Booting...", "");
    delay(5000);

    pinMode(PIN_A, INPUT_PULLUP);
    pinMode(PIN_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_A), handleA, CHANGE);

    loadingAnimation();
    BLEDevice::init("ESP32-MediaClient");
    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    scan->setActiveScan(true);
    scan->start(0, false);

    showMessage("Scanning...", "");
}

void loop() {
    if (doConnect) {
        doConnect = false;
        connectToPhone();
    }
    upOrDown = 0;
    noInterrupts();
    interrupts();
    if(upOrDown == 1)
    {
      Serial.println("Sending Volume Up Command...");
      sendCommand("volup"); // Volume Up
      upOrDown = 0;
    }

    if(upOrDown == -1)
    {
      Serial.println("Sending Volume Down Command...");
      sendCommand("voldown"); // Volume Down
      upOrDown = 0;
    }

    delay(100);
}
