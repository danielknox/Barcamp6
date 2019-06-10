Needs the FastLED and IRLremote libraries. This can be compiled for attiny85 (production) or uno/leonardo (for debugging). 

I use https://raw.githubusercontent.com/damellis/attiny/ide-1.6.x-boards-manager/package_damellis_attiny_index.json. to compile for the ATtiny85 from the Arduino IDE:
- 8MHz internal clock.

HOWEVER! You need to modify the pins_arduino.h (appData/local/arduino15/packages/attiny/hardware/avr/1.0.2/variants/tiny8) to have the digitalPinToInterrupt macro.

Add this:
'#define digitalPinToInterrupt(p)  ((p) == 2 ? 0 : NOT_AN_INTERRUPT);
 