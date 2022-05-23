/*
 * fake_can_linux.h
 *
 *  Created on: May 21, 2022
 *      Author: refo
 */

#ifndef _FAKE_CAN_LINUX_H_
#define _FAKE_CAN_LINUX_H_

int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data);
int fake_can_rx_task(void *params);
int fake_can_init(long tx_delay_us, void *params);
void fake_cantx_confirm_cb(void *params);
void fake_canrx_cb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, void *params);

#endif /* _FAKE_CAN_LINUX_H_ */
