#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"


/**
 * @brief      LinkLayer RX & TX FIFO configuration
 */
/* CAL_LL_ACL_RX_BUF_SIZE(maxRxOct): maxRxOct + 22, then 16 byte align */
#define RX_FIFO_SIZE	64
/* must be: 2^n, (power of 2);at least 4; recommended value: 4, 8, 16 */
#define RX_FIFO_NUM		8


/* CAL_LL_ACL_TX_BUF_SIZE(maxTxOct):  maxTxOct + 10, then 4 byte align */
#define TX_FIFO_SIZE	40
/* must be: (2^n), (power of 2); at least 8; recommended value: 8, 16, 32, other value not allowed. */
#define TX_FIFO_NUM		16
#define MAC_CACHE_SIZE 32

static void blm_le_adv_report_event_handle(u8 *p);




_attribute_data_retention_  u8 		 	blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_rxfifo = {
												RX_FIFO_SIZE,
												RX_FIFO_NUM,
												0,
												0,
												blt_rxfifo_b,};


_attribute_data_retention_  u8 		 	blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_txfifo = {
												TX_FIFO_SIZE,
												TX_FIFO_NUM,
												0,
												0,
												blt_txfifo_b,};


static u8	tbl_advData[] = {
 7,  DT_COMPLETE_LOCAL_NAME, 				'H', 'H', 'C', 'C', 'H', 'H',
 2,	 DT_FLAGS, 								0x01, 					// BLE limited discoverable mode and BR/EDR not supported
 3,  DT_APPEARANCE, 						0x66, 0x66, 			// 384, Generic Remote Control, Generic category
 5,  DT_INCOMPLETE_LIST_16BIT_SERVICE_UUID,	0xAA, 0xBB, 0xCC, 0xDD,	// incomplete list of service class UUIDs (0x1812, 0x180F)
};


static u8 mac_cache[MAC_CACHE_SIZE][6];
static u8 mac_cache_count = 0;



static int controller_event_callback (u32 h, u8 *p, int n)
{
    (void)h;(void)p;(void)n;
	if (h &HCI_FLAG_EVENT_BT_STD)		//ble controller hci event
	{
		u8 evtCode = h & 0xff;
		if(evtCode == HCI_EVT_LE_META)
		{
			u8 subEvt_code = p[0];
			//--------hci le event: le ADV report event ----------------------------------------
			if (subEvt_code == HCI_SUB_EVT_LE_ADVERTISING_REPORT)	// ADV packet
			{
				blm_le_adv_report_event_handle(p);
				// high-level notification kept for backward compatibility
				// u_printf("le ADV report event !\n");

			}

		}
	}
	return 0;
}

/*
 * Parse LE Advertising Report Event parameters and print fields.
 * p points to the LE Meta Event parameters starting at the subevent code.
 * Format (Core v5.0, Vol 2, Part E, 7.7.65.2):
 *  subevent (1), num_reports (1), for each report:
 *    event_type (1), address_type (1), address (6), data_length (1), data (N), RSSI (1)
 */
static void blm_le_adv_report_event_handle(u8 *p)
{
	u8 sub_code = p[0];
	u8 num_reports = p[1];
	int idx = 2;

	(void)sub_code; // kept for clarity

	for (int r = 0; r < num_reports; r++) {
		if (idx + 9 > 255) { // basic sanity
			u_printf("adv report: malformed, idx overflow\n");
			return;
		}

		// u8 event_type = p[idx++];
		// u8 addr_type = p[idx++];
		idx++;idx++; // skip event_type and addr_type
		u8 addr[6];
		for (int i = 0; i < 6; i++) {
			addr[i] = p[idx++];
		}

		// Check if MAC is already in cache
		int mac_seen = 0;
		for (int i = 0; i < mac_cache_count; i++) {
			int same = 1;
			for (int j = 0; j < 6; j++) {
				if (mac_cache[i][j] != addr[j]) {
					same = 0;
					break;
				}
			}
			if (same) {
				mac_seen = 1;
				break;
			}
		}

		if (!mac_seen)
		{
			// Add to cache if space
			if (mac_cache_count < MAC_CACHE_SIZE)
			{
				for (int j = 0; j < 6; j++) mac_cache[mac_cache_count][j] = addr[j];
                u_printf("MAC cache count is %d\n", mac_cache_count);
				mac_cache_count++;
			}

			u8 data_len = p[idx++];
			u8 *adv_data = &p[idx];
			idx += data_len;

			s8 rssi = 0;
			/* RSSI is present after the data according to spec */
			rssi = (s8)p[idx++];

			// u_printf("ADV report %d: ", r);
			// u_printf("evtType=0x%x ", event_type);
			// u_printf("addrType=0x%x\n", addr_type);
			u_printf("===================================================\n");
			u_printf("addr=%x:", addr[5]);
			u_printf("%x:", addr[4]);
			u_printf("%x:", addr[3]);
			u_printf("%x:", addr[2]);
			u_printf("%x:", addr[1]);
			u_printf("%x\n", addr[0]);
			u_printf("data_len=%d, ", data_len);
			u_printf("RSSI=%d\n", rssi);

			/* Parse Advertising Data (AD structures) */
			int ad_idx = 0;
			while (ad_idx < data_len) {
				u8 ad_len = adv_data[ad_idx++];
				if (ad_len == 0) break; /* no more AD structures */
				if (ad_idx >= data_len) break;

				u8 ad_type = adv_data[ad_idx++];
				u8 ad_val_len = ad_len - 1; /* subtract the type byte */
				u8 *ad_val = &adv_data[ad_idx];

				/* Print AD type and value */
				switch (ad_type) {
					case 0x01: /* Flags */
						u_printf("  AD: Flags (0x01): 0x%x\n", ad_val_len ? ad_val[0] : 0);
						break;
					case 0x02: /* Incomplete List of 16-bit Service Class UUIDs */
					case 0x03: /* Complete List of 16-bit Service Class UUIDs */
						u_printf("  AD: 16-bit Service UUIDs (0x%x), len=%d:\n", ad_type, ad_val_len);
						for (int j = 0; j + 1 < ad_val_len + 1; j += 2) {
							if (j + 1 >= ad_val_len) break;
							u16 uuid16 = ad_val[j] | (ad_val[j+1] << 8);
							u_printf("    - 0x%x\n", uuid16);
						}
						break;
					case 0x06: /* Incomplete List of 128-bit Service Class UUIDs */
					case 0x07: /* Complete List of 128-bit Service Class UUIDs */
						u_printf("  AD: 128-bit Service UUIDs (0x%x), len=%d\n", ad_type, ad_val_len);
						for (int j = 0; j + 15 < ad_val_len + 1; j += 16) {
							u_printf("    - ");
							for (int k = 15; k >= 0; k--) {
								u_printf("%x", ad_val[j + k]);
							}
							u_printf("\n");
						}
						break;
					case 0x08: /* Shortened Local Name */
					case 0x09: /* Complete Local Name */
						{
							/* print as string */
							char name[32];
							int copy_len = ad_val_len < (int)(sizeof(name)-1) ? ad_val_len : (int)(sizeof(name)-1);
							for (int j = 0; j < copy_len; j++) name[j] = (char)ad_val[j];
							name[copy_len] = '\0';
							u_printf("  AD: Local Name (0x%x): %s\n", ad_type, name);
						}
						break;
					case 0x0A: /* Tx Power Level */
						u_printf("  AD: Tx Power Level (0x0A): %d dBm\n", (int)(s8)ad_val[0]);
						break;
					case 0x16: /* Service Data - 16-bit UUID */
						if (ad_val_len >= 2) {
							u16 sd_uuid = ad_val[0] | (ad_val[1] << 8);
							u_printf("  AD: Service Data - 16-bit UUID: 0x%x, data_len=%d\n", sd_uuid, ad_val_len-2);
						} else {
							u_printf("  AD: Service Data (0x16) len invalid\n");
						}
						break;
					case 0xFF: /* Manufacturer Specific Data */
						u_printf("  AD: Manufacturer Specific Data (0xFF), len=%d:\n", ad_val_len);
						u_printf("    ");
						for (int j = 0; j < ad_val_len; j++) u_printf("%x", ad_val[j]);
						u_printf("\n");
						break;
					default:
						u_printf("  AD: type=0x%x, len=%d:\n    ", ad_type, ad_val_len);
						for (int j = 0; j < ad_val_len; j++) u_printf("%x", ad_val[j]);
						u_printf("\n");
						break;
				}

				/* advance ad_idx by ad_val_len */
				ad_idx += ad_val_len;
			}
		} 
		else 
		{
			// Skip printing for duplicate MAC
			u8 data_len = p[idx++];
			idx += data_len;
			idx++; // skip RSSI
		}
	}
}

void controllerInitialization(void)
{
	u8  mac_public[6];
	u8  mac_random_static[6];
	u8 adv_param_status = BLE_SUCCESS;

	/* for 512K Flash, flash_sector_mac_address equals to 0x76000, for 1M  Flash, flash_sector_mac_address equals to 0xFF000 */
	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);
	// u_printf("Public Address: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_public[5], mac_public[4], mac_public[3], mac_public[2], mac_public[1], mac_public[0]);

	blc_ll_initBasicMCU();                      //mandatory
	blc_ll_initStandby_module(mac_public);		//mandatory
	blc_ll_initAdvertising_module(mac_public); 	//legacy advertising module: mandatory for BLE slave
	blc_ll_initScanning_module(mac_public);

	adv_param_status = bls_ll_setAdvParam(  ADV_INTERVAL_30MS,
											ADV_INTERVAL_50MS,
											ADV_TYPE_NONCONNECTABLE_UNDIRECTED,
											OWN_ADDRESS_PUBLIC,
											0,
											NULL,
											BLT_ENABLE_ADV_ALL,
											ADV_FP_NONE);

	if (adv_param_status != BLE_SUCCESS) 
	{
		u_printf("ADV parameters set status: 0x%x\n", adv_param_status);
		return;
	}

	blc_ll_clearResolvingList();
	bls_ll_setAdvData( (u8 *)tbl_advData, sizeof(tbl_advData) );

	bls_ll_setAdvEnable(BLC_ADV_ENABLE);  //must: set ADV enable

	blc_ll_addScanningInAdvState();  //add scan in ADV state
	blc_ll_setScanParameter(SCAN_TYPE_PASSIVE,
							SCAN_INTERVAL_50MS,
							SCAN_INTERVAL_50MS,
							OWN_ADDRESS_PUBLIC,
							SCAN_FP_ALLOW_ADV_ANY);

	blc_hci_le_setEventMask_cmd(HCI_LE_EVT_MASK_ADVERTISING_REPORT);
	
	blc_hci_registerControllerEventHandler(controller_event_callback);

	blc_ll_setScanEnable (BLC_SCAN_ENABLE, DUP_FILTER_ENABLE);
}

void updateAdvData(u8 data, u8 position)
{
	tbl_advData[position] = data;
	bls_ll_setAdvData( (u8 *)tbl_advData, sizeof(tbl_advData) );
}