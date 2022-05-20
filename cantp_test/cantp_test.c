/*
 * cantp_test.c
 *
 *  Created on: May 9, 2022
 *      Author: refo
 *
 *      https://stackoverflow.com/questions/48241561/blocking-pipe-c-linux
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "can-tp.h"
#include "cbtimer_lin.h"

typedef union {
	struct __attribute__((packed)) {
		uint32_t id;
		uint8_t idt;
		uint8_t dlc;
		uint8_t data[8];
	};
	uint8_t u8[14];
} fake_can_phy_t;

enum cantp_result_status_e {
	CANTP_RESULT_WAITING = 0,
	CANTP_RESULT_RECEIVED
};

enum candrv_rx_stat_e {
	CANDRV_RX_STATUS_WAITING = 0,
	CANDRV_RX_STATUS_RECEIVED
};

static const char *cantp_frame_t_enum_str[] = {
		FOREACH_CANTP_N_PCI_TYPE(GENERATE_STRING)
};

static const char *cantp_result_enum_str[] = {
		FOREACH_CANTP_RESULT(GENERATE_STRING)
};

fake_can_phy_t fake_can_phy;
cbtimer_t candrv_tx_timer;
long candrv_tx_delay = 1000;
atomic_int cantp_result_status;
int can_tx_pipe[2];
int can_rx_pipe[2];
static fake_can_phy_t can_frame;

void print_cantp_frame(cantp_frame_t cantp_frame)
{
	printf("CAN-TP Frame Type: %s ", cantp_frame_t_enum_str[cantp_frame.frame_t]);
	for (uint8_t i=0; i < 8; i++) {
		printf("0x%02x ", cantp_frame.u8[i]);
	}
	printf("\n");
}

int candrv_rx_task(cantp_context_t *ctx)
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
	printf("Waiting to cinfirm tx\n"); fflush(0);
	uint8_t rx_confirm;
	ssize_t rlen = fread(&rx_confirm, 1, 1, rx_stream);
	if (rx_confirm == 1) {
		printf("Confirmed transmission of %ld bytes\n", (long)wlen); fflush(0);
		cantp_cantx_confirm_cb((cantp_context_t *)t->cb_params);
	} else {
		printf("rlen = %ld\n", rlen);
	}
	fclose(rx_stream);
}

int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	can_frame.id = id;
	can_frame.idt = idt;
	can_frame.dlc = dlc;
	for (uint8_t i = 0; i < 8; i++) {
		can_frame.data[i] = data[i];
	}
	cbtimer_start(&candrv_tx_timer, candrv_tx_delay);
}

static void cantp_tx_t_cb(cbtimer_t *tim)
{
	cantp_context_t *ctx = (cantp_context_t *)(tim->cb_params);
	cantp_tx_timer_cb(ctx);
}

void cantp_timer_start(void *timer, const char *name, long tout_us)
{
	cbtimer_t *t;
	t = (cbtimer_t *)timer;
	fflush(0);
	cbtimer_set_name(t, name);
	cbtimer_start(t, tout_us);
}

void cantp_timer_stop(void *timer)
{
	cbtimer_t *t;
	t = (cbtimer_t *)timer;
	cbtimer_stop(t);
}

void cantp_result_cb(int result)
{
	printf("CAN-TP Result: %s\n", cantp_result_enum_str[result]);
	atomic_store(&cantp_result_status, CANTP_RESULT_RECEIVED);
}

int main(int argc, char **argv)
{
	static cantp_context_t cantp_ctx;
	pid_t pid;

	if (pipe(can_tx_pipe)) {
		fprintf(stderr, "Pipe failed.\n");
		return EXIT_FAILURE;
	}
	if (pipe(can_rx_pipe)) {
		fprintf(stderr, "Pipe failed.\n");
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid == (pid_t) 0) {
		//This is the child process.
//		close(can_tx_pipe[1]);//Close other end first.
		candrv_rx_task(&cantp_ctx);
		return EXIT_SUCCESS;
	} else if (pid < (pid_t) 0) {
		fprintf(stderr, "Fork failed.\n");
		return EXIT_FAILURE;
	}
//	close(can_tx_pipe[0]);	//Close other end first.

	//setting up a fake timer that simulates the latency of
	//the transmission of the CAN driver (data link layer)
	cbtimer_set_cb(&candrv_tx_timer, candrv_tx_timer_cb, &cantp_ctx);
	cbtimer_set_name(&candrv_tx_timer, "CANDRV");

	static uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

	cbtimer_t cantp_tx_timer;
	cantp_set_timer_ptr(&cantp_tx_timer, &cantp_ctx.tx_state);
	cbtimer_set_cb(&cantp_tx_timer, cantp_tx_t_cb, &cantp_ctx);

	atomic_store(&cantp_result_status, CANTP_RESULT_WAITING);

	candrv_tx_delay = atoi(argv[1]);
	cantp_send(&cantp_ctx, 0xAAA, 0, data, 7);

	while (atomic_load(&cantp_result_status) == CANTP_RESULT_WAITING) {
		usleep(1000);
	}
//	sleep(3);
	printf("EndMain\n");fflush(0);
	return 0;
}
