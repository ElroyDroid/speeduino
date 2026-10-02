#include "secondaryTables.h"
#include "corrections.h"
#include "load_source.h"
#include "maths.h"
#include "unit_testing.h"
#include "globals.h"
#include "units.h"

/**
 * @brief Looks up and returns the VE value from the secondary fuel table
 * 
 * This performs largely the same operations as getVE() however the lookup is of the secondary fuel table and uses the secondary load source
 * @return byte 
 */
static inline uint8_t lookupVE2(const config10 &page10, const table3d16RpmLoad &veLookupTable, const statuses &current)
{
  return get3DTableValue(&veLookupTable, getLoad(page10.fuel2Algorithm, current), current.RPM); //Perform lookup into fuel map for RPM vs MAP value
}

// Both tune fields use the same selector encoding. Keep that assumption checked
// if either set of constants changes in the future.
static_assert(FUEL2_CONDITION_RPM == SPARK2_CONDITION_RPM
           && FUEL2_CONDITION_MAP == SPARK2_CONDITION_MAP
           && FUEL2_CONDITION_TPS == SPARK2_CONDITION_TPS
           && FUEL2_CONDITION_ETH == SPARK2_CONDITION_ETH,
              "Secondary table switch selectors must match");

static inline bool isSecondarySwitchActive(uint8_t variable, uint16_t threshold, const statuses &current)
{
  return ((variable == FUEL2_CONDITION_RPM) && (current.RPM > threshold))
      || ((variable == FUEL2_CONDITION_MAP) && (current.MAP > threshold))
      || ((variable == FUEL2_CONDITION_TPS) && (current.TPS > threshold))
      || ((variable == FUEL2_CONDITION_ETH) && (current.ethanolPct > threshold));
}

static inline bool fuelModeCondSwitchActive(const config10 &page10, const statuses &current) {
  return (page10.fuel2Mode == FUEL2_MODE_CONDITIONAL_SWITCH)
      && isSecondarySwitchActive(page10.fuel2SwitchVariable, page10.fuel2SwitchValue, current);
}

static inline bool fuelModeInputSwitchActive(const config10 &page10) {
  return (page10.fuel2Mode == FUEL2_MODE_INPUT_SWITCH)
      && (digitalRead(pinNumbers.pinFuel2Input) == page10.fuel2InputPolarity);
}

/** Calculate the Table 2 contribution for Flex Blend mode.
 * fuel2SwitchValue / spark2SwitchValue is reused as the ethanol percentage
 * at which Table 2 reaches 100%. This adds no EEPROM fields and leaves VVT
 * storage/layout untouched. A value of 70 means E35 = 50% blend and E70+ =
 * 100% Table 2.
 */
static inline uint8_t flexTableBlendPercent(uint8_t ethanolPct, uint8_t startTable2At, uint16_t fullTable2At)
{
  const uint16_t startAt = (std::min)((uint16_t)startTable2At, (uint16_t)99U);
  const uint16_t fullAtRaw = clamp(fullTable2At, (uint16_t)1U, (uint16_t)100U);
  const uint16_t fullAt = (fullAtRaw > startAt) ? fullAtRaw : (uint16_t)(startAt + 1U);
  const uint16_t ethanol = (std::min)((uint16_t)ethanolPct, (uint16_t)100U);

  if (ethanol <= startAt) { return 0U; }
  if (ethanol >= fullAt) { return 100U; }

  const uint16_t range = fullAt - startAt;
  const uint16_t aboveStart = ethanol - startAt;
  return (uint8_t)(((aboveStart * 100U) + (range / 2U)) / range);
}

static inline uint8_t blendU8(uint8_t table1, uint8_t table2, uint8_t table2Pct)
{
  const uint16_t table1Pct = (uint16_t)(100U - table2Pct);
  const uint32_t weighted = ((uint32_t)table1 * table1Pct) + ((uint32_t)table2 * table2Pct);
  return (uint8_t)((weighted + 50U) / 100U);
}

static inline int8_t blendAdvance(int8_t table1, int8_t table2, uint8_t table2Pct)
{
  const int16_t table1Pct = (int16_t)(100U - table2Pct);
  const int32_t weighted = ((int32_t)table1 * table1Pct) + ((int32_t)table2 * (int16_t)table2Pct);
  // Round symmetrically for negative timing values.
  const int32_t rounded = (weighted >= 0) ? (weighted + 50L) : (weighted - 50L);
  return (int8_t)clamp((int16_t)(rounded / 100L), (int16_t)INT8_MIN, (int16_t)INT8_MAX);
}

void calculateSecondaryFuel(const config10 &page10, const table3d16RpmLoad &veLookupTable, statuses &current)
{
  //If the secondary fuel table is in use, also get the VE value from there
  if(page10.fuel2Mode == FUEL2_MODE_MULTIPLY)
  {
    current.VE2 = lookupVE2(page10, veLookupTable, current);
    current.secondFuelTableActive = true;
    //Fuel 2 table is treated as a % value. Table 1 and 2 are multiplied together and divided by 100
    auto combinedVE = percentage(current.VE2, current.VE1);
    current.VE = (uint8_t)(std::min)((uint32_t)UINT8_MAX, combinedVE);
  }
  else if(page10.fuel2Mode == FUEL2_MODE_ADD)
  {
    current.VE2 = lookupVE2(page10, veLookupTable, current);
    current.secondFuelTableActive = true;
    //Fuel tables are added together, but a check is made to make sure this won't overflow the 8-bit VE value
    uint16_t combinedVE = (uint16_t)current.VE1 + (uint16_t)current.VE2;
    current.VE = (uint8_t)(std::min)((uint16_t)UINT8_MAX, combinedVE);
  }
  else if((page10.fuel2Mode == FUEL2_MODE_FLEX_BLEND) && (configPage2.flexEnabled == 1U))
  {
    current.VE2 = lookupVE2(page10, veLookupTable, current);
    const uint8_t blendPct = flexTableBlendPercent(current.ethanolPct, configPage15.fuel2FlexBlendStart, page10.fuel2SwitchValue);
    current.secondFuelTableActive = (blendPct > 0U);
    current.VE = blendU8(current.VE1, current.VE2, blendPct);
  }
  else if(fuelModeCondSwitchActive(page10, current) || fuelModeInputSwitchActive(page10))
  {
    current.VE2 = lookupVE2(page10, veLookupTable, current);
    current.secondFuelTableActive = true;
    current.VE = current.VE2;
  }
  else
  {
    // Unknown mode or mode not activated
    current.secondFuelTableActive = false;
    current.VE2 = 0U;
  }
}

// The bounds of the spark table vary depending on the mode (see the INI file).
// int16_t is wide enough to capture the full range of the table.
static inline int16_t lookupSpark2(const config10 &page10, const table3d16RpmLoad &sparkLookupTable, const statuses &current) {
  return IGNITION_ADVANCE_LARGE.toUser(get3DTableValue(&sparkLookupTable, getLoad(page10.spark2Algorithm, current), current.RPM));  
}

static inline int8_t constrainAdvance(int16_t advance)
{
  // Clamp to return type range.
  return (int8_t)clamp(advance, (int16_t)INT8_MIN, (int16_t)INT8_MAX);
}

static inline bool sparkModeCondSwitchActive(const config10 &page10, const statuses &current) {
  return (page10.spark2Mode == SPARK2_MODE_CONDITIONAL_SWITCH)
      && isSecondarySwitchActive(page10.spark2SwitchVariable, page10.spark2SwitchValue, current);
}

static inline bool sparkModeInputSwitchActive(const config10 &page10) {
  return (page10.spark2Mode == SPARK2_MODE_INPUT_SWITCH)
      && (digitalRead(pinNumbers.pinSpark2Input) == page10.spark2InputPolarity);
}

static inline bool isFixedTimingOn(const config2 &page2, const statuses &current) {
            // Fixed timing is in effect
    return  (page2.fixAngEnable == 1U)
            // Cranking, so the cranking advance angle is in effect
            || (current.rotationStatus==EngineRotationStatus::Cranking);
}

void calculateSecondarySpark(const config2 &page2, const config10 &page10, const table3d16RpmLoad &sparkLookupTable, statuses &current)
{
  current.secondSparkTableActive = false; //Clear the bit indicating that the 2nd spark table is in use. 
  current.advance2 = 0;

  if ((page10.spark2Mode == SPARK2_MODE_FLEX_BLEND) && (page2.flexEnabled == 1U))
  {
    // advance1 is deliberately the raw Table 1 value in Flex Blend mode.
    // Blend raw Table 1 and Table 2 first, then run correctionsIgn() once.
    const int8_t rawAdvance2 = constrainAdvance(lookupSpark2(page10, sparkLookupTable, current));
    const uint8_t blendPct = flexTableBlendPercent(current.ethanolPct, configPage15.spark2FlexBlendStart, page10.spark2SwitchValue);
    current.advance2 = rawAdvance2;
    current.secondSparkTableActive = (blendPct > 0U);
    const int8_t blendedBaseAdvance = blendAdvance(current.advance1, rawAdvance2, blendPct);
    current.advance = correctionsIgn(blendedBaseAdvance);
  }
  else if (!isFixedTimingOn(page2, current))
  {
    if(page10.spark2Mode == SPARK2_MODE_MULTIPLY)
    {
      current.secondSparkTableActive = true;
      uint8_t spark2Percent = (uint8_t)clamp(lookupSpark2(page10, sparkLookupTable, current), (int16_t)0, (int16_t)UINT8_MAX);
      //Spark 2 table is treated as a % value. Table 1 and 2 are multiplied together and divided by 100
      int16_t combinedAdvance = div100((int16_t)(spark2Percent * current.advance1));
      //make sure we don't overflow and accidentally set negative timing: current.advance can only hold a signed 8 bit value
      current.advance = constrainAdvance(combinedAdvance);

      // This is informational only, but the value needs corrected into the int8_t range
      current.advance2 = constrainAdvance((int16_t)spark2Percent-(int16_t)INT8_MAX);
    }
    else if(page10.spark2Mode == SPARK2_MODE_ADD)
    {    
      current.secondSparkTableActive = true;
      current.advance2 = constrainAdvance(lookupSpark2(page10, sparkLookupTable, current));
      //Spark tables are added together, but a check is made to make sure this won't overflow the 8-bit VE value
      int16_t combinedAdvance = (int16_t)current.advance1 + (int16_t)current.advance2;
      current.advance = constrainAdvance(combinedAdvance);
    }
    else if(sparkModeCondSwitchActive(page10, current) || sparkModeInputSwitchActive(page10))
    {
      current.secondSparkTableActive = true;
#if defined(UNIT_TEST)
      current.advance2 = constrainAdvance(lookupSpark2(page10, sparkLookupTable, current));
#else
      //Perform the corrections calculation on the secondary advance value, only if it uses a switched mode
      current.advance2 = correctionsIgn(constrainAdvance(lookupSpark2(page10, sparkLookupTable, current)));
#endif      
      current.advance = current.advance2;
    }
    else
    {
      // Unknown mode or mode not activated
      // Keep MISRA checker happy.
    }
  }
}
