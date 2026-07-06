#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#define W 128
#define H 64
static const uint8_t OLED_ADDR = 0x3C;
Adafruit_SSD1306 oled(W, H, &Wire, -1);

// PINLER
const uint8_t PIN_RELAY = 8;
const uint8_t PIN_ACS   = A0;
const uint8_t PIN_VBAT  = A1;

const uint8_t PIN_BTN_CUT  = 2; // interrupt
const uint8_t PIN_BTN_PAGE = 4;

const uint8_t PIN_LED_G = 6;
const uint8_t PIN_LED_R = 7;

const bool RELAY_ACTIVE_LOW = true;

// ÖLÇEK
const float DIV_RATIO = 2.0f;        // 100k/100k ise 2.0
const float ACS_MV_PER_A = 185.0f;   // ACS712 5A
const uint16_t ADC_MAX = 1023;

// KALİBRASYON
float VBAT_CAL = 0.95f;

// EŞİKLER
const float V_CUT = 4.15f;
const float V_MAX = 4.30f;
const float V_MIN = 2.90f;
const float I_MAX = 3.00f;
const unsigned long CUT_HOLD_MS = 1500UL;

// FULL kesmeyi “taper” ile yapan patch (önceki ek)
const float I_TAPER = 0.12f;                   // FULL için akım düşme eşiği
const unsigned long STARTUP_IGNORE_MS = 3000UL; // ilk 3sn cut kontrol yok
unsigned long startupMs = 0;

// ======= YENİ İSTEDİĞİN: 4.00V ÜSTÜ 10sn -> BLINK + SAYAC +1 =======
const float V_DONE = 4.00f;
const unsigned long DONE_HOLD_MS = 10000UL;    // 10 saniye
const unsigned long DONE_BLINK_MS = 10000UL;   // 10 saniye blink süresi

unsigned long doneCandidateMs = 0;
bool doneLatched = false;                       // 4.00 üstü 10sn oldu mu?
bool doneBlinkActive = false;
unsigned long doneBlinkUntilMs = 0;

// cycleCount çift artmasın diye:
bool cycleCountCredited = false;                // bu cycle içinde sayac +1 verildi mi?
// ===================================================================

// Şarj LED hysteresis (şarjdayken yeşil net)
const float I_CHG_ON  = 0.12f;
const float I_CHG_OFF = 0.08f;
bool chargingLatched = false;

volatile bool cutEvent = false;

bool relayOn = true;
uint8_t page = 0;

float vcc = 5.0f;
float acsZeroV = 2.5f;

float vbat = 0.0f;
float ibat = 0.0f;

unsigned long lastSampleMs = 0;
unsigned long lastUiMs = 0;
unsigned long cutCandidateMs = 0;

enum State { ST_ON=0, ST_OFF, ST_CHG, ST_FULL, ST_OCP, ST_OVP, ST_UVP };
State st = ST_ON;

// =====================================================
// EEPROM (ÇÖP VERİYİ ENGELLEME)  -> BOOT/CYCLE DÜZGÜN!
// =====================================================
static const int EE_ADDR = 0;          // tek kayıt kullanıyoruz
static const uint16_t EE_MAGIC = 0xB007;

struct EERec {
  uint16_t magic;   // 0xB007
  uint16_t boot;    // 0..65535
  uint16_t cycle;   // 0..65535
  uint16_t crc;     // checksum
};

uint16_t crc16_simple(uint16_t a, uint16_t b, uint16_t c) {
  uint16_t x = 0xA55A;
  x ^= a; x = (x << 1) | (x >> 15);
  x ^= b; x = (x << 1) | (x >> 15);
  x ^= c; x = (x << 1) | (x >> 15);
  return x;
}

bool eeValid(const EERec &r) {
  if (r.magic != EE_MAGIC) return false;
  return (r.crc == crc16_simple(r.magic, r.boot, r.cycle));
}

void eeLoadOrInit(uint16_t &bootCount, uint16_t &cycleCount) {
  EERec r;
  EEPROM.get(EE_ADDR, r);
  if (!eeValid(r)) {
    // EEPROM çöp -> otomatik temiz kayıt (SIFIRLAMA butonu falan yok)
    r.magic = EE_MAGIC;
    r.boot  = 0;
    r.cycle = 0;
    r.crc   = crc16_simple(r.magic, r.boot, r.cycle);
    EEPROM.put(EE_ADDR, r);
  }
  bootCount  = r.boot;
  cycleCount = r.cycle;
}

void eeSaveCounts(uint16_t bootCount, uint16_t cycleCount) {
  EERec r;
  r.magic = EE_MAGIC;
  r.boot  = bootCount;
  r.cycle = cycleCount;
  r.crc   = crc16_simple(r.magic, r.boot, r.cycle);
  EEPROM.put(EE_ADDR, r);
}

uint16_t bootCount = 0;
uint16_t cycleCount = 0;

// ===================== SON 10 CYCLE LOG =====================
enum EndReason : uint8_t { ER_FULL=0, ER_MAN=1, ER_OCP=2, ER_OVP=3, ER_UVP=4 };

struct CycleLog {
  uint16_t mAh;
  uint16_t vEnd_mV;
  uint8_t  reason;
};

static const uint8_t LOG_N = 10;
CycleLog logs[LOG_N];
uint8_t logHead = 0;
uint8_t logCount = 0;

bool cycleActive = false;
float mAh_acc = 0.0f;

CycleLog getLogNewest(uint8_t idx0_newest);

// ISR
void onCutIsr() { cutEvent = true; }

// ---------------- helpers ----------------
void setRelay(bool on) {
  relayOn = on;
  if (RELAY_ACTIVE_LOW) digitalWrite(PIN_RELAY, on ? LOW : HIGH);
  else                  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
}

long readVcc_mV() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC)) {}
  uint16_t adc = ADC;
  return 1125300L / adc;
}

float adcToV(uint16_t a) {
  return ((float)a / (float)ADC_MAX) * vcc;
}

uint16_t median5(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e) {
  uint16_t x[5] = {a,b,c,d,e};
  for (int i=0;i<5;i++) for (int j=i+1;j<5;j++) if (x[j]<x[i]) {uint16_t t=x[i];x[i]=x[j];x[j]=t;}
  return x[2];
}

float readBatteryV_stable() {
  uint16_t a1=analogRead(PIN_VBAT);
  uint16_t a2=analogRead(PIN_VBAT);
  uint16_t a3=analogRead(PIN_VBAT);
  uint16_t a4=analogRead(PIN_VBAT);
  uint16_t a5=analogRead(PIN_VBAT);
  uint16_t m = median5(a1,a2,a3,a4,a5);

  float v = adcToV(m) * DIV_RATIO;
  v *= VBAT_CAL;
  return v;
}

float readAcsCurrentA_stable() {
  uint16_t a1=analogRead(PIN_ACS);
  uint16_t a2=analogRead(PIN_ACS);
  uint16_t a3=analogRead(PIN_ACS);
  uint16_t a4=analogRead(PIN_ACS);
  uint16_t a5=analogRead(PIN_ACS);
  uint16_t m = median5(a1,a2,a3,a4,a5);

  float vout = adcToV(m);
  return ((vout - acsZeroV) * 1000.0f) / ACS_MV_PER_A;
}

void calibrateAcsZero(uint16_t samples = 220) {
  bool prev = relayOn;
  setRelay(false);
  delay(200);

  float sum = 0;
  for (uint16_t i=0;i<samples;i++) {
    uint16_t a = analogRead(PIN_ACS);
    sum += adcToV(a);
    delay(2);
  }
  acsZeroV = sum / (float)samples;

  setRelay(prev);
}

uint8_t socFromV(float v) {
  float x = (v - 3.0f) / 1.2f;
  if (x < 0) x = 0;
  if (x > 1) x = 1;
  return (uint8_t)(x * 100.0f + 0.5f);
}

const char* stStr() {
  switch (st) {
    case ST_ON:   return "ON";
    case ST_OFF:  return "OFF";
    case ST_CHG:  return "CHG";
    case ST_FULL: return "FULL";
    case ST_OCP:  return "OCP";
    case ST_OVP:  return "OVP";
    case ST_UVP:  return "UVP";
    default:      return "-";
  }
}

const char* reasonStr(uint8_t r) {
  switch ((EndReason)r) {
    case ER_FULL: return "F";
    case ER_MAN:  return "M";
    case ER_OCP:  return "I";
    case ER_OVP:  return "V";
    case ER_UVP:  return "U";
    default:      return "?";
  }
}

// ===================== cycle =====================
void cycleStartIfNeeded() {
  if (cycleActive) return;
  if (!relayOn) return;
  if (fabs(ibat) <= 0.10f) return;
  cycleActive = true;
  mAh_acc = 0.0f;

  // yeni cycle başladı: 4.00V event ve sayaç kilitleri sıfırlansın
  doneCandidateMs = 0;
  doneLatched = false;
  doneBlinkActive = false;
  doneBlinkUntilMs = 0;
  cycleCountCredited = false;
}

void cycleAccumulate(unsigned long dtMs) {
  if (!cycleActive) return;
  if (!relayOn) return;
  float a = fabs(ibat);
  mAh_acc += a * ((float)dtMs / 3600000.0f) * 1000.0f;
}

void cycleEnd(uint8_t reason) {
  if (!cycleActive) return;

  CycleLog e;
  e.mAh     = (uint16_t)min(65535.0f, mAh_acc + 0.5f);
  e.vEnd_mV = (uint16_t)min(65535.0f, vbat * 1000.0f + 0.5f);
  e.reason  = reason;

  logs[logHead] = e;
  logHead = (logHead + 1) % LOG_N;
  if (logCount < LOG_N) logCount++;

  cycleActive = false;
  mAh_acc = 0.0f;

  // cycle sayısını artır + EEPROM'a yaz
  // ama 4.00V/10sn eventinde zaten +1 verdiysek, burada tekrar verme (çifte saymasın)
  if (!cycleCountCredited) {
    if (cycleCount < 65535) cycleCount++;
    eeSaveCounts(bootCount, cycleCount);
  }
}

CycleLog getLogNewest(uint8_t idx0_newest) {
  CycleLog z{};
  if (idx0_newest >= logCount) return z;

  int last = (int)logHead - 1;
  if (last < 0) last += LOG_N;

  int phys = last - (int)idx0_newest;
  while (phys < 0) phys += LOG_N;

  return logs[phys];
}

// ---------------- butonlar ----------------
void handleCut() {
  if (!cutEvent) return;
  cutEvent = false;

  if (relayOn) {
    if (cycleActive) cycleEnd(ER_MAN);
    setRelay(false);
    st = ST_OFF;
  } else {
    vcc = readVcc_mV() / 1000.0f;
    calibrateAcsZero(200);
    setRelay(true);
    st = ST_ON;
    cutCandidateMs = 0;
    startupMs = millis(); // ilk 3sn cut yok

    // röle tekrar açıldı: done event sıfır (yeni şarj gibi)
    doneCandidateMs = 0;
    doneLatched = false;
    doneBlinkActive = false;
    doneBlinkUntilMs = 0;
    cycleCountCredited = false;
  }
}

void handlePage() {
  static bool last = true;
  bool now = digitalRead(PIN_BTN_PAGE);
  if (last && !now) page = (page + 1) % 4; // SARJ, PIL, LOG, TEKNIK
  last = now;
}

// ---------------- logic ----------------
void updateStateAndCut() {
  if (!relayOn) {
    if (st != ST_FULL) st = ST_OFF;
    return;
  }

  float a = fabs(ibat);

  if (a > I_MAX) { st = ST_OCP; if (cycleActive) cycleEnd(ER_OCP); setRelay(false); return; }
  if (vbat > V_MAX) { st = ST_OVP; if (cycleActive) cycleEnd(ER_OVP); setRelay(false); return; }
  if (vbat < V_MIN) { st = ST_UVP; if (cycleActive) cycleEnd(ER_UVP); setRelay(false); return; }

  bool charging = (a > 0.10f);

  // ilk 3sn FULL kesme kontrolü pas (ölçüm otursun)
  if (millis() - startupMs < STARTUP_IGNORE_MS) {
    cutCandidateMs = 0;
    st = charging ? ST_CHG : ST_ON;
    return;
  }

  // ====== YENİ: 4.00V üstü 10sn -> blink + sayac +1 ======
  // Not: cycleActive ve röle açıkken anlamlı (şarj döngüsü sırasında)
  if (cycleActive && relayOn && !doneLatched) {
    if (vbat >= V_DONE) {
      if (doneCandidateMs == 0) doneCandidateMs = millis();
      if (millis() - doneCandidateMs >= DONE_HOLD_MS) {
        doneLatched = true;

        // LED blink başlat
        doneBlinkActive = true;
        doneBlinkUntilMs = millis() + DONE_BLINK_MS;

        // sayaç +1 (EEPROM'a yaz) — ama sadece 1 kere
        if (!cycleCountCredited) {
          cycleCountCredited = true;
          if (cycleCount < 65535) cycleCount++;
          eeSaveCounts(bootCount, cycleCount);
        }
      }
    } else {
      doneCandidateMs = 0;
    }
  }
  // vbat 3.95 altına düşerse (histerezis) tekrar aday olsun
  if (doneLatched && vbat < (V_DONE - 0.05f)) {
    doneLatched = false;
    doneCandidateMs = 0;
    // sayaç kilidini açmıyorum; aynı cycle içinde bir daha +1 istemiyoruz
  }
  // ========================================================

  // FULL kesme: voltaj + taper akım
  if (vbat >= V_CUT && a <= I_TAPER) {
    if (cutCandidateMs == 0) cutCandidateMs = millis();
    if (millis() - cutCandidateMs >= CUT_HOLD_MS) {
      st = ST_FULL;
      if (cycleActive) cycleEnd(ER_FULL);
      setRelay(false);
      return;
    }
  } else {
    cutCandidateMs = 0;
  }

  st = charging ? ST_CHG : ST_ON;
}

void updateLeds() {
  // charging latch
  float aAbs = fabs(ibat);
  if (relayOn) {
    if (!chargingLatched && aAbs >= I_CHG_ON) chargingLatched = true;
    if (chargingLatched && aAbs <= I_CHG_OFF) chargingLatched = false;
  } else {
    chargingLatched = false;
  }

  // FAULT öncelik
  if (st == ST_OCP || st == ST_OVP || st == ST_UVP) {
    digitalWrite(PIN_LED_R, HIGH);
    digitalWrite(PIN_LED_G, LOW);
    return;
  }

  // ====== YENİ: “bitti” blink aktifse yeşil blink ======
  if (doneBlinkActive) {
    if (millis() >= doneBlinkUntilMs) {
      doneBlinkActive = false;
    } else {
      digitalWrite(PIN_LED_R, LOW);
      digitalWrite(PIN_LED_G, (millis()/250)%2); // 4Hz gibi
      return;
    }
  }
  // ======================================================

  if (st == ST_FULL) {
    digitalWrite(PIN_LED_R, LOW);
    digitalWrite(PIN_LED_G, (millis()/300)%2);
    return;
  }
  if (!relayOn) {
    digitalWrite(PIN_LED_R, LOW);
    digitalWrite(PIN_LED_G, LOW);
    return;
  }
  if (chargingLatched) {
    digitalWrite(PIN_LED_R, LOW);
    digitalWrite(PIN_LED_G, HIGH);
    return;
  }
  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_G, LOW);
}

// ---------------- UI (FERAH) ----------------
void drawTopBar(const char* title) {
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0,0);
  oled.print(title);

  oled.setCursor(62,0);
  oled.print("B:");
  oled.print(bootCount);

  oled.setCursor(98,0);
  oled.print("#");
  oled.print(cycleCount);
}

void drawBar(int x, int y, int w, int h, int pct) {
  oled.drawRect(x, y, w, h, SSD1306_WHITE);
  int fill = (int)((w - 2) * (pct / 100.0f));
  if (fill < 0) fill = 0;
  if (fill > w - 2) fill = w - 2;
  oled.fillRect(x+1, y+1, fill, h-2, SSD1306_WHITE);
}

void pageMain() {
  oled.clearDisplay();
  drawTopBar("SARJ");

  uint8_t soc = socFromV(vbat);

  oled.setTextSize(2);
  oled.setCursor(0, 14);
  oled.print(vbat, 2);
  oled.print("V");

  oled.setCursor(86, 14);
  oled.print(soc);
  oled.print("%");

  oled.setTextSize(1);
  oled.setCursor(0, 40);
  oled.print("I:"); oled.print(ibat, 2); oled.print("A");

  oled.setCursor(0, 54);
  oled.print("ST:"); oled.print(stStr());
  oled.print(" R:"); oled.print(relayOn ? "ON" : "OFF");
  oled.print(chargingLatched ? " CHG" : "    ");

  oled.display();
}

void pageBattery() {
  oled.clearDisplay();
  drawTopBar("PIL");

  int soc = socFromV(vbat);

  oled.setTextSize(2);
  oled.setCursor(0, 14);
  oled.print(soc);
  oled.print("%");

  oled.setTextSize(1);
  oled.setCursor(0, 38);
  oled.print("V:"); oled.print(vbat, 3);
  oled.print(" I:"); oled.print(ibat, 2);

  drawBar(0, 52, 128, 10, soc);
  oled.display();
}

void pageLog() {
  oled.clearDisplay();
  drawTopBar("LOG");

  oled.setTextSize(1);
  oled.setCursor(0, 12);
  oled.print("mAh V R | mAh V R");

  for (uint8_t row = 0; row < 5; row++) {
    uint8_t iL = row * 2;
    uint8_t iR = row * 2 + 1;
    int y = 22 + row * 8;

    if (iL < logCount) {
      CycleLog L = getLogNewest(iL);
      oled.setCursor(0, y);
      oled.print(L.mAh);
      oled.print(" ");
      oled.print(L.vEnd_mV/1000.0f, 2);
      oled.print(reasonStr(L.reason));
    }
    if (iR < logCount) {
      CycleLog R = getLogNewest(iR);
      oled.setCursor(72, y);
      oled.print(R.mAh);
      oled.print(" ");
      oled.print(R.vEnd_mV/1000.0f, 2);
      oled.print(reasonStr(R.reason));
    }
  }

  oled.display();
}

void pageTech() {
  oled.clearDisplay();
  drawTopBar("TEKNIK");

  oled.setTextSize(1);
  oled.setCursor(0, 14);
  oled.print("CUT:"); oled.print(V_CUT,2); oled.print("V");

  oled.setCursor(0, 26);
  oled.print("CAL:"); oled.print(VBAT_CAL,2);

  oled.setCursor(0, 38);
  oled.print("Vcc:"); oled.print(vcc,2);
  oled.print(" Z:"); oled.print(acsZeroV,2);

  oled.setCursor(0, 50);
  oled.print("I:"); oled.print(ibat,2);
  oled.print(" "); oled.print(stStr());

  oled.display();
}

// ---------------- setup/loop ----------------
void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);

  pinMode(PIN_BTN_CUT, INPUT_PULLUP);
  pinMode(PIN_BTN_PAGE, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CUT), onCutIsr, FALLING);

  Wire.begin();

  // *** SENİN ÇALIŞAN DÜZENİNİ BOZMADIM ***
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.clearDisplay();
  oled.display();

  // EEPROM güvenli yükle (çöpse otomatik 0'a çeker)
  eeLoadOrInit(bootCount, cycleCount);

  // her enerji verince boot +1
  if (bootCount < 65535) bootCount++;
  eeSaveCounts(bootCount, cycleCount);

  vcc = readVcc_mV() / 1000.0f;

  setRelay(false);
  delay(200);
  calibrateAcsZero(220);
  setRelay(true);

  lastSampleMs = millis();
  lastUiMs = millis();
  startupMs = millis();
}

void loop() {
  unsigned long now = millis();

  // Vcc yenile
  static unsigned long lastVccMs = 0;
  if (now - lastVccMs > 2000) {
    vcc = readVcc_mV() / 1000.0f;
    lastVccMs = now;
  }

  // ölçüm
  if (now - lastSampleMs >= 25) {
    unsigned long dt = now - lastSampleMs;
    lastSampleMs = now;

    float vb = readBatteryV_stable();
    float ia = readAcsCurrentA_stable();

    // filtre
    const float aV = 0.10f;
    const float aI = 0.12f;
    vbat = (1.0f - aV)*vbat + aV*vb;
    ibat = (1.0f - aI)*ibat + aI*ia;

    // cycle
    cycleStartIfNeeded();
    cycleAccumulate(dt);

    updateStateAndCut();
  }

  handleCut();
  handlePage();
  updateLeds();

  // UI
  if (now - lastUiMs > 200) {
    lastUiMs = now;
    if      (page == 0) pageMain();
    else if (page == 1) pageBattery();
    else if (page == 2) pageLog();
    else                pageTech();
  }
}
