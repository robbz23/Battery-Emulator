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
  static const uint32_t ID_SYSTEM_CONTROL = SUNWODA_BASE_ID + 0x46;  // gCtrlInfo_70 (host -> BCMU, RW)
  static const uint32_t ID_ALARM_INFO = SUNWODA_BASE_ID + 0x34;      // gAlarmInfo_52
  static const uint32_t ID_FAULT_INFO = SUNWODA_BASE_ID + 0x35;      // gFaultInfo_53
  static const uint32_t ID_CLUSTER_INFO = SUNWODA_BASE_ID + 0x36;    // gClusterInfo_54 (contactor self-test)
  static const uint32_t ID_CELL_BALANCE_0 = SUNWODA_BASE_ID + 0x3C;  // gCellBalInfo_60 (cells 1-252)
  static const uint32_t ID_MAIN_INFO = SUNWODA_BASE_ID + 0x50;       // gMainInfo_80
  static const uint32_t ID_VOLT_CHARA = SUNWODA_BASE_ID + 0x51;      // gVoltChara_81
  static const uint32_t ID_TEMP_CHARA = SUNWODA_BASE_ID + 0x52;      // gTempChara_82
  static const uint32_t ID_CELL_VOLTAGE_0 = SUNWODA_BASE_ID + 0x55;  // gCellVolt_85 (cells 1-252, mV)

  // Software/hardware version info. Not part of the gXxxInfo_NN / 0x0C50FF00+address family
  // above - the vendor's variable map (tools/SoftwareAddresses.txt) lists these as "Software
  // version number" and "Hardware version number" (both U16, display format factor 10), but
  // gives no explicit CAN ID. These two IDs were instead identified from a real CAN capture
  // (tools/pcanOut.txt): they are the only frames that broadcast a small, constant value once
  // per second, which fits a version number far better than any other observed field. Treat as
  // a best-effort inference pending confirmation against real hardware.
  static const uint32_t ID_SOFTWARE_VERSION = 0x0C506E07;
  static const uint32_t ID_HARDWARE_VERSION = 0x0C506E1D;

  /*
  Start command (gCtrlInfo_70 / System control commands, 0x0C50FF46). Confirmed from the vendor's
  BCMU_APP CAN variable map (tools/_current_status.txt row 22-27, "Current status" sheet): subindex 0
  is "Working status control" (0=Start, 1=Stop, 2=Emergency Stop, 4=Clear Fault), RW, sent on-demand
  by the host (period=0), not broadcast periodically like the gXxxInfo_NN status frames.

  A real capture (tools/pcanOut.txt) shows the pack sitting at "Battery protection status: Standby"
  / "Operating status: Stop" for the whole ~5.5s log with contactors open (0x0C50FF33 output switch
  status stuck at 0x0000) and this ID never transmitted by anything - i.e. nothing had ever told the
  BCMU to start, which is consistent with contactors never closing. Sending this Start command is a
  best-effort inference: the vendor doc does not show an example write frame, so the byte layout below
  mirrors the read-side multiplex convention (mux 0x43 = subindex 0-2 in one frame) rather than being
  independently confirmed. Verify on real hardware (watch operatingStatus on the advanced battery page)
  before relying on it.
  */
  CAN_frame SUNWODA_START_COMMAND = {.FD = false,
                                     .ext_ID = true,
                                     .DLC = 8,
                                     .ID = ID_SYSTEM_CONTROL,
                                     .data = {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  //                                          ^mux  ^sub0  ^--Start=0--  ^--mode=Normal--  ^--ctrl=Remote--
  unsigned long previousMillisStartCommand = 0;
  static const unsigned long START_COMMAND_INTERVAL_MS = 1000;

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