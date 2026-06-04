/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2022, Arm Limited. All rights reserved.
 *
 */

#ifndef __RSE_COMMS_PROTOCOL_EMBED_H__
#define __RSE_COMMS_PROTOCOL_EMBED_H__

#include <lk/compiler.h>

#include <psa/client.h>

#define PLAT_RSE_COMMS_PAYLOAD_MAX_SIZE (0x40 + 0x800)

struct rse_embed_msg_t {
	psa_handle_t handle;
	uint32_t ctrl_param; /* type, in_len, out_len */
	uint16_t io_size[PSA_MAX_IOVEC];
	uint8_t trailer[PLAT_RSE_COMMS_PAYLOAD_MAX_SIZE];
} __PACKED;

struct rse_embed_reply_t {
	int32_t return_val;
	uint16_t out_size[PSA_MAX_IOVEC];
	uint8_t trailer[PLAT_RSE_COMMS_PAYLOAD_MAX_SIZE];
} __PACKED;

psa_status_t rse_protocol_embed_serialize_msg(psa_handle_t handle,
					      int16_t type,
					      const uint8_t *in_data,
					      struct rse_embed_msg_t *msg,
					      size_t *msg_len);

psa_status_t rse_protocol_embed_deserialize_reply( const uint8_t *in_data,
	      	  	  	  	  uint8_t *out_data,
						  psa_status_t *return_val,
						  const struct rse_embed_reply_t
						  *reply,
						  size_t reply_size);

#endif /* __RSE_COMMS_PROTOCOL_EMBED_H__ */
