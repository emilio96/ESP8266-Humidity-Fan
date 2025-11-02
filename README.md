# ESP8266 Smart Fan Controller for Humidity Control

This project is an intelligent fan controller designed to manage air circulation in areas with high humidity, such as behind wardrobes or in damp rooms.

It operates by monitoring ambient temperature and humidity with an **AHTx0 sensor**. Based on these readings, it intelligently controls the speed of a 4-pin PWM computer fan (specifically tested with an ARCTIC F12 PWM) to ensure continuous air movement and prevent moisture buildup.

The system offers two primary interfaces for monitoring and control:
1.  A **Blynk** dashboard for remote access.
2.  A **local Web Server** that provides a clean, modern UI for anyone on the same WiFi network.


## Key Features

* **Humidity-Based Speed Control:** Fan speed is automatically adjusted based on humidity. The fan is off below a minimum threshold (`UMIDITA_MIN`) and scales up as it approaches the maximum threshold (`UMIDITA_MAX`).
***Smart Night Mode:** Automatically reduces the maximum fan speed during specific overnight hours to minimize noise. This requires an active internet connection for NTP time synchronization.
* **Dual Interface (Blynk & Web):**
    * **Blynk Integration:**
        * Sends all key data (Temp, Humidity, RPM, Target Speed, Night Mode) to the Blynk cloud.
        * Uses an efficient `HTTPS batch/update` API call to send all sensor data in one request.
        * Features a `manualOverride` switch on Virtual Pin `V0` to remotely force the fan off.
    * **Local Web Server:**
        * Hosts a responsive HTML/JavaScript webpage on the ESP8266's local IP address.
        * The page provides a real-time dashboard displaying all sensor readings and system status.
        * Includes a toggle button to switch between "Automatic" control and "Manual Override (Off)".
**True RPM Feedback:** Reads the fan's tachometer (TACH) pin to measure and report the *actual* fan speed in RPM.
**Non-Blocking Operation:** Uses `BlynkTimer` to manage all tasks (sensor reads, control logic, data sending, WiFi checks), ensuring the main loop remains responsive.
**25kHz PWM Frequency:** Operates the fan at the standard 25kHz PWM frequency for precise and quiet control.

---

## Hardware & Build

### Required Components

* **Controller:** ESP8266 (e.g., NodeMCU, Wemos D1 Mini).
* **Sensor:** `Adafruit AHTX0` (AHT10 or AHT20) I2C Temperature & Humidity Sensor.
* **Fan:** 1x 4-Pin PWM PC Fan (e.g., ARCTIC F12 PWM).
* **Power Supply:** 12V DC Power Adapter.
* **Voltage Regulator:** `LM7805` 5V linear voltage regulator (and any necessary capacitors).

### Power Setup

The system is designed to run from a single **12V DC power adapter**:
1.  The **12V line** is used to power the fan directly (via its 12V pin).
2.  The **12V line** also feeds the input of an **LM7805 linear voltage regulator**.
3.  The **5V output** from the LM7805 is used to power the ESP8266 (via its `Vin` pin).

### 3D-Printed Enclosure

This repository includes the `.stl` files for a custom 3D-printed enclosure. The case is designed to house the ESP8266, AHTx0 sensor, and the voltage regulator circuit.

---

## Libraries & Dependencies

* `ESP8266WiFi`
* `Adafruit_AHTX0`
* `time.h`
* `WiFiUdp`
* `ESP8266WebServer`
* `BlynkSimpleEsp8266`
* `ESP8266HTTPClient`
* `WiFiClientSecure`

---

## Configuration

Before uploading, you must set the user configuration variables at the top of the code file:

// --- BLYNK ---
#define BLYNK_TEMPLATE_ID ""    // Your blynk template id
#define BLYNK_TEMPLATE_NAME ""  // Your blynk template name
#define BLYNK_AUTH_TOKEN ""     // Your blynk token

// --- WIFI ---
const char* WIFI_SSID = "";     // Your wifi ssid name
const char* WIFI_PASSWORD = ""; // Your wifi password

// --- FAN PINS ---
const int FAN_PWM_PIN = 14;     // Pin D5 on NodeMCU
const int FAN_TACH_PIN = 12;    // Pin D6 on NodeMCU

// --- CONTROL LOGIC ---
const float UMIDITA_MIN = 50.0; // Humidity % to start the fan
const float UMIDITA_MAX = 85.0; // Humidity % to run fan at 100%

// --- NIGHT MODE ---
const int ORA_INIZIO_NOTTE = 23;  // 11:00 PM
const int ORA_FINE_NOTTE = 9;   // 9:00 AM
const int VELOCITA_MAX_NOTTE = 30; // Max speed % during the night

// --- TIMEZONE ---
const char* NTP_SERVER = "pool.ntp.org";
// Find your TZ String here: [https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv)
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3"; // Example for Central Europe


## Blynk Dashboard Setup
The code is pre-configured to send data to specific Virtual Pins. Set up your Blynk dashboard with the following widgets:

V0 (Switch): Power
  Set as a Switch widget.
  Controls the manualOverride. Label it "Power" or "Manual Override".

V1 (Labeled Value): Temperature
  Set as a Labeled Value or Gauge widget.
  Receives var_aht.temp.temperature. Suggested label: %.1f °C.

V2 (Gauge): Humidity
  Set as a Gauge widget (Range 0-100).
  Receives var_aht.humidity.relative_humidity. Suggested label: %.1f %%.

V3 (Gauge): RPM
  Set as a Gauge widget (Range 0-1500, or as needed).
  Receives fanRpm. Suggested label: %.0f RPM.

V4 (Gauge): Target Speed
  Set as a Gauge widget (Range 0-100).
  Receives targetSpeed. Suggested label: %.0f %%.

V5 (LED): NightMode
  Set as an LED widget.
  Receives isNightMode status (1 for ON, 0 for OFF).

Chart (SuperChart):
  Add a SuperChart widget.
  Create datastreams linked to V1 (Temperature) and V2 (Humidity).
