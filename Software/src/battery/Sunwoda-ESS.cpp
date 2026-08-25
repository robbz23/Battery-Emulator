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

    case ID_ALARM_INFO:  // 0x0C50FF34 - Alarm information. Bit-level meanings not
                          // confirmed yet, so we only expose "any bit set".

      memcpy(extended_data.rawAlarm, rx_frame.data.u8, rx_frame.DLC > 8 ? 8 : rx_frame.DLC);

      extended_data.alarmActive = false;
      for (uint8_t i = 0; i < rx_frame.DLC && i < 8; i++) {
        if (extended_data.rawAlarm[i] != 0) {
          extended_data.alarmActive = true;
          break;
        }
      }

      // Surface an unspecified BMS alarm on the events page, same as other drivers do for a
      // generic "caution" bit whose exact meaning isn't broken out (e.g. Nissan Leaf case 4).
      if (extended_data.alarmActive) {
        set_event(EVENT_BATTERY_CAUTION, 0);
      } else {
        clear_event(EVENT_BATTERY_CAUTION);
      }

      break;

    case ID_FAULT_INFO:  // 0x0C50FF35 - Fault information. Bit-level meanings not
                         // confirmed yet, so we only expose "any bit set".

      memcpy(extended_data.rawFault, rx_frame.data.u8, rx_frame.DLC > 8 ? 8 : rx_frame.DLC);

      extended_data.faultActive = false;
      for (uint8_t i = 0; i < rx_frame.DLC && i < 8; i++) {
        if (extended_data.rawFault[i] != 0) {
          extended_data.faultActive = true;
          break;
        }
      }

      // A Fault frame is assumed to be more severe than an Alarm one, so it is mapped to the
      // generic ERROR-level "stop charge/discharge" event rather than the INFO-level caution
      // used for Alarm above. Same pattern as Nissan Leaf's worst-case failsafe status (case 7).
      if (extended_data.faultActive) {
        set_event(EVENT_BATTERY_CHG_DISCHG_STOP_REQ, 0);
      } else {
        clear_event(EVENT_BATTERY_CHG_DISCHG_STOP_REQ);
      }

      break;

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
