#ifndef _ASM_ISP_H
#define _ASM_ISP_H

// STRIP_ABD
//    To really cut down on space, strip the Board Detector and Fuse Calculator out... defeats
//    my purposes, but since I do include the SPI fixes and clock pin, someone else might want
//    just the AVRISP portion of this code...  Saves roughly 18000 bytes.
//#define STRIP_ABD

// Pins we use:
#define CLOCK_1MHZ   3
#define RESET       SS
#define LED_HB       9
#define LED_ERR      8
#define LED_PMODE    7
#define ABD_SELECTOR 6 /* button to trigger ABD output */
#define PIEZO       A0

#include "pins_arduino.h"  // defines SS,MOSI,MISO,SCK

#ifndef STRIP_ABD
#include "ABD.h"
#endif

#define PROG_FLICKER true

#define HWVER 2
#define SWMAJ 1
#define SWMIN 18

#endif /* _ASM_ISP_H */
