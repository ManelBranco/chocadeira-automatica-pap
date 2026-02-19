#include <LiquidCrystal_I2C.h>
#include <DHT.h>

#define DHTPIN 7      // Pino de dados do DHT22
#define DHTTYPE DHT22 // Tipo do sensor

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

float temperatura = 0;
float humidade = 0;

void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Temp / Humidade");

  dht.begin();
}

void loop() {
  temperatura = dht.readTemperature();   // Celsius
  humidade    = dht.readHumidity();

  // Verifica erro no sensor
  if (isnan(temperatura) || isnan(humidade)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ERRO DHT22!");
    lcd.setCursor(0, 1);
    lcd.print("Check ligacao");
    Serial.println("Erro ao ler o DHT22!");
    delay(2000);
    return;
  }

  // Serial Debug
  Serial.print("Temp: ");
  Serial.print(temperatura, 1);
  Serial.print(" C | Hum: ");
  Serial.print(humidade, 1);
  Serial.println(" %");

  // LCD
  lcd.setCursor(0, 1);
  lcd.print(temperatura, 1);
  lcd.print("C ");

  lcd.print(humidade, 0);
  lcd.print("%   ");

  delay(1000);
}
