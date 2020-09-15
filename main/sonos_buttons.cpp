/******************************************************************************
 * Sonos button control.
 * ESP32 project to control a sonos player with some buttons
 ******************************************************************************/    
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <AsyncUDP.h>
#include <string.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <driver/touch_pad.h>
#include <driver/rtc_io.h>
#include "ulp_main.h"
#include "sonos.h"
#include <esp32/ulp.h>
#include "config.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

//config variables
#define NUM_BTN_COLUMNS (4)
#define NUM_BTN_ROWS (1)
#define NUM_LED_COLUMNS (4)
#define NUM_LED_ROWS (1)

#define MAX_DEBOUNCE (3)

// This is roughly 30 seconds with the various delays + scanning time
#define IDLE_LOOPS_SLEEPY 4500

static const gpio_num_t btncolumnpins[NUM_BTN_COLUMNS] = {GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26};
static const gpio_num_t btnrowpins[NUM_BTN_ROWS]       = {GPIO_NUM_33};

static const gpio_num_t ledcolumnpins[NUM_LED_COLUMNS] = {GPIO_NUM_25, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_18};
static const gpio_num_t colorpins[NUM_LED_ROWS]        = {GPIO_NUM_32};

extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");

static int8_t debounce_count[NUM_BTN_COLUMNS][NUM_BTN_ROWS];
static const char* TAG = "SonosButtons";

// bit field representing the buttons activated on the last scan loop
static uint8_t buttons_released = 0;
// bit field representing the buttons activated that caused us to wakeup
uint8_t sleep_buttons = 0;

static uint8_t LEDS_lit = 0;
static IPAddress targetSonos;

// Store the base station mac address and channel in RTC memory so we can re-connect more quickly
static RTC_DATA_ATTR struct {
    uint8_t bssid [6];
    uint16_t channel; // Make this 16 bites to align on the 32 bit boundary
} wifi_cache;

static RTC_DATA_ATTR uint32_t wifi_cache_checksum;

void clearWifiCache() {
    wifi_cache_checksum = 1;
    for (uint32_t i = 0; i < sizeof(wifi_cache.bssid); i++) {
        wifi_cache.bssid[i] = 0xFF;
    }
    wifi_cache.channel = 0;
}

bool checkWifiCache() {
    uint32_t readSum = 0;
    uint32_t *p = (uint32_t *) wifi_cache.bssid;
    for (uint32_t i = 0; i < sizeof(wifi_cache) / 4; i++) {
        readSum += p[i];
    }

    bool hasRealData = false;
    for (uint32_t i = 0; i < sizeof(wifi_cache.bssid); i++) {
        hasRealData |= (wifi_cache.bssid[i] != 0 && wifi_cache.bssid[i] != 0xFF);
    }
    if (readSum == wifi_cache_checksum && hasRealData) {
        ESP_LOGD(TAG, "Good Wifi cache checksum %d, %X:%X:%X:%X:%X:%X, channel %d\n",
            readSum,
            wifi_cache.bssid[0],
            wifi_cache.bssid[1],
            wifi_cache.bssid[2],
            wifi_cache.bssid[3],
            wifi_cache.bssid[4],
            wifi_cache.bssid[5],
            wifi_cache.channel
        );
        return true;
    } else {
        ESP_LOGI(TAG, "Bad wifi cache checksum, clearing storage");
        clearWifiCache();
        return false;
    }
}

void storeWifiCache() {
    uint8_t *bssid = WiFi.BSSID();
    memcpy(wifi_cache.bssid, bssid, sizeof(wifi_cache.bssid));
    wifi_cache.channel = WiFi.channel();

    uint32_t checkSum = 0;
    uint32_t *p = (uint32_t *) wifi_cache.bssid;

    for (uint32_t i = 0; i < sizeof(wifi_cache) / 4; i++) {
        checkSum += p[i];
    }

    wifi_cache_checksum = checkSum;
}


static void setuppins() {
    uint8_t i, j;

    // button columns
    for (i = 0; i < NUM_BTN_COLUMNS; i++) {
        rtc_gpio_deinit(btncolumnpins[i]);
        pinMode(btncolumnpins[i], OUTPUT);
        digitalWrite(btncolumnpins[i], HIGH);
    }

    for (i = 0; i < NUM_LED_COLUMNS; i++) {
        pinMode(ledcolumnpins[i], OUTPUT);
        digitalWrite(ledcolumnpins[i], HIGH);
    }

    // button row input lines
    for (i = 0; i < NUM_BTN_ROWS; i++) {
        rtc_gpio_deinit(btnrowpins[i]);
        pinMode(btnrowpins[i], INPUT_PULLUP);
    }

    // LED drive lines
    for (i = 0; i < NUM_LED_ROWS; i++) {
        pinMode(colorpins[i], OUTPUT);
        digitalWrite(colorpins[i], LOW);
    }

    // Initialize the debounce counter array
    for (i = 0; i < NUM_BTN_COLUMNS; i++) {
        for (j = 0; j < NUM_BTN_ROWS; j++)  {
            debounce_count[i][j] = 0;
        }
    }

    pinMode(5, OUTPUT);
    digitalWrite(5, LOW);
    LEDS_lit = 0;
}

// Button detection, adapted from the sparkfun hookup guide at https://learn.sparkfun.com/tutorials/button-pad-hookup-guide
static void scan() {
    buttons_released = 0;
    static uint8_t current = 0;
    uint8_t val;
    uint8_t j;

    // Select current columns
    digitalWrite(btncolumnpins[current], LOW);
    // pause a moment
    delay(1);

    // Read the button inputs
    for (j = 0; j < NUM_BTN_ROWS; j++) {
        val = digitalRead(btnrowpins[j]);

        if (val == LOW) {
            // active low: val is low when btn is pressed
            if (debounce_count[current][j] < MAX_DEBOUNCE) {
                debounce_count[current][j]++;
            }
        }
        else {
            // otherwise, button is released
            if (debounce_count[current][j] > 0) {
                debounce_count[current][j]--;

                if (debounce_count[current][j] == 0 ) {
                    uint8_t released = (current * NUM_BTN_ROWS) + j;
                    bitSet(buttons_released, released);
                    // Turn on the LED while we're working
                    bitSet(LEDS_lit, released);
                }
            }
        }
    }// for j = 0 to 3;

    delay(1);
    digitalWrite(btncolumnpins[current], HIGH);

    current++;
    if (current >= NUM_BTN_COLUMNS) {
        current = 0;
    }
}

void ledLoop( void * args ) {
  
    uint8_t i;
    static uint8_t current = 0;

    for ( ;; ) {
        for (current = 0; current < NUM_BTN_COLUMNS; current++) {
            delay(1);
            // output LED row values
            for (i = 0; i < NUM_LED_ROWS; i++) {
                digitalWrite(colorpins[i], LOW);
                if (bitRead(LEDS_lit, (current * NUM_BTN_ROWS) + i)) {
                    digitalWrite(ledcolumnpins[current], HIGH);
                }
            }
        }
        delay(10); 
        for (current = 0; current < NUM_BTN_COLUMNS; current++) {
            digitalWrite(ledcolumnpins[current], LOW);
            for (i = 0; i < NUM_LED_ROWS; i++) {
                digitalWrite(colorpins[i], HIGH);
            }
        }
        if (LEDS_lit == 0) {
            delay(25);
        } 
    }
}

void blinkAll(uint8_t times, int waitTime) {
    uint8_t startVal = LEDS_lit;
    for (uint8_t i = 0; i < times; i++) {
       delay(waitTime);
       LEDS_lit = 255;
       delay(waitTime);
       LEDS_lit = startVal;
    }
}

boolean didJustWake() {
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();

    boolean wokeUp = false;
    if (wakeup_reason == ESP_SLEEP_WAKEUP_ULP) {
        ESP_LOGI(TAG, "Woke up from sleep (ULP)");
        sleep_buttons = ulp_wake_gpio_bit & 0xFF;
        ESP_LOGD(TAG, "GPIO pressed was %d\n", ulp_wake_gpio_bit & 0xFF);
        ulp_wake_gpio_bit = 0;
        LEDS_lit = sleep_buttons;
        wokeUp = true;
    } else {
        ESP_LOGW(TAG, "Wakeup was not caused by deep sleep: %d\n",wakeup_reason); 
        sleep_buttons = 0;
    }

    return wokeUp;
}

boolean connectWifi() {
    WiFi.mode(WIFI_STA);
    bool wasCached = checkWifiCache();
    if (wasCached) {
        // Connect using a cached bssid and channel
        ESP_LOGD(TAG, "Connecting using cached wifi config");
        WiFi.begin(SSID, PASSWORD, wifi_cache.channel, wifi_cache.bssid, true);
    } else {
        ESP_LOGD(TAG, "Connecting to without cached wifi config");
        WiFi.begin(SSID, PASSWORD);
    }

    ESP_LOGD(TAG, "Waiting for WiFi to connect...");
    uint8_t wifiTries = 0;
    wl_status_t status = WiFi.status();
    while (status != WL_CONNECTED && status != WL_CONNECT_FAILED && wifiTries < 50) {
        delay(50);
        status = WiFi.status();
        wifiTries += 1;
    }

    if (! WiFi.isConnected() ) {
        if (wasCached) {
            clearWifiCache();
            return connectWifi();
        } else {
            ESP_LOGE(TAG, "WiFi connect failed, restarting");
            delay(1000);
            blinkAll(10, 100);
            ESP.restart();
        }
    }
    ESP_LOGI(TAG, "WiFi connect succeeded");
    return true;
}

void setup() {
    // setup hardware
    Serial.begin(115200);
    setuppins();

    xTaskCreatePinnedToCore(
        ledLoop,
        "LEDLoop",
        1024,
        NULL,
        2,
        NULL,
        1
    );
    boolean woke = didJustWake();

    if (!woke) {
        LEDS_lit = 1;
        delay(250);
        LEDS_lit = 3;
        delay(250);
        LEDS_lit = 7;
        delay(250);
        LEDS_lit = 15;
        delay(250);
        LEDS_lit = 0;
    }
    connectWifi();

    LEDS_lit = 255;
    delay(100);
    LEDS_lit = 0;

    Preferences prefs;
    prefs.begin("sonos", true);
    String addr = prefs.getString("playerAddress", "");
    String prefUid = prefs.getString("playerUid", "");
    prefs.end();
    // If the configured sonos UID is different than what we stored, we need to forget our cached IP
    if (prefUid != String(SONOS_UID)) {
        prefs.begin("sonos", false);
        prefs.remove("playerAddress");
        prefs.end();
        addr = String("");
    }
    if (addr.length() > 0) {
        IPAddress ip;
        ip.fromString(addr.c_str());
        if (ip) {
            targetSonos = ip;
            ESP_LOGI(TAG, "Using cached sonos IP %s", targetSonos.toString().c_str());
        }
    }
    if (!targetSonos) {
        targetSonos = discoverSonos(std::string(SONOS_UID));
    }
}

void napTime() {
    if (WiFi.isConnected()) {
        storeWifiCache();
    }
    esp_wifi_stop();
    ESP_LOGD(TAG, "Starting ULP processor");

    for (uint8_t i = 0; i < NUM_BTN_COLUMNS; i++) {
        rtc_gpio_init(btncolumnpins[i]);
        rtc_gpio_set_direction(btncolumnpins[i], RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_pulldown_dis(btncolumnpins[i]);
        rtc_gpio_pullup_dis(btncolumnpins[i]);
        rtc_gpio_hold_dis(btncolumnpins[i]);
    }
  
    for (uint8_t i = 0; i < NUM_BTN_ROWS; i++) {
        rtc_gpio_init(btnrowpins[i]);
        rtc_gpio_set_direction(btnrowpins[i], RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_dis(btnrowpins[i]);
        rtc_gpio_pullup_en(btnrowpins[i]);
        rtc_gpio_hold_en(btnrowpins[i]);
    }

    esp_sleep_enable_ulp_wakeup();
    ESP_ERROR_CHECK( ulp_load_binary(
        0 /* load address, set to 0 when using default linker scripts */,
        bin_start,
        (bin_end - bin_start) / sizeof(uint32_t)) 
    );
    // Reset the ULP wake bit to zero in case an intervening run set it to a button and it wasn't cleaned up
    ulp_wake_gpio_bit = 0;
    ESP_LOGI(TAG, "Going to sleep now");
    ESP_ERROR_CHECK( ulp_run(&ulp_scan_btns - RTC_SLOW_MEM) );
    // Wakeup the ULP processor every 100 ms to check for button presses
    ESP_ERROR_CHECK( ulp_set_wakeup_period(0, 100000) );
    esp_deep_sleep_start();
}

// Second layer of sonos operation wrapper to handle the rediscovery logic
void doSonos(int (*operation)(HTTPClient *http, IPAddress targetSonos)) {
    if (!targetSonos) {
        targetSonos = discoverSonos(std::string(SONOS_UID));
    }
    if (!targetSonos) {
        ESP_LOGE(TAG, "Couldn't find the right sonos, bailing");
        return;
    }

    int error = sonosOperation(operation, targetSonos);
    if (error) {
        // try rediscovering
        targetSonos = discoverSonos(std::string(SONOS_UID));
    }
}

void loop() {
    static int idleLoopCount = 0;

    int handle_buttons = buttons_released | sleep_buttons;
    // Clear the sleep_buttons variable so we only handle it once
    sleep_buttons = 0;
    scan();
    if (handle_buttons != 0) {
        idleLoopCount = 0;
        if (bitRead(handle_buttons, 0)) {
            ESP_LOGI(TAG, "Sending play/pause");
            doSonos(sonosPlay);
            bitClear(LEDS_lit, 0);
        } else if (bitRead(handle_buttons, 1)) { 
            ESP_LOGI(TAG, "Sending next");
            doSonos(sonosNext);
            bitClear(LEDS_lit, 1);
        } else if (bitRead(handle_buttons, 2)) {
            ESP_LOGI(TAG, "Sending volume up");
            doSonos(volumeUp);
            bitClear(LEDS_lit, 2);
        } else if (bitRead(handle_buttons, 3)) {
            ESP_LOGI(TAG, "Sending volume down");
            doSonos(volumeDown);
            bitClear(LEDS_lit, 3);
        } else {
            ESP_LOGE(TAG, "Not implemented");
            delay(1000);
            LEDS_lit = 0;
        }
    } else {
        delay(5);
        idleLoopCount += 1;
        if (idleLoopCount >= IDLE_LOOPS_SLEEPY) {
            idleLoopCount = 0;
            napTime();
        }
    }
}
