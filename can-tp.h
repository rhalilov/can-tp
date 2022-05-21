/*
 * can-tp.h
 *
 *  Created on: May 9, 2022
 *      Author: refo
 */

#ifndef _CAN_TP_H_
#define _CAN_TP_H_

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

#define CANTP_N_AS_TIMER_MS	1000	//Time for transmission of the CAN frame
									//(any N_PDU) on the sender side
#define CANTP_N_BS_TIMER_MS	1000	//Time until reception of the next
									//FlowControl N_PDU
#define CANTP_N_CS_TIMER_MS	1000	//Time until transmission of the next
									//ConsecutiveFrame N_PDU

#define CANTP_FC_STARUS_CTS		0
#define CANTP_FC_STARUS_WAIT	1
#define CANTP_FC_STARUS_OF		2

#define CANTP_FF_LEN_H(x) ((uint8_t)((0xF00 & x) >> 8))
#define CANTP_FF_LEN_L(x) ((uint16_t)(0xFF & x))

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
					uint8_t frame_t:4;
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

typedef enum {
	CANTP_STATE_IDOL	= 0,
	CANTP_STATE_SF_SENDING,			//SF sent to CAN driver and waiting for response
	CANTP_STATE_SF_SENT,			//SF sending successful
	CANTP_STATE_FF_SENT,			//First frame sent
	CANTP_STATE_FF_FC_WAIT,			//First frame sent but waiting for FC frame
	CANTP_STATE_FC_RCVD,			//Flow control frame received
	CANTP_STATE_CF_SENT,			//Consecutive frame sent (see sequence number)
	CANTP_STATE_CF_FC_WAIT,			//Consecutive frame sent but waiting for FC frame
	CANTP_STATE_TX_DONE,
} cantp_state_t;

typedef struct cantp_rxtx_state_s {
	cantp_state_t state;
	uint32_t id;
	uint8_t idt;
	uint8_t *data;			//pointer to a buffer of data to be sent/received
	uint16_t len;			//Number of bytes that should be send/received
	uint16_t index;			//Bytes already sent/received when using segmented data
	uint8_t sn;				//Sequence number
	uint8_t bs;				//The number of CF that will be send/received without FC
	uint8_t bl_index;		//counter related to bs
	uint8_t st;				//SeparationTime minimum
	uint32_t st_timer_us;	//timer related to st in μs (microseconds)
	uint8_t error;			//Error code
	void *timer;
} cantp_rxtx_status_t;

typedef struct cantp_context_s {
	cantp_rxtx_status_t rx_state;
	cantp_rxtx_status_t tx_state;
	void (*timer_start)(void *timer, long tout_us);
	void (*timer_stop)(void *timer);
} cantp_context_t;


static inline void cantp_ff_len_set(cantp_frame_t *cantp_ff, uint16_t len)
{
	cantp_ff->ff.len_h = CANTP_FF_LEN_H(len);
	cantp_ff->ff.len_l = CANTP_FF_LEN_L(len);
}

static inline void cantp_set_timer_ptr(void *timer, cantp_rxtx_status_t *state)
{
	state->timer = timer;
}

/*
 *
 *
 */
int cantp_timer_start(void *timer, char *name,  long tout_us);

/*
 *
 *
 */
void cantp_timer_stop(void *timer);

/*
 * int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
 *
 * Transmits a CAN Frame
 * Should be implemented from physical layer (CAN Driver)
 *
 * This function is not blocking. Usualy puts the next frame into
 * a queue that is transmitted by the CAN driver
 *
 */
int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data);

/*
 * This function should be called from CAN Driver (Physical Layer)
 * in every successful transmission of a CAN Frame
 *
 */
void cantp_cantx_confirm_cb(cantp_context_t *ctx);

/*
 * void cantp_result_cb(int result)
 *
 * This function is called by CAN-TP layer to inform the upper layer
 * protocols for the result of the transmission or reception
 * the result codes are listed above in CANTP_RESULT enum
 * and are defined by ISO 15765-2:2016 8.3.7 <N_Result>
 *
 */
void cantp_result_cb(int result);

/*
 *
 */
void cantp_rx_cb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data,
					cantp_context_t *ctx);

/*
 * cantp_tx_timer_cb(cantp_context_t *ctx)
 *
 * Should be called on timer expire event
 *
 */
void cantp_tx_timer_cb(cantp_context_t *ctx);

/*
 * can_tp_send(cantp_context_t *cantp_ctx,
 * 				uint32_t id,
 * 				uint8_t idt,
 * 				uint8_t *data,
 * 				uint16_t len)
 *
 */
int cantp_send(cantp_context_t *ctx,
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
