#include "colorShifters.h"
#include <stdio.h>

uint32_t hexstring2rbg(char * hex_string) { 
    
    int r, g, b;

    // %2x reads exactly 2 hexadecimal characters into each variable
    int parsed = sscanf(hex_string, "%2x%2x%2x", &r, &g, &b);
    if (parsed != 3) return 0;
    return rgbs2rbg(r,g,b);

}



uint32_t rgbs2rbg(int r, int g, int b) {
    return (r << 16) | (b << 8) | g;
}

uint32_t rgb2rbg(uint32_t rgb) {
    uint8_t r, g, b;    
    r = rgb >> 16;
    g = (rgb >> 8) & 0xFF;
    b = rgb & 0xFF;

    return (r << 16) | (b << 8) | g;
}

