const int botao1 = 2;
const int botao2 = 3;
const int botao3 = 4;

void setup() {
  Serial.begin(9600);

  pinMode(botao1, INPUT);
  pinMode(botao2, INPUT);
  pinMode(botao3, INPUT);
}

void loop() {

  if (digitalRead(botao1) == HIGH) {
    Serial.println("Botao 1 pressionado");
    delay(300);
  }

  if (digitalRead(botao2) == HIGH) {
    Serial.println("Botao 2 pressionado");
    delay(300);
  }

  if (digitalRead(botao3) == HIGH) {
    Serial.println("Botao 3 pressionado");
    delay(300);
  }

}
