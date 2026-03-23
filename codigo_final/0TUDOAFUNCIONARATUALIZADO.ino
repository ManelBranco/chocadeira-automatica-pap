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

// ===== HUMIDIFICADOR =====
#define HUMIDIFIER_PIN 6

// ===== RELÉS =====
#define HEATER_RELAY_1 7
#define BUZZER         8
#define HEATER_RELAY_2 9

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

int viragensPorDia[] = {4,3,6,4,4,4};

const int NUM_PRESETS = sizeof(presets)/sizeof(presets[0]);
const int LAST_DAYS = 3;
const int HUMIDITY_INCREASE = 10;

// ===== ESTADO =====
int selectedPreset = 0;
bool presetConfirmado = false;
bool incubacaoTerminada = false;

DateTime startDate;

float tempAtual = 0;
float humAtual  = 0;

bool aquecimentoLigado = false;
bool humidificadorLigado = false;
int relesAquecimentoAtivos = 0;

unsigned long ultimaViragemUnix = 0;
unsigned long intervaloViragem = 0;
unsigned long ultimoPrintSerial = 0;
unsigned long inicioPulsoHumidificador = 0;
unsigned long ultimoCicloHumidade = 0;

int margemHum = 3;
float margemTemp = 0.25;

const unsigned long DURACAO_PULSO_HUM_MS = 8000;
const unsigned long INTERVALO_MIN_HUM_MS = 20000;

// ===== FUNÇÕES HUMIDIFICADOR =====

void cliqueHumidificador(){
  digitalWrite(HUMIDIFIER_PIN, LOW);
  delay(120);
  digitalWrite(HUMIDIFIER_PIN, HIGH);
}

void ligarHumidificador(){
  cliqueHumidificador();
}

void desligarHumidificador(){
  cliqueHumidificador();
  delay(200);
  cliqueHumidificador();
}

void ligarHumidificadorSeNecessario(){
  if(!humidificadorLigado){
    ligarHumidificador();
    humidificadorLigado = true;
    inicioPulsoHumidificador = millis();
    ultimoCicloHumidade = millis();
    Serial.println(F("HUMIDIFICADOR LIGADO"));
  }
}

void desligarHumidificadorSeNecessario(){
  if(humidificadorLigado){
    desligarHumidificador();
    humidificadorLigado = false;
    Serial.println(F("HUMIDIFICADOR DESLIGADO"));
  }
}

void definirAquecimento(int relesAtivos){
  if(relesAtivos < 0) relesAtivos = 0;
  if(relesAtivos > 2) relesAtivos = 2;

  digitalWrite(HEATER_RELAY_1, relesAtivos >= 1 ? HIGH : LOW);
  digitalWrite(HEATER_RELAY_2, relesAtivos >= 2 ? HIGH : LOW);

  relesAquecimentoAtivos = relesAtivos;
  aquecimentoLigado = (relesAtivos > 0);
}

void printDateTime(DateTime dt){
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  Serial.print(dt.day(), DEC);
  Serial.print(' ');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  if(dt.minute() < 10) Serial.print('0');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
  if(dt.second() < 10) Serial.print('0');
  Serial.print(dt.second(), DEC);
}

void formatarTempo(unsigned long segundos){
  if(segundos >= 3600){
    unsigned long horas = segundos / 3600;
    unsigned long minutos = (segundos % 3600) / 60;
    unsigned long segs = segundos % 60;

    Serial.print(horas);
    Serial.print(":");
    if(minutos < 10) Serial.print("0");
    Serial.print(minutos);
    Serial.print(":");
    if(segs < 10) Serial.print("0");
    Serial.print(segs);
    Serial.print(" horas");
  }
  else if(segundos >= 60){
    unsigned long minutos = segundos / 60;
    unsigned long segs = segundos % 60;

    Serial.print(minutos);
    Serial.print(":");
    if(segs < 10) Serial.print("0");
    Serial.print(segs);
    Serial.print(" minutos");
  }
  else{
    Serial.print(segundos);
    Serial.print(" segundos");
  }
}

// ===================================================

void setup() {

  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();

  pinMode(BTN_NEXT,INPUT_PULLUP);
  pinMode(BTN_SELECT,INPUT_PULLUP);
  pinMode(BTN_BACK,INPUT_PULLUP);

  servoMotor.attach(SERVO_PIN);
  servoMotor.write(90);

  pinMode(HUMIDIFIER_PIN,OUTPUT);
  pinMode(HEATER_RELAY_1,OUTPUT);
  pinMode(HEATER_RELAY_2,OUTPUT);

  digitalWrite(HUMIDIFIER_PIN,HIGH);
  digitalWrite(HEATER_RELAY_1,LOW);
  digitalWrite(HEATER_RELAY_2,LOW);

  rtc.begin();

  lcd.setCursor(0,0);
  lcd.print("SELECIONE");
  lcd.setCursor(0,1);
  lcd.print("PRESET");
}

// ===================================================

void loop(){

if(!presetConfirmado){

  bool estadoNext=digitalRead(BTN_NEXT);
  bool estadoSelect=digitalRead(BTN_SELECT);

  lcd.setCursor(0,1);
  lcd.print("> ");
  lcd.print(presets[selectedPreset].name);
  lcd.print("        ");

  if(prevNext==HIGH && estadoNext==LOW){
    selectedPreset++;
    if(selectedPreset>=NUM_PRESETS) selectedPreset=0;
  }

  if(prevSelect==HIGH && estadoSelect==LOW){

    presetConfirmado=true;
    incubacaoTerminada=false;
    humidificadorLigado=false;
    aquecimentoLigado=false;
    relesAquecimentoAtivos=0;

    startDate=rtc.now();
    ultimaViragemUnix=startDate.unixtime();
    intervaloViragem=86400/viragensPorDia[selectedPreset];
    inicioPulsoHumidificador=0;
    ultimoCicloHumidade=millis()-INTERVALO_MIN_HUM_MS;
    ultimoPrintSerial=0;

    lcd.clear();
  }

  prevNext=estadoNext;
  prevSelect=estadoSelect;

  delay(50);
  return;
}

// ===== BOTÃO BACK =====

if(digitalRead(BTN_BACK)==LOW){

  presetConfirmado=false;
  incubacaoTerminada=false;

  desligarHumidificadorSeNecessario();

  definirAquecimento(0);

  aquecimentoLigado=false;
  humidificadorLigado=false;
  inicioPulsoHumidificador=0;
  ultimoCicloHumidade=0;

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SELECIONE");
  lcd.setCursor(0,1);
  lcd.print("PRESET");

  delay(300);

  while(digitalRead(BTN_BACK)==LOW);

  return;
}

// ===== RTC =====

DateTime now=rtc.now();

int diaAtual=(now.unixtime()-startDate.unixtime())/86400+1;
int duracaoTotal=presets[selectedPreset].durationDays;

if(diaAtual>duracaoTotal){
  diaAtual=duracaoTotal;
  incubacaoTerminada=true;
}
else{
  incubacaoTerminada=false;
}

// ===== SENSOR =====

tempAtual=dht.readTemperature();
humAtual=dht.readHumidity();

if(isnan(tempAtual)||isnan(humAtual)){
  lcd.setCursor(0,0);
  lcd.print("ERRO SENSOR");
  return;
}

// ===== HUMIDADE ALVO =====

int humAlvo=presets[selectedPreset].targetHum;

bool ultimosDias=(diaAtual>duracaoTotal-LAST_DAYS);

if(ultimosDias) humAlvo+=HUMIDITY_INCREASE;

// ===== CONTROLO HUMIDIFICADOR =====

if(humidificadorLigado && millis() - inicioPulsoHumidificador >= DURACAO_PULSO_HUM_MS){
  desligarHumidificadorSeNecessario();
}

if(humAtual < humAlvo - margemHum){

  if(!humidificadorLigado && (ultimoCicloHumidade == 0 || millis() - ultimoCicloHumidade >= INTERVALO_MIN_HUM_MS)){

    ligarHumidificadorSeNecessario();

  }

}

if(humAtual > humAlvo + margemHum){

  desligarHumidificadorSeNecessario();

}

// ===== AQUECIMENTO =====

float tempAlvo=presets[selectedPreset].targetTemp;

float tempLiga1=tempAlvo-margemTemp;
float tempLiga2=tempAlvo-(margemTemp*2.0);
float tempMax=tempAlvo+margemTemp;

if(tempAtual<tempLiga2){

  definirAquecimento(2);

}

else if(tempAtual<tempLiga1){

  definirAquecimento(1);

}

else if(tempAtual>tempMax){

  definirAquecimento(0);

}

// ===== VIRAGEM =====

if(!incubacaoTerminada && !ultimosDias){

  unsigned long agora=now.unixtime();

  if(agora-ultimaViragemUnix>=intervaloViragem){

    servoMotor.write(110);
    delay(2000);
    servoMotor.write(90);

    ultimaViragemUnix=agora;

  }

}

// ===== BUZZER =====

if(incubacaoTerminada){

  tone(BUZZER,1000,3000);

  definirAquecimento(0);

  desligarHumidificadorSeNecessario();

}

// ===== SERIAL MONITOR =====

if(millis() - ultimoPrintSerial > 30000){
  Serial.println(F("--- STATUS ATUAL ---"));

  Serial.print(F("Preset: "));
  Serial.println(presets[selectedPreset].name);

  Serial.print(F("Inicio: "));
  printDateTime(startDate);
  Serial.println();

  Serial.print(F("Dia: "));
  Serial.print(diaAtual);
  Serial.print(F("/"));
  Serial.println(duracaoTotal);

  if(!incubacaoTerminada && !ultimosDias){
    unsigned long agora = now.unixtime();
    unsigned long segundosDecorridos = agora - ultimaViragemUnix;
    unsigned long segundosProximaViragem = (segundosDecorridos >= intervaloViragem) ? 0 : (intervaloViragem - segundosDecorridos);

    Serial.print(F("Proxima viragem em: "));
    formatarTempo(segundosProximaViragem);
    Serial.println();
  }
  else if(ultimosDias){
    Serial.println(F("Periodo de lockdown - SEM VIRAGENS"));
  }
  else if(incubacaoTerminada){
    Serial.println(F("Incubacao terminada"));
  }

  Serial.print(F("Temperatura: "));
  Serial.print(tempAtual, 1);
  Serial.print(F("C (Alvo: "));
  Serial.print(tempAlvo, 1);
  Serial.println(F("C)"));

  Serial.print(F("Humidade: "));
  Serial.print(humAtual, 0);
  Serial.print(F("% (Alvo: "));
  Serial.print(humAlvo);
  Serial.println(F("%)"));

  Serial.print(F("Aquecimento: "));
  if(relesAquecimentoAtivos == 0) Serial.println(F("desligado (0 reles)"));
  else if(relesAquecimentoAtivos == 1) Serial.println(F("LIGADO (1 rele)"));
  else Serial.println(F("LIGADO (2 reles)"));

  Serial.print(F("Humidificador: "));
  Serial.println(humidificadorLigado ? F("LIGADO") : F("desligado"));

  Serial.println(F("--------------------"));

  ultimoPrintSerial = millis();
}

// ===== LCD =====

lcd.clear();

lcd.setCursor(0,0);

lcd.print("D");
lcd.print(diaAtual);
lcd.print("/");
lcd.print(duracaoTotal);

lcd.print(" ");

lcd.print(now.hour());
lcd.print(":");

if(now.minute()<10) lcd.print("0");

lcd.print(now.minute());

lcd.setCursor(0,1);

lcd.print(tempAtual,1);
lcd.print("C ");

lcd.print(humAtual,0);
lcd.print("%");

if(humidificadorLigado) lcd.print("*");
else lcd.print(" ");

if(aquecimentoLigado) lcd.print("H");
else lcd.print(" ");

delay(1000);

}
