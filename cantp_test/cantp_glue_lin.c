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
	printf("\n");
}

void cantp_result_cb(int result)
{
	printf("CAN-TP Result: %s\n", cantp_result_enum_str[result]);
	atomic_store(&cantp_result_status, CANTP_RESULT_RECEIVED);
}

void cantp_result_status_init(void)
{
	atomic_store(&cantp_result_status, CANTP_RESULT_WAITING);
}

void cantp_wait_for_result(void)
{
	while (atomic_load(&cantp_result_status) == CANTP_RESULT_WAITING) {
		usleep(1000);
	}
}
