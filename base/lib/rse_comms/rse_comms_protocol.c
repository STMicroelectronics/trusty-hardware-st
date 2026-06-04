// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2022, Arm Limited. All rights reserved.
 *
 */
#include <assert.h>

#include "rse_comms_protocol.h"

#define TLOG_TAG "rse_comms"
#include <trusty_log.h>

psa_status_t rse_protocol_serialize_msg(psa_handle_t handle,
					int16_t type,
					const uint8_t *in_data,
					const uint8_t *out_data,
					struct serialized_rse_comms_msg_t *msg,
					size_t *msg_len)
{
	psa_status_t status;

	assert(msg);
	assert(msg_len);
	assert(in_data);

	switch (msg->header.protocol_ver) {
	case RSE_COMMS_PROTOCOL_EMBED:
		status = rse_protocol_embed_serialize_msg(handle, type,
	                          in_data,
					          &msg->msg.embed,
							  msg_len);
		if (status != PSA_SUCCESS)
			return status;
		break;
	case RSE_COMMS_PROTOCOL_POINTER_ACCESS:
		status = rse_protocol_pointer_access_serialize_msg(handle, type,
								in_data,
								out_data,
								&msg->msg.pointer_access,
								msg_len);
		if (status != PSA_SUCCESS)
			return status;
		break;
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}

	*msg_len += sizeof(struct serialized_rse_comms_header_t);

	return PSA_SUCCESS;
}


psa_status_t rse_protocol_deserialize_reply(const uint8_t *in_data,
						uint8_t *out_data,
					    psa_status_t *return_val,
					    const struct
					    serialized_rse_comms_reply_t *reply,
					    size_t reply_size)
{
	assert(reply);
	assert(return_val);

	switch (reply->header.protocol_ver) {
	case RSE_COMMS_PROTOCOL_EMBED:
		return rse_protocol_embed_deserialize_reply(in_data, out_data,
							    return_val,
							    &reply->reply.embed,
							    reply_size);
	case RSE_COMMS_PROTOCOL_POINTER_ACCESS:
		return rse_protocol_pointer_access_deserialize_reply(in_data,
								     return_val,
								     &reply->reply.pointer_access,
								     reply_size);
	default:
		return PSA_ERROR_NOT_SUPPORTED;
	}

	return PSA_SUCCESS;
}

