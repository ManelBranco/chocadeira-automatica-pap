#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Servo.h>

// ====== CONFIGURAÇÃO TESTE ======
#define MODO_TESTE true   // true = 1 dia = 1 min | false = 24h real

unsigned long DURACAO_DIA = MODO_TESTE ? 60000UL : 86400000UL;

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== DHT =====
#define DHTPIN A0
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ===== BOTÕES =====
#define BTN_NEXT   2
#define BTN_SELECT 3
#define BTN_BACK   4

// ===== SERVO =====
#define SERVO_PIN 5
Servo servoMotor;

// ===== RELÉS =====
#define HUMIDIFIER_PIN 6
#define HEATER_RELAY   7   // active HIGH (HIGH liga, LOW desliga)

// ===== PRESETS =====
struct Preset {
  const char* name;
  float targetTemp;
  int targetHum;
  int durationDays;
};

Preset presets[] = {
  {"GALINHA", 37.6, 55, 21},
  {"CODORNIZ", 37.6, 55, 17},
  {"PATO", 37.8, 60, 28},
  {"PERU", 37.5, 65, 28},
  {"FAISAO", 37.5, 55, 25},
  {"PAVAO", 37.5, 55, 30}
};

int viragensPorDia[] = {4, 3, 6, 4, 4, 4};

const int NUM_PRESETS = sizeof(presets) / sizeof(presets[0]);

// ===== PARÂMETROS =====
const int LAST_DAYS = 3;
const int HUMIDITY_INCREASE = 10;

// ===== ESTADO =====
int selectedPreset = 0;
bool presetConfirmado = false;

float tempAtual = 0;
float humAtual  = 0;

int diaAtual = 1;

bool aquecimentoLigado = false;
bool humidificadorLigado = false;

unsigned long lastDayMillis = 0;
unsigned long ultimaViragemMillis = 0;
unsigned long intervaloViragem = 0;

void setup() {

  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  pinMode(BTN_NEXT, INPUT);
  pinMode(BTN_SELECT, INPUT);
  pinMode(BTN_BACK, INPUT);

  servoMotor.attach(SERVO_PIN);
  servoMotor.write(90);

  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(HEATER_RELAY, OUTPUT);

  digitalWrite(HUMIDIFIER_PIN, LOW);
  digitalWrite(HEATER_RELAY, LOW); // começa desligado (active HIGH)

  lcd.setCursor(0,0);
  lcd.print("SELECIONE");
  lcd.setCursor(0,1);
  lcd.print("PRESET");
}

void loop() {

  // ===== SELEÇÃO PRESET =====
  if (!presetConfirmado) {

    lcd.setCursor(0,1);
    lcd.print("> ");
    lcd.print(presets[selectedPreset].name);
    lcd.print("        ");

    if (digitalRead(BTN_NEXT) == HIGH) {
      selectedPreset = (selectedPreset + 1) % NUM_PRESETS;
      delay(250);
      while (digitalRead(BTN_NEXT) == HIGH);
    }

    if (digitalRead(BTN_SELECT) == HIGH) {
      presetConfirmado = true;
      diaAtual = 1;
      lastDayMillis = millis();
      ultimaViragemMillis = millis();
      intervaloViragem = DURACAO_DIA / viragensPorDia[selectedPreset];
      lcd.clear();
      delay(300);
      while (digitalRead(BTN_SELECT) == HIGH);
    }
    return;
  }

  // ===== BOTÃO BACK =====
  if (digitalRead(BTN_BACK) == HIGH) {

    presetConfirmado = false;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("SELECIONE");
    lcd.setCursor(0,1);
    lcd.print("PRESET");

    digitalWrite(HUMIDIFIER_PIN, LOW);
    digitalWrite(HEATER_RELAY, LOW); // desliga aquecimento

    delay(300);
    while (digitalRead(BTN_BACK) == HIGH);
    return;
  }

  // ===== CONTADOR DE DIAS =====
  if (millis() - lastDayMillis >= DURACAO_DIA) {

    diaAtual++;
    lastDayMillis = millis();

    if (diaAtual > presets[selectedPreset].durationDays) {
      diaAtual = presets[selectedPreset].durationDays;
    }
  }

  // ===== LEITURA SENSOR =====
  tempAtual = dht.readTemperature();
  humAtual  = dht.readHumidity();

  if (isnan(tempAtual) || isnan(humAtual)) {
    lcd.setCursor(0,0);
    lcd.print("ERRO SENSOR   ");
    return;
  }

  int duracao = presets[selectedPreset].durationDays;
  bool ultimosDias = (diaAtual > (duracao - LAST_DAYS));

  // ===== HUMIDADE =====
  int humAlvo = presets[selectedPreset].targetHum;
  if (ultimosDias) humAlvo += HUMIDITY_INCREASE;

  if (humAtual < humAlvo) {
    digitalWrite(HUMIDIFIER_PIN, HIGH);
    humidificadorLigado = true;
  } else {
    digitalWrite(HUMIDIFIER_PIN, LOW);
    humidificadorLigado = false;
  }

  // ===== AQUECIMENTO =====
  float tempAlvo = presets[selectedPreset].targetTemp;
  float tempMin = tempAlvo - 0.2;
  float tempMax = tempAlvo + 0.2;

  if (tempAtual < tempMin && !aquecimentoLigado) {
    digitalWrite(HEATER_RELAY, HIGH); // liga (active HIGH)
    aquecimentoLigado = true;
  }
  else if (tempAtual > tempMax && aquecimentoLigado) {
    digitalWrite(HEATER_RELAY, LOW);  // desliga
    aquecimentoLigado = false;
  }

  // ===== VIRAGENS =====
  if (!ultimosDias) {

    if (millis() - ultimaViragemMillis >= intervaloViragem) {

      servoMotor.write(110);
      delay(2000);
      servoMotor.write(90);

      ultimaViragemMillis = millis();
    }
  }

  // ===== LCD =====
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("DIA ");
  lcd.print(diaAtual);
  lcd.print("/");
  lcd.print(duracao);

  if (ultimosDias) {
    lcd.setCursor(13,0);
    lcd.print("FIM");
  }

  lcd.setCursor(0,1);
  lcd.print(tempAtual,1);
  lcd.print("C ");
  lcd.print(humAtual,0);
  lcd.print("%");

  if (humidificadorLigado) lcd.print("*");
  else lcd.print(" ");

  if (aquecimentoLigado) lcd.print("H");
  else lcd.print(" ");

  delay(1000);
}