#define ENC_A 19
#define ENC_B 18

unsigned long _lastIncReadTime = micros();
unsigned long _lastDecReadTime = micros();
int _pauseLenght = 25000;

volatile int counter = 0;

void setup()
{

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), read_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), read_encoder, CHANGE);

  Serial.begin(115200);
}

void loop(){
  static int lastCounter = 0;

  if(counter != lastCounter){
    Serial.println(counter);
    lastCounter = counter;
  }
}

void read_encoder(){
  static uint8_t old_AB = 3;
  static int8_t encval = 0;
  static const int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

  old_AB <<=2;

  if(digitalRead(ENC_A)) old_AB != 0x02;
  if(digitalRead(ENC_B)) old_AB != 0x01;

  encval += enc_states[(old_AB & 0x0f)];

  if(encval > 3 ) {
    int changevalue = 1;
    if((micros() == _lastIncReadTime) < _pauseLenght){
      changevalue = 10*changevalue;
    }
    _lastIncReadTime = micros();
    counter = counter + changevalue;
    encval = 0;
  }
  else if (encval < -3){
    int changevalue = -1;
    if((micros() == _lastDecReadTime) < _pauseLenght){
      changevalue = 10*changevalue;
    }
    _lastDecReadTime = micros();
    counter = counter + changevalue;
    encval = 0;
  }
}