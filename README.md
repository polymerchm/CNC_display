/*
Bodgery CNC-Router Heads-Up Display

Rev 0:
    reads FM1 via an I2C ADC - a voltage proportional to the spindle frequency/speed
        ADC Model: Gravity: 0-10V 15-Bit Dual-Channel High-Precision ADC Module (using ADS1115)
    
    monitors the T1 line for spindle turn on
        GPIO line XXX pulled up, looking for active low (interups on negative)
    accumulates the spindle time since bootup
        task running in background when spindle is on.
    displays relevant information

    spindle_active_task:
        monitors spindle state

    ADC_task:
        gets current speed by reading the ADC and converting 
        the 0-10 V signal to 0-24,000 RPM
        calibrations for conversion of voltage to spped/frequencey stored in NVS

    display_task:
        updates programmed RPM
        updated accumulated spindle time
        display spindle state

    relay_task:
        base on spindle state:
            turns on/off the relays
                chiller (0-10v)
                dust collector (0-10V)
                heads up display (0-xV)
    */

/*
Rev 1
    monitors/displays sound levels
    MQTT logging
    scrape data from fob reader api
*/