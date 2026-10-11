/*
Speeduino - Simple engine management for the Arduino Mega 2560 platform
Copyright (C) Josh Stewart
A full copy of the license may be found in the projects root directory
*/

/*
Timers are used for having actions performed repeatedly at a fixed interval (Eg every 100ms)
They should not be confused with Schedulers, which are for performing an action once at a given point of time in the future

Timers are typically low resolution (Compared to Schedulers), with maximum frequency currently being approximately every 10ms
*/
#include "timers.h"
#include "globals.h"
#include "sensors.h"
#include "scheduler_fuel_controller.h"
#include "scheduler_ignition_controller.h"
#include "comms.h"
#include "maths.h"
#include "preprocessor.h"
#include "scheduledIO_ign.h"
#include "scheduledIO_inj.h"
#include "src/pins/boardOutputPin.h"
#include "src/controllers/fuelPump/fuelPumpController.h"
#include "src/controllers/fan/fanController.h"
#include "src/controllers/tacho/tachoController.h"

// v28 flex-content plausibility state.
static uint32_t flexContentLastHighLoadMs = 0UL;
static bool flexContentHasHeld = false;

volatile uint16_t lastRPM_100ms; //Need to record this for rpmDOT calculation
volatile byte loop5ms;
volatile byte loop20ms;
volatile byte loop33ms;
volatile byte loop66ms;
volatile byte loop100ms;
volatile byte loop250ms;
volatile int loopSec;

void __attribute__((optimize("Os"))) initialiseTimers(void)
{
  lastRPM_100ms = 0;
  loop5ms = 0;
  loop20ms = 0;
  loop33ms = 0;
  loop66ms = 0;
  loop100ms = 0;
  loop250ms = 0;
  loopSec = 0;
}

TESTABLE_STATIC volatile uint8_t TIMER_mask;

uint8_t getAndClearTimerMask(void)
{
  ATOMIC() {
    uint8_t mask = TIMER_mask;
    TIMER_mask = 0U;
    return mask;
  }
  // LCOV_EXCL_START
  return 0U; // Suppress false compiler warning
  // LCOV_EXCL_STOP
}

void oneMSInterval(void)
{
  BIT_SET(TIMER_mask, BIT_TIMER_1KHZ);

  //Increment Loop Counters
  loop5ms++;
  loop20ms++;
  loop33ms++;
  loop66ms++;
  loop100ms++;
  loop250ms++;
  loopSec++;

  applyOverDwellProtection(configPage4, currentStatus);
  tachoControl(currentStatus);

  //200Hz loop
  if(loop5ms == 5)
  {
    loop5ms = 0; //Reset counter
    BIT_SET(TIMER_mask, BIT_TIMER_200HZ);
  }

  //50Hz loop
  if(loop20ms == 20)
  {
    loop20ms = 0; //Reset counter
    BIT_SET(TIMER_mask, BIT_TIMER_50HZ);
  }

  //30Hz loop
  if (loop33ms == 33)
  {
    loop33ms = 0;
    BIT_SET(TIMER_mask, BIT_TIMER_30HZ);
  }

  //15Hz loop
  if (loop66ms == 66)
  {
    loop66ms = 0;
    BIT_SET(TIMER_mask, BIT_TIMER_15HZ);
  }

  //10Hz loop
  if (loop100ms == 100)
  {
    loop100ms = 0; //Reset counter
    BIT_SET(TIMER_mask, BIT_TIMER_10HZ);

    currentStatus.rpmDOT = (currentStatus.RPM - lastRPM_100ms) * 10; //This is the RPM per second that the engine has accelerated/decelerated in the last loop
    lastRPM_100ms = currentStatus.RPM; //Record the current RPM for next calc

    if ( currentStatus.rotationStatus==EngineRotationStatus::Running ) { runSecsX10++; }
    else { runSecsX10 = 0; }

    if ( (currentStatus.injPrimed == false) && (seclx10 >= configPage2.primingDelay) && (currentStatus.RPM == 0) && (currentStatus.initialisationComplete == true) ) 
    { 
      beginInjectorPriming(currentStatus, configPage4); 
      currentStatus.injPrimed = true; 
    }
    seclx10++;
  }

  //4Hz loop
  if (loop250ms == 250)
  {
    loop250ms = 0; //Reset Counter
    BIT_SET(TIMER_mask, BIT_TIMER_4HZ);
    #if defined(CORE_STM32) //debug purpose, only visual for running code
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    #endif
  }

  //1Hz loop
  if (loopSec == 1000)
  {
    loopSec = 0; //Reset counter.
    BIT_SET(TIMER_mask, BIT_TIMER_1HZ);

    currentStatus.crankRPM = ((unsigned int)configPage4.crankRPM * 10);

    //**************************************************************************************************************************************************
    //This updates the runSecs variable
    //If the engine is running or cranking, we need to update the run time counter.
    if (currentStatus.rotationStatus!=EngineRotationStatus::Stopped)
    { //NOTE - There is a potential for a ~1sec gap between engine crank starting and the runSec number being incremented. This may delay ASE!
      if (currentStatus.runSecs <= (UINT8_MAX-1U)) //Ensure we cap out at 255 and don't overflow. (which would reset ASE and cause problems with the closed loop fuelling (Which has to wait for the O2 to warmup))
        { currentStatus.runSecs++; } //Increment our run counter by 1 second.
    }
    //**************************************************************************************************************************************************
    //This records the number of main loops the system has completed in the last second
    currentStatus.loopsPerSecond = mainLoopCount;
    mainLoopCount = 0;
    //**************************************************************************************************************************************************
    //increment secl (secl is simply a counter that increments every second and is used to track whether the system has unexpectedly reset
    currentStatus.secl++;
    //**************************************************************************************************************************************************
    //Check the fan output status
    if (configPage2.fanEnable >= 1)
    {
       fanControl();            // Function to turn the cooling fan on/off
    }

    //Check whether fuel pump priming is complete
    stopPumpPriming(currentStatus, configPage2);
    
    //**************************************************************************************************************************************************
    //Set the flex reading (if enabled). The flexCounter is updated with every pulse from the sensor. If cleared once per second, we get a frequency reading
    if(configPage2.flexEnabled == true)
    {
      const uint8_t measuredFreq = flexCounter;
      flexCounter = 0U;

      byte tempEthPct = 0U;
      bool flexSampleValid = true;

      if (configPage15.flexContentHoldEnabled != 0U)
      {
        // A genuine fuel-content change is slow. A large frequency dropout or
        // diagnostic over-frequency is treated as implausible and the last
        // accepted ethanol value is retained.
        const uint8_t lowTolerance = 5U;
        const uint8_t minPlausibleFreq = (configPage2.flexFreqLow > lowTolerance)
                                       ? (configPage2.flexFreqLow - lowTolerance)
                                       : 0U;

        if (measuredFreq < minPlausibleFreq)
        {
          flexSampleValid = false;
        }
        else if (measuredFreq < configPage2.flexFreqLow)
        {
          tempEthPct = 0U;
        }
        else if (measuredFreq > (configPage2.flexFreqHigh + 1U))
        {
          if (measuredFreq < (configPage2.flexFreqHigh + 20U))
          {
            tempEthPct = 100U;
          }
          else
          {
            flexSampleValid = false;
          }
        }
        else
        {
          tempEthPct = measuredFreq - configPage2.flexFreqLow;
        }
      }
      else
      {
        // Original Speeduino behaviour when the v28 plausibility feature is off.
        if(measuredFreq < configPage2.flexFreqLow)
        {
          tempEthPct = 0U;
        }
        else if (measuredFreq > (configPage2.flexFreqHigh + 1U))
        {
          if(measuredFreq < (configPage2.flexFreqHigh + 20U)) { tempEthPct = 100U; }
          else { tempEthPct = 0U; }
        }
        else
        {
          tempEthPct = measuredFreq - configPage2.flexFreqLow;
        }
      }

      if (tempEthPct == 1U) { tempEthPct = 0U; }
      if (tempEthPct > 100U) { tempEthPct = 100U; }

      bool flexContentHoldActive = false;
      if (configPage15.flexContentHoldEnabled != 0U)
      {
        const uint16_t holdRPM = (uint16_t)configPage15.flexContentHoldRPMdiv100 * 100U;
        const bool highLoad = (currentStatus.TPS >= configPage15.flexContentHoldTPS)
                           || (currentStatus.RPM >= holdRPM);

        if (highLoad)
        {
          flexContentLastHighLoadMs = millis();
          flexContentHasHeld = true;
          flexContentHoldActive = true;
        }
        else if (flexContentHasHeld)
        {
          const uint32_t resumeDelayMs = (uint32_t)configPage15.flexContentResume10ms * 10UL;
          if ((millis() - flexContentLastHighLoadMs) < resumeDelayMs)
          {
            flexContentHoldActive = true;
          }
        }
      }

      if (flexSampleValid && !flexContentHoldActive)
      {
        uint8_t filteredEthPct = (uint8_t)LOW_PASS_FILTER((uint16_t)tempEthPct,
                                                          configPage4.FILTER_FLEX,
                                                          (uint16_t)currentStatus.ethanolPct);

        // This block executes once per second, so the configured limit is
        // directly percentage-points of ethanol per second.
        const uint8_t maxDelta = configPage15.flexContentMaxDelta;
        if (maxDelta > 0U)
        {
          const uint8_t currentEth = currentStatus.ethanolPct;
          if (filteredEthPct > currentEth)
          {
            const uint16_t upper = (uint16_t)currentEth + maxDelta;
            if ((uint16_t)filteredEthPct > upper)
            {
              filteredEthPct = (upper > 100U) ? 100U : (uint8_t)upper;
            }
          }
          else
          {
            const uint8_t lower = (currentEth > maxDelta) ? (currentEth - maxDelta) : 0U;
            if (filteredEthPct < lower) { filteredEthPct = lower; }
          }
        }

        currentStatus.ethanolPct = filteredEthPct;
      }

      // Fuel-temperature reading remains live even while ethanol content is held.
      flexPulseWidth = constrain(flexPulseWidth, 1000UL, 5000UL);
      int32_t tempX100 = (int32_t)rshift<10>((uint32_t)(4224UL * flexPulseWidth)) - 8125L;
      currentStatus.fuelTemp = div100((int16_t)tempX100);
    }
  } // end 1Hz loop


#if defined(CORE_AVR) //AVR chips use the ISR for this
    //Reset Timer2 to trigger in another ~1ms
    TCNT2 = 131;            //Preload timer2 with 100 cycles, leaving 156 till overflow.
#endif
}
