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

static cantp_context_t cantp_ctx;
atomic_int cantp_result_status;

void fake_cantx_confirm_cb(void *params)
{
	cantp_cantx_confirm_cb((cantp_context_t *)params);
}

void fake_canrx_cb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	cantp_canrx_cb(id, idt, dlc, data, &cantp_ctx);
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

int main(int argc, char **argv)
{
	long candrv_tx_delay = 1000;
	if (argc > 1) {
		candrv_tx_delay = atoi(argv[1]);
		printf("candrv_tx_delay = %ldμs\n", candrv_tx_delay);
	}
	fake_can_init(candrv_tx_delay, &cantp_ctx);

	pid_t pid = fork();
	if (pid == (pid_t) 0) {
		//This is the child process.
//		close(can_tx_pipe[1]);//Close other end first.
		fake_can_rx_task();
		printf("END child\n"); fflush(0);
		return EXIT_SUCCESS;
	} else if (pid < (pid_t) 0) {
		fprintf(stderr, "Fork failed.\n");
		return EXIT_FAILURE;
	}
//	printf("pid = %d\n", pid);
//	close(can_tx_pipe[0]);	//Close other end first.

	atomic_store(&cantp_result_status, CANTP_RESULT_WAITING);

	uint16_t dlen = 7;
	uint8_t *data = malloc(dlen);
	for (uint16_t i = 0; i < dlen; i++) {
		data[i] = (uint8_t)(0xff & i);
	}

	cantp_init(&cantp_ctx);

	cantp_send(&cantp_ctx, 0xAAA, 0, data, dlen);

	while (atomic_load(&cantp_result_status) == CANTP_RESULT_WAITING) {
		usleep(1000);
	}
	kill(pid, SIGHUP);
	printf("EndMain\n");fflush(0);
	return 0;
}
