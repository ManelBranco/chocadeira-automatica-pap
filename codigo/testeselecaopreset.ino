#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== DHT =====
#define DHTPIN A0
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ===== BOTÕES =====
#define BTN_NEXT   2
#define BTN_SELECT 3

// ===== PRESETS =====
struct Preset {
  const char* name;
  float targetTemp;
  int targetHum;
};

Preset presets[] = {
  {"GALINHA", 37.6, 55},
  {"CODORNIZ", 37.5, 55},
  {"PATO", 37.8, 60},
  {"PERU", 37.2, 55},
  {"FAISAO", 37.5, 55},
  {"PAVAO", 37.5, 55}
};

const int NUM_PRESETS = sizeof(presets) / sizeof(presets[0]);

// ===== ESTADO =====
int selectedPreset = 0;
bool presetConfirmado = false;

float tempAtual = 0;
float humAtual  = 0;

unsigned long lastInfoMillis = 0;
bool mostrarIdeal = false;

void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  lcd.setCursor(0,0);
  lcd.print("SELECIONE");
  lcd.setCursor(0,1);
  lcd.print("PRESET");
}

void loop() {

  // ===== MENU PRESETS =====
  if (!presetConfirmado) {

    lcd.setCursor(0,1);
    lcd.print("> ");
    lcd.print(presets[selectedPreset].name);
    lcd.print("        ");

    if (digitalRead(BTN_NEXT) == LOW) {
      Serial.println("BTN_NEXT");
      selectedPreset = (selectedPreset + 1) % NUM_PRESETS;
      delay(250);
      while(digitalRead(BTN_NEXT) == LOW);
    }

    if (digitalRead(BTN_SELECT) == LOW) {
      Serial.print("PRESET SELECIONADO: ");
      Serial.println(presets[selectedPreset].name);

      presetConfirmado = true;
      lcd.clear();
      delay(300);
      while(digitalRead(BTN_SELECT) == LOW);
    }

    return;
  }

  // ===== LEITURA SENSOR =====
  tempAtual = dht.readTemperature();
  humAtual  = dht.readHumidity();

  if (isnan(tempAtual) || isnan(humAtual)) {
    lcd.setCursor(0,0);
    lcd.print("ERRO SENSOR   ");
    return;
  }

  // ===== MOSTRAR IDEAL DE X EM X TEMPO =====
  if (millis() - lastInfoMillis > 8000) {
    mostrarIdeal = !mostrarIdeal;
    lastInfoMillis = millis();
  }

  lcd.clear();

  if (mostrarIdeal) {
    lcd.setCursor(0,0);
    lcd.print("IDEAL ");
    lcd.print(presets[selectedPreset].name);
    lcd.setCursor(0,1);
    lcd.print(presets[selectedPreset].targetTemp,1);
    lcd.print("C ");
    lcd.print(presets[selectedPreset].targetHum);
    lcd.print("%");
  } else {
    lcd.setCursor(0,0);
    lcd.print("TEMP / HUM");
    lcd.setCursor(0,1);
    lcd.print(tempAtual,1);
    lcd.print("C ");
    lcd.print(humAtual,0);
    lcd.print("%");
  }

  delay(1000);
}
