#ifndef SUNWODA_ESS_BATTERY_H
#define SUNWODA_ESS_BATTERY_H

#include "CanBattery.h"
#include "Sunwoda-ESS-HTML.h"

/*
Sunwoda BCMU (Battery Cluster Management Unit) home ESS battery, as used behind the
"FerroAMP" branded native Windows app.

CAN IDs follow the pattern 0x0C50FF00 + <address>, where <address> is the decimal
number embedded in the vendor's variable names (e.g. gMainInfo_80 -> 0x0C50FF00 + 80
= 0x0C50FF50). This mapping was reverse engineered from the vendor's BCMU_APP CAN
variable map together with a real CAN capture (see tools/pcanOut.txt), so treat
anything not explicitly listed here as unconfirmed.

CAN speed is not confirmed from the vendor documentation. 500kbit/s is assumed as a
starting point (most common default); change CAN_Speed below if the battery does not
appear on the bus.
*/
class SunwodaBattery : public CanBattery {
 public:
  SunwodaBattery() : CanBattery(CAN_Speed::CAN_SPEED_500KBPS) {}

  virtual void setup(void);
  virtual void handle_incoming_can_frame(CAN_frame rx_frame);
  virtual void update_values();
  virtual void transmit_can(unsigned long currentMillis);

  static constexpr const char* Name = "Sunwoda ESS Battery (BCMU)";

  BatteryHtmlRenderer& get_status_renderer() { return renderer; }

 private:
  SunwodaExtendedData extended_data;
  SunwodaHtmlRenderer renderer = SunwodaHtmlRenderer(&extended_data);

  // Base CAN ID for the BCMU broadcast frames. Actual IDs are this + <address>.
  static const uint32_t SUNWODA_BASE_ID = 0x0C50FF00;

  static const uint32_t ID_SYSTEM_STATUS = SUNWODA_BASE_ID + 0x32;   // gStateInfo_50
  static const uint32_t ID_SWITCH_STATUS = SUNWODA_BASE_ID + 0x33;   // gIoSwhInfo_51 (contactors)
  static const uint32_t ID_ALARM_INFO = SUNWODA_BASE_ID + 0x34;      // gAlarmInfo_52
  static const uint32_t ID_FAULT_INFO = SUNWODA_BASE_ID + 0x35;      // gFaultInfo_53
  static const uint32_t ID_CLUSTER_INFO = SUNWODA_BASE_ID + 0x36;    // gClusterInfo_54 (contactor self-test)
  static const uint32_t ID_CELL_BALANCE_0 = SUNWODA_BASE_ID + 0x3C;  // gCellBalInfo_60 (cells 1-252)
  static const uint32_t ID_MAIN_INFO = SUNWODA_BASE_ID + 0x50;       // gMainInfo_80
  static const uint32_t ID_VOLT_CHARA = SUNWODA_BASE_ID + 0x51;      // gVoltChara_81
  static const uint32_t ID_TEMP_CHARA = SUNWODA_BASE_ID + 0x52;      // gTempChara_82
  static const uint32_t ID_CELL_VOLTAGE_0 = SUNWODA_BASE_ID + 0x55;  // gCellVolt_85 (cells 1-252, mV)

  // Decoded values, applied to the datalayer in update_values()
  float packVoltage = 0.0f;
  float soc = 0.0f;
  float soh = 0.0f;

  uint16_t minCellVoltage = 3200;
  uint16_t maxCellVoltage = 3200;
  uint16_t minCellNumber = 0;
  uint16_t maxCellNumber = 0;

  int16_t minTemperature = 0;
  int16_t maxTemperature = 0;

  uint8_t actual_cell_count = 0;

  // Not confirmed from the vendor spec, chosen in line with similar LFP home ESS batteries
  // (e.g. Growatt HV Ark uses 150mV). Tune once real pack behavior is known.
  static const uint16_t MAX_CELL_DEVIATION_MV = 200;

  static uint16_t u16(const uint8_t* d) { return (uint16_t)d[0] | ((uint16_t)d[1] << 8); }
};

#endif