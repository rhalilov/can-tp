/*
 * can-tp.h
 *
 *  Created on: May 9, 2022
 *      Author: refo
 */

#ifndef _CAN_TP_H_
#define _CAN_TP_H_

//#include "cantp_config.h"

#define CANTP_LOG_NONE 0
#define CANTP_LOG_INFO 1
#define CANTP_LOG_DEBUG 2
#define CANTP_LOG_VERBOSE 3

#define CANTP_LOG CANTP_LOG_VERBOSE

#if CANTP_LOG >= CANTP_LOG_INFO
	#define cantp_logi printf
#else
	#define cantp_logi(__fmt, ...)
#endif

#if CANTP_LOG >= CANTP_LOG_DEBUG
	#define cantp_logd(__fmt, ...) printf(__fmt, ## __VA_ARGS__)
#else
	#define cantp_logd(__fmt, ...)
#endif

#if CANTP_LOG == CANTP_LOG_VERBOSE
	#define cantp_logv(__fmt, ...) printf(__fmt, ## __VA_ARGS__)
#else
	#define cantp_logv(__fmt, ...)
#endif

#ifndef GENERATE_ENUM
#define GENERATE_ENUM(ENUM) ENUM,
#endif
#ifndef GENERATE_STRING
#define GENERATE_STRING(STRING) #STRING,
#endif

// ISO 15765-2:2016 Table 8
// Definition of N_PCItype (network protocol control information type) bit values

#define FOREACH_CANTP_N_PCI_TYPE(CANTP_N_PCI_TYPE) \
	CANTP_N_PCI_TYPE(CANTP_SINGLE_FRAME) \
	CANTP_N_PCI_TYPE(CANTP_FIRST_FRAME) \
	CANTP_N_PCI_TYPE(CANTP_CONSEC_FRAME) \
	CANTP_N_PCI_TYPE(CANTP_FLOW_CONTROLL) \

enum CANTP_N_PCI_TYPE_ENUM {
	FOREACH_CANTP_N_PCI_TYPE(GENERATE_ENUM)
};

#define FOREACH_CANTP_RESULT(CANTP_RESULT) \
	CANTP_RESULT(CANTP_RESULT_N_OK) \
	CANTP_RESULT(CANTP_RESULT_N_TIMEOUT_As) \
	CANTP_RESULT(CANTP_RESULT_N_TIMEOUT_Bs) \
	CANTP_RESULT(CANTP_RESULT_N_TIMEOUT_Cr) \
	CANTP_RESULT(CANTP_RESULT_N_WRONG_SN) \
	CANTP_RESULT(CANTP_RESULT_N_INVALID_FS) \
	CANTP_RESULT(CANTP_RESULT_N_UNEXP_PDU) \
	CANTP_RESULT(CANTP_RESULT_N_WFT_OVRN) \
	CANTP_RESULT(CANTP_RESULT_N_BUFFER_OVFLW) \
	CANTP_RESULT(CANTP_RESULT_N_ERROR) \

enum CANTP_RESULT_ENUM {
	FOREACH_CANTP_RESULT(GENERATE_ENUM)
};

#define FOREACH_CANTP_FC_FLOW_STATUS(CANTP_FC_FLOW_STATUS) \
		CANTP_FC_FLOW_STATUS(CANTP_FC_FLOW_STATUS_CTS) \
		CANTP_FC_FLOW_STATUS(CANTP_FC_FLOW_STATUS_WAIT) \
		CANTP_FC_FLOW_STATUS(CANTP_FC_FLOW_STATUS_OVF) \

enum CANTP_FC_FLOW_STATUS_ENUM {
	FOREACH_CANTP_FC_FLOW_STATUS(GENERATE_ENUM)
};

#define CANTP_N_AS_TIMER_MS	1000	//Time for transmission of the CAN frame
									//(any N_PDU) on the sender side
#define CANTP_N_BS_TIMER_MS	1000	//Time until reception of the next
									//FlowControl N_PDU
#define CANTP_N_CS_TIMER_MS	1000	//Time until transmission of the next
									//ConsecutiveFrame N_PDU
#define CANTP_N_AR_TIMER_MS	1000	//Time for transmission of the CAN frame
									//(any N_PDU) on the receiver side
#define CANTP_N_BR_TIMER_MS	1000
#define CANTP_N_CR_TIMER_MS	1000

#define CANTP_FF_LEN_H(x) ((uint8_t)((0xF00 & x) >> 8))
#define CANTP_FF_LEN_L(x) ((uint16_t)(0xFF & x))


//Please note that the both roles "Receiver" and "Sender" form CAN-TP
// point of view can send and receive CAN frames (link layer)
#define FOREACH_CANTP_STATE(CANTP_STATE) \
		CANTP_STATE(CANTP_STATE_IDLE) \
		CANTP_STATE(CANTP_STATE_SF_SENDING)	/*SF sent to CAN driver and waiting for response */ \
		CANTP_STATE(CANTP_STATE_FF_SENDING)	/*SF sent to link layer and waiting for confirma */ \
		CANTP_STATE(CANTP_STATE_FF_SENT) 	/*First frame sent from the Sender               */ \
		CANTP_STATE(CANTP_STATE_FF_RCVD) 	/*First frame received from Receiver             */ \
		CANTP_STATE(CANTP_STATE_FF_FC_WAIT)	/*First frame sent but waiting for FC frame      */ \
		CANTP_STATE(CANTP_STATE_FC_SENDING)	/*Flow control frame sending from the receiver   */ \
		CANTP_STATE(CANTP_STATE_FC_SENT) 	/*FC is already sent from the Receiver           */ \
		CANTP_STATE(CANTP_STATE_FC_RCVD) 	/*Flow control frame received from the receiver  */ \
		CANTP_STATE(CANTP_STATE_CF_WAIT) 	/*Consecutive frame waiting from the receiver    */ \
		CANTP_STATE(CANTP_STATE_CF_SENDING)	/*CF sent to link layer but TX is not confirmed  */ \
		CANTP_STATE(CANTP_STATE_CF_SENT) 	/*CF sent. Next will send another CF*/ \
		CANTP_STATE(CANTP_STATE_CF_FC_WAIT)	/*CF sent. Waiting for FC frame */ \
		CANTP_STATE(CANTP_STATE_TX_DONE) \
		CANTP_STATE(CANTP_STATE_RX_DONE)

enum CANTP_STATE_ENUM {
		FOREACH_CANTP_STATE(GENERATE_ENUM)
};

#define CANTP_SF_NUM_DATA_BYTES	7
#define CANTP_FF_NUM_DATA_BYTES	6
#define CANTP_CF_NUM_DATA_BYTES 7

typedef struct __attribute__((packed)) {
	union {
		union {
			uint8_t u8[8];
			uint16_t u16[4];
			uint32_t u32[2];
			uint64_t u64;
		};
		struct __attribute__((packed)) {
			union {
				struct __attribute__((packed)) {
					uint8_t :4;
					uint8_t n_pci_t:4;
				};
				struct __attribute__((packed)) {
					uint8_t len:4;
					uint8_t :4;
					uint8_t d[CANTP_SF_NUM_DATA_BYTES];
				} sf;	// SingleFrame
				struct __attribute__((packed)) {
					uint8_t len_h:4;
					uint8_t :4;
					uint8_t len_l;
					uint8_t d[CANTP_FF_NUM_DATA_BYTES];
				} ff;	// FirstFrame
				struct __attribute__((packed)) {
					uint8_t sn:4;
					uint8_t :4;
					uint8_t d[CANTP_CF_NUM_DATA_BYTES];
				} cf;	// ConsecutiveFrame
				struct __attribute__((packed)) {
					uint8_t fs:4;	// FlowStatus error handling
					uint8_t :4;
					uint8_t bs;	// BlockSize parameter definition
					uint8_t st;	// SeparationTime minimum (STmin) parameter definition
				} fc;	// FlowControl
			};
		};
	};
} cantp_frame_t;

typedef struct __attribute__((packed)) {
	union {
		uint32_t fir;
		struct {
			uint32_t dlc:4;	//number of data bytes in message
			uint32_t :2;	//reserved
			uint32_t rtr:1;	//Remote Transmission Request
			uint32_t idt:1;	//ID type(Frame Format): 0: 11bit or 1: 29 bit
			uint32_t :24;
		};
	};
	uint32_t id;			//CAN ID
		uint8_t data_u8[8];
} cantp_can_frame_t;

//These are local parameters either we are on the role of Receiver or Sender
typedef struct cantp_rcvr_params_s {
	uint32_t id;		//CAN-LL Identifier
	uint8_t idt;		//CAN-LL ID Type (11-bit or 29-bit)
	uint8_t block_size;	//Block Size parameter of the Receiver
	uint32_t st_min_us;	//STmin parameter of the Receiver in microseconds
	uint8_t st_min;		//FC.STmin frame parameter of the Receiver
	uint8_t wft_num;	//Number of FC.WAIT frames that will be transmitted in a raw
	uint8_t wft_max;	//Maximum number of FC.WAIT frame transmissions (N_WFTmax)
						//of the Receiver
	uint32_t wft_tim_us;//Period on which the the FC.WAIT frame will be transmitted
} cantp_params_t;

typedef struct sndr_state_s {
	uint8_t state;
	void *state_sem;
	uint8_t *data;		//pointer to a buffer of data to be sent/received
	uint16_t len;		//Number of bytes that should be send/received
	uint16_t index;		//Bytes already sent/received when using segmented data
	uint32_t id_pair;	//CAN-ID of the other peer that we are communicating with
	uint8_t sn;			//Sequence number
	uint8_t bs_pair;	//The number of CF that will be send/received without FC
	uint8_t bl_index;	//counter related to bs
	uint8_t st_pair;	//SeparationTime minimum of the Receiver (other pair)
	uint32_t st_tim_us;	//timer related to st in μs (microseconds)
	void *timer;
	void *st_timer;
} sndr_state_t;

typedef struct rcvr_state_s {
	uint8_t state;
	uint8_t *data;		//pointer to a buffer of data to be sent/received
	uint16_t len;		//Number of bytes that should be send/received
	uint16_t index;		//Bytes already sent/received when using segmented data
	uint32_t id_pair;	//CAN-ID of the other peer that we are communicating with
	uint8_t sn;			//Sequence number
	uint8_t block_size;	//Block Size parameter of the Receiver
	uint8_t bl_index;	//counter related to bs
	uint8_t wft_cntr;	//Counter of FC.WAIT frame transmissions (N_WFTmax)
	void *timer;
} rcvr_state_t;

typedef struct cantp_rxtx_state_s {
	sndr_state_t sndr;
	rcvr_state_t rcvr;
	cantp_params_t *params;
	void *cb_ctx;
} cantp_rxtx_status_t;

static inline void cantp_ff_len_set(cantp_frame_t *cantp_ff, uint16_t len)
{
	cantp_ff->ff.len_h = CANTP_FF_LEN_H(len);
	cantp_ff->ff.len_l = CANTP_FF_LEN_L(len);
}

static inline uint16_t cantp_ff_len_get(cantp_frame_t *cantp_ff)
{
	return ((uint16_t)cantp_ff->ff.len_h << 8) | (cantp_ff->ff.len_l);
}

static inline void cantp_set_sndr_timer_ptr(void *timer, cantp_rxtx_status_t *state)
{
	cantp_logv("\t\tcantp_set_sndr_timer_ptr (%p)\n", timer); fflush(0);
	state->sndr.timer = timer;
}

static inline void cantp_set_rcvr_timer_ptr(void *timer, cantp_rxtx_status_t *state)
{
	state->rcvr.timer = timer;
//	printf("cantp_timer(2) = %x\n", state->timer);
}

static inline void cantp_set_st_timer_ptr(void *timer, cantp_rxtx_status_t *state)
{
	state->sndr.st_timer = timer;
//	printf("cantp_timer = %x\n", state->st_timer);
}

static inline void cantp_set_sttimer_ptr(void *timer, cantp_rxtx_status_t *state)
{
	state->sndr.st_timer = timer;
}

/*
 * int cantp_rcvr_params_init(cantp_rxtx_status_t *ctx, cantp_params_t *par, char *name)
 *
 */
int cantp_rcvr_params_init(cantp_rxtx_status_t *ctx, cantp_params_t *par, char *name);

/*
 * int cantp_timer_start(void *timer, char *name,  long tout_us);
 *
 */
int cantp_timer_start(void *timer, char *name,  long tout_us);

/*
 *
 */
int cantp_is_timer_expired(void *timer);

/*
 *
 */
void cantp_usleep(long tout_us);

/*
 * void cantp_timer_stop(void *timer);
 *
 */
void cantp_timer_stop(void *timer);

/*
 *
 *
 */
int cantp_sndr_state_sem_take(cantp_rxtx_status_t *ctx, uint32_t tout_us);

/*
 *
 *
 */
void cantp_sndr_state_sem_give(cantp_rxtx_status_t *ctx);

/*
 *
 *
 *
 */
int cantp_can_rx(cantp_can_frame_t *rx_frame, uint32_t tout_us);

/*
 * int cantp_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
 *
 * Transmits a CAN Frame in none blocking mode
 * Should be implemented from physical/link layer (CAN Driver)
 *
 * This function is not blocking. Usualy puts the next frame into
 * a queue that is transmitted by the link layer (CAN driver)
 *
 */
int cantp_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data);

/*
 * int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data,
 *														uint32_t tout_us);
 *
 * Same as cantp_can_tx_nb() but blocks untill the CAN Frame is sent.
 * Should be implemented from physical/link layer (CAN Driver)
 */
int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, long tout_us);

/*
 * This function should be called from the data link layer (CAN Driver)
 * in every successful transmission of a CAN Frame
 *
 */
void cantp_cantx_confirm_cb(cantp_rxtx_status_t *ctx);

/*
 * void cantp_sndr_result_cb(int result)
 *
 * ISO 15765-2:2016 Sender N_USData.con
 *
 * This function is called by:
 *  - CAN-TP (transport/network) layer to inform the session layer protocols
 *  for the result of the transmission or reception of a CAN-TP message or
 *  - when the some of the timers N_As, N_Bs or N_Cs has expired.
 *
 * The result codes are listed above in CANTP_RESULT enum
 * and are defined by ISO 15765-2:2016 8.3.7 <N_Result>
 *
 */
void cantp_sndr_result_cb(int result);

/*
 * void cantp_received_cb(cantp_rxtx_status_t *ctx,
 * 			uint32_t id, uint8_t idt, uint8_t *data, uint8_t len);
 *
 * This function should be implemented by session layer to receive
 * the message from CAN-TP (transport/network layer)
 *
 */
void cantp_received_cb(cantp_rxtx_status_t *ctx,
			uint32_t id, uint8_t idt, uint8_t *data, uint8_t len);

/*
 * void cantp_canrx_cb(uint32_t id, uint8_t idt, uint8_t dlc,
 * 							uint8_t *data, cantp_rxtx_status_t *ctx)
 *
 * ISO 15765-2:2016 Receiver N_USData.ind
 *
 * This function is called by data link layer (CAN driver) when
 * a valid CAN frame has been received (Receiver L_Data.ind).
 * It analyzes the CAN frame if it is part of data receiving flow
 * or data sending flow (Flow Control frame).
 * - In case of CAN frame is a part of data receiving flow and is a
 * Single Frame or Last Consecutive Frame the function should call
 * the session layer to inform for the completion of receiving of
 * the CAN-TP message. This is done by calling:
 * void cantp_received_cb(cantp_rxtx_status_t *ctx,
 *		uint32_t id, uint8_t idt, uint8_t *data, uint16_t len);
 * - In case of CAN frame is a part of data sending flow (Flow Control
 * frame) it should set the previously defined parameters (Block Size
 * and STmin) that depend on characteristics of the receiving hardware
 * and software.
 *
 */
void cantp_canrx_cb(uint32_t id, uint8_t idt, uint8_t dlc,
								uint8_t *data, cantp_rxtx_status_t *ctx);



/*
 * void cantp_rcvr_rx_ff_cb(cantp_rxtx_status_t *ctx,
 *			uint32_t id, uint8_t idt, uint8_t *data, uint16_t len)
 *
 * This function is a part of the Receiver side.
 * It should be implemented from the Session Layer and will be called
 * from CAN-TP (transport/network) layer to inform it for a beginning
 * of a reception of a segmented message with a given length coming
 * from given "id" and "idt"
 *
 * The session layer should allocate a memory for the data buffer
 * and perform some other tasks.
 *
 * returns:
 * 0 if the Receiver accepts the connection from this ID (Sender)
 */
int cantp_rcvr_rx_ff_cb(uint32_t id, uint8_t idt, uint8_t **data, uint16_t len);

/*
 * void cantp_sndr_tx_done_cb(void);
 *
 * This function is a part of the Sender side.
 * It should be implemented from the Session Layer and will be called
 * from CAN-TP (transport/network) layer to inform it for the end
 * transmission of the segmented message.
 *
 */
void cantp_sndr_tx_done_cb(void);

/*
 * void cantp_sndr_timer_cb(cantp_rxtx_status_t *ctx)
 *
 * Should be called on sender timer expire event
 *
 */
void cantp_sndr_timer_cb(cantp_rxtx_status_t *ctx);

/*
 * void cantp_rcvr_timer_cb(cantp_rxtx_status_t *ctx)
 *
 * Should be called on sender timer expire event
 */
void cantp_rcvr_timer_cb(cantp_rxtx_status_t *ctx);

/*
 * void cantp_sndr_wait_tx_done(cantp_rxtx_status_t *ctx, uint32_t tout_us)
 *
 */
int cantp_sndr_wait_tx_done(cantp_rxtx_status_t *ctx, uint32_t tout_us);

/*
 *
 *
 */
void cantp_rx_task(void *arg);

/*
 *
 *
 *
 */
void cantp_sndr_task(void *arg);

/*
 * cantp_send_nb(	cantp_rxtx_status_t *cantp_ctx,
 * 					uint32_t id,
 * 					uint8_t idt,
 * 					uint8_t *data,
 * 					uint16_t len)
 *
 */
void cantp_send(	cantp_rxtx_status_t *ctx,
					uint32_t id,
					uint8_t idt,
					uint8_t *data,
					uint16_t len);

/*
 * void print_cantp_frame(cantp_frame_t cantp_frame)
 *
 * Should be externally implemented for different systems
 *
 */
void print_cantp_frame(cantp_frame_t cantp_frame);

#endif /* _CAN_TP_H_ */
