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
		candrv_rx_task();
		return EXIT_SUCCESS;
	} else if (pid < (pid_t) 0) {
		fprintf(stderr, "Fork failed.\n");
		return EXIT_FAILURE;
	}
//	close(can_tx_pipe[0]);	//Close other end first.

	static uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

	cantp_init(&cantp_ctx);
//	printf("cantp_init: OK\n");

	cantp_send(&cantp_ctx, 0xAAA, 0, data, 7);

	cantp_wait_for_result();
//	sleep(3);
	printf("EndMain\n");fflush(0);
	return 0;
}
