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
	printf("CAN-TP cantp_tx_timer_cb: ");
	//stopping N_As timer
	cantp_timer_stop(ctx->timer);
	switch (ctx->state) {
	case CANTP_STATE_SF_SENDING: {
//			//stopping N_As timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
		}
		break;
	case CANTP_STATE_FF_SENT: {
			//stopping N_As timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDOL;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
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

void cantp_cantx_confirm_cb(cantp_rxtx_status_t *ctx)
{
	//The Sender transmits only Single Frame, First Frame and Consecutive Frames
	//so only this three types of frames can receive a transmit confirmation
	printf("-----------cantp_cantx_confirm_cb------------\n"); fflush(0);
	switch(ctx->state) {
	case CANTP_STATE_SF_SENDING: {
			cantp_timer_stop(ctx->timer);
			cantp_result_cb(CANTP_RESULT_N_OK);
		} break;
	case CANTP_STATE_FF_SENDING: {
			//First Frame is sent only from the Sender peer
			//We received a confirmation from data link layer
			//that the FF has being acknowledged (sent)
			//ISO 15765-2:2016 Figure 11 step(Key) 2
			ctx->state = CANTP_STATE_FF_SENT;
			printf("\033[0;35mCAN-TP Sender: Received TX confirmation of FF\033[0m\n");
			fflush(0);
			//so we are stopping N_As timer
			printf("\033[0;35mCAN-TP Sender: \033[0m");
			cantp_timer_stop(ctx->timer); fflush(0);
			//starting new timer (N_Bs) for receiving Flow Control frame
			printf("\033[0;35mCAN-TP Sender: \033[0m");
			cantp_timer_start(ctx->timer, "N_Bs", 1000 * CANTP_N_BS_TIMER_MS);
			fflush(0);
			//Setting the sequence number so that the next transmission
			//of the Consecutive Frame start with 1
			ctx->sn = 1;
			ctx->index += CANTP_FF_NUM_DATA_BYTES;
			//Now Sender should wait for Flow Control frame
		} break;
	case CANTP_STATE_CF_SENDING: {
			//Sender has received a confirmation for transmission of a CF
			printf("\033[0;35mCAN-TP Sender: "
								"Received TX confirmation of CF\033[0m\n");

			//Checking if it is Last Consecutive Frame
			int remaining = ctx->len - ctx->index;
			if (remaining < 0) {
				printf("\033[0;35mCAN-TP Sender: \033[0m "
						"Error: Wrong number of remaining bytes %d", remaining);
				//TODO: Error handling
				return;
			} else if (remaining == 0) {
				//the last Consecutive frame (for which we just received the
				//transmission confirmation) were the Last Consecutive Frame
				ctx->state = CANTP_STATE_TX_DONE;
				cantp_sndr_tx_done_cb();
				break;
			}

			ctx->sn ++;
			if (ctx->sn > 0x0f) {
				ctx->sn = 0;
			}

			//stopping N_As timer
			printf("\033[0;35mCAN-TP Sender: \033[0m");
			cantp_timer_stop(ctx->timer); fflush(0);

			//check if Block Size (BS) parameter form the Receiver is > 0
			//that could mean the Receiver will expect BS counts of
			//Consecutive Frames and then (the Receiver) will have to send
			//a Flow Control frame
			if (ctx->bs > 0) {
				ctx->bl_index++;
				if (ctx->bl_index > ctx->bs) {
					ctx->bl_index = 0;
					//We should wait for Flow Control Frame from the Receiver
					printf("\033[0;35mCAN-TP Sender: "
							"Waiting for Control Frame from Receiver\033[0m\n");
					printf("\033[0;35mCAN-TP Sender: \033[0m");
					cantp_timer_start(ctx->timer, "N_Bs",
										1000 * CANTP_N_BS_TIMER_MS); fflush(0);
					ctx->state = CANTP_STATE_FF_SENT;
					break;
				}
			}
			ctx->state = CANTP_STATE_FF_SENT;

			printf("\033[0;35mCAN-TP Sender: "
									"Sending next Consecutive Frame\033[0m\n");
			//starting timer N_Cs
			printf("\033[0;35mCAN-TP Sender: \033[0m");
			cantp_timer_start(ctx->timer, "N_Cs",
									1000 * CANTP_N_CS_TIMER_MS); fflush(0);

			uint8_t n_to_send; //Number of bytes to send with this CAN frame
			if (remaining > CANTP_CF_NUM_DATA_BYTES) {
				n_to_send = CANTP_CF_NUM_DATA_BYTES;
			} else {
				n_to_send = remaining;
			}

			//Now should prepare to send the next Consecutive Frame
			cantp_frame_t tx_frame = { 0 };
			tx_frame.n_pci_t = CANTP_CONSEC_FRAME;
			tx_frame.cf.sn = ctx->sn;

			//copy the data to CAN tx_frame
			for (uint8_t i=0; i < n_to_send; i++) {
				tx_frame.cf.d[i] = ctx->data[ctx->index++];
			}
			cantp_can_tx_nb(ctx->id, ctx->idt, 8, tx_frame.u8);

			//Checking the number of remaining bytes to send with
			//the next Consecutive frame
//			int rem_after_send = ctx->len - ctx->index;
//			if (rem_after_send > 0) {
//
//			}
		} break;
	}
}

void cantp_rx_timer_cb(cantp_rxtx_status_t *ctx)
{
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer);
	switch (ctx->state) {
	case CANTP_STATE_FF_RCVD: {
			//cantp_rcvd_ff_cb() took too long to return
			printf("\033[0;33mCAN-TP Receiver: \033[0m");
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
		} break;
	}
}

static inline void cantp_send_cf(cantp_frame_t *rx_frame, cantp_rxtx_status_t *ctx)
{
	//The Consecutive frame is send only from the Sender side and
	//only after a Flow Control frame or other Consecutive Frame

	//Checking if it will be the Last Consecutive Frame
	int remaining = ctx->len - ctx->index;
	if (remaining <= 0) {
		printf("\033[0;35mCAN-TP Sender: \033[0m "
				"Error: Wrong number of remaining bytes %d", remaining);
		//TODO: Error handling
		return;
	}

	uint8_t n_to_send; //Number of bytes to send with this CAN frame
	if (remaining > CANTP_CF_NUM_DATA_BYTES) {
		n_to_send = CANTP_CF_NUM_DATA_BYTES;
	} else {
		n_to_send = remaining;
	}

	//Now should prepare to send the next Consecutive Frame
	cantp_frame_t tx_frame = { 0 };
	tx_frame.n_pci_t = CANTP_CONSEC_FRAME;
	tx_frame.cf.sn = ctx->sn;

	//copy the data to CAN tx_frame
	for (uint8_t i=0; i < n_to_send; i++) {
		tx_frame.cf.d[i] = ctx->data[ctx->index++];
	}

	ctx->state = CANTP_STATE_CF_SENDING;
	printf("\033[0;35mCAN-TP Sender: Sending\033[0m ");
	print_cantp_frame(tx_frame); fflush(0);
	cantp_can_tx_nb(ctx->id, ctx->idt, 8, tx_frame.u8);
	//wait for transmission to be confirmed cantp_cantx_confirm_cb()


//	cantp_frame_t tx_frame = { 0 };
//	switch (rx_frame->n_pci_t) {
//	case CANTP_FLOW_CONTROLL: {
//			tx_frame.n_pci_t  = CANTP_CONSEC_FRAME;
//			tx_frame.cf.sn = ctx->sn;
//			for (uint16_t i=0; i < CANTP_CF_NUM_DATA_BYTES; i++) {
//				tx_frame.cf.d[i] = ctx->data[ctx->index++];
//			}
//			ctx->state = CANTP_STATE_CF_SENDING;
//			printf("\033[0;35mCAN-TP Sender: Sending\033[0m ");
//			print_cantp_frame(tx_frame); fflush(0);
//			cantp_can_tx_nb(ctx->id, ctx->idt, 8, tx_frame.u8);
//			//wait for transmission to be confirmed cantp_cantx_confirm_cb()
//		} break;
//	case CANTP_CONSEC_FRAME: {
//
//	} break;
//	default:
//		printf("\033[0;31mERROR:\033[0m: "
//			"Consecutive Frame can not be send after %d\n", rx_frame->n_pci_t);
//	}
}

static inline void cantp_rx_first_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };
	printf("\033[0;33mCAN-TP Receiver: Received \033[0m");
	print_cantp_frame(*cantp_rx_frame); fflush(0);
	ctx->state = CANTP_STATE_FF_RCVD;
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	//Starting N_Br timer
	cantp_timer_start(ctx->timer, "N_Br", 1000 * CANTP_N_BR_TIMER_MS);
	//update some connection parameters into Receiver state
	//Actually the FF carries only CAN-TP message length information
	ctx->peer_id = id;//TODO: I still don't know what to do with this
				//so for now I am just passing it to the session layer
	ctx->len = cantp_ff_len_get(cantp_rx_frame);
	ctx->index = 0;
	ctx->bl_index = 0;
	//Calling cantp_rcvd_ff_cb()
	cantp_rcvr_rx_ff_cb(id, idt, &ctx->data, ctx->len);
	if (ctx->data == NULL) {
		printf("\033[0;33mCAN-TP Receiver: "
				"Error: No buffer allocated from Session Layer\033[0m");
		return;
	}
	for (uint8_t i=0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
		printf(" 0x%02x", cantp_rx_frame->ff.d[i]);
	}
	printf("\n"); fflush(0);
	ctx->index = CANTP_FF_NUM_DATA_BYTES;

	//Stopping timer N_Br. This is the time of processing cantp_rcvd_ff_cb()
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer);
	//Starting timer A_Cr
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_start(ctx->timer, "N_Ar", 1000 * CANTP_N_AR_TIMER_MS);
	rcvr_tx_frame.n_pci_t = CANTP_FLOW_CONTROLL;
	rcvr_tx_frame.fc.bs = ctx->bs;
	rcvr_tx_frame.fc.st = ctx->st;
	rcvr_tx_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;
	ctx->state = CANTP_STATE_FC_SENDING;
	printf("\033[0;33mCAN-TP Receiver: Sending \033[0m");
	print_cantp_frame(rcvr_tx_frame);
	//Sending FC frame
	cantp_can_tx(ctx->id, ctx->idt, 8, rcvr_tx_frame.u8);
	if (cantp_can_wait_txdone(1000 * CANTP_N_AR_TIMER_MS) < 0) {
		//Abort message reception and issue N_USData.indication
		//with <N_Result> = N_TIMEOUT_A
		cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
	}
	ctx->state = CANTP_STATE_FC_SENT;
	//Stopping timer N_Ar
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer); fflush(0);
	//Starting N_Cr
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_start(ctx->timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);
	//Now the Receiver should wait for first Consecutive Frame

}

static inline void cantp_rx_flow_control_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };
	printf("\033[0;35mCAN-TP Sender: Received \033[0m");
	print_cantp_frame(*cantp_rx_frame); fflush(0);
	//Stopping N_Bs timer
	printf("\033[0;35mCAN-TP Sender:\033[0m ");
	cantp_timer_stop(ctx->timer); fflush(0);
	//starting timer N_Cs
	printf("\033[0;35mCAN-TP Sender:\033[0m ");
	cantp_timer_start(ctx->timer, "N_Cs", 1000 * CANTP_N_CS_TIMER_MS);
	//Saving connection parameters sent from the Receiver
	ctx->peer_id = id;	//Saving the peer (Receiver) ID. The IDT should
						//be same type as ours (Sender)
	ctx->bs = cantp_rx_frame->fc.bs;
	ctx->st = cantp_rx_frame->fc.st;
	if (ctx->st < 0x80) {
		ctx->st_timer_us = 1000 * ctx->st;
	} else if ((ctx->st >= 0xf1) && (ctx->st <= 0xf9)) {
		ctx->st_timer_us = 100 * (ctx->st - 0xf0);
	} else {
		printf("\033[0;31mERROR:\033[0m: incompatible value of STmin\n");
		//TODO: Error check !!!
		return;
	}
	switch (cantp_rx_frame->fc.fs) {
	case CANTP_FC_FLOW_STATUS_CTS: {
			//Continue to send
			if (ctx->st == 0) {
				cantp_send_cf(cantp_rx_frame, ctx);
			} else {
				//TODO: We need to implement here STmin delay if STmin > 0
			}

		} break;
	case CANTP_FC_FLOW_STATUS_WAIT: {

		} break;
	case CANTP_FC_FLOW_STATUS_OVF: {

		} break;
	}

}

static inline void cantp_rx_consecutive_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };
	printf("\033[0;33mCAN-TP Receiver: Received \033[0m");
	print_cantp_frame(*cantp_rx_frame); fflush(0);
	//Stopping N_Cr timer
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer);
//TODO: We need to check here if we expect Flow Control Frame
// Same should be done on the Sender part too
//Now we are assuming that we expect next Consecutive Frame
	//Starting N_Cr timer again
	printf("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_start(ctx->timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS); fflush(0);

}

void cantp_canrx_cb(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					uint8_t *data,
					cantp_rxtx_status_t *ctx)
{
	//TODO: We need to make some ID checks here

	//TODO: check validity of dlc
	cantp_frame_t cantp_rx_frame, rcvr_tx_frame = { 0 };
	for (uint8_t i=0; i < dlc; i++) {
		cantp_rx_frame.u8[i] = data[i];
	}
	switch (cantp_rx_frame.n_pci_t) {
	case CANTP_SINGLE_FRAME: {
			//the data receiving flow here isn't started any timers
			//and doesn't need to do anything else than present the
			//received data to the Session Layer (Receiver N_USData.ind)
			cantp_received_cb(ctx, id, idt,
					cantp_rx_frame.sf.d, cantp_rx_frame.sf.len);
		} break;
	case CANTP_FIRST_FRAME: {
			//First Frame can be received only from the Receiver side
			cantp_rx_first_frame(id, idt, dlc, &cantp_rx_frame, ctx);
		} break;
	case CANTP_FLOW_CONTROLL: {
			//Flow Control frame can be received only from the Sender side
			cantp_rx_flow_control_frame(id, idt, dlc, &cantp_rx_frame, ctx);
		} break;
	case CANTP_CONSEC_FRAME: {
			//Consecutive Frame can be received only from the Receiver
			cantp_rx_consecutive_frame(id, idt, dlc, &cantp_rx_frame, ctx);
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
	printf("\033[0;35mCAN-TP Sender: \033[0m Sending ");
	if (len <= CANTP_SF_NUM_DATA_BYTES) {
		//sending Single Frame
		txframe.sf.len = len;
		txframe.n_pci_t = CANTP_SINGLE_FRAME;
		for (uint8_t i = 0; i < len; i++) {
			txframe.sf.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		printf("\033[0;35mCAN-TP Sender: \033[0m");
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
		printf("\033[0;35mCAN-TP Sender: \033[0m");
		if (cantp_timer_start(ctx->timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}; fflush(0);
		cantp_can_tx_nb(id, idt, 8, txframe.u8);
		ctx->state = CANTP_STATE_FF_SENDING;
	}
	return res;
}


