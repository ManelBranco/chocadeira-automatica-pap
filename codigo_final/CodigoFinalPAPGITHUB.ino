#define BLYNK_TEMPLATE_ID "ID do seu Template do Blynk"
#define BLYNK_TEMPLATE_NAME "Nome do seu Template do Blynk"
#define BLYNK_AUTH_TOKEN "Auth_Token do seu Dispositivo do Blynk"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <RTClib.h>
#include <time.h>

/*
  CHOCADEIRA AUTOMÁTICA - ESP32 NODEMCU-32

  FUNÇÕES:
  - Menu por botões
  - Presets de incubação
  - LCD I2C 16x2
  - RTC DS3231
  - DHT22
  - Servo de viragem
  - 2 relés de aquecimento
  - Controlo de humidificador por pulso
  - Página web para telemóvel
  - Compatibilidade com o Blynk

*/
// ===================== Blynk =====================
const char* NTP_TIMEZONE = "WET0WEST,M3.5.0/1,M10.5.0";
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const char* NTP_SERVER_3 = "time.windows.com";

// ===================== WIFI =====================
const char* WIFI_SSID = "Nome da Sua Rede";
const char* WIFI_PASS = "Password da Sua Rede";

WebServer server(80);

// ===================== PINOS ESP32 =====================
#define I2C_SDA 21
#define I2C_SCL 22

#define DHTPIN 4
#define DHTTYPE DHT22

#define BTN_NEXT   18
#define BTN_SELECT 19
#define BTN_BACK   23

#define SERVO_PIN 13
#define HUMIDIFIER_PIN 26
#define HEATER_RELAY_1 27
#define BUZZER_PIN 25
#define HEATER_RELAY_2 33

// ===================== AJUSTES DE HARDWARE =====================
// Com o comportamento observado neste hardware:
// LOW = desligado
// HIGH = ligado
const bool HEATER_RELAY_1_ACTIVE_LOW = false;  // pino 27
const bool HEATER_RELAY_2_ACTIVE_LOW = false;  // pino 33 (principal)

// Mudar para true se a saída do humidificador for ativa em LOW
const bool HUMIDIFIER_PULSE_ACTIVE_LOW = true;

// PWM do servo
const int SERVO_MIN_US = 500;
const int SERVO_MAX_US = 2400;

// ===================== LCD / RTC / DHT / SERVO =====================
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
DHT dht(DHTPIN, DHTTYPE);
Servo servoMotor;

// ===================== PRESETS =====================
struct Preset {
  const char* name;
  float targetTemp;
  int targetHum;
  int durationDays;
};

Preset presets[] = {
  {"GALINHA", 37.6, 55, 21},
  {"CODORNIZ", 37.5, 55, 17},
  {"PATO", 37.8, 60, 28},
  {"PERU", 37.2, 55, 28},
  {"FAISAO", 37.5, 55, 25},
  {"PAVAO", 37.5, 55, 30}
};

int viragensPorDia[] = {4, 3, 6, 4, 4, 4};

const int NUM_PRESETS = sizeof(presets) / sizeof(presets[0]);
const int LAST_DAYS = 3;
const int HUMIDITY_INCREASE = 10;

// ===================== ESTADO =====================
int selectedPreset = 0;
bool presetConfirmado = false;
bool incubacaoTerminada = false;
bool wifiLigado = false;
bool rtcOk = false;
bool blynkConfigurado = false;
bool blynkManualAquecimento = false;
bool cicloHumidificadorManualBlynk = false;

DateTime startDate;

float tempAtual = NAN;
float humAtual = NAN;

bool aquecimentoLigado = false;
bool humidificadorLigado = false;
int relesAquecimentoAtivos = 0;

bool prevNext = HIGH;
bool prevSelect = HIGH;
bool prevBack = HIGH;

unsigned long ultimaViragemUnix = 0;
unsigned long intervaloViragem = 0;
unsigned long ultimoPrintSerial = 0;
unsigned long inicioPulsoHumidificador = 0;
unsigned long ultimoCicloHumidade = 0;
unsigned long ultimoLeituraSensor = 0;
unsigned long ultimoLCD = 0;
unsigned long ultimoBuzzerFim = 0;
unsigned long ultimoEnvioBlynk = 0;
unsigned long ultimoReconnectBlynk = 0;
unsigned long ultimaSincronizacaoRtc = 0;

int margemHum = 3;
float margemTemp = 0.25;

const unsigned long DURACAO_PULSO_HUM_MS = 8000;
const unsigned long INTERVALO_MIN_HUM_MS = 20000;
const unsigned long INTERVALO_SENSOR_MS = 2000;
const unsigned long INTERVALO_LCD_MS = 1000;
const unsigned long INTERVALO_SERIAL_MS = 30000;
const unsigned long BUZZER_INTERVALO_MS = 30000;
const unsigned long INTERVALO_BLYNK_MS = 3000;
const unsigned long INTERVALO_RECONNECT_BLYNK_MS = 10000;
const unsigned long INTERVALO_RESYNC_RTC_MS = 21600000UL;

// ===================== FUNÇÕES BASE =====================
void escreverSaidaLogica(int pin, bool ativo, bool activeLow) {
  digitalWrite(pin, (ativo ^ activeLow) ? HIGH : LOW);
}

bool relayHeaterActiveLow(int pin) {
  if (pin == HEATER_RELAY_2) return HEATER_RELAY_2_ACTIVE_LOW;
  return HEATER_RELAY_1_ACTIVE_LOW;
}

void setRelayPin(int pin, bool ligado) {
  escreverSaidaLogica(pin, ligado, relayHeaterActiveLow(pin));
}

void inicializarSaidasCriticas() {
  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(HEATER_RELAY_1, OUTPUT);
  pinMode(HEATER_RELAY_2, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  inicializarSaidaHumidificadorIdle();
  setRelayPin(HEATER_RELAY_1, false);
  setRelayPin(HEATER_RELAY_2, false);
}

void inicializarSaidaHumidificadorIdle() {
  // deixa a linha em repouso
  digitalWrite(HUMIDIFIER_PIN, HUMIDIFIER_PULSE_ACTIVE_LOW ? HIGH : LOW);
}

void pulsoHumidificador() {
  digitalWrite(HUMIDIFIER_PIN, HUMIDIFIER_PULSE_ACTIVE_LOW ? LOW : HIGH);
  delay(120);
  digitalWrite(HUMIDIFIER_PIN, HUMIDIFIER_PULSE_ACTIVE_LOW ? HIGH : LOW);
}

void ligarHumidificador() {
  pulsoHumidificador();
}

void desligarHumidificador() {
  pulsoHumidificador();
  delay(200);
  pulsoHumidificador();
}

void ligarHumidificadorSeNecessario() {
  if (!humidificadorLigado) {
    ligarHumidificador();
    humidificadorLigado = true;
    inicioPulsoHumidificador = millis();
    ultimoCicloHumidade = millis();
    Serial.println("HUMIDIFICADOR LIGADO");
  }
}

void desligarHumidificadorSeNecessario() {
  if (humidificadorLigado) {
    desligarHumidificador();
    humidificadorLigado = false;
    cicloHumidificadorManualBlynk = false;
    Serial.println("HUMIDIFICADOR DESLIGADO");
  }
}

void definirAquecimento(int relesAtivos) {
  if (relesAtivos < 0) relesAtivos = 0;
  if (relesAtivos > 2) relesAtivos = 2;

  // O relé do pino 33 é o principal. O do pino 27 só entra como reforço.
  bool ligarRelayPrincipal = (relesAtivos >= 1);
  bool ligarRelayApoio = (relesAtivos >= 2);

  setRelayPin(HEATER_RELAY_2, ligarRelayPrincipal);
  setRelayPin(HEATER_RELAY_1, ligarRelayApoio);

  relesAquecimentoAtivos = (ligarRelayPrincipal ? 1 : 0) + (ligarRelayApoio ? 1 : 0);
  aquecimentoLigado = (relesAquecimentoAtivos > 0);
}

void beep(unsigned int freq, unsigned long duracaoMs) {
  ledcAttach(BUZZER_PIN, freq, 8);
  ledcWriteTone(BUZZER_PIN, freq);
  delay(duracaoMs);
  ledcWriteTone(BUZZER_PIN, 0);
}

String formatarDataHora(const DateTime& dt) {
  char buf[25];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  return String(buf);
}

String boolTxt(bool v) {
  return v ? "SIM" : "NAO";
}

bool rtcHoraValida(const DateTime& dt) {
  return dt.year() >= 2024 && dt.year() <= 2099;
}

DateTime horaCompilacao() {
  return DateTime(F(__DATE__), F(__TIME__));
}

bool sincronizarRTCComNTP(bool forcar) {
  if (!wifiLigado || !rtcOk) return false;

  if (!forcar && ultimaSincronizacaoRtc != 0 &&
      millis() - ultimaSincronizacaoRtc < INTERVALO_RESYNC_RTC_MS) {
    return true;
  }

  configTzTime(NTP_TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 15000)) {
    Serial.println("Falha ao obter hora por NTP");
    return false;
  }

  DateTime horaNtp(timeinfo.tm_year + 1900,
                   timeinfo.tm_mon + 1,
                   timeinfo.tm_mday,
                   timeinfo.tm_hour,
                   timeinfo.tm_min,
                   timeinfo.tm_sec);

  DateTime horaRtc = rtc.now();
  long diferenca = (long)horaRtc.unixtime() - (long)horaNtp.unixtime();
  if (diferenca < 0) diferenca = -diferenca;

  if (forcar || rtc.lostPower() || !rtcHoraValida(horaRtc) || diferenca > 2) {
    rtc.adjust(horaNtp);
    Serial.print("RTC sincronizado por NTP: ");
    Serial.println(formatarDataHora(horaNtp));
  }

  ultimaSincronizacaoRtc = millis();
  return true;
}

void garantirHoraValidaRTC() {
  if (!rtcOk) return;

  if (wifiLigado && sincronizarRTCComNTP(true)) {
    return;
  }

  DateTime horaRtc = rtc.now();
  if (rtc.lostPower() || !rtcHoraValida(horaRtc)) {
    DateTime compilacao = horaCompilacao();
    rtc.adjust(compilacao);
    ultimaSincronizacaoRtc = millis();
    Serial.print("RTC ajustado para compilacao: ");
    Serial.println(formatarDataHora(compilacao));
  }
}

bool blynkLigado() {
  return wifiLigado && blynkConfigurado && Blynk.connected();
}

// ===================== TEMPO / ESTADO =====================

DateTime agoraRTC() {
  if (rtcOk) return rtc.now();
  return horaCompilacao();
}

int diaAtualIncubacao() {
  if (!presetConfirmado || !rtcOk) return 0;
  DateTime now = rtc.now();
  int dia = (now.unixtime() - startDate.unixtime()) / 86400 + 1;
  if (dia < 1) dia = 1;
  int maxDias = presets[selectedPreset].durationDays;
  if (dia > maxDias) dia = maxDias;
  return dia;
}

bool emUltimosDias() {
  if (!presetConfirmado) return false;
  int diaAtual = diaAtualIncubacao();
  return diaAtual > (presets[selectedPreset].durationDays - LAST_DAYS);
}

int humidadeAlvoAtual() {
  int humAlvo = presets[selectedPreset].targetHum;
  if (presetConfirmado && emUltimosDias()) humAlvo += HUMIDITY_INCREASE;
  return humAlvo;
}

float temperaturaAlvoAtual() {
  return presets[selectedPreset].targetTemp;
}

unsigned long segundosProximaViragem() {
  if (!presetConfirmado || incubacaoTerminada || emUltimosDias() || !rtcOk) return 0;
  unsigned long agora = rtc.now().unixtime();
  unsigned long decorridos = agora - ultimaViragemUnix;
  return (decorridos >= intervaloViragem) ? 0 : (intervaloViragem - decorridos);
}

void reiniciarSistema() {
  presetConfirmado = false;
  incubacaoTerminada = false;
  blynkManualAquecimento = false;
  cicloHumidificadorManualBlynk = false;
  desligarHumidificadorSeNecessario();
  definirAquecimento(0);
  humidificadorLigado = false;
  aquecimentoLigado = false;
  relesAquecimentoAtivos = 0;
  inicioPulsoHumidificador = 0;
  ultimoCicloHumidade = 0;
  ultimoEnvioBlynk = 0;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SELECIONE");
  lcd.setCursor(0, 1);
  lcd.print("PRESET");
}

void iniciarIncubacao() {
  if (!rtcOk) return;
  presetConfirmado = true;
  incubacaoTerminada = false;
  blynkManualAquecimento = false;
  cicloHumidificadorManualBlynk = false;
  desligarHumidificadorSeNecessario();
  definirAquecimento(0);
  humidificadorLigado = false;
  aquecimentoLigado = false;
  relesAquecimentoAtivos = 0;

  startDate = rtc.now();
  ultimaViragemUnix = startDate.unixtime();
  intervaloViragem = 86400 / viragensPorDia[selectedPreset];
  inicioPulsoHumidificador = 0;
  ultimoCicloHumidade = millis() - INTERVALO_MIN_HUM_MS;
  ultimoLeituraSensor = 0;
  ultimoPrintSerial = 0;

  lcd.clear();
}

void viragemManual() {
  servoMotor.write(110);
  delay(2000);
  servoMotor.write(90);
  if (presetConfirmado && rtcOk) ultimaViragemUnix = rtc.now().unixtime();
}

void gerirPulsoHumidificador() {
  if (humidificadorLigado && millis() - inicioPulsoHumidificador >= DURACAO_PULSO_HUM_MS) {
    desligarHumidificadorSeNecessario();
  }
}

void resetarComandosBlynk() {
  if (!blynkLigado()) return;
  Blynk.virtualWrite(V3, 0);
  Blynk.virtualWrite(V4, 0);
  Blynk.virtualWrite(V5, 0);
  Blynk.virtualWrite(V6, 0);
}

void atualizarEstadoBlynk() {
  if (!blynkLigado()) return;

  Blynk.virtualWrite(V0, isnan(tempAtual) ? 0 : tempAtual);
  Blynk.virtualWrite(V1, isnan(humAtual) ? 0 : humAtual);
  Blynk.virtualWrite(V2, blynkManualAquecimento ? 1 : 0);
  Blynk.virtualWrite(V7, diaAtualIncubacao());
  Blynk.virtualWrite(V8, temperaturaAlvoAtual());
  Blynk.virtualWrite(V9, humidadeAlvoAtual());
  Blynk.virtualWrite(V10, relesAquecimentoAtivos);
  Blynk.virtualWrite(V11, aquecimentoLigado ? 1 : 0);
  Blynk.virtualWrite(V12, humidificadorLigado ? 1 : 0);
  Blynk.virtualWrite(V13, formatarDataHora(agoraRTC()));
  Blynk.virtualWrite(V14, presets[selectedPreset].name);
  Blynk.virtualWrite(V15, presetConfirmado ? 1 : 0);
  Blynk.virtualWrite(V16, incubacaoTerminada ? 1 : 0);
}

BLYNK_CONNECTED() {
  Serial.println("Blynk ligado");
  resetarComandosBlynk();
  atualizarEstadoBlynk();
}

BLYNK_WRITE(V2) {
  blynkManualAquecimento = (param.asInt() == 1);

  if (blynkManualAquecimento) {
    definirAquecimento(2);
  } else if (!presetConfirmado) {
    definirAquecimento(0);
  }

  ultimoEnvioBlynk = 0;
}

BLYNK_WRITE(V3) {
  if (param.asInt() == 1) {
    ligarHumidificadorSeNecessario();
    cicloHumidificadorManualBlynk = true;
  }

  resetarComandosBlynk();
  ultimoEnvioBlynk = 0;
}

BLYNK_WRITE(V4) {
  if (param.asInt() == 1) {
    viragemManual();
  }

  resetarComandosBlynk();
  ultimoEnvioBlynk = 0;
}

BLYNK_WRITE(V5) {
  if (param.asInt() == 1 && !presetConfirmado && rtcOk) {
    iniciarIncubacao();
  }

  resetarComandosBlynk();
  ultimoEnvioBlynk = 0;
}

BLYNK_WRITE(V6) {
  if (param.asInt() == 1) {
    reiniciarSistema();
  }

  resetarComandosBlynk();
  ultimoEnvioBlynk = 0;
}

// ===================== WEB =====================
String estadoJson() {
  DateTime now = agoraRTC();
  String json = "{";
  json += "\"wifiLigado\":" + String(wifiLigado ? "true" : "false") + ",";
  json += "\"blynkLigado\":" + String(blynkLigado() ? "true" : "false") + ",";
  json += "\"ip\":\"" + (wifiLigado ? WiFi.localIP().toString() : String("sem_wifi")) + "\",";
  json += "\"rtcOk\":" + String(rtcOk ? "true" : "false") + ",";
  json += "\"dataHora\":\"" + formatarDataHora(now) + "\",";
  json += "\"preset\":\"" + String(presets[selectedPreset].name) + "\",";
  json += "\"presetConfirmado\":" + String(presetConfirmado ? "true" : "false") + ",";
  json += "\"temperatura\":" + (isnan(tempAtual) ? String("null") : String(tempAtual, 1)) + ",";
  json += "\"humidade\":" + (isnan(humAtual) ? String("null") : String(humAtual, 0)) + ",";
  json += "\"tempAlvo\":" + String(temperaturaAlvoAtual(), 1) + ",";
  json += "\"humAlvo\":" + String(humidadeAlvoAtual()) + ",";
  json += "\"diaAtual\":" + String(diaAtualIncubacao()) + ",";
  json += "\"duracao\":" + String(presets[selectedPreset].durationDays) + ",";
  json += "\"aquecimento\":" + String(aquecimentoLigado ? "true" : "false") + ",";
  json += "\"relesAtivos\":" + String(relesAquecimentoAtivos) + ",";
  json += "\"humidificador\":" + String(humidificadorLigado ? "true" : "false") + ",";
  json += "\"incubacaoTerminada\":" + String(incubacaoTerminada ? "true" : "false") + ",";
  json += "\"ultimosDias\":" + String(emUltimosDias() ? "true" : "false") + ",";
  json += "\"proximaViragem\":" + String(segundosProximaViragem()) + ",";
  json += "\"margemTemp\":" + String(margemTemp, 2) + ",";
  json += "\"margemHum\":" + String(margemHum);
  json += "}";
  return json;
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Chocadeira ESP32</title>
<style>
  body{font-family:Arial,sans-serif;background:#eef2f7;margin:0;padding:14px;color:#1f2937}
  .card{background:white;border-radius:16px;padding:16px;margin:0 0 14px 0;box-shadow:0 2px 10px rgba(0,0,0,.08)}
  h1,h2{margin:0 0 10px 0}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  .item{background:#f8fafc;border-radius:12px;padding:12px;text-align:center}
  .gauge-container{position:relative;width:100%;max-width:130px;margin:0 auto}
  .gauge-container svg{display:block;width:100%;height:auto}
  .gauge-label{font-size:0.85rem;margin-bottom:4px;color:#374151}
  .gauge-value{font-size:1.25rem;font-weight:bold;fill:#1f2937}
  .gauge-unit{font-size:0.7rem;fill:#64748b}
  button,select,input{width:100%;padding:12px;border-radius:10px;border:1px solid #d1d5db;font-size:16px;box-sizing:border-box}
  button{background:#2563eb;color:white;border:none;margin-top:8px}
  button.red{background:#dc2626}
  button.green{background:#059669}
  button.gray{background:#475569}
  .row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
  .ok{color:#047857;font-weight:bold}
  .off{color:#b91c1c;font-weight:bold}
  .small{font-size:.92rem;color:#475569}
  /* Transição suave para os arcos */
  .gauge-fill{transition: stroke-dashoffset 0.7s ease;}
</style>
</head>
<body>
<div class="card">
  <h1>Chocadeira ESP32</h1>
  <div>IP: <strong id="ip">-</strong></div>
  <div>Data/Hora: <strong id="dataHora">-</strong></div>
  <div>RTC: <strong id="rtcOk">-</strong></div>
</div>

<div class="card">
  <h2>Leituras</h2>
  <div class="grid">
    <!-- Temperatura -->
    <div class="item">
      <div class="gauge-label">Temperatura</div>
      <div class="gauge-container">
        <svg viewBox="0 0 100 100">
          <circle cx="50" cy="50" r="40" stroke="#e2e8f0" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="0"
                  transform="rotate(135 50 50)" stroke-linecap="round"/>
          <circle cx="50" cy="50" r="40" stroke="#2563eb" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="188.5"
                  transform="rotate(135 50 50)" stroke-linecap="round" class="gauge-fill" id="gTemp"/>
          <text x="50" y="48" text-anchor="middle" class="gauge-value" id="gTempVal">-</text>
          <text x="50" y="64" text-anchor="middle" class="gauge-unit">°C</text>
        </svg>
      </div>
    </div>
    <!-- Humidade -->
    <div class="item">
      <div class="gauge-label">Humidade</div>
      <div class="gauge-container">
        <svg viewBox="0 0 100 100">
          <circle cx="50" cy="50" r="40" stroke="#e2e8f0" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="0"
                  transform="rotate(135 50 50)" stroke-linecap="round"/>
          <circle cx="50" cy="50" r="40" stroke="#0891b2" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="188.5"
                  transform="rotate(135 50 50)" stroke-linecap="round" class="gauge-fill" id="gHum"/>
          <text x="50" y="48" text-anchor="middle" class="gauge-value" id="gHumVal">-</text>
          <text x="50" y="64" text-anchor="middle" class="gauge-unit">%</text>
        </svg>
      </div>
    </div>
    <!-- Alvo temperatura -->
    <div class="item">
      <div class="gauge-label">Alvo temp.</div>
      <div class="gauge-container">
        <svg viewBox="0 0 100 100">
          <circle cx="50" cy="50" r="40" stroke="#e2e8f0" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="0"
                  transform="rotate(135 50 50)" stroke-linecap="round"/>
          <circle cx="50" cy="50" r="40" stroke="#d97706" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="188.5"
                  transform="rotate(135 50 50)" stroke-linecap="round" class="gauge-fill" id="gTempAlvo"/>
          <text x="50" y="48" text-anchor="middle" class="gauge-value" id="gTempAlvoVal">-</text>
          <text x="50" y="64" text-anchor="middle" class="gauge-unit">°C</text>
        </svg>
      </div>
    </div>
    <!-- Alvo humidade -->
    <div class="item">
      <div class="gauge-label">Alvo hum.</div>
      <div class="gauge-container">
        <svg viewBox="0 0 100 100">
          <circle cx="50" cy="50" r="40" stroke="#e2e8f0" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="0"
                  transform="rotate(135 50 50)" stroke-linecap="round"/>
          <circle cx="50" cy="50" r="40" stroke="#7c3aed" stroke-width="8" fill="none"
                  stroke-dasharray="188.5 251.3" stroke-dashoffset="188.5"
                  transform="rotate(135 50 50)" stroke-linecap="round" class="gauge-fill" id="gHumAlvo"/>
          <text x="50" y="48" text-anchor="middle" class="gauge-value" id="gHumAlvoVal">-</text>
          <text x="50" y="64" text-anchor="middle" class="gauge-unit">%</text>
        </svg>
      </div>
    </div>
  </div>
</div>

<div class="card">
  <h2>Estado</h2>
  <div>Preset: <strong id="preset">-</strong></div>
  <div>Incubação iniciada: <strong id="presetConfirmado">-</strong></div>
  <div>Dia: <strong id="diaAtual">-</strong>/<strong id="duracao">-</strong></div>
  <div>Aquecimento: <span id="aquecimento">-</span></div>
  <div>Relés ativos: <strong id="relesAtivos">-</strong></div>
  <div>Humidificador: <span id="humidificador">-</span></div>
  <div>Últimos dias: <span id="ultimosDias">-</span></div>
  <div>Incubação terminada: <span id="incubacaoTerminada">-</span></div>
  <div>Próxima viragem: <strong id="proximaViragem">-</strong> s</div>
</div>

<div class="card">
  <h2>Controlos</h2>
  <label>Preset</label>
  <select id="presetSel">
    <option value="0">GALINHA</option>
    <option value="1">CODORNIZ</option>
    <option value="2">PATO</option>
    <option value="3">PERU</option>
    <option value="4">FAISÃO</option>
    <option value="5">PAVÃO</option>
  </select>
  <div class="row">
    <button onclick="selecionarPreset()">Escolher</button>
    <button class="green" onclick="iniciar()">Iniciar</button>
  </div>
  <div class="row">
    <button class="gray" onclick="viragemManual()">Viragem manual</button>
    <button class="red" onclick="parar()">Parar</button>
  </div>
</div>

<div class="card">
  <h2>Ajustes rápidos</h2>
  <div class="small">Estes valores ficam na RAM. Ao reiniciar o ESP32 voltam aos valores padrão do código.</div>
  <label>Margem temperatura (°C)</label>
  <input id="mTemp" type="number" step="0.01">
  <label>Margem humidade (%)</label>
  <input id="mHum" type="number" step="1">
  <button onclick="guardarAjustes()">Guardar ajustes</button>
</div>

<script>
const ARC_LENGTH = 188.5; // comprimento do arco de 270° com r=40

function txt(v){ return v ? '<span class="ok">Ligado</span>' : '<span class="off">Desligado</span>'; }
function simNao(v){ return v ? '<span class="ok">Sim</span>' : '<span class="off">Não</span>'; }
async function api(url){ const r = await fetch(url); return r.text(); }

/** Atualiza um gauge circular
 *  @param {string} idFill  - ID do <circle> de preenchimento
 *  @param {string} idText - ID do <text> do valor
 *  @param {number} value  - valor atual
 *  @param {number} min    - mínimo da escala
 *  @param {number} max    - máximo da escala
 *  @param {string} unit   - unidade (não usado no texto, apenas para fallback)
 */
function updateGauge(idFill, idText, value, min, max) {
  const fillEl = document.getElementById(idFill);
  const textEl = document.getElementById(idText);
  if (!fillEl || !textEl) return;

  // Se valor inválido
  if (value === null || value === undefined || isNaN(value)) {
    fillEl.setAttribute('stroke-dashoffset', ARC_LENGTH);
    textEl.textContent = '-';
    return;
  }

  // Limita ao intervalo
  let clamped = Math.min(max, Math.max(min, value));
  let percent = (clamped - min) / (max - min); // 0 a 1
  let offset = ARC_LENGTH * (1 - percent);

  fillEl.setAttribute('stroke-dashoffset', offset);
  // Exibe valor com uma casa decimal se necessário
  textEl.textContent = Number.isInteger(clamped) ? clamped : clamped.toFixed(1);
}

async function atualizar(){
  try {
    const r = await fetch('/status');
    const s = await r.json();

    // Cabeçalho
    document.getElementById('ip').textContent = s.ip;
    document.getElementById('dataHora').textContent = s.dataHora;
    document.getElementById('rtcOk').innerHTML = simNao(s.rtcOk);

    // Estado
    document.getElementById('preset').textContent = s.preset;
    document.getElementById('presetConfirmado').innerHTML = simNao(s.presetConfirmado);
    document.getElementById('diaAtual').textContent = s.diaAtual;
    document.getElementById('duracao').textContent = s.duracao;
    document.getElementById('relesAtivos').textContent = s.relesAtivos;
    document.getElementById('proximaViragem').textContent = s.proximaViragem;
    document.getElementById('aquecimento').innerHTML = txt(s.aquecimento);
    document.getElementById('humidificador').innerHTML = txt(s.humidificador);
    document.getElementById('ultimosDias').innerHTML = simNao(s.ultimosDias);
    document.getElementById('incubacaoTerminada').innerHTML = simNao(s.incubacaoTerminada);

    // Inputs de ajuste
    document.getElementById('mTemp').value = s.margemTemp;
    document.getElementById('mHum').value = s.margemHum;

    // Gauges
    updateGauge('gTemp', 'gTempVal', s.temperatura, 0, 50);
    updateGauge('gHum', 'gHumVal', s.humidade, 0, 100);
    updateGauge('gTempAlvo', 'gTempAlvoVal', s.tempAlvo, 0, 50);
    updateGauge('gHumAlvo', 'gHumAlvoVal', s.humAlvo, 0, 100);

  } catch (e) {
    console.error('Erro ao atualizar:', e);
  }
}

async function selecionarPreset(){
  const v = document.getElementById('presetSel').value;
  await api('/preset?i=' + v);
  atualizar();
}
async function iniciar(){ await api('/start'); atualizar(); }
async function parar(){ await api('/stop'); atualizar(); }
async function viragemManual(){ await api('/turn'); atualizar(); }
async function guardarAjustes(){
  const mt = document.getElementById('mTemp').value;
  const mh = document.getElementById('mHum').value;
  await api('/config?mt=' + encodeURIComponent(mt) + '&mh=' + encodeURIComponent(mh));
  atualizar();
}

setInterval(atualizar, 2000);
atualizar();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  server.send(200, "application/json", estadoJson());
}

void handlePreset() {
  if (!presetConfirmado && server.hasArg("i")) {
    int idx = server.arg("i").toInt();
    if (idx >= 0 && idx < NUM_PRESETS) selectedPreset = idx;
  }
  server.send(200, "text/plain", "OK");
}

void handleStart() {
  if (!presetConfirmado) iniciarIncubacao();
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  reiniciarSistema();
  server.send(200, "text/plain", "OK");
}

void handleTurn() {
  viragemManual();
  server.send(200, "text/plain", "OK");
}

void handleConfig() {
  if (server.hasArg("mt")) {
    float mt = server.arg("mt").toFloat();
    if (mt >= 0.05 && mt <= 2.0) margemTemp = mt;
  }
  if (server.hasArg("mh")) {
    int mh = server.arg("mh").toInt();
    if (mh >= 1 && mh <= 20) margemHum = mh;
  }
  server.send(200, "text/plain", "OK");
}

void configurarWeb() {
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/preset", handlePreset);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/turn", handleTurn);
  server.on("/config", handleConfig);
  server.begin();
}

void ligarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Ligando ao Wi-Fi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  wifiLigado = (WiFi.status() == WL_CONNECTED);
  if (wifiLigado) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Falha ao ligar Wi-Fi");
  }
}

void configurarBlynk() {
  if (!wifiLigado) return;

  Blynk.config(BLYNK_AUTH_TOKEN);
  blynkConfigurado = true;
  ultimoReconnectBlynk = millis();

  if (Blynk.connect(5000)) {
    Serial.println("Blynk conectado");
  } else {
    Serial.println("Blynk indisponivel no arranque");
  }
}

void manterBlynkLigado() {
  if (!wifiLigado || !blynkConfigurado || Blynk.connected()) return;
  if (millis() - ultimoReconnectBlynk < INTERVALO_RECONNECT_BLYNK_MS) return;

  ultimoReconnectBlynk = millis();
  Serial.println("A tentar religar Blynk...");
  Blynk.connect(3000);
}

void manterRTCsincronizado() {
  if (!wifiLigado || !rtcOk) return;
  if (ultimaSincronizacaoRtc != 0 &&
      millis() - ultimaSincronizacaoRtc < INTERVALO_RESYNC_RTC_MS) {
    return;
  }

  sincronizarRTCComNTP(false);
}

void enviarBlynkPeriodico() {
  if (!blynkLigado()) return;
  if (millis() - ultimoEnvioBlynk < INTERVALO_BLYNK_MS) return;

  ultimoEnvioBlynk = millis();
  atualizarEstadoBlynk();
}

// ===================== LEITURAS =====================
void atualizarSensores() {
  if (ultimoLeituraSensor != 0 &&
      millis() - ultimoLeituraSensor < INTERVALO_SENSOR_MS) return;
  ultimoLeituraSensor = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    tempAtual = t;
    humAtual = h;
  }
}

// ===================== BOTÕES =====================
void lerBotoes() {
  bool estadoNext = digitalRead(BTN_NEXT);
  bool estadoSelect = digitalRead(BTN_SELECT);
  bool estadoBack = digitalRead(BTN_BACK);

  if (!presetConfirmado) {
    if (prevNext == HIGH && estadoNext == LOW) {
      selectedPreset++;
      if (selectedPreset >= NUM_PRESETS) selectedPreset = 0;
    }
    if (prevSelect == HIGH && estadoSelect == LOW) {
      iniciarIncubacao();
    }
  }

  if (prevBack == HIGH && estadoBack == LOW) {
    reiniciarSistema();
  }

  prevNext = estadoNext;
  prevSelect = estadoSelect;
  prevBack = estadoBack;
}

void aplicarControlosManuaisBlynk() {
  if (presetConfirmado || incubacaoTerminada) return;

  if (blynkManualAquecimento) {
    definirAquecimento(2);
  } else {
    definirAquecimento(0);
  }
}

// ===================== CONTROLO PRINCIPAL =====================
void controlarIncubacao() {
  if (!presetConfirmado || !rtcOk) return;
  if (isnan(tempAtual) || isnan(humAtual)) return;

  DateTime now = rtc.now();
  int diaAtual = (now.unixtime() - startDate.unixtime()) / 86400 + 1;
  int duracaoTotal = presets[selectedPreset].durationDays;

  if (diaAtual > duracaoTotal) {
    diaAtual = duracaoTotal;
    incubacaoTerminada = true;
  } else {
    incubacaoTerminada = false;
  }

  int humAlvo = presets[selectedPreset].targetHum;
  bool ultimosDias = (diaAtual > duracaoTotal - LAST_DAYS);
  if (ultimosDias) humAlvo += HUMIDITY_INCREASE;

  // ===== HUMIDIFICADOR =====
  if (!cicloHumidificadorManualBlynk) {
    if (humAtual < humAlvo - margemHum) {
      if (!humidificadorLigado && (ultimoCicloHumidade == 0 || millis() - ultimoCicloHumidade >= INTERVALO_MIN_HUM_MS)) {
        ligarHumidificadorSeNecessario();
      }
    }

    if (humAtual > humAlvo + margemHum) {
      desligarHumidificadorSeNecessario();
    }
  }

  // ===== AQUECIMENTO =====
  float tempAlvo = presets[selectedPreset].targetTemp;
  float tempLigaPrincipal = tempAlvo - margemTemp;
  float tempLigaApoio = tempAlvo - (margemTemp * 2.0f);
  float tempMax = tempAlvo + margemTemp;

  if (blynkManualAquecimento) {
    definirAquecimento(2);
  } else if (tempAtual <= tempLigaApoio) {
    definirAquecimento(2);
  } else if (tempAtual <= tempLigaPrincipal) {
    definirAquecimento(1);
  } else if (tempAtual >= tempMax) {
    definirAquecimento(0);
  }

  // ===== VIRAGEM =====
  if (!incubacaoTerminada && !ultimosDias) {
    unsigned long agora = now.unixtime();
    if (agora - ultimaViragemUnix >= intervaloViragem) {
      servoMotor.write(110);
      delay(2000);
      servoMotor.write(90);
      ultimaViragemUnix = agora;
    }
  }

  // ===== FIM =====
  if (incubacaoTerminada) {
    blynkManualAquecimento = false;
    definirAquecimento(0);
    desligarHumidificadorSeNecessario();

    if (millis() - ultimoBuzzerFim >= BUZZER_INTERVALO_MS) {
      beep(1000, 500);
      ultimoBuzzerFim = millis();
    }
  }
}

// ===================== LCD =====================
void atualizarLCD() {
  if (millis() - ultimoLCD < INTERVALO_LCD_MS) return;
  ultimoLCD = millis();

  if (!presetConfirmado) {
    lcd.setCursor(0, 0);
    lcd.print("SELECIONE      ");
    lcd.setCursor(0, 1);
    lcd.print("> ");
    lcd.print(presets[selectedPreset].name);
    lcd.print("          ");
    return;
  }

  if (!rtcOk) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ERRO RTC");
    lcd.setCursor(0, 1);
    lcd.print("VERIFIQUE");
    return;
  }

  if (isnan(tempAtual) || isnan(humAtual)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ERRO SENSOR");
    lcd.setCursor(0, 1);
    lcd.print("DHT22");
    return;
  }

  DateTime now = rtc.now();
  int diaAtual = diaAtualIncubacao();
  int duracao = presets[selectedPreset].durationDays;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("D");
  lcd.print(diaAtual);
  lcd.print("/");
  lcd.print(duracao);
  lcd.print(" ");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());

  lcd.setCursor(0, 1);
  lcd.print(tempAtual, 1);
  lcd.print("C ");
  lcd.print(humAtual, 0);
  lcd.print("%");
  lcd.print(humidificadorLigado ? "*" : " ");
  lcd.print(aquecimentoLigado ? "H" : " ");
}

// ===================== SERIAL =====================
void printStatusSerial() {
  if (millis() - ultimoPrintSerial < INTERVALO_SERIAL_MS) return;
  ultimoPrintSerial = millis();

  Serial.println("--- STATUS ATUAL ---");
  Serial.print("Preset: ");
  Serial.println(presets[selectedPreset].name);
  Serial.print("RTC: ");
  Serial.println(rtcOk ? "OK" : "ERRO");
  if (rtcOk) {
    Serial.print("Agora: ");
    Serial.println(formatarDataHora(rtc.now()));
  }
  Serial.print("Dia: ");
  Serial.print(diaAtualIncubacao());
  Serial.print("/");
  Serial.println(presets[selectedPreset].durationDays);
  Serial.print("Temperatura: ");
  Serial.println(isnan(tempAtual) ? -999 : tempAtual);
  Serial.print("Humidade: ");
  Serial.println(isnan(humAtual) ? -999 : humAtual);
  Serial.print("Temp alvo: ");
  Serial.println(temperaturaAlvoAtual());
  Serial.print("Hum alvo: ");
  Serial.println(humidadeAlvoAtual());
  Serial.print("Aquecimento (relés): ");
  Serial.println(relesAquecimentoAtivos);
  Serial.print("Relay 33 principal: ");
  Serial.println(relesAquecimentoAtivos >= 1 ? "LIGAR" : "DESLIGAR");
  Serial.print("Relay 27 apoio: ");
  Serial.println(relesAquecimentoAtivos >= 2 ? "LIGAR" : "DESLIGAR");
  Serial.print("Humidificador: ");
  Serial.println(humidificadorLigado ? "LIGADO" : "DESLIGADO");
  Serial.print("Próxima viragem (s): ");
  Serial.println(segundosProximaViragem());
  Serial.println("--------------------");
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  inicializarSaidasCriticas();
  Wire.begin(I2C_SDA, I2C_SCL);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  servoMotor.setPeriodHertz(50);
  servoMotor.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoMotor.write(90);

  rtcOk = rtc.begin();
  if (!rtcOk) {
    Serial.println("RTC DS3231 não encontrado");
  }

  ligarWiFi();
  garantirHoraValidaRTC();
  configurarWeb();
  configurarBlynk();
  reiniciarSistema();

  if (wifiLigado) {
    Serial.println("Abra no telemóvel o IP mostrado acima.");
  }
}
// ===================== LOOP =====================
void loop() {
  if (wifiLigado && blynkConfigurado) {
    Blynk.run();
  }

  server.handleClient();
  manterBlynkLigado();
  manterRTCsincronizado();
  gerirPulsoHumidificador();
  lerBotoes();
  aplicarControlosManuaisBlynk();
  atualizarSensores();
  controlarIncubacao();
  atualizarLCD();
  enviarBlynkPeriodico();
  printStatusSerial();
  delay(20);
}
