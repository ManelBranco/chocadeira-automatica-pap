#include <Servo.h>

// ===== SERVO =====
Servo servo360;
#define SERVO_PIN 9


void setup() {
  Serial.begin(9600);
  Serial.println("Sistema iniciado");


  servo360.attach(SERVO_PIN);
  servo360.write(90); // servo parado

}

void loop() {
    servo360.write(0);
    
    servo360.write(90);
  delay(1000);
}
