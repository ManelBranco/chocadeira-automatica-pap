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
bool prevNext   = HIGH;
bool prevSelect = HIGH;
bool prevBack   = HIGH;
// ===== SERVO =====
#define SERVO_PIN 5
Servo servoMotor;

// ===== RELÉS =====
#define HUMIDIFIER_PIN 6
#define HEATER_RELAY   7   // active HIGH
#define BUZZER         8   // movido para pino 8

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

// Número de viragens por dia para cada preset (mesma ordem dos presets)
int viragensPorDia[] = {4, 3, 6, 4, 4, 4};

const int NUM_PRESETS = sizeof(presets) / sizeof(presets[0]);
const int LAST_DAYS = 3;               // últimos 3 dias com humidade aumentada e sem viragem
const int HUMIDITY_INCREASE = 10;      // aumento de humidade nos últimos dias

// ===== ESTADO =====
int selectedPreset = 0;
bool presetConfirmado = false;
bool incubacaoTerminada = false;

DateTime startDate;

float tempAtual = 0;
float humAtual  = 0;

bool aquecimentoLigado = false;
bool humidificadorLigado = false;

unsigned long ultimaViragemUnix = 0;   // timestamp da última viragem (segundos)
unsigned long intervaloViragem = 0;    // intervalo entre viragens em segundos
unsigned long ultimoPrintSerial = 0;   // para controlar a frequência de prints

void toggleHumidificador() {
  digitalWrite(HUMIDIFIER_PIN, LOW);
  delay(150);
  digitalWrite(HUMIDIFIER_PIN, HIGH);
}

// ===================================================

void setup() {
  Serial.begin(9600);
  Serial.println(F("========================================"));
  Serial.println(F("SISTEMA DE INCUBAÇÃO INICIADO"));
  Serial.println(F("========================================"));

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  servoMotor.attach(SERVO_PIN);
  servoMotor.write(90);

  pinMode(HUMIDIFIER_PIN, OUTPUT);
  pinMode(HEATER_RELAY, OUTPUT);
  digitalWrite(HUMIDIFIER_PIN, HIGH); // estado neutro
  digitalWrite(HEATER_RELAY, LOW);

  rtc.begin();

  // DESCOMENTAR APENAS UMA VEZ PARA ACERTAR A HORA
//rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  lcd.setCursor(0, 0);
  lcd.print("SELECIONE");
  lcd.setCursor(0, 1);
  lcd.print("PRESET");
  
  Serial.println(F("Aguardando seleção de preset..."));
}

// ===================================================

void loop() {
  // ----- MENU DE SELEÇÃO DE PRESET -----
if (!presetConfirmado) {

  bool estadoNext   = digitalRead(BTN_NEXT);
  bool estadoSelect = digitalRead(BTN_SELECT);

  lcd.setCursor(0, 1);
  lcd.print("> ");
  lcd.print(presets[selectedPreset].name);
  lcd.print("        ");

  // DETEÇÃO DE CLIQUE (transição HIGH -> LOW)
  if (prevNext == HIGH && estadoNext == LOW) {
    selectedPreset++;
    if (selectedPreset >= NUM_PRESETS) selectedPreset = 0;

    Serial.print(F("Preset selecionado: "));
    Serial.println(presets[selectedPreset].name);
  }

  if (prevSelect == HIGH && estadoSelect == LOW) {
    presetConfirmado = true;
    incubacaoTerminada = false;
    startDate = rtc.now();
    ultimaViragemUnix = startDate.unixtime();
    intervaloViragem = 86400 / viragensPorDia[selectedPreset];

    Serial.println(F("========================================"));
    Serial.println(F("INCUBAÇÃO INICIADA"));
    Serial.print(F("Preset: "));
    Serial.println(presets[selectedPreset].name);
    Serial.print(F("Duração total: "));
    Serial.print(presets[selectedPreset].durationDays);
    Serial.println(F(" dias"));
    Serial.print(F("Data/Hora início: "));
    printDateTime(startDate);
    Serial.print(F("Viragens por dia: "));
    Serial.print(viragensPorDia[selectedPreset]);
    Serial.print(F(" (a cada "));
    Serial.print(intervaloViragem);
    Serial.println(F(" segundos)"));
    Serial.println(F("========================================"));

    lcd.clear();
  }

  prevNext = estadoNext;
  prevSelect = estadoSelect;

  delay(50); // pequeno debounce
  return;
}

  // ----- BOTÃO BACK: VOLTA AO MENU E DESLIGA TUDO -----
  if (digitalRead(BTN_BACK) == LOW) {
    presetConfirmado = false;
    incubacaoTerminada = false;
    digitalWrite(HUMIDIFIER_PIN, LOW);
    digitalWrite(HEATER_RELAY, LOW);
    aquecimentoLigado = false;
    humidificadorLigado = false;
    
    Serial.println(F("========================================"));
    Serial.println(F("INCUBAÇÃO INTERROMPIDA - VOLTANDO AO MENU"));
    Serial.println(F("========================================"));
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SELECIONE");
    lcd.setCursor(0, 1);
    lcd.print("PRESET");
    delay(300);
    while (digitalRead(BTN_BACK) == LOW);
    return;
  }

  // ----- LEITURA DO RTC E CÁLCULO DO DIA ATUAL -----
  DateTime now = rtc.now();
  int diaAtual = (now.unixtime() - startDate.unixtime()) / 86400 + 1;
  int duracaoTotal = presets[selectedPreset].durationDays;

  if (diaAtual > duracaoTotal) {
    diaAtual = duracaoTotal;
    if (!incubacaoTerminada) {
      incubacaoTerminada = true;
      Serial.println(F("========================================"));
      Serial.println(F("!!! INCUBAÇÃO TERMINADA !!!"));
      Serial.println(F("========================================"));
    }
  } else {
    incubacaoTerminada = false;
  }

  // ----- LEITURA DO SENSOR DHT -----
  tempAtual = dht.readTemperature();
  humAtual  = dht.readHumidity();

  if (isnan(tempAtual) || isnan(humAtual)) {
    lcd.setCursor(0, 0);
    lcd.print("ERRO SENSOR    ");
    return;
  }

  // ----- DEFINIÇÃO DOS ALVOS (COM AUMENTO NOS ÚLTIMOS DIAS) -----
  int humAlvo = presets[selectedPreset].targetHum;
  bool ultimosDias = (diaAtual > duracaoTotal - LAST_DAYS);
  if (ultimosDias) {
    humAlvo += HUMIDITY_INCREASE;
  }

// ----- CONTROLO DO HUMIDIFICADOR (TOGGLE POR PULSO) -----
if (humAtual < humAlvo) {

  if (!humidificadorLigado) {
    toggleHumidificador();
    humidificadorLigado = true;
    Serial.println(F("HUMIDIFICADOR LIGADO"));
  }

} else {

  if (humidificadorLigado) {
    toggleHumidificador();
    humidificadorLigado = false;
    Serial.println(F("HUMIDIFICADOR DESLIGADO"));
  }

}

  // ----- CONTROLE DO AQUECIMENTO (COM HISTERESE DE 0.2°C) -----
  float tempAlvo = presets[selectedPreset].targetTemp;
  float tempMin = tempAlvo - 0.2;
  float tempMax = tempAlvo + 0.2;

  if (tempAtual < tempMin && !aquecimentoLigado) {
    digitalWrite(HEATER_RELAY, HIGH);
    Serial.println(F("AQUECIMENTO LIGADO"));
    aquecimentoLigado = true;
  } else if (tempAtual > tempMax && aquecimentoLigado) {
    digitalWrite(HEATER_RELAY, LOW);
    Serial.println(F("AQUECIMENTO DESLIGADO"));
    aquecimentoLigado = false;
  }

  // ----- VIRAGENS AUTOMÁTICAS (apenas fora dos últimos dias) -----
  if (!incubacaoTerminada && !ultimosDias) {
    unsigned long agora = now.unixtime();
    if (agora - ultimaViragemUnix >= intervaloViragem) {
      Serial.print(F(">>> VIRAGEM EXECUTADA <<<"));
      
      // Executa a viragem
      servoMotor.write(110);
      delay(2000);
      servoMotor.write(90);
      ultimaViragemUnix = agora;
    }
  }

  // ----- BUZZER DE FIM DE INCUBAÇÃO -----
  if (incubacaoTerminada) {
    tone(BUZZER, 1000, 3000);
    digitalWrite(HEATER_RELAY, LOW);
    digitalWrite(HUMIDIFIER_PIN, LOW);
    aquecimentoLigado = false;
    humidificadorLigado = false;
  }

  // ----- INFORMAÇÕES PERIÓDICAS NO SERIAL (a cada 30 segundos) -----
  if (millis() - ultimoPrintSerial > 30000) {
    Serial.println(F("--- STATUS ATUAL ---"));
    Serial.print(F("Dia: "));
    Serial.print(diaAtual);
    Serial.print(F("/"));
    Serial.println(duracaoTotal);
    
    if (!incubacaoTerminada && !ultimosDias) {
      unsigned long agora = now.unixtime();
      unsigned long segundosProximaViragem = intervaloViragem - (agora - ultimaViragemUnix);
      
      Serial.print(F("Próxima viragem em: "));
      formatarTempo(segundosProximaViragem);
      Serial.println();
    } else if (ultimosDias) {
      Serial.println(F("Período de lockdown - SEM VIRAGENS"));
    } else if (incubacaoTerminada) {
      Serial.println(F("Incubação terminada"));
    }
    
    Serial.print(F("Temperatura: "));
    Serial.print(tempAtual, 1);
    Serial.print(F("°C (Alvo: "));
    Serial.print(tempAlvo, 1);
    Serial.println(F("°C)"));
    
    Serial.print(F("Humidade: "));
    Serial.print(humAtual, 0);
    Serial.print(F("% (Alvo: "));
    Serial.print(humAlvo);
    Serial.println(F("%)"));
    
    Serial.print(F("Aquecimento: "));
    Serial.println(aquecimentoLigado ? "LIGADO" : "desligado");
    Serial.print(F("Humidificador: "));
    Serial.println(humidificadorLigado ? "LIGADO" : "desligado");
    
    Serial.println(F("--------------------"));
    
    ultimoPrintSerial = millis();
  }

  // ----- ATUALIZAÇÃO DO LCD -----
  lcd.clear();

  // Linha 0: DIA x/x HH:MM
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

  // Linha 1: Temperatura e Humidade com indicadores
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

// ----- FUNÇÃO AUXILIAR PARA IMPRIMIR DATA/HORA -----
void printDateTime(DateTime dt) {
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  Serial.print(dt.day(), DEC);
  Serial.print(' ');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  if (dt.minute() < 10) Serial.print('0');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
  if (dt.second() < 10) Serial.print('0');
  Serial.print(dt.second(), DEC);
}

// ----- FUNÇÃO PARA FORMATAR TEMPO (SEGUNDOS -> H:MM:SS OU MM:SS) -----
void formatarTempo(unsigned long segundos) {
  if (segundos >= 3600) {
    // Formato H:MM:SS para horas
    unsigned long horas = segundos / 3600;
    unsigned long minutos = (segundos % 3600) / 60;
    unsigned long segs = segundos % 60;
    
    Serial.print(horas);
    Serial.print(":");
    if (minutos < 10) Serial.print("0");
    Serial.print(minutos);
    Serial.print(":");
    if (segs < 10) Serial.print("0");
    Serial.print(segs);
    Serial.print(" horas");
  } 
  else if (segundos >= 60) {
    // Formato MM:SS para minutos
    unsigned long minutos = segundos / 60;
    unsigned long segs = segundos % 60;
    
    Serial.print(minutos);
    Serial.print(":");
    if (segs < 10) Serial.print("0");
    Serial.print(segs);
    Serial.print(" minutos");
  } 
  else {
    // Apenas segundos
    Serial.print(segundos);
    Serial.print(" segundos");
  }
}