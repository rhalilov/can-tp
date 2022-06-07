/*
 * fake_can_linux.c
 *
 *  Created on: May 21, 2022
 *      Author: refo
 *
 * https://stackoverflow.com/questions/4812891/fork-and-pipes-in-c
 * https://stackoverflow.com/questions/48241561/blocking-pipe-c-linux
 * https://stackoverflow.com/questions/16400820/how-to-use-posix-semaphores-on-forked-processes-in-c
 *
 */

#include <stdio.h>			//printf()
#include <stdlib.h>			//exit(), malloc(), free()
#include <semaphore.h>	//sem_open(), sem_destroy(), sem_wait()..
#include <sys/mman.h>	//mmap()
#include <stdatomic.h>	//atomic_load(), atomic_store()
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "fake_can_linux.h"
#include "cbtimer_lin.h"

#define fake_can_log(__fmt, ...) printf("\t\t"__fmt, ## __VA_ARGS__)
//#define fake_can_log

enum candrv_rx_stat_e {
	CANLL_RX_STATUS_WAITING = 0,
	CANLL_RX_STATUS_RECEIVED
};

enum candrv_frame_type_e {
	CANLL_FRAMEt_FRAME = 0,
	CANLL_FRAMEt_ACK
};

typedef union {
	struct __attribute__((packed)) {
		struct __attribute__((packed)) {
			uint32_t ack:8;
			uint32_t id:24;
		};
		uint8_t idt;
		uint8_t dlc;
		uint8_t data[8];
	};
	uint8_t u8[14];
} fake_can_phy_t;

enum fake_can_tx_status_e {
	CANDRV_TX_IDLE = 0,
	CANDRV_TRANSMITTING,
	CANDRV_TX_DONE
};

static const char *can_ll_pear_str[] = {
		FOREACH_CAN_LL_PEER_TYPE(GENERATE_STRING)
};

static sem_t tx_done_sem;
static atomic_int tx_ack;
static cbtimer_t tx_timer_us;
fake_can_phy_t fake_can_phy;
static long candrv_tx_delay_us;
static int can_sndr_pipe[2];
static int can_rcvr_pipe[2];
FILE *tx_stream, *rx_stream;
static fake_can_phy_t can_frame;
static char *candrv_name;
static void *cb_params;
pthread_t tx_ack_thrd_id;

int fake_can_rx_task(void *params)
{
	int loop = 0;
	fake_can_log("%s fake_can_rx_task PID=%d Waiting to receive data\n",
				candrv_name, getpid()); fflush(0);
	fake_can_phy_t can_frame;
	ssize_t rlen = fread(can_frame.u8, 1, sizeof(fake_can_phy_t), rx_stream);
//	printf("------------------- %s fake_can_rx_task rlen=%ld ack=0x%x\n",
//			candrv_name, rlen, can_frame.u8[0]); fflush(0);
	if (rlen == sizeof(fake_can_phy_t)) {
		fake_can_log("%s: Received %ld bytes ", candrv_name, (long)rlen);
		fake_can_log("id=0x%08x idt=%d dlc=%d: 0x%02x 0x%02x 0x%02x 0x%02x "
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
			can_frame.id, can_frame.idt, can_frame.dlc,
			can_frame.data[0], can_frame.data[1], can_frame.data[2], can_frame.data[3],
			can_frame.data[4], can_frame.data[5], can_frame.data[6], can_frame.data[7]);
		fflush(0);

		//put the transmission delay
		usleep(candrv_tx_delay_us);

		//Send back the acknowledge to the transmitter (either Sender or Receiver)
		uint8_t tx_ack = CANLL_FRAMEt_ACK;
		ssize_t wlen = fwrite(&tx_ack, 1, 1, tx_stream);
		fake_can_log("%s fake_can_rx_task PID=%d Sent ACK.\n",
					candrv_name, getpid()); fflush(0);

		//Send to to the transport/network layer Receiver L_Data.ind
		fake_canrx_cb(can_frame.id, can_frame.idt, can_frame.dlc, can_frame.data, params);
	} else {
		fake_can_log("RX rdlen=%ld\n", rlen); fflush(0);
		if (can_frame.ack = CANLL_FRAMEt_ACK) {
			usleep(10000);
		}
	}
	fake_can_log("%s fake_can_rx_task PID=%d Done.\n",
				candrv_name, getpid()); fflush(0);
}

void *fake_can_tx_ack_thread(void *arg)
{
	uint8_t confirm;
	printf(" \b"); fflush(0);//it is not working without this line
	ssize_t rlen = fread(&confirm, 1, 1, rx_stream);

	cbtimer_stop(&tx_timer_us);
	atomic_store(&tx_ack, 1);
	sem_post(&tx_done_sem);
}

int fake_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	can_frame.ack = CANLL_FRAMEt_FRAME;
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	fake_can_log("%sNB: ", candrv_name);
	fake_can_log("%s: SendingNB %ld bytes\n", candrv_name, sizeof(fake_can_phy_t));

	ssize_t wlen = fwrite(can_frame.u8, 1, sizeof(fake_can_phy_t), tx_stream);

	pthread_create(&tx_ack_thrd_id, NULL, fake_can_tx_ack_thread, NULL);

	sem_wait(&tx_done_sem);

	fake_cantx_confirm_cb(cb_params);
}

void fake_can_tx_toutus_cb(struct cbtimer_s *timer)
{
	atomic_store(&tx_ack, 0);
	sem_post(&tx_done_sem);
}

int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, long tout_us)
{
	can_frame.ack = CANLL_FRAMEt_FRAME;
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	fake_can_log("%s : ", candrv_name);
	fake_can_log("%s: Sending %ld bytes\n", candrv_name, sizeof(fake_can_phy_t));

	ssize_t wlen = fwrite(can_frame.u8, 1, sizeof(fake_can_phy_t), tx_stream);

	atomic_store(&tx_ack, 0);

	cbtimer_start(&tx_timer_us, tout_us);

	pthread_create(&tx_ack_thrd_id, NULL, fake_can_tx_ack_thread, NULL);

	sem_wait(&tx_done_sem);

	return (atomic_load(&tx_ack) == 1) ? 0 : -1;
}

int fake_can_init(long tx_delay_us, char *name, void *params, uint8_t candrv_sndr_rcvr)
{
	candrv_tx_delay_us = tx_delay_us;
//	candrv_sndr_rcvr = sndr_rcvr;
	candrv_name = name;
	cb_params = params;

	printf("Initializing %s \n", can_ll_pear_str[candrv_sndr_rcvr]);

	sem_init(&tx_done_sem, 1, 0);

	//make pipe initialization only from Sender side
	if (candrv_sndr_rcvr == CAN_LL_SENDER) {
		if (pipe(can_sndr_pipe)) {
			fake_can_log("Pipe failed.\n");
			return -1;
		}
		if (pipe(can_rcvr_pipe)) {
			fake_can_log("Pipe failed.\n");
			return -1;
		}
	}

	if (candrv_sndr_rcvr == CAN_LL_RECEIVER) {
		rx_stream = fdopen(can_sndr_pipe[0], "rb");
		tx_stream = fdopen(can_rcvr_pipe[1], "wb");
	} else {
		rx_stream = fdopen(can_rcvr_pipe[0], "rb");
		tx_stream = fdopen(can_sndr_pipe[1], "wb");
	}

	//tx_timer_us
	cbtimer_set_cb(&tx_timer_us, fake_can_tx_toutus_cb, NULL);
	cbtimer_set_name(&tx_timer_us, "CANLL TX Tout uS");

	return 0;
}
