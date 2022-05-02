/*

Portions Copyright 2012 Nick Gammon.

     PERMISSION TO DISTRIBUTE

     Permission is hereby granted, free of charge, to any person obtaining a copy of this software
     and associated documentation files (the "Software"), to deal in the Software without restriction,
     including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
     and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
     subject to the following conditions:

     The above copyright notice and this permission notice shall be included in
     all copies or substantial portions of the Software.


     LIMITATION OF LIABILITY

     The software is provided "as is", without warranty of any kind, express or implied,
     including but not limited to the warranties of merchantability, fitness for a particular
     purpose and noninfringement. In no event shall the authors or copyright holders be liable
     for any claim, damages or other liability, whether in an action of contract,
     tort or otherwise, arising from, out of or in connection with the software
     or the use or other dealings in the software.

*/

#include <SPI.h>
#include "ASM_ISP.h"

#ifndef STRIP_ABD

// programming commands to send via SPI to the chip
enum {
    progamEnable = 0xAC,
    programAcknowledge = 0x53,

    readSignatureByte = 0x30,
    readCalibrationByte = 0x38,

    readLowFuseByte = 0x50,       readLowFuseByteArg2 = 0x00,
    readExtendedFuseByte = 0x50,  readExtendedFuseByteArg2 = 0x08,
    readHighFuseByte = 0x58,      readHighFuseByteArg2 = 0x08,
    readLockByte = 0x58,          readLockByteArg2 = 0x00,

    readProgramMemory = 0x20,
    loadExtendedAddressByte = 0x4D,

} ;  // end of enum

// copy of fuses/lock bytes found for this processor
uint8_t fuses[5];

// meaning of bytes in above array
enum {
      lowFuse,
      highFuse,
      extFuse,
      lockByte,
      calibrationByte
};

const char fuse_0_label[] PROGMEM = "lfuse" ; const char fuse_1_label[] PROGMEM = "hfuse" ;
const char fuse_2_label[] PROGMEM = "efuse" ; const char fuse_3_label[] PROGMEM = "lock " ;
const char fuse_4_label[] PROGMEM = "clock calibration byte" ;
const char* const fuseLabels[] PROGMEM = { fuse_0_label, fuse_1_label, fuse_2_label, fuse_3_label, fuse_4_label };

// handler for special things like bootloader size
typedef void (*specialHandlerFunction) (const uint8_t val, const uint16_t bootLoaderSize);

// item for one piece of fuse information
typedef struct {
   uint8_t whichFuse;
   uint8_t mask;
   const char * meaningIfProgrammed;
   specialHandlerFunction specialHandler;
} fuseMeaning;

// Messages stored in PROGMEM to save RAM

const char descExternalResetDisable    [] PROGMEM = "External Reset Disable";
const char descDebugWireEnable         [] PROGMEM = "Debug Wire Enable";
const char descSerialProgrammingEnable [] PROGMEM = "Enable Serial (ICSP) Programming";
const char descWatchdogTimerAlwaysOn   [] PROGMEM = "Watchdog Timer Always On";
const char descEEPROMsave              [] PROGMEM = "Preserve EEPROM through chip erase";
const char descBootIntoBootloader      [] PROGMEM = "Boot into bootloader";
const char descDivideClockBy8          [] PROGMEM = "Divide clock by 8";
const char descClockOutput             [] PROGMEM = "Clock output";
const char descSelfProgrammingEnable   [] PROGMEM = "Self Programming Enable";
const char descHardwareBootEnable      [] PROGMEM = "Hardware Boot Enable";
const char descOCDEnable               [] PROGMEM = "OCD Enable";
const char descJtagEnable              [] PROGMEM = "JTAG Enable";
const char descOscillatorOptions       [] PROGMEM = "Oscillator Options";
const char descBrownOutDetectorEnable  [] PROGMEM = "Brown out detector enable";
const char descBrownOutDetectorLevel   [] PROGMEM = "Brown out detector level";

// calculate size of bootloader
void fBootloaderSize(const uint8_t val, const uint16_t bootLoaderSize)  {
  Serial.print(F("Bootloader size: "));
  uint16_t len = bootLoaderSize;
  switch (val & 3)  {
    case 0: len *= 8; break;
    case 1: len *= 4; break;
    case 2: len *= 2; break;
    case 3: len *= 1; break;
  }  // end of switch
  Serial.print(len); Serial.println(F(" bytes."));
} // end of fBootloaderSize

// show brownout level
void fBrownoutDetectorLevel(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Brownout detection at: "));
  switch(val & 3) {
    case 0b11: Serial.println(F("disabled."));   break;
    case 0b10: Serial.println(F("1.8V."));       break;
    case 0b01: Serial.println(F("2.7V."));       break;
    case 0b00: Serial.println(F("4.3V."));       break;
    default:   Serial.println(F("reserved."));   break;
  }  // end of switch
} // end of fBrownoutDetectorLevel

// show brownout level (alternative)
void fBrownoutDetectorLevelAtmega8U2(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Brownout detection at: "));
  switch (val) {
    case 0b111: Serial.println(F("disabled."));   break;
    case 0b110: Serial.println(F("2.7V."));       break;
    case 0b100: Serial.println(F("3.0V."));       break;
    case 0b011: Serial.println(F("3.5V."));       break;
    case 0b001: Serial.println(F("4.0V."));       break;
    case 0b000: Serial.println(F("4.3V."));       break;
    default:    Serial.println(F("reserved."));   break;
  }  // end of switch
} // end of fBrownoutDetectorLevelAtmega8U2

// show brownout level (alternative)
void fBrownoutDetectorLevelAtmega32U4(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Brownout detection at: "));
  switch (val) {
    case 0b111: Serial.println(F("disabled."));   break;
    case 0b110: Serial.println(F("2.0V."));       break;
    case 0b101: Serial.println(F("2.2V."));       break;
    case 0b100: Serial.println(F("2.4V."));       break;
    case 0b011: Serial.println(F("2.6V."));       break;
    case 0b010: Serial.println(F("3.4V."));       break;
    case 0b001: Serial.println(F("3.5V."));       break;
    case 0b000: Serial.println(F("4.3V."));       break;
    default:    Serial.println(F("reserved."));   break;
  }  // end of switch
} // end of fBrownoutDetectorLevelAtmega32U4


// show clock start-up times
void fStartUpTime(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Start-up time: SUT0:"));
  if ((val & 1) == 0)  // if zero, the fuse is "programmed"
    Serial.print(F(" [X]"));
  else
    Serial.print(F(" [ ]"));
  Serial.print(F("  SUT1:"));
  if ((val & 2) == 0)  // if zero, the fuse is "programmed"
    Serial.print(F(" [X]"));
  else
    Serial.print(F(" [ ]"));
  Serial.println(F(" (see datasheet)"));
} // end of fStartUpTime

// work out clock source
void fClockSource(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Clock source: "));
  switch (val) {
    case 0b1000 ... 0b1111: Serial.println(F("low-power crystal."));                    break;
    case 0b0110 ... 0b0111: Serial.println(F("full-swing crystal."));                   break;
    case 0b0100 ... 0b0101: Serial.println(F("low-frequency crystal."));                break;
    case 0b0011:            Serial.println(F("internal 128 KHz oscillator."));          break;
    case 0b0010:            Serial.println(F("calibrated internal oscillator."));       break;
    case 0b0000:            Serial.println(F("external clock."));                       break;
    default:                Serial.println(F("reserved."));                             break;
  }  // end of switch
} // end of fClockSource

// work out clock source (Atmega8A)
void fClockSource2(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Clock source: "));
  switch (val) {
    case 0b1010 ... 0b1111: Serial.println(F("low-power crystal."));                    break;
    case 0b1001           : Serial.println(F("low-frequency crystal."));                break;
    case 0b0101 ... 0b1000: Serial.println(F("external RC oscillator."));               break;
    case 0b0001 ... 0b0100: Serial.println(F("calibrated internal oscillator."));       break;
    case 0b0000:            Serial.println(F("external clock."));                       break;
    default:                Serial.println(F("reserved."));                             break;
  }  // end of switch
} // end of fClockSource2

// Decipher Lockbyte
void fLockBitMode(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Lockbit Mode: "));
  switch(val & 3) {
    case 0b00: Serial.println(F("3 - Further programming and verification disabled.")); break;
    case 0b01: Serial.println(F("undefined"));                                          break;
    case 0b10: Serial.println(F("2 - Further programming disabled."));                  break;
    case 0b11: Serial.println(F("1 - No memory lock features enabled."));               break;
    default:    Serial.println(F("reserved."));                                         break;
  }  // end of switch
} // end of fLockbitMode

// Decipher Lockbyte
void fBootLoaderProtection(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Boot Loader Protection Mode: "));
  switch(val & 3) {
    case 0b00: Serial.println(F("3 - LPM and SPM prohibited in Boot Loader Section.")); break;
    case 0b01: Serial.println(F("4 - LPM prohibited in Boot Loader Section."));         break;
    case 0b10: Serial.println(F("2 - SPM prohibited in Boot Loader Section."));         break;
    case 0b11: Serial.println(F("1 - No lock on SPM and LPM in Boot Loader Section.")); break;
    default:    Serial.println(F("reserved."));   break;
  }  // end of switch
} // end of fBootLoaderProtection

// Decipher Lockbyte
void fApplicationProtection(const uint8_t val, const uint16_t bootLoaderSize) {
  Serial.print(F("Application Protection Mode: "));
  switch(val & 3) {
    case 0b00: Serial.println(F("3 - LPM and SPM prohibited in Application Section.")); break;
    case 0b01: Serial.println(F("4 - LPM prohibited in Application Section."));         break;
    case 0b10: Serial.println(F("2 - SPM prohibited in Application Section."));         break;
    case 0b11: Serial.println(F("1 - No lock on SPM and LPM in Application Section.")); break;
    default:    Serial.println(F("reserved."));   break;
  }  // end of switch
} // end of fApplicationProtection

// fuses for various processors

const fuseMeaning ATmega48PA_fuses[] PROGMEM = {
  { extFuse,  0x01, descSelfProgrammingEnable },
  { highFuse, 0x80, descExternalResetDisable },
  { highFuse, 0x40, descDebugWireEnable },
  { highFuse, 0x20, descSerialProgrammingEnable },
  { highFuse, 0x10, descWatchdogTimerAlwaysOn },
  { highFuse, 0x08, descEEPROMsave },
  { lowFuse,  0x80, descDivideClockBy8 },
  { lowFuse,  0x40, descClockOutput },
  // special (combined) bits
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource },
  { highFuse, 0x07, NULL, fBrownoutDetectorLevel },
  { lockByte, 0x03, NULL, fLockBitMode },
};  // end of ATmega48PA_fuses

const fuseMeaning ATmega88PA_fuses[] PROGMEM = {
  { highFuse, 0x80, descExternalResetDisable },
  { highFuse, 0x40, descDebugWireEnable },
  { highFuse, 0x20, descSerialProgrammingEnable },
  { highFuse, 0x10, descWatchdogTimerAlwaysOn },
  { highFuse, 0x08, descEEPROMsave },
  { lowFuse,  0x80, descDivideClockBy8 },
  { lowFuse,  0x40, descClockOutput },
  { extFuse, 0x01, descBootIntoBootloader },
  // special (combined) bits
  { extFuse,  0x06, NULL, fBootloaderSize },
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource },
  { highFuse, 0x07, NULL, fBrownoutDetectorLevel },
  { lockByte, 0x03, NULL, fLockBitMode },
  { lockByte, 0x0C, NULL, fApplicationProtection },
  { lockByte, 0x30, NULL, fBootLoaderProtection },
};  // end of ATmega88PA_fuses

const fuseMeaning ATmega328P_fuses[] PROGMEM = {
  { highFuse, 0x80, descExternalResetDisable },
  { highFuse, 0x40, descDebugWireEnable },
  { highFuse, 0x20, descSerialProgrammingEnable },
  { highFuse, 0x10, descWatchdogTimerAlwaysOn },
  { highFuse, 0x08, descEEPROMsave },
  { highFuse, 0x01, descBootIntoBootloader },
  { lowFuse,  0x80, descDivideClockBy8 },
  { lowFuse,  0x40, descClockOutput },
  // special (combined) bits
  { highFuse, 0x06, NULL, fBootloaderSize },
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource },
  { extFuse,  0x07, NULL, fBrownoutDetectorLevel },
  { lockByte, 0x03, NULL, fLockBitMode },
  { lockByte, 0x0C, NULL, fApplicationProtection },
  { lockByte, 0x30, NULL, fBootLoaderProtection },
};  // end of ATmega328P_fuses

const fuseMeaning ATmega8U2_fuses[] PROGMEM = {
  { extFuse,  0x08, descHardwareBootEnable },
  { highFuse, 0x80, descDebugWireEnable },
  { highFuse, 0x40, descExternalResetDisable },
  { highFuse, 0x20, descSerialProgrammingEnable },
  { highFuse, 0x10, descWatchdogTimerAlwaysOn },
  { highFuse, 0x08, descEEPROMsave },
  { highFuse, 0x01, descBootIntoBootloader },
  { lowFuse,  0x80, descDivideClockBy8 },
  { lowFuse,  0x40, descClockOutput },
  // special (combined) bits
  { highFuse, 0x06, NULL, fBootloaderSize },
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource },
  { extFuse,  0x07, NULL, fBrownoutDetectorLevelAtmega8U2 },
  { lockByte, 0x03, NULL, fLockBitMode },
  { lockByte, 0x0C, NULL, fApplicationProtection },
  { lockByte, 0x30, NULL, fBootLoaderProtection },
};  // end of ATmega8U2_fuses

const fuseMeaning ATmega32U4_fuses[] PROGMEM = {
  { extFuse,  0x08, descHardwareBootEnable },
  { highFuse, 0x80, descOCDEnable },
  { highFuse, 0x40, descJtagEnable },
  { highFuse, 0x20, descSerialProgrammingEnable },
  { highFuse, 0x10, descWatchdogTimerAlwaysOn },
  { highFuse, 0x08, descEEPROMsave },
  { highFuse, 0x01, descBootIntoBootloader },
  { lowFuse,  0x80, descDivideClockBy8 },
  { lowFuse,  0x40, descClockOutput },
  // special (combined) bits
  { highFuse, 0x06, NULL, fBootloaderSize },
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource },
  { extFuse,  0x07, NULL, fBrownoutDetectorLevelAtmega32U4 },
  { lockByte, 0x03, NULL, fLockBitMode },
  { lockByte, 0x0C, NULL, fApplicationProtection },
  { lockByte, 0x30, NULL, fBootLoaderProtection },
};  // end of ATmega32U4_fuses

const fuseMeaning ATmega164P_fuses[] PROGMEM = {
  { highFuse, 0x80, descOCDEnable },
  { highFuse, 0x40, descJtagEnable },
  { highFuse, 0x20, descSerialProgrammingEnable },
  { highFuse, 0x10, descWatchdogTimerAlwaysOn },
  { highFuse, 0x08, descEEPROMsave },
  { highFuse, 0x01, descBootIntoBootloader },
  { lowFuse,  0x80, descDivideClockBy8 },
  { lowFuse,  0x40, descClockOutput },
  // special (combined) bits
  { highFuse, 0x06, NULL, fBootloaderSize },
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource },
  { extFuse,  0x07, NULL, fBrownoutDetectorLevel },
  { lockByte, 0x03, NULL, fLockBitMode },
  { lockByte, 0x0C, NULL, fApplicationProtection },
  { lockByte, 0x30, NULL, fBootLoaderProtection },
};  // end of ATmega164P_fuses

const fuseMeaning ATtiny4313_fuses[] PROGMEM = {
  { extFuse,  0x01, descSelfProgrammingEnable },
  { highFuse, 0x80, descDebugWireEnable },
  { highFuse, 0x40, descEEPROMsave },
  { highFuse, 0x20, descSerialProgrammingEnable },
  { highFuse, 0x10, descWatchdogTimerAlwaysOn },
  { highFuse, 0x01, descExternalResetDisable },
  { lowFuse,  0x80, descDivideClockBy8 },
  { lowFuse,  0x40, descClockOutput },
  // special (combined) bits
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource },
  { highFuse, 0x0E, NULL, fBrownoutDetectorLevel },
  { lockByte, 0x03, NULL, fLockBitMode },
};  // end of ATtiny4313_fuses

const fuseMeaning ATtiny13_fuses[] PROGMEM = {
  { highFuse, 0x10, descSelfProgrammingEnable },
  { highFuse, 0x08, descDebugWireEnable },
  { highFuse, 0x01, descExternalResetDisable },
  { lowFuse,  0x80, descSerialProgrammingEnable },
  { lowFuse,  0x40, descEEPROMsave },
  { lowFuse,  0x20, descWatchdogTimerAlwaysOn },
  { lowFuse,  0x10, descDivideClockBy8 },
  // special (combined) bits
  { lowFuse,  0x0C, NULL, fStartUpTime },
  { lowFuse,  0x03, NULL, fClockSource },
  { highFuse, 0x06, NULL, fBrownoutDetectorLevel },
  { lockByte, 0x03, NULL, fLockBitMode },
};  // end of ATtiny13_fuses

const fuseMeaning ATmega8_fuses[] PROGMEM = {
  { highFuse, 0x80, descExternalResetDisable },
  { highFuse, 0x40, descWatchdogTimerAlwaysOn },
  { highFuse, 0x20, descSelfProgrammingEnable },
  { highFuse, 0x10, descOscillatorOptions },
  { highFuse, 0x80, descEEPROMsave },
  { highFuse, 0x01, descBootIntoBootloader },
  { lowFuse,  0x80, descBrownOutDetectorLevel },
  { lowFuse,  0x40, descBrownOutDetectorEnable },
  // special (combined) bits
  { highFuse, 0x06, NULL, fBootloaderSize },
  { lowFuse,  0x30, NULL, fStartUpTime },
  { lowFuse,  0x0F, NULL, fClockSource2 },
  { lockByte, 0x03, NULL, fLockBitMode },
  { lockByte, 0x0C, NULL, fApplicationProtection },
  { lockByte, 0x30, NULL, fBootLoaderProtection },
};  // end of ATmega8_fuses

typedef struct {
   uint8_t           sig[3] ;
   char              *desc ;
   uint32_t           flashSize ;
   uint16_t           baseBootSize ;
   uint32_t           pageSize ;     // bytes
   uint8_t            fuseWithBootloaderSize ;  // ie. one of: lowFuse, highFuse, extFuse
   const fuseMeaning *fusesInfo;
   uint8_t           numberOfFuseInfo;
   uint8_t            timedWrites ;    // if pollUntilReady won't work by polling the chip
} signatureType ;

const uint32_t  kb = 1024 ;
const uint8_t NO_FUSE = 0xFF ;

// see Atmega datasheets
const signatureType signatures[] PROGMEM = {
//                                                 base        flash  fuse w/
//                                                 bootloader  page   actual
//     signature        description   flash size   size        size   bl size  fuse meaning      number of fuse definitions  timedWrites?
  // Attiny84 family
  { { 0x1E, 0x91, 0x0B }, "ATtiny24",   2 * kb,           0,   32,   NO_FUSE },
  { { 0x1E, 0x92, 0x07 }, "ATtiny44",   4 * kb,           0,   64,   NO_FUSE },
  { { 0x1E, 0x93, 0x0C }, "ATtiny84",   8 * kb,           0,   64,   NO_FUSE },
  // Attiny85 family
  { { 0x1E, 0x91, 0x08 }, "ATtiny25",   2 * kb,           0,   32,   NO_FUSE,  ATmega48PA_fuses, NUMITEMS(ATmega48PA_fuses) },      // same as ATmega48PA
  { { 0x1E, 0x92, 0x06 }, "ATtiny45",   4 * kb,           0,   64,   NO_FUSE,  ATmega48PA_fuses, NUMITEMS(ATmega48PA_fuses) },
  { { 0x1E, 0x93, 0x0B }, "ATtiny85",   8 * kb,           0,   64,   NO_FUSE,  ATmega48PA_fuses, NUMITEMS(ATmega48PA_fuses) },
  // Atmega328 family
  { { 0x1E, 0x92, 0x0A }, "ATmega48PA",   4 * kb,         0,    64,  NO_FUSE,  ATmega48PA_fuses, NUMITEMS(ATmega48PA_fuses) },
  { { 0x1E, 0x93, 0x0F }, "ATmega88PA",   8 * kb,       256,   128,  extFuse,  ATmega88PA_fuses, NUMITEMS(ATmega88PA_fuses) },
  { { 0x1E, 0x94, 0x0B }, "ATmega168PA", 16 * kb,       256,   128,  extFuse,  ATmega88PA_fuses, NUMITEMS(ATmega88PA_fuses) },      // same as ATmega88PA
  { { 0x1E, 0x95, 0x0F }, "ATmega328P",  32 * kb,       512,   128,  highFuse, ATmega328P_fuses, NUMITEMS(ATmega328P_fuses) },
  // Atmega644 family
  { { 0x1E, 0x94, 0x0A }, "ATmega164P",   16 * kb,      256,   128,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },
  { { 0x1E, 0x95, 0x08 }, "ATmega324P",   32 * kb,      512,   128,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },
  { { 0x1E, 0x96, 0x0A }, "ATmega644P",   64 * kb,   1 * kb,   256,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },
  // Atmega2560 family
  { { 0x1E, 0x96, 0x08 }, "ATmega640",    64 * kb,   1 * kb,   256,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },      // same as ATmega164P
  { { 0x1E, 0x97, 0x03 }, "ATmega1280",  128 * kb,   1 * kb,   256,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },      // same as ATmega164P
  { { 0x1E, 0x97, 0x04 }, "ATmega1281",  128 * kb,   1 * kb,   256,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },      // same as ATmega164P
  { { 0x1E, 0x98, 0x01 }, "ATmega2560",  256 * kb,   1 * kb,   256,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },      // same as ATmega164P
  { { 0x1E, 0x98, 0x02 }, "ATmega2561",  256 * kb,   1 * kb,   256,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses) },      // same as ATmega164P
  // AT90USB family
  { { 0x1E, 0x93, 0x82 }, "At90USB82",    8 * kb,       512,   128,  highFuse, ATmega8U2_fuses,  NUMITEMS(ATmega8U2_fuses)  },      // same as ATmega8U2
  { { 0x1E, 0x94, 0x82 }, "At90USB162",  16 * kb,       512,   128,  highFuse, ATmega8U2_fuses,  NUMITEMS(ATmega8U2_fuses)  },      // same as ATmega8U2
  // Atmega32U2 family
  { { 0x1E, 0x93, 0x89 }, "ATmega8U2",    8 * kb,       512,   128,  highFuse, ATmega8U2_fuses,  NUMITEMS(ATmega8U2_fuses)  },
  { { 0x1E, 0x94, 0x89 }, "ATmega16U2",  16 * kb,       512,   128,  highFuse, ATmega8U2_fuses,  NUMITEMS(ATmega8U2_fuses)  },      // same as ATmega8U2
  { { 0x1E, 0x95, 0x8A }, "ATmega32U2",  32 * kb,       512,   128,  highFuse, ATmega8U2_fuses,  NUMITEMS(ATmega8U2_fuses)  },      // same as ATmega8U2
  // Atmega32U4 family
  { { 0x1E, 0x94, 0x88 }, "ATmega16U4",  16 * kb,       512,   128,  highFuse, ATmega32U4_fuses, NUMITEMS(ATmega32U4_fuses)  },
  { { 0x1E, 0x95, 0x87 }, "ATmega32U4",  32 * kb,       512,   128,  highFuse, ATmega32U4_fuses, NUMITEMS(ATmega32U4_fuses)  },
  // ATmega1284P family
  { { 0x1E, 0x97, 0x05 }, "ATmega1284P", 128 * kb,   1 * kb,   256,  highFuse, ATmega164P_fuses, NUMITEMS(ATmega164P_fuses)  },     // same as ATmega164P
  // ATtiny4313 family
  { { 0x1E, 0x91, 0x0A }, "ATtiny2313A",   2 * kb,        0,    32,  NO_FUSE,  ATtiny4313_fuses, NUMITEMS(ATtiny4313_fuses)  },
  { { 0x1E, 0x92, 0x0D }, "ATtiny4313",    4 * kb,        0,    64,  NO_FUSE,  ATtiny4313_fuses, NUMITEMS(ATtiny4313_fuses)  },
  // ATtiny13 family
  { { 0x1E, 0x90, 0x07 }, "ATtiny13A",     1 * kb,        0,    32,  NO_FUSE,  ATtiny13_fuses,   NUMITEMS(ATtiny13_fuses)    },
  // Atmega8A family
  { { 0x1E, 0x93, 0x07 }, "ATmega8A",      8 * kb,      256,    64,  highFuse, ATmega8_fuses,    NUMITEMS(ATmega8_fuses)   , true },
} ;  // end of signatures

// if signature found in above table, this is its index
int8_t foundSig = -1 ;
uint8_t lastAddressMSB = 0 ;

// copy of current signature entry for matching processor
signatureType currentSignature ;

// for looking up known signatures
typedef struct {
   uint8_t md5sum[16] ;
   char const * filename ;
} deviceDatabaseType ;

// These are bootloaders we know about.

const char ATmegaBOOT_168_atmega328                  [] PROGMEM = "ATmegaBOOT_168_atmega328" ;
const char ATmegaBOOT_168_atmega328_pro_8MHz         [] PROGMEM = "ATmegaBOOT_168_atmega328_pro_8MHz" ;
const char ATmegaBOOT_168_atmega1280                 [] PROGMEM = "ATmegaBOOT_168_atmega1280" ;
const char ATmegaBOOT_168_diecimila                  [] PROGMEM = "ATmegaBOOT_168_diecimila" ;
const char ATmegaBOOT_168_ng                         [] PROGMEM = "ATmegaBOOT_168_ng" ;
const char ATmegaBOOT_168_pro_8MHz                   [] PROGMEM = "ATmegaBOOT_168_pro_8MHz" ;
const char ATmegaBOOT                                [] PROGMEM = "ATmegaBOOT" ;
const char ATmegaBOOT_168                            [] PROGMEM = "ATmegaBOOT_168" ;
const char ATmegaBOOT_168_atmega328_bt               [] PROGMEM = "ATmegaBOOT_168_atmega328_bt" ;
const char LilyPadBOOT_168                           [] PROGMEM = "LilyPadBOOT_168" ;
const char optiboot_atmega328_IDE_0022               [] PROGMEM = "optiboot_atmega328_IDE_0022" ;
const char optiboot_atmega328_pro_8MHz               [] PROGMEM = "optiboot_atmega328_pro_8MHz" ;
const char optiboot_lilypad                          [] PROGMEM = "optiboot_lilypad" ;
const char optiboot_luminet                          [] PROGMEM = "optiboot_luminet" ;
const char optiboot_pro_16MHz                        [] PROGMEM = "optiboot_pro_16MHz" ;
const char optiboot_pro_20mhz                        [] PROGMEM = "optiboot_pro_20mhz" ;
const char stk500boot_v2_mega2560                    [] PROGMEM = "stk500boot_v2_mega2560" ;
const char DiskLoader_Leonardo                       [] PROGMEM = "DiskLoader-Leonardo" ;
const char optiboot_atmega8                          [] PROGMEM = "optiboot_atmega8" ;
const char optiboot_atmega168                        [] PROGMEM = "optiboot_atmega168" ;
const char optiboot_atmega328                        [] PROGMEM = "optiboot_atmega328" ;
const char optiboot_atmega328_Mini                   [] PROGMEM = "optiboot_atmega328-Mini" ;
const char ATmegaBOOT_324P                           [] PROGMEM = "ATmegaBOOT_324P" ;
const char ATmegaBOOT_644                            [] PROGMEM = "ATmegaBOOT_644" ;
const char ATmegaBOOT_644P                           [] PROGMEM = "ATmegaBOOT_644P" ;
const char Mega2560_Original                         [] PROGMEM = "Mega2560_Original" ;
const char optiboot_atmega1284p                      [] PROGMEM = "optiboot_atmega1284p" ;
const char Ruggeduino                                [] PROGMEM = "Ruggeduino" ;
const char Leonardo_prod_firmware_2012_04_26         [] PROGMEM = "Leonardo-prod-firmware-2012-04-26" ;
const char atmega2560_bootloader_wd_bug_fixed        [] PROGMEM = "atmega2560_bootloader_watchdog_bug_fixed" ;
const char Caterina_Esplora                          [] PROGMEM = "Esplora" ;
const char Sanguino_ATmegaBOOT_644P                  [] PROGMEM = "Sanguino_ATmegaBOOT_644P" ;
const char Sanguino_ATmegaBOOT_168_atmega644p        [] PROGMEM = "Sanguino_ATmegaBOOT_168_atmega644p" ;
const char Sanguino_ATmegaBOOT_168_atmega1284p       [] PROGMEM = "Sanguino_ATmegaBOOT_168_atmega1284p" ;
const char Sanguino_ATmegaBOOT_168_atmega1284p_8m    [] PROGMEM = "Sanguino_ATmegaBOOT_168_atmega1284p_8m" ;
const char Arduino_dfu_usbserial_atmega16u2_Uno_Rev3 [] PROGMEM = "Arduino-dfu-usbserial-atmega16u2-Uno-Rev3" ;

// Signatures (MD5 sums) for above bootloaders
const deviceDatabaseType deviceDatabase[] PROGMEM = {
  { { 0x0A, 0xAC, 0xF7, 0x16, 0xF4, 0x3C, 0xA2, 0xC9, 0x27, 0x7E, 0x08, 0xB9, 0xD6, 0x90, 0xBC, 0x02,  }, ATmegaBOOT_168_atmega328 },
  { { 0x27, 0xEB, 0x87, 0x14, 0x5D, 0x45, 0xD4, 0xD8, 0x41, 0x44, 0x52, 0xCE, 0x0A, 0x2B, 0x8C, 0x5F,  }, ATmegaBOOT_168_atmega328_pro_8MHz },
  { { 0x01, 0x24, 0x13, 0x56, 0x60, 0x4D, 0x91, 0x7E, 0xDC, 0xEE, 0x84, 0xD1, 0x19, 0xEF, 0x91, 0xCE,  }, ATmegaBOOT_168_atmega1280 },
  { { 0x14, 0x61, 0xCE, 0xDF, 0x85, 0x46, 0x0D, 0x96, 0xCC, 0x41, 0xCB, 0x01, 0x69, 0x40, 0x28, 0x1A,  }, ATmegaBOOT_168_diecimila },
  { { 0x6A, 0x22, 0x9F, 0xB4, 0x64, 0x37, 0x3F, 0xA3, 0x0C, 0x68, 0x39, 0x1D, 0x6A, 0x97, 0x2C, 0x40,  }, ATmegaBOOT_168_ng },
  { { 0xFF, 0x99, 0xA2, 0xC0, 0xD9, 0xC9, 0xE5, 0x1B, 0x98, 0x7D, 0x9E, 0x56, 0x12, 0xC2, 0xA4, 0xA1,  }, ATmegaBOOT_168_pro_8MHz },
  { { 0x98, 0x6D, 0xCF, 0xBB, 0x55, 0xE1, 0x22, 0x1E, 0xE4, 0x3C, 0xC2, 0x07, 0xB2, 0x2B, 0x46, 0xAE,  }, ATmegaBOOT },
  { { 0x37, 0xC0, 0xFC, 0x90, 0xE2, 0xA0, 0x5D, 0x8F, 0x62, 0xEB, 0xAE, 0x9C, 0x36, 0xC2, 0x24, 0x05,  }, ATmegaBOOT_168 },
  { { 0x29, 0x3E, 0xB3, 0xB7, 0x39, 0x84, 0x2D, 0x35, 0xBA, 0x9D, 0x02, 0xF9, 0xC7, 0xF7, 0xC9, 0xD6,  }, ATmegaBOOT_168_atmega328_bt },
  { { 0xFC, 0xAF, 0x05, 0x0E, 0xB4, 0xD7, 0x2D, 0x75, 0x8F, 0x41, 0x8C, 0x85, 0x83, 0x56, 0xAA, 0x35,  }, LilyPadBOOT_168 },
  { { 0x55, 0x71, 0xA1, 0x8C, 0x81, 0x3B, 0x9E, 0xD2, 0xE6, 0x3B, 0xC9, 0x3B, 0x9A, 0xB1, 0x79, 0x53,  }, optiboot_atmega328_IDE_0022 },
  { { 0x3C, 0x08, 0x90, 0xA1, 0x6A, 0x13, 0xA2, 0xF0, 0xA5, 0x1D, 0x26, 0xEC, 0xF1, 0x4B, 0x0F, 0xB3,  }, optiboot_atmega328_pro_8MHz },
  { { 0xAD, 0xBD, 0xA7, 0x4A, 0x4F, 0xAB, 0xA8, 0x65, 0x34, 0x92, 0xF8, 0xC9, 0xCE, 0x58, 0x7D, 0x78,  }, optiboot_lilypad },
  { { 0x7B, 0x5C, 0xAC, 0x08, 0x2A, 0x0B, 0x2D, 0x45, 0x69, 0x11, 0xA7, 0xA0, 0xAE, 0x65, 0x7F, 0x66,  }, optiboot_luminet },
  { { 0x6A, 0x95, 0x0A, 0xE1, 0xDB, 0x1F, 0x9D, 0xC7, 0x8C, 0xF8, 0xA4, 0x80, 0xB5, 0x1E, 0x54, 0xE1,  }, optiboot_pro_16MHz },
  { { 0x2C, 0x55, 0xB4, 0xB8, 0xB5, 0xC5, 0xCB, 0xC4, 0xD3, 0x36, 0x99, 0xCB, 0x4B, 0x9F, 0xDA, 0xBE,  }, optiboot_pro_20mhz },
  { { 0x1E, 0x35, 0x14, 0x08, 0x1F, 0x65, 0x7F, 0x8C, 0x96, 0x50, 0x69, 0x9F, 0x19, 0x1E, 0x3D, 0xF0,  }, stk500boot_v2_mega2560 },
  { { 0xC2, 0x59, 0x71, 0x5F, 0x96, 0x28, 0xE3, 0xAA, 0xB0, 0x69, 0xE2, 0xAF, 0xF0, 0x85, 0xA1, 0x20,  }, DiskLoader_Leonardo },
  { { 0xE4, 0xAF, 0xF6, 0x6B, 0x78, 0xDA, 0xE4, 0x30, 0xFE, 0xB6, 0x52, 0xAF, 0x53, 0x52, 0x18, 0x49,  }, optiboot_atmega8 },
  { { 0x3A, 0x89, 0x30, 0x4B, 0x15, 0xF5, 0xBB, 0x11, 0xAA, 0xE6, 0xE6, 0xDC, 0x7C, 0xF5, 0x91, 0x35,  }, optiboot_atmega168 },
  { { 0xFB, 0xF4, 0x9B, 0x7B, 0x59, 0x73, 0x7F, 0x65, 0xE8, 0xD0, 0xF8, 0xA5, 0x08, 0x12, 0xE7, 0x9F,  }, optiboot_atmega328 },
  { { 0x7F, 0xDF, 0xE1, 0xB2, 0x6F, 0x52, 0x8F, 0xBD, 0x7C, 0xFE, 0x7E, 0xE0, 0x84, 0xC0, 0xA5, 0x6B,  }, optiboot_atmega328_Mini },
  { { 0x31, 0x28, 0x0B, 0x06, 0xAD, 0xB5, 0xA4, 0xC9, 0x2D, 0xEF, 0xB3, 0x69, 0x29, 0x22, 0xEA, 0xBF,  }, ATmegaBOOT_324P },
  { { 0xE8, 0x93, 0x44, 0x43, 0x37, 0xD3, 0x28, 0x3C, 0x7D, 0x9A, 0xEB, 0x84, 0x46, 0xD5, 0x45, 0x42,  }, ATmegaBOOT_644 },
  { { 0x51, 0x69, 0x10, 0x40, 0x8F, 0x07, 0x81, 0xC6, 0x48, 0x51, 0x54, 0x5E, 0x96, 0x73, 0xC2, 0xEB,  }, ATmegaBOOT_644P },
  { { 0xB9, 0x49, 0x93, 0x09, 0x49, 0x1A, 0x64, 0x6E, 0xCD, 0x58, 0x47, 0x89, 0xC2, 0xD8, 0xA4, 0x6C,  }, Mega2560_Original },
  { { 0x71, 0xDD, 0xC2, 0x84, 0x64, 0xC4, 0x73, 0x27, 0xD2, 0x33, 0x01, 0x1E, 0xFA, 0xE1, 0x24, 0x4B,  }, optiboot_atmega1284p },
  { { 0x0F, 0x02, 0x31, 0x72, 0x95, 0xC8, 0xF7, 0xFD, 0x1B, 0xB7, 0x07, 0x17, 0x85, 0xA5, 0x66, 0x87,  }, Ruggeduino },
  { { 0x53, 0xE0, 0x2C, 0xBC, 0x87, 0xF5, 0x0B, 0x68, 0x2C, 0x71, 0x13, 0xE0, 0xED, 0x84, 0x05, 0x34,  }, Leonardo_prod_firmware_2012_04_26 },
  { { 0x12, 0xAA, 0x80, 0x07, 0x4D, 0x74, 0xE3, 0xDA, 0xBF, 0x2D, 0x25, 0x84, 0x6D, 0x99, 0xF7, 0x20,  }, atmega2560_bootloader_wd_bug_fixed },
  { { 0x32, 0x56, 0xC1, 0xD3, 0xAC, 0x78, 0x32, 0x4D, 0x04, 0x6D, 0x3F, 0x6D, 0x01, 0xEC, 0xAE, 0x09,  }, Caterina_Esplora },
  { { 0x39, 0xCC, 0x80, 0xD6, 0xDE, 0xA2, 0xC4, 0x91, 0x6F, 0xBC, 0xE8, 0xDD, 0x70, 0xF2, 0xA2, 0x33,  }, Sanguino_ATmegaBOOT_644P },
  { { 0x60, 0x49, 0xC6, 0x0A, 0xE6, 0x31, 0x5C, 0xC1, 0xBA, 0xD7, 0x24, 0xEF, 0x8B, 0x6D, 0xE6, 0xD0,  }, Sanguino_ATmegaBOOT_168_atmega644p },
  { { 0xC1, 0x17, 0xE3, 0x5E, 0x9C, 0x43, 0x66, 0x5F, 0x1E, 0x4C, 0x41, 0x95, 0x44, 0x60, 0x47, 0xD5,  }, Sanguino_ATmegaBOOT_168_atmega1284p },
  { { 0x27, 0x4B, 0x68, 0x8A, 0x8A, 0xA2, 0x4C, 0xE7, 0x30, 0x7F, 0x97, 0x37, 0x87, 0x16, 0x4E, 0x21,  }, Sanguino_ATmegaBOOT_168_atmega1284p_8m },
  { { 0xD8, 0x8C, 0x70, 0x6D, 0xFE, 0x1F, 0xDC, 0x38, 0x82, 0x1E, 0xCE, 0xAE, 0x23, 0xB2, 0xE6, 0xE7,  }, Arduino_dfu_usbserial_atmega16u2_Uno_Rev3 },
} ;

uint8_t printProgStr(const char * str) {
  uint8_t count = 0;
  char c;
  if (str) {
    while ((c = pgm_read_byte(str++))) {
      Serial.print(c);
      count++;
    }
  }
  return count;
} // end of printProgStr

// execute one programming instruction ... b1 is command, b2, b3, b4 are arguments
//  processor may return a result on the 4th transfer, this is returned.
uint8_t program(const uint8_t b1, const uint8_t b2 = 0, const uint8_t b3 = 0, const uint8_t b4 = 0) {
  SPI.transfer(b1) ; SPI.transfer(b2) ; SPI.transfer(b3) ;
  return SPI.transfer(b4) ;
}

uint8_t readFlash(uint32_t  addr) {
  // set the extended (most significant) address byte if necessary
  uint8_t MSB = (addr >> 16) & 0xFF ;
  if (MSB != lastAddressMSB) {
    program(loadExtendedAddressByte, 0, MSB) ;
    lastAddressMSB = MSB ;
  }

  uint8_t high = (addr & 1) ? 0x08 : 0 ;  // set if high byte wanted
  addr >>= 1 ;  // turn into word address
  return program(readProgramMemory | high, highByte(addr), lowByte(addr)) ;
}

void showHex(const uint8_t b, const boolean newline = false) {
  // try to avoid using sprintf
  char buf[4] = { ((b >> 4) & 0x0F) | '0', (b & 0x0F) | '0', ' ' , 0 } ;
  if (buf[0] > '9') buf[0] += 7 ;
  if (buf[1] > '9') buf[1] += 7 ;
  Serial.print(buf) ;
  if (newline) Serial.println() ;
}

void showBinary(const uint8_t b, const boolean newline = false) {
  char buf[10] = { ((b >> 7) & 0x01) | '0', ((b >> 6) & 0x01) | '0', ((b >> 5) & 0x01) | '0', ((b >> 4) & 0x01) | '0',
                   ((b >> 3) & 0x01) | '0', ((b >> 2) & 0x01) | '0', ((b >> 1) & 0x01) | '0',        (b & 0x01) | '0',
                  ' ', 0 } ;
  Serial.print(buf) ;
  if (newline) Serial.println() ;
}

void showYesNo(const boolean b, const boolean newline = false) {
  Serial.print(b ? F("Yes") : F("No")) ;
  if (newline) Serial.println() ;
}

void readBootloader() {
  uint32_t  addr ;
  uint16_t  len ;

  if (currentSignature.baseBootSize == 0) {
    Serial.println(F("No bootloader support.")) ;
    return ;
  }

  uint8_t fusenumber = currentSignature.fuseWithBootloaderSize ;
  uint8_t whichFuse ;
  uint8_t hFuse = program(readHighFuseByte, readHighFuseByteArg2) ;

  switch (fusenumber) {
    case lowFuse:
      whichFuse = program(readLowFuseByte, readLowFuseByteArg2) ; break ;
    case highFuse:
      whichFuse = program(readHighFuseByte, readHighFuseByteArg2) ; break ;
    case extFuse:
      whichFuse = program(readExtendedFuseByte, readExtendedFuseByteArg2) ; break ;
    default:
      Serial.println(F("No bootloader fuse.")) ;
      return ;
  }

  addr = currentSignature.flashSize ;
  len = currentSignature.baseBootSize ;

//  Serial.print(F("Bootloader in use: ")) ; showYesNo((whichFuse & bit(0)) == 0, true) ;
//  Serial.print(F("EEPROM preserved through erase: ")) ; showYesNo((hFuse & bit(3)) == 0, true) ;
//  Serial.print(F("Watchdog timer always on: ")) ; showYesNo((hFuse & bit(4)) == 0, true) ;

  // work out bootloader size
  // these 2 bits basically give a base bootloader size multiplier
  switch ((whichFuse >> 1) & 3) {
    case 0: len *= 8 ; break ;
    case 1: len *= 4 ; break ;
    case 2: len *= 2 ; break ;
    case 3: len *= 1 ; break ;
  }

  // where bootloader starts
  addr -= len ;

  Serial.println() ;
  Serial.print(F("Bootloader is ")) ;  Serial.print(len) ;
  Serial.print(F(" bytes starting at ")) ;  Serial.print(addr, HEX) ;
  Serial.println(F(":")) ;

  for(uint16_t i = 0 ; i < len ; i++) {
    // show address
    if (i % PROG_DUMP_WIDTH == 0) {
      Serial.print(addr + i, HEX) ;
      Serial.print(F(" : ")) ;
    }
    showHex(readFlash(addr + i), (i % PROG_DUMP_WIDTH == (PROG_DUMP_WIDTH - 1))) ;
  }
  Serial.print(F("MD5 sum:  ")) ;

  md5_context ctx ;
  uint8_t md5sum[16] ;
  uint8_t mem ;
  bool allFF = true ;

  md5_starts(&ctx) ;

  while (len--) {
    mem = readFlash(addr++) ;
    if (mem != 0xFF) allFF = false ;
    md5_update(&ctx, &mem, 1) ;
  }

  md5_finish(&ctx, md5sum) ;

  for(uint16_t i = 0 ; i < sizeof md5sum ; i++) showHex(md5sum[i], ((i+1) == sizeof md5sum)) ;

  if (allFF) Serial.println(F("No bootloader (all 0xFF)")) ;
  else {
    bool found = false ;
    for(uint8_t i = 0 ; i < NUMITEMS(deviceDatabase) ; i++) {
      deviceDatabaseType dbEntry ;
      memcpy_P(&dbEntry, &deviceDatabase[i], sizeof(dbEntry)) ;
      if (memcmp(dbEntry.md5sum, md5sum, sizeof md5sum) != 0) continue ;
      // found match!
      Serial.print(F("Bootloader name: ")) ;
      printProgStr(dbEntry.filename) ;
      Serial.println() ;
      found = true ;
      break ;
    }

    if (!found) Serial.println(F("Bootloader MD5 sum not known.")) ;
  }
}

void readProgram() {
  uint32_t  addr = 0 ;
  uint16_t  len = 256 ;
  Serial.println() ; Serial.println(F("First 256 bytes of program memory:")) ;

  for(uint16_t i = 0 ; i < len ; i++) {
    if (i % PROG_DUMP_WIDTH == 0) {
      showHex(addr+i) ;
      Serial.print(F(": ")) ;
    }
    showHex(readFlash(addr + i), (i % PROG_DUMP_WIDTH == (PROG_DUMP_WIDTH - 1))) ;
  }
}

bool startProgramming()
  {
  Serial.print(F("Attempting to enter programming mode ...")) ;
  digitalWrite(RESET, HIGH) ;  // ensure SS stays high for now
  SPI.begin() ;
  SPI.setClockDivider(SPI_CLOCK_DIV64) ;

  uint8_t confirm ;

  pinMode(RESET, OUTPUT) ;
  pinMode(SCK, OUTPUT) ;

  uint8_t  timeout = 0 ;

  // we are in sync if we get back programAcknowledge on the third byte
  do {
    delay(100) ;

    // ensure SCK low then pulse reset, see page 309 of datasheet
    digitalWrite(SCK, LOW) ;
    digitalWrite(RESET, HIGH) ;
    delay(1) ;  // pulse for at least 2 clock cycles
    digitalWrite(RESET, LOW) ;

    delay(25) ;  // wait at least 20 mS
    SPI.transfer(progamEnable) ;
    SPI.transfer(programAcknowledge) ;
    confirm = SPI.transfer(0) ;
    SPI.transfer(0) ;

    if (confirm != programAcknowledge) {
      Serial.print(F(".")) ;
      if (timeout++ >= ENTER_PROGRAMMING_ATTEMPTS) {
        Serial.println(F(" Failed to enter programming mode. Double-check wiring!")) ;
        return false ;
      }
    }
  } while (confirm != programAcknowledge) ;

  Serial.println(F(" Entered programming mode.")) ;
  return true ;
}

void getSignature() {
  foundSig = -1 ;
  uint8_t sig[3] ;
  Serial.print(F("Signature = ")) ;
  for(uint8_t i = 0 ; i < 3 ; i++) {
    sig[i] = program(readSignatureByte, 0, i) ;
    showHex(sig[i], (i == 2)) ;
  }

  for(uint8_t j = 0 ; j < NUMITEMS(signatures) ; j++) {
    memcpy_P(&currentSignature, &signatures[j], sizeof currentSignature) ;

    if (memcmp(sig, currentSignature.sig, sizeof sig) == 0) {
      foundSig = j ;
      Serial.print(F("Processor = ")) ; Serial.println(currentSignature.desc) ;
      Serial.print(F("Flash memory size = ")) ; Serial.print(currentSignature.flashSize, DEC) ; Serial.println(F(" bytes.")) ;
      return ;
    }
  }
  Serial.println(F("Unrecogized signature.")) ;
}

void showFuseMeanings() {
  if (currentSignature.fusesInfo == NULL) {
     Serial.println(F("No fuse information for this processor."));
     return;
   } // end if no information

  for(uint8_t i = 0; i < currentSignature.numberOfFuseInfo; i++) {
    fuseMeaning thisFuse;

    // make a copy of this table entry to save a lot of effort
    memcpy_P(&thisFuse, &currentSignature.fusesInfo[i], sizeof thisFuse);

    // find the fuse value
    uint8_t val = thisFuse.whichFuse;
    // and which mask this entry is for
    uint8_t mask = thisFuse.mask;

    Serial.print(F("    ")) ;
    printProgStr((char*)pgm_read_word(&(fuseLabels[val]))) ; Serial.print(F(" & ")) ; showBinary(mask) ;
    Serial.print(F(":: ")) ;
    // if we have a description, show it
    if (thisFuse.meaningIfProgrammed) {
      uint8_t count = printProgStr(thisFuse.meaningIfProgrammed);
      while (count++ < 40) Serial.print(F("."));
      if ((fuses[val] & mask) == 0)  // if zero, the fuse is "programmed"
        Serial.println(F(" [X]"));
      else
        Serial.println(F(" [ ]"));
    }

    // some fuses use multiple bits so we'll call a special handling function
    if (thisFuse.specialHandler) {
      // get value into low-order bits
      uint8_t adjustedVal = fuses[val] & mask;
      while ((mask & 1) == 0) {
        adjustedVal >>= 1;
        mask >>= 1;
      }

      thisFuse.specialHandler(adjustedVal, currentSignature.baseBootSize);
    }
  }
}

void getFuseBytes() {
  fuses[lowFuse] = program(readLowFuseByte, readLowFuseByteArg2);
  fuses[highFuse] = program(readHighFuseByte, readHighFuseByteArg2);
  fuses[extFuse] = program(readExtendedFuseByte, readExtendedFuseByteArg2);
  fuses[lockByte] = program(readLockByte, readLockByteArg2);
  fuses[calibrationByte]  = program(readCalibrationByte);

  for(uint8_t i = 0 ; i < 5 ; i++) {
    printProgStr((char*)pgm_read_word(&(fuseLabels[i]))) ;
    Serial.print(F(":")) ;
//    showHex(fuses[i], (i & 2) | (i & 4)) ;
    showHex(fuses[i], (i > 2)) ;
  }
  showFuseMeanings() ;
}

void detectBoard() {
  Serial.begin(115200) ;
  while (!Serial) ;  // for Leonardo, Micro etc.

  Serial.println() ;

//  Serial.print(" DDRB: ") ; showBinary(DDRB) ; Serial.print("  PORTB: ") ; showBinary(PORTB) ;
//  Serial.print(" DDRC: ") ; showBinary(DDRC) ; Serial.print("  PORTC: ") ; showBinary(PORTC) ;
//  Serial.print(" DDRD: ") ; showBinary(DDRD) ; Serial.print("  PORTD: ") ; showBinary(PORTD, true) ;

  Serial.println(F("       ATmega chip detector and fuse calculator adapted from code by Nick Gammon")) ;
  Serial.println(F("Portions Copyright 2012 Nick Gammon. See https://github.com/nickgammon/arduino_sketches")) ;
  Serial.println(F("     Version " xstr(ABD_VERSION) "/" xstr(AFC_VERSION) ", Compiled on " __DATE__ " at " __TIME__ " with Arduino IDE " xstr(ARDUINO))) ;
  Serial.println() ;

  if (startProgramming()) {
    getSignature() ; getFuseBytes() ;

    if (foundSig != -1) readBootloader() ;

    readProgram() ;
  }   // end of if entered programming mode OK

  digitalWrite(RESET, HIGH) ;  //Disables reset line to secondary processor...

  SPI.end() ;

//  Serial.print(" DDRB: ") ; showBinary(DDRB) ; Serial.print("  PORTB: ") ; showBinary(PORTB) ;
//  Serial.print(" DDRC: ") ; showBinary(DDRC) ; Serial.print("  PORTC: ") ; showBinary(PORTC) ;
//  Serial.print(" DDRD: ") ; showBinary(DDRD) ; Serial.print("  PORTD: ") ; showBinary(PORTD, true) ;

 // only need to see output once, so wait till switch released.
  while (digitalRead(ABD_SELECTOR) == LOW) ;

  delay(20) ; // make sure serial output complete before we chop it off...
  softwareReset() ;
}

#endif
