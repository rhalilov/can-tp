/*
 * cantp_test.c
 *
 *  Created on: May 9, 2022
 *      Author: refo
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "can-tp.h"
#include "cbtimer_lin.h"
#include "fake_can_linux.h"
#include "cantp_glue_lin.h"

enum cantp_result_status_e {
	CANTP_RESULT_WAITING = 0,
	CANTP_RESULT_RECEIVED
};

static const char *cantp_result_enum_str[] = {
		FOREACH_CANTP_RESULT(GENERATE_STRING)
};

static cantp_rxtx_status_t cantp_sndr_ctx;
atomic_int cantp_result_status;

void fake_cantx_confirm_cb(void *params)
{
	cantp_cantx_confirm_cb((cantp_rxtx_status_t *)params);
}

void fake_canrx_cb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, void *params)
{
	cantp_canrx_cb(id, idt, dlc, data, params);
}

void cantp_result_cb(int result)
{
	printf("CAN-TP Sender: Received result: ");
	if (result == CANTP_RESULT_N_OK) {
		printf("\033[0;32m");
	} else {
		printf("\033[0;31m");
	}
	printf("%s\033[0m\n", cantp_result_enum_str[result]);
	atomic_store(&cantp_result_status, CANTP_RESULT_RECEIVED);
}

void cantp_received_cb(cantp_rxtx_status_t *ctx,
			uint32_t id, uint8_t idt, uint8_t *data, uint16_t len)
{
	printf("\033[0;33mCAN-SL Receiver Received "
			"from ID=0x%06x IDT=%d len=%d bytes:\033[0m ", id, idt, len);
	for (uint16_t i = 0; i < len; i++) {
		printf("0x%02x ", data[i]);
	}
	printf("\n"); fflush(0);
}

void cantp_rcvd_ff_cb(cantp_rxtx_status_t *ctx,
			uint32_t id, uint8_t idt, uint8_t *data, uint16_t len)
{
	printf("\033[0;33mCAN-SL Receiver Received First Frame "
			"from ID=0x%06x IDT=%d len=%d bytes:\033[0m \n", id, idt, len);
	fflush(0);

}

static void cantp_rx_t_cb(cbtimer_t *tim)
{
	cantp_rxtx_status_t *ctx = (cantp_rxtx_status_t *)(tim->cb_params);
	cantp_rx_timer_cb(ctx);
}

void receiver_task(uint8_t rx_bs, uint8_t rx_st)
{
	printf("\033[0;33mReceiver Task START\033[0m\n");
	static cantp_rxtx_status_t rcvr_state;
	static cbtimer_t cantp_rx_timer;

	cantp_rx_params_init(&rcvr_state, rx_bs, rx_st);
	cantp_set_timer_ptr(&cantp_rx_timer, &rcvr_state);
	cbtimer_set_cb(&cantp_rx_timer, cantp_rx_t_cb, &rcvr_state);

	fake_can_rx_task(&rcvr_state);
}

int main(int argc, char **argv)
{
	long candrv_tx_delay = 1000;
	if (argc > 1) {
		candrv_tx_delay = atoi(argv[1]);
		printf("candrv_tx_delay = %ldμs\n", candrv_tx_delay);
	}
	fake_can_init(candrv_tx_delay, &cantp_sndr_ctx);

	pid_t pid = fork();
	if (pid == (pid_t) 0) {
		//This is the child process.
		receiver_task(0, 0);
		printf("END child\n"); fflush(0);
		return EXIT_SUCCESS;
	} else if (pid < (pid_t) 0) {
		fprintf(stderr, "Fork failed.\n");
		return EXIT_FAILURE;
	}
//	printf("pid = %d\n", pid);

	atomic_store(&cantp_result_status, CANTP_RESULT_WAITING);

	uint16_t dlen = 16;
	uint8_t *data = malloc(dlen);
	for (uint16_t i = 0; i < dlen; i++) {
		data[i] = (uint8_t)(0xff & i) + 1;
	}

	static cbtimer_t cantp_tx_timer;
	cantp_set_timer_ptr(&cantp_tx_timer, &cantp_sndr_ctx);
	cbtimer_set_cb(&cantp_tx_timer, cantp_tx_t_cb, &cantp_sndr_ctx);

	cantp_send(&cantp_sndr_ctx, 0xAAA, 0, data, dlen);

	while (atomic_load(&cantp_result_status) == CANTP_RESULT_WAITING) {
		usleep(1000);
	}
	sleep(3);
	kill(pid, SIGHUP);
	printf("EndMain\n");fflush(0);
	return 0;
}
