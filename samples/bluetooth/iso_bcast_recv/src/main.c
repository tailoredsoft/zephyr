/*
 * Copyright (c) 2021-2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/sys/byteorder.h>

/*============================================================================*/
/* Compile time config validation */
#if CONFIG_BT_CTLR_ADV_ISO_PDU_LEN_MAX<CONFIG_BT_ISO_TX_MTU
	#error "CONFIG_BT_CTLR_ADV_ISO_PDU_LEN_MAX must >= CONFIG_BT_ISO_TX_MTU"
#endif
#if CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE<(CONFIG_BT_ISO_TX_MTU+8)
	#error "CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE must >= (CONFIG_BT_ISO_TX_MTU+8)"
#endif
#if CONFIG_BT_CTLR_ISO_TX_SDU_LEN_MAX<CONFIG_BT_ISO_TX_MTU
	#error "CONFIG_BT_CTLR_ISO_TX_SDU_LEN_MAX must >= CONFIG_BT_ISO_TX_MTU"
#endif
/*============================================================================*/

#define CONFIGURE_BIS2_AS_RECV

#define BIS_INDEX_INVALID        (0)
#define BIG_SDU_INTERVAL_US      (10000)
#define BUF_ALLOC_TIMEOUT_US     (BIG_SDU_INTERVAL_US * 2U) /* milliseconds */
#define BIG_TERMINATE_TIMEOUT_US (CONFIG_BIG_TERMINATE_TIMEOUT_SECONDS * USEC_PER_SEC) 

#define BIS_ISO_CHAN_COUNT 2
NET_BUF_POOL_FIXED_DEFINE(bis_tx_pool, BIS_ISO_CHAN_COUNT,
			  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static K_SEM_DEFINE(sem_big_cmplt, 0, BIS_ISO_CHAN_COUNT);
static K_SEM_DEFINE(sem_big_term, 0, BIS_ISO_CHAN_COUNT);
static K_SEM_DEFINE(sem_iso_data, CONFIG_BT_ISO_TX_BUF_COUNT,
				   CONFIG_BT_ISO_TX_BUF_COUNT);

#define INITIAL_TIMEOUT_COUNTER (BIG_TERMINATE_TIMEOUT_US / BIG_SDU_INTERVAL_US)

/* converts iso chan pointer into to a bis_index in the range 1..N and o
   deemed invalid using the pointer values to search in the bis array */
uint8_t get_bis_idx(struct bt_iso_chan *chan);


static uint16_t seq_num;
static uint32_t iso_send_count=0U;
static uint32_t     iso_frame_count; /* incremented once per iso interval */
static uint32_t     iso_bis_lost_count[BIS_ISO_CHAN_COUNT];

static void iso_bis_connected(struct bt_iso_chan *chan)
{
	const struct bt_iso_chan_path hci_path = {
		.pid = BT_ISO_DATA_PATH_HCI,
		.format = BT_HCI_CODING_FORMAT_TRANSPARENT,
	};
	int err;

	printk("ISO Channel %p connected\n", chan);

	uint8_t bis_idx = get_bis_idx(chan);
	if(bis_idx == BIS_INDEX_INVALID) {
		printk("Invalid BIS channel %p (index %u)\n", chan, bis_idx);
		return;
	}

	seq_num = 0U;
	iso_send_count=0U;

	uint8_t dir=BT_HCI_DATAPATH_DIR_HOST_TO_CTLR; /* assume tx bis */
#if defined(CONFIGURE_BIS2_AS_RECV)	
	if(bis_idx>1){
		/* BIS2 and onwards are always RX */
		dir = BT_HCI_DATAPATH_DIR_CTLR_TO_HOST;
	} 
#endif	
	err = bt_iso_setup_data_path(chan, BT_HCI_DATAPATH_DIR_HOST_TO_CTLR, &hci_path);
	if (err != 0) {
		if(dir == BT_HCI_DATAPATH_DIR_CTLR_TO_HOST) {
			printk("Failed to setup ISO RX data path: %d\n", err);
		} else {
			printk("Failed to setup ISO TX data path: %d\n", err);
		}
	} else {
			printk("  Set BIS%u datapath = ", bis_idx);
		if(dir == BT_HCI_DATAPATH_DIR_CTLR_TO_HOST) {
			printk("RECV\n");
		} else {
			printk("SEND\n");
		}
	}

	iso_frame_count=0;

	k_sem_give(&sem_big_cmplt);
}

static void iso_bis_disconnected(struct bt_iso_chan *chan, uint8_t reason)
{
	printk("ISO Channel %p disconnected with reason 0x%02x\n",
	       chan, reason);
	k_sem_give(&sem_big_term);
}

static void iso_bis_sent(struct bt_iso_chan *chan)
{
	uint8_t bis_idx = get_bis_idx(chan);
	if(bis_idx == BIS_INDEX_INVALID) {
		printk("Invalid BIS channel %p (index %u)\n", chan, bis_idx);
		return;
	}

	/* Increment frame count if this is BIS1 as that is always the start of the frame*/
	if(bis_idx==1){
		iso_frame_count++;
	}

	k_sem_give(&sem_iso_data);
}

#if defined(CONFIGURE_BIS2_AS_RECV)	
static void iso_bis_recv(struct bt_iso_chan *chan, const struct bt_iso_recv_info *info,
		struct net_buf *buf)
{
	/*
	  The following code is used to print the received data from all BIS
	  channels. 
	  The data is expected to be in the format:
 	   S d < n:[s,m,c,l] n:[s,m,c,l] n:[s,m,c,l] n:[s,m,c,l] ...
		 where for each BIS channel ...
		  d is the delta time in milliseconds since last print
		  n is the BIS index 1..N as per the callback chan pointer
		  s is the source id 1=Primary, 10..99=Secondary
		  m is the source bis index in the payload
		  c is the packet count provided by source in the payload
		  l is the packet lost count for this bis channel 'n' since last print
	*/
	uint8_t bis_idx = get_bis_idx(chan);
	if(bis_idx == BIS_INDEX_INVALID) {
		printk("Invalid BIS channel %p (index %u)\n", chan, bis_idx);
		return;
	}
	uint8_t index = bis_idx-1;

	/* Enable the following code to print the timestamps of the first 
	   few callbacks. Typical output will be 
	   Log: 1:0 1:10000 1:2 1:10000 1:4294967295 1:10000 1:0 1:10000 1:0 1:10000
	   where the 4294967295 implies that most likely the timestamp did not exist
	   */	
#if 0
	#define LOG_DIFF_ARRAY_SIZE 12  //must be >=8
	static bool log_enable = true;
	static uint32_t log_idx=0;
	static uint8_t log_bis_idx[LOG_DIFF_ARRAY_SIZE];
	static uint32_t log_time_diff[LOG_DIFF_ARRAY_SIZE];
	if(log_enable){
		if(log_idx<LOG_DIFF_ARRAY_SIZE) {
			/* log the bis_idx and time diff for this callback */
			log_bis_idx[log_idx] = bis_idx;
			//log_time_diff[log_idx] = k_uptime_get();
			log_time_diff[log_idx] = info->ts;
			log_idx++;
		} 
			
		if(log_idx==LOG_DIFF_ARRAY_SIZE) {
			/* if log is full then print it and reset */
			printk("\nLog: ");
			for(uint8_t i=1; i<LOG_DIFF_ARRAY_SIZE-1; i++) {
				printk("%u:%u ", log_bis_idx[i], log_time_diff[i]-log_time_diff[i-1]);
			}
			printk("\n");
			log_enable=false;
		}
	}
#endif

	/* if invalid packet then increment appropriate count*/
	if((info->flags & BT_ISO_FLAGS_VALID)==0) {
		iso_bis_lost_count[index]++;
	}

	if ((iso_frame_count % CONFIG_ISO_PRINT_INTERVAL) == 0) {
		/* Print this BIS in format n:[s,m,c,l] */
		uint8_t *payload = buf->data;
	    
		/* calculate the delta time since the last print */
		static k_ticks_t old_ms = 0;
		k_ticks_t now_ms = k_uptime_get();
		k_ticks_t delta_ms = now_ms - old_ms;
		old_ms = now_ms;

		/* Print the current BIS number for this callback and a newline also if BIS1*/
		if(bis_idx==1){
			printk("\nS %u < %u:", (uint32_t)delta_ms, bis_idx);
		} else {
			printk(" %u:", bis_idx);
		}
		/* print data if valid in this callback */
		if(info->flags & BT_ISO_FLAGS_VALID){
			uint32_t count = *(uint32_t *)&payload[0];
			printk("[%u,%u,%u,%u]", payload[4], payload[5], count, iso_bis_lost_count[index]);
		} else {
			printk("[-,-,-,%u]", iso_bis_lost_count[index]);
		}
		/* reset the lost count for this BIS channel */
		iso_bis_lost_count[index]=0;
	}
}
#endif

static struct bt_iso_chan_ops iso_ops = {
#if defined(CONFIGURE_BIS2_AS_RECV)	
	.recv		    = iso_bis_recv,
#endif	
	.connected	    = iso_bis_connected,
	.disconnected	= iso_bis_disconnected,
	.sent           = iso_bis_sent,
};

#if defined(CONFIGURE_BIS2_AS_RECV)	
static struct bt_iso_chan_io_qos iso_rx_qos = {
	0 /* will be updated when sync'd */
};

static struct bt_iso_chan_qos bis_iso_qos_rx = {
	.rx = &iso_rx_qos,
};

#endif	

static struct bt_iso_chan_io_qos iso_tx_qos = {
	.sdu = CONFIG_BT_ISO_TX_MTU, /* bytes */
	.rtn = 1,
	.phy = BT_GAP_LE_PHY_2M,
};

static struct bt_iso_chan_qos bis_iso_qos = {
	.tx = &iso_tx_qos,
};

static struct bt_iso_chan bis_iso_chan[] = {
	{ .ops = &iso_ops, .qos = &bis_iso_qos, },
#if defined(CONFIGURE_BIS2_AS_RECV)	
	{ .ops = &iso_ops, .qos = &bis_iso_qos_rx, },
#else
	{ .ops = &iso_ops, .qos = &bis_iso_qos, },
#endif	
};

static struct bt_iso_chan *bis[] = {
	&bis_iso_chan[0],
	&bis_iso_chan[1],
};

static struct bt_iso_big_create_param big_create_param = {
	.num_bis = BIS_ISO_CHAN_COUNT,
	.bis_channels = bis,
	.interval = BIG_SDU_INTERVAL_US, /* in microseconds */
	.latency = 10, /* in milliseconds */
	.packing = (IS_ENABLED(CONFIG_ISO_PACKING_INTERLEAVED) ?
		    BT_ISO_PACKING_INTERLEAVED :
		    BT_ISO_PACKING_SEQUENTIAL),
	.framing = 0, /* 0 - unframed, 1 - framed */
};

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

uint8_t get_bis_idx(struct bt_iso_chan *chan)
{
	/* valid bis index values are 1 .. CONFIG_BT_ISO_MAX_CHAN*/
	if(chan) {
		if(chan->bis_index > 0 && chan->bis_index <= BIS_ISO_CHAN_COUNT) {
			return chan->bis_index;
		}
	}
	return BIS_INDEX_INVALID;
}

int main(void)
{
	/* Some controllers work best while Extended Advertising interval to be a multiple
	 * of the ISO Interval minus 10 ms (max. advertising random delay). This is
	 * required to place the AUX_ADV_IND PDUs in a non-overlapping interval with the
	 * Broadcast ISO radio events.
	 * For 10ms SDU interval a extended advertising interval of 60 - 10 = 50 is suitable
	 */
	const uint16_t adv_interval_ms = 60U;
	const uint16_t ext_adv_interval_ms = adv_interval_ms - 10U;
	const struct bt_le_adv_param *ext_adv_param = BT_LE_ADV_PARAM(
		BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY , 
		BT_GAP_MS_TO_ADV_INTERVAL(ext_adv_interval_ms),
		BT_GAP_MS_TO_ADV_INTERVAL(ext_adv_interval_ms), 
		NULL);
	const struct bt_le_per_adv_param *per_adv_param = BT_LE_PER_ADV_PARAM(
		BT_GAP_MS_TO_PER_ADV_INTERVAL(adv_interval_ms),
		BT_GAP_MS_TO_PER_ADV_INTERVAL(adv_interval_ms), BT_LE_PER_ADV_OPT_NONE);
#if CONFIG_BIG_TERMINATE_TIMEOUT_SECONDS>0		
	uint32_t timeout_counter = INITIAL_TIMEOUT_COUNTER;
#endif /* CONFIG_BIG_TERMINATE_TIMEOUT_SECONDS>0 */
	struct bt_le_ext_adv *adv;
	struct bt_iso_big *big;
	int err;

	/*========================================================================*/
	/* Validation of configs at run-time - if none of these are valid there is 
	   no need to proceed any further 
	*/
	
	if(iso_tx_qos.sdu != CONFIG_BT_ISO_TX_MTU){
		printk("iso_tx_qos.sdu %u != CONFIG_BT_ISO_TX_MTU %u\n",
		       iso_tx_qos.sdu, CONFIG_BT_ISO_TX_MTU);
		return 0;
	}

	/*========================================================================*/

	uint8_t iso_data[CONFIG_BT_ISO_TX_MTU] = { 0 };

	printk("Starting ISO Broadcast Demo\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}
   
	/* Create a non-connectable advertising set */
	err = bt_le_ext_adv_create(ext_adv_param, NULL, &adv);
	if (err) {
		printk("Failed to create advertising set (err %d)\n", err);
		return 0;
	}

	/* Set advertising data to have complete local name set */
	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Failed to set advertising data (err %d)\n", err);
		return 0;
	}

	/* Set periodic advertising parameters */
	err = bt_le_per_adv_set_param(adv, per_adv_param);
	if (err) {
		printk("Failed to set periodic advertising parameters"
		       " (err %d)\n", err);
		return 0;
	}

	/* Enable Periodic Advertising */
	err = bt_le_per_adv_start(adv);
	if (err) {
		printk("Failed to enable periodic advertising (err %d)\n", err);
		return 0;
	}

	/* Start extended advertising */
	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("Failed to start extended advertising (err %d)\n", err);
		return 0;
	}

	/* Create BIG */
	err = bt_iso_big_create(adv, &big_create_param, &big);
	if (err) {
		printk("Failed to create BIG (err %d)\n", err);
		return 0;
	}

	for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
		printk("Waiting for BIG complete chan %u...\n", chan);
		err = k_sem_take(&sem_big_cmplt, K_FOREVER);
		if (err) {
			printk("failed (err %d)\n", err);
			return 0;
		}
		printk("BIG create complete chan %u.\n", chan);
	}

#if CONFIG_BIG_TERMINATE_TIMEOUT_SECONDS==0
	printk("BIG Terminate timeout disabled\n");
#endif		
	while (true) {
		/* Send all appropriate BISes in the BIG */
		uint32_t bis_tx_enabled=1;
		for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
			struct net_buf *buf;
			int ret;

			if(bis_tx_enabled & CONFIG_BIS_SEND_PAYLOAD_MASK){
				buf = net_buf_alloc(&bis_tx_pool, K_USEC(BUF_ALLOC_TIMEOUT_US));
				if (!buf) {
					printk("Data buffer allocate timeout on channel"
						" %u\n", chan);
					return 0;
				}

				ret = k_sem_take(&sem_iso_data, K_USEC(BUF_ALLOC_TIMEOUT_US));
				if (ret) {
					printk("k_sem_take for ISO data sent failed\n");
					net_buf_unref(buf);
					return 0;
				}

				net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);
				sys_put_le32(iso_send_count, &iso_data[0]);
				iso_data[4] = 0x01; /* from BIG creator */
				iso_data[5] = chan + 1; /* BIS index */
				net_buf_add_mem(buf, iso_data, sizeof(iso_data));
				ret = bt_iso_chan_send(&bis_iso_chan[chan], buf, seq_num);
				if (ret < 0) {
					printk("Unable to broadcast data on channel %u"
						" : %d", chan, ret);
					net_buf_unref(buf);
					return 0;
				}

	#if CONFIG_ISO_PRINT_INTERVAL>1			
				/* 
				Print the BIS payload content, ensuring if first bis then
				a new line is started .
				When all BISes are printed a single line will be shown as
					B(2)>  1:[9375,1,1] 2:[9375,1,2]
					where B(2) means there are 2 bis channels and
					N[C,M,P] means 
						N - BIS Index 1..N as received 
						C - Packet count since BIG creation
						M - Who sent the packet
						P - BIS Index 1..N echoed in payload
				*/
				if ((iso_send_count % CONFIG_ISO_PRINT_INTERVAL) == 0) {
					if(chan==0){
						printk("\nP(%u)>",BIS_ISO_CHAN_COUNT);
					}
					printk(" %u:[%u,1,%u]",(chan+1),iso_send_count,(chan+1));
				}
	#endif	
			}
			bis_tx_enabled = bis_tx_enabled << 1;
		}

#if CONFIG_ISO_PRINT_INTERVAL==0	
		/* in this case just print the counter in one line per frame as all
		   bis contain the same data */
		if ((iso_send_count % CONFIG_ISO_PRINT_INTERVAL) == 0) {
			printk("Sending value %u\n", iso_send_count);
		}
#endif			

		iso_send_count++;
		seq_num++;

#if CONFIG_BIG_TERMINATE_TIMEOUT_SECONDS>0		
		timeout_counter--;
		if (!timeout_counter) {
			timeout_counter = INITIAL_TIMEOUT_COUNTER;

			printk("BIG Terminate...");
			err = bt_iso_big_terminate(big);
			if (err) {
				printk("failed (err %d)\n", err);
				return 0;
			}
			printk("done.\n");

			for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT;
			     chan++) {
				printk("Waiting for BIG terminate complete"
				       " chan %u...\n", chan);
				err = k_sem_take(&sem_big_term, K_FOREVER);
				if (err) {
					printk("failed (err %d)\n", err);
					return 0;
				}
				printk("BIG terminate complete chan %u.\n",
				       chan);
			}

			printk("Create BIG...");
			err = bt_iso_big_create(adv, &big_create_param, &big);
			if (err) {
				printk("failed (err %d)\n", err);
				return 0;
			}
			printk("done.\n");

			for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT;
			     chan++) {
				printk("Waiting for BIG complete chan %u...\n",
				       chan);
				err = k_sem_take(&sem_big_cmplt, K_FOREVER);
				if (err) {
					printk("failed (err %d)\n", err);
					return 0;
				}
				printk("BIG create complete chan %u.\n", chan);
			}
		}
#endif /* CONFIG_BIG_TERMINATE_TIMEOUT_SECONDS>0 */		
	}
}
