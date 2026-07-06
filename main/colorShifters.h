#ifndef MAIN_UI_COMPONENTS_COLORSHIFTERS
#define MAIN_UI_COMPONENTS_COLORSHIFTERS
#ifndef COMPONENTS_INCLUDE_COLORSHIFTERS
#define COMPONENTS_INCLUDE_COLORSHIFTERS

#include <stdint.h>

/*
*   @brief: takes a rgb color string and return a rbg value
*
*  @param: hex_string representation of an rgb color
*  @returns: uint32 value in rbg order
*
*/
uint32_t hexstring2rbg(char * hex_string); 

/*
*   @brief: takes a rgb interger colors and return a rbg value
*
*  @params: infividual r, g, abd b color values 
*  @returns: uint32 value in rbg order
*
*/
uint32_t rgbs2rbg(int r, int g, int b);


/*
*   @brief: takes a rgb uint32 color and return a uint32 rbg value
*
*  @params: color in rgb order
*  @returns: uint32 value in rbg order
*
*/
uint32_t rgb2rbg(uint32_t rgb);


#endif  /* COMPONENTS_INCLUDE_COLORSHIFTERS */


#endif  /* MAIN_UI_COMPONENTS_COLORSHIFTERS */
