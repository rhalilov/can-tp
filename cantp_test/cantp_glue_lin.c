/*
 * cantp_glue_lin.c
 *
 *  Created on: May 21, 2022
 *      Author: refo
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include "cbtimer_lin.h"
#include "fake_can_linux.h"
#include "can-tp.h"

enum cantp_result_status_e {
	CANTP_RESULT_WAITING = 0,
	CANTP_RESULT_RECEIVED
};

static const char *cantp_frame_t_enum_str[] = {
		FOREACH_CANTP_N_PCI_TYPE(GENERATE_STRING)
};

static const char *cantp_result_enum_str[] = {
		FOREACH_CANTP_RESULT(GENERATE_STRING)
};

atomic_int cantp_result_status;

void print_cantp_frame(cantp_frame_t cantp_frame)
{
	printf("CAN-TP Frame Type: %s ", cantp_frame_t_enum_str[cantp_frame.frame_t]);
	for (uint8_t i=0; i < 8; i++) {
		printf("0x%02x ", cantp_frame.u8[i]);
	}
	printf("\n");fflush(0);
}

int cantp_timer_start(void *timer, char *name, long tout_us)
{
	cbtimer_t *t;
	t = (cbtimer_t *)timer;
	fflush(0);
	cbtimer_set_name(t, name);
	return cbtimer_start(t, tout_us);
}

static void cantp_tx_t_cb(cbtimer_t *tim)
{
	cantp_context_t *ctx = (cantp_context_t *)(tim->cb_params);
	cantp_tx_timer_cb(ctx);
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

int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	return fake_can_tx(id, idt, dlc, data);
}

void cantp_init(cantp_context_t *cantp_ctx)
{
	static cbtimer_t cantp_tx_timer;
	cantp_set_timer_ptr(&cantp_tx_timer, &cantp_ctx->tx_state);
	cbtimer_set_cb(&cantp_tx_timer, cantp_tx_t_cb, cantp_ctx);
	atomic_store(&cantp_result_status, CANTP_RESULT_WAITING);
}

void cantp_wait_for_result(void)
{
	while (atomic_load(&cantp_result_status) == CANTP_RESULT_WAITING) {
		usleep(1000);
	}
}
