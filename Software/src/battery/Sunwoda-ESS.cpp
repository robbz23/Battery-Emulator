#include "Sunwoda-ESS.h"
#include "../battery/BATTERIES.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"

/*
Based on reverse engineering of the Sunwoda BCMU CAN variable map together with a real
CAN capture (tools/pcanOut.txt). See Sunwoda-ESS.h for how CAN IDs map to the vendor's
address scheme. Fields not listed in the switch below have not been decoded yet.
*/

void SunwodaBattery::setup(void) {  // Performs one time setup at startup
  strncpy(datalayer.system.info.battery_protocol, Name, 63);
  datalayer.system.info.battery_protocol[63] = '\0';

  datalayer.battery.info.chemistry = battery_chemistry_enum::LFP;

  if (user_selected_max_pack_voltage_dV > 0) {
    datalayer.battery.info.max_design_voltage_dV = user_selected_max_pack_voltage_dV;
  }
  if (user_selected_min_pack_voltage_dV > 0) {
    datalayer.battery.info.min_design_voltage_dV = user_selected_min_pack_voltage_dV;
  }
  if (user_selected_max_cell_voltage_mV > 0) {
    datalayer.battery.info.max_cell_voltage_mV = user_selected_max_cell_voltage_mV;
  }
  if (user_selected_min_cell_voltage_mV > 0) {
    datalayer.battery.info.min_cell_voltage_mV = user_selected_min_cell_voltage_mV;
  }

  datalayer.battery.info.max_cell_voltage_deviation_mV = MAX_CELL_DEVIATION_MV;
}

void SunwodaBattery::update_values() {
  datalayer.battery.status.real_soc = (uint16_t)(soc * 100);  // 98.3% -> 9830

  datalayer.battery.status.soh_pptt = (uint16_t)(soh * 100);  // 100.0% -> 10000

  datalayer.battery.status.voltage_dV = (uint16_t)(packVoltage * 10);  // 622.1V -> 6221

  datalayer.battery.status.cell_max_voltage_mV = maxCellVoltage;
  datalayer.battery.status.cell_min_voltage_mV = minCellVoltage;

  datalayer.battery.status.temperature_min_dC = minTemperature * 10;  // whole degrees C -> deci-degrees
  datalayer.battery.status.temperature_max_dC = maxTemperature * 10;

  if (actual_cell_count > 0) {
    datalayer.battery.info.number_of_cells = actual_cell_count;
  }

  datalayer.battery.status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer.battery.status.real_soc) / 10000) * datalayer.battery.info.total_capacity_Wh);

  //TODO: max_charge_power_W / max_discharge_power_W / current_dA have not been located
  //in the CAN stream yet. Charge/discharge current limit and power limit live under
  //gCurrLimit_90 (0x0C50FF5A) but the byte layout is not confirmed.

  /*
  Contactor closing policy:

  The BCMU owns its own negative/main/precharge contactors and closes them autonomously
  (observed in tools/pcanOut.txt: the negative contactor bit comes up on its own a few
  hundred ms after power-up, with no request frame from the host at all). So unlike
  batteries whose BMS waits for an explicit "close contactors" CAN command from us, there
  is nothing to transmit here - we only need to *observe* what the BCMU already decided
  and mirror that into the datalayer so battery-emulator's own contactor/precharge state
  machine (precharge_control.cpp) knows it is safe to connect the DC bus to the inverter.

  main_contactor_closed comes from ID_SWITCH_STATUS (gIoSwhInfo_51) and is the direct
  feedback of the BCMU's own main contactor, so it is a stronger signal than the
  "sleeping" bit used for the same purpose on Growatt HV Ark (which lacks contactor
  feedback). faultActive comes from ID_FAULT_INFO; alarmActive (caution-level) is
  intentionally not included here, matching the ERROR vs INFO split already used above.
  */
  datalayer.battery.status.real_bms_status =
      extended_data.faultActive ? BMS_FAULT : (extended_data.main_contactor_closed ? BMS_ACTIVE : BMS_STANDBY);

  datalayer.system.status.battery_allows_contactor_closing =
      extended_data.main_contactor_closed && !extended_data.faultActive;
}

void SunwodaBattery::handle_incoming_can_frame(CAN_frame rx_frame) {
  switch (rx_frame.ID) {
    case ID_MAIN_INFO:  // 0x0C50FF50 - Total voltage / current / power / SOC / SOH / avg temp

      if (rx_frame.DLC < 8) {
        break;
      }

      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;

      /*
      Example:
      43 00 4D 18 00 00 00 00
      0x184D = 6221 -> 622.1V
      */
      if (rx_frame.data.u8[0] == 0x43) {
        packVoltage = (float)u16(&rx_frame.data.u8[2]) / 10.0f;
      }

      /*
      Example:
      83 03 D7 03 E8 03 16 00
      SOC = 983 = 98.3%
      SOH = 1000 = 100%
      */
      if (rx_frame.data.u8[0] == 0x83) {
        soc = (float)u16(&rx_frame.data.u8[2]) / 10.0f;
        soh = (float)u16(&rx_frame.data.u8[4]) / 10.0f;
      }

      break;

    case ID_VOLT_CHARA:  // 0x0C50FF51 - Voltage characteristics (lowest/highest cell)

      if (rx_frame.DLC < 8) {
        break;
      }

      // 43 frame: lowest cell number + lowest voltage
      if (rx_frame.data.u8[0] == 0x43) {
        minCellNumber = u16(&rx_frame.data.u8[2]);
        minCellVoltage = u16(&rx_frame.data.u8[4]);
      }

      // 83 frame: highest cell number + highest voltage
      if (rx_frame.data.u8[0] == 0x83) {
        maxCellNumber = u16(&rx_frame.data.u8[2]);
        maxCellVoltage = u16(&rx_frame.data.u8[4]);
      }

      break;

    case ID_TEMP_CHARA:  // 0x0C50FF52 - Temperature characteristics (lowest/highest)

      if (rx_frame.DLC < 8) {
        break;
      }

      if (rx_frame.data.u8[0] == 0x43) {  // Lowest temperature
        minTemperature = (int16_t)u16(&rx_frame.data.u8[4]);
      }

      if (rx_frame.data.u8[0] == 0x83) {  // Highest temperature
        maxTemperature = (int16_t)u16(&rx_frame.data.u8[4]);
      }

      break;

    case ID_SWITCH_STATUS: {  // 0x0C50FF33 - Contactor / IO switch status (gIoSwhInfo_51)

      if (rx_frame.DLC < 8) {
        break;
      }

      /*
      Example: C3 00 00 00 01 00 03 00
      byte0: mux (ignored)
      byte1: reserved
      [2:4]: output switch I/O status - bit0 negative contactor, bit1 main contactor,
             bit2 precharge contactor, bit8 hard contact 1, bit9 hard contact 2
      [4:6]: input switch I/O status (raw, meaning of individual bits not confirmed)
      [6:8]: safety switch stable state - bit0 disconnecting switch, bit1 surge protector
      */
      uint16_t output_switch_status = u16(&rx_frame.data.u8[2]);
      uint16_t input_switch_status = u16(&rx_frame.data.u8[4]);
      uint16_t safety_switch_status = u16(&rx_frame.data.u8[6]);

      extended_data.raw_output_switch_status = output_switch_status;
      extended_data.raw_input_switch_status = input_switch_status;

      extended_data.negative_contactor_closed = (output_switch_status & 0x0001) != 0;
      extended_data.main_contactor_closed = (output_switch_status & 0x0002) != 0;
      extended_data.precharge_contactor_closed = (output_switch_status & 0x0004) != 0;
      extended_data.hard_contact_1_closed = (output_switch_status & 0x0100) != 0;
      extended_data.hard_contact_2_closed = (output_switch_status & 0x0200) != 0;

      extended_data.disconnect_switch_closed = (safety_switch_status & 0x0001) != 0;
      extended_data.surge_protector_closed = (safety_switch_status & 0x0002) != 0;

    } break;

    case ID_CLUSTER_INFO:  // 0x0C50FF36 - Contactor self-test status (gClusterInfo_54)

      if (rx_frame.DLC < 4) {
        break;
      }

      extended_data.contactor_selftest_status = u16(&rx_frame.data.u8[2]);

      break;

    case ID_ALARM_INFO: {  // 0x0C50FF34 - Alarm information (gAlarmInfo_52)

      /*
      Multiplexed the same way as ID_SWITCH_STATUS/ID_VOLT_CHARA: byte0 is a mux marker (ignored),
      byte1 is the subindex of the first word carried in this frame, and each subsequent u16 is one
      more consecutive subindex. gAlarmInfo_52 has 4 subindices (external alarm 0/1, internal alarm
      0/1), confirmed from the vendor's BCMU_APP CAN variable map (tools/H102025_P02_BCMU_APP_V1.16_
      FerroAMP_20210420_4BMU translated.xlsx, "Current status" sheet). They normally arrive as one
      DLC8 frame (subindex 0-2) followed by one DLC4 frame (subindex 3 only) - e.g. the header bytes
      themselves (0x43/0x00 or 0x81/0x03) must NOT be treated as alarm data, otherwise the mux/
      subindex bytes falsely look like an active alarm even when the real bits are all zero.
      */
      if (rx_frame.DLC < 4) {
        break;
      }

      uint8_t start_subindex = rx_frame.data.u8[1];
      uint8_t words_in_frame = (rx_frame.DLC - 2) / 2;
      for (uint8_t i = 0; i < words_in_frame; i++) {
        uint8_t subindex = start_subindex + i;
        if (subindex >= 4) {
          continue;
        }
        extended_data.alarmWords[subindex] = u16(&rx_frame.data.u8[2 + i * 2]);
      }

      extended_data.alarmActive = (extended_data.alarmWords[0] | extended_data.alarmWords[1] |
                                    extended_data.alarmWords[2] | extended_data.alarmWords[3]) != 0;

      // Surface an unspecified BMS alarm on the events page, same as other drivers do for a
      // generic "caution" bit whose exact meaning isn't broken out (e.g. Nissan Leaf case 4).
      if (extended_data.alarmActive) {
        set_event(EVENT_BATTERY_CAUTION, 0);
      } else {
        clear_event(EVENT_BATTERY_CAUTION);
      }

    } break;

    case ID_FAULT_INFO: {  // 0x0C50FF35 - Fault information (gFaultInfo_53)

      // Same subindex-multiplexed layout as ID_ALARM_INFO above (4 subindices: external fault
      // 0/1, internal fault 0/1), confirmed from the vendor's BCMU_APP CAN variable map.
      if (rx_frame.DLC < 4) {
        break;
      }

      uint8_t start_subindex = rx_frame.data.u8[1];
      uint8_t words_in_frame = (rx_frame.DLC - 2) / 2;
      for (uint8_t i = 0; i < words_in_frame; i++) {
        uint8_t subindex = start_subindex + i;
        if (subindex >= 4) {
          continue;
        }
        extended_data.faultWords[subindex] = u16(&rx_frame.data.u8[2 + i * 2]);
      }

      extended_data.faultActive = (extended_data.faultWords[0] | extended_data.faultWords[1] |
                                    extended_data.faultWords[2] | extended_data.faultWords[3]) != 0;

      // A Fault frame is assumed to be more severe than an Alarm one, so it is mapped to the
      // generic ERROR-level "stop charge/discharge" event rather than the INFO-level caution
      // used for Alarm above. Same pattern as Nissan Leaf's worst-case failsafe status (case 7).
      if (extended_data.faultActive) {
        set_event(EVENT_BATTERY_CHG_DISCHG_STOP_REQ, 0);
      } else {
        clear_event(EVENT_BATTERY_CHG_DISCHG_STOP_REQ);
      }

    } break;

    case ID_CELL_VOLTAGE_0: {  // 0x0C50FF55 - Individual cell voltages, 3 cells per frame

      if (rx_frame.DLC < 8) {
        break;
      }

      uint8_t start_index = rx_frame.data.u8[1];  // 0-based cell index of the first cell in this frame

      for (uint8_t i = 0; i < 3; i++) {
        uint8_t cell_index = start_index + i;
        if (cell_index >= MAX_AMOUNT_CELLS) {
          continue;
        }
        uint16_t cell_mV = u16(&rx_frame.data.u8[2 + i * 2]);
        if (cell_mV == 0) {
          continue;
        }
        datalayer.battery.status.cell_voltages_mV[cell_index] = cell_mV;
        if ((uint8_t)(cell_index + 1) > actual_cell_count) {
          actual_cell_count = cell_index + 1;
        }
      }

    } break;

    case ID_CELL_BALANCE_0: {  // 0x0C50FF3C - Cell balancing status, 6 cells per frame

      if (rx_frame.DLC < 8) {
        break;
      }

      uint8_t start_index = rx_frame.data.u8[1];  // 0-based cell index of the first cell in this frame

      for (uint8_t i = 0; i < 6; i++) {
        uint8_t cell_index = start_index + i;
        if (cell_index >= MAX_AMOUNT_CELLS) {
          continue;
        }
        // 0: Idle, 1: Equalize charge, 2: Equalize discharge
        datalayer.battery.status.cell_balancing_status[cell_index] = (rx_frame.data.u8[2 + i] != 0);
      }

    } break;

    default:
      break;
  }
}

void SunwodaBattery::transmit_can(unsigned long currentMillis) {
  // The BCMU broadcasts its status periodically without requiring a request frame.
  // No outgoing frames have been identified yet.
}
