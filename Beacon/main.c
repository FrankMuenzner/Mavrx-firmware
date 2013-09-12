/*!
\file Beacon/main.c
\brief Beacon main file

On bootup, a startup function is run (part of the function library, and 
not	documented in this code.  The startuf function after initialising 
the hardware proceeds to invoke the function setup() followed by loop().

This file contains setup() and loop(), as well as some of the interrupt 
service routines that are set to run repeatedly.

\author Yuan Gao
*/

#include "all.h"

/*!
\brief This is the setup function, sets everything up.

This is the first user function called after the initialisation is complete.
All the hardware peripherals and stuff are initialised here.
*/
void setup() {
    // *** LED setup
    LEDInit(PLED);
    LEDOn(PLED);

    // *** XBee and MAVLink
    XBeeInit();
    MAVLinkInit();

    // *** GPS
    GPSInit();
    GPSSetRate(ID_NAV_POSLLH, 1); // every one nav solutions (i.e. as fast as possible)
    GPSSetRate(ID_NAV_STATUS, 3); // every three nav solutions
    GPSSetRate(ID_NAV_VELNED, 1);
	
    // *** Things start happening as soon as RIT is enabled!3
    RITInitms(1000/MESSAGE_LOOP_HZ);  // RIT at 25Hz
    LEDOff(PLED);
    LEDInit(VLED);
	Port0Init(PIN13 | PIN14 | PIN15 | PIN16);
	Port0SetOut(PIN13 | PIN14 | PIN15 | PIN16);
    PRGBlankTimer = 100;
    PRGPushTime = 0;
    PRGTimer = 0;
}

/*!
\brief Main loop (idle loop, doesn't do much)

This is the main loop, the processor sits in here when it's not dealing with 
interrupts.  This loop's only purpose right now is to deal with button presses,
and XBee modem status
*/
void loop() {
    if(PRGBlankTimer == 0) {
        if(PRGTimer > 3000) {
            flashVLED = 0;
            LEDOn(VLED);
        }
        
        // begin working out button presses
        if(PRGPushTime > 15000) {   // Factory reset the XBee
            flashVLED = 5;
            craftValid = 0;
            XBeeFactoryReset();
            
            PRGPushTime = 0;
            PRGTimer = 0;
            PRGBlankTimer = 100;
        }
        else if(PRGPushTime > 3000) { // Create new network
            flashVLED = XBEE_JOINPERIOD*10;
            craftValid = 0;
            XBeeCoordinatorJoin();
            PRGMode = 1;
            
            PRGPushTime = 0;
            PRGTimer = 0;
            PRGBlankTimer = 100;
        }
        // Mesh not supported
        /*else if(PRGPushTime > 50) {
            if(PRGMode == 0) {
                flashVLED = XBEE_JOINPERIOD*10;
                XBeeAllowJoin();
                PRGMode = 1;
            }
            else {
                flashVLED = 1;
                XBeeStopJoin();
                PRGMode = 0;
            }
            
            PRGPushTime = 0;
            PRGTimer = 0;
            PRGBlankTimer = 100;
        }*/
    }
    
    if(xbee_modem_status.isNew) {
        xbee_modem_status.isNew = 0;
        if(xbee_modem_status.status == 2) {
            allowTransmit = 1;
        }
        else if(xbee_modem_status.status == 3) {
            allowTransmit = 0;
        }
    }
}

/*!
\brief System Tick Timer, deals with timing, flashing, pushing.

This function is triggered by interrupt every 1ms, its purpose is to keep
timings, flash LEDs, time button pushes, and miscellaneous other timing needs
*/
void SysTickInterrupt(void) {
    // deal with button(s)
    if(PRGBlankTimer) {
        PRGBlankTimer--;
    }
    else {
        if(PRGPoll() == 0) {
            PRGTimer++;
            PRGLastState = 0;
        }
        else {
            if(PRGLastState == 0) {
                PRGPushTime = PRGTimer;
            }
            PRGTimer = 0;
            PRGLastState = 1;
        }
    }
    
    sysMS ++;
    sysUS += 1000;
    
    // deal with flashing LEDs
    if(sysMS % 100 == 0) {
        if(flashVLED > 0) {
            flashVLED--;
            LEDToggle(VLED);
            if(flashVLED == 0) LEDOff(VLED);
        }
    }
}

/*!
\brief Repetitive Interrput Timer, this is the main action loop.

This function is triggered by interrupt every few tens of ms (actual speed, is 
defined by MESSAGE_LOOP_HZ).
*/
void RITInterrupt(void) {
	static unsigned char heartbeatCounter=0;

    // *** Watchdogs
    // Incoming heartbeat watchdog
    heartbeatWatchdog++;
    if(heartbeatWatchdog >= MESSAGE_LOOP_HZ*XBEE_PANIC) {
        // heartbeat from ground control lost
        heartbeatWatchdog = MESSAGE_LOOP_HZ*(XBEE_PANIC+1); // prevent overflow
        //flashVLED = 5;
    }
	
    // GPS watchdog
    gpsWatchdog++;
    if(gpsWatchdog >= MESSAGE_LOOP_HZ*GPS_PANIC) {
        // GPS signal loss/no-fix
        gpsWatchdog = MESSAGE_LOOP_HZ*(GPS_PANIC+1); // prevent overflow
        gpsFixed = 0;
    }
    
	// *** Outgoing heartbeat
    heartbeatCounter++;
    if(heartbeatCounter >= MESSAGE_LOOP_HZ) { // 1Hz loop
        heartbeatCounter = 0;
        MAVSendHeartbeat();
    }
    
	// Telemetry
    if(allowTransmit) {
		
    }
	
	// GPS
    static double craft_X = 0;
    static double craft_Y = 0;
    static double craft_Z = 0;

	static unsigned short gpsFetchCounter = 0;
	gpsFetchCounter++;
	if(gpsFetchCounter > MESSAGE_LOOP_HZ/10) {
		gpsFetchCounter = 0; // fetch GPS at 10Hz
        
		// *** Process GPS
		XBeeInhibit(); // XBee input needs to be inhibited while processing GPS to avoid disrupting the I2C
		GPSFetchData();
		XBeeAllow();

        // *** Get GPS data
        if(gps_nav_status.isNew) {
            gps_nav_status.isNew = 0;

            if((gps_nav_status.gpsFix == 0x03 || gps_nav_status.gpsFix == 0x04) && gps_nav_status.flags & 0x1) { // fix is 3D and valid
                gpsFixed = 1;
            }
            else {
                gpsFixed = 0;
            }
            //mavlink_gps_raw_int.satellites_visible = gps_nav_sol.numSV;
        }

        if(gps_nav_posllh.isNew) {
            gps_nav_posllh.isNew = 0;

            if(gpsFixed == 1) {
                gpsWatchdog = 0;
                craft_X = gps_nav_posllh.lat / 10000000.0d;
                craft_Y = gps_nav_posllh.lon / 10000000.0d;
                craft_Z = (double)gps_nav_posllh.hMSL/ 1000.0d;
            }
        }

        if(gps_nav_velned.isNew) {
            gps_nav_velned.isNew = 0;

        }
	}
}

