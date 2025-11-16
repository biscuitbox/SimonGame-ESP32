/*
  Simon Game (ESP32 + 1.3" SH1106 128x64 I2C OLED + Buzzer + 4LED/4Buttons)
  - U8g2 사용 (한글: unifont)
  - 제목: 16px급 굵은 폰트(fub14) + 그림자 + 라운드 라인박스
  - 본문: 한글 유니폰트, "제목박스 하단 + 여백 + height" 규칙으로 안정 배치
  - 시작 전 두 문구 번갈아 표시, 시작/진행/종료 화면 모두 동일 규칙
  - 5레벨마다 속도 증가, 최고기록 NVS 저장, 색상별 음계
  - NEW: 시작 대기에서
      * BTN0+BTN3 동시 롱프레스(1.5s) → 최고기록 초기화
      * BTN1+BTN2 동시 롱프레스(1.5s) → 음소거 토글 (NVS 저장)
      * 단일 버튼 "짧은 눌림(≤700ms)"만 게임 시작
*/

#include <Wire.h>
#include <U8g2lib.h>
#include <Preferences.h>

// ---------- OLED (SH1106) ----------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0,
  U8X8_PIN_NONE
);

// ---------- 핀 매핑 ----------
const int ledPins[4]    = {14, 27, 26, 25};  // R, G, B, Y
const int buttonPins[4] = {12, 13, 15, 4};   // 버튼 (INPUT_PULLUP) -> idx 0..3
const int buzzerPin     = 32;                // 피에조 부저(+)

// ---------- 게임 파라미터 ----------
const int MAX_LEVEL = 50;
int sequenceArr[MAX_LEVEL];
int currentLevel = 0;
int bestLevel    = 0;

Preferences prefs;

// 색상별 음계
int tonesHz[4] = {440, 523, 587, 659}; // A4, C5, D5, E5

// ---------- 사운드 ON/OFF ----------
bool mute = false; // NVS에 저장/복원

// =======================================================
// 공통 헬퍼
// =======================================================

// 중앙 정렬(가로) - y는 baseline
void drawCenteredText(int y, const char* text) {
  int w = u8g2.getUTF8Width(text);
  int x = (u8g2.getDisplayWidth() - w) / 2;
  u8g2.drawUTF8(x, y, text);
}

/*
  제목: 그림자 + 라운드 라인박스
  - 제목 폰트: fub14 (약 16px 수준)
  - titleY: 제목 baseline (권장 22~28)
  - 반환: 라운드 박스의 하단 y (본문은 여기서 marginTop만큼 띄워 배치)
*/
int drawTitleShadowBox(const char* title, int titleY,
                       int shadowDx = 1, int shadowDy = 1,
                       int padX = 6,  int padY = 4,  int r = 5) {
  u8g2.setFont(u8g2_font_fub14_tf); // Bold 14 (약 16px 체감)
  int w   = u8g2.getUTF8Width(title);
  int h   = u8g2.getMaxCharHeight();
  int x   = (u8g2.getDisplayWidth() - w) / 2;
  int top = titleY - h;

  // 그림자 → 본문 텍스트
  u8g2.setDrawColor(1);
  u8g2.drawUTF8(x + shadowDx, titleY + shadowDy, title);
  u8g2.drawUTF8(x, titleY, title);

  // 라운드 라인박스 (텍스트 박스에 패딩)
  int fx = x - padX;
  int fy = top - padY;
  int fw = w + 2 * padX;
  int fh = h + 2 * padY;

  // 화면 경계 보정
  if (fx < 0) fx = 0;
  if (fy < 0) fy = 0;
  if (fx + fw > u8g2.getDisplayWidth())  fw = u8g2.getDisplayWidth() - fx;
  if (fy + fh > u8g2.getDisplayHeight()) fh = u8g2.getDisplayHeight() - fy;

  u8g2.drawRFrame(fx, fy, fw, fh, r);
  return fy + fh; // 박스 하단 y
}

// =======================================================
// 화면 렌더링
// =======================================================

// 시작/안내 화면: 제목 + 본문 1~2줄
void showTitleAndBody(const char* title, const char* line1 = "", const char* line2 = "") {
  u8g2.clearBuffer();
  int titleBottom = drawTitleShadowBox(title, /*titleY=*/24, 1,1,6,4,5);

  // 본문 (한글 폰트)
  u8g2.setFont(u8g2_font_unifont_t_korean2);
  int marginTop = 10;                           // 제목 박스와 본문 간 여백
  int ascentOrH = u8g2.getMaxCharHeight();      // ascent 대체
  int lineH     = u8g2.getMaxCharHeight() + 2;

  int y1 = titleBottom + marginTop + ascentOrH; // 첫 줄 baseline
  if (line1 && line1[0]) drawCenteredText(y1, line1);
  if (line2 && line2[0]) drawCenteredText(y1 + lineH, line2);

  u8g2.sendBuffer();
}

// 진행 화면: LEVEL
void showLevelScreen(int level, int best) {
  u8g2.clearBuffer();
  int titleBottom = drawTitleShadowBox("LEVEL", /*titleY=*/24, 1,1,6,4,5);

  u8g2.setFont(u8g2_font_unifont_t_korean2);
  int marginTop = 10;
  int ascentOrH = u8g2.getMaxCharHeight();

  char buf[40];
  snprintf(buf, sizeof(buf), "You:%d Best:%d", level, best);
  int y = titleBottom + marginTop + ascentOrH;
  drawCenteredText(y, buf);

  u8g2.sendBuffer();
}

// 종료 화면: GAME OVER
void showGameOver(int finished, int best, bool newRecord) {
  u8g2.clearBuffer();
  int titleBottom = drawTitleShadowBox("GAME OVER", /*titleY=*/24, 1,1,6,4,5);

  u8g2.setFont(u8g2_font_unifont_t_korean2);
  int marginTop = 10;
  int ascentOrH = u8g2.getMaxCharHeight();
  int lineH     = u8g2.getMaxCharHeight() + 2;

  int y1 = titleBottom + marginTop + ascentOrH;

  char buf[40];
  snprintf(buf, sizeof(buf), "You:%d Best:%d", finished, best);
  drawCenteredText(y1, buf);

  if (newRecord) {
    drawCenteredText(y1 + lineH, "NEW RECORD!");
  }

  u8g2.sendBuffer();
}

// =======================================================
// 부저 / 톤
// =======================================================
void buzzerPlay(int freq, int dur) {
  if (mute) { delay(dur); return; }  // 음소거 시 시간만 소비
  tone(buzzerPin, freq, dur);
  delay(dur);
  noTone(buzzerPin);
}
void playStartSound()    { buzzerPlay(600,120); buzzerPlay(800,120); buzzerPlay(1000,150); }
void playGameOverSound() { buzzerPlay(200,200); delay(50); buzzerPlay(160,300); }
void playCongratsSound() { buzzerPlay(700,120); buzzerPlay(900,120); buzzerPlay(1100,120); buzzerPlay(1300,200); }
void playColorTone(int idx, int dur) { buzzerPlay(tonesHz[idx], dur); }
void playConfirmBeep()   { buzzerPlay(900,100); delay(60); buzzerPlay(1200,120); } // 확인음

// =======================================================
// 입력 / 버튼
// =======================================================
bool isButtonPressedIdx(int idx) {
  return digitalRead(buttonPins[idx]) == LOW;
}
bool isAnyButtonPressed() {
  for (int i = 0; i < 4; i++) if (isButtonPressedIdx(i)) return true;
  return false;
}

// 4개 버튼의 현재 상태를 비트마스크로 반환 (눌림=1)
uint8_t readButtonMask() {
  uint8_t m = 0;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(buttonPins[i]) == LOW) m |= (1 << i);
  }
  return m;
}

// "단일 버튼"의 "짧은 눌림"을 감지 (maxPressMs 이내에 눌렀다 뗀 경우만 인정)
// 반환: 눌린 버튼 인덱스(0~3), 없으면 -1
int detectSingleShortPress(uint16_t maxPressMs = 700) {
  uint8_t m = readButtonMask();
  if (m == 0) return -1;                // 아무것도 안 눌림
  if ((m & (m - 1)) != 0) return -1;    // 2개 이상 동시 눌림 → 시작 아님

  int idx = __builtin_ctz(m);           // set bit 위치 → 버튼 인덱스
  uint32_t t0 = millis();

  // 눌리는 동안 “다른 버튼”이 추가로 눌려지면 취소(=시작 아님)
  while (readButtonMask() != 0) {
    uint8_t cur = readButtonMask();
    if ((cur & (cur - 1)) != 0) return -1; // 중간에 2개 이상 눌리면 무효
    if (millis() - t0 > maxPressMs) return -1; // 길게 눌렀으면 시작 아님
    delay(5);
  }
  delay(30); // 바운스 정리
  return idx;
}

// 특정 두 버튼이 ms 동안 **동시에 계속** 눌려 있는지 검사
bool twoButtonLongPress(int idxA, int idxB, uint16_t ms = 1500) {
  // 시작 시 두 버튼 모두 눌려 있어야 시작
  if (!(isButtonPressedIdx(idxA) && isButtonPressedIdx(idxB))) return false;
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    // 두 버튼 중 하나라도 떼면 취소
    if (!(isButtonPressedIdx(idxA) && isButtonPressedIdx(idxB))) return false;
    delay(10);
  }
  // 성공: 두 버튼 모두 릴리즈될 때까지 대기(중복 트리거 방지)
  while (isButtonPressedIdx(idxA) || isButtonPressedIdx(idxB)) delay(10);
  return true;
}

int waitForButton() {
  while (true) {
    for (int i = 0; i < 4; i++) {
      if (digitalRead(buttonPins[i]) == LOW) {
        // 소프트 디바운스
        delay(15);
        if (digitalRead(buttonPins[i]) != LOW) continue;

        digitalWrite(ledPins[i], HIGH);
        playColorTone(i, 250);
        digitalWrite(ledPins[i], LOW);

        // 눌림 해제까지 대기
        while (digitalRead(buttonPins[i]) == LOW) delay(5);
        delay(100);
        return i;
      }
    }
  }
}

// =======================================================
// 속도 / 패턴
// =======================================================
int getShowDelay(int level) {
  int base   = 550;                  // 0~4
  int faster = (level / 5) * 60;     // 5,10,15..마다 빨라짐
  int result = base - faster;
  if (result < 200) result = 200;
  return result;
}
void playSequence(int level) {
  int showDelay = getShowDelay(level);
  for (int i = 0; i < level; i++) {
    int c = sequenceArr[i];

    showLevelScreen(level, bestLevel);      // 진행 화면 유지
    digitalWrite(ledPins[c], HIGH);
    playColorTone(c, showDelay);
    digitalWrite(ledPins[c], LOW);

    delay(150); // 다음 색 간격
  }
}
bool getUserInput(int level) {
  for (int i = 0; i < level; i++) {
    if (waitForButton() != sequenceArr[i]) return false;
  }
  return true;
}

// =======================================================
// 시작 대기
//   - BTN0+BTN3(동시롱프레스) → 최고기록 초기화
//   - BTN1+BTN2(동시롱프레스) → 사운드 음소거 토글
//   - 단일 버튼 "짧은 눌림(≤700ms)"만 게임 시작
// =======================================================
void waitForStartScreen() {
  uint32_t lastSwap = millis();
  bool showA = true; // 안내 문구 교대용

  while (true) {
    // 1) 컨트롤: 동시 롱프레스 우선 처리
    if (twoButtonLongPress(/*A=*/0, /*B=*/3, 1500)) { // 초기화
      bestLevel = 0;
      prefs.putInt("best", 0);
      showTitleAndBody("Reset", "Best cleared!");
      if (!mute) playConfirmBeep();
      while (readButtonMask() != 0) delay(10); // 모든 버튼 떼질 때까지 대기
      delay(700);
    } else if (twoButtonLongPress(/*A=*/1, /*B=*/2, 1500)) { // 음소거 토글
      mute = !mute;
      prefs.putBool("mute", mute);
      if (mute) {
        showTitleAndBody("Sound", "OFF");
      } else {
        showTitleAndBody("Sound", "ON");
        playConfirmBeep();
      }
      while (readButtonMask() != 0) delay(10);
      delay(700);
    }

    // 2) 단일 버튼 짧은 눌림 → 게임 시작
    int startBtn = detectSingleShortPress(700);
    if (startBtn >= 0) {
      return; // loop()에서 게임 시작
    }

    // 3) 안내 문구 표현(1초 간격 교대)
    if (millis() - lastSwap > 1000) {
      lastSwap = millis();
      showA = !showA;
      if (showA) showTitleAndBody("Simon", "Waiting...");
      else       showTitleAndBody("Game",  "Press a button");
    }

    delay(50); // 폴링 텀
  }
}

// =======================================================
// Arduino 생명주기
// =======================================================
void setup() {
  for (int i = 0; i < 4; i++) {
    pinMode(ledPins[i], OUTPUT);
    pinMode(buttonPins[i], INPUT_PULLUP);
  }
  pinMode(buzzerPin, OUTPUT);

  Wire.begin(21, 22);       // SDA=21, SCL=22
  // 주소가 0x3D라면:
  // u8g2.setI2CAddress(0x3D << 1);
  u8g2.begin();

  randomSeed(analogRead(34)); // 비연결 ADC로 시드
  prefs.begin("simongame", false);
  bestLevel = prefs.getInt("best", 0);
  mute      = prefs.getBool("mute", false);  // 음소거 복원

  waitForStartScreen();
}

void loop() {
  showTitleAndBody("Start", "You Can Do It!");
  playStartSound();
  delay(600);

  currentLevel = 0;

  while (true) {
    sequenceArr[currentLevel] = random(0, 4);
    currentLevel++;

    showLevelScreen(currentLevel, bestLevel);
    delay(400);

    playSequence(currentLevel);
    bool ok = getUserInput(currentLevel);

    if (!ok) {
      int finished = currentLevel - 1;
      if (finished < 0) finished = 0;

      bool newRec = false;
      if (finished > bestLevel) {
        bestLevel = finished;
        prefs.putInt("best", bestLevel);
        newRec = true;
      }

      showGameOver(finished, bestLevel, newRec);
      if (newRec) playCongratsSound();
      else        playGameOverSound();

      delay(2000);
      waitForStartScreen();
      break;
    } else {
      buzzerPlay(900, 120); // 짧은 성공음
      delay(400);
    }
  }
}
