/*
Bodgery CNC-Router Heads-Up Display

Rev 0:
    reads FM1 via an I2C ADC - a voltage proportional to the spindle frequency/speed
        ADC Model: Gravity: 0-10V 15-Bit Dual-Channel High-Precision ADC Module (using ADS1115)

    send a voltage between 0 and 10V to program the VFD
        DAC Model: Gravity 0-10 V 16 bit DAC 

    using a rotory encoder to change the desired spindle speed (  programs the to the DAC)
    
    monitors the T1 line for spindle turn on
        GPIO line 403 pulled up, looking for active low (toggles on any change)
        ISR is debounced
    accumulates the spindle time since bootup
        task running in background when spindle is on.
    displays relevant information

    spindle_active_task:
        monitors spindle state

    ADC_task:
        gets current frequency by reading the ADC and converting 

    DAC_task
        monitors its queu change changes the output voltag

    Rotory encoder task
        send new voltage to the DAC queue
    display_task:
        updates programmed RPM
        updated accumulated spindle time
        display spindle state

    relay_task:
        base on spindle state:
            turns on/off the relays
                chiller (0-10v)
                dust collector (0-10V)
                spindle (0-xV)
    */

/*
Rev 1
    monitors/displays sound levels
    MQTT logging
    scrape data from fob reader api
*/

Sparkfun qwik (I2C) cables
    Black = GND 
    Red = 3.3V 
    Blue = SDA 
    Yellow = SCL

Adafruit (STEMMA) I2C
    Black = GND
    Red = V+
    White = SDA
    Green = SCL

NOTE:

LVGL expects color in RBG order, NOT RGB.   helper function rgb2rbg etc fizx that.