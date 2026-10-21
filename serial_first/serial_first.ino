void setup() {
  Serial.begin(9600);
  Serial.println("Hello Arduino");
}

void loop() {
  Serial.println("Loop!");
  delay(500);
  Serial.println("--");
  delay(500);
}
