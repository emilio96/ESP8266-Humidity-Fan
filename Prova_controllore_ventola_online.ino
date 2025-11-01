// %%% The used fan is an ARCTIC F12 PWM %%%

#define BLYNK_TEMPLATE_ID ""    // Your blynk teplate id 
#define BLYNK_TEMPLATE_NAME ""  // Your blynk template name
#define BLYNK_AUTH_TOKEN ""     // Your blynk token
#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <Adafruit_AHTX0.h>
#include <time.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>  // NUOVA LIBRERIA PER IL WEB SERVER
#include <BlynkSimpleEsp8266.h>

////////
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
////////

// --- CONFIGURAZIONE UTENTE ---
const char* WIFI_SSID = "";     // Your wifi ssid name
const char* WIFI_PASSWORD = ""; // Your wifi password
const int FAN_PWM_PIN = 14;
const int FAN_TACH_PIN = 12;
const float UMIDITA_MIN = 50.0;  //35.0;
const float UMIDITA_MAX = 85.0;
const int PWM_FREQ = 25000;
const int PWM_MAX = 1023 * 0.25;
const int PWM_MIN = 3;
const int ORA_INIZIO_NOTTE = 23;
const int ORA_FINE_NOTTE = 9;
const int VELOCITA_MAX_NOTTE = 30;  //35;
const char* NTP_SERVER = "pool.ntp.org";
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";

// --- OGGETTO SERVER WEB ---
ESP8266WebServer server(80);  // Crea un'istanza del server sulla porta 80

// --- VARIABILI GLOBALI ---
BlynkTimer timer;
Adafruit_AHTX0 aht;
struct dati_aht10 {
  sensors_event_t humidity, temp;
  unsigned long last_temperature_sync = 0;
} var_aht;
volatile unsigned long tachPulses = 0;
unsigned long lastTachRead = 0;
static unsigned long lastPrint = 0;
float fanRpm = 0.0;
int targetSpeed = 0;
time_t now;
struct tm* timeinfo;
bool isaht = false;
bool isserial = false;
bool istime = false;
bool isNightMode = false;
bool manualOverride = false;  // true = ventola forzata a OFF, false = controllo automatico

float last_sent_temp = -100.0;
float last_sent_hum = -100.0;
float last_sent_rpm = -1.0;
int last_sent_target = -1.0;
bool last_sent_night = false;
bool first_time_sync = false;

// --- FUNZIONI ---

void IRAM_ATTR countPulse() {
  static unsigned long lastMillis = 0;
  if (millis() - lastMillis < 20) { return; }
  tachPulses++;
  lastMillis = millis();
}

BLYNK_WRITE(V0) {
  int value = param.asInt();
  manualOverride = (value == 1);
  if (isserial) Serial.println("Stato override manuale cambiato a: " + String(manualOverride));
}

bool check_before_send() {
  const float temp_threshold = 0.5;
  const float hum_threshold = 0.5;
  const float rpm_threshold = 250;
  const float speed_threshold = 2.5;
  int change_cont = 0;
  if (abs(var_aht.temp.temperature - last_sent_temp) > temp_threshold) change_cont++;
  if (abs(var_aht.humidity.relative_humidity - last_sent_hum) > hum_threshold) change_cont++;
  if (abs(fanRpm - last_sent_rpm) > rpm_threshold) change_cont++;
  if (abs(targetSpeed - last_sent_target) > speed_threshold) change_cont++;
  if (isNightMode != last_sent_night) change_cont++;
  if (change_cont > 0)
    return true;
  else
    return false;
}

// --- FUNZIONE PER INVIARE I DATI A BLYNK TRAMITE HTTPS BATCH ---
void sendBatchToBlynk() {
  //if (WiFi.status() != WL_CONNECTED) return;
  String url = "https://blynk.cloud/external/api/batch/update?token=";
  url += BLYNK_AUTH_TOKEN;
  url += "&v1=" + String(var_aht.temp.temperature, 1);
  url += "&v2=" + String(var_aht.humidity.relative_humidity, 1);
  url += "&v3=" + String(fanRpm, 0);
  url += "&v4=" + String(targetSpeed);
  url += "&v5=" + String(isNightMode ? 1 : 0);

  WiFiClientSecure client;
  client.setInsecure();  // Disabilita verifica certificato (semplice)
  HTTPClient http;

  if (http.begin(client, url)) {
    int code = http.GET();
    String payload = http.getString();
    if (isserial) {
      Serial.print("HTTP code: ");
      Serial.println(code);
      Serial.print("Response: ");
      Serial.println(payload);
    }
    http.end();
  } else {
    if (isserial) Serial.println("Errore inizializzazione HTTPClient");
  }
}

void sendSensorDataToBlynk() {
  if (WiFi.status() != WL_CONNECTED) return;
  bool send = check_before_send();
  if (send) {
    if (Blynk.connected() == false) Blynk.connect(3000);
    //Blynk.virtualWrite(V1, var_aht.temp.temperature, V2, var_aht.humidity.relative_humidity, V3, fanRpm, V4, targetSpeed, V5, isNightMode);
    sendBatchToBlynk();
    last_sent_night = isNightMode;
    last_sent_hum = var_aht.humidity.relative_humidity;
    last_sent_temp = var_aht.temp.temperature;
    last_sent_rpm = fanRpm;
    last_sent_target = targetSpeed;
    if (isserial) Serial.println("Pacchetto dati inviato a Blynk.");
  }
}

void printLocalTime() {
  time(&now);                         // Aggiorna la variabile 'now' con il tempo attuale
  timeinfo = localtime(&now);         // Converte il timestamp in una struttura leggibile

  Serial.print("Data: ");
  Serial.print(timeinfo->tm_mday);    // Giorno del mese (1-31)
  Serial.print("/");
  Serial.print(timeinfo->tm_mon + 1);  // Mese (0-11, quindi aggiungiamo 1)
  Serial.print("/");
  Serial.print(timeinfo->tm_year + 1900);  // Anno (dal 1900, quindi aggiungiamo 1900)

  Serial.print(" | Ora: ");
  Serial.print(timeinfo->tm_hour);  // Ora (0-23)
  Serial.print(":");
  if (timeinfo->tm_min < 10) Serial.print("0");  // Aggiunge lo zero per i minuti < 10
  Serial.print(timeinfo->tm_min);                // Minuti (0-59)
  Serial.print(":");
  if (timeinfo->tm_sec < 10) Serial.print("0");  // Aggiunge lo zero per i secondi < 10
  Serial.println(timeinfo->tm_sec);              // Secondi (0-59)
}


// Funzione #1: Serve la pagina HTML principale (che contiene JavaScript)
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><title>Controllo Ventilazione</title>
<style>
  body{font-family: Arial, sans-serif; background-color: #f4f4f4; text-align: center;}
  .container{width: 80%; max-width: 600px; margin: 20px auto; padding: 20px; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1);}
  h1{color: #333;} p{font-size: 1.2em; color: #555;} span{font-weight: bold; color: #007BFF;}
  .button{display: inline-block; padding: 10px 20px; margin-top: 20px; font-size: 1em; color: white; border-radius: 5px; text-decoration: none; cursor: pointer;}
  .button-on{background-color: #28a745;} .button-off{background-color: #dc3545;}
</style>
</head><body><div class="container">
  <h1>Stato Sistema di Ventilazione</h1>
  <p>Ora: <span id="time">--</span></p>
  <p>Temperatura: <span id="temp">--</span> &deg;C</p>
  <p>Umidita': <span id="hum">--</span> %</p>
  <p>Velocita' Target: <span id="target">--</span> %</p>
  <p>Velocita' Reale: <span id="rpm">--</span> RPM</p>
  <p>Modalita' Notte: <span id="night">--</span></p>
  <p>Stato Controllo: <span id="status">--</span></p>
  <div id="button-container"></div>
</div>
<script>
function updateData() {
  fetch('/data')
    .then(response => response.json())
    .then(data => {
      document.getElementById('time').innerText = data.time;
      document.getElementById('temp').innerText = data.temp;
      document.getElementById('hum').innerText = data.hum;
      document.getElementById('target').innerText = data.target;
      document.getElementById('rpm').innerText = data.rpm;
      document.getElementById('night').innerText = data.night ? 'ATTIVA' : 'NON ATTIVA';
      
      let statusText = data.override ? 'MANUALE (SPENTO)' : 'AUTOMATICO';
      document.getElementById('status').innerText = statusText;
      
      let btnContainer = document.getElementById('button-container');
      if (data.override) {
        btnContainer.innerHTML = "<a href='/?fan=on' class='button button-on'>Riattiva Controllo Automatico</a>";
      } else {
        btnContainer.innerHTML = "<a href='/?fan=off' class='button button-off'>Forza Spegnimento Ventola</a>";
      }
    });
}
setInterval(updateData, 2000); // Aggiorna i dati ogni 2 secondi
window.onload = updateData; // Aggiorna subito al caricamento della pagina
</script>
</body></html>
)rawliteral";

  // Gestione dei comandi ON/OFF
  if (server.hasArg("fan")) {
    String fanState = server.arg("fan");
    if (fanState == "off") {
      manualOverride = true;
    } else if (fanState == "on") {
      manualOverride = false;
    }
    Blynk.virtualWrite(V0, manualOverride ? 1 : 0);
  }

  server.send(200, "text/html", html);
}

// Serve solo i dati in formato JSON
void handleData() {
  time(&now);
  timeinfo = localtime(&now);
  char timeStr[20];
  sprintf(timeStr, "%02d/%02d/%04d %02d:%02d:%02d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  String json = "{";
  json += "\"time\":\"" + String(timeStr) + "\",";
  json += "\"temp\":" + String(var_aht.temp.temperature) + ",";
  json += "\"hum\":" + String(var_aht.humidity.relative_humidity) + ",";
  json += "\"target\":" + String(targetSpeed) + ",";
  json += "\"rpm\":" + String(fanRpm) + ",";
  json += "\"night\":" + String(isNightMode ? "true" : "false") + ",";
  json += "\"override\":" + String(manualOverride ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void fan_rpm() {
  if (millis() - lastTachRead >= 10000) {
    detachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN));
    fanRpm = (tachPulses * 0.1 * 60.0) / 2.0;
    tachPulses = 0;
    lastTachRead = millis();
    attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), countPulse, FALLING);
  }
}

void read_sensor_data() {
  if (millis() - var_aht.last_temperature_sync >= 10000) {
    aht.getEvent(&var_aht.humidity, &var_aht.temp);
    var_aht.temp.temperature = round(var_aht.temp.temperature * 10) / 10;
    var_aht.humidity.relative_humidity = round(var_aht.humidity.relative_humidity * 10) / 10;
    var_aht.last_temperature_sync = millis();
  }
}

void target_speed() {
  if (manualOverride) {
    targetSpeed = 0;
    return;
  }
  if (var_aht.humidity.relative_humidity >= UMIDITA_MIN) {
    targetSpeed = map(var_aht.humidity.relative_humidity, UMIDITA_MIN, UMIDITA_MAX, 0, 100);
    targetSpeed = constrain(targetSpeed, 0, 100);
  } else if (var_aht.humidity.relative_humidity < UMIDITA_MIN)
    targetSpeed = 0;
  //Modalità notte
  if (istime) {
    time(&now);                  // Aggiorna la variabile 'now' con il tempo attuale
    timeinfo = localtime(&now);  // Converte il timestamp in una struttura leggibile
    int oraCorrente = timeinfo->tm_hour;
    isNightMode = (oraCorrente >= ORA_INIZIO_NOTTE || oraCorrente < ORA_FINE_NOTTE);
    if (isNightMode) {
      targetSpeed = map(var_aht.humidity.relative_humidity, UMIDITA_MIN, UMIDITA_MAX, 0, VELOCITA_MAX_NOTTE);
      targetSpeed = constrain(targetSpeed, 0, VELOCITA_MAX_NOTTE);
    }
  } else if (!istime && !first_time_sync) {  // Se non ho sincronizzato l'orario nemmeno la prima volta vado a vedlocià basse
    targetSpeed = map(var_aht.humidity.relative_humidity, UMIDITA_MIN, UMIDITA_MAX, 0, VELOCITA_MAX_NOTTE);
    targetSpeed = constrain(targetSpeed, 0, VELOCITA_MAX_NOTTE);
  }
}

void pwm_setting() {
  int pwmValue = map(targetSpeed, 0, 100, 3, PWM_MAX);
  analogWrite(FAN_PWM_PIN, pwmValue);
}

void wifi_check() {
  if (WiFi.status() == WL_CONNECTED && Blynk.connected() == true) {
    return;
  } else if (WiFi.status() == WL_CONNECTED && Blynk.connected() == false) {
    Blynk.connect(3000);
  } else {
    if (isserial) Serial.print("Tentativo riconnessione a WiFi e BLynk ");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long wifiStartTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 3000) {
      delay(100);
      if (isserial) Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) Blynk.connect(3000);
    if (Blynk.connected() == true) {
      if (isserial) Serial.println("Connessione riuscita!");
    } else {
      if (isserial) Serial.println("Connessione fallita :( Nuovo tentativo tra 30 min.");
    }
  }
}

void resyncTime() {
  if (WiFi.status() != WL_CONNECTED)
    wifi_check();
  if (WiFi.status() == WL_CONNECTED) {
    configTime(TZ_INFO, NTP_SERVER);
    unsigned long ntp_start_time = millis();
    while (time(nullptr) < 1672531200 && millis() - ntp_start_time < 3000) {
      delay(100);
    }
    if (time(nullptr) > 1672531200) {
      istime = true;
      if (isserial) Serial.println("Orario sincronizzato con successo!");
    } else {
      istime = false;
      if (isserial) Serial.println("Sincronizzazione orario fallita.");
    }
  } else {
    if (isserial) Serial.println("Sincronizzazione orario fallita perchè non connesso al WiFi.");
    istime = false;
  }
}

void report_print() {
  if (isserial) {
    Serial.println("--- REPORT ---");
    // ... (tutta la tua logica di stampa rimane invariata) ...
    if (istime) printLocalTime();
    else Serial.println("Ora: non disponibile");
    Serial.print("Temperatura: ");
    Serial.print(var_aht.temp.temperature);
    Serial.println(" *C");
    Serial.print("Umidita': ");
    Serial.print(var_aht.humidity.relative_humidity);
    Serial.println(" %");
    Serial.print("Velocita' Ventola (Target): ");
    Serial.print(targetSpeed);
    Serial.println(" %");
    Serial.print("Velocita' Ventola (RPM Reali): ");
    Serial.print(fanRpm);
    Serial.println(" RPM");
    Serial.print("Modalita' Notte: ");
    Serial.println(isNightMode ? "ATTIVA" : "NON ATTIVA");
    lastPrint = millis();
  }
}

void run_control() {
  read_sensor_data();
  target_speed();
  pwm_setting();
  if (millis() - lastPrint >= 10000) {
    report_print();
  }
}

void first_update() {
  target_speed();
  pwm_setting();
  //fan_rpm();    //Non viene eseguita perchè non sono passati 10s
  delay(3000);
  sendSensorDataToBlynk();
  //Blynk.virtualWrite(V1, var_aht.temp.temperature, V2, var_aht.humidity.relative_humidity, V3, fanRpm, V4, targetSpeed, V5, isNightMode);
}

void setup() {
  Serial.begin(115200);
  unsigned long start_time = millis();
  while (!Serial && millis() - start_time < 3000) {}
  if (!Serial) isserial = false;
  else isserial = true;
  if (isserial) Serial.println("\n\nAvvio del sistema di controllo ventilazione...");

  if (!aht.begin()) {
    if (isserial) Serial.println("Errore: sensore AHT10 non trovato! Blocco del programma.");
    isaht = false;
  } else {
    isaht = true;
    if (isserial) Serial.println("Sensore AHT10 inizializzato.");
    //Prima lettura
    aht.getEvent(&var_aht.humidity, &var_aht.temp);
    var_aht.temp.temperature = round(var_aht.temp.temperature * 10) / 10;
    var_aht.humidity.relative_humidity = round(var_aht.humidity.relative_humidity * 10) / 10;
  }

  pinMode(FAN_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), countPulse, FALLING);
  analogWriteFreq(PWM_FREQ);
  pinMode(FAN_PWM_PIN, OUTPUT);
  analogWrite(FAN_PWM_PIN, PWM_MIN);
  if (!isaht) analogWrite(FAN_PWM_PIN, 80);
  if (isserial) Serial.println("Controllo ventola inizializzato.");

  if (isserial) Serial.print("Tentativo di connessione a ");
  if (isserial) Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 30000) {
    delay(500);
    if (isserial) Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (isserial) Serial.println("\nConnessione WiFi riuscita!");
    if (isserial) Serial.print("Indirizzo IP: ");
    if (isserial) Serial.println(WiFi.localIP());
    // Sincronizzazione dell'ora
    resyncTime();
    if (istime) first_time_sync = true;
    // --- AVVIO DEL SERVER WEB ---
    server.on("/", handleRoot);  // Associa la funzione handleRoot all'indirizzo principale
    server.on("/data", handleData);
    server.begin();  // Avvia il server
    if (isserial) Serial.println("Server HTTP avviato.");
    // Avvio di blynk
    //Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD);
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect();
    if (isserial) Serial.println("Sincronizzazione Blynk avviata");
  } else {
    istime = false;
    if (isserial) Serial.println("\nConnessione WiFi fallita! Funzionamento offline.");
  }
  //10000L = 10s. 120000L = 120s = 2m.
  timer.setInterval(120000L, sendSensorDataToBlynk);  // ogni 2 minuti
  timer.setInterval(5000L, run_control);              // ogni 5 secondi
  timer.setInterval(6L * 3600L * 1000L, resyncTime);  // ogni 6 ore
  timer.setInterval(30L * 60L * 1000L, wifi_check);   // ogni 30 minuti
  //Primo calcolo e invio dei parametri
  first_update();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();  // Controlla se ci sono client connessi
    if (Blynk.connected()) {
      Blynk.run();  // Mantiene la connessione e gestisce gli eventi di Blynk
    } else {
      Blynk.connect(1000);
      if (Blynk.connected()) Blynk.run();
    }
  }
  fan_rpm();
  timer.run();  // Esegue le funzioni schedulate dal timer
}