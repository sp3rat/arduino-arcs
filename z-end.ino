/******************************************************************************
 *
 * This file is part of the Arduino-Arcs project, see
 *      https://github.com/pavelmc/arduino-arcs
 *
 * Copyright (C) 2016...2017 Pavel Milanes (CO7WT) <pavelmc@gmail.com>
 *
 * This program is free software under the GNU GPL v3.0
 *
 * ***************************************************************************/


// main setup function
void setup() {
    // set the defult values, before restoring theEEPROM ones
    setDefaultVals();

    #ifdef CAT_CONTROL
        // CAT Library setup
        cat.addCATPtt(catGoPtt);
        cat.addCATAB(catGoToggleVFOs);
        cat.addCATFSet(catSetFreq);
        cat.addCATMSet(catSetMode);
        cat.addCATGetFreq(catGetFreq);
        cat.addCATGetMode(catGetMode);
        cat.addCATSMeter(catGetSMeter);
        cat.addCATTXStatus(catGetTXStatus);
        // now we activate the library
        cat.begin(38400, SERIAL_8N2);
    #endif

    #ifdef LCD
        #ifdef SMETER
            // LCD init, create the custom chars first
            // depending on the selected smeter type
            #ifdef SMETER_ALT
                lcd.createChar(0, bar);
                lcd.createChar(1, s1);
                lcd.createChar(2, s3);
                lcd.createChar(3, s5);
                lcd.createChar(4, s7);
                lcd.createChar(5, s9);
            #else
                // default
                lcd.createChar(0, full);
                lcd.createChar(1, half);
            #endif
        #endif  // smeter

        // now load the library
        lcd.begin(16, 2);
        lcd.clear();
    #endif  // nolcd

    #ifdef ABUT
        // analog buttons setup
        abm.init(KEYS_PIN, 3, 20);
        abm.add(bvfoab);
        abm.add(bmode);
        abm.add(brit);
        abm.add(bsplit);

        // how many memories this chip supports
        #ifdef MEMORIES
            memCount = (EEPROM.length() - MEMSTART) / sizeof(mmem);
            // self limiting the mem amount to 100 (0-99)
            // we have only two chars on the LCD and 100 mems are a lot
            if (memCount > 99) memCount = 99;
        #endif
    #endif

    #ifdef ROTARY
        // buttons debounce & PTT
        pinMode(btnPush, INPUT_PULLUP);
        dbBtnPush.attach(btnPush);
        dbBtnPush.interval(debounceInterval);
        // pin mode of the PTT
        pinMode(inPTT, INPUT_PULLUP);
        dbPTT.attach(inPTT);
        dbPTT.interval(debounceInterval);
        // default awake mode is RX
    #endif  // ROTARY

    // set PTT at the start to LOW aka RX
    pinMode(PTT, OUTPUT);
    digitalWrite(PTT, 0);

    // I2C init
    Wire.begin();

    // check the EEPROM to know if I need to initialize it
    if (checkInitEEPROM()) {
        // just if it's already ok
        loadEEPROMConfig();
    } else {
        #ifdef LCD
            // full init, LCD banner
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Init EEPROM"));
            lcd.setCursor(0, 1);
            lcd.print(F("Please wait"));
        #endif  // nolcd

        // init eeprom.
        saveEEPROM();

        #ifdef MEMORIES
            wipeMEM();
        #endif

        // make a smart delay
        smartDelay();

        #ifdef LCD
            lcd.clear();
        #endif  // nolcd
    }

    #ifdef LCD
        // Welcome screen
        lcd.clear();
        lcd.print(F("  Aduino Arcs  "));
        lcd.setCursor(0, 1);
        lcd.print(F("Fv: "));
        lcd.print(FMW_VER);
        lcd.print(F("  Mfv: "));
        lcd.print(EEP_VER);
    #endif  // nolcd

    // A software controlling the CAT via USB will reset the sketch upon
    // connection, so we need turn the cat.check() when running the welcome
    // banners (use case: Fldigi)

    // make a smart delay
    smartDelay();

    #ifdef LCD
        lcd.setCursor(0, 0);
        lcd.print(F(" by Pavel CO7WT "));
    #endif  // nolcd

    // make a smart delay
    smartDelay();

    #ifdef LCD
        lcd.clear();
    #endif  // nolcd

    #ifdef ROTARY   // check for SETUP mode
        // if you have no analog buttons then
        // there is no meaning for the setup mode
        #ifdef ABUT
            // Check for setup mode
            if (digitalRead(btnPush) == LOW) {
                // sound signal
                beep();
                delay(50);
                beop();

                // rise the flag of setup mode for every body to see it.
                runMode = false;

                // beep signal to the user
                tone(4, 800, 250);
                delay(250);

                #ifdef CAT_CONTROL
                    // CAT is disabled in SETUP mode
                    cat.enabled = false;
                #endif

                #ifdef LCD
                    // we are in the setup mode
                    lcd.setCursor(0, 0);
                    lcd.print(F(" You are in the "));
                    lcd.setCursor(0, 1);
                    lcd.print(F("   SETUP MODE   "));
                    delay(2000);
                    lcd.clear();
                    // show setup mode
                    showConfig();
                #endif  // lcd
            }
        #endif // abut
    #endif  // rotary

    // setting up VFO A as principal.
    activeVFO = true;
    ptrVFO = &u.a;
    ptrMode = &u.aMode;

    // init the wire lib
    Wire.begin();

    // start the VFOa and it's mode
    updateAllFreq();

    // reset the Si5351 for the forst time
    Si5351_resets();
}


// forever loop
void loop() {
    #ifdef ROTARY
        // encoder check
        encoderState = encoder.process();
        if (encoderState == DIR_CW)  encoderMoved(+1);
        if (encoderState == DIR_CCW) encoderMoved(-1);
    #endif  // rotary

    // LCD update check in normal mode
    if (update and runMode) {
        #ifdef LCD
            // update and reset the flag
            updateLcd();
        #endif  // nolcd
        update = false;
    }

    #ifdef ROTARY
        // check Hardware PTT and make the RX/TX changes
        if (dbPTT.update()) {
            // state changed
            if (dbPTT.fell()) {
                // line asserted (PTT Closed) going to TX
                going2TX();

                // clear the memory scan flag if active
                #ifdef MEMORIES
                    #ifdef MEM_SCAN
                    mscan = false;
                    #endif
                #endif
            } else {
                // line left open, going to RX
                going2RX();
            }

            // update flag
            update = true;
        }

        // debouce for the push
        dbBtnPush.update();
        tbool = dbBtnPush.fell();

        if (runMode) {
            // we are in normal mode

            // step (push button)
            if (tbool) {
                // beep
                beep();

                // VFO step change
                changeStep();
                update = true;
            }

            #ifdef SMETER
                // Second line of the LCD, I must show the bargraph only if not rit nor steps
                if ((!ritActive and showStepCounter == 0) and smeterOk)
                    showBarGraph();
            #endif
        }


        #ifdef ABUT     // no setup mode if no analog buttons to change it
            if (!runMode) {
                // setup mode

                // Push button is step in Config mode
                if (tbool) {
                    // change the step and show it on the LCD
                    changeStep();
                    #ifdef LCD
                        showStep();
                    #endif     // nolcd
                }

            }
        #endif // abut
    #endif  // rotary

    // timed actions, it ticks every 1/4 second (250 msecs)
    if ((millis() - lastMilis) >= TICK_INTERVAL) {
        // Reset the last reading to keep track
        lastMilis = millis();

        #ifdef SMETER
            // I must sample the input for the bar graph
            smeter();
        #endif

        // time counter for VFO remember after power off
        if (qcounter < SAVE_INTERVAL) {
            qcounter += 1;
        } else {
            // the saveEEPROM has already a mechanism to change only the parts
            // that has changed, to protect the EEPROM of wear out
            saveEEPROM();
            qcounter = 0;
        }

        // step show time show the first time
        if (showStepCounter >= STEP_SHOW_TIME) {
            #ifdef LCD
                showStep();
            #endif  // nolcd
        }

        // decrement timer
        if (showStepCounter > 0) {
            showStepCounter -= 1;
            #ifdef SMETER
                // flag to redraw the bar graph only if zero
                if (showStepCounter == 0) barReDraw = true;
            #endif
        }
    }

    #ifdef CAT_CONTROL
        // CAT check
        cat.check();
    #endif

    #ifdef ABUT
        // analog buttons check
        abm.check();
    #endif

    #ifdef MEMORIES
        #ifdef MEM_SCAN
            // memory scan timming check
            checkMemScan();
        #endif
    #endif
}
