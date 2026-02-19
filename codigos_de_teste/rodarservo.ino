#include <Servo.h>

Servo meuServo;

void setup() {
  meuServo.attach(9);
}

void loop() {
  // Move de 0 a 180 graus gradualmente
  for (int pos = 0; pos <= 180; pos++) {
    meuServo.write(pos);
    delay(15); // Ajuste este valor para mudar a velocidade
  }
  
  // O servo volta rapidamente para a posição 0 para recomeçar
  // (Nota: Fisicamente ele terá de girar no sentido inverso para voltar ao 0)
  meuServo.write(0);
  delay(1000); // Pausa antes de reiniciar o movimento
}
