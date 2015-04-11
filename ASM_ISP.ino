// ASM_ISP.ino -- modified ArduinoISP

// Copyright 2015 Aaron Magill -- MIT LICENSE -- see LICENSE file for text of license
// Portions hold other copyrights -- see LICENSE file for details

// This has only been tested on an UNO R3.  If you run this on another platform or change pins,
// you'll probably want to comment out ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP in ASM_ISP.h initially
// to verify it works for you -- this is the only code that I know for certain deviates from the
// more generic sources I pulled code from.

// See ASM_ISP.h for the following:
//
// ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
//    This saves roughly 300 bytes, and was introduced before I was certain the Board Detector
//    and Fuse Calculator would safely fit together within 32k. It uses DDRx and PORTx manipulation
//    for most pin access, so if you change the pins, or use on an untested board,  then either
//    comment out this line or edit the appropriate places in ASM_ISP.ino and ABD.cpp.
//
// STRIP_ABD
//    To really cut down on space, strip the Board Detector and Fuse Calculator out... defeats
//    my purposes, but since I do include the SPI fixes and clock pin, someone else might want
//    just the AVRISP portion of this code...  Saves roughly 18000 bytes.
//
// pin name:    not-mega:         mega(1280 and 2560)
// slave reset: 10:               53
// MOSI:        11:               51
// MISO:        12:               50
// SCK:         13:               52
//
// CLOCK_1MHZ    3:               .kbv
//
// Put an LED (with resistor) on the following pins:
//  9: Heartbeat   - shows the programmer is running
//  8: Error       - Lights up if something goes wrong (use red if that makes sense)
//  7: Programming - In communication with the slave
// A0: Piezo speaker
//
//  6: ABD_SELECTOR  toggle to ground to acticvate board detector output

// Based upon code form the following sources --
//
// Arduino 1.6: example ArduinoISP.ino -- initial starting point
// 1 MHz on Pin 3 per code found in avrfreaks forum http://www.avrfreaks.net/comment/885558
// Sound, and buffered serial input from https://github.com/adafruit/ArduinoISP
// AtmelBoardDetector and AtmelFuseDecoder from
//     https://github.com/whiteneon/ArduinoISP_AtmelBoardDetector and
//     https://github.com/nickgammon/arduino_sketches
// Conversion to SPI library from https://github.com/rsbohn/ArduinoISP
//
// Under Consideration:
//     8MHz clock from https://github.com/adafruit/ArduinoISP
//     Target serial support ala https://github.com/cloudformdesign/ArduinoISP and/or
//         https://github.com/TheIronDuke5000/ArduinoISP-with-Serial-Debug
//     Low Speed from https://github.com/adafruit/ArduinoISP
//         "Can also rescue some bricked chips with bad oscillator fuses"
//          worth it? compatible with SPI library updates?
//     Add picture/schematic of board
//
// Probably not any more...
//     alt serial port for direct debug output (input?) to avrisp? was under consideration
//         before figuring out baud rate issue with SPI code.
//     https://github.com/MrCullDog/ArduinoISP4PICAXE 6 lines - easy enough ; just changes IO
//         lines but github changes look like they are incomplete -- one-way rather than 2-way
//         communication; will need actual PICAXE to test before considering...
//

#include <SPI.h>
#include "ASM_ISP.h"

// STK Definitions
const uint8_t STK_OK           = 0x10;
const uint8_t STK_FAILED       = 0x11;
const uint8_t STK_UNKNOWN      = 0x12;
const uint8_t STK_INSYNC       = 0x14;
const uint8_t STK_NOSYNC       = 0x15;
const uint8_t CRC_EOP          = 0x20; // ' '

const uint8_t STK_GET_SYNC     = 0x30; // '0'
const uint8_t STK_GET_SIGNON   = 0x31; // '1'
const uint8_t STK_GET_PARM     = 0x41; // 'A'
const uint8_t STK_SET_PARM     = 0x42; // 'B'
const uint8_t STK_SET_PARM_EXT = 0x45; // 'E'
const uint8_t STK_PMODE_START  = 0x50; // 'P'
const uint8_t STK_PMODE_END    = 0x51; // 'Q'
const uint8_t STK_SET_ADDR     = 0x55; // 'U'
const uint8_t STK_UNIVERSAL    = 0x56; // 'V'
const uint8_t STK_PROG_FLASH   = 0x60; // '`'
const uint8_t STK_PROG_DATA    = 0x61; // 'a'
const uint8_t STK_PROG_PAGE    = 0x64; // 'd'
const uint8_t STK_READ_PAGE    = 0x74; // 't'
const uint8_t STK_READ_SIGN    = 0x75; // 'u'

// Flags indicating status of Error and Programming LEDs
uint8_t error = 0, pmode = 0;

// SPI.end() doesn't return it's pins back to previous state after it's called... presumably
// not a huge problem, but does affect output voltage of at least pin 13 (UNO) on slave device,
// which was looked at during my testing for various blink patterns; so we store them in
// start_pmode() and restore in end_pmode().
uint8_t preSPI_DDRB, preSPI_PORTB;

// Addresses are weird b/c different platforms have different int size.
unsigned int _addr;         // Allow avrisp platforms int to be used, whatever size
uint8_t      _buffer[256];  // serial port buffer
uint8_t      buff[256];     // temporary serial read buffer
uint8_t      pBuffer = 0;   // buffer pointer -- needs to be big enough for buff size
uint8_t      iBuffer = 0;   // buffer index   -- needs to be big enough for buff size
boolean      EOP_SEEN = false;

#define beget16(addr) (*addr * 256 + *(addr+1) )
typedef struct param {
  uint8_t  devicecode;
  uint8_t  revision;
  uint8_t  progtype;
  uint8_t  parmode;
  uint8_t  polling;
  uint8_t  selftimed;
  uint8_t  lockbytes;
  uint8_t  fusebytes;
  uint8_t  flashpoll;
  uint16_t eeprompoll;
  uint16_t pagesize;    // was int... maybe to catch 16th bit as negative? but
                        // my understanding of possible values (256, 128, .., 32)
                        // means beget() will never set 16th bit. See write_flash()
  uint16_t eepromsize;
  uint32_t flashsize;
}
parameter;

parameter param;

// this provides a heartbeat on pin 9, so you can tell the software is running.
uint8_t hbval = 128;
int8_t hbdelta = 2;
void heartbeat() {
  if (hbval > 192) hbdelta = -hbdelta;
  if (hbval < 32) hbdelta = -hbdelta;
  hbval += hbdelta;
  analogWrite(LED_HB, hbval);
  delay(10);
  // delay(40);
}

uint8_t getch() {
  if (pBuffer == iBuffer) {  // spin until data available ???
    pulse(LED_ERR, 1);
    beep(1700, 20);
    error++;
    return -1;
  }
  uint8_t ch = _buffer[pBuffer];  // get next char
  pBuffer = (++pBuffer)%256;  // increment and wrap
  return ch;
}

void readbytes(uint8_t n) { //**
  for(uint8_t x = 0; x < n; x++) { //**
    buff[x] = getch();
  }
}

#define PTIME 30
// #define PTIME 50
void pulse(uint8_t pin, uint8_t times, uint32_t ptime) { //**
  do {
//#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP    // Not smaller than code below.
    digitalWrite(pin, HIGH);
    delay(ptime);
    digitalWrite(pin, LOW);
//#else
//    switch (pin) {
//      case 7: PORTD |= B10000000 ; break ;
//      case 8: PORTB |= B00000001 ; break ;
//      case 9: PORTB |= B00000010 ; break ;
//      default: break ;
//    }
//    delay(ptime) ;
//    switch (pin) {
//      case 7: PORTD &= ~B10000000 ; break ;
//      case 8: PORTB &= ~B00000001 ; break ;
//      case 9: PORTB &= ~B00000010 ; break ;
//      default: break ;
//    }
//#endif
    delay(ptime);
    times--;
  }
  while (times > 0);
}
void pulse(uint8_t pin, uint8_t times) { //**
  pulse(pin, times, PTIME);
}

void prog_lamp(uint8_t state) { //**
  if (PROG_FLICKER)
//#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP    // Not smaller than code below.
    digitalWrite(LED_PMODE, state);
//#else
//    PORTD = state ? (PORTD | B10000000) : (PORTD & ~B10000000) ;
//#endif
}

uint8_t spi_transaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  uint8_t n;
  SPI.transfer(a);
  n=SPI.transfer(b);
  //if (n != a) error = -1;
  n=SPI.transfer(c);
  return SPI.transfer(d);
}

void replyOK() {
//  if (EOP_SEEN == true) {
  if (CRC_EOP == getch()) {  // EOP should be next char
    Serial.write(STK_INSYNC);
    Serial.write(STK_OK);
  }
  else {
//    pulse(LED_ERR, 2);
    error++;
    Serial.write(STK_NOSYNC);
  }
}

void breply(uint8_t b) {
  if (CRC_EOP == getch()) {  // EOP should be next char
    Serial.write(STK_INSYNC);
    Serial.write(b);
    Serial.write(STK_OK);
  }
  else {
    error++;
    Serial.write(STK_NOSYNC);
  }
}

void get_parameter(uint8_t c) {
  switch (c) {
    case 0x80:
      breply(HWVER);
      break;
    case 0x81:
      breply(SWMAJ);
      break;
    case 0x82:
      breply(SWMIN);
      break;
    case 0x93:
      breply('S'); // serial programmer
      break;
    default:
      breply(0);
  }
}

void set_parameters() {
  // call this after reading paramter packet into buff[]
  param.devicecode = buff[0];
  param.revision   = buff[1];
  param.progtype   = buff[2];
  param.parmode    = buff[3];
  param.polling    = buff[4];
  param.selftimed  = buff[5];
  param.lockbytes  = buff[6];
  param.fusebytes  = buff[7];
  param.flashpoll  = buff[8];
  // ignore buff[9] (= buff[8])
  // following are 16 bits (big endian)
  param.eeprompoll = beget16(&buff[10]);
  param.pagesize   = beget16(&buff[12]);
  param.eepromsize = beget16(&buff[14]);

  // 32 bits flashsize (big endian)
  param.flashsize = buff[16] * 0x01000000
                    + buff[17] * 0x00010000
                    + buff[18] * 0x00000100
                    + buff[19];

}

void start_pmode() {
  preSPI_DDRB = DDRB ; preSPI_PORTB = PORTB ;
  SPI.begin() ;
#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
  digitalWrite(RESET, HIGH) ;
  pinMode(RESET, OUTPUT) ;
  pinMode(SCK, OUTPUT) ;
  delay(20);
  digitalWrite(RESET, LOW);
#else
  PORTB |= B00000100 ;
  DDRB |= B00000100 ;
  PORTB &= ~B00100000 ;
  delay(20);
  PORTB &= ~B00000100 ;
#endif
  spi_transaction(0xAC, 0x53, 0x00, 0x00);
  pmode = 1;
}

void end_pmode() {
  SPI.end();
#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
  digitalWrite(RESET, HIGH);
  pinMode(RESET, INPUT);
#else
  PORTB |= B00000100 ;
  DDRB &= ~B00000100 ;
#endif
  pmode = 0;
  DDRB = preSPI_DDRB ; PORTB = preSPI_PORTB ;
}

void universal() {
  uint8_t ch;
  readbytes(4);
  ch = spi_transaction(buff[0], buff[1], buff[2], buff[3]);
  breply(ch);
}

void flash(uint8_t hilo, uint32_t addr, uint8_t data) {
  spi_transaction(0x40 + 8 * hilo, addr >> 8 & 0xFF, addr & 0xFF, data);
}
void commit(uint32_t addr) {
  if (PROG_FLICKER) prog_lamp(LOW);
  spi_transaction(0x4C, (addr >> 8) & 0xFF, addr & 0xFF, 0);
  if (PROG_FLICKER) {
    delay(PTIME);
    prog_lamp(HIGH);
  }
}

//#define _current_page(x) (_addr & 0xFFFFE0)
uint32_t current_page(uint32_t addr) { //**
  if (param.pagesize == 32) return addr & 0xFFFFFFF0;
  if (param.pagesize == 64) return addr & 0xFFFFFFE0;
  if (param.pagesize == 128) return addr & 0xFFFFFFC0;
  if (param.pagesize == 256) return addr & 0xFFFFFF80;
  return addr;
}

uint8_t write_flash(uint16_t length) { //**
// // This makes no sense because even if pagesize is an int (as it was in the
// // original code), if I understand beget properly, no recognized page size would
// // set the 16th bit... must investigate further...  Guessing for now we just want
// // to make sure pagesize doesn't exceed buf[]?
//  if (param.pagesize < 1) { //**
//    return STK_FAILED;
//  }
  //if (param.pagesize != 64) return STK_FAILED; // legacy holdover?
  if (param.pagesize > 128) { //**
    return STK_FAILED;
  }
  uint32_t page = current_page(_addr); //**
  uint16_t x = 0; //**
  while (x < length) {
    if (page != current_page(_addr)) {
      commit(page);
      page = current_page(_addr);
    }
    flash(LOW, _addr, buff[x++]);
    flash(HIGH, _addr, buff[x++]);
    _addr++;
  }
  commit(page);
  return STK_OK;
}

uint8_t write_eeprom(uint8_t length) { //**
  // _addr is a word address, so we use _addr*2
  // this writes byte-by-byte,
  // page writing may be faster (4 bytes at a time)
  prog_lamp(LOW);
  for(uint8_t x = 0; x < length; x++) { //**
    uint32_t addr = _addr * 2 + x ;
    spi_transaction(0xC0, (addr >> 8) & 0xFF, addr & 0xFF, buff[x]);
    delay(45);
  }
  prog_lamp(HIGH);
  return STK_OK;
}

void program_page() {
  uint8_t result = STK_FAILED;
  uint16_t length = 256 * getch(); //**
  length += getch();
  if (length > 256) {
      error++;
      Serial.write(STK_FAILED);
      return;
  }
  char memtype = (char)getch();
  readbytes(length);
  if (CRC_EOP == getch()) {
    Serial.write(STK_INSYNC);
    if (memtype == 'F') result = write_flash(length);
    if (memtype == 'E') result = write_eeprom(length);
    Serial.write(result);
    if (result != STK_OK) {
      error++;
      Serial.write(STK_NOSYNC);
    }
  }
  else {
    error++;
    Serial.write(STK_NOSYNC);
  }
}

uint8_t flash_read(uint8_t hilo, uint16_t addr) {
  return spi_transaction(0x20 + hilo * 8, (addr >> 8) & 0xFF, addr & 0xFF, 0);
}

char flash_read_page(uint16_t length) {
  for(uint16_t x = 0; x < length; x+=2) {
    uint8_t low = flash_read(LOW, _addr);
    Serial.write(low);
    uint8_t high = flash_read(HIGH, _addr);
    Serial.write(high);
    _addr++;
  }
  return STK_OK;
}

char eeprom_read_page(uint16_t length) { //**
  // _addr again we have a word address
  for(uint16_t x = 0; x < length; x++) { //**
    uint8_t ee = spi_transaction(0xA0, 0x00, _addr * 2 + x, 0xFF);
    Serial.write(ee);
  }
  return STK_OK;
}

void read_page() {
  uint8_t result = (uint8_t)STK_FAILED;
  uint16_t length = 256 * getch(); //**
  length += getch();
  char memtype = getch();
  if (CRC_EOP != getch()) {
//    error++; ?? in original and SPI version, but not others... Do we really want the light on for this?
    Serial.write(STK_NOSYNC);
    return;
  }
  Serial.write(STK_INSYNC);
  if (memtype == 'F') result = flash_read_page(length);
  if (memtype == 'E') result = eeprom_read_page(length);
  Serial.write(result);
  return;
}

void read_signature() {
  if (CRC_EOP != getch()) {
    error++;
    Serial.write(STK_NOSYNC);
    return;
  }
  Serial.write(STK_INSYNC);
  uint8_t high = spi_transaction(0x30, 0x00, 0x00, 0x00);
  Serial.write(high);
  uint8_t middle = spi_transaction(0x30, 0x00, 0x01, 0x00);
  Serial.write(middle);
  uint8_t low = spi_transaction(0x30, 0x00, 0x02, 0x00);
  Serial.write(low);
  Serial.write(STK_OK);
}

void beep(uint16_t tone, uint16_t duration){ //**
  uint16_t elapsed = 0; //**
  while (elapsed < (duration * 10000)) {
#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
    digitalWrite(PIEZO, HIGH);
#else
    PORTC |= B00000001 ;  // A0
#endif
    delayMicroseconds(tone / 2);
#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
    digitalWrite(PIEZO, LOW);
#else
    PORTC &= ~B00000001 ;  // A0
#endif
    delayMicroseconds(tone / 2);
    // Keep track of how long we pulsed
    elapsed += tone;
  }
}

////////////////////////////////////
////////////////////////////////////

void avrisp() {
  uint8_t data, low, high;
  uint8_t avrch = getch();

  switch (avrch) {
    case STK_GET_SYNC:
                            error = 0;
                            replyOK();
                            break;
    case STK_GET_SIGNON:
                            if (getch() == CRC_EOP) {
                              Serial.write(STK_INSYNC);
                              Serial.write("AVR ISP");
                              Serial.write(STK_OK);
                            } else {
                              error++;
                              Serial.write(STK_NOSYNC);
                            }
                            break;
    case STK_GET_PARM:
                            get_parameter(getch());
                            break;
    case STK_SET_PARM:
                            readbytes(20);
                            set_parameters();
                            replyOK();
                            break;
    case STK_SET_PARM_EXT:
                            readbytes(5);
                            replyOK();
                            break;
    case STK_PMODE_START:
                            beep(3000, 50);
                            if (pmode) {
                              pulse(LED_ERR, 3);
                            } else {
                              start_pmode();
                            }
                            replyOK();
                            break;
    case STK_PMODE_END:
                            beep(1000, 50);
                            error = 0;
                            end_pmode();
                            replyOK();
                            break;
    case STK_SET_ADDR:
                            _addr = getch();
                            _addr += 256 * getch();
                            replyOK();
                            break;
    case STK_UNIVERSAL:
                            universal();
                            break;
    case STK_PROG_FLASH:
                            low = getch();
                            high = getch();
                            replyOK();
                            break;
    case STK_PROG_DATA:
                            data = getch();
                            replyOK();
                            break;
    case STK_PROG_PAGE:
                            // beep(1912, 5);      // Until I can get better control of buzzer, it's too damn noisy
                            program_page();
                            break;
    case STK_READ_PAGE:
                            read_page();
                            break;
    case STK_READ_SIGN:
                            read_signature();
                            break;

    case CRC_EOP:   // expecting a command, not CRC_EOP -- get back in sync
                            error++;
                            Serial.write(STK_NOSYNC);
                            break;
    default:        // anything else we will return STK_UNKNOWN
                            error++;
                            if (CRC_EOP == getch())
                              Serial.write(STK_UNKNOWN);
                            else
                              Serial.write(STK_NOSYNC);
  }
}

// Where real loop activities should go since getEOP is called from more than just loop()
// and doesn't hang on Serial.available like previous ArduinoISP versions sometimes did.

void getEOP() {
  uint16_t minL = 0; //**
  uint8_t  avrch = 0;
  while (!EOP_SEEN) {
    while (Serial.available()>0) {
      uint8_t ch = Serial.read();
      _buffer[iBuffer] = ch;
      iBuffer = (++iBuffer)%256;  // increment and wrap
      if (iBuffer == 1)  avrch = ch;  // save command
      if ((avrch == STK_PROG_PAGE) && (iBuffer==3)) {
        minL = 256*_buffer[1] + _buffer[2] + 4;
      }
      if ((iBuffer>minL) && (ch == CRC_EOP)) {
        EOP_SEEN = true;
      }
    }

    // Do loop stuff unless we have something to do -- that takes priority
    if (!EOP_SEEN) {
      // Real Loop stuff should go here

      heartbeat(); //delay(10) ;            // light the heartbeat LED

#ifndef STRIP_ABD
#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
      if (digitalRead(ABD_SELECTOR) == LOW && !pmode) { softwareReset(); }
#else
      if (!(PIND & B01000000) && !pmode) { softwareReset(); }
#endif /* ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP */
#endif /* STRIP_ABD */

    }
  }
}

////////////////////////////////////
////////////////////////////////////

void setup() {

#ifndef STRIP_ABD
  // Using WatchDogTimer for a more "accepted" software reset... see ABD.h
  // Needs to be as early as possible so watchdog is reset as soon as possible in case old bootloader doesn't.
  MCUSR=0; wdt_disable();
#endif

// Set up pins

#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
// Portable, but uses more bytes.  If pin defs change above, this will need to be used or code below changed.
  pinMode(LED_PMODE,    OUTPUT);
  pinMode(LED_ERR,      OUTPUT);
  pinMode(LED_HB,       OUTPUT);
  pinMode(PIEZO,        OUTPUT);
  pinMode(ABD_SELECTOR, INPUT_PULLUP);
  // .kbv these next statements provide a 1MHz clock signal on pin 3
  // DDRD |= (1 << 3);                    // make pin 3 an output (equiv to DDRD = DDRD | B00001000)
  pinMode(CLOCK_1MHZ, OUTPUT) ;           // same as above, but more portable, we don't care about timing yet, and don't have to worry about D versus B if CLOCK_1MHZ > 7)
#else
// Less portable, but saves space.  Change this if pins change, or comment out ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP at top of file
  DDRB  |= B00000011 ;                     // D9,D8 = OUTPUT
  DDRD = (DDRD | B10001000) & ~B01000000 ; // D7,D3 = OUTPUT, D6 = INPUT
  PORTD |= B01000000 ;                     // D6 = INPUT_PULLUP
  DDRC  |= B00000001 ;                     // A3 = OUTPUT
#endif

// End of pin setup

  // .kbv part of 1MHz clock signal on pin 3
  OCR2A = F_CPU/2/1000000 - 1;            // CTC toggle @ 1MHz
  OCR2B = OCR2A;                          // match B
  TCCR2A = (1 << COM2B0) | (1 << WGM21);  // Toggle OC2B in CTC mode
  TCCR2B = (1 << CS20);                   // run timer2 at div1

#ifndef STRIP_ABD

#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
  if (digitalRead(ABD_SELECTOR) == LOW) {
    digitalWrite(LED_PMODE, HIGH) ;
    digitalWrite(LED_ERR, HIGH) ;
    digitalWrite(LED_HB, HIGH) ;
#else
  if (!(PIND & B01000000)) {
    PORTB |= B00000011 ;
    PORTD |= B10000000 ;
#endif /* ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP */
    detectBoard();
  } else {
#endif /* STRIP_ABD */

// No, it's time to be an ISP

    Serial.begin(19200);
    SPI.setDataMode(0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV128); // Clock Div can be 2,4,8,16,32,64, or 128

    EOP_SEEN = false;      // Defaults set in definition above -- do we need to reset them here?
    iBuffer = pBuffer = 0; // Saves 20 bytes if we don't... need to see if this works across resets.

    beep(1500, 10);
    pulse(LED_PMODE, 2, 20);
    pulse(LED_ERR, 2, 20);
    pulse(LED_HB, 2, 20);
    beep(1500, 10);

#ifndef STRIP_ABD
  }
#endif

}

// Loopy loopy

void loop(void) {
//#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP    // Not smaller than code below.
  digitalWrite(LED_PMODE, pmode ? HIGH : LOW) ; // is pmode active?
  digitalWrite(LED_ERR,   error ? HIGH : LOW) ; // is there an error?
//#else
//  PORTD = pmode ? (PORTD | B10000000) : (PORTD & ~B10000000) ;
//  PORTB = error ? (PORTB | B00000001) : (PORTB & ~B00000001) ;
//#endif /* ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP */

  getEOP();  // <-- Put loop stuff like Heartbeat, ABD_SELECTOR check, etc. in getEOP() above.

  // have we received a complete request?  (ends with CRC_EOP)
  if (EOP_SEEN) {
#ifndef ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP
    digitalWrite(LED_PMODE, HIGH);
#else
  PORTD |= B10000000 ;
#endif /* ANAL_SPACE_SAVING_BUT_HARD_PIN_SETUP */
    EOP_SEEN = false;
    avrisp();
    iBuffer = pBuffer = 0;  // restart buffer
  }
}
