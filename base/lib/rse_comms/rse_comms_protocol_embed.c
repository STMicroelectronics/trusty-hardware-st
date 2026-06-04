// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2022-2024, Arm Limited. All rights reserved.
 *
 */

#include <assert.h>
#include "rse_comms_protocol_common.h"
#include "rse_comms_protocol_embed.h"
#include <string.h>

#define TLOG_TAG "rse_comms"
#include <trusty_log.h>

psa_status_t rse_protocol_embed_serialize_msg(psa_handle_t handle,
					      int16_t type,
					      const uint8_t *in_data,
					      struct rse_embed_msg_t *msg,
					      size_t *msg_len)
{
	uint32_t i;
    struct psa_iov_len* head_data_in;
	uint32_t payload_offset = 0;

	assert(msg);
	assert(msg_len);
	assert(in_data);

	head_data_in = (struct psa_iov_len*) in_data;

	msg->ctrl_param = PARAM_PACK(type, head_data_in->iov_in_len, head_data_in->iov_out_len -1);
	msg->handle = handle;

	/* Fill msg iovec lengths */
	for (i = 0U; i < head_data_in->iov_in_len; ++i) {
		msg->io_size[i] = head_data_in->io_in_size[i];

		if (head_data_in->io_in_size[i] > sizeof(msg->trailer) - payload_offset)
			return PSA_ERROR_INVALID_ARGUMENT;

		payload_offset = payload_offset + head_data_in->io_in_size[i];
	}

	for (i = 0U; i < (head_data_in->iov_out_len -1); ++i)
		msg->io_size[head_data_in->iov_in_len + i] = head_data_in->io_out_size[i+1];

	memcpy(msg->trailer, in_data + sizeof(struct psa_iov_len), payload_offset);

	/* Output the actual size of the message, to optimize sending */
	*msg_len = sizeof(*msg) - sizeof(msg->trailer) + payload_offset;

	return PSA_SUCCESS;
}


psa_status_t rse_protocol_embed_deserialize_reply( const uint8_t *in_data,
	      	  	  	  	  uint8_t *out_data,
						  psa_status_t *return_val,
						  const struct rse_embed_reply_t
						  *reply,
						  size_t reply_size)
{
	uint32_t payload_offset = 0;
    struct psa_iov_len* head_data_in;
	uint32_t i;

	assert(reply);
	assert(return_val);
	assert(in_data);
	assert(out_data);

	head_data_in = (struct psa_iov_len*) in_data;

	/* Fill msg iovec lengths */
	for (i = 0U; i < (head_data_in->iov_out_len - 1); ++i) {
		if ((sizeof(*reply) - sizeof(reply->trailer) + payload_offset)
		    > reply_size) {
			return PSA_ERROR_INVALID_ARGUMENT;
		}
		payload_offset += reply->out_size[i];
	}

	memcpy(out_data, reply->trailer, payload_offset);

	*return_val = reply->return_val;

	return PSA_SUCCESS;
}


