#include <EEPROM.h>

// #define DEBUG
#define MAX_REMOTES 10

#define PIN_VOL PIN_PA2
#define PIN_MELODY PIN_PA0
#define PIN_RADIO PIN_PA1
#define PI_SX073A PIN_PA3
#define BEEP 0x42
#define BEEP_BEEP 0x43
#define STOP_PLAY 0xFF  //?

struct HighLow {
  uint8_t high, low;
};

struct Protocol {
  uint16_t pulseLength;
  HighLow sync, zero, one;
  bool inverted : 1;
  unsigned nBits : 7;
};

struct StateMachine {
  bool sync;
  uint8_t nBit;
  uint32_t val;
};

struct Remote {
  uint8_t protocol;
  uint32_t code;
};

//The "real" for pulse duration values have been fine tuned on actual receive performance
const Protocol proto[] = {
  { 400/*350*/, { 1, 31 }, { 1, 3 }, { 3, 1 }, true, 24 }, //original remote
  { 300/*270*/, { 36, 1 }, { 1, 2 }, { 2, 1 }, false, 12 } //HT12E
};

StateMachine sm[sizeof proto / sizeof(*proto)];

uint8_t currentMelody = 0, currentVolume = 3, btnVol = 0, btnMelody = 0;
uint32_t tStartPulse, tEndPulse, tPrevStartPulse;
bool learning = false;
uint64_t tStartLearning = 0;
volatile bool positivePulse;
volatile uint32_t _tEndPulse;
volatile bool ready = true, pulseAvailable = false;

bool checkDuration(uint32_t a, uint32_t b, uint32_t c) {
  return abs(abs(a - b) - c) < c / 3;
}

ISR(PORTA_PORT_vect) {
  uint8_t flags = VPORTA.INTFLAGS;

  VPORTA.INTFLAGS = flags;
  if (flags & (1 << 1)) {  //PA1 change
    if (!ready) return;
    positivePulse = !digitalReadFast(PIN_RADIO);
    _tEndPulse = micros();
    ready = false;
    pulseAvailable = true;
  }
}

void setup() {
#ifdef DEBUG
  Serial.begin(9600);
#endif
  pinModeFast(PIN_VOL, INPUT_PULLUP);
  pinModeFast(PIN_MELODY, INPUT_PULLUP);
  pinModeFast(PI_SX073A, OUTPUT);
  digitalWriteFast(PI_SX073A, HIGH);
  PORTA.PIN1CTRL |= PORT_ISC_BOTHEDGES_gc;

#ifndef DEBUG
  while (!digitalReadFast(PIN_MELODY));
  while (!digitalReadFast(PIN_VOL));
  delay(1000);
  play(BEEP_BEEP);
#endif
}

//send a command to the SX073A melody generator
//protocol: 10.05ms low, then 8 bit (MSB first) encoded in 1.45ms each as follows:
//  0: 0.35ms pulse
//  1: 1.05ms pulse
//subsequent commands must be separated by a pause of at least 10.05ms
void play(uint8_t melody) {
#ifndef DEBUG
  digitalWriteFast(PI_SX073A, LOW);
  delayMicroseconds(10050);

  for (int i = 0; i < 8; i++) {
    digitalWriteFast(PI_SX073A, HIGH);
    unsigned d = ((melody & 0x80) == 0) ? 350 : 1050;
    delayMicroseconds(d);
    digitalWriteFast(PI_SX073A, LOW);
    delayMicroseconds(1450 - d);
    melody <<= 1;
  }
  digitalWriteFast(PI_SX073A, HIGH);
  delayMicroseconds(10050);
#endif
}

int findRemoteAddress(uint8_t protocol, uint32_t code) {
#ifndef DEBUG
  for (int i = 0; i < MAX_REMOTES; i++) {
    uint8_t p = EEPROM.read(6 * i);
    uint32_t c;
    EEPROM.get(6 * i + 1, c);
    if (p == 0xFF)
      return -i - 1;  //not found: first free slot returned as negative
    if (protocol == p && code == c)
      return i;  //found remote
  }
#endif
  return MAX_REMOTES;  //no room left
}

void saveRemote(uint8_t protocol, uint32_t code, uint8_t melody) {
#ifndef DEBUG
  int n = findRemoteAddress(protocol, code);
  if (n == MAX_REMOTES) return;  //no room, do nothing
  if (n < 0) {                   //new remote, write the whole record in EEPROM
    n = -n - 1;
    EEPROM.write(6 * n, protocol);
    EEPROM.put(6 * n + 1, code);
  }
  EEPROM.write(6 * n + 5, currentMelody);  //update melody for this remote
#endif
}

void startLearning() {
  learning = true;
  tStartLearning = millis();
  play(STOP_PLAY);
  play(BEEP);
}

void stopLearning() {
  learning = false;
  tStartLearning = 0;
  play(BEEP_BEEP);
}

void loop() {
#ifndef DEBUG
  if (tStartLearning != 0 && millis() - tStartLearning > 8000) 
    stopLearning();

  btnVol = (btnVol * 9 + (digitalReadFast(PIN_VOL) ? 0 : 100)) / 10;
  btnMelody = (btnMelody * 9 + (digitalReadFast(PIN_MELODY) ? 0 : 100)) / 10;

  if (btnVol > 50) {
#ifdef DEBUG
    Serial.printf("VOL\n");
#endif
    currentVolume--;
    if (currentVolume == 0xFF) currentVolume = 3;
    const static uint8_t volumes[] = { 0x10,4,9,15 };
    play(0xE0 | volumes[currentVolume]);
    play(currentMelody);
    btnVol = 0;
    uint64_t t = millis();
    while (!digitalReadFast(PIN_VOL))
      if (millis() - t > 10000) {
        play(BEEP_BEEP);
        for (int i=0;i<12;i++)  //Erase EEPROM
          EEPROM.write(i,0xFF);
        break;
      }
    while (!digitalReadFast(PIN_VOL))
      ;
    delay(200);
  }

  if (btnMelody > 50) {
#ifdef DEBUG
    Serial.printf("MEL\n");
#endif
    btnMelody = 0;
    uint64_t t = millis();
    while (!digitalReadFast(PIN_MELODY))
      if (millis() - t > 2000) {
#ifdef DEBUG
        Serial.printf("LONG\n");
#endif
        startLearning();
        break;
      }
    if (!learning) {
      currentMelody++;
      if (currentMelody == 60)
        currentMelody = 0;
      play(currentMelody);
    }
    while (!digitalReadFast(PIN_MELODY))
      ;
    delay(200);
  }

  if (!pulseAvailable)
    return;
#endif

  pulseAvailable = false;
  tEndPulse = _tEndPulse;
  ready = true;

  for (unsigned i = 0; i < sizeof proto / sizeof(*proto); i++) {
    StateMachine *pStateMachine = sm + i;
    const Protocol *p = proto + i;
    if (p->inverted == positivePulse) continue;

    if (pStateMachine->sync) {
      if (checkDuration(tPrevStartPulse, tStartPulse, p->pulseLength * p->one.high) && checkDuration(tStartPulse, tEndPulse, p->pulseLength * p->one.low))
        pStateMachine->val |= 1;
      else if (!checkDuration(tPrevStartPulse, tStartPulse, p->pulseLength * p->zero.high) || !checkDuration(tStartPulse, tEndPulse, p->pulseLength * p->zero.low)) {
        pStateMachine->val = pStateMachine->nBit = 0;
        pStateMachine->sync = false;
        break;
      }

      if (pStateMachine->nBit < p->nBits)
        pStateMachine->nBit++;

      if (pStateMachine->nBit == p->nBits) {
#ifdef DEBUG
        Serial.printf("received code=%lX protocol=%d\n", pStateMachine->val, i);
#endif
        int n = findRemoteAddress(i, pStateMachine->val);
        if (learning) {
          stopLearning();
          saveRemote(i, pStateMachine->val, currentMelody);
        } else if (n >= 0 && n < MAX_REMOTES) {
          play(EEPROM.read(n * 6 + 5));
        }
        delay(2000);
#ifdef DEBUG
          Serial.printf("\tremote=%d\n", n);
#endif
        pStateMachine->val = pStateMachine->nBit = 0;
        pStateMachine->sync = false;
      } else {
        pStateMachine->val <<= 1;
        pStateMachine->val &= ((1UL << p->nBits) - 1);
      }
    } else if (checkDuration(tPrevStartPulse, tStartPulse, p->pulseLength * p->sync.high) && checkDuration(tStartPulse, tEndPulse, p->pulseLength * p->sync.low)) {
      pStateMachine->sync = true;
    }
  }
  tPrevStartPulse = tStartPulse;
  tStartPulse = tEndPulse;
}
