#define BTN_NEXT   2
#define BTN_SELECT 3
#define BTN_BACK   4

bool prevNext   = HIGH;
bool prevSelect = HIGH;
bool prevBack   = HIGH;

void setup() {
  Serial.begin(9600);

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  Serial.println("Teste de botoes iniciado...");
}

void loop() {

  bool leituraNext = digitalRead(BTN_NEXT);
  if (leituraNext == LOW && prevNext == HIGH) {
    Serial.println("NEXT pressionado");
    delay(200);
  }
  prevNext = leituraNext;


  bool leituraSelect = digitalRead(BTN_SELECT);
  if (leituraSelect == LOW && prevSelect == HIGH) {
    Serial.println("SELECT pressionado");
    delay(200);
  }
  prevSelect = leituraSelect;


  bool leituraBack = digitalRead(BTN_BACK);
  if (leituraBack == LOW && prevBack == HIGH) {
    Serial.println("BACK pressionado");
    delay(200);
  }
  prevBack = leituraBack;

}