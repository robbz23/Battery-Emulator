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

  // Raw alarm/fault words for diagnostics. Bit-level meanings are not confirmed yet,
  // so we only expose "any bit set" plus the raw bytes.
  uint8_t rawAlarm[8] = {0};
  uint8_t rawFault[8] = {0};
  bool alarmActive = false;
  bool faultActive = false;
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

    char buf[24];
    snprintf(buf, sizeof(buf), "%02X %02X %02X %02X %02X %02X %02X %02X", data->rawAlarm[0], data->rawAlarm[1],
             data->rawAlarm[2], data->rawAlarm[3], data->rawAlarm[4], data->rawAlarm[5], data->rawAlarm[6],
             data->rawAlarm[7]);
    content += "<h4>Raw alarm bytes: " + String(buf) + "</h4>";

    snprintf(buf, sizeof(buf), "%02X %02X %02X %02X %02X %02X %02X %02X", data->rawFault[0], data->rawFault[1],
             data->rawFault[2], data->rawFault[3], data->rawFault[4], data->rawFault[5], data->rawFault[6],
             data->rawFault[7]);
    content += "<h4>Raw fault bytes: " + String(buf) + "</h4>";

    return content;
  }

 private:
  SunwodaExtendedData* data;
};

#endif
