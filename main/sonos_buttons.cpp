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

//config variables
#define NUM_BTN_COLUMNS (4)
#define NUM_BTN_ROWS (1)
#define NUM_LED_COLUMNS (4)
#define NUM_LED_ROWS (1)

#define MAX_DEBOUNCE (3)

// This is roughly 30 seconds with the various delays + scanning time
#define IDLE_LOOPS_SLEEPY 4500

/*
 * Your configuration goes here
 */
#define SSID "your ssid"
#define PASSWORD "your password"
static const std::string SONOS_UID = std::string("your sonos player UID");


static const gpio_num_t btncolumnpins[NUM_BTN_COLUMNS] = {GPIO_NUM_12, GPIO_NUM_14, GPIO_NUM_27, GPIO_NUM_26};
static const gpio_num_t btnrowpins[NUM_BTN_ROWS]       = {GPIO_NUM_13};

static const gpio_num_t ledcolumnpins[NUM_LED_COLUMNS] = {GPIO_NUM_17, GPIO_NUM_4, GPIO_NUM_0, GPIO_NUM_2};
static const gpio_num_t colorpins[NUM_LED_ROWS]        = {GPIO_NUM_16};

extern const uint8_t bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t bin_end[]   asm("_binary_ulp_main_bin_end");

static int8_t debounce_count[NUM_BTN_COLUMNS][NUM_BTN_ROWS];


// bit field representing the buttons activated on the last scan loop
static uint8_t buttons_released = 0;
// bit field representing the buttons activated that caused us to wakeup
static uint8_t sleep_buttons = 0;

static uint8_t LEDS_lit = 0;
static IPAddress targetSonos;

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
    boolean touchWakeUp = false;
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0 : 
        case ESP_SLEEP_WAKEUP_EXT1 : 
        case ESP_SLEEP_WAKEUP_TIMER :
        case ESP_SLEEP_WAKEUP_TOUCHPAD : 
            Serial.println("Woke up from touch");
            touchWakeUp = true;
            // fall through
        case ESP_SLEEP_WAKEUP_ULP : 
            Serial.println("Woke up from sleep (ULP)");
            Serial.printf("GPIO pressed was %d\n", ulp_wake_gpio_bit);
            sleep_buttons = ulp_wake_gpio_bit;
            LEDS_lit = sleep_buttons;
            wokeUp = true;
            break;
        default : 
            Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); 
            wokeUp = false;
            break;
    }

    if (touchWakeUp) {
        touch_pad_t pin;
        pin = esp_sleep_get_touchpad_wakeup_status();
        Serial.printf("Got touch wakeup from %d\n", pin);
    }

    return wokeUp;
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
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);

    Serial.print("Waiting for WiFi to connect...");
    uint8_t wifiTries = 0;
    wl_status_t status = WiFi.status();
    while (status != WL_CONNECTED && wifiTries < 10) {
        Serial.printf("Wifi status is: %d\n", status);
        delay(500);
        status = WiFi.status();
        wifiTries += 1;
    }
  
    if (! WiFi.isConnected() ) {
        Serial.println(" Failed, restarting");
        delay(1000);
        blinkAll(10, 100);
        ESP.restart();
    }
    Serial.println(" connected");

    LEDS_lit = 255;
    delay(100);
    LEDS_lit = 0;

    Preferences prefs;
    prefs.begin("sonos", true);
    String addr = prefs.getString("playerAddress", "");
    String prefUid = prefs.getString("playerUid", "");
    prefs.end();
    // If the configured sonos UID is different than what we stored, we need to forget our cached IP
    if (prefUid != String(SONOS_UID.c_str())) {
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
            Serial.printf("Using cached sonos IP %s\n", targetSonos.toString().c_str());
        }
    }
    if (!targetSonos) {
        targetSonos = discoverSonos(SONOS_UID);
    }
}

void napTime() {
    esp_wifi_stop();
    Serial.println("Starting ULP processor");

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
    // Wakeup the ULP processor every 100 ms to check for button presses
    ESP_ERROR_CHECK( ulp_set_wakeup_period(0, 100000) );
    ESP_ERROR_CHECK( ulp_load_binary(
        0 /* load address, set to 0 when using default linker scripts */,
        bin_start,
        (bin_end - bin_start) / sizeof(uint32_t)) 
    );

    Serial.println("Going to sleep now");
    ESP_ERROR_CHECK( ulp_run(&ulp_test_set - RTC_SLOW_MEM) );
    esp_deep_sleep_start();
}

// Second layer of sonos operation wrapper to handle the rediscovery logic
void doSonos(int (*operation)(HTTPClient *http, IPAddress targetSonos)) {
    if (!targetSonos) {
        targetSonos = discoverSonos(SONOS_UID);
    }
    if (!targetSonos) {
        Serial.println("Couldn't find the right sonos, bailing");
        return;
    }

    int error = sonosOperation(operation, targetSonos);
    if (error) {
        // try rediscovering
        targetSonos = discoverSonos(SONOS_UID);
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
            Serial.println("Sending play/pause");
            doSonos(sonosPlay);
            bitClear(LEDS_lit, 0);
        } else if (bitRead(handle_buttons, 1)) { 
            Serial.println("Sending next");
            doSonos(sonosNext);
            bitClear(LEDS_lit, 1);
        } else if (bitRead(handle_buttons, 2)) {
            Serial.println("Sending volume up");
            doSonos(volumeUp);
            bitClear(LEDS_lit, 2);
        } else if (bitRead(handle_buttons, 3)) {
            Serial.println("Sending volume down");
            doSonos(volumeDown);
            bitClear(LEDS_lit, 3);
        } else {
            Serial.println("Not implemented");
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
