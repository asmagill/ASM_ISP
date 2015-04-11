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

#ifndef _ABD_H
#define _ABD_H

#include <avr/wdt.h>
extern "C" {
  #include "md5.h"
}

#define ENTER_PROGRAMMING_ATTEMPTS 10
#define PROG_DUMP_WIDTH 32

#define ABD_VERSION 1.13
#define AFC_VERSION 1.10

//#define softwareReset(x) asm volatile("jmp 0") // not recommended... use what follows instead
#define softwareReset(x)  do { wdt_enable(WDTO_15MS); for(;;); } while (0) ;

void detectBoard() ;

// stringification for Arduino IDE version
#define xstr(s) str(s)
#define str(s) #s

// number of items in an array
#define NUMITEMS(arg) ((uint8_t) (sizeof(arg) / sizeof(arg[0])))

#endif /* ABD.h */

