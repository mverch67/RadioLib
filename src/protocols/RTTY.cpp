#include "RTTY.h"

ITA2String::ITA2String(char c) {
  _len = 1;
  _str = new char[1];
  _str[0] = c;
  _ita2Len = 0;
}

ITA2String::ITA2String(const char* str) {
  _len = strlen(str);
  _str = new char[_len];
  strcpy(_str, str);
  _ita2Len = 0;
}

ITA2String::~ITA2String() {
  delete[] _str;
}

size_t ITA2String::length() {
  // length returned by this method is different than the length of ASCII-encoded _str
  // ITA2-encoded string length varies based on how many number and characters the string contains
  
  if(_ita2Len == 0) {
    // ITA2 length wasn't calculated yet, call byteArr() to calculate it
    byteArr();
  }
  
  return(_ita2Len);
}

uint8_t* ITA2String::byteArr() {
  // create temporary array 2x the string length (figures may be 3 bytes)
  uint8_t* temp = new uint8_t[_len*2 + 1];
  
  size_t arrayLen = 0;
  bool flagFigure = false;
  for(size_t i = 0; i < _len; i++) {
    uint16_t code = getBits(_str[i]);
    uint8_t shift = (code >> 5) & 0b11111;
    uint8_t character = code & 0b11111;
    // check if the code is letter or figure
    if(shift == ITA2_FIGS) {
      // check if this is the first figure in sequence
      if(!flagFigure) {
        flagFigure = true;
        temp[arrayLen++] = ITA2_FIGS;
      }
      
      // add the character code
      temp[arrayLen++] = character & 0b11111;
      
      // check the following character (skip for message end)
      if(i < (_len - 1)) {
        uint16_t nextCode = getBits(_str[i+1]);
        uint8_t nextShift = (nextCode >> 5) & 0b11111;
        if(nextShift == ITA2_LTRS) {
          // next character is a letter, terminate figure shift
          temp[arrayLen++] = ITA2_LTRS;
          flagFigure = false;
        }
      } else {
        // reached the end of the message, terminate figure shift
        temp[arrayLen++] = ITA2_LTRS;
        flagFigure = false;
      }
    } else {
      temp[arrayLen++] = character & 0b11111;
    }
  }
  
  // save ITA2 string length
  _ita2Len = arrayLen;
  
  uint8_t* arr = new uint8_t[arrayLen];
  memcpy(arr, temp, arrayLen);
  delete[] temp;
  
  return(arr);
}

uint16_t ITA2String::getBits(char c) {
  // search ITA2 table
  uint16_t code = 0x0000;
  for(uint8_t i = 0; i < ITA2_LENGTH; i++) {
    if(ITA2Table[i][0] == c) {
      // character is in letter shift
      code = (ITA2_LTRS << 5) | i;
      break;
    } else if(ITA2Table[i][1] == c) {
      // character is in figures shift
      code = (ITA2_FIGS << 5) | i;
      break;
    }
  }
  
  return(code);
}

RTTYClient::RTTYClient(PhysicalLayer* phy) {
  _phy = phy;
}

int16_t RTTYClient::begin(float base, uint16_t shift, uint16_t rate, uint8_t encoding, uint8_t stopBits) {
  // check supplied values
  if(shift < 30) {
    return(ERR_INVALID_RTTY_SHIFT);
  }
  
  // clamp shift to multiples of 61 Hz (SX127x synthesis resolution)
  if(shift % 61 < 31) {
    _shift = shift / 61;
  } else {
    _shift = (shift / 61) + 1;
  }
  
  // save configuration
  _encoding = encoding;
  _stopBits = stopBits;
  
  switch(encoding) {
    case ASCII:
      _dataBits = 7;
      break;
    case ASCII_EXTENDED:
      _dataBits = 8;
      break;
    case ITA2:
      _dataBits = 5;
      break;
    default:
      return(ERR_UNSUPPORTED_ENCODING);
  }
  
  // calculate duration of 1 bit
  _bitDuration = (uint32_t)1000000/rate;
  
  // calculate 24-bit frequency
  uint32_t mult = 1;
  _base = (base * (mult << 19)) / 32.0;

  // set module frequency deviation to 0
  int16_t state = _phy->setFrequencyDeviation(0);
  
  return(state);
}

void RTTYClient::idle() {
  _phy->transmitDirect();
  
  mark();
}

size_t RTTYClient::write(const char* str) {
  if(str == NULL) {
    return(0);
  }
  return(RTTYClient::write((uint8_t *)str, strlen(str)));
}

size_t RTTYClient::write(uint8_t* buff, size_t len) {
  size_t n = 0;
  for(size_t i = 0; i < len; i++) {
    n += RTTYClient::write(buff[i]);
  }
  return(n);
}

size_t RTTYClient::write(uint8_t b) {
  space();
  
  for(uint16_t mask = 0x01; mask <= (uint16_t)(0x01 << (_dataBits - 1)); mask <<= 1) {
    if(b & mask) {
      mark();
    } else {
      space();
    }
  }
  
  for(uint8_t i = 0; i < _stopBits; i++) {
    mark();
  }
  
  return(1);
}

size_t RTTYClient::print(ITA2String& ita2) {
  uint8_t* arr = ita2.byteArr();
  size_t n = RTTYClient::write(arr, ita2.length());
  delete[] arr;
  return(n);
}

size_t RTTYClient::print(const String& str) {
  size_t n = 0;
  if(_encoding == ITA2) {
    ITA2String ita2 = str.c_str();
    n = RTTYClient::print(ita2);
  } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
    n = RTTYClient::write((uint8_t*)str.c_str(), str.length());
  }
  return(n);
}

size_t RTTYClient::print(const char str[]) {
  size_t n = 0;
  if(_encoding == ITA2) {
    ITA2String ita2 = str;
    n = RTTYClient::print(ita2);
  } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
    n = RTTYClient::write((uint8_t*)str, strlen(str));
  }
  return(n);
}

size_t RTTYClient::print(char c) {
  size_t n = 0;
  if(_encoding == ITA2) {
    ITA2String ita2 = c;
    n = RTTYClient::print(ita2);
  } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
    n = RTTYClient::write(c);
  }
  return(n);
}

size_t RTTYClient::print(unsigned char b, int base) {
  return(RTTYClient::print((unsigned long)b, base));
}

size_t RTTYClient::print(int n, int base) {
  return(RTTYClient::print((long)n, base));
}

size_t RTTYClient::print(unsigned int n, int base) {
  return(RTTYClient::print((unsigned long)n, base));
}

size_t RTTYClient::print(long n, int base) {
  if(base == 0) {
    return(RTTYClient::write(n));
  } else if(base == DEC) {
    if (n < 0) {
      int t = RTTYClient::print('-');
      n = -n;
      return(RTTYClient::printNumber(n, DEC) + t);
    }
    return(RTTYClient::printNumber(n, DEC));
  } else {
    return(RTTYClient::printNumber(n, base));
  }
}

size_t RTTYClient::print(unsigned long n, int base) {
  if(base == 0) {
    return(RTTYClient::write(n));
  } else {
    return(RTTYClient::printNumber(n, base));
  }
}

size_t RTTYClient::print(double n, int digits) {
  return(RTTYClient::printFloat(n, digits));
}

size_t RTTYClient::println(void) {
  size_t n = 0;
  if(_encoding == ITA2) {
    ITA2String lf = "\r\n";
    n = RTTYClient::print(lf);
  } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
    n = RTTYClient::write("\r\n");
  }
  return(n);
}

size_t RTTYClient::println(ITA2String& ita2) {
  size_t n = RTTYClient::print(ita2);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(const String& str) {
  size_t n = RTTYClient::print(str);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(const char* str) {
  size_t n = RTTYClient::print(str);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(char c) {
  size_t n = RTTYClient::print(c);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(unsigned char b, int base) {
  size_t n = RTTYClient::print(b, base);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(int num, int base) {
  size_t n = RTTYClient::print(num, base);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(unsigned int num, int base) {
  size_t n = RTTYClient::print(num, base);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(long num, int base) {
  size_t n = RTTYClient::print(num, base);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(unsigned long num, int base) {
  size_t n = RTTYClient::print(num, base);
  n += RTTYClient::println();
  return(n);
}

size_t RTTYClient::println(double d, int digits) {
  size_t n = RTTYClient::print(d, digits);
  n += RTTYClient::println();
  return(n);
}

void RTTYClient::mark() {
  uint32_t start = micros();
  _phy->transmitDirect(_base + _shift);
  while(micros() - start < _bitDuration);
}

void RTTYClient::space() {
  uint32_t start = micros();
  _phy->transmitDirect(_base);
  while(micros() - start < _bitDuration);
}

size_t RTTYClient::printNumber(unsigned long n, uint8_t base) {
  char buf[8 * sizeof(long) + 1];
  char *str = &buf[sizeof(buf) - 1];

  *str = '\0';

  if(base < 2) {
    base = 10;
  }

  do {
    char c = n % base;
    n /= base;

    *--str = c < 10 ? c + '0' : c + 'A' - 10;
  } while(n);
  
  size_t l = 0;
  if(_encoding == ITA2) {
    ITA2String ita2 = str;
    uint8_t* arr = ita2.byteArr();
    l = RTTYClient::write(arr, ita2.length());
    delete[] arr;
  } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
    l = RTTYClient::write(str);
  }
  
  return(l);
}

// TODO: improve ITA2 float print speed
//       (characters are sent one at a time)
size_t RTTYClient::printFloat(double number, uint8_t digits)  { 
  size_t n = 0;
  
  char code[] = {0x00, 0x00, 0x00, 0x00};
  if (isnan(number)) strcpy(code, "nan");
  if (isinf(number)) strcpy(code, "inf");
  if (number > 4294967040.0) strcpy(code, "ovf");  // constant determined empirically
  if (number <-4294967040.0) strcpy(code, "ovf");  // constant determined empirically
  
  if(code[0] != 0x00) {
    if(_encoding == ITA2) {
      ITA2String ita2 = code;
      uint8_t* arr = ita2.byteArr();
      n = RTTYClient::write(arr, ita2.length());
      delete[] arr;
      return(n);
    } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
      return(RTTYClient::write(code));
    }
  }
  
  // Handle negative numbers
  if (number < 0.0) {
    if(_encoding == ITA2) {
      ITA2String ita2 = "-";
      uint8_t* arr = ita2.byteArr();
      n += RTTYClient::write(arr, ita2.length());
      delete[] arr;
    } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
      n += RTTYClient::print('-');
    }
    number = -number;
  }

  // Round correctly so that print(1.999, 2) prints as "2.00"
  double rounding = 0.5;
  for(uint8_t i = 0; i < digits; ++i) {
    rounding /= 10.0;
  }
  number += rounding;

  // Extract the integer part of the number and print it
  unsigned long int_part = (unsigned long)number;
  double remainder = number - (double)int_part;
  n += RTTYClient::print(int_part);

  // Print the decimal point, but only if there are digits beyond
  if(digits > 0) {
    if(_encoding == ITA2) {
      ITA2String ita2 = ".";
      uint8_t* arr = ita2.byteArr();
      n += RTTYClient::write(arr, ita2.length());
      delete[] arr;
    } else if((_encoding == ASCII) || (_encoding == ASCII_EXTENDED)) {
      n += RTTYClient::print('.');
    }
  }

  // Extract digits from the remainder one at a time
  while(digits-- > 0) {
    remainder *= 10.0;
    unsigned int toPrint = (unsigned int)(remainder);
    n += RTTYClient::print(toPrint);
    remainder -= toPrint; 
  } 
  
  return n;
}
