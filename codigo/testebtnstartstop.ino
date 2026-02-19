#include <LiquidCrystal_I2C.h>
#include <DHT.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define DHTPIN A0
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define BTN_PAUSA 2
#define BTN_INICIAR 3

bool pausado = false;
float temperatura = 0;
float humidade = 0;

void setup() {
  Serial.begin(9600);
  Serial.println("Sistema iniciado");

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0,0);
  lcd.print("Chocadeira");
  lcd.setCursor(0,1);
  lcd.print("ligada");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Temp / Hum");

  dht.begin();

  pinMode(BTN_PAUSA, INPUT_PULLUP);
  pinMode(BTN_INICIAR, INPUT_PULLUP);
}

void loop() {

  // ===== BOTAO PAUSA =====
  if (digitalRead(BTN_PAUSA) == LOW && !pausado) {
    Serial.println("PAUSA ATIVADA");
    pausado = true;

    lcd.setCursor(0,1);
    lcd.print("PAUSADO        ");

    delay(300);
    while (digitalRead(BTN_PAUSA) == LOW);
  }

  // ===== BOTAO INICIAR =====
  if (digitalRead(BTN_INICIAR) == LOW && pausado) {
    Serial.println("MEDICAO INICIADA");
    pausado = false;

    lcd.setCursor(0,1);
    lcd.print("A MEDIR...     ");

    delay(300);
    while (digitalRead(BTN_INICIAR) == LOW);
  }

  // ===== LEITURA =====
  if (!pausado) {
    temperatura = dht.readTemperature();
    humidade = dht.readHumidity();

    if (!isnan(temperatura) && !isnan(humidade)) {
      lcd.setCursor(0,1);
      lcd.print(temperatura,1);
      lcd.print("C ");
      lcd.print(humidade,0);
      lcd.print("%   ");
    }
  }

  delay(1000);
}
