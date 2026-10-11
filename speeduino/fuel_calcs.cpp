#include "fuel_calcs.h"
#include "maths.h"
#include "unit_testing.h"
#include "globals.h"
#include "decoders.h"
#include "units.h"
#include "scheduler_fuel_controller.h"

TESTABLE_INLINE_STATIC uint16_t calculateRequiredFuel(const config2 &page2, const statuses &current) {
  uint16_t reqFuel = page2.reqFuel * 100U; //Convert to uS and an int. This is the only variable to be used in calculations
  if ((page2.strokes == FOUR_STROKE) && (current.injLayout != INJ_SEQUENTIAL))
  {
    //Default is 1 squirt per revolution, so we halve the given req-fuel figure (Which would be over 2 revolutions)
    //The req_fuel calculation above gives the total required fuel (At VE 100%) in the full cycle.
    //If we're doing more than 1 squirt per cycle then we need to split the amount accordingly.
    //(Note that in a non-sequential 4-stroke setup you cannot have less than 2 squirts as you cannot determine the
    //stroke to make the single squirt on)
    reqFuel = reqFuel / 2U; 
  }

  return reqFuel;
}

static inline uint8_t calcNitrousStagePercent(uint8_t minRPMDiv100, uint8_t maxRPMDiv100, const statuses &current) {
  uint16_t minRPM = minRPMDiv100*UINT16_C(100);
  uint16_t maxRPM = maxRPMDiv100*UINT16_C(100);
  uint16_t rpmRange = maxRPM - minRPM;
  auto adderPercent = (uint8_t)fast_div32_16((current.RPM - minRPM) * UINT32_C(100), rpmRange); //The percentage of the way through the RPM range
  return 100U - adderPercent; //Flip the percentage as we go from a higher adder to a lower adder as the RPMs rise
}

static inline uint16_t calcNitrousStagePulseWidth(uint8_t minRPMDiv100, uint8_t maxRPMDiv100, uint8_t adderMinDiv100, uint8_t adderMaxDiv100, const statuses &current)
{
  uint16_t adderMin = adderMinDiv100 * UINT16_C(100);
  uint16_t adderMax = adderMaxDiv100 * UINT16_C(100);
  uint16_t adderRange = adderMin - adderMax;
  return adderMax + percentageApprox(calcNitrousStagePercent(minRPMDiv100, maxRPMDiv100, current), adderRange); //Calculate the above percentage of the calculated ms value.
}

//Manual adder for nitrous. These are not in correctionsFuel() because they are direct adders to the ms value, not % based
TESTABLE_INLINE_STATIC uint16_t pwApplyNitrous(uint16_t pw, const config10 &page10, const statuses &current)
{
  if (current.nitrous_status!=NITROUS_OFF && pw!=0U)
  {
    if( (current.nitrous_status == NITROUS_STAGE1) || (current.nitrous_status == NITROUS_BOTH) )
    {
      pw = pw + calcNitrousStagePulseWidth(page10.n2o_stage1_minRPM, page10.n2o_stage1_maxRPM, page10.n2o_stage1_adderMin, page10.n2o_stage1_adderMax, current);
    }
    if( (current.nitrous_status == NITROUS_STAGE2) || (current.nitrous_status == NITROUS_BOTH) )
    {
      pw = pw + calcNitrousStagePulseWidth(page10.n2o_stage2_minRPM, page10.n2o_stage2_maxRPM, page10.n2o_stage2_adderMin, page10.n2o_stage2_adderMax, current);
    }
  }

  return pw;
}

TESTABLE_INLINE_STATIC uint16_t calculatePWLimit(const config2 &page2, const statuses &current)
{
  uint32_t tempLimit = percentageApprox(page2.dutyLim, current.revolutionTime); //The pulsewidth limit is determined to be the duty cycle limit (Eg 85%) by the total time it takes to perform 1 revolution
  //Handle multiple squirts per rev
  if (page2.strokes == FOUR_STROKE) { tempLimit = tempLimit * 2U; }
  //Optimise for power of two divisions where possible
  switch(current.nSquirts)  {
    case 1:
      //No action needed
      break;
    case 2:
      tempLimit = tempLimit >> 1U;
      break;
    case 4:
      tempLimit = tempLimit >> 2U;
      break;
    case 8:
      tempLimit = tempLimit >> 3U;
      break;
    default:
      //Non-PoT squirts value. Perform (slow) uint32_t division
      tempLimit = fast_div (tempLimit, current.nSquirts);
      break;
  }
  return (uint16_t)(std::min)(tempLimit, (uint32_t)UINT16_MAX);
}


static inline uint32_t applyMapMode(uint32_t intermediate, const config2 &page2, const statuses &current) {
  if ( page2.multiplyMAP == MULTIPLY_MAP_MODE_100) { 
    uint16_t multiplier = div100(lshift<7U>((uint32_t)current.MAP));
    return rshift<7U>(intermediate * (uint32_t)multiplier); 
  }
  if( page2.multiplyMAP == MULTIPLY_MAP_MODE_BARO) { 
     uint16_t multiplier = fast_div32_16(lshift<7U>((uint32_t)current.MAP), current.baro); 
    return rshift<7U>(intermediate * (uint32_t)multiplier); 
  }
  return intermediate;
}

static inline uint32_t applyAFRMultiplier(uint32_t intermediate, const config2 &page2, const config6 &page6, const statuses &current) {
  if (page2.includeAFR == true) {
    if ((page6.egoType == EGO_TYPE_WIDE) && (current.runSecs > page6.ego_sdelay) ) {
      uint16_t multiplier = fast_div(lshift<7U>((uint16_t)current.O2), current.afrTarget);  //Include AFR (vs target) if enabled
      return rshift<7U>(intermediate * (uint32_t)multiplier); 
    }
  } else {
    if ( page2.incorporateAFR ) {
      uint16_t multiplier = fast_div(lshift<7U>((uint16_t)page2.stoich), current.afrTarget);  //Incorporate stoich vs target AFR, if enabled.
      return rshift<7U>(intermediate * (uint32_t)multiplier); 
    }
  }
  return intermediate;
}

static inline uint32_t applyCorrections(uint32_t intermediate, uint16_t corrections) {
  return percentageApprox(corrections, intermediate); 
}


// Return injector differential pressure in 0.1 bar. currentStatus.fuelPressure is
// gauge PSI; MAP and baro are absolute kPa. Therefore delta-P across the injector is:
// rail(gauge) + barometric - manifold absolute.
TESTABLE_INLINE_STATIC uint8_t calculateInjectorDifferentialPressureDbar(const statuses &current) {
  const int16_t railDbar = (int16_t)(((uint16_t)current.fuelPressure * 689U + 500U) / 1000U); // psi -> 0.1 bar
  const int16_t manifoldTermDbar = ((int16_t)current.baro - (int16_t)current.MAP) / 10;        // kPa -> 0.1 bar
  const int16_t delta = railDbar + manifoldTermDbar;
  return (uint8_t)clamp(delta, (int16_t)1, (int16_t)255);
}

// Small integer square-root avoids pulling floating point sqrt into the Mega build.
static uint16_t integerSqrt32(uint32_t value) {
  uint32_t op = value;
  uint32_t res = 0U;
  uint32_t one = UINT32_C(1) << 30;
  while (one > op) { one >>= 2U; }
  while (one != 0U) {
    if (op >= (res + one)) {
      op -= res + one;
      res = (res >> 1U) + one;
    } else {
      res >>= 1U;
    }
    one >>= 2U;
  }
  return (uint16_t)res;
}

// Flow multiplier in per-mille. 1000 = no correction.
TESTABLE_INLINE_STATIC uint16_t calculateInjectorPressureFlowPermille(const config10 &page10, const statuses &current) {
  if ((configPage15.injPressureFlowEnable == 0U) || (page10.fuelPressureEnable == 0U) ||
      (current.fuelPressure < 5U) || (configPage15.injPressureRefDbar == 0U)) {
    return 1000U;
  }
  const uint8_t deltaP = calculateInjectorDifferentialPressureDbar(current);
  // sqrt(ref/actual)*1000 = sqrt(ref*1,000,000/actual)
  const uint32_t radicand = ((uint32_t)configPage15.injPressureRefDbar * UINT32_C(1000000)) / deltaP;
  return clamp(integerSqrt32(radicand), (uint16_t)500U, (uint16_t)2000U);
}

static inline uint32_t applyInjectorPressureFlow(uint32_t effectivePw, const config10 &page10, const statuses &current) {
  const uint16_t permille = calculateInjectorPressureFlowPermille(page10, current);
  return (effectivePw * permille + 500U) / 1000U;
}

static uint8_t find6Bin(const byte *bins, uint8_t value) {
  if (value <= bins[0]) { return 0U; }
  for (uint8_t i = 0U; i < 5U; i++) {
    if (value <= bins[i + 1U]) { return i; }
  }
  return 4U;
}

static uint16_t interpolateU8(uint8_t x, uint8_t x0, uint8_t x1, uint16_t y0, uint16_t y1) {
  if ((x1 <= x0) || (x <= x0)) { return y0; }
  if (x >= x1) { return y1; }
  const uint16_t span = (uint16_t)x1 - x0;
  const uint16_t pos = (uint16_t)x - x0;
  if (y1 >= y0) { return y0 + (uint16_t)(((uint32_t)(y1 - y0) * pos) / span); }
  return y0 - (uint16_t)(((uint32_t)(y0 - y1) * pos) / span);
}

// Bilinear pressure x voltage table. Stored table values are 0.01 ms = 10 us.
TESTABLE_INLINE_STATIC uint16_t calculatePressureDeadTimeUs(const config2 &page2, const config10 &page10, const statuses &current) {
  if ((configPage15.injPressureDeadTimeEnable == 0U) || (page10.fuelPressureEnable == 0U) ||
      (current.fuelPressure < 5U)) {
    return page2.injOpen * current.batCorrection;
  }

  const uint8_t p = calculateInjectorDifferentialPressureDbar(current);
  const uint8_t v = current.battery10;
  const byte *pBins = configPage15.injDeadTimePressureBins;
  const byte *vBins = configPage15.injDeadTimeVoltageBins;
  const uint8_t pi = find6Bin(pBins, p);
  const uint8_t vi = find6Bin(vBins, v);

  const uint8_t p0 = pBins[pi];
  const uint8_t p1 = pBins[pi + 1U];
  const uint8_t v0 = vBins[vi];
  const uint8_t v1 = vBins[vi + 1U];

  // Table is laid out [pressure row][voltage column].
  const uint8_t *table = configPage15.injDeadTimeTable;
  const uint16_t q00 = (uint16_t)table[(uint8_t)(pi * 6U + vi)] * 10U;
  const uint16_t q01 = (uint16_t)table[(uint8_t)(pi * 6U + vi + 1U)] * 10U;
  const uint16_t q10 = (uint16_t)table[(uint8_t)((pi + 1U) * 6U + vi)] * 10U;
  const uint16_t q11 = (uint16_t)table[(uint8_t)((pi + 1U) * 6U + vi + 1U)] * 10U;

  const uint16_t lowP = interpolateU8(v, v0, v1, q00, q01);
  const uint16_t highP = interpolateU8(v, v0, v1, q10, q11);
  return interpolateU8(p, p0, p1, lowP, highP);
}

static inline uint16_t computeInitialPw(uint16_t REQ_FUEL, uint8_t VE) {
  // REQ_FUEL max is 255*100 = 25500
  // VE max is 255
  // Therefore max result = 65025. So just fits in a uint16_t.
  return percentageApprox(VE, REQ_FUEL); 
}

static inline uint32_t includeOpenTime(uint32_t intermediate, uint16_t injOpen) {
    return intermediate + injOpen; //Add the injector opening time
}

static inline uint32_t includeAe(uint32_t intermediate, uint16_t REQ_FUEL, const config2 &page2, const statuses &current) {
  // We need to add Acceleration Enrichment pct increase if the engine is accelerating
  if ((current.isAcceleratingTPS) && (page2.aeApplyMode == AE_MODE_ADDER) && (current.AEamount>100U)) {
    return intermediate + percentageApprox((uint16_t)(current.AEamount - 100U), REQ_FUEL);
  }
  return intermediate;
}

TESTABLE_INLINE_STATIC uint16_t calcPrimaryPulseWidth(uint16_t injOpenTime, const config2 &page2, const config6 &page6, const config10 &page10, const statuses &current) {
  uint16_t REQ_FUEL = calculateRequiredFuel(page2, current);

  //Standard float version of the calculation
  //return (REQ_FUEL * (float)(VE/100.0) * (float)(MAP/100.0) * (float)(TPS/100.0) * (float)(corrections/100.0) + injOpenTime);
  //Note: The MAP and TPS portions are currently disabled, we use VE and corrections only
  uint32_t intermediate = 
    includeAe(
      includeOpenTime(
        applyInjectorPressureFlow(
          applyCorrections(
            applyAFRMultiplier(
              applyMapMode(
                computeInitialPw(REQ_FUEL, current.VE),
                page2, current),
              page2, page6, current),
            current.corrections),
          page10, current),
      injOpenTime),
      REQ_FUEL, page2, current);

  // Make sure this won't overflow when we convert to uInt. This means the maximum pulsewidth possible is 65.535mS
  return pwApplyNitrous((uint16_t)(std::min)(intermediate, (uint32_t)UINT16_MAX), page10, current);
}

// Apply the pwLimit if staging is disabled and engine is not cranking
TESTABLE_INLINE_STATIC uint16_t applyPwLimits(uint16_t pw, uint16_t pwLimit, const config10 &page10, const statuses &current) {
  if( (current.rotationStatus!=EngineRotationStatus::Cranking) && (page10.stagingEnabled == false) ) { 
    return (std::min)(pw, pwLimit);
  }
  return pw;
}

static inline bool canApplyStaging(const config2 &page2, const config10 &page10) {
    //To run staged injection, the number of cylinders must be less than the injector channels (ie Assuming you're running paired injection, you need at least as many injector channels as you have cylinders, half for the primaries and half for the secondaries)
 return  (page10.stagingEnabled == true) 
      && (page2.nCylinders <= (uint8_t)INJ_CHANNELS || page2.injType == INJ_TYPE_TBODY); //Final check is to ensure that DFCO isn't active, which would cause an overflow below (See #267)  
}

static inline uint32_t calcTotalStagePw(uint16_t primaryPW, uint16_t injOpenTime, const config10 &page10) {
  // Subtract the opening time from PW1 as it needs to be multiplied out again by the pri/sec req_fuel values below. 
  // It is added on again after that calculation. 
  primaryPW = primaryPW - injOpenTime;
  uint32_t totalInjector = page10.stagedInjSizePri + page10.stagedInjSizeSec;
  return ((uint32_t)primaryPW)*totalInjector;
}

static inline uint32_t calcStagePrimaryPw(uint32_t totalPw, const config10 &page10) noexcept {
  return fast_div(totalPw, page10.stagedInjSizePri);
}
static inline uint32_t calcStageSecondaryPw(uint32_t totalPw, const config10 &page10) noexcept {
  return fast_div(totalPw, page10.stagedInjSizeSec);
}

static inline pulseWidths applyStagingModeTable(uint16_t primaryPW, uint16_t injOpenTime, const config10 &page10, const statuses &current) {
  uint32_t totalPw = calcTotalStagePw(primaryPW, injOpenTime, page10);
  //Subtract the opening time from PW1 as it needs to be multiplied out again by the pri/sec req_fuel values below. It is added on again after that calculation. 
  uint32_t pwPrimaryStaged = calcStagePrimaryPw(totalPw, page10);

  uint8_t stagingSplit = get3DTableValue(&stagingTable, current.fuelLoad, current.RPM);
  if(stagingSplit > 0U) 
  { 
    uint32_t pwSecondaryStaged = calcStageSecondaryPw(totalPw, page10);
    uint32_t primary = percentageApprox((uint8_t)(100U - stagingSplit), pwPrimaryStaged) + injOpenTime;
    uint32_t secondary = percentageApprox(stagingSplit, pwSecondaryStaged) + injOpenTime;
    return { 
      (uint16_t)(std::min)(primary, (uint32_t)UINT16_MAX),
      (uint16_t)(std::min)(secondary, (uint32_t)UINT16_MAX),
    };
  }

  return { (uint16_t)(std::min)(pwPrimaryStaged + injOpenTime, (uint32_t)UINT16_MAX), 0U};
}

static inline pulseWidths applyStagingModeAuto(uint16_t primaryPW, uint16_t pwLimit, uint16_t injOpenTime, const config10 &page10) {
  uint32_t pwPrimaryStaged = calcStagePrimaryPw(calcTotalStagePw(primaryPW, injOpenTime, page10), page10);

  //If automatic mode, the primary injectors are used all the way up to their limit (Configured by the pulsewidth limit setting)
  //If they exceed their limit, the extra duty is passed to the secondaries
  if(pwPrimaryStaged > pwLimit)
  {
    uint32_t extraPW = pwPrimaryStaged - pwLimit + injOpenTime; //The open time must be added here AND below because pwPrimaryStaged does not include an open time. The addition of it here takes into account the fact that pwLlimit does not contain an allowance for an open time. 
    uint32_t secondary = fast_div(extraPW * page10.stagedInjSizePri, page10.stagedInjSizeSec) + injOpenTime;
    return { 
      pwLimit,
      (uint16_t)(std::min)(secondary, (uint32_t)UINT16_MAX),
    };
  }

  return { (uint16_t)(std::min)(pwPrimaryStaged + injOpenTime, (uint32_t)UINT16_MAX), 0U};
}


TESTABLE_INLINE_STATIC pulseWidths calculateSecondaryPw(uint16_t primaryPw, uint16_t pwLimit, uint16_t injOpenTime, const config2 &page2, const config10 &page10, const statuses &current) {
  if(canApplyStaging(page2, page10) && (primaryPw!=0U) )
  {
    //Scale the 'full' pulsewidth by each of the injector capacities
    if(page10.stagingMode == STAGING_MODE_TABLE) {
      return applyStagingModeTable(primaryPw, injOpenTime, page10, current);
    }
    if(page10.stagingMode == STAGING_MODE_AUTO) {
      return applyStagingModeAuto(primaryPw, pwLimit, injOpenTime, page10);
    }
  }

  return { primaryPw, 0U };
}

TESTABLE_INLINE_STATIC uint16_t calculateOpenTime(const config2 &page2, const config10 &page10, const statuses &current) {
  // Use the pressure x voltage table when enabled, otherwise retain legacy Speeduino behaviour.
  return calculatePressureDeadTimeUs(page2, page10, current);
}

pulseWidths computePulseWidths(const config2 &page2, const config6 &page6, const config10 &page10, const statuses &current) {
  if (current.corrections!=0U) {
    uint16_t pwLimit = calculatePWLimit(page2, current);
    uint16_t injOpenTime = calculateOpenTime(page2, page10, current);
    uint16_t primaryPw = applyPwLimits(calcPrimaryPulseWidth( injOpenTime, 
                                                              page2,
                                                              page6,
                                                              page10, 
                                                              current),
                                        pwLimit,
                                        page10,
                                        current);
    return calculateSecondaryPw(primaryPw, pwLimit, injOpenTime, page2, page10, current);  
  }
  return { 0U, 0U };
}