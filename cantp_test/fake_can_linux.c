/*
 * fake_can_linux.c
 *
 *  Created on: May 21, 2022
 *      Author: refo
 *
 * https://stackoverflow.com/questions/48241561/blocking-pipe-c-linux
 */

#include <stdio.h>
#include <stdlib.h>
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


fake_can_phy_t fake_can_phy;
cbtimer_t candrv_tx_timer;
long candrv_tx_delay = 1000;
int can_tx_pipe[2];
int can_rx_pipe[2];
static fake_can_phy_t can_frame;

int candrv_rx_task(void)
{
	FILE *rx_stream, *tx_stream;
	rx_stream = fdopen(can_tx_pipe[0], "rb");
	tx_stream = fdopen(can_tx_pipe[1], "wb");

	fake_can_phy_t can_frame;
	ssize_t rlen = fread(can_frame.u8, 1, sizeof(fake_can_phy_t), rx_stream);
	if (rlen == sizeof(fake_can_phy_t)) {
		uint8_t tx_confirm = 1;
		ssize_t wlen = fwrite(&tx_confirm, 1, 1, tx_stream);

		printf("Received %ld bytes", (long)rlen);
		printf(" RX id=0x%08x idt=%d dlc=%d: 0x%02x 0x%02x 0x%02x 0x%02x "
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
			can_frame.id, can_frame.idt, can_frame.dlc,
			can_frame.data[0], can_frame.data[1], can_frame.data[2], can_frame.data[3],
			can_frame.data[4], can_frame.data[5], can_frame.data[6], can_frame.data[7]);
		fflush(0);
//		cantp_rx_cb(can_frame.id, can_frame.idt, can_frame.dlc, can_frame.data, ctx);
	} else {
		printf("RX rdlen=%ld", rlen);
	}
	fclose(rx_stream);
	fclose(tx_stream);
}

static void candrv_tx_timer_cb(cbtimer_t *t)
{
	FILE *tx_stream, *rx_stream;
	tx_stream = fdopen(can_tx_pipe[1], "wb");
	rx_stream = fdopen(can_tx_pipe[0], "rb");

	printf("Sending %ld bytes\n", sizeof(fake_can_phy_t));
	ssize_t wlen = fwrite(can_frame.u8, 1, sizeof(fake_can_phy_t), tx_stream);
	fclose(tx_stream);
	printf("Waiting to confirm tx\n"); fflush(0);
	uint8_t rx_confirm;
	ssize_t rlen = fread(&rx_confirm, 1, 1, rx_stream);
	if (rx_confirm == 1) {
		printf("Confirmed transmission of %ld bytes\n", (long)wlen); fflush(0);
		fake_cantx_confirm_cb(t->cb_params);
	} else {
		printf("rlen = %ld confirm = %d\n", rlen, rx_confirm);
	}
	fclose(rx_stream);
}

int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	cbtimer_start(&candrv_tx_timer, candrv_tx_delay);
}

int fake_can_init(void *params)
{
	if (pipe(can_tx_pipe)) {
		fprintf(stderr, "Pipe failed.\n");
		return EXIT_FAILURE;
	}
	//setting up a fake timer that simulates the latency of
	//the transmission of the CAN driver (data link layer)
	cbtimer_set_cb(&candrv_tx_timer, candrv_tx_timer_cb, params);
	cbtimer_set_name(&candrv_tx_timer, "CANDRV");
}
