/*
 * fake_can_linux.h
 *
 *  Created on: May 21, 2022
 *      Author: refo
 */

#ifndef _FAKE_CAN_LINUX_H_
#define _FAKE_CAN_LINUX_H_

#ifndef GENERATE_ENUM
#define GENERATE_ENUM(ENUM) ENUM,
#endif
#ifndef GENERATE_STRING
#define GENERATE_STRING(STRING) #STRING,
#endif

#define FOREACH_CAN_LL_PEER_TYPE(CAN_LL_PEER_TYPE) \
	CAN_LL_PEER_TYPE(CAN_LL_SENDER) \
	CAN_LL_PEER_TYPE(CAN_LL_RECEIVER) \

enum CAN_LL_PEER_TYPE_ENUM {
	FOREACH_CAN_LL_PEER_TYPE(GENERATE_ENUM)
};

int fake_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data);
int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, long tout_us);
int fake_can_wait_txdone(long tout_us);
int fake_can_rx_task(void *params);
int fake_can_init(long tx_delay_us, char *name, void *params, uint8_t sndr_rcvr);
void fake_cantx_confirm_cb(void *params);
void fake_canrx_cb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, void *params);

#endif /* _FAKE_CAN_LINUX_H_ */
