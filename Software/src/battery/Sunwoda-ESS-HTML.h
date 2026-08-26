#ifndef _SUNWODA_ESS_HTML_H
#define _SUNWODA_ESS_HTML_H

#include "../datalayer/datalayer.h"
#include "../devboard/webserver/BatteryHtmlRenderer.h"

/** Extra data parsed from the Sunwoda BCMU CAN protocol that has no datalayer home.
 *  Filled by SunwodaBattery, displayed by SunwodaHtmlRenderer.
 *
 *  Contactor bit meanings come from the "Switch status information" (gIoSwhInfo_51)
 *  entry of the vendor CAN map (0x0C50FF33): bit0 negative contactor, bit1 main
 *  contactor, bit2 precharge contactor, bit8/9 hard contacts 1/2. Safety switch bits
 *  from the same frame: bit0 disconnecting switch, bit1 surge protector.
 */
struct SunwodaExtendedData {
  // Contactor / switch status, 0x0C50FF33
  bool negative_contactor_closed = false;
  bool main_contactor_closed = false;
  bool precharge_contactor_closed = false;
  bool hard_contact_1_closed = false;
  bool hard_contact_2_closed = false;
  bool disconnect_switch_closed = false;
  bool surge_protector_closed = false;
  uint16_t raw_output_switch_status = 0;
  uint16_t raw_input_switch_status = 0;

  // Contactor self-test status, 0x0C50FF36 (0 = no self-test run, 1 = self-test completed)
  uint16_t contactor_selftest_status = 0;

  // Alarm (gAlarmInfo_52, 0x0C50FF34) and Fault (gFaultInfo_53, 0x0C50FF35) words, one per
  // subindex: [0] external 0, [1] external 1, [2] internal 0, [3] internal 1. Bit meanings
  // come from the vendor's BCMU_APP CAN variable map (tools/H102025_P02_BCMU_APP_V1.16_
  // FerroAMP_20210420_4BMU translated.xlsx, "Fault alarm information" sheet) - see the
  // SUNWODA_ALARM_BIT_NAMES/SUNWODA_FAULT_BIT_NAMES tables below.
  uint16_t alarmWords[4] = {0, 0, 0, 0};
  uint16_t faultWords[4] = {0, 0, 0, 0};
  bool alarmActive = false;
  bool faultActive = false;
};

// Bit names for gAlarmInfo_52 / gFaultInfo_53, indexed [subindex][bit]. nullptr = unused/reserved.
// Source: vendor's BCMU_APP CAN variable map, "Fault alarm information" sheet.
static const char* const SUNWODA_ALARM_BIT_NAMES[4][16] = {
    {"Overcurrent during charging", "Overcurrent discharge", "Battery overvoltage", "Battery undervoltage",
     "High temperature during battery charging", "Low temperature during battery charging",
     "High temperature during battery discharge", "Low temperature during battery discharge",
     "Disconnect switch open", "Reserved switch 1 is off", "Reserved switch 2 disconnected",
     "BCMU high-voltage sampling anomaly", "BCMU temperature sampling anomaly", "BCMU leakage detection failure",
     "Precharge overheating", "Contactor overheating"},
    {"Fuse overheating", "Battery pack failed to start", "Charging stopped", "Battery pack self-protection",
     "Communication CAN bus disconnected with high voltage detection module", nullptr, nullptr, nullptr, nullptr,
     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"Charging current exceeds limit", "Discharge current exceeds limit", "Severe overcurrent during charging",
     "Severe overcurrent discharge", "Severe high pressure", "Severe low pressure",
     "Severe low temperature during charging", "Severe low temperature discharge", "Voltage consistency decline",
     "Uneven temperature distribution", "Abnormal capacity decay", "Low battery remaining capacity",
     "Battery cell failure", nullptr, nullptr, nullptr},
    {"BMU equalization feedback circuit malfunction", "BMU equalization control circuit malfunction", nullptr,
     nullptr, nullptr, nullptr, "BMU node number duplicate", "BMU software version error", "High voltage warning",
     "Low voltage warning", "Surge protector status", "Equalization voltage is normal", nullptr, nullptr, nullptr,
     nullptr},
};

static const char* const SUNWODA_FAULT_BIT_NAMES[4][16] = {
    {"BMU sampling circuit malfunction", "BMU sampling line disconnected",
     "Internal communication with BMU via CAN bus disconnection",
     "External communication with BSMU CAN bus disconnected", "Current sampling circuit malfunction",
     "Contactor feedback circuit malfunction", "Fuse blows", "Smoke alarm", "Battery pack leakage",
     "The rechargeable battery is severely overheating", "Severe overheating of the discharged battery",
     "Battery cell failure", "Negative contactor malfunction", "Main contactor malfunction",
     "Precharge contactor malfunction", "Instantaneous overcurrent protection during charging"},
    {"Discharge short circuit overcurrent protection", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"The battery pack is severely leaking current", "Negative contactor retest abnormal",
     "Main contactor retest abnormal", "Precharge contactor re-inspection abnormal", nullptr, nullptr, nullptr,
     nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"BMU sampling circuit malfunction", "BMU voltage sampling line broken", "BMU temperature sampling line broken",
     nullptr, nullptr, nullptr, nullptr, nullptr, "BMU internal CAN disconnection", nullptr, nullptr, nullptr,
     nullptr, nullptr, nullptr, nullptr},
};

class SunwodaHtmlRenderer : public BatteryHtmlRenderer {
 public:
  SunwodaHtmlRenderer(SunwodaExtendedData* d) : data(d) {}

  String get_status_html() {
    String content;

    static const char* openClosed[2] = {"Open", "Closed"};

    content += "<h4>Contactors:</h4>";
    content += "<h4>Negative: " + String(openClosed[data->negative_contactor_closed]) + "</h4>";
    content += "<h4>Main: " + String(openClosed[data->main_contactor_closed]) + "</h4>";
    content += "<h4>Precharge: " + String(openClosed[data->precharge_contactor_closed]) + "</h4>";
    content += "<h4>Hard contact 1: " + String(openClosed[data->hard_contact_1_closed]) + "</h4>";
    content += "<h4>Hard contact 2: " + String(openClosed[data->hard_contact_2_closed]) + "</h4>";

    content += "<h4>Safety switches:</h4>";
    content += "<h4>Disconnecting switch: " + String(openClosed[data->disconnect_switch_closed]) + "</h4>";
    content += "<h4>Surge protector: " + String(openClosed[data->surge_protector_closed]) + "</h4>";

    content += "<h4>Contactor self-test: " +
               String(data->contactor_selftest_status == 1 ? "Completed" : "Not run") + "</h4>";

    content += "<h4>Alarm active: " + String(data->alarmActive ? "Yes" : "No") + "</h4>";
    content += "<h4>Fault active: " + String(data->faultActive ? "Yes" : "No") + "</h4>";

    content += list_active_bits("Active alarms", data->alarmWords, SUNWODA_ALARM_BIT_NAMES);
    content += list_active_bits("Active faults", data->faultWords, SUNWODA_FAULT_BIT_NAMES);

    return content;
  }

 private:
  SunwodaExtendedData* data;

  // Renders any set bits as their vendor-documented name; falls back to "wordN.bitM" for
  // bits with no confirmed meaning, and shows "None" when every word is zero.
  static String list_active_bits(const char* title, const uint16_t words[4], const char* const names[4][16]) {
    String content = "<h4>" + String(title) + ":</h4>";
    bool any = false;
    for (uint8_t word = 0; word < 4; word++) {
      for (uint8_t bit = 0; bit < 16; bit++) {
        if ((words[word] & (1u << bit)) == 0) {
          continue;
        }
        any = true;
        const char* name = names[word][bit];
        if (name != nullptr) {
          content += "<h4>- " + String(name) + "</h4>";
        } else {
          content += "<h4>- word" + String(word) + ".bit" + String(bit) + " (unconfirmed)</h4>";
        }
      }
    }
    if (!any) {
      content += "<h4>- None</h4>";
    }
    return content;
  }
};

#endif
