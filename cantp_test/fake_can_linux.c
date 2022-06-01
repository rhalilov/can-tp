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
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "fake_can_linux.h"

//#define fake_can_log	printf
#define fake_can_log

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

//static sem_t tx_done_sem;
static sem_t *tx_done_sem;

fake_can_phy_t fake_can_phy;
static long candrv_tx_delay_us;
static int can_sndr_pipe[2];
static int can_rcvr_pipe[2];
FILE *tx_stream, *rx_stream;
static fake_can_phy_t can_frame;
static char *candrv_name;
static void *cb_params;

int fake_can_rx_task(void *params)
{
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

		//Send back the acknowledge to the transmiter (either Sender or Receiver)
		uint8_t tx_ack = CANLL_FRAMEt_ACK;
		ssize_t wlen = fwrite(&tx_ack, 1, 1, tx_stream);

		//Send to to the transport/network layer Receiver L_Data.ind
		fake_canrx_cb(can_frame.id, can_frame.idt, can_frame.dlc, can_frame.data, params);
	} else {
		fake_can_log("+++++ RX rdlen=%ld\n", rlen); fflush(0);
		if (can_frame.ack = CANLL_FRAMEt_ACK) {
			usleep(10000);
		}
	}
}

static ssize_t candrv_send(void)
{
	ssize_t wlen = fwrite(can_frame.u8, 1, sizeof(fake_can_phy_t), tx_stream);
//	printf("==== %s ===== sent %ld\n", candrv_name, wlen); fflush(0);
//	usleep(1000);
	uint8_t confirm;
	ssize_t rlen = fread(&confirm, 1, 1, rx_stream);
//	printf("==== %s  ACK\n", candrv_name); fflush(0);
	return wlen;
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

	pid_t pid = fork();
	if (pid == (pid_t) 0) {
		//This is the child process.
		uint8_t confirm;
		ssize_t rlen = fread(&confirm, 1, 1, rx_stream);
		sem_post(tx_done_sem);
		return EXIT_SUCCESS;
	} else if (pid < (pid_t) 0) {
		fprintf(stderr, "Fork failed.\n");
		return EXIT_FAILURE;
	}
	printf("%s: Waiting...\n", candrv_name); fflush(0);
	usleep(1000);
	sem_wait(tx_done_sem);
	fake_cantx_confirm_cb(cb_params);
}

int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
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
	ssize_t wlen = candrv_send();
	sem_post(tx_done_sem);
}

int fake_can_wait_txdone(long tout_us)
{
	fake_can_log("%s: waiting for the end of transmission\n", candrv_name); fflush(0);
	//TODO: Implement the tout timer
	sem_wait(tx_done_sem);
	fake_can_log("%s: TX Done\n", candrv_name); fflush(0);
	return 0;
}

int fake_can_init(long tx_delay_us, char *name, void *params, uint8_t candrv_sndr_rcvr)
{
	candrv_tx_delay_us = tx_delay_us;
//	candrv_sndr_rcvr = sndr_rcvr;
	candrv_name = name;
	cb_params = params;

	printf("Initializing %s \n", can_ll_pear_str[candrv_sndr_rcvr]);

	tx_done_sem = (sem_t *)mmap(NULL, sizeof(sem_t),
			PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);

	sem_init(tx_done_sem, 1, 0);

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
	return 0;
}
