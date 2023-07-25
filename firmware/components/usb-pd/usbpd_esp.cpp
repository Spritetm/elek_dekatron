#include <stdlib.h>
#include "usbpd_esp.h"
#include "esp_log.h"
#include "fusb302b.h"
#include "fusb302_defines.h"
#include "policy_engine.h"
#include "esp_timer.h"
#include "esp_check.h"

extern "C" {

#define TAG "usbpd_esp"

static i2c_port_t port;
static FUSB302 *fusb;
static PolicyEngine *pe;

static bool fusb_rd(const uint8_t deviceAddr, const uint8_t registerAdd, const uint8_t size, uint8_t *buf) {
	esp_err_t r=i2c_master_write_read_device(port, deviceAddr>>1, &registerAdd, 1, buf, size, pdMS_TO_TICKS(100));
	if (r!=ESP_OK) ESP_LOGE(TAG, "fusb_rd: i2c returned error");
	return (r==ESP_OK);
}

static bool fusb_wr(const uint8_t deviceAddr, const uint8_t registerAdd, const uint8_t size, uint8_t *buf) {
	ESP_LOGI(TAG, "Writing %d bytes to addr 0x%2X", size, registerAdd);
	i2c_cmd_handle_t h=i2c_cmd_link_create();
	ESP_ERROR_CHECK(i2c_master_start(h));
	ESP_ERROR_CHECK(i2c_master_write_byte(h, deviceAddr, true));
	ESP_ERROR_CHECK(i2c_master_write_byte(h, registerAdd, true));
	ESP_ERROR_CHECK(i2c_master_write(h, buf, size, true));
	ESP_ERROR_CHECK(i2c_master_stop(h));
	esp_err_t r=i2c_master_cmd_begin(port, h, pdMS_TO_TICKS(10));
	i2c_cmd_link_delete(h);
	if (r!=ESP_OK) ESP_LOGE(TAG, "fusb_wr: i2c returned error");
	return (r==ESP_OK);
}

static void fusb_delay(uint32_t ms) {
	vTaskDelay(pdMS_TO_TICKS(ms));
}

static uint32_t pd_gettimestamp() { //in ms
	uint64_t ts_us=esp_timer_get_time();
	uint64_t ts_ms=ts_us/1000;
	return ts_ms;
}

static void usbpd_task(void *arg) {
	ESP_LOGI(TAG, "task running");
	while(1) {
		pe->IRQOccured();
		while(pe->thread()) ;
		vTaskDelay(pdMS_TO_TICKS(5));
	}
}

/* The current draw when the output is disabled */
#define DPM_MIN_CURRENT PD_MA2PDI(100)

bool pdbs_dpm_evaluate_capability(const pd_msg *capabilities, pd_msg *request) {

	/* Get the number of PDOs */
	uint8_t numobj = PD_NUMOBJ_GET(capabilities);

	/* Get whether or not the power supply is constrained */

	/* Make sure we have configuration */
	/* Look at the PDOs to see if one matches our desires */
	// Look against USB_PD_Desired_Levels to select in order of preference
	uint8_t bestIndex = 0xFF;
	int bestIndexVoltage = 0;
	int bestIndexCurrent = 0;
	bool bestIsPPS = false;
	for (uint8_t i = 0; i < numobj; i++) {
		/* If we have a fixed PDO, its V equals our desired V, and its I is
		 * at least our desired I */
		if ((capabilities->obj[i] & PD_PDO_TYPE) == PD_PDO_TYPE_FIXED) {
			// This is a fixed PDO entry
			// Evaluate if it can produve sufficient current based on the
			// tipResistance (ohms*10) V=I*R -> V/I => minimum resistance, if our tip
			// resistance is >= this then we can use this supply

			int voltage_mv = PD_PDV2MV(
					PD_PDO_SRC_FIXED_VOLTAGE_GET(capabilities->obj[i])); // voltage in mV units
			int current_a_x100 = PD_PDO_SRC_FIXED_CURRENT_GET(
					capabilities->obj[i]);            // current in 10mA units
			printf("PD slot %d -> %d mV; %d mA\r\n", i, voltage_mv,
					current_a_x100 * 10);
			if (voltage_mv == 12000 || bestIndex == 0xFF) {
				// Higher voltage and valid, select this instead
				bestIndex = i;
				bestIndexVoltage = voltage_mv;
				bestIndexCurrent = current_a_x100;
				bestIsPPS = false;
			}
		} else if ((capabilities->obj[i] & PD_PDO_TYPE) == PD_PDO_TYPE_AUGMENTED
				&& (capabilities->obj[i] & PD_APDO_TYPE) == PD_APDO_TYPE_PPS) {
			// If this is a PPS slot, calculate the max voltage in the PPS range that
			// can we be used and maintain
			uint16_t max_voltage = PD_PAV2MV(
					PD_APDO_PPS_MAX_VOLTAGE_GET(capabilities->obj[i]));
			// uint16_t min_voltage =
			// PD_PAV2MV(PD_APDO_PPS_MIN_VOLTAGE_GET(capabilities->obj[i]));
			uint16_t max_current = PD_PAI2CA(
					PD_APDO_PPS_CURRENT_GET(capabilities->obj[i])); // max current in 10mA units
			printf("PD PDO slot %d -> %d mV; %d mA\r\n", i, max_voltage,
					max_current * 10);
			// Using the current and tip resistance, calculate the ideal max voltage
			// if this is range, then we will work with this voltage
			// if this is not in range; then max_voltage can be safely selected
			if (max_voltage > bestIndexVoltage || bestIndex == 0xFF) {
				bestIndex = i;
				bestIndexVoltage = max_voltage;
				bestIndexCurrent = max_current;
				bestIsPPS = true;
			}
		}
	}

	if (bestIndex != 0xFF) {
		printf("Found desired capability at index  %d, %d mV, %d mA\r\n",
				(int) bestIndex, bestIndexVoltage, bestIndexCurrent * 10);

		/* We got what we wanted, so build a request for that */
		request->hdr = PD_MSGTYPE_REQUEST | PD_NUMOBJ(1);
		if (bestIsPPS) {
			request->obj[0] =
					PD_RDO_PROG_CURRENT_SET(
							PD_CA2PAI(bestIndexCurrent)) | PD_RDO_PROG_VOLTAGE_SET(PD_MV2PRV(bestIndexVoltage)) | PD_RDO_NO_USB_SUSPEND | PD_RDO_OBJPOS_SET(bestIndex + 1);
		} else {
			request->obj[0] =
					PD_RDO_FV_MAX_CURRENT_SET(
							bestIndexCurrent) | PD_RDO_FV_CURRENT_SET(bestIndexCurrent) | PD_RDO_NO_USB_SUSPEND | PD_RDO_OBJPOS_SET(bestIndex + 1);
		}
		// USB Data
		request->obj[0] |= PD_RDO_USB_COMMS;
	} else {
		/* Nothing matched (or no configuration), so get 5 V at low current */
		request->hdr = PD_MSGTYPE_REQUEST | PD_NUMOBJ(1);
		request->obj[0] =
				PD_RDO_FV_MAX_CURRENT_SET(
						DPM_MIN_CURRENT) | PD_RDO_FV_CURRENT_SET(DPM_MIN_CURRENT) | PD_RDO_NO_USB_SUSPEND | PD_RDO_OBJPOS_SET(1);
		/* If the output is enabled and we got here, it must be a capability
		 * mismatch. */
		if (false /*TODO: Check if you have already negotiated*/) {
			request->obj[0] |= PD_RDO_CAP_MISMATCH;
		}
		// USB Data
		request->obj[0] |= PD_RDO_USB_COMMS;
	}
	// Even if we didnt match, we return true as we would still like to handshake
	// on 5V at the minimum
	return true;
}

void pdbs_dpm_get_sink_capability(pd_msg *cap, const bool isPD3) {
	/* Keep track of how many PDOs we've added */
	int numobj = 0;

	// Must always have a PDO object for vSafe5V, indicate the bare minimum power required
	/* Minimum current, 5 V, and higher capability. */
	cap->obj[numobj++] =
			PD_PDO_TYPE_FIXED
					| PD_PDO_SNK_FIXED_VOLTAGE_SET(
							PD_MV2PDV(5000)) | PD_PDO_SNK_FIXED_CURRENT_SET(DPM_MIN_CURRENT);

	if (true) { // If requesting more than 5V
		/* Get the current we want */
		uint16_t voltage = 12 * 1000; // in mv => 12V
		uint16_t current = 1 * 100;   // In centi-amps => 1A

		/* Add a PDO for the desired power. */
		cap->obj[numobj++] =
				PD_PDO_TYPE_FIXED
						| PD_PDO_SNK_FIXED_VOLTAGE_SET(
								PD_MV2PDV(voltage)) | PD_PDO_SNK_FIXED_CURRENT_SET(current);

		/* If we want more than 5 V, set the Higher Capability flag */
		if (PD_MV2PDV(voltage) != PD_MV2PDV(5000)) {
			cap->obj[0] |= PD_PDO_SNK_FIXED_HIGHER_CAP;
		}
		/* If we're using PD 3.0, add a PPS APDO for our desired voltage */
		if (isPD3) {
			cap->obj[numobj++] =
					PD_PDO_TYPE_AUGMENTED | PD_APDO_TYPE_PPS
							| PD_APDO_PPS_MAX_VOLTAGE_SET(
									PD_MV2PAV(voltage)) | PD_APDO_PPS_MIN_VOLTAGE_SET(PD_MV2PAV(voltage)) | PD_APDO_PPS_CURRENT_SET(PD_CA2PAI(current));
		}
	}
	/* Set the USB communications capable flag. */
	cap->obj[0] |= PD_PDO_SNK_FIXED_USB_COMMS;
	// if this device is unconstrained, set the flag
	cap->obj[0] |= PD_PDO_SNK_FIXED_UNCONSTRAINED;

	/* Set the Sink_Capabilities message header */
	cap->hdr = PD_MSGTYPE_SINK_CAPABILITIES | PD_NUMOBJ(numobj);
}


bool pds_dpm_epr_evaluate_capability(const epr_pd_msg *capabilities, pd_msg *request) {
	ESP_LOGW(TAG, "pds_dpm_epr_evaluate_capability: stub");
	return false;
}

esp_err_t usbpd_esp_init(i2c_port_t i2c_port) {
	port=i2c_port;
	fusb=new FUSB302(FUSB302B_ADDR, fusb_rd, fusb_wr, fusb_delay);
	if (!fusb->fusb_read_id()) {
		ESP_LOGE(TAG, "fusb_read_id failed!");
		goto err_fusb;
	}
	if (!fusb->fusb_setup()) {
		ESP_LOGE(TAG, "fusb_setup failed!");
		goto err_fusb;
	}
	pe=new PolicyEngine(*fusb, pd_gettimestamp, fusb_delay, 
			pdbs_dpm_get_sink_capability, pdbs_dpm_evaluate_capability, 
			pds_dpm_epr_evaluate_capability, 5);

	xTaskCreate(usbpd_task, "usbpd", 4096, NULL, 23, NULL);
	return ESP_OK;
//err_pe:
	delete(pe);
err_fusb:
	delete(fusb);
	return ESP_ERR_INVALID_RESPONSE;
}


}