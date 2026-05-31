#include "tl_common.h"
#include "pendant/app_pendant.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"

#define RX_FIFO_SIZE                           64
#define RX_FIFO_NUM                            8
#define TX_FIFO_SIZE                           40
#define TX_FIFO_NUM                            16

#define APP_ADV_SETS_NUMBER                    1
#define APP_MAX_LENGTH_ADV_DATA                240
#define APP_MAX_LENGTH_SCAN_RESPONSE_DATA      240

_attribute_data_retention_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_rxfifo = {
	RX_FIFO_SIZE,
	RX_FIFO_NUM,
	0,
	0,
	blt_rxfifo_b,
};

_attribute_data_retention_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_txfifo = {
	TX_FIFO_SIZE,
	TX_FIFO_NUM,
	0,
	0,
	blt_txfifo_b,
};

u8 app_adv_set_param[ADV_SET_PARAM_LENGTH * APP_ADV_SETS_NUMBER];
u8 app_primary_adv_pkt[MAX_LENGTH_PRIMARY_ADV_PKT * APP_ADV_SETS_NUMBER];
u8 app_secondary_adv_pkt[MAX_LENGTH_SECOND_ADV_PKT * APP_ADV_SETS_NUMBER];
u8 app_scanRspData[APP_MAX_LENGTH_SCAN_RESPONSE_DATA * APP_ADV_SETS_NUMBER];
u8 app_advData[APP_MAX_LENGTH_ADV_DATA * APP_ADV_SETS_NUMBER];

static u8 tbl_advData[] = {
	8, DT_COMPLETE_LOCAL_NAME, 'P', 'E', 'N', 'D', 'A', 'N', 'T',
	2, DT_FLAGS, 0x06,
};

static void handle_legacy_adv_report(const u8 *p, int n)
{
	u8 num_reports;
	int idx;
	u8 i;

	if (!p || n < 2) {
		return;
	}

	num_reports = p[1];
	idx = 2;
	for (i = 0; i < num_reports; i++) {
		const u8 *addr;
		const u8 *adv_data;
		u8 data_len;
		s8 rssi;

		if (idx + 9 > n) {
			return;
		}

		idx += 2;
		addr = &p[idx];
		idx += 6;
		data_len = p[idx++];

		if (idx + data_len + 1 > n) {
			return;
		}

		adv_data = &p[idx];
		idx += data_len;
		rssi = (s8)p[idx++];

		app_pendant_on_adv_report(adv_data, data_len, rssi, addr);
	}
}

static void handle_extended_adv_report(const u8 *p, int n)
{
	u8 num_reports;
	int idx;
	u8 i;

	if (!p || n < 2) {
		return;
	}

	num_reports = p[1];
	idx = 2;
	for (i = 0; i < num_reports; i++) {
		const u8 *addr;
		const u8 *adv_data;
		u8 data_len;
		s8 rssi;

		if (idx + 24 > n) {
			return;
		}

		idx += 2;
		idx += 1;
		addr = &p[idx];
		idx += 6;
		idx += 4;
		rssi = (s8)p[idx++];
		idx += 2;
		idx += 1;
		idx += 6;
		data_len = p[idx++];

		if (idx + data_len > n) {
			return;
		}

		adv_data = &p[idx];
		idx += data_len;

		app_pendant_on_adv_report(adv_data, data_len, rssi, addr);
	}
}

static int controller_event_callback(u32 h, u8 *p, int n)
{
	if (h & HCI_FLAG_EVENT_BT_STD) {
		u8 evt_code = h & 0xff;

		if (evt_code == HCI_EVT_DISCONNECTION_COMPLETE) {
			hci_disconnectionCompleteEvt_t *evt = (hci_disconnectionCompleteEvt_t *)p;
			app_pendant_on_app_disconnected(evt->reason);
		} else if (evt_code == HCI_EVT_LE_META) {
			u8 sub_evt_code = p[0];

			if (sub_evt_code == HCI_SUB_EVT_LE_CONNECTION_COMPLETE) {
				hci_le_connectionCompleteEvt_t *evt = (hci_le_connectionCompleteEvt_t *)p;
				if (evt->status == BLE_SUCCESS) {
					app_pendant_on_app_connected(evt->peerAddr, evt->connHandle);
				}
			} else if (sub_evt_code == HCI_SUB_EVT_LE_ENHANCED_CONNECTION_COMPLETE) {
				hci_le_enhancedConnCompleteEvt_t *evt = (hci_le_enhancedConnCompleteEvt_t *)p;
				if (evt->status == BLE_SUCCESS) {
					app_pendant_on_app_connected(evt->PeerAddr, evt->connHandle);
				}
			} else if (sub_evt_code == HCI_SUB_EVT_LE_ADVERTISING_REPORT) {
				handle_legacy_adv_report(p, n);
			} else if (sub_evt_code == HCI_SUB_EVT_LE_EXTENDED_ADVERTISING_REPORT) {
				handle_extended_adv_report(p, n);
			}
		}
	}

	return 0;
}

void controllerInitialization(void)
{
	u8 mac_public[6];
	u8 mac_random_static[6];
	u8 adv_param_status;

	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);

	blc_ll_initBasicMCU();
	blc_ll_initStandby_module(mac_public);
	blc_ll_initAdvertising_module(mac_public);
	blc_ll_initScanning_module(mac_public);
	blc_ll_initConnection_module();
	blc_ll_initSlaveRole_module();
	blc_ll_initExtendedAdvertising_module(app_adv_set_param, app_primary_adv_pkt, APP_ADV_SETS_NUMBER);
	blc_ll_initExtSecondaryAdvPacketBuffer(app_secondary_adv_pkt, MAX_LENGTH_SECOND_ADV_PKT);
	blc_ll_initExtAdvDataBuffer(app_advData, APP_MAX_LENGTH_ADV_DATA);
	blc_ll_initExtScanRspDataBuffer(app_scanRspData, APP_MAX_LENGTH_SCAN_RESPONSE_DATA);

	adv_param_status = blc_ll_setExtAdvParam(ADV_HANDLE0,
											 ADV_EVT_PROP_EXTENDED_CONNECTABLE_UNDIRECTED,
											 ADV_INTERVAL_50MS,
											 ADV_INTERVAL_50MS,
											 BLT_ENABLE_ADV_ALL,
											 OWN_ADDRESS_PUBLIC,
											 BLE_ADDR_PUBLIC,
											 NULL,
											 ADV_FP_NONE,
											 TX_POWER_8dBm,
											 BLE_PHY_1M,
											 0,
											 BLE_PHY_1M,
											 ADV_SID_0,
											 0);

	if (adv_param_status != BLE_SUCCESS) {
		u_printf("set ext adv param failed: 0x%x\r\n", adv_param_status);
		return;
	}

	blc_ll_clearResolvingList();
	blc_ll_setExtAdvData(ADV_HANDLE0, DATA_OPER_COMPLETE, DATA_FRAGM_ALLOWED, sizeof(tbl_advData), tbl_advData);
	blc_ll_setExtAdvEnable(BLC_ADV_ENABLE, 1, ADV_HANDLE0, 0, 0);

	blc_ll_addScanningInAdvState();
	blc_ll_setScanParameter(SCAN_TYPE_PASSIVE,
							SCAN_INTERVAL_50MS,
							SCAN_INTERVAL_50MS,
							OWN_ADDRESS_PUBLIC,
							SCAN_FP_ALLOW_ADV_ANY);

	blc_hci_setEventMask_cmd(HCI_EVT_MASK_DISCONNECTION_COMPLETE);
	blc_hci_le_setEventMask_cmd(HCI_LE_EVT_MASK_CONNECTION_COMPLETE |
								 HCI_LE_EVT_MASK_ENHANCED_CONNECTION_COMPLETE |
								 HCI_LE_EVT_MASK_ADVERTISING_REPORT |
								 HCI_LE_EVT_MASK_EXTENDED_ADVERTISING_REPORT);
	blc_hci_registerControllerEventHandler(controller_event_callback);

	blc_ll_setScanEnable(BLC_SCAN_ENABLE, DUP_FILTER_ENABLE);
}
