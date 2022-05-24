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

void cantp_tx_timer_cb(cantp_rxtx_status_t *ctx)
{
	printf("CAN-TP Sender: ");
	//stopping N_As timer
	cantp_timer_stop(ctx->timer);
	switch (ctx->state) {
	case CANTP_STATE_SF_SENDING: {
//			//stopping N_As timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_As);
		}
		break;
	case CANTP_STATE_FF_SENT: {
			//stopping N_As timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_As);
		}
		break;
	case CANTP_STATE_FF_FC_WAIT: {
			//stopping N_Bs timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
		}
		break;
	} //switch (ctx->tx_state.state)

}

void cantp_rx_timer_cb(cantp_rxtx_status_t *ctx)
{
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer);
}

void cantp_cantx_confirm_cb(cantp_rxtx_status_t *ctx)
{
//	printf("cantp_cantx_success_cb:\n");
	switch(ctx->state) {
	case CANTP_STATE_SF_SENDING: {
			cantp_timer_stop(ctx->timer);
			cantp_result_cb(CANTP_RESULT_N_OK);
		} break;
	case CANTP_STATE_FF_SENDING: {
			//We have received a confirmation from data link layer
			//that the FF has being acknowledged
			//ISO 15765-2:2016 Figure 11 step(Key) 2
			//so we are stopping N_As timer
//			printf("CAN-TP Sender: "); fflush(0);
			cantp_timer_stop(ctx->timer); fflush(0);
			//starting new timer for receiving Flow Control frame
//			printf("CAN-TP Sender: "); fflush(0);
			cantp_timer_start(ctx->timer, "N_Bs",
									1000 * CANTP_N_BS_TIMER_MS); fflush(0);
			ctx->state = CANTP_STATE_FF_SENT;
			//now we are waiting for Flow Control frame
		} break;
	case CANTP_STATE_FF_SENT: {
			//stopping N-Bs timer
			cantp_timer_stop(ctx->timer);

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
					cantp_rxtx_status_t *ctx)
{
	//TODO: We need to make some ID checks here

	//TODO: check validity of dlc
	cantp_frame_t cantp_rx_frame;
	for (uint8_t i=0; i < dlc; i++) {
		cantp_rx_frame.u8[i] = data[i];
	}
	printf("\033[0;33mCAN-TP Receiver: "
//			"Received (ID=0x%06x IDT=%d DLC=%d) ",
//			id, idt, dlc
			);
	print_cantp_frame(cantp_rx_frame); fflush(0);
	switch (cantp_rx_frame.n_pci_t) {
	case CANTP_SINGLE_FRAME: {
			//the data receiving flow here isn't started any timers
			//and doesn't need to do anything else than present the
			//received data to the Session Layer (Receiver N_USData.ind)
			cantp_received_cb(ctx, id, idt,
					cantp_rx_frame.sf.d, cantp_rx_frame.sf.len);
		} break;
	case CANTP_FIRST_FRAME: {
			//Here we have more things to do.
			//Starting N_Br timer
			printf("\033[0;33mCAN-TP Receiver: \033[0m");
			if (cantp_timer_start(ctx->timer, "N_Br",
									1000 * CANTP_N_BR_TIMER_MS) < 0 ) {
				printf("\033[0;33mCAN-TP Receiver: Error setting timer\033[0m");
				fflush(0);
				return;
			}
			//store some connection parameters
			ctx->len = cantp_ff_len_get(&cantp_rx_frame);
			ctx->index = 0;
			ctx->bl_index = 0;
			cantp_rcvd_ff_cb(ctx, id, idt, cantp_rx_frame.ff.d,
													CANTP_FF_NUM_DATA_BYTES);
			ctx->index = CANTP_FF_NUM_DATA_BYTES;
			printf("\033[0;33mCAN-TP Receiver: \033[0m");
			cantp_timer_stop(ctx->timer);
			cantp_frame_t rcvr_tx_frame = { 0 };
			rcvr_tx_frame.n_pci_t = CANTP_FLOW_CONTROLL;
			rcvr_tx_frame.fc.bs = ctx->bs;
			rcvr_tx_frame.fc.st = ctx->st;
			rcvr_tx_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;
			ctx->state = CANTP_STATE_FC_SENDING;
			printf("\033[0;33mCAN-TP Receiver Sending: \033[0m");
			print_cantp_frame(rcvr_tx_frame);

//			if (cantp_can_tx(ctx->id, ctx->idt, 8, rcvr_tx_frame.u8,
//											1000 * CANTP_N_AR_TIMER_MS) < 0) {
			if (cantp_can_tx(ctx->id, ctx->idt, 8, rcvr_tx_frame.u8) < 0) {
				//TODO: Timeout!!!
				return;
			}
			if (cantp_can_wait_txdone(1000 * CANTP_N_AR_TIMER_MS) < 0) {
				//TODO: Timeout!!!
				return;
			}
			printf("-----------done----------\n");fflush(0);
			printf("\033[0;33mCAN-TP Receiver: \033[0m");
			if (cantp_timer_start(ctx->timer, "N_Cr",
									1000 * CANTP_N_CR_TIMER_MS) < 0 ) {
				printf("\033[0;33mCAN-TP Receiver: Error setting timer\033[0m");
				fflush(0);
				return;
			}

		} break;
	}
}

int cantp_send	(cantp_rxtx_status_t *ctx,
				uint32_t id,
				uint8_t idt,
				uint8_t *data,
				uint16_t len)
{
	int res;
	cantp_frame_t txframe;
	printf("CAN-TP Sender: Sending ");
	if (len <= CANTP_SF_NUM_DATA_BYTES) {
		//sending Single Frame
		txframe.sf.len = len;
		txframe.n_pci_t = CANTP_SINGLE_FRAME;
		for (uint8_t i = 0; i < len; i++) {
			txframe.sf.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		printf("CAN-TP Sender ");
		if (cantp_timer_start(ctx->timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}; fflush(0);
		cantp_can_tx_nb(id, idt, len + 1, txframe.u8);
		ctx->state = CANTP_STATE_SF_SENDING;
	} else {
		//we need to send segmented data
		//so first we are sending First Frame
		ctx->id = id;
		ctx->idt = idt;
		ctx->len = len;
		ctx->data = data;
		txframe.n_pci_t = CANTP_FIRST_FRAME;
		cantp_ff_len_set(&txframe, len);
		for (uint8_t i = 0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
			txframe.ff.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		printf("CAN-TP Sender: ");
		if (cantp_timer_start(ctx->timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}; fflush(0);
		cantp_can_tx_nb(id, idt, 8, txframe.u8);
//		cantp_can_tx(id, idt, 8, txframe.u8, 1000000);
		ctx->state = CANTP_STATE_FF_SENDING;
	}
//	ctx->start_tx_task(ctx);
	return res;
}


