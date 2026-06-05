#include "tl_common.h"
#include "pendant/app_pendant.h"
#include "pendant/host_gatt/app_host_gatt.h"
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

#ifndef APP_BLE_ENABLE_GATT_DEBUG
#define APP_BLE_ENABLE_GATT_DEBUG 0
#endif
#ifndef APP_BLE_ENABLE_DISCOVERY_SCAN
#define APP_BLE_ENABLE_DISCOVERY_SCAN 1
#endif
#ifndef BLC_SCAN_DISABLE
#define BLC_SCAN_DISABLE 0
#endif
#ifndef DUP_FILTER_DISABLE
#define DUP_FILTER_DISABLE 0
#endif

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

		if (evt_code == HCI_EVT_LE_META) {
			u8 sub_evt_code = p[0];

			if (sub_evt_code == HCI_SUB_EVT_LE_ADVERTISING_REPORT) {
				handle_legacy_adv_report(p, n);
			} else if (sub_evt_code == HCI_SUB_EVT_LE_EXTENDED_ADVERTISING_REPORT) {
				handle_extended_adv_report(p, n);
			}
		}
	}

	return 0;
}

#if APP_BLE_ENABLE_GATT_DEBUG
static int app_host_event_callback(u32 h, u8 *para, int n)
{
	u8 event = h & 0xff;

	(void)para;
	(void)n;

	if (event == GAP_EVT_ATT_EXCHANGE_MTU) {
		u_printf("mtu exchange event\r\n");
	} else if (event == GAP_EVT_SMP_PAIRING_BEGIN) {
		u_printf("pairing begin\r\n");
	} else if (event == GAP_EVT_SMP_PAIRING_SUCCESS) {
		u_printf("pairing success\r\n");
	} else if (event == GAP_EVT_SMP_PAIRING_FAIL) {
		gap_smp_pairingFailEvt_t *evt = (gap_smp_pairingFailEvt_t *)para;
		u_printf("pairing fail, reason 0x%x\r\n", evt ? evt->reason : 0);
	}

	return 0;
}

static void task_connect(u8 e, u8 *p, int n)
{
	tlk_contr_evt_connect_t *evt = (tlk_contr_evt_connect_t *)p;

	(void)e;
	(void)n;

#if APP_BLE_ENABLE_DISCOVERY_SCAN
	blc_ll_setScanEnable(BLC_SCAN_DISABLE, DUP_FILTER_DISABLE);
#endif

	u_printf("[APP][EVT] connect\r\n");
	app_pendant_on_app_connected(evt ? evt->initA : 0, BLS_CONN_HANDLE);
}

static void task_terminate(u8 e, u8 *p, int n)
{
	tlk_contr_evt_terminate_t *evt = (tlk_contr_evt_terminate_t *)p;
	u8 reason = evt ? evt->terminate_reason : 0;

	(void)e;
	(void)n;

	u_printf("[APP][EVT] disconnect, reason 0x%x\r\n", reason);
	app_pendant_on_app_disconnected(reason);

#if APP_BLE_ENABLE_DISCOVERY_SCAN
	blc_ll_setScanEnable(BLC_SCAN_ENABLE, DUP_FILTER_ENABLE);
#endif
}
#endif

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
	blc_ll_initChannelSelectionAlgorithm_2_feature();

#if APP_BLE_ENABLE_GATT_DEBUG
	app_host_gatt_init();
	blc_gap_registerHostEventHandler(app_host_event_callback);
	blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_BEGIN |
						  GAP_EVT_MASK_SMP_PAIRING_SUCCESS |
						  GAP_EVT_MASK_SMP_PAIRING_FAIL |
						  GAP_EVT_MASK_ATT_EXCHANGE_MTU);
	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, task_connect);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, task_terminate);
#endif

	adv_param_status = blc_ll_setExtAdvParam(ADV_HANDLE0,
#if APP_BLE_ENABLE_GATT_DEBUG
											 ADV_EVT_PROP_EXTENDED_CONNECTABLE_UNDIRECTED,
#else
											 ADV_EVT_PROP_EXTENDED_NON_CONNECTABLE_NON_SCANNABLE_UNDIRECTED,
#endif
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

#if APP_BLE_ENABLE_DISCOVERY_SCAN
	blc_ll_addScanningInAdvState();
	blc_ll_setScanParameter(SCAN_TYPE_PASSIVE,
							SCAN_INTERVAL_50MS,
							SCAN_INTERVAL_50MS,
							OWN_ADDRESS_PUBLIC,
							SCAN_FP_ALLOW_ADV_ANY);

	blc_hci_le_setEventMask_cmd(HCI_LE_EVT_MASK_ADVERTISING_REPORT |
								 HCI_LE_EVT_MASK_EXTENDED_ADVERTISING_REPORT);
#else
	blc_hci_le_setEventMask_cmd(HCI_LE_EVT_MASK_ADVERTISING_REPORT |
								 HCI_LE_EVT_MASK_EXTENDED_ADVERTISING_REPORT);
#endif
	blc_hci_registerControllerEventHandler(controller_event_callback);

#if APP_BLE_ENABLE_DISCOVERY_SCAN
	blc_ll_setScanEnable(BLC_SCAN_ENABLE, DUP_FILTER_DISABLE);
#endif

	blc_app_checkControllerHostInitialization();
}
