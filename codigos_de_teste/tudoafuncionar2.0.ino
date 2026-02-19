#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Servo.h>

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

// ===== HUMIDIFICADOR =====
#define HUMIDIFIER_PIN 6    // Pino para controlar o humidificador (relé)

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

// ===== PARÂMETROS DE INCUBAÇÃO =====
const int LAST_DAYS = 3;               // Nº de dias finais para alterar comportamento
const int HUMIDITY_INCREASE = 10;     // Aumento da humidade nos últimos dias (%)

// ===== ESTADO =====
int selectedPreset = 0;
bool presetConfirmado = false;

float tempAtual = 0;
float humAtual  = 0;

unsigned long lastInfoMillis = 0;
bool mostrarIdeal = false;

// ===== CONTROLO SERVO =====
unsigned long lastServoMillis = 0;

// ===== CONTROLO DIAS =====
unsigned long lastDayMillis = 0;
int diaAtual = 1;

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
  digitalWrite(HUMIDIFIER_PIN, LOW);   // Inicia desligado

  lcd.setCursor(0, 0);
  lcd.print("SELECIONE");
  lcd.setCursor(0, 1);
  lcd.print("PRESET");
}

void loop() {
  // --- SELEÇÃO DE PRESET ---
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
      diaAtual = 1;
      lastDayMillis = millis();
      lcd.clear();
      delay(300);
      while (digitalRead(BTN_SELECT) == HIGH);
    }
    return;
  }

  // --- BOTÃO BACK (voltar à seleção) ---
  if (digitalRead(BTN_BACK) == HIGH) {
    presetConfirmado = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SELECIONE");
    lcd.setCursor(0, 1);
    lcd.print("PRESET");
    digitalWrite(HUMIDIFIER_PIN, LOW);   // Desliga humidificador
    delay(300);
    while (digitalRead(BTN_BACK) == HIGH);
    return;
  }

  // --- CONTADOR DE DIAS (TESTE: 10s = 1 dia) ---
  if (millis() - lastDayMillis > 10000) {
    diaAtual++;
    lastDayMillis = millis();
    if (diaAtual > presets[selectedPreset].durationDays) {
      diaAtual = presets[selectedPreset].durationDays;
    }
  }

  // --- LEITURA DO SENSOR ---
  tempAtual = dht.readTemperature();
  humAtual  = dht.readHumidity();

  if (isnan(tempAtual) || isnan(humAtual)) {
    lcd.setCursor(0, 0);
    lcd.print("ERRO SENSOR   ");
    return;
  }

  // --- DEFINIÇÃO DA HUMIDADE ALVO (aumenta nos últimos 3 dias) ---
  int duracao = presets[selectedPreset].durationDays;
  int humAlvo = presets[selectedPreset].targetHum;
  bool ultimosDias = (diaAtual > (duracao - LAST_DAYS));

  if (ultimosDias) {
    humAlvo += HUMIDITY_INCREASE;   // Humidade mais alta na reta final
  }

  // --- CONTROLO DO HUMIDIFICADOR (liga se humidade < alvo) ---
  bool humidificadorLigado = false;
  if (humAtual < humAlvo) {
    digitalWrite(HUMIDIFIER_PIN, HIGH);
    humidificadorLigado = true;
  } else {
    digitalWrite(HUMIDIFIER_PIN, LOW);
  }

  // --- CONTROLO DO SERVO (apenas se NÃO estiver nos últimos 3 dias) ---
  if (!ultimosDias) {
    if (millis() - lastServoMillis > 10000) {
      servoMotor.write(110);
      delay(2000);
      servoMotor.write(90);
      lastServoMillis = millis();
    }
  }

  // --- ATUALIZAÇÃO DO LCD ---
  lcd.clear();

  // Linha 0: DIA X/Y
  lcd.setCursor(0, 0);
  lcd.print("DIA ");
  lcd.print(diaAtual);
  lcd.print("/");
  lcd.print(duracao);

  // Linha 1: Temperatura e Humidade
  lcd.setCursor(0, 1);
  lcd.print(tempAtual, 1);
  lcd.print("C ");
  lcd.print(humAtual, 0);
  lcd.print("%");

  // Indicador visual de humidificador ativo
  if (humidificadorLigado) {
    lcd.print("*");   // Mostra * quando o humidificador está a trabalhar
  } else {
    lcd.print(" ");
  }

  // Pequeno aviso se estiver nos últimos dias (opcional)
  if (ultimosDias) {
    lcd.setCursor(13, 0);
    lcd.print("FIM");
  }

  delay(1000);
}