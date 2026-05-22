#include <Arduino.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "a3144_counter.h"
#include "config.h"
#include "gauss_monitor.h"
#include "motor_driver.h"
#include "presets_store.h"

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLS, LCD_ROWS);

enum ScreenMode {
  SCREEN_MANUAL,
  SCREEN_PRESET_LIST,
  SCREEN_PRESET_NEW,
  SCREEN_PRESET_NAME,
  SCREEN_PRESET_VIEW,
  SCREEN_PRESET_FULL,
  SCREEN_PREWIND,
  SCREEN_GAUSS = 90,
  SCREEN_COUNTDOWN,
  SCREEN_WINDING,
  SCREEN_DONE
};

enum ButtonEvent : uint8_t { BTN_NONE, BTN_CLICK, BTN_LONG };

static ScreenMode screenMode = SCREEN_MANUAL;
static ScreenMode lastScreenMode = SCREEN_MANUAL;
static int manualField = 0, menuIndex = 0, presetIndex = 0, presetListOffset = 0;
static int presetField = 0, nameIndex = 0, manualDigitIndex = 0, presetDigitIndex = 0;
static char presetName[PRESET_NAME_LEN] = "PRESET";
static int countdownValue = 3;
static unsigned long countdownTickMs = 0;
static bool windingPaused = false, blinkOn = true;
static unsigned long blinkTickMs = 0, blinkDirty = false, windingUpdateMs = 0;
static long targetTurns = 1000;
static int targetRpm = 300;
static bool targetDirectionCW = true;
static int turnsDigits[TURN_DIGITS], rpmDigits[RPM_DIGITS];
static volatile int encoderDelta = 0;
static volatile uint8_t encoderState = 0;
static int encoderAccum = 0;
static unsigned long buttonDownMs = 0;
static bool buttonWasDown = false;
static long targetSteps = 0;
static float turnsAccum = 0.0f;
static float prewindStepCarry = 0.0f;
static long prewindStepsQueued = 0;
static uint32_t prewindLastStepUs = 0;

static ButtonEvent readButton() {
  bool pressed = digitalRead(ENC_BTN_PIN) == LOW;
  if (pressed && !buttonWasDown) { buttonDownMs = millis(); buttonWasDown = true; }
  if (!pressed && buttonWasDown) {
    unsigned long held = millis() - buttonDownMs;
    buttonWasDown = false;
    return held > LONG_PRESS_MS ? BTN_LONG : BTN_CLICK;
  }
  return BTN_NONE;
}

static void handleEncoderInterrupt() {
  uint8_t state = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  uint8_t combined = (encoderState << 2) | state;
  static const int8_t table[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  encoderDelta += table[combined];
  encoderState = state;
}

static int readEncoderDetent() {
  int delta = 0;
  noInterrupts(); delta = encoderDelta; encoderDelta = 0; interrupts();
  if (!delta) return 0;
  encoderAccum += delta;
  if (encoderAccum >= 4) { encoderAccum = 0; return 1; }
  if (encoderAccum <= -4) { encoderAccum = 0; return -1; }
  return 0;
}

static void printPadded(const char *text) {
  lcd.print(text);
  for (int i = strlen(text); i < 20; i++) lcd.print(' ');
}

static void valueToDigits(long v, int *d, int n) { for (int i=n-1;i>=0;i--){d[i]=v%10;v/=10;} }
static long digitsToValue(const int *d, int n) { long v=0; for(int i=0;i<n;i++) v=v*10+d[i]; return v; }
static int wrapDigit(int digit, int delta) { int v=(digit+delta)%10; return v<0?v+10:v; }

static void syncDigitsFromTargets() {
  valueToDigits(targetTurns, turnsDigits, TURN_DIGITS);
  valueToDigits(targetRpm, rpmDigits, RPM_DIGITS);
}
static void clampTargets() {
  if (targetTurns<1) targetTurns=1; else if(targetTurns>MAX_TURNS) targetTurns=MAX_TURNS;
  if (targetRpm<MIN_RPM) targetRpm=MIN_RPM; else if(targetRpm>MAX_RPM_USER) targetRpm=MAX_RPM_USER;
  syncDigitsFromTargets();
}

static void printDigitsLine(const char *label, const int *digits, int count, bool sel, int active) {
  char line[21];
  int off = snprintf(line,sizeof(line),"%s%s",sel?"> ":"  ",label);
  for(int i=0;i<count&&off+i<20;i++)
    line[off+i]=(sel&&i==active&&!blinkOn)?' ':(char)('0'+digits[i]);
  for(int i=off+count;i<20;i++) line[i]=' ';
  line[20]='\0'; printPadded(line);
}

static void drawManualScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded("Manual mode");
  lcd.setCursor(0,1); printDigitsLine("Turns:",turnsDigits,TURN_DIGITS,manualField==0,manualField==0?manualDigitIndex:-1);
  lcd.setCursor(0,2); printDigitsLine("RPM:",rpmDigits,RPM_DIGITS,manualField==1,manualField==1?manualDigitIndex:-1);
  lcd.setCursor(0,3);
  if(manualField==2){ char l[21]; snprintf(l,sizeof(l),"> Dir:%s",blinkOn?(targetDirectionCW?"CW":"CCW"):"  "); printPadded(l);}
  else if(manualField==3) printPadded("> Start winding");
  else printPadded("Hold: presets");
}

static void drawPresetListScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded("Presets");
  int total=1+MAX_PRESETS;
  if(menuIndex<presetListOffset) presetListOffset=menuIndex;
  if(menuIndex>=presetListOffset+2) presetListOffset=menuIndex-1;
  for(int row=0;row<2;row++){
    int item=presetListOffset+row; lcd.setCursor(0,row+1);
    if(item>=total){ printPadded(" "); continue; }
    char line[21]; snprintf(line,sizeof(line),"%s",menuIndex==item?"> ":"  ");
    int off=strlen(line);
    if(item==0) snprintf(line+off,sizeof(line)-off,"New preset");
    else snprintf(line+off,sizeof(line)-off,"%s",presets[item-1].valid?presets[item-1].name:"(empty)");
    printPadded(line);
  }
  lcd.setCursor(0,3); printPadded("Hold: back");
}

static void drawPresetNewScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded("New preset");
  lcd.setCursor(0,1); printDigitsLine("Turns:",turnsDigits,TURN_DIGITS,presetField==0,presetField==0?presetDigitIndex:-1);
  lcd.setCursor(0,2); printDigitsLine("RPM:",rpmDigits,RPM_DIGITS,presetField==1,presetField==1?presetDigitIndex:-1);
  lcd.setCursor(0,3); char l[21]; snprintf(l,sizeof(l),"%s Dir:%s",presetField==2?"> ":" ",(presetField==2&&!blinkOn)?"  ":(targetDirectionCW?"CW":"CCW"));
  printPadded(l);
}

static void drawPresetNameScreen() {
  char dn[PRESET_NAME_LEN]; strncpy(dn,presetName,sizeof(dn));
  if(!blinkOn&&nameIndex>=0&&nameIndex<(int)sizeof(dn)-1) dn[nameIndex]=' ';
  lcd.clear(); lcd.setCursor(0,0); printPadded("Preset name");
  lcd.setCursor(0,1); printPadded(dn);
  lcd.setCursor(0,2); printPadded("Click: next");
  lcd.setCursor(0,3); printPadded("Hold: save");
}

static void drawPresetViewScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded(presets[presetIndex].name);
  char l[21];
  lcd.setCursor(0,1); snprintf(l,sizeof(l),"Turns: %ld",presets[presetIndex].turns); printPadded(l);
  lcd.setCursor(0,2); snprintf(l,sizeof(l),"RPM: %d",presets[presetIndex].rpm); printPadded(l);
  lcd.setCursor(0,3); snprintf(l,sizeof(l),"Dir: %s",presets[presetIndex].directionCW?"CW":"CCW"); printPadded(l);
}

static void drawGaussScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded("Magnet gauss meter");
  lcd.setCursor(0,2); printPadded("Auto-hide <50G");
}
static void drawGaussValues() {
  lcd.setCursor(0,1); char l[21]; float g=gaussValue(); float a=fabsf(g);
  const char *pole="CENTER";
  if(g>=GAUSS_ENTER_THRESHOLD) pole="N"; else if(g<=-GAUSS_ENTER_THRESHOLD) pole="S";
  snprintf(l,sizeof(l),"G:%7.1f  Pole:%s",a,pole); printPadded(l);
}

static void drawPrewindScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded("Prewind mode");
  lcd.setCursor(0,1); printPadded("Rotate encoder");
  lcd.setCursor(0,2); printPadded("Click: start");
  lcd.setCursor(0,3); printPadded("Hold: cancel");
}
static void drawProgressLine() {
  lcd.setCursor(0,1); char l[21];
  long t=(long)a3144Turns(); if(t<0)t=0;
  long pct=targetTurns>0?(t*100L)/targetTurns:0;
  snprintf(l,sizeof(l),"Turns:%5ld %3ld%%",t,pct); printPadded(l);
}
static void drawCountdownScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded("Get ready");
  lcd.setCursor(0,2); char l[21]; snprintf(l,sizeof(l),"Starting in: %d",countdownValue); printPadded(l);
}
static void drawWindingScreen() {
  lcd.clear(); lcd.setCursor(0,0); printPadded("Pickup winding");
  lcd.setCursor(0,2); printPadded("Press to pause");
}
static void drawPausedScreen() {
  lcd.setCursor(0,0); printPadded("Paused (hold menu)");
  long t=(long)a3144Turns(); char l[21];
  lcd.setCursor(0,1); snprintf(l,sizeof(l),"Turns: %ld/%ld",t,targetTurns); printPadded(l);
  lcd.setCursor(0,3); printPadded("Press: resume");
}
static void drawDoneScreen() {
  lcd.clear(); lcd.setCursor(0,1); printPadded("Winding complete");
  lcd.setCursor(0,2); printPadded("Press to return");
}

static char nextNameChar(char c,int d){
  const char cs[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  int n=sizeof(cs)-1,idx=0; for(int i=0;i<n;i++) if(cs[i]==c){idx=i;break;}
  idx=(idx+d)%n; if(idx<0) idx+=n; return cs[idx];
}

static void enterPrewind() {
  screenMode=SCREEN_PREWIND;
  motorResetStepAccumulator(); turnsAccum=0; a3144Reset();
  prewindStepCarry=0; prewindStepsQueued=0; prewindLastStepUs=micros();
  windingPaused=false; targetSteps=targetTurns*COUNT_STEPS_PER_REV;
  motorEnable(true); motorSetDirection(targetDirectionCW);
  a3144SetTargetDirection(targetDirectionCW); a3144OnMotorDirection(targetDirectionCW);
  analogWrite(STEP_PIN,0); windingUpdateMs=millis(); drawPrewindScreen();
}

static void startCountdownFromPrewind() {
  prewindStepsQueued=0; prewindStepCarry=0; analogWrite(STEP_PIN,0);
  screenMode=SCREEN_COUNTDOWN; countdownValue=3; countdownTickMs=millis(); drawCountdownScreen();
}

static void startWindingNow() {
  clampTargets();
  screenMode=SCREEN_WINDING;
  motorEnable(true);
  lcd.clear();
  motorSetDirection(targetDirectionCW);
  a3144SetTargetDirection(targetDirectionCW);
  a3144OnMotorDirection(targetDirectionCW);
  motorStartWinding(MIN_RPM, targetRpm, true);
  windingUpdateMs=millis();
  drawWindingScreen();
}

void setup() {
  Wire.setSDA(I2C_SDA_PIN); Wire.setSCL(I2C_SCL_PIN);
  Wire.begin(); Wire.setClock(100000);
  pinMode(ENC_A_PIN,INPUT_PULLUP); pinMode(ENC_B_PIN,INPUT_PULLUP); pinMode(ENC_BTN_PIN,INPUT_PULLUP);
  lcd.init(); lcd.backlight();
  lcd.clear(); lcd.setCursor(0,0); printPadded("Pickup winder");
  lcd.setCursor(0,1); printPadded("Zeroing sensors...");
  presetsBegin(); presetsLoad();
  gaussBegin();
  a3144Begin();
  motorDriverBegin();
  motorEnable(false);
  valueToDigits(targetTurns,turnsDigits,TURN_DIGITS);
  valueToDigits(targetRpm,rpmDigits,RPM_DIGITS);
  blinkTickMs=millis();
  encoderState=(digitalRead(ENC_A_PIN)<<1)|digitalRead(ENC_B_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN),handleEncoderInterrupt,CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN),handleEncoderInterrupt,CHANGE);
  delay(300);
  drawManualScreen();
}

void loop() {
  ButtonEvent btn=readButton();
  unsigned long nowMs=millis();
  int delta=readEncoderDetent();
  bool allowBlink=(screenMode==SCREEN_MANUAL&&manualField<=2)||screenMode==SCREEN_PRESET_NEW||screenMode==SCREEN_PRESET_NAME;
  if(allowBlink && nowMs-blinkTickMs>=BLINK_MS){ blinkTickMs=nowMs; blinkOn=!blinkOn; blinkDirty=true; }
  if(screenMode!=lastScreenMode){ lastScreenMode=screenMode; blinkDirty=false; }

  int sm=(int)screenMode;
  gaussUpdate(nowMs, sm, sm, true);
  screenMode=(ScreenMode)sm;
  if(screenMode==SCREEN_GAUSS && lastScreenMode!=SCREEN_GAUSS){ drawGaussScreen(); drawGaussValues(); windingUpdateMs=nowMs; }
  if(screenMode==SCREEN_GAUSS){
    if(nowMs-windingUpdateMs>=WINDING_UI_INTERVAL_MS){ windingUpdateMs=nowMs; drawGaussValues(); }
    if(btn==BTN_LONG){ gaussForceExit(); screenMode=(ScreenMode)SCREEN_MANUAL; drawManualScreen(); }
    return;
  }

  turnsAccum=a3144Turns();

  switch(screenMode){
  case SCREEN_MANUAL:
    if(delta){
      if(manualField==0){turnsDigits[manualDigitIndex]=wrapDigit(turnsDigits[manualDigitIndex],delta);targetTurns=digitsToValue(turnsDigits,TURN_DIGITS);}
      else if(manualField==1){rpmDigits[manualDigitIndex]=wrapDigit(rpmDigits[manualDigitIndex],delta);targetRpm=digitsToValue(rpmDigits,RPM_DIGITS);}
      else if(manualField==2) targetDirectionCW=delta>0;
      drawManualScreen();
    }
    if(btn==BTN_CLICK){
      if(manualField==0){manualDigitIndex++; if(manualDigitIndex>=TURN_DIGITS){manualDigitIndex=0;manualField=1;} clampTargets(); drawManualScreen();}
      else if(manualField==1){manualDigitIndex++; if(manualDigitIndex>=RPM_DIGITS){manualDigitIndex=0;manualField=2;} clampTargets(); drawManualScreen();}
      else if(manualField==2){manualField=3; drawManualScreen();}
      else enterPrewind();
    } else if(btn==BTN_LONG){ screenMode=SCREEN_PRESET_LIST; menuIndex=0; drawPresetListScreen(); }
    break;

  case SCREEN_PRESET_LIST:
    if(delta){ menuIndex=constrain(menuIndex+delta,0,MAX_PRESETS); drawPresetListScreen(); }
    if(btn==BTN_CLICK){
      if(menuIndex==0){ screenMode=SCREEN_PRESET_NEW; presetField=0; drawPresetNewScreen(); }
      else { presetIndex=menuIndex-1; if(presets[presetIndex].valid){ screenMode=SCREEN_PRESET_VIEW; drawPresetViewScreen(); } }
    } else if(btn==BTN_LONG){ screenMode=SCREEN_MANUAL; drawManualScreen(); }
    break;

  case SCREEN_PRESET_NEW:
    if(delta){
      if(presetField==0){turnsDigits[presetDigitIndex]=wrapDigit(turnsDigits[presetDigitIndex],delta);targetTurns=digitsToValue(turnsDigits,TURN_DIGITS);}
      else if(presetField==1){rpmDigits[presetDigitIndex]=wrapDigit(rpmDigits[presetDigitIndex],delta);targetRpm=digitsToValue(rpmDigits,RPM_DIGITS);}
      else targetDirectionCW=delta>0;
      drawPresetNewScreen();
    }
    if(btn==BTN_CLICK){
      if(presetField==0){presetDigitIndex++; if(presetDigitIndex>=TURN_DIGITS){presetDigitIndex=0;presetField=1;} clampTargets(); drawPresetNewScreen();}
      else if(presetField==1){presetDigitIndex++; if(presetDigitIndex>=RPM_DIGITS){presetDigitIndex=0;presetField=2;} clampTargets(); drawPresetNewScreen();}
      else { screenMode=SCREEN_PRESET_NAME; strncpy(presetName,"PRESET",sizeof(presetName)-1); nameIndex=0; drawPresetNameScreen(); }
    } else if(btn==BTN_LONG){ screenMode=SCREEN_PRESET_LIST; drawPresetListScreen(); }
    break;

  case SCREEN_PRESET_NAME:
    if(delta){ presetName[nameIndex]=nextNameChar(presetName[nameIndex],delta); drawPresetNameScreen(); }
    if(btn==BTN_CLICK){ nameIndex++; if(nameIndex>=PRESET_NAME_LEN-1) nameIndex=0; drawPresetNameScreen(); }
    else if(btn==BTN_LONG){
      int slot=presetsFindEmptySlot();
      if(slot<0){ screenMode=SCREEN_PRESET_FULL; lcd.clear(); lcd.setCursor(0,0); printPadded("Memory full"); }
      else {
        Preset p{}; strncpy(p.name,presetName,sizeof(p.name)-1); p.turns=targetTurns; p.rpm=min(targetRpm,MAX_RPM_USER);
        p.directionCW=targetDirectionCW; p.valid=true; presetsSave(slot,p);
        screenMode=SCREEN_PRESET_LIST; drawPresetListScreen();
      }
    }
    break;

  case SCREEN_PRESET_VIEW:
    if(btn==BTN_CLICK){
      targetTurns=presets[presetIndex].turns; targetRpm=min(presets[presetIndex].rpm,MAX_RPM_USER);
      targetDirectionCW=presets[presetIndex].directionCW; syncDigitsFromTargets(); enterPrewind();
    } else if(btn==BTN_LONG){ presetsDelete(presetIndex); screenMode=SCREEN_PRESET_LIST; drawPresetListScreen(); }
    break;

  case SCREEN_PREWIND:
    if(btn==BTN_CLICK) startCountdownFromPrewind();
    else if(btn==BTN_LONG){ motorEnable(false); analogWrite(STEP_PIN,0); screenMode=SCREEN_MANUAL; drawManualScreen(); }
    else if(delta){
      float spd=(float)STEPS_PER_REV/(float)ENCODER_DETENTS_PER_REV;
      prewindStepCarry+=spd*(float)delta;
      long steps=(long)prewindStepCarry;
      if(steps){ prewindStepCarry-=(float)steps; prewindStepsQueued+=steps; }
    }
    if(prewindStepsQueued){
      uint32_t nowUs=micros();
      if(nowUs-prewindLastStepUs>=PREWIND_STEP_INTERVAL_US){
        prewindLastStepUs=nowUs;
        bool cw=prewindStepsQueued>0?targetDirectionCW:!targetDirectionCW;
        motorSingleStep(cw); a3144OnMotorDirection(cw);
        prewindStepsQueued+=(prewindStepsQueued>0)?-1:1;
      }
    }
    if(nowMs-windingUpdateMs>=WINDING_UI_INTERVAL_MS){ windingUpdateMs=nowMs; drawProgressLine(); }
    break;

  case SCREEN_COUNTDOWN:
    if(btn==BTN_CLICK) startWindingNow();
    else if(nowMs-countdownTickMs>=1000){
      countdownTickMs=nowMs; countdownValue--;
      if(countdownValue<0) startWindingNow(); else drawCountdownScreen();
    }
    break;

  case SCREEN_WINDING:
    motorUpdate(nowMs);
    a3144OnMotorDirection(motorDirectionCW());
    if(windingPaused){
      if(btn==BTN_CLICK){ windingPaused=false; screenMode=SCREEN_COUNTDOWN; countdownValue=3; countdownTickMs=millis(); drawCountdownScreen(); }
      else if(btn==BTN_LONG){ windingPaused=false; motorStopImmediate(); motorEnable(false); screenMode=SCREEN_MANUAL; drawManualScreen(); }
      break;
    }
    if(btn==BTN_CLICK) motorRequestStop(false,true,false);
    else if(btn==BTN_LONG) motorRequestStop(false,false,true);
    if(motorStopPending() && !motorRampActive()){
      motorStopImmediate(); motorEnable(false);
      if(motorStopForCompletion()){ screenMode=SCREEN_DONE; drawDoneScreen(); }
      else if(motorStopPause()){ windingPaused=true; lcd.clear(); drawPausedScreen(); }
      else if(motorStopToMenu()){ screenMode=SCREEN_MANUAL; drawManualScreen(); }
      break;
    }
    if(turnsAccum>=(float)targetTurns) motorRequestStop(true,false,false);
    else if(nowMs-windingUpdateMs>=WINDING_UI_INTERVAL_MS){ windingUpdateMs=nowMs; drawProgressLine(); }
    break;

  case SCREEN_DONE:
    if(btn==BTN_CLICK){ screenMode=SCREEN_MANUAL; drawManualScreen(); }
    break;
  default: break;
  }
}
