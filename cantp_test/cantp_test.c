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
#include <unistd.h>
#include <stdarg.h>
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

int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	return fake_can_tx(id, idt, dlc, data);
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

void fake_cantx_confirm_cb(void *params)
{
	cantp_cantx_confirm_cb((cantp_context_t *)params);
}

int main(int argc, char **argv)
{
	static cantp_context_t cantp_ctx;

	fake_can_init(&cantp_ctx);

	pid_t pid = fork();
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

	static uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

	cbtimer_t cantp_tx_timer;
	cantp_set_timer_ptr(&cantp_tx_timer, &cantp_ctx.tx_state);
	cbtimer_set_cb(&cantp_tx_timer, cantp_tx_t_cb, &cantp_ctx);

	cantp_result_status_init();

	cantp_send(&cantp_ctx, 0xAAA, 0, data, 7);

	cantp_wait_for_result();
//	sleep(3);
	printf("EndMain\n");fflush(0);
	return 0;
}
