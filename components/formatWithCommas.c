#include <stdio.h>
#include <string.h>

void format_with_commas(long long num, char *output) {
    char temp[50];
    // Convert number to plain string first
    sprintf(temp, "%lld", num); 
    
    int len = strlen(temp);
    int commas = (len - 1) / 3;
    int new_len = len + commas;
    
    output[new_len] = '\0';
    
    int src = len - 1;
    int dst = new_len - 1;
    int count = 0;
    
    while (src >= 0) {
        if (count > 0 && count % 3 == 0) {
            output[dst--] = ',';
        }
        output[dst--] = temp[src--];
        count++;
    }
}