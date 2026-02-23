#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Servo.h>
#include <RTClib.h>

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== RTC =====
RTC_DS3231 rtc;

// ===== DHT =====
#define DHTPIN A15
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
#define HEATER_RELAY   7   // active HIGH

// ===== BUZZER =====
#define BUZZER_PIN 8        // pino do buzzer

// ===== CONFIGURAÇÃO TESTE =====
#define DURACAO_DIA_MS 30000UL  // 30 segundos por dia simulado
#define INTERVALO_SERVO_MS 5000  // servo a cada 5 segundos

// ===== PRESETS =====
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

const int NUM_PRESETS = sizeof(presets) / sizeof(presets[0]);
const int LAST_DAYS = 3;               // últimos 3 dias com humidade aumentada e sem viragem
const int HUMIDITY_INCREASE = 10;      // aumento de humidade nos últimos dias

// ===== ESTADO =====
int selectedPreset = 0;
bool presetConfirmado = false;

float tempAtual = 0;
float humAtual  = 0;

int diaAtual = 1;
bool ultimosDias = false;
bool incubacaoTerminada = false;

bool aquecimentoLigado = false;
bool humidificadorLigado = false;

unsigned long startMillis = 0;          // millis no início da incubação
unsigned long lastDayMillis = 0;        // última atualização de dia
unsigned long lastServoMillis = 0;      // última movimentação do servo
unsigned long lastBuzzerMillis = 0;     // controle do buzzer
bool buzzerAtivo = false;

void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  pinMode(BTN_NEXT, INPUT);
  pinMode(BTN_SELECT, INPUT);
  pinMode(BTN_BACK, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  servoMotor.attach(SERVO_PIN);
  servoMotor.write(90);

  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(HEATER_RELAY, OUTPUT);
  digitalWrite(HUMIDIFIER_PIN, LOW);
  digitalWrite(HEATER_RELAY, LOW);

  rtc.begin();
  // Se necessário, ajuste a hora descomentando a linha abaixo (executar apenas uma vez)
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  lcd.setCursor(0, 0);
  lcd.print("SELECIONE");
  lcd.setCursor(0, 1);
  lcd.print("PRESET");
}

void loop() {
  // ===== SELEÇÃO DE PRESET =====
  if (!presetConfirmado) {
    lcd.setCursor(0, 1);
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
      incubacaoTerminada = false;
      diaAtual = 1;
      startMillis = millis();
      lastDayMillis = startMillis;
      lastServoMillis = startMillis;
      lcd.clear();
      delay(300);
      while (digitalRead(BTN_SELECT) == HIGH);
    }
    return;
  }

  // ===== BOTÃO BACK =====
  if (digitalRead(BTN_BACK) == HIGH) {
    presetConfirmado = false;
    incubacaoTerminada = false;
    buzzerAtivo = false;
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(HUMIDIFIER_PIN, LOW);
    digitalWrite(HEATER_RELAY, LOW);
    aquecimentoLigado = false;
    humidificadorLigado = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SELECIONE");
    lcd.setCursor(0, 1);
    lcd.print("PRESET");
    delay(300);
    while (digitalRead(BTN_BACK) == HIGH);
    return;
  }

  // ===== ATUALIZAÇÃO DO DIA SIMULADO =====
  if (millis() - lastDayMillis >= DURACAO_DIA_MS) {
    diaAtual++;
    lastDayMillis = millis();
  }

  int duracaoTotal = presets[selectedPreset].durationDays;
  
  // Verifica se a incubação terminou
  if (diaAtual > duracaoTotal) {
    diaAtual = duracaoTotal;
    incubacaoTerminada = true;
  } else {
    incubacaoTerminada = false;
  }
  
  ultimosDias = (diaAtual > duracaoTotal - LAST_DAYS);

  // ===== LEITURA DO SENSOR =====
  tempAtual = dht.readTemperature();
  humAtual = dht.readHumidity();

  if (isnan(tempAtual) || isnan(humAtual)) {
    lcd.setCursor(0, 0);
    lcd.print("ERRO SENSOR    ");
    return;
  }

  // ===== DEFINIÇÃO DOS ALVOS =====
  int humAlvo = presets[selectedPreset].targetHum;
  if (ultimosDias) humAlvo += HUMIDITY_INCREASE;

  float tempAlvo = presets[selectedPreset].targetTemp;
  float tempMin = tempAlvo - 0.2;
  float tempMax = tempAlvo + 0.2;

  // ===== CONTROLE DO HUMIDIFICADOR (apenas se não terminou) =====
  if (!incubacaoTerminada) {
    if (humAtual < humAlvo) {
      digitalWrite(HUMIDIFIER_PIN, HIGH);
      humidificadorLigado = true;
    } else {
      digitalWrite(HUMIDIFIER_PIN, LOW);
      humidificadorLigado = false;
    }
  } else {
    digitalWrite(HUMIDIFIER_PIN, LOW);
    humidificadorLigado = false;
  }

  // ===== CONTROLE DO AQUECIMENTO (apenas se não terminou) =====
  if (!incubacaoTerminada) {
    if (tempAtual < tempMin && !aquecimentoLigado) {
      digitalWrite(HEATER_RELAY, HIGH);
      aquecimentoLigado = true;
    } else if (tempAtual > tempMax && aquecimentoLigado) {
      digitalWrite(HEATER_RELAY, LOW);
      aquecimentoLigado = false;
    }
  } else {
    digitalWrite(HEATER_RELAY, LOW);
    aquecimentoLigado = false;
  }

  // ===== MOVIMENTO DO SERVO (a cada 5s, apenas se não terminou e fora dos últimos dias) =====
  if (!incubacaoTerminada && !ultimosDias && (millis() - lastServoMillis >= INTERVALO_SERVO_MS)) {
    servoMotor.write(110);
    delay(2000);
    servoMotor.write(90);
    lastServoMillis = millis();
  }

  // ===== BUZZER NO FIM DA INCUBAÇÃO =====
  if (incubacaoTerminada && !buzzerAtivo) {
    tone(BUZZER_PIN, 1000, 5000);  // 1000Hz por 5 segundos
    buzzerAtivo = true;
    lastBuzzerMillis = millis();
  }
  
  // Se o buzzer já tocou, espera 10 segundos antes de permitir tocar novamente
  if (buzzerAtivo && (millis() - lastBuzzerMillis > 10000)) {
    buzzerAtivo = false;
  }

  // ===== LEITURA DA HORA REAL DO RTC =====
  DateTime now = rtc.now();

  // ===== ATUALIZAÇÃO DO LCD =====
  lcd.clear();

  // Linha 0: Dia simulado e hora real
  lcd.setCursor(0, 0);
  lcd.print("D");
  lcd.print(diaAtual);
  lcd.print("/");
  lcd.print(duracaoTotal);
  lcd.print(" ");
  lcd.print(now.hour());
  lcd.print(":");
  if (now.minute() < 10) lcd.print("0");
  lcd.print(now.minute());

  // Indicação de últimos dias ou fim
  if (incubacaoTerminada) {
    lcd.setCursor(12, 0);
    lcd.print("FIM");
  } else if (ultimosDias) {
    lcd.setCursor(13, 0);
    lcd.print("FIM");
  }

  // Linha 1: Temperatura e humidade com indicadores
  lcd.setCursor(0, 1);
  lcd.print(tempAtual, 1);
  lcd.print("C ");
  lcd.print(humAtual, 0);
  lcd.print("%");

  if (humidificadorLigado) lcd.print("*");
  else lcd.print(" ");

  if (aquecimentoLigado) lcd.print("H");
  else lcd.print(" ");

  delay(1000);
}