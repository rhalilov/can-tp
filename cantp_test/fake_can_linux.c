/*
 * fake_can_linux.c
 *
 *  Created on: May 21, 2022
 *      Author: refo
 *
 * https://stackoverflow.com/questions/48241561/blocking-pipe-c-linux
 * https://stackoverflow.com/questions/16400820/how-to-use-posix-semaphores-on-forked-processes-in-c
 *
 */

#include <stdio.h>			//printf()
#include <stdlib.h>			//exit(), malloc(), free()
#include <stdatomic.h>
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

fake_can_phy_t fake_can_phy;
cbtimer_t candrv_tx_timer;
atomic_int candrv_tx_status;
long candrv_tx_delay_us;
int can_tx_pipe[2];
int can_rx_pipe[2];
static fake_can_phy_t can_frame;

int fake_can_rx_task(void *params)
{
	FILE *rx_stream, *tx_stream;
	rx_stream = fdopen(can_tx_pipe[0], "rb");
	tx_stream = fdopen(can_tx_pipe[1], "wb");

	fake_can_phy_t can_frame;
	ssize_t rlen = fread(can_frame.u8, 1, sizeof(fake_can_phy_t), rx_stream);
	if (rlen == sizeof(fake_can_phy_t)) {
		//Sending back a confirmation
		uint8_t tx_confirm = rlen;
		ssize_t wlen = fwrite(&tx_confirm, 1, 1, tx_stream);

		printf("\033[0;33mCAN-LL Receiver: Received %ld bytes\033[0m ", (long)rlen);
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
	fclose(tx_stream);
}

static ssize_t candrv_send(uint8_t *rx_confirm)
{
	FILE *tx_stream, *rx_stream;
	tx_stream = fdopen(can_tx_pipe[1], "wb");
	rx_stream = fdopen(can_tx_pipe[0], "rb");

	ssize_t wlen = fwrite(can_frame.u8, 1, sizeof(fake_can_phy_t), tx_stream);
	fclose(tx_stream);
	printf("CAN-LL Sender: Waiting to confirm... \n"); fflush(0);//do not touch this line
	usleep(1000);
	ssize_t rlen = fread(rx_confirm, 1, 1, rx_stream);
	fclose(rx_stream);
	return wlen;
}

static void candrv_tx_timer_cb(cbtimer_t *t)
{
	uint8_t rx_confirm = 0;
	printf("CAN-LL SenderNB: Sending %ld bytes\n", sizeof(fake_can_phy_t));
	ssize_t wlen = candrv_send(&rx_confirm);
	if (rx_confirm == wlen) {
		//Sender L_Data.con
		printf("CAN-LL Sender: Confirmed transmission of %ld bytes\n", (long)wlen); fflush(0);
		//Sender L_Data.con: data link layer issues to the transport/network
		//layer the reception of the CAN frame
		if (atomic_load(&candrv_tx_status) == CANDRV_TX_IDLE) {
			//the current transmit is on non blocking mode
			//so we need to call che callback function to inform
			//the caller (sender) for completition of the transmission
			printf("CAN-LL TX Done. Calling CB\n"); fflush(0);
			fake_cantx_confirm_cb(t->cb_params);
		} else {
			//in blocking mode we just informing the fake_can_tx()
			//function that the transmission has being completed
			printf("CAN-LL TX Done\n"); fflush(0);
			atomic_store(&candrv_tx_status, CANDRV_TX_DONE);
		}
	} else {
		printf("wlen = %ld confirm = %d\n", wlen, rx_confirm);
	}

}

int fake_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	printf("CAN-LL SenderNB: ");
	cbtimer_start(&candrv_tx_timer, candrv_tx_delay_us);
	// follows at candrv_tx_timer_cb(cbtimer_t *t)
}

int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data,
														uint32_t tout_us)
{
	int res = 0;
	//For testing purposes we do not expect to have parallel transmission
	//so we are skipping the checks if the driver already is sending anything
//	atomic_store(&candrv_tx_status, CANDRV_TRANSMITTING);
//	fake_can_tx_nb(id, idt, dlc, data);
//	uint32_t t = 0;
//	while (atomic_load(&candrv_tx_status) == CANDRV_TRANSMITTING) {
//		usleep(1000);
//		t += 1000;
//		if (t > tout_us) {
//			res = -1;
//			break;
//		}
//	}
//	atomic_store(&candrv_tx_status, CANDRV_TX_IDLE);
//	return res;
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	printf("CAN-LL Sender: Sending %ld bytes\n", sizeof(fake_can_phy_t));
	uint8_t rx_confirm = 0;
	ssize_t wlen = candrv_send(&rx_confirm);
	if (rx_confirm == wlen) {
		//Sender L_Data.con
		printf("CAN-LL Sender: Confirmed transmission of %ld bytes\n", (long)wlen); fflush(0);
		//Sender L_Data.con: data link layer issues to the transport/network
		//layer the reception of the CAN frame
	} else {
		printf("wlen = %ld confirm = %d\n", wlen, rx_confirm);
		res = -1;
	}
	return res;
}

int fake_can_init(long tx_delay_us, void *params)
{
	candrv_tx_delay_us = tx_delay_us;

	atomic_store(&candrv_tx_status, CANDRV_TX_IDLE);

	if (pipe(can_tx_pipe)) {
		fprintf(stderr, "Pipe failed.\n");
		return -1;
	}
	//setting up a fake timer that simulates the latency of
	//the transmission of the CAN driver (data link layer)
	if (cbtimer_set_cb(&candrv_tx_timer, candrv_tx_timer_cb, params) < 0) {
		return -1;
	}
	cbtimer_set_name(&candrv_tx_timer, "CAN-LL");
	return 0;
}
