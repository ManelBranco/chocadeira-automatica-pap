#define BTN 2
#define HUMIDIFIER_PIN 6

void setup() {
  Serial.begin(9600);

  pinMode(BTN, INPUT_PULLUP);
  pinMode(HUMIDIFIER_PIN, OUTPUT);

  digitalWrite(HUMIDIFIER_PIN, HIGH); // estado normal
}

void loop() {

  if (digitalRead(BTN) == LOW) {
    delay(200); 
    while (digitalRead(BTN) == LOW);

    Serial.println("Pulso enviado");

    // simula clique no botão do módulo
    digitalWrite(HUMIDIFIER_PIN, LOW);
    delay(150);              // pulso curto
    digitalWrite(HUMIDIFIER_PIN, HIGH);
  }
}