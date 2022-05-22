/*
 * can-tp.c
 *
 *  Created on: May 15, 2022
 *      Author: refo
 *
 *      for timer see:
 *      https://man7.org/linux/man-pages/man2/timer_create.2.html
 *      https://stackoverflow.com/questions/64429205/how-to-use-sigev-thread-sigevent-for-linux-timers-expiration-handling-in-c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "can-tp.h"

void cantp_tx_timer_cb(cantp_context_t *ctx)
{
	switch (ctx->tx_state.state) {
	case CANTP_STATE_SF_SENDING: {
			//stopping N_As timer
			cantp_timer_stop(ctx->tx_state.timer);
			ctx->tx_state.state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_As);
		}
		break;
	case CANTP_STATE_FF_SENT: {
			//stopping N_As timer
			cantp_timer_stop(ctx->tx_state.timer);
			ctx->tx_state.state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_As);
		}
		break;
	case CANTP_STATE_FF_FC_WAIT: {
			//stopping N_Bs timer
			cantp_timer_stop(ctx->tx_state.timer);
			ctx->tx_state.state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
		}
		break;
	} //switch (ctx->tx_state.state)

}

void cantp_cantx_confirm_cb(cantp_context_t *ctx)
{
//	printf("cantp_cantx_success_cb:\n"); fflush(0);
	switch(ctx->tx_state.state) {
	case CANTP_STATE_SF_SENDING: {
			cantp_timer_stop(ctx->tx_state.timer);
			cantp_result_cb(CANTP_RESULT_N_OK);
		} break;
	case CANTP_STATE_FF_SENDING: {
			//We have received a confirmation from data link layer
			//that the FF has being acknowledged
			//ISO 15765-2:2016 Figure 11 step(Key) 2
			//so we are stopping N_As timer
			cantp_timer_stop(ctx->tx_state.timer);
			//starting new timer for receiving Flow Control frame
			cantp_timer_start(ctx->tx_state.timer, "CANTP_N_BS_TIMER_MS",
											1000 * CANTP_N_BS_TIMER_MS);
			ctx->tx_state.state = CANTP_STATE_FF_SENT;
			//now we are waiting for Flow Control frame
		} break;
	case CANTP_STATE_FF_SENT: {
			//stopping N-Bs timer
			cantp_timer_stop(ctx->tx_state.timer);

		} break;
	case CANTP_STATE_FC_RCVD:
		//nothing to do here
		break;
	}
}

void cantp_canrx_cb(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					uint8_t *data,
					cantp_context_t *ctx)
{
	//TODO: We need to make some ID checks here
	printf("\033[0;33mCAN-TP Receiver Received ");

	//TODO: check validity of dlc
	cantp_frame_t cantp_rx_frame;
	for (uint8_t i=0; i < dlc; i++) {
		cantp_rx_frame.u8[i] = data[i];
	}
	print_cantp_frame(cantp_rx_frame);
	switch (cantp_rx_frame.n_pci_t) {
	case CANTP_SINGLE_FRAME: {

		} break;
	case CANTP_FIRST_FRAME: {

		} break;
	}
	switch (ctx->tx_state.state) {
	case CANTP_STATE_FF_FC_WAIT: {

		}
		break;
	default:
		break;
	} //switch (ctx->tx_state.state)
}

int cantp_send	(cantp_context_t *ctx,
				uint32_t id,
				uint8_t idt,
				uint8_t *data,
				uint16_t len)
{
	int res;
	cantp_frame_t txframe;
	printf("CAN-TP Sender Sending ");
	if (len <= CANTP_SF_NUM_DATA_BYTES) {
		//sending Single Frame
		txframe.sf.len = len;
		txframe.n_pci_t = CANTP_SINGLE_FRAME;
		for (uint8_t i = 0; i < len; i++) {
			txframe.sf.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		printf("CAN-TP Sender ");
		if (cantp_timer_start(ctx->tx_state.timer, "CANTP_N_AS_TIMER_MS",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}
		cantp_can_tx(id, idt, len + 1, txframe.u8);
		ctx->tx_state.state = CANTP_STATE_SF_SENDING;
	} else {
		//we need to send segmented data
		//so first we are sending First Frame
		ctx->tx_state.id = id;
		ctx->tx_state.idt = idt;
		ctx->tx_state.len = len;
		ctx->tx_state.data = data;
		txframe.n_pci_t = CANTP_FIRST_FRAME;
		cantp_ff_len_set(&txframe, len);
		for (uint8_t i = 0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
			txframe.ff.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		printf("CAN-TP Sender ");
		if (cantp_timer_start(ctx->tx_state.timer, "CANTP_N_AS_TIMER_MS",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}
		cantp_can_tx(id, idt, 8, txframe.u8);
		ctx->tx_state.state = CANTP_STATE_FF_SENDING;
	}
//	ctx->start_tx_task(ctx);
	return res;
}


