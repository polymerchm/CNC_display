
Bodgery CNC-Router Heads-Up Display

Rev 0:
    reads FM1 via an I2C ADC - a voltage proportional to the spindle frequency/speed
        ADC Model: Sparkfun ADS 1115

    send a voltage between 0 and 10V to program the VFD
        DAC Model: Sparkfun MCP4725 

    using a rotory encoder to change the desired spindle speed ( programs the to the DAC)
    
    monitors the T1 line for spindle turn on
        GPIO line 32 pulled up, looking for active low (toggles on any change)
        ISR is debounced
    accumulates the spindle time since bootup
        task running in background when spindle is on.
    displays relevant information

    spindle_active_task:
        monitors spindle state

    ADC_task:
        gets current frequency by reading the ADC and converting 

    DAC_task
        monitors its queue change changes the output voltage

    Rotory encoder task
        send new voltage to the DAC queue
    display_task:
        updates programmed RPM
        updated accumulated spindle time
        display spindle state

    relay_task:
        base on spindle state:
            turns on/off the relays
                chiller (0-5v)
                dust collector (0-5V)
                red/yellow LED (DPDT switch in AUTO) (green when DPDT switch in OFF position)



Rev 1
    monitors/displays sound levels
    MQTT logging
    scrape data from fob reader api


Sparkfun qwik (I2C) cables
- Black = GND 
- Red = 3.3V 
- Blue = SDA 
- Yellow = SCL

Adafruit (STEMMA) I2C
- Black = GND
-  Red = 3.3v
-  White = SDA
-  Green = SCL

USB-C Data Cable
- Sheild - GND
- Blue - D+
- White - D-

Off/Auto Switch
- 1- Off 1 : LED_GREEN (green wire to GX12-4)
- 2- Common 1 :  VFD_COM (green wire from GX12-7)
- 3- Auto 1 : Relay #3 Common (orange wire )
- 4- Off 2 : VFD_DI1 (brown wire from GX-12-7)
- 5- Common 2 :  SPINDLE_CMD (white wire from GX1-2 spindle_CMD))
- 6- Auto 2 : NC

22/8 Cable (used for GX12-7 connector)
- Red - 1
- Black - 2
- White - 3
- Green - 4
- Yellow - 5
- Brown - 6
- Blue - 7

GX-12-2 connector (TechnoCNC Command) [ start motor relay on TechnoCNC controller]
- Pin 1 - left of alighment = Common (green wire  connected to GX-12 pin 6, green wire)
- Pin 2 - Right of alignment = Command (floating) - white wire 

GX12-4 connector
- Pin 1 - left of alighnment then CCW to Pin 4
- Pin 1 - LED_PLUS - blue wire  (to GX12-7, pin 5 (+24V))
- PIN 2 - LED_RED - red wire
- PIN 3 - LED_YELLOW - yellow wire
- PIN 4 - LED_GREEN - green wire

GX-4 Cabling
- Black - LED_PLUS 
- Red - LED_RED
- White - LED_GREEN
- Yellow - LED_YELLOW

GX12-3 connectors
Pin 1 right of the alignment slot then CW to  Pin 3
- Pin 1 - signal from relay to power strip enable (chiller yellow wire, dust collector blue wire)
- Pin 2 - NC
- Pin 3 - GND (white wire )                                                                )


GX12-7 Connector, As viewed from solder points
Pin 1 - left of alignment pin, then CCW around to Pin 6.  Pin 7 in the center.
- Pin 1 - VFD Run Sense (GPIO32) blue wire [ programmed relay on the VFD, closed when running ]
- Pin 2 - VFD FM1 (ADC in) green wire [input proportional to speed]
- Pin 3 - VFD VF1 (DAC out) orange wire [ouput proportional to desired frequency]
- Pin 4 - VFD GND (tied to system ground/neutral of power 5V power) white wire
- PIN 5 - VFD 24V (LED_PLUS) - blue to GX12-4 pin 1 [common anode for warning LEDs]
- Pin 6 - VFD Common (also to off/auto pin 2 ) green wire 
- Pin 7 - VFD DI1 (also to off/auto pin 4) brown wire [tell VFD to start the motor]

Rotory Encoder (ribbon cable)
- GND - grey (top pin)
- Vcc - brown +3.3v
- switch - blue GPIO_33
- dt - green GPIO_26
- clk - yellow GPIO_25

Display (ribbon cable)
- Vcc - +5v grey (on top)
- GND - violet
- CS -  GPIO_5    blue
- RST - GPIO_15   green
- DC  - GPIO_19   yellow
- MOSI - GPIO_23  orange
- SCK - GPIO_18   red
- LED - GPIO_02   brown
- MISO, touch and SD card - NC

QWIC cable 
- red - Vcc
- yellow - SCL             
- blue  - SDA
- black - GND


I2C Connections (QWIC-9 on ADC/DAC) 
- SDA - 21 
- SCL - 22
- GND - GND
- Vcc +3.3V on ESP32

I2C Connections (relay board)
- SDA - qwic connector on  ADC
- SCL - qwic connector on ADC
- GND - qwic connect on ADV
- Vcc - +5v on ESP32


NOTE:

- LVGL (using version 8.4 for SwquareLine Studio compatibility) expects color in RBG order, NOT RGB.   helper function rgb2rbg etc fixes that.