#include <FastLED.h>

#include "IRLremote.h"

#include <avr/sleep.h>

#include <avr/power.h>

#include <avr/interrupt.h>

#include <avr/wdt.h>

#include <EEPROM.h>

#define DEBUG
#define RED_PIN 4
#define GREEN_PIN 0
#define BLUE_PIN 1
#define IR_PIN 2

#define BEER_COUNT_DEFAULT 2
#define TEAM_DEFAULT neutral

#define FIRST_RUN_ADDR        (0)
typedef byte FIRST_RUN_TYPE;
#define LOCAL_SETTINGS_ADDR   (FIRST_RUN_ADDR + sizeof(FIRST_RUN_TYPE))

#define adc_disable()(ADCSRA &= ~(1 << ADEN)) // macro to disable ADC

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_LEONARDO)
#warning "Debug statements will be enabled"
#elif defined(ARDUINO_attiny) || defined(ARDUINO_AVR_DIGISPARK)
#warning "Production Mode"
#else
#error "Unsupported board type"
#endif

enum wake {
  def = 0, ir = 1, dog = 2
}; // enum for types of wake

enum teamColour : byte {
  fire = 0, earth = 90,  air = 159, water = 160, neutral = 42
}; // enum for team colours

CRGBPalette16 teamPallet(
  CHSV( (teamColour) fire, 255, 255),
  CHSV( (teamColour) fire, 255, 255),
  CHSV( (teamColour) fire, 255, 255),
  CHSV( (teamColour) fire, 255, 255),
  CHSV( (teamColour) water, 255, 255),
  CHSV( (teamColour) water, 255, 255),
  CHSV( (teamColour) water, 255, 255),
  CHSV( (teamColour) water, 255, 255),
  CHSV( (teamColour) earth, 255, 255),
  CHSV( (teamColour) earth, 255, 255),
  CHSV( (teamColour) earth, 255, 255),
  CHSV( (teamColour) earth, 255, 255),
  CHSV( (teamColour) air, 200, 255),
  CHSV( (teamColour) air, 200, 255),
  CHSV( (teamColour) air, 200, 255),
  CHSV( (teamColour) air, 200, 255)
);

enum irCodes : uint32_t
{
  changeTeamToFire = 4033872000,
  changeTeamToEarth = 4050714752,
  changeTeamToWater = 4084400256,
  changeTeamToAir = 4067557504,
  changeTeamToNeutral = 2989621376,
  playVictoryAnimation = 3377004672,
  playFailAnimationFireDominant = 3461218432,
  playFailAnimationWaterDominant = 3478061184,
  playFailAnimationEarthDominant = 3410690176,
  playFailAnimationAirDominant = 3393847424,
  playFailAnimationNeutralDominant = 479529344,
  playRegret = 1423179904,
  flashNumOfBeers = 3107520640,
  incrementNumOfBeers = 2686451840,
  decrementNumOfBeers = 2703294592,
  resurrect = 3073835136,
  kill = 1406337152
}; // enum for translating IR commands to game events

struct settings {
  volatile teamColour currentTeam = TEAM_DEFAULT;
  volatile byte beerCount = BEER_COUNT_DEFAULT;
  volatile boolean dead = false;
} localSettings;

CPanasonic IRLremote; // shitty ir remote uses Nec protocol

volatile wake wake_source = def; // allows us to handle different wakeup sources

ISR(WDT_vect) {
  wdt_disable(); // disable watchdog
  pinChangeInteruptDisable(); // disable pinchange
  sleep_disable(); // disable sleep
  wake_source = dog; // change wake source to dog
}

ISR(PCINT0_vect) {
  wdt_disable(); // disable watchdog
  pinChangeInteruptDisable(); // disable pinchange
  sleep_disable(); // disable sleep
  wake_source = ir; // change wake source to ir
}

/**
    Enable watchdog timer
*/
void watchdogEnable() {
#if defined(ARDUINO_attiny) || defined(ARDUINO_AVR_DIGISPARK)
  WDTCR = bit(WDCE) | bit(WDE); // allow changes to wdt, disable reset
  WDTCR = bit(WDIE) | bit(WDP3) | bit(WDP0); // set wdt to interupt mode, with 8 seconds delay
#endif
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  WDTCSR = bit (WDCE) | bit (WDE); // allow changes to wdt, disable reset
  WDTCSR = bit (WDIE) | bit (WDP3) | bit (WDP0); // set wdt to interupt mode, with 8 seconds delay
#endif
  wdt_reset(); // kick the dog
}

/**
    Enable pinchangeinterupt
*/
void pinChangeInteruptEnable() {
#if defined(ARDUINO_attiny) || defined(ARDUINO_AVR_DIGISPARK)
  GIMSK |= (1 << PCIE); // pin change interrupt enable
  PCMSK |= (1 << PCINT2); // pin change interrupt enabled for PCINT2
#endif
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  PCMSK0 |= (1 << PCINT2); // pin change interrupt enable for PCINT2
  PCIFR = 0x1; // pin change interrupt flag
#endif
}

/**
    Disable pinchangeinterupt
*/
void pinChangeInteruptDisable() {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  PCMSK0 |= (0 << PCINT2); // pin change interrupt disable for PCINT2
  PCIFR = 0x0; // pin change interrupt flag
#endif
#if defined(ARDUINO_attiny) || defined(ARDUINO_AVR_DIGISPARK)
  GIMSK |= (0 << PCIE); // pin change interrupt disable
  PCMSK |= (0 << PCINT2); // pin change interrupt disable for PCINT2
#endif
}

/**
    Handles putting the board to sleep with two wakeup sources, IR and watchdog timer
*/
void enterSleep() {
  showAnalogRGB(CRGB::Black); // turns off leds
  MCUSR = 0; // clear "reset" flags
  cli(); // disable interupts
  watchdogEnable();
  pinChangeInteruptEnable();
  sleep_enable(); // enable sleep
  sei(); // enable interupts
  sleep_cpu(); // actually sleep the cpu
}

/**
    On first run, default populate the eeeprom
*/
boolean firstRun() {
  FIRST_RUN_TYPE i;
  EEPROM.get(FIRST_RUN_ADDR, i);
  if (i != 1) {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
    Serial.println("First Run");
#endif
    EEPROM.put(LOCAL_SETTINGS_ADDR, localSettings);
    EEPROM.put(FIRST_RUN_ADDR, 1);
    return true;
  }
  return false;
}

/**
    Loads the settings from eeprom
*/
void loadSettings() {
  EEPROM.get(LOCAL_SETTINGS_ADDR, localSettings);
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  Serial.println("Loaded Settings");
#endif
}

/**
    Updates the eeprom settings
*/
void updateSettings() {
  EEPROM.put(LOCAL_SETTINGS_ADDR, localSettings);
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  Serial.println("Updated Settings");
#endif
}

/**
    Switch team
*/
void switchTeam(teamColour newTeam) {
  if (localSettings.currentTeam != newTeam) {
    localSettings.currentTeam = newTeam;
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
    Serial.print("Team Switched: ");
    Serial.println(localSettings.currentTeam, HEX);
#endif
    updateSettings();
    flashTeam();
  }
}

void setDeath(boolean dead) {
  localSettings.dead = dead;
  updateSettings();
}

/**
    Pass a RGB structure and create it using each LED channel
*/
void showAnalogRGB(const CRGB & rgb) {
  // Common anode LED, so need to subtract
  analogWrite(RED_PIN, 255 - rgb.r / 8);
  analogWrite(GREEN_PIN, 255 - rgb.g / 6);
  analogWrite(BLUE_PIN, 255 - rgb.b);
}

/**
    Set LEDs to team colour at given brightness
*/
void showTeamColour(byte brightness) {
  if (localSettings.currentTeam == (teamColour) air) {
    showAnalogRGB( CHSV( localSettings.currentTeam, 200, brightness )); // desaturate air so it looks closer to white
  } else {
    showAnalogRGB( CHSV( localSettings.currentTeam, 255, brightness ));
  }
}

/**
    Flicker effect
*/
void flicker(int duration) {
  while (duration > 200) {
    showTeamColour(30 + random(225));
    int r = random(200);
    delay(r);
    duration -= r;
  }
  showTeamColour(30 + random(225));
  delay(duration);
}

/**
    Cycles through all team colours in a nice blend
*/
void neutralCycle() {
  for (byte j = 0; j < 255; j++) {
    showAnalogRGB((ColorFromPalette( teamPallet, j)));
    delay(50);
  }
}

/**
    Loosing animation -- flicker period gradually increases over 'stable' period until death.
*/
#define CYCLE_STEP_MS     (100)
#define CYCLE_STEP_DURATION_MS  (1100)

//Total duration = ( CYCLE_STEP_DURATION_MS * CYCLE_STEP_DURATION_MS ) / CYCLE_STEP_MS

void loose(teamColour dominantColour) {
  showTeamColour(255);
  if (dominantColour == localSettings.currentTeam) // dominant team gets to live (only a little bit longer!!)
    delay(20000);
  else {
    delay(5000);
  }
  int timer = CYCLE_STEP_DURATION_MS;

  while (timer > 0) {
    showTeamColour(255);
    delay(timer);
    flicker(CYCLE_STEP_DURATION_MS - timer);
    timer -= CYCLE_STEP_MS;
  }
  showAnalogRGB(CRGB::Black);
  setDeath(true);
}

void victory(byte regret) {
  if (regret == 0) { // regret means we already went through the loose scene and everyone wants to 'undo' their actions
    loose((teamColour) neutral); // make it look like we lost
  }
  // A dim white light (a ray of hope type shit)
  setDeath(false);
  CRGB ghost = CRGB::GhostWhite;
  showAnalogRGB(ghost.fadeToBlackBy(253));
  delay(10000);


  // Gradually increase brightness -- not quite sure why the transition to this is choppy
  for (byte i = 253; i > 0; i--) {
    CRGB white = CRGB::GhostWhite;
    showAnalogRGB(white.fadeToBlackBy(i));
    delay(50);

  }

  delay(5000);

  // Shift to first colour in pallet
  for (byte i = 0; i < 255; i++) {
    showAnalogRGB(blend(CRGB::GhostWhite, ColorFromPalette( teamPallet, 0), i));
    delay(10);
  }

  // Begin cycling through the team colour pallet
  neutralCycle();

  // Rainbow mix -- because everyone deserves more awesome
  CRGBPalette16 myPalette = RainbowStripeColors_p;

  // Shift to the more awesome mix
  for (byte i = 0; i < 255; i++) {
    showAnalogRGB(blend(ColorFromPalette( teamPallet, 255), ColorFromPalette( myPalette, 0), i));
    delay(10);
  }

  byte numOfCycles = 0;
  while (numOfCycles < 10) {
    // Cycle through the awesome mix
    for (byte i = 0; i < 255; i++) {
      showAnalogRGB(ColorFromPalette( myPalette, i));
      delay(50);
    }
    numOfCycles++;
  }

  //  Player becomes neutral
  switchTeam((teamColour) neutral);
}

/**
    Simple fade in and out of team colour
*/
void heartbeat() {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  Serial.println("Heartbeat");
#endif
  if (localSettings.dead == false) {
    if (localSettings.currentTeam == (teamColour) neutral) {
      neutralCycle();
    }
    else {
      for (byte i = 0; i < 255; i++) {
        showTeamColour(i);
        delay(2);
      }
      for (byte i = 255; i > 0; i--) {
        showTeamColour(i);
        delay(2);
      }
    }
  }
}

/**
    Cycle through team colours. Good for testing channels
*/
void colorBars() {
  showAnalogRGB(CRGB::Red);
  delay(500);
  showAnalogRGB(CRGB::Green);
  delay(500);
  showAnalogRGB(CRGB::Blue);
  delay(500);
  showAnalogRGB( CHSV( (teamColour) air, 200, 255) );
  delay(500);
}

/**
    Flashes your team
*/
void flashTeam() {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  Serial.println("Team Flash");
#endif
  if (localSettings.currentTeam == (teamColour) neutral) {
    colorBars();
  } else {
    for (byte i = 0; i < 2; i++) {
      showAnalogRGB(CRGB::Black);
      delay(500);
      showTeamColour(255);
      delay(500);
    }
  }
}

/**
    Increment beer allowance
*/
void incrementBeer() {
  if (localSettings.beerCount < 255) {
    localSettings.beerCount++;
    updateSettings();
  }
}

/**
    Decrement beer allowance
*/
void decrementBeer() {
  if (localSettings.beerCount > 0) {
    localSettings.beerCount--;
    updateSettings();
  }
}

/**
    Flashes your beer allowance
*/
void flashBeerCount() {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  Serial.println("Beer Flash");
#endif
  for (byte i = 0; i < localSettings.beerCount; i++) {
    showAnalogRGB(CRGB::Black);
    delay(500);
    showAnalogRGB(CRGB::White);
    delay(500);
  }
}

void handleIr() {
  auto data = IRLremote.read(); // this contains the IR data

#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  Serial.println("Handling IR");
#endif
  if (data.address == 0x2002) {
    switch (data.command) { // the command that was sent

      case (irCodes) changeTeamToFire:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Change team fire");
        #endif
        switchTeam((teamColour) fire);
        break;
      case (irCodes) changeTeamToEarth:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Change team earth");
        #endif
        switchTeam((teamColour) earth);
        break;
      case (irCodes) changeTeamToWater:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Change team water");
        #endif
        switchTeam((teamColour) water);
        break;
      case (irCodes) changeTeamToAir:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Change team air");
        #endif
        switchTeam((teamColour) air);
        break;
      case (irCodes) changeTeamToNeutral:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Change team neutral");
        #endif
        switchTeam((teamColour) neutral);
        break;

      case (irCodes) playVictoryAnimation:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Play victory");
        #endif
        victory(0);
        break;
      case (irCodes) playRegret:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Play Regret");
        #endif
        victory(1);
        break;

      case (irCodes) resurrect:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Resurrect");
        #endif
        setDeath(false);
        break;

      case (irCodes) kill:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Killed Player");
        #endif
        setDeath(true);
        break;
        
      case (irCodes) playFailAnimationFireDominant:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Fire dominant");
        #endif
        loose((teamColour) fire);
        break;
      case (irCodes) playFailAnimationWaterDominant:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Water dominant");
        #endif
        loose((teamColour) water);
        break;
      case (irCodes) playFailAnimationEarthDominant:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Earth dominant");
        #endif
        loose((teamColour) earth);
        break;
      case (irCodes) playFailAnimationAirDominant:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Air dominant");
        #endif
        loose((teamColour) air);
        break;
      case (irCodes) playFailAnimationNeutralDominant:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Neutral dominant");
        #endif
        loose((teamColour) neutral);
       break;

      case (irCodes) flashNumOfBeers:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Flash beer count");
        #endif
        flashBeerCount();
        break;

      case (irCodes) incrementNumOfBeers:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Increment number of beers");
        #endif
        incrementBeer();
        flashBeerCount();
        break;

      case (irCodes) decrementNumOfBeers:
        #if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
          Serial.println("Decrement number of beers");
        #endif
        decrementBeer();
        flashBeerCount();
        break;

      default:
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
        Serial.print("Unknown IR: ");
        Serial.println(data.command);
#endif
        break;
    }
  } else {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
    Serial.println("For your reference:");
    Serial.print("0x");
    Serial.println(data.address, HEX);
    Serial.println(data.command);
#endif
  }
}

void loop() {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  wake_source = ir; // needed to trap leonardo into a constant ir loop for testing
#endif

  switch (wake_source) {
    case dog:
      {
        heartbeat();  // call color bars
        wake_source = def;  // reset wake_source
        break;
      }
    case ir:
      {
        unsigned long startMillis = millis();
        // Window to recieve IR
        while (IRLremote.receiving() || ( (unsigned long)(millis() - startMillis) <= 2000)) {
          if (IRLremote.available())  // we have some ir data
          {
            handleIr();
            break;
          }
        }
        wake_source = def; // reset wake_source
        break;
      }
    default:
      {
        break;
      }
  }
#if defined(ARDUINO_attiny)
  enterSleep(); // power down cpu if atiny
#endif
}

void setup() {
#if defined(ARDUINO_AVR_UNO) || defined (ARDUINO_AVR_LEONARDO)
  Serial.begin(115200);
  while (!Serial.available());
#endif
  // Power Conservation
  adc_disable(); // ADC uses ~320uA, disable it
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // default sleep mode to power down (see sleep_mode vs sleep_cpu)

  if (!firstRun()) { // on first run set some defaults to eeprom.
    loadSettings(); // if not first run, load the settings from eeprom to local variable
  }

  IRLremote.begin(IR_PIN); // begin infrared handling
  pinMode(RED_PIN, OUTPUT); // set red pin to output
  pinMode(GREEN_PIN, OUTPUT); // set green pin to output
  pinMode(BLUE_PIN, OUTPUT); // set blue pin to output
  FastLED.setCorrection(Typical8mmPixel); // compensate colours for led type
  colorBars();
}
