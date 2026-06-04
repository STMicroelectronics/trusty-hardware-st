/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define TLOG_TAG "hwkey_ST_srv"

#include <assert.h>
#include <lk/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/err.h>

#include <openssl/aes.h>
#include <openssl/cipher.h>
#include <openssl/digest.h>
#include <openssl/err.h>
#include <openssl/hkdf.h>

#include <interface/hwaes/hwaes.h>
#include <interface/hwkey/hwkey.h>
#include <lib/system_state/system_state.h>
#include <lib/tipc/tipc.h>
#include <lib/tipc/tipc_srv.h>
#include <trusty_log.h>

#include <st_hwcrypto_consts.h>
#include "st_hwkey_srv_priv.h"
#include <interface/hwpsa/tfm_builtin_key_ids.h>

#pragma message "Compiling STM HWKEY provider"


/* 0 means unlimited number of connections */
#define HWKEY_MAX_NUM_CHANNELS 0

/*
 *  This module is a sample only. For real device, this code
 *  needs to be rewritten to operate on real per device key that
 *  should come directly or indirectly from hardware.
 */

/* M33_TFM_PSA : target is to fuse the device key and shared key in OTP,
  currently the key resulting of the HKDF executed in M33 TFM with a builtin key,
  is not the one expected because an OTP subkey is derived first,
  in order to complete the android boot and execute CTS tests a workaround is to import the secret key,
  (fake_device_key and share_device_key).
*/

/*static uint8_t fake_device_key[32] = "this is a fake unique device key";*/
/* M33_TFM_PSA : in hexa, to compare with the implementation of the fake OTP key in M33 TFM builtin key */
/*static uint8_t fake_device_key[32] = {0x74, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20,
									  0x61, 0x20, 0x66, 0x61, 0x6b, 0x65, 0x20, 0x75,
									  0x6e, 0x69, 0x71, 0x75, 0x65, 0x20, 0x64, 0x65,
									  0x76, 0x69, 0x63, 0x65, 0x20, 0x6b, 0x65, 0x79};*/

/*TODO: M33_TFM_PSA : it is planned also to fuse the shared key in OTP but in a second step, the shared key is only use in the derive_key_versioned_v1
 * function , is it really used ?  do we need to keep the implementation so the shared key ?
 */
static uint8_t fake_shared_key[32] = "this is a fake shared device key";


#define KEY_DERIV_SECRET_TRUSTY_LEN 32

/*
 * Fake rollback versions for testing the sample app. These versions should be
 * pulled from a secure source of truth in a real implementation.
 */
#define FAKE_TRUSTY_RUNNING_ROLLBACK_VERSION 2
#define FAKE_TRUSTY_COMMITTED_ROLLBACK_VERSION 1

/* This input vector is taken from RFC 5869 (Extract-and-Expand HKDF) */
static const uint8_t IKM[] = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                              0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                              0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};

static const uint8_t salt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                               0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};

static const uint8_t info[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
                               0xf5, 0xf6, 0xf7, 0xf8, 0xf9};

/* Expected pseudorandom key */
/*static const uint8_t exp_PRK[] = { 0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32,
   0xdf, 0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63, 0x90, 0xb6, 0xc7, 0x3b,
   0xb5, 0x0f, 0x9c, 0x31, 0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5 };*/

/* Expected Output Key */
static const uint8_t exp_OKM[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f,
        0x64, 0xd0, 0x36, 0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a,
        0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf, 0x34,
        0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65};

static bool hkdf_self_test(void) {
    int res;
    uint8_t OKM[sizeof(exp_OKM)];

    TLOGI("hkdf self test\n");

    /* Check if OKM is OK */
    memset(OKM, 0x55, sizeof(OKM));

    /* TODO: M33_TFM_PSA the secret key is the IKM, the key need to be imported in M33 TFM */
    res = derive_key_v1(TFM_BUILTIN_KEY_ID_MIN, IKM, sizeof(IKM), salt, sizeof(salt), info, sizeof(info), OKM, sizeof(exp_OKM));

    if (res) {
        TLOGE("hkdf: failed 0x%x\n", ERR_get_error());
        return false;
    }

    res = memcmp(OKM, exp_OKM, sizeof(OKM));
    if (res) {
        TLOGE("hkdf: data mismatch\n");
        return false;
    }

    TLOGI("hkdf self test: PASSED\n");
    return true;
}

/*
 * Derive key V1 - HMAC SHA256 based Key derivation function
 */

#define KEY_DERIV_OUTPUT_LEN       32

/* TODO: M33_TFM_PSA : intermediate large buffer for exported key,
 * perhaps we can export the key directly to the key_buf, and set the key output attribut
 * with the size the expected key ley_len
 */
uint8_t exported_deriv_data[2*KEY_DERIV_OUTPUT_LEN] = {0};
    size_t exported_deriv_data_size = 0;

uint32_t derive_key_v1(psa_key_id_t secret_key_id,
                       const uint8_t* key_to_import,
                       size_t key_to_import_len,
                       const uint8_t* salt,
                       size_t salt_len,
                       const uint8_t* ikm_data,
                       size_t ikm_len,
                       uint8_t* key_buf,
                       size_t key_len) {

	int ret = 0;
	hwpsa_session_t session;
	psa_key_id_t input_key_id_local = TFM_BUILTIN_KEY_ID_TRUSTY_DK;
	psa_key_id_t output_key_id_local = PSA_KEY_ID_NULL;
	psa_algorithm_t deriv_alg = PSA_ALG_HKDF(PSA_ALG_SHA_256);
	psa_key_attributes_t output_key_attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_derivation_operation_t deriv_ops;
	psa_status_t status = PSA_SUCCESS;

	/****************/
	/****************/
	/* TODO: M33_TFM_PSA : psa_key_attributes_t contain  mbedtls_svc_key_id_t MBEDTLS_PRIVATE(id)
	 * to set the key_id and the owner, the owner can be used to derive a key link to an application/user
	 * the attributs of the key in OTP are hardcoded, how can we add this owner parameter and which value,
	 * can we use this Trusty owner value to prevent subkey derivation.
	 */
	psa_key_attributes_t input_key_attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_type_t key_type;

	session = hwpsa_open();

	deriv_ops = psa_key_derivation_operation_init();

	psa_set_key_usage_flags(&input_key_attr, PSA_KEY_USAGE_DERIVE);
	psa_set_key_algorithm(&input_key_attr, deriv_alg);
	key_type = PSA_KEY_TYPE_DERIVE;
	psa_set_key_type(&input_key_attr, key_type);

	switch (secret_key_id) {

	case TFM_BUILTIN_KEY_ID_TRUSTY_DK :
		input_key_id_local = TFM_BUILTIN_KEY_ID_TRUSTY_DK;
		/* M33_TFM_PSA : WORKAROUND import the secret key instead of using the TFM builtin key in OTP */
		/* status = psa_import_key_ss(session, &input_key_attr, fake_device_key,
					KEY_DERIV_SECRET_TRUSTY_LEN, &input_key_id_local);*/
		break;
	case TFM_BUILTIN_KEY_ID_TRUSTY_SK :
		status = psa_import_key_ss(session, &input_key_attr, fake_shared_key,
					KEY_DERIV_SECRET_TRUSTY_LEN, &input_key_id_local);
		break;
	case TFM_BUILTIN_KEY_ID_MIN :
		status = psa_import_key_ss(session, &input_key_attr, key_to_import,
				key_to_import_len, &input_key_id_local);
		break;
	default :
		return HWKEY_ERR_NOT_VALID;
		break;
	}

    if (status != PSA_SUCCESS) {
		return HWKEY_ERR_GENERIC;
	}
	

	deriv_ops = psa_key_derivation_operation_init();

	status = psa_key_derivation_setup_ss(session, &deriv_ops, deriv_alg);
	if (status != PSA_SUCCESS) {
		ret = HWKEY_ERR_GENERIC;
		goto destroy_key;
	}

	status = psa_key_derivation_input_bytes_ss(session, &deriv_ops,
						PSA_KEY_DERIVATION_INPUT_SALT,
						salt,
						salt_len);

	if (status != PSA_SUCCESS) {
		ret = HWKEY_ERR_GENERIC;
		goto deriv_abort;
	}

	status = psa_key_derivation_input_key_ss(session, &deriv_ops,
					      PSA_KEY_DERIVATION_INPUT_SECRET,
						  input_key_id_local);

	if (status != PSA_SUCCESS) {
		ret = HWKEY_ERR_GENERIC;
		goto deriv_abort;
	}

	status = psa_key_derivation_input_bytes_ss(session, &deriv_ops,
						PSA_KEY_DERIVATION_INPUT_INFO,
						ikm_data,
						ikm_len);

	if (status != PSA_SUCCESS) {
		ret = HWKEY_ERR_GENERIC;
		goto deriv_abort;
	}

	psa_set_key_type(&output_key_attr, PSA_KEY_TYPE_RAW_DATA);
	psa_set_key_bits(&output_key_attr, PSA_BYTES_TO_BITS(key_len));
	psa_set_key_usage_flags(&output_key_attr, PSA_KEY_USAGE_EXPORT);

	status = psa_key_derivation_output_key_ss(session, &output_key_attr, &deriv_ops,
					       &output_key_id_local);
	if (status != PSA_SUCCESS) {
		ret = HWKEY_ERR_GENERIC;
		goto deriv_abort;
	}

	if (psa_export_key_ss(session, output_key_id_local,
			   exported_deriv_data,
			   sizeof(exported_deriv_data),
			   &exported_deriv_data_size)
	    != PSA_SUCCESS) {
		ret = HWKEY_ERR_GENERIC;
		goto deriv_abort;
	}
		
	if (exported_deriv_data_size > 2*KEY_DERIV_OUTPUT_LEN) {
		ret = HWKEY_ERR_GENERIC;
		goto deriv_abort;
	}
	memcpy(key_buf, exported_deriv_data, key_len);

deriv_abort:
	psa_key_derivation_abort_ss(session, &deriv_ops);
destroy_key:
    /* M33_TFM_PSA : WORKAROUND !!! delete in M33 the imported DK key also  */
       if (input_key_id_local != TFM_BUILTIN_KEY_ID_TRUSTY_DK) {
	        psa_destroy_key_ss(session, input_key_id_local);
	} 
	if (output_key_id_local) {
		psa_destroy_key_ss(session, output_key_id_local);
	}

	hwpsa_close(session);

	return ret;
}

/*
 * Context labels for key derivation contexts, see derive_key_versioned_v1() for
 * details.
 */
#define HWKEY_DERIVE_VERSIONED_CONTEXT_LABEL "DERIVE VERSIONED"
#define ROOT_OF_TRUST_DERIVE_CONTEXT_LABEL "TZOS"

#define HWKEY_DERIVE_VERSIONED_SALT "hwkey derive versioned salt"

static uint8_t context_buf[4096];

/**
 * fill_context_buf() - Add data to context_buf
 * @src: Pointer to data to copy into the context buf. If null, @len zero bytes
 *       will be added.
 * @len: Number of bytes of data to add.
 * @cur_position: Pointer to the next unwritten byte of context_buf. Updated
 *                with the new current position when successful.
 *
 * Return: HWKEY_NO_ERROR on success, HWKEY_ERR_BAD_LEN if @len will cause the
 * buffer to overflow.
 */
static uint32_t fill_context_buf(const void* src,
                                 size_t len,
                                 size_t* cur_position) {
    size_t new_position;
    if (len == 0) {
        return HWKEY_NO_ERROR;
    }
    if (__builtin_add_overflow(*cur_position, len, &new_position) ||
        new_position >= sizeof(context_buf)) {
        return HWKEY_ERR_BAD_LEN;
    }
    if (src == NULL) {
        memset(&context_buf[*cur_position], 0, len);
    } else {
        memcpy(&context_buf[*cur_position], src, len);
    }
    *cur_position = new_position;
    return HWKEY_NO_ERROR;
}

/*
 * In a real implementation this portion of the derivation should be done by a
 * trusted source of the Trusty OS rollback version. Doing the key derivation
 * here in the hwkey service protects against some app-level compromises, but
 * does not protect against compromise of any Trusty code that can derive
 * directly using the secret key derivation input - which in this sample
 * implementation would be the kernel and the hwkey service.
 *
 * This function MUST mix @rollback_version_source, @os_rollback_version, and
 * @hwkey_context into the derivation context in a way that the client cannot
 * forge.
 */
static uint32_t root_of_trust_derive_key(bool shared,
                                         uint32_t rollback_version_source,
                                         int32_t os_rollback_version,
                                         const uint8_t* hwkey_context,
                                         size_t hwkey_context_len,
                                         uint8_t* key_buf,
                                         size_t key_len) {
    size_t context_len = 0;
    int rc;
    const size_t root_of_trust_context_len =
            sizeof(ROOT_OF_TRUST_DERIVE_CONTEXT_LABEL) +
            sizeof(rollback_version_source) + sizeof(os_rollback_version);
    psa_key_id_t secret_key_id;
    size_t total_len;

    /*
     * We need to move the hwkey_context (currently at the beginning of
     * context_buf) over to make room for the root-of-trust context injected
     * before it. We avoid the need for a separate buffer by memmoving it first
     * then adding the context into the space we made.
     */
    if (__builtin_add_overflow(hwkey_context_len, root_of_trust_context_len,
                               &total_len) ||
        total_len >= sizeof(context_buf)) {
        return HWKEY_ERR_BAD_LEN;
    }
    memmove(&context_buf[root_of_trust_context_len], hwkey_context,
            hwkey_context_len);

    /*
     * Add a fixed label to ensure that another user of the same key derivation
     * primitive will not collide with this use, regardless of the provided
     * hwkey_context (as long as other users also add a different fixed label).
     */
    rc = fill_context_buf(ROOT_OF_TRUST_DERIVE_CONTEXT_LABEL,
                          sizeof(ROOT_OF_TRUST_DERIVE_CONTEXT_LABEL),
                          &context_len);
    if (rc) {
        return rc;
    }
    /* Keys for different version limit sources must be different */
    rc = fill_context_buf(&rollback_version_source,
                          sizeof(rollback_version_source), &context_len);
    if (rc) {
        return rc;
    }
    /*
     * Keys with different rollback versions must not be the same. This is part
     * of the root-of-trust context to ensure that a compromised kernel cannot
     * forge a version (if the root of trust is outside of Trusty)
     */
    rc = fill_context_buf(&os_rollback_version, sizeof(os_rollback_version),
                          &context_len);
    if (rc) {
        return rc;
    }

    assert(root_of_trust_context_len == context_len);

    /*
     * We already moved the hwkey_context into place after the root of trust
     * context.
     */
    context_len += hwkey_context_len;

    if (shared) {
    	secret_key_id = TFM_BUILTIN_KEY_ID_TRUSTY_SK;
    } else {
     	secret_key_id = TFM_BUILTIN_KEY_ID_TRUSTY_DK;
    }


    return derive_key_v1(secret_key_id, NULL, 0, (const uint8_t*)HWKEY_DERIVE_VERSIONED_SALT, sizeof(HWKEY_DERIVE_VERSIONED_SALT), context_buf, context_len, key_buf, key_len);
}

int32_t get_current_os_rollback_version(uint32_t source) {
    switch (source) {
    case HWKEY_ROLLBACK_RUNNING_VERSION:
        return FAKE_TRUSTY_RUNNING_ROLLBACK_VERSION;

    case HWKEY_ROLLBACK_COMMITTED_VERSION:
        return FAKE_TRUSTY_COMMITTED_ROLLBACK_VERSION;

    default:
        TLOGE("Unknown rollback version source: %u\n", source);
        return ERR_NOT_VALID;
    }
}

/*
 * Derive a versioned key - HMAC SHA256 based versioned key derivation function
 */
uint32_t derive_key_versioned_v1(
        const uuid_t* uuid,
        bool shared,
        uint32_t rollback_version_source,
        int32_t rollback_versions[HWKEY_ROLLBACK_VERSION_INDEX_COUNT],
        const uint8_t* user_context,
        size_t user_context_len,
        uint8_t* key_buf,
        size_t key_len) {
    size_t context_len = 0;
    int i;
    uint32_t rc = HWKEY_NO_ERROR;
    int32_t os_rollback_version =
            rollback_versions[HWKEY_ROLLBACK_VERSION_OS_INDEX];
    int32_t os_rollback_version_current =
            get_current_os_rollback_version(rollback_version_source);

    if (os_rollback_version_current < 0) {
        rc = HWKEY_ERR_NOT_VALID;
        goto err;
    }

    if (os_rollback_version > os_rollback_version_current) {
        TLOGE("Requested rollback version too new: %u\n", os_rollback_version);
        rc = HWKEY_ERR_NOT_FOUND;
        goto err;
    }

    /* short-circuit derivation if we have nothing to derive */
    if (key_len == 0) {
        rc = HWKEY_NO_ERROR;
        goto err;
    }

    /* for compatibility with unversioned derive, require a context */
    if (!key_buf || !user_context || user_context_len == 0) {
        rc = HWKEY_ERR_NOT_VALID;
        goto err;
    }

    /*
     * This portion of the context may always be added by the hwkey service, as
     * it deals with the identity of the client requesting the key derivation.
     */
    /*
     * Fixed label ensures that this derivation will not collide with a
     * different user of root_of_trust_derive_key(), regardless of the provided
     * user context (as long as other users also add a different fixed label).
     */
    rc = fill_context_buf(HWKEY_DERIVE_VERSIONED_CONTEXT_LABEL,
                          sizeof(HWKEY_DERIVE_VERSIONED_CONTEXT_LABEL),
                          &context_len);
    if (rc) {
        goto err;
    }
    /* Keys for different apps must be different */
    rc = fill_context_buf(uuid, sizeof(*uuid), &context_len);
    if (rc) {
        goto err;
    }
    for (i = 0; i < HWKEY_ROLLBACK_VERSION_SUPPORTED_COUNT; ++i) {
        /*
         * We skip the OS version because the root-of-trust should be inserting
         * that, and we don't want to mask a buggy implementation there in
         * testing. If the root-of-trust somehow did not insert the OS version,
         * we want to notice.
         */
        if (i == HWKEY_ROLLBACK_VERSION_OS_INDEX) {
            continue;
        }
        rc = fill_context_buf(&rollback_versions[i], sizeof(*rollback_versions),
                              &context_len);
        if (rc) {
            goto err;
        }
    }
    /* Reserve space for additional versions in the future */
    if (HWKEY_ROLLBACK_VERSION_SUPPORTED_COUNT <
        HWKEY_ROLLBACK_VERSION_INDEX_COUNT) {
        rc = fill_context_buf(NULL,
                              sizeof(*rollback_versions) *
                                      (HWKEY_ROLLBACK_VERSION_INDEX_COUNT -
                                       HWKEY_ROLLBACK_VERSION_SUPPORTED_COUNT),
                              &context_len);
        if (rc) {
            goto err;
        }
    }
    /*
     * Clients need to be able to generate multiple different keys in the same
     * app.
     */
    rc = fill_context_buf(user_context, user_context_len, &context_len);
    if (rc) {
        goto err;
    }

    rc = root_of_trust_derive_key(shared, rollback_version_source,
                                  os_rollback_version, context_buf, context_len,
                                  key_buf, key_len);
    if (rc) {
        goto err;
    }

err:
    memset(context_buf, 0, sizeof(context_buf));
    return rc;
}

/* UUID of HWCRYPTO_UNITTEST application */
static const uuid_t hwcrypto_unittest_uuid = HWCRYPTO_UNITTEST_APP_UUID;
static const uuid_t hwcrypto_unittest_rust_uuid =
        HWCRYPTO_UNITTEST_RUST_APP_UUID;

#if WITH_HWCRYPTO_UNITTEST
/*
 *  Support for hwcrypto unittest keys should be only enabled
 *  to test hwcrypto related APIs
 */

static uint8_t _unittest_key32[32] = "unittestkeyslotunittestkeyslotun";
static uint32_t get_unittest_key32(uint8_t* kbuf,
                                   size_t kbuf_len,
                                   size_t* klen) {
    assert(kbuf);
    assert(klen);
    assert(kbuf_len >= sizeof(_unittest_key32));

    /* just return predefined key */
    memcpy(kbuf, _unittest_key32, sizeof(_unittest_key32));
    *klen = sizeof(_unittest_key32);

    return HWKEY_NO_ERROR;
}

static uint32_t get_unittest_key32_handler(const struct hwkey_keyslot* slot,
                                           uint8_t* kbuf,
                                           size_t kbuf_len,
                                           size_t* klen) {
    return get_unittest_key32(kbuf, kbuf_len, klen);
}

/*
 * "unittestderivedkeyslotunittestde" encrypted with _unittest_key32 using an
 * all 0 IV. IV is prepended to the ciphertext.
 */
static uint8_t _unittest_encrypted_key32[48] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x3e, 0x2b, 0x02, 0x54, 0x54, 0x8c, 0xa7, 0xb8,
        0xa3, 0xfa, 0xf5, 0xd0, 0xbc, 0x1d, 0x40, 0x11, 0xac, 0x68, 0xbb, 0xf0,
        0x55, 0xa3, 0xc5, 0x49, 0x3e, 0x77, 0x4a, 0x8b, 0x3f, 0x33, 0x56, 0x07,
};

static unsigned int _unittest_encrypted_key32_size =
        sizeof(_unittest_encrypted_key32);

static uint32_t get_unittest_key32_derived(
        const struct hwkey_derived_keyslot_data* data,
        uint8_t* kbuf,
        size_t kbuf_len,
        size_t* klen) {
    return get_unittest_key32(kbuf, kbuf_len, klen);
}

static const struct hwkey_derived_keyslot_data hwcrypto_unittest_derived_data =
        {
                .encrypted_key_data = _unittest_encrypted_key32,
                .encrypted_key_size_ptr = &_unittest_encrypted_key32_size,
                .retriever = get_unittest_key32_derived,
};

static uint32_t derived_keyslot_handler(const struct hwkey_keyslot* slot,
                                        uint8_t* kbuf,
                                        size_t kbuf_len,
                                        size_t* klen) {
    assert(slot);
    return hwkey_get_derived_key(slot->priv, kbuf, kbuf_len, klen);
}

static const uuid_t* unittest_allowed_opaque_key_uuids[] = {
        &hwcrypto_unittest_uuid,
        &hwcrypto_unittest_rust_uuid,
};

static uint32_t get_unittest_key32_opaque(
        const struct hwkey_opaque_handle_data* data,
        uint8_t* kbuf,
        size_t kbuf_len,
        size_t* klen) {
    return get_unittest_key32(kbuf, kbuf_len, klen);
}

static struct hwkey_opaque_handle_data unittest_opaque_handle_data = {
        .allowed_uuids = unittest_allowed_opaque_key_uuids,
        .allowed_uuids_len = countof(unittest_allowed_opaque_key_uuids),
        .retriever = get_unittest_key32_opaque,
};

static struct hwkey_opaque_handle_data unittest_opaque_handle_data2 = {
        .allowed_uuids = unittest_allowed_opaque_key_uuids,
        .allowed_uuids_len = countof(unittest_allowed_opaque_key_uuids),
        .retriever = get_unittest_key32_opaque,
};

static struct hwkey_opaque_handle_data unittest_opaque_handle_data_noaccess = {
        .allowed_uuids = NULL,
        .allowed_uuids_len = 0,
        .retriever = get_unittest_key32_opaque,
};

/*
 * Adapter to cast hwkey_opaque_handle_data.priv field to struct
 * hwkey_derived_keyslot_data*
 */
static uint32_t get_derived_key_opaque(
        const struct hwkey_opaque_handle_data* data,
        uint8_t* kbuf,
        size_t kbuf_len,
        size_t* klen) {
    assert(data);
    return hwkey_get_derived_key(data->priv, kbuf, kbuf_len, klen);
}

static struct hwkey_opaque_handle_data unittest_opaque_derived_data = {
        .allowed_uuids = unittest_allowed_opaque_key_uuids,
        .allowed_uuids_len = countof(unittest_allowed_opaque_key_uuids),
        .retriever = get_derived_key_opaque,
        .priv = &hwcrypto_unittest_derived_data,
};

#endif /* WITH_HWCRYPTO_UNITTEST */

/*
 *  RPMB Key support
 */
#define RPMB_SS_AUTH_KEY_SIZE 32
#define RPMB_SS_AUTH_KEY_ID "com.android.trusty.storage_auth.rpmb"

/* Secure storage service app uuid */
static const uuid_t ss_uuid = SECURE_STORAGE_SERVER_APP_UUID;

static uint32_t get_rpmb_ss_auth_key(const struct hwkey_keyslot* slot,
                                     uint8_t* kbuf,
                                     size_t kbuf_len,
									 size_t* klen) {
    assert(kbuf);
    assert(klen);

    if (kbuf_len < RPMB_SS_AUTH_KEY_SIZE) {
        return HWKEY_ERR_BAD_LEN;
    }

    *klen = RPMB_SS_AUTH_KEY_SIZE;

	int ret = 0;
	hwpsa_session_t session;
	psa_key_id_t rpmb_key_id_local = TFM_BUILTIN_KEY_ID_TRUSTY_RPMB;

	session = hwpsa_open();

	if (psa_export_key_ss(session, rpmb_key_id_local,
			   exported_deriv_data,
			   sizeof(exported_deriv_data),
			   &exported_deriv_data_size)
	    != PSA_SUCCESS) {
		ret = HWKEY_ERR_GENERIC;
		goto rpmb_abort;
	}

	memcpy(kbuf, exported_deriv_data, *klen);

rpmb_abort :

	hwpsa_close(session);

	return ret;
}
/*
 * Keymint KAK support
 */
#define KM_KAK_SIZE 32
/* TODO import this constant from KM TA when build support is ready */
#define KM_KAK_ID "com.android.trusty.keymint.kak"

/* KM app uuid */
static const uuid_t km_uuid = KM_APP_UUID;

/* KM rust app uuid */
static const uuid_t km_rust_uuid = KM_RUST_APP_UUID;

#if TEST_BUILD
/* KM rust unit test uuid */
static const uuid_t km_rust_unittest_uuid = KM_RUST_UNITTEST_UUID;

/* HWCrypto HAL rust unit test uuid */
static const uuid_t hwcryptohal_rust_unittest_uuid =
        HWCRYPTOHAL_UNITTEST_RUST_APP_UUID;
#endif

/* HWCrypto HAL rust unit test uuid */
static const uuid_t hwcryptohal_rust_uuid = HWCRYPTOHAL_RUST_APP_UUID;

static uint8_t kak_salt[KM_KAK_SIZE] = {
        0x70, 0xc4, 0x7c, 0xfa, 0x2c, 0xb1, 0xee, 0xdc, 0xa5, 0xdf, 0xbc,
        0x8d, 0xd4, 0xf7, 0x0d, 0x42, 0x93, 0x3b, 0x7f, 0x7b, 0xc2, 0x9e,
        0x6d, 0xa5, 0xb2, 0x92, 0x7a, 0x21, 0x8e, 0xc9, 0xe6, 0x9a,
};

/*
 * This should be replaced with a device-specific implementation such that
 * any Strongbox on the device will have the same KAK.
 */
static uint32_t get_km_kak_key(const struct hwkey_keyslot* slot,
                               uint8_t* kbuf,
                               size_t kbuf_len,
                               size_t* klen) {
    assert(kbuf);
    assert(klen);

    if (kbuf_len < KM_KAK_SIZE) {
        return HWKEY_ERR_BAD_LEN;
    }
    *klen = KM_KAK_SIZE;

    return derive_key_v1(TFM_BUILTIN_KEY_ID_TRUSTY_DK, NULL, 0, (const uint8_t*)slot->uuid, sizeof(uuid_t),kak_salt, KM_KAK_SIZE, kbuf, *klen);
}

static const uuid_t hwaes_uuid = SAMPLE_HWAES_APP_UUID;

#if WITH_HWCRYPTO_UNITTEST
static const uuid_t hwaes_unittest_uuid = HWAES_UNITTEST_APP_UUID;
static const uuid_t hwaes_bench_uuid = HWAES_BENCH_APP_UUID;

static const uuid_t* hwaes_unittest_allowed_opaque_key_uuids[] = {
        &hwaes_uuid,
};

static struct hwkey_opaque_handle_data hwaes_unittest_opaque_handle_data = {
        .allowed_uuids = hwaes_unittest_allowed_opaque_key_uuids,
        .allowed_uuids_len = countof(hwaes_unittest_allowed_opaque_key_uuids),
        .retriever = get_unittest_key32_opaque,
};
#endif

/*
 * Apploader key(s)
 */
struct apploader_key {
    const uint8_t* key_data;

    // Pointer to the symbol holding the size of the key.
    // This needs to be a pointer because the size is not a
    // constant known to the compiler at compile time,
    // so it cannot be used to initialize the field directly.
    const unsigned int* key_size_ptr;
};

#define INCLUDE_APPLOADER_KEY(key, key_file)   \
    INCFILE(key##_data, key##_size, key_file); \
    static struct apploader_key key = {        \
            .key_data = key##_data,            \
            .key_size_ptr = &key##_size,       \
    };

#undef APPLOADER_HAS_SIGNING_KEYS
#undef APPLOADER_HAS_ENCRYPTION_KEYS

#ifdef APPLOADER_SIGN_PUBLIC_KEY_0_FILE
INCLUDE_APPLOADER_KEY(apploader_sign_key_0, APPLOADER_SIGN_PUBLIC_KEY_0_FILE);
#define APPLOADER_SIGN_KEY_0 "com.android.trusty.apploader.sign.key.0"
#define APPLOADER_HAS_SIGNING_KEYS
#ifdef APPLOADER_SIGN_KEY_0_UNLOCKED_ONLY
#define APPLOADER_SIGN_KEY_0_HANDLER get_apploader_sign_unlocked_key
#else
#define APPLOADER_SIGN_KEY_0_HANDLER get_apploader_sign_key
#endif
#endif

#ifdef APPLOADER_SIGN_PUBLIC_KEY_1_FILE
INCLUDE_APPLOADER_KEY(apploader_sign_key_1, APPLOADER_SIGN_PUBLIC_KEY_1_FILE);
#define APPLOADER_SIGN_KEY_1 "com.android.trusty.apploader.sign.key.1"
#define APPLOADER_HAS_SIGNING_KEYS
#ifdef APPLOADER_SIGN_KEY_1_UNLOCKED_ONLY
#define APPLOADER_SIGN_KEY_1_HANDLER get_apploader_sign_unlocked_key
#else
/*
 * Rather than rely on a correct build configuration, a real hwkey
 * implementation should ensure that dev signing keys are not allowed in
 * unlocked state by either hard-coding dev key slot handlers to a handler that
 * checks the unlock state or by erroring out here if the build configuration is
 * unexpected.
 */
#pragma message "Apploader signing key 1 is not gated on unlock status"
#define APPLOADER_SIGN_KEY_1_HANDLER get_apploader_sign_key
#endif
#endif

#ifdef APPLOADER_ENCRYPT_KEY_0_FILE
INCLUDE_APPLOADER_KEY(apploader_encrypt_key_0, APPLOADER_ENCRYPT_KEY_0_FILE);
#define APPLOADER_ENCRYPT_KEY_0 "com.android.trusty.apploader.encrypt.key.0"
#define APPLOADER_HAS_ENCRYPTION_KEYS
#endif

#ifdef APPLOADER_ENCRYPT_KEY_1_FILE
INCLUDE_APPLOADER_KEY(apploader_encrypt_key_1, APPLOADER_ENCRYPT_KEY_1_FILE);
#define APPLOADER_ENCRYPT_KEY_1 "com.android.trusty.apploader.encrypt.key.1"
#define APPLOADER_HAS_ENCRYPTION_KEYS
#endif

#if defined(APPLOADER_HAS_SIGNING_KEYS) || \
        defined(APPLOADER_HAS_ENCRYPTION_KEYS)
/* Apploader app uuid */
static const uuid_t apploader_uuid = APPLOADER_APP_UUID;

static uint32_t get_apploader_key(const struct apploader_key* key,
                                  uint8_t* kbuf,
                                  size_t kbuf_len,
                                  size_t* klen) {
    assert(kbuf);
    assert(klen);
    assert(key);
    assert(key->key_size_ptr);

    size_t key_size = (size_t)*key->key_size_ptr;
    assert(kbuf_len >= key_size);

    memcpy(kbuf, key->key_data, key_size);
    *klen = key_size;

    return HWKEY_NO_ERROR;
}
#endif

#ifdef APPLOADER_HAS_SIGNING_KEYS
static uint32_t get_apploader_sign_key(const struct hwkey_keyslot* slot,
                                       uint8_t* kbuf,
                                       size_t kbuf_len,
                                       size_t* klen) {
    assert(slot);
    return get_apploader_key(slot->priv, kbuf, kbuf_len, klen);
}

/*
 * Retrieve the respective key only if the system state APP_LOADING_UNLOCKED
 * flag is true
 */
static uint32_t get_apploader_sign_unlocked_key(
        const struct hwkey_keyslot* slot,
        uint8_t* kbuf,
        size_t kbuf_len,
        size_t* klen) {
    assert(slot);
    if (system_state_app_loading_unlocked()) {
        return get_apploader_key(slot->priv, kbuf, kbuf_len, klen);
    } else {
        return HWKEY_ERR_NOT_FOUND;
    }
}
#endif

#ifdef APPLOADER_HAS_ENCRYPTION_KEYS

static const uuid_t* apploader_allowed_opaque_key_uuids[] = {
        &hwaes_uuid,
};

/* Adapter to cast hwkey_opaque_handle_data.priv to struct apploader_key* */
static uint32_t get_apploader_key_opaque(
        const struct hwkey_opaque_handle_data* data,
        uint8_t* kbuf,
        size_t kbuf_len,
        size_t* klen) {
    assert(data);
    return get_apploader_key(data->priv, kbuf, kbuf_len, klen);
}

#ifdef APPLOADER_ENCRYPT_KEY_0
static struct hwkey_opaque_handle_data
        apploader_encrypt_key_0_opaque_handle_data = {
                .allowed_uuids = apploader_allowed_opaque_key_uuids,
                .allowed_uuids_len =
                        countof(apploader_allowed_opaque_key_uuids),
                .retriever = get_apploader_key_opaque,
                .priv = &apploader_encrypt_key_0,
};
#endif

#ifdef APPLOADER_ENCRYPT_KEY_1
static struct hwkey_opaque_handle_data
        apploader_encrypt_key_1_opaque_handle_data = {
                .allowed_uuids = apploader_allowed_opaque_key_uuids,
                .allowed_uuids_len =
                        countof(apploader_allowed_opaque_key_uuids),
                .retriever = get_apploader_key_opaque,
                .priv = &apploader_encrypt_key_1,
};
#endif

#endif

static const uuid_t gatekeeper_uuid = GATEKEEPER_APP_UUID;
static const uuid_t hwbcc_uuid = SAMPLE_HWBCC_APP_UUID;
static const uuid_t hwbcc_unittest_uuid = HWBCC_UNITTEST_APP_UUID;

/* Clients that are allowed to connect to this service */
static const uuid_t* allowed_clients[] = {
        /* Needs to derive keys and access keyslot RPMB_SS_AUTH_KEY_ID */
        &ss_uuid,
        /* Needs to derive keys and access keyslot KM_KAK_ID */
        &km_uuid,
        &km_rust_uuid,
#if TEST_BUILD
        &km_rust_unittest_uuid,
        /*HWCrypto HAL needs to derive key and access keylots*/
        &hwcryptohal_rust_unittest_uuid,
#endif
        &hwcryptohal_rust_uuid,
        /* Needs access to opaque keys */
        &hwaes_uuid,
        /* Needs to derive keys */
        &gatekeeper_uuid,

#if defined(APPLOADER_HAS_SIGNING_KEYS) || \
        defined(APPLOADER_HAS_ENCRYPTION_KEYS)
        /* Needs to access apploader keys */
        &apploader_uuid,
#endif

        /* Needs to derive keys even if it doesn't have test keyslots */
        &hwcrypto_unittest_uuid,
        &hwcrypto_unittest_rust_uuid,

#if WITH_HWCRYPTO_UNITTEST
        &hwaes_unittest_uuid,
        &hwaes_bench_uuid,
#endif
        /* Needs to derive keys */
        &hwbcc_uuid,
        /* Needs to derive keys */
        &hwbcc_unittest_uuid,
};

/*
 *  List of keys slots that hwkey service supports
 */
static const struct hwkey_keyslot _keys[] = {
        {
                .uuid = &ss_uuid,
                .key_id = RPMB_SS_AUTH_KEY_ID,
                .handler = get_rpmb_ss_auth_key,
        },
        {
                .uuid = &km_uuid,
                .key_id = KM_KAK_ID,
                .handler = get_km_kak_key,
        },
        {
                .uuid = &km_rust_uuid,
                .key_id = KM_KAK_ID,
                .handler = get_km_kak_key,
        },
#if TEST_BUILD
        {
                .uuid = &km_rust_unittest_uuid,
                .key_id = KM_KAK_ID,
                .handler = get_km_kak_key,
        },
#endif
#ifdef APPLOADER_SIGN_KEY_0
        {
                .uuid = &apploader_uuid,
                .key_id = APPLOADER_SIGN_KEY_0,
                .handler = APPLOADER_SIGN_KEY_0_HANDLER,
                .priv = &apploader_sign_key_0,
        },
#endif
#ifdef APPLOADER_SIGN_KEY_1
        {
                .uuid = &apploader_uuid,
                .key_id = APPLOADER_SIGN_KEY_1,
                .handler = APPLOADER_SIGN_KEY_1_HANDLER,
                .priv = &apploader_sign_key_1,
        },
#endif
#ifdef APPLOADER_ENCRYPT_KEY_0
        {
                .uuid = &apploader_uuid,
                .key_id = APPLOADER_ENCRYPT_KEY_0,
                .handler = get_key_handle,
                .priv = &apploader_encrypt_key_0_opaque_handle_data,
        },
#endif
#ifdef APPLOADER_ENCRYPT_KEY_1
        {
                .uuid = &apploader_uuid,
                .key_id = APPLOADER_ENCRYPT_KEY_1,
                .handler = get_key_handle,
                .priv = &apploader_encrypt_key_1_opaque_handle_data,
        },
#endif

#if WITH_HWCRYPTO_UNITTEST
        {
                .uuid = &hwcrypto_unittest_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.key32",
                .handler = get_unittest_key32_handler,
        },
        {
                .uuid = &hwcrypto_unittest_rust_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.key32",
                .handler = get_unittest_key32_handler,
        },
        {
                .uuid = &hwcrypto_unittest_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.derived_key32",
                .priv = &hwcrypto_unittest_derived_data,
                .handler = derived_keyslot_handler,
        },
        {
                .uuid = &hwcrypto_unittest_rust_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.derived_key32",
                .priv = &hwcrypto_unittest_derived_data,
                .handler = derived_keyslot_handler,
        },
        {
                .uuid = &hwcrypto_unittest_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.opaque_handle",
                .handler = get_key_handle,
                .priv = &unittest_opaque_handle_data,
        },
        {
                .uuid = &hwcrypto_unittest_rust_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.opaque_handle",
                .handler = get_key_handle,
                .priv = &unittest_opaque_handle_data,
        },
        {
                .uuid = &hwcrypto_unittest_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.opaque_handle2",
                .handler = get_key_handle,
                .priv = &unittest_opaque_handle_data2,
        },
        {
                .uuid = &hwcrypto_unittest_rust_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.opaque_handle2",
                .handler = get_key_handle,
                .priv = &unittest_opaque_handle_data2,
        },
        {
                .uuid = &hwcrypto_unittest_uuid,
                .key_id =
                        "com.android.trusty.hwcrypto.unittest.opaque_handle_noaccess",
                .handler = get_key_handle,
                .priv = &unittest_opaque_handle_data_noaccess,
        },
        {
                .uuid = &hwcrypto_unittest_rust_uuid,
                .key_id =
                        "com.android.trusty.hwcrypto.unittest.opaque_handle_noaccess",
                .handler = get_key_handle,
                .priv = &unittest_opaque_handle_data_noaccess,
        },
        {
                .uuid = &hwcrypto_unittest_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.opaque_derived",
                .handler = get_key_handle,
                .priv = &unittest_opaque_derived_data,
        },
        {
                .uuid = &hwcrypto_unittest_rust_uuid,
                .key_id = "com.android.trusty.hwcrypto.unittest.opaque_derived",
                .handler = get_key_handle,
                .priv = &unittest_opaque_derived_data,
        },
        {
                .uuid = &hwaes_unittest_uuid,
                .key_id = "com.android.trusty.hwaes.unittest.opaque_handle",
                .handler = get_key_handle,
                .priv = &hwaes_unittest_opaque_handle_data,
        },
        {
                .uuid = &hwaes_bench_uuid,
                .key_id = "com.android.trusty.hwaes.unittest.opaque_handle",
                .handler = get_key_handle,
                .priv = &hwaes_unittest_opaque_handle_data,
        },
#endif /* WITH_HWCRYPTO_UNITTEST */
};

/*
 *  Run Self test
 */
static bool hwkey_self_test(void) {
    TLOGI("hwkey self test\n");

    if (!hkdf_self_test())
        return false;

    TLOGI("hwkey self test: PASSED\n");
    return true;
}

/*
 *  Initialize HWKEY service
 */
static int hwkey_start_service(struct tipc_hset* hset) {
    TLOGD("Start HWKEY service\n");

    static struct tipc_port_acl acl = {
            .flags = IPC_PORT_ALLOW_TA_CONNECT,
            .uuid_num = countof(allowed_clients),
            .uuids = allowed_clients,
    };

    static struct tipc_port port = {
            .name = HWKEY_PORT,
            .msg_max_size = HWKEY_MAX_MSG_SIZE,
            .msg_queue_len = 1,
            .acl = &acl,
    };

    static struct tipc_srv_ops ops = {
            .on_message = hwkey_chan_handle_msg,
            .on_connect = hwkey_chan_ctx_create,
            .on_channel_cleanup = hwkey_chan_ctx_close,
    };

    return tipc_add_service(hset, &port, 1, HWKEY_MAX_NUM_CHANNELS, &ops);
}

/*
 *  Initialize ST HWKEY service provider
 */
int hwkey_init_srv_provider(struct tipc_hset* hset) {
    int rc;

    TLOGE("Init STM HWKEY service provider\n");
 
    /* run self test */
    if (!hwkey_self_test()) {
        TLOGE("hwkey_self_test failed\n");
        abort();
    }

    /* install key handlers */
    hwkey_install_keys(_keys, countof(_keys));

    /* start service */
    rc = hwkey_start_service(hset);
    if (rc != NO_ERROR) {
        TLOGE("failed (%d) to start HWKEY service\n", rc);
    }

    return rc;
}
