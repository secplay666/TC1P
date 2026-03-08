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

//static void blm_le_adv_report_event_handle(u8 *p);




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
				// u_u_printf("le ADV report event !\n");

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
/*static void blm_le_adv_report_event_handle(u8 *p)
{
	u8 sub_code = p[0];
	u8 num_reports = p[1];
	int idx = 2;

	(void)sub_code; // kept for clarity

	for (int r = 0; r < num_reports; r++) {
		if (idx + 9 > 255) { // basic sanity
			u_u_printf("adv report: malformed, idx overflow\n");
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
                u_u_printf("MAC cache count is %d\n", mac_cache_count);
				mac_cache_count++;
			}

			u8 data_len = p[idx++];
			u8 *adv_data = &p[idx];
			idx += data_len;

			s8 rssi = 0;
			// RSSI is present after the data according to spec
			rssi = (s8)p[idx++];

			// u_u_printf("ADV report %d: ", r);
			// u_u_printf("evtType=0x%x ", event_type);
			// u_u_printf("addrType=0x%x\n", addr_type);
			u_u_printf("===================================================\n");
			u_u_printf("addr=%x:", addr[5]);
			u_u_printf("%x:", addr[4]);
			u_u_printf("%x:", addr[3]);
			u_u_printf("%x:", addr[2]);
			u_u_printf("%x:", addr[1]);
			u_u_printf("%x\n", addr[0]);
			u_u_printf("data_len=%d, ", data_len);
			u_u_printf("RSSI=%d\n", rssi);

			// Parse Advertising Data (AD structures)
			int ad_idx = 0;
			while (ad_idx < data_len) {
				u8 ad_len = adv_data[ad_idx++];
				if (ad_len == 0) break; // no more AD structures
				if (ad_idx >= data_len) break;

				u8 ad_type = adv_data[ad_idx++];
				u8 ad_val_len = ad_len - 1; // subtract the type byte
				u8 *ad_val = &adv_data[ad_idx];

				// Print AD type and value
				switch (ad_type) {
					case 0x01: // Flags
						u_u_printf("  AD: Flags (0x01): 0x%x\n", ad_val_len ? ad_val[0] : 0);
						break;
					case 0x02: // Incomplete List of 16-bit Service Class UUIDs
					case 0x03: // Complete List of 16-bit Service Class UUIDs
						u_u_printf("  AD: 16-bit Service UUIDs (0x%x), len=%d:\n", ad_type, ad_val_len);
						for (int j = 0; j + 1 < ad_val_len + 1; j += 2) {
							if (j + 1 >= ad_val_len) break;
							u16 uuid16 = ad_val[j] | (ad_val[j+1] << 8);
							u_u_printf("    - 0x%x\n", uuid16);
						}
						break;
					case 0x06: // Incomplete List of 128-bit Service Class UUIDs
					case 0x07: // Complete List of 128-bit Service Class UUIDs
						u_u_printf("  AD: 128-bit Service UUIDs (0x%x), len=%d\n", ad_type, ad_val_len);
						for (int j = 0; j + 15 < ad_val_len + 1; j += 16) {
							u_u_printf("    - ");
							for (int k = 15; k >= 0; k--) {
								u_u_printf("%x", ad_val[j + k]);
							}
							u_u_printf("\n");
						}
						break;
					case 0x08: // Shortened Local Name
					case 0x09: // Complete Local Name
						{
							// print as string
							char name[32];
							int copy_len = ad_val_len < (int)(sizeof(name)-1) ? ad_val_len : (int)(sizeof(name)-1);
							for (int j = 0; j < copy_len; j++) name[j] = (char)ad_val[j];
							name[copy_len] = '\0';
							u_u_printf("  AD: Local Name (0x%x): %s\n", ad_type, name);
						}
						break;
					case 0x0A: // Tx Power Level
						u_u_printf("  AD: Tx Power Level (0x0A): %d dBm\n", (int)(s8)ad_val[0]);
						break;
					case 0x16: // Service Data - 16-bit UUID
						if (ad_val_len >= 2) {
							u16 sd_uuid = ad_val[0] | (ad_val[1] << 8);
							u_u_printf("  AD: Service Data - 16-bit UUID: 0x%x, data_len=%d\n", sd_uuid, ad_val_len-2);
						} else {
							u_u_printf("  AD: Service Data (0x16) len invalid\n");
						}
						break;
					case 0xFF: // Manufacturer Specific Data
						u_u_printf("  AD: Manufacturer Specific Data (0xFF), len=%d:\n", ad_val_len);
						u_u_printf("    ");
						for (int j = 0; j < ad_val_len; j++) u_u_printf("%x", ad_val[j]);
						u_u_printf("\n");
						break;
					default:
						u_u_printf("  AD: type=0x%x, len=%d:\n    ", ad_type, ad_val_len);
						for (int j = 0; j < ad_val_len; j++) u_u_printf("%x", ad_val[j]);
						u_u_printf("\n");
						break;
				}

				// advance ad_idx by ad_val_len
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
}*/

void controllerInitialization(void)
{
	u8  mac_public[6];
	u8  mac_random_static[6];
	u8 adv_param_status = BLE_SUCCESS;

	/* for 512K Flash, flash_sector_mac_address equals to 0x76000, for 1M  Flash, flash_sector_mac_address equals to 0xFF000 */
	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);
	// u_u_printf("Public Address: %02x:%02x:%02x:%02x:%02x:%02x\n", mac_public[5], mac_public[4], mac_public[3], mac_public[2], mac_public[1], mac_public[0]);

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

//////--------------------------20260225----------------------------//////
///
/** static array structure for the link list**/
#define DEV_NUM 32

enum{
	LOW_BATTERY_DETECT,
	FIRST_DETECT_CLOSER_ONE_TIME,
	CLOSER_TWO_TIMES,
	CLOEST_THREE_TIMES,
	FAR_TWO_TIMES,
	FAR_ONE_TIME
};

enum{
	IDLE,
	FIRST_TIME_DETECT,
	SECOND_DETECT_AND_CLOSER,
	THIRD_DETECT_AND_CLOSEST,
	QUIT_FROM_FIRST_TIME,
	QUIT_FROM_SECOND_TIME,
	QUIT_FROM_THIRD_TIME
} rssi_state;

typedef struct device_detect{
	u8 mac_addr[6];
	struct device_detect *next;
	u8 rssi_state;
	u32 time;
} device_detect;

device_detect dev[DEV_NUM] = {};

static device_detect *head = NULL, *tail = NULL;

u8 broadcast [6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void init_devlist(){
	  int i;
	  for (i = 0; i < DEV_NUM; i ++) {
		  memcpy(&dev[i].mac_addr[0], &broadcast, sizeof(broadcast));
		  dev[i].next = (i == DEV_NUM - 1 ? NULL : &dev[i + 1]);
		  dev[i].rssi_state = IDLE;
		  dev[i].time = 0;
	  }

	  head = NULL;
	  tail = dev;//tail means all avaliable node
}

void add_dev(u8 * addr){
	device_detect * new_node = NULL;
	device_detect * head_list = head;
	device_detect * head_list_last = NULL;

	if (head_list == NULL){//first device
		new_node = tail;
		tail = tail->next;
		head = new_node;
		memcpy(head->mac_addr, addr, 6);
		head->next = NULL;
	}
	else {
		while(head_list->next != NULL){
			head_list = head_list -> next;
		}
		head_list_last = tail;
		tail = tail->next;
		memcpy(head_list_last->mac_addr, addr, 6);
		head_list_last->next = NULL;
		head_list ->next = head_list_last;
	}
}

int find_mac_in_devlist(u8 * addr){
	device_detect * head_list = head;
	if (head_list == NULL){//first device
		add_dev(addr);
		return 1;
	}
	else {
		while(head_list != NULL){
			if (memcmp(head_list->mac_addr, addr, 6) == 0) { //find, don't change
				return 2;
			}
			head_list = head_list -> next;
		}
		add_dev(addr);//add new one
		return 3;
	}
}

void set_rssi_state(u8 * addr, int rssi_state){
	device_detect * head_list = head;
	if (head_list == NULL){//first device
		u_printf("critical warning1!!!\n");
		return;
	}
	else {
		while(head_list != NULL){
			if (memcmp(head_list->mac_addr, addr, 6) == 0) {
				//find, don't change
				head_list->rssi_state = rssi_state;
				return;
			}
			head_list = head_list -> next;
		}
		u_printf("critical warning2!!!\n");
		return;
	}
}

int get_rssi_state(u8 * addr){
	device_detect * head_list = head;
	if (head_list == NULL){//first device
		u_printf("critical warning1!!!\n");
		return -1;
	}
	else {
		while(head_list != NULL){
			if (memcmp(head_list->mac_addr, addr, 6) == 0) {
				//find, don't change
				return head_list->rssi_state;
			}
			head_list = head_list -> next;
		}
		u_printf("critical warning2!!!\n");
		return -1;
	}
}

void set_state_time(u8 * addr, u32 time){ //1s to stay this state
	device_detect * head_list = head;
	if (head_list == NULL){//first device
		u_printf("critical warning1!!!\n");
		return;
	}
	else {
		while(head_list != NULL){
			if (memcmp(head_list->mac_addr, addr, 6) == 0) {
				//find, don't change
				head_list->time = time;
				return;
			}
			head_list = head_list -> next;
		}
		u_printf("critical warning2!!!\n");
		return;
	}
}

unsigned long get_state_time(u8 * addr){
	device_detect * head_list = head;
	if (head_list == NULL){//first device
		u_printf("critical warning3!!!\n");
		return -1;
	}
	else {
		while(head_list != NULL){
			if (memcmp(head_list->mac_addr, addr, 6) == 0) {
				//find, don't change
				return head_list->time;
			}
			head_list = head_list -> next;
		}
		u_printf("critical warning4!!!\n");
		return -1;
	}
}


///------------------////
volatile int waveform_flag = 0;//0 is avaliable

void write_wave(int waveform){
	unsigned char write_result = 0;
	switch (waveform){
		case LOW_BATTERY_DETECT:


		break;
		case FIRST_DETECT_CLOSER_ONE_TIME:
			//read 0x07
			write_result = i2c_read_byte(0x07, 1);
		    u_printf("[closer1]read 0x07, read value is 0x%02x\n", write_result);
		   //set mode[1:0] 1
		    i2c_write_byte(0x07, 1, write_result & 0xFD);
			write_result = i2c_read_byte(0x07, 1);
		    u_printf("[closer1]read 0x07, read value is 0x%02x\n", write_result);

		    i2c_write_byte(0x08, 1, 0x88);

			i2c_write_byte(0x19, 1, 0x00);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[closer1]reset repeat, read 0x19, read value is 0x%02x\n", write_result);

		    //write wave form
		    i2c_write_byte(0x0F, 1, 0x54);//wave form
		 	write_result = i2c_read_byte(0x0F, 1);
		    u_printf("[closer1]read 0x0F, read value is 0x%02x\n", write_result);

		    i2c_write_byte(0x10, 1, 0x00);//wave form
		 	write_result = i2c_read_byte(0x10, 1);
		    u_printf("[closer1] set 0, read 0x10, read value is 0x%02x\n", write_result);

		    // set Go
		    i2c_write_byte(0x0C, 1, (unsigned char)0x01);
		    write_result = i2c_read_byte(0x0C, 1);
		    u_printf("[closer1]write 0x01 in 0x0C, read value is 0x%02x\n", write_result);
		    break;
		case FAR_ONE_TIME:
			//read 0x07
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[far1]read 0x07, read value is 0x%02x\n", write_result);
		   //set mode[1:0] 1
			i2c_write_byte(0x07, 1, write_result & 0xFD);
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[far1]read 0x07, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x08, 1, 0x88);

			i2c_write_byte(0x19, 1, 0x00);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[far1]reset repeat, read 0x19, read value is 0x%02x\n", write_result);

			//write wave form
			i2c_write_byte(0x0F, 1, 0x36);//wave form
			write_result = i2c_read_byte(0x0F, 1);
			u_printf("[far1]read 0x0F, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x10, 1, 0x00);//wave form
			write_result = i2c_read_byte(0x10, 1);
			u_printf("[far1] set 0, read 0x10, read value is 0x%02x\n", write_result);
			// set Go
			i2c_write_byte(0x0C, 1, (unsigned char)0x01);
			write_result = i2c_read_byte(0x0C, 1);
			u_printf("[far1]write 0x01 in 0x0C, read value is 0x%02x\n", write_result);
			break;
		case CLOSER_TWO_TIMES:
			//read 0x07
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[closer2]read 0x07, read value is 0x%02x\n", write_result);
		   //set mode[1:0] 1
			i2c_write_byte(0x07, 1, write_result & 0xFD);
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[closer2]read 0x07, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x08, 1, 0x88);

			i2c_write_byte(0x19, 1, 0x00);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[closer2]reset repeat, read 0x19, read value is 0x%02x\n", write_result);

			//write wave form
			i2c_write_byte(0x0F, 1, 0x54);//wave form
			write_result = i2c_read_byte(0x0F, 1);
			u_printf("[closer2]read 0x0F, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x10, 1, 0x82);//delay
			write_result = i2c_read_byte(0x10, 1);
			u_printf("[closer2]read 0x10, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x11, 1, 0x00);//delay
			write_result = i2c_read_byte(0x11, 1);
			u_printf("[closer2]set 0 ,read 0x11, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x19, 1, 0x01);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[closer2]read 0x19, read value is 0x%02x\n", write_result);

			// set Go
			i2c_write_byte(0x0C, 1, (unsigned char)0x01);
			write_result = i2c_read_byte(0x0C, 1);
			u_printf("[closer2]write 0x01 in 0x0C, read value is 0x%02x\n", write_result);
			break;
		case FAR_TWO_TIMES:
			//read 0x07
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[far2]read 0x07, read value is 0x%02x\n", write_result);
		   //set mode[1:0] 1
			i2c_write_byte(0x07, 1, write_result & 0xFD);
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[far2]read 0x07, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x08, 1, 0x88);

			i2c_write_byte(0x19, 1, 0x00);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[far2]reset repeat, read 0x19, read value is 0x%02x\n", write_result);

			//write wave form
			i2c_write_byte(0x0F, 1, 0x36);//wave form
			write_result = i2c_read_byte(0x0F, 1);
			u_printf("[far2]read 0x0F, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x10, 1, 0x82);//delay
			write_result = i2c_read_byte(0x10, 1);
			u_printf("[far2]read 0x10, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x11, 1, 0x00);//delay
			write_result = i2c_read_byte(0x11, 1);
			u_printf("[far2]set 0 ,read 0x11, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x19, 1, 0x01);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[far2]read 0x19, read value is 0x%02x\n", write_result);
			// set Go
			i2c_write_byte(0x0C, 1, (unsigned char)0x01);
			write_result = i2c_read_byte(0x0C, 1);
			u_printf("[far2]write 0x01 in 0x0C, read value is 0x%02x\n", write_result);

			break;
		case CLOEST_THREE_TIMES:
			//read 0x07
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[closer3]read 0x07, read value is 0x%02x\n", write_result);
		   //set mode[1:0] 1
			i2c_write_byte(0x07, 1, write_result & 0xFD);
			write_result = i2c_read_byte(0x07, 1);
			u_printf("[closer3]read 0x07, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x08, 1, 0x88);

			i2c_write_byte(0x19, 1, 0x00);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[closer3]reset repeat, read 0x19, read value is 0x%02x\n", write_result);

			//write wave form
			i2c_write_byte(0x0F, 1, 0x54);//wave form
			write_result = i2c_read_byte(0x0F, 1);
			u_printf("[closer3]read 0x0F, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x10, 1, 0x82);//delay
			write_result = i2c_read_byte(0x10, 1);
			u_printf("[closer3]read 0x10, read value is 0x%02x\n", write_result);

			i2c_write_byte(0x19, 1, 0x02);//repeat 0x01 means two times
			write_result = i2c_read_byte(0x19, 1);
			u_printf("[closer3]read 0x19, read value is 0x%02x\n", write_result);
			// set Go
			i2c_write_byte(0x0C, 1, (unsigned char)0x01);
			write_result = i2c_read_byte(0x0C, 1);
			u_printf("[closer3]write 0x01 in 0x0C, read value is 0x%02x\n", write_result);
			break;
		}
		waveform_flag = 0;



}

/**
 * @brief      call this function when  HCI Controller Event :HCI_SUB_EVT_LE_ADVERTISING_REPORT
 *     		   after controller is set to scan state, it will report all the ADV packet it received by this event
 * @param[in]  p - data pointer of event
 * @return     0
 */

int blm_le_adv_report_event_handle(u8 *p)
{
	event_adv_report_t *pa = (event_adv_report_t *)p;
	s8 rssi = pa->data[pa->len];
	u8 keyword [7] = {'z','h','a','o','h','u','i'};//7A 68 61 6F 68 75 69
	u8 broadcast [6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	enum{
		IDLE,
		FIRST_TIME_DETECT,
		SECOND_DETECT_AND_CLOSER,
		THIRD_DETECT_AND_CLOSEST,
		QUIT_FROM_FIRST_TIME,
		QUIT_FROM_SECOND_TIME,
		QUIT_FROM_THIRD_TIME
	};

	enum{
		LOW_BATTERY_DETECT,
		FIRST_DETECT_CLOSER_ONE_TIME,
		CLOSER_TWO_TIMES,
		CLOEST_THREE_TIMES,
		FAR_TWO_TIMES,
		FAR_ONE_TIME
	};


	enum{
		WEAK_IN = -75,
		MIDDLE_IN = -45,
		WEAK_OUT = -85,
		MIDDLE_OUT = -55
	};

	//rssi intensity: -20dB <10cm  -40dB: 0.3-0.5m  -50dB: 1m  -60dB:2-3m -70dB: 5-8m

#define SECOND_TIME (1 * 1000 * 1000) //1s


///////////////----------------------///////////////////////

	if (pa->mac[3] == 0x38 && pa->mac[4] == 0xc1 && pa->mac[5] == 0xa4) {
	    /*u_printf("telink device packet received! subcode=%d nreport=%d event_type=%d adr_type=%d\n",
	           pa->subcode, pa->nreport, pa->event_type, pa->adr_type);

	    u_printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
	           pa->mac[0], pa->mac[1], pa->mac[2],
	           pa->mac[3], pa->mac[4], pa->mac[5]);
*/
	    u_printf("AdvData[%u]: ", pa->len);
		int i;
	    for (i = 0; i < pa->len; ++i) u_printf("%02X ", pa->data[i]);
	    u_printf("\n");
	    u_printf("rssi is %d, gpio_value is %d, lowpowerin is %d, time is %d, gpio_oldvalue is %d\n", rssi, pa->data[23], pa->data[24], pa->data[25], pa->data[26]);
	    u_printf("\n");



	    if (memcmp(&pa->data[0], keyword, sizeof(keyword)) == 0){//keyword detected
	    	//u_printf("keyword detect ok\n");
	    	int ret = find_mac_in_devlist(&pa->mac[0]);
	    	//u_printf("find mac ok\n");
	    	if (ret == 1 || ret == 3){
	    		if(waveform_flag) {
	    			return 0;
	    		}
	    		u_printf("new device detected! \n");
	    		set_rssi_state(&pa->mac[0], FIRST_TIME_DETECT);
	    		set_state_time(&pa->mac[0],clock_time());
	    		//vibrate oncewaveform_flag
	    		waveform_flag = 1;
	    		write_wave(FIRST_DETECT_CLOSER_ONE_TIME);
	    	}
	    	else if (ret == 2) {
	    		if(waveform_flag || !clock_time_exceed(get_state_time(&pa->mac[0]), SECOND_TIME)) {
	    			u_printf("state keep time less than 1s\n");
	    			return 0;
	    		}

	    	    if (get_rssi_state(&pa->mac[0]) == FIRST_TIME_DETECT && rssi > MIDDLE_IN){
	    	    	u_printf("device cloestttt!\n");
	    	    	set_rssi_state(&pa->mac[0], THIRD_DETECT_AND_CLOSEST);
	    	    	//vibrate three times closer mode
	    	    	waveform_flag = 1;
	    	    	write_wave(CLOEST_THREE_TIMES);
	    	    }
	    		else if (get_rssi_state(&pa->mac[0]) == FIRST_TIME_DETECT && rssi > WEAK_IN){
	    			u_printf("device closer!\n");
	    			set_rssi_state(&pa->mac[0], SECOND_DETECT_AND_CLOSER);
	    			//vibrate twice closer mode
	    			waveform_flag = 1;
	    			write_wave(CLOSER_TWO_TIMES);
	    		}
	    		else if (get_rssi_state(&pa->mac[0]) ==  SECOND_DETECT_AND_CLOSER && rssi > MIDDLE_IN){
	    			u_printf("device cloestttt!\n");
	    			set_rssi_state(&pa->mac[0], THIRD_DETECT_AND_CLOSEST);
	    			//vibrate three times closer mode
	    			waveform_flag = 1;
	    			write_wave(CLOEST_THREE_TIMES);
	    		}
	    		else if (get_rssi_state(&pa->mac[0]) ==  SECOND_DETECT_AND_CLOSER && rssi < WEAK_OUT){
	    			u_printf("device far from closer!\n");
	    			set_rssi_state(&pa->mac[0], FIRST_TIME_DETECT);
	    			//vibrate once far mode
	    			waveform_flag = 1;
	    			write_wave(FAR_ONE_TIME);
	    		}
	    		else if ((get_rssi_state(&pa->mac[0]) ==  THIRD_DETECT_AND_CLOSEST && rssi < WEAK_OUT)){
	    			u_printf("device far from closer!\n");
	    			set_rssi_state(&pa->mac[0], FIRST_TIME_DETECT);
	    			//vibrate once far mode
	    			waveform_flag = 1;
	    			write_wave(FAR_ONE_TIME);
	    		}
	    		else if (get_rssi_state(&pa->mac[0]) ==  THIRD_DETECT_AND_CLOSEST && rssi < MIDDLE_OUT){
	    			u_printf("device far from cloestttt!\n");
	    			set_rssi_state(&pa->mac[0], SECOND_DETECT_AND_CLOSER);
	    			//vibrate twice far mode
	    			waveform_flag = 1;
	    			write_wave(FAR_TWO_TIMES);
	    		}
	    	}
	    	else {
	    		u_printf("unknown error!!\n");
	    	}

	    }
	    else {
	    	u_printf("memcmp result is &d", memcmp(&pa->data[0], keyword, sizeof(keyword)));
	    }

	}
}
