#define BTN_HEATER 2
#define HEATER_RELAY 7

bool heaterState = false;
bool lastButtonState = HIGH;

void setup() {
  Serial.begin(9600);

  pinMode(BTN_HEATER, INPUT_PULLUP);
  pinMode(HEATER_RELAY, OUTPUT);

  digitalWrite(HEATER_RELAY, HIGH); // começa desligado (active LOW)
}

void loop() {

  bool currentButtonState = digitalRead(BTN_HEATER);

  if (lastButtonState == HIGH && currentButtonState == LOW) {

    heaterState = !heaterState;

    digitalWrite(HEATER_RELAY, heaterState ? LOW : HIGH);

    Serial.print("Aquecimento: ");
    Serial.println(heaterState ? "LIGADO" : "DESLIGADO");

    delay(200);
  }

  lastButtonState = currentButtonState;
}
