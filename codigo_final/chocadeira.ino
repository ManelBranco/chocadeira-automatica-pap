#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <RTClib.h>

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

*/

// ===================== WIFI =====================
const char* WIFI_SSID = "Pixel";
const char* WIFI_PASS = "Ricardo200815";

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
// Mudar para true se o módulo de relé for ativo em LOW
const bool RELAY_ACTIVE_LOW = true;

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

int margemHum = 3;
float margemTemp = 0.25;

const unsigned long DURACAO_PULSO_HUM_MS = 8000;
const unsigned long INTERVALO_MIN_HUM_MS = 20000;
const unsigned long INTERVALO_SENSOR_MS = 2000;
const unsigned long INTERVALO_LCD_MS = 1000;
const unsigned long INTERVALO_SERIAL_MS = 30000;
const unsigned long BUZZER_INTERVALO_MS = 30000;

// ===================== FUNÇÕES BASE =====================
void escreverSaidaLogica(int pin, bool ativo, bool activeLow) {
  digitalWrite(pin, (ativo ^ activeLow) ? HIGH : LOW);
}

void setRelayPin(int pin, bool ligado) {
  escreverSaidaLogica(pin, ligado, RELAY_ACTIVE_LOW);
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
    Serial.println("HUMIDIFICADOR DESLIGADO");
  }
}

void definirAquecimento(int relesAtivos) {
  if (relesAtivos < 0) relesAtivos = 0;
  if (relesAtivos > 2) relesAtivos = 2;

  setRelayPin(HEATER_RELAY_1, relesAtivos >= 1);
  setRelayPin(HEATER_RELAY_2, relesAtivos >= 2);

  relesAquecimentoAtivos = relesAtivos;
  aquecimentoLigado = (relesAtivos > 0);
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

// ===================== TEMPO / ESTADO =====================

DateTime agoraRTC() {
  if (rtcOk) return rtc.now();
  return DateTime(2025, 1, 1, 0, 0, 0); // fallback só para evitar crash visual
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
  desligarHumidificadorSeNecessario();
  definirAquecimento(0);
  humidificadorLigado = false;
  aquecimentoLigado = false;
  relesAquecimentoAtivos = 0;
  inicioPulsoHumidificador = 0;
  ultimoCicloHumidade = 0;

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
  humidificadorLigado = false;
  aquecimentoLigado = false;
  relesAquecimentoAtivos = 0;

  startDate = rtc.now();
  ultimaViragemUnix = startDate.unixtime();
  intervaloViragem = 86400 / viragensPorDia[selectedPreset];
  inicioPulsoHumidificador = 0;
  ultimoCicloHumidade = millis() - INTERVALO_MIN_HUM_MS;
  ultimoPrintSerial = 0;

  lcd.clear();
}

void viragemManual() {
  servoMotor.write(110);
  delay(2000);
  servoMotor.write(90);
  if (presetConfirmado && rtcOk) ultimaViragemUnix = rtc.now().unixtime();
}

// ===================== WEB =====================
String estadoJson() {
  DateTime now = agoraRTC();
  String json = "{";
  json += "\"wifiLigado\":" + String(wifiLigado ? "true" : "false") + ",";
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
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.item{background:#f8fafc;border-radius:12px;padding:12px}
.big{font-size:1.45rem;font-weight:bold}
button,select,input{width:100%;padding:12px;border-radius:10px;border:1px solid #d1d5db;font-size:16px;box-sizing:border-box}
button{background:#2563eb;color:white;border:none;margin-top:8px}
button.red{background:#dc2626}
button.green{background:#059669}
button.gray{background:#475569}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.ok{color:#047857;font-weight:bold}
.off{color:#b91c1c;font-weight:bold}
.small{font-size:.92rem;color:#475569}
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
    <div class="item"><div>Temperatura</div><div class="big"><span id="temperatura">-</span> °C</div></div>
    <div class="item"><div>Humidade</div><div class="big"><span id="humidade">-</span> %</div></div>
    <div class="item"><div>Alvo temperatura</div><div class="big"><span id="tempAlvo">-</span> °C</div></div>
    <div class="item"><div>Alvo humidade</div><div class="big"><span id="humAlvo">-</span> %</div></div>
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
function txt(v){return v ? '<span class="ok">Ligado</span>' : '<span class="off">Desligado</span>';}
function simNao(v){return v ? '<span class="ok">Sim</span>' : '<span class="off">Não</span>';}
async function api(url){const r=await fetch(url); return r.text();}
async function atualizar(){
  const r = await fetch('/status');
  const s = await r.json();
  document.getElementById('ip').textContent = s.ip;
  document.getElementById('dataHora').textContent = s.dataHora;
  document.getElementById('rtcOk').innerHTML = simNao(s.rtcOk);
  document.getElementById('preset').textContent = s.preset;
  document.getElementById('presetConfirmado').innerHTML = simNao(s.presetConfirmado);
  document.getElementById('temperatura').textContent = s.temperatura ?? '-';
  document.getElementById('humidade').textContent = s.humidade ?? '-';
  document.getElementById('tempAlvo').textContent = s.tempAlvo;
  document.getElementById('humAlvo').textContent = s.humAlvo;
  document.getElementById('diaAtual').textContent = s.diaAtual;
  document.getElementById('duracao').textContent = s.duracao;
  document.getElementById('relesAtivos').textContent = s.relesAtivos;
  document.getElementById('proximaViragem').textContent = s.proximaViragem;
  document.getElementById('aquecimento').innerHTML = txt(s.aquecimento);
  document.getElementById('humidificador').innerHTML = txt(s.humidificador);
  document.getElementById('ultimosDias').innerHTML = simNao(s.ultimosDias);
  document.getElementById('incubacaoTerminada').innerHTML = simNao(s.incubacaoTerminada);
  document.getElementById('mTemp').value = s.margemTemp;
  document.getElementById('mHum').value = s.margemHum;
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

// ===================== LEITURAS =====================
void atualizarSensores() {
  if (millis() - ultimoLeituraSensor < INTERVALO_SENSOR_MS) return;
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
  if (humidificadorLigado && millis() - inicioPulsoHumidificador >= DURACAO_PULSO_HUM_MS) {
    desligarHumidificadorSeNecessario();
  }

  if (humAtual < humAlvo - margemHum) {
    if (!humidificadorLigado && (ultimoCicloHumidade == 0 || millis() - ultimoCicloHumidade >= INTERVALO_MIN_HUM_MS)) {
      ligarHumidificadorSeNecessario();
    }
  }

  if (humAtual > humAlvo + margemHum) {
    desligarHumidificadorSeNecessario();
  }

  // ===== AQUECIMENTO =====
  float tempAlvo = presets[selectedPreset].targetTemp;
  float tempLiga1 = tempAlvo - margemTemp;
  float tempLiga2 = tempAlvo - (margemTemp * 2.0f);
  float tempMax = tempAlvo + margemTemp;

  if (tempAtual < tempLiga2) {
    definirAquecimento(2);
  } else if (tempAtual < tempLiga1) {
    definirAquecimento(1);
  } else if (tempAtual > tempMax) {
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
  Serial.print("Humidificador: ");
  Serial.println(humidificadorLigado ? "LIGADO" : "DESLIGADO");
  Serial.print("Próxima viragem (s): ");
  Serial.println(segundosProximaViragem());
  Serial.println("--------------------");
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(HEATER_RELAY_1, OUTPUT);
  pinMode(HEATER_RELAY_2, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  inicializarSaidaHumidificadorIdle();
  setRelayPin(HEATER_RELAY_1, false);
  setRelayPin(HEATER_RELAY_2, false);

  servoMotor.setPeriodHertz(50);
  servoMotor.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoMotor.write(90);

  rtcOk = rtc.begin();
  if (!rtcOk) {
    Serial.println("RTC DS3231 não encontrado");
  }

  ligarWiFi();
  configurarWeb();
  reiniciarSistema();

  if (wifiLigado) {
    Serial.println("Abra no telemóvel o IP mostrado acima.");
  }
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();
  lerBotoes();
  atualizarSensores();
  controlarIncubacao();
  atualizarLCD();
  printStatusSerial();
  delay(20);
}

