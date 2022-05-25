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
#include <stdatomic.h>
#include <semaphore.h>	//sem_open(), sem_destroy(), sem_wait()..
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "fake_can_linux.h"
#include "cbtimer_lin.h"

enum candrv_rx_stat_e {
	CANDRV_RX_STATUS_WAITING = 0,
	CANDRV_RX_STATUS_RECEIVED
};

typedef union {
	struct __attribute__((packed)) {
		uint32_t id;
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

static sem_t sem;

fake_can_phy_t fake_can_phy;
cbtimer_t candrv_txnb_timer;
cbtimer_t candrv_tx_timer;
static long candrv_tx_delay_us;
static int can_sndr_pipe[2];
//static int can_rcvr_pipe[2];
static fake_can_phy_t can_frame;
static char *candrv_name;

int fake_can_rx_task(void *params)
{
	FILE *rx_stream;
	rx_stream = fdopen(can_sndr_pipe[0], "rb");

	printf("%s fake_can_rx_task PID=%d Ready\n", candrv_name, getpid());
	fake_can_phy_t can_frame;
	ssize_t rlen = fread(can_frame.u8, 1, sizeof(fake_can_phy_t), rx_stream);
	if (rlen == sizeof(fake_can_phy_t)) {
		printf("%s: Received %ld bytes ", candrv_name, (long)rlen);
		printf("id=0x%08x idt=%d dlc=%d: 0x%02x 0x%02x 0x%02x 0x%02x "
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
			can_frame.id, can_frame.idt, can_frame.dlc,
			can_frame.data[0], can_frame.data[1], can_frame.data[2], can_frame.data[3],
			can_frame.data[4], can_frame.data[5], can_frame.data[6], can_frame.data[7]);
		fflush(0);
		//Send to to the transport/network layer Receiver L_Data.ind
		fake_canrx_cb(can_frame.id, can_frame.idt, can_frame.dlc, can_frame.data, params);
	} else {
		printf("RX rdlen=%ld\n", rlen);
	}
	fclose(rx_stream);
}

static ssize_t candrv_send(void)
{
	FILE *tx_stream, *rx_stream;
	tx_stream = fdopen(can_sndr_pipe[1], "wb");

	ssize_t wlen = fwrite(can_frame.u8, 1, sizeof(fake_can_phy_t), tx_stream);
	fclose(tx_stream);
	return wlen;
}

static void candrv_txnb_timer_cb(cbtimer_t *t)
{
	printf("%s: SendingNB %ld bytes\n", candrv_name, sizeof(fake_can_phy_t));
	ssize_t wlen = candrv_send();

	//the current transmit is on non blocking mode
	//so we need to call the callback function to inform
	//the caller (sender) for completion of the transmission
	printf("%s: TX Done. Calling CB\n",  candrv_name); fflush(0);
	fake_cantx_confirm_cb(t->cb_params);
}

static void candrv_tx_timer_cb(cbtimer_t *t)
{
	printf("%s: Sending %ld bytes\n", candrv_name, sizeof(fake_can_phy_t));
	ssize_t wlen = candrv_send();
	//in blocking mode we just informing the fake_can_tx()
	//function that the transmission has being completed
	sem_post(&sem);
}

int fake_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	printf("%sNB: ", candrv_name);
	cbtimer_start(&candrv_txnb_timer, candrv_tx_delay_us);
	// follows at candrv_txnb_timer_cb(cbtimer_t *t)
}

int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	printf("%s: ", candrv_name);
	cbtimer_start(&candrv_tx_timer, candrv_tx_delay_us);
	// follows at candrv_tx_timer_cb(cbtimer_t *t)
}

int fake_can_wait_txdone(long tout_us)
{
	printf("%s: waiting for the end of transmission\n", candrv_name); fflush(0);
	//TODO: Implement the tout timer
	sem_wait(&sem);
	printf("%s: TX Done\n", candrv_name); fflush(0);
	return 0;
}

int fake_can_init(long tx_delay_us, char *name, void *params)
{
	candrv_tx_delay_us = tx_delay_us;

	candrv_name = name;

	//make pipe initialization only from Sender side
	if (pipe(can_sndr_pipe)) {
		fprintf(stderr, "Pipe failed.\n");
		return -1;
	}
	//setting up a fake timer that simulates the latency of
	//the transmission of the CAN driver (data link layer)
	if (cbtimer_set_cb(&candrv_txnb_timer, candrv_txnb_timer_cb, params) < 0) {
		return -1;
	}
	cbtimer_set_name(&candrv_txnb_timer, name);
	return 0;
}
//this function is supposed to be called from the Receiver side
//for testing purposes only from another (peer) process or thread
int fake_can_rcvr_init(long tx_delay_us, char *name, void *params)
{
	candrv_tx_delay_us = tx_delay_us;
	candrv_name = name;

	sem_init(&sem, 1, 0);

	//setting up a fake timer that simulates the latency of
	//the transmission of the CAN driver (data link layer)
	if (cbtimer_set_cb(&candrv_txnb_timer, candrv_txnb_timer_cb, params) < 0) {
		return -1;
	}
	cbtimer_set_name(&candrv_txnb_timer, name);

	if (cbtimer_set_cb(&candrv_tx_timer, candrv_tx_timer_cb, params) < 0) {
		return -1;
	}
	cbtimer_set_name(&candrv_tx_timer, name);
	return 0;
}
