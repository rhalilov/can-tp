/*
 * cantp_glue_lin.h
 *
 *  Created on: May 21, 2022
 *      Author: refo
 */

#ifndef _CANTP_GLUE_LIN_H_
#define _CANTP_GLUE_LIN_H_

void cantp_result_cb(int result);
void cantp_sndr_t_cb(cbtimer_t *tim);
void cantp_rcvr_t_cb(cbtimer_t *tim);

#endif /* _CANTP_GLUE_LIN_H_ */
