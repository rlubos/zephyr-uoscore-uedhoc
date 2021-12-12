/*
   Copyright (c) 2021 Fraunhofer AISEC. See the COPYRIGHT
   file at the top-level directory of this distribution.

   Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
   http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
   <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
   option. This file may not be copied, modified, or distributed
   except according to those terms.
*/

#include "../edhoc.h"
#include "../inc/associated_data_encode.h"
#include "../../common/inc/crypto_wrapper.h"
#include "../inc/err_msg.h"
#include "../../common/inc/oscore_edhoc_error.h"
#include "../inc/hkdf_info.h"
#include "../inc/memcpy_s.h"
#include "../inc/messages.h"
#include "../inc/okm.h"
#include "../inc/plaintext.h"
#include "../inc/print_util.h"
#include "../inc/prk.h"
#include "../inc/retrieve_cred.h"
#include "../inc/signature_or_mac_msg.h"
#include "../inc/suites.h"
#include "../inc/th.h"
#include "../inc/txrx_wrapper.h"
#include "../inc/c_x.h"
#include "../inc/ciphertext.h"
#include "../cbor/encode_message_1.h"
#include "../cbor/decode_message_2.h"
#include "../cbor/encode_message_3.h"
//#include "../cbor/decode_message_2_c_i.h"

/**
 * @brief   Parses message 2
 * @param   c initiator context
 * @param   msg2 pointer to a buffer containign message 2 
 * @param   msg2_len the length of the raw message
 * @param   msg2_struct pointer to a structure for the parsed message
 * @param   err_msg pointer to an error message structure
 */
static inline enum err msg2_parse(const struct edhoc_initiator_context *c,
				  uint8_t *msg2, uint32_t msg2_len,
				  uint8_t *g_y, uint64_t g_y_len,
				  struct c_x *c_r, uint8_t *ciphertext2,
				  uint64_t *ciphertext2_len)
{
	uint32_t decode_len = 0;
	struct m2 m;

	TRY_EXPECT(cbor_decode_m2(msg2, msg2_len, &m, &decode_len), true);
	TRY(_memcpy_s(g_y, g_y_len, m._m2_G_Y_CIPHERTEXT_2.value, g_y_len));
	PRINT_ARRAY("g_y", g_y, g_y_len);

	TRY(_memcpy_s(ciphertext2, *ciphertext2_len,
		      m._m2_G_Y_CIPHERTEXT_2.value + g_y_len,
		      m._m2_G_Y_CIPHERTEXT_2.len - g_y_len));

	*ciphertext2_len = m._m2_G_Y_CIPHERTEXT_2.len - g_y_len;
	PRINT_ARRAY("ciphertext2", ciphertext2, *ciphertext2_len);

	if (m._m2_C_R_choice == _m2_C_R_int) {
		TRY(c_x_set(INT, NULL, 0, m._m2_C_R_int, c_r));
		PRINTF("C_R is an int: %d\n", c_r->mem.c_x_int);
	} else {
		TRY(c_x_set(BSTR, m._m2_C_R_bstr.value, m._m2_C_R_bstr.len, 0,
			    c_r));
		PRINT_ARRAY("C_R_raw bstr", c_r->mem.c_x_bstr.ptr,
			    c_r->mem.c_x_bstr.len);
	}

	return ok;
}
/**
 * @brief   Encodes message 1
 * @param   c initiator context
 * @param   msg1 pointer to a buffer for holding the encoded message
 * @param   msg1_len length of the encoded message
 */
static inline enum err msg1_encode(const struct edhoc_initiator_context *c,
				   uint8_t *msg1, uint32_t *msg1_len)
{
	struct message_1 m1;

	/*METHOD_CORR*/
	m1._message_1_METHOD = c->method;

	/*SUITES_I*/
	if (c->suites_i.len == 1) {
		/* only one suite, encode into int */
		m1._message_1_SUITES_I_choice = _message_1_SUITES_I_int;
		m1._message_1_SUITES_I_int = c->suites_i.ptr[0];
	} else if (c->suites_i.len > 1) {
		/* more than one suites, encode into array */
		m1._message_1_SUITES_I_choice = _SUITES_I__suite;
		m1._SUITES_I__suite_suite_count = c->suites_i.len;
		for (uint32_t i = 0; i < c->suites_i.len; i++) {
			m1._SUITES_I__suite_suite[i] = c->suites_i.ptr[i];
		}
	}

	/* G_X ephemeral public key */
	m1._message_1_G_X.value = c->g_x.ptr;
	m1._message_1_G_X.len = c->g_x.len;

	/* C_I connection id, encoded as  bstr_identifier */
	if (c->c_i.type == INT) {
		m1._message_1_C_I_choice = _message_1_C_I_int;
		m1._message_1_C_I_int = c->c_i.mem.c_x_int;
	} else {
		m1._message_1_C_I_choice = _message_1_C_I_bstr;
		m1._message_1_C_I_bstr.value = c->c_i.mem.c_x_bstr.ptr;
		m1._message_1_C_I_bstr.len = c->c_i.mem.c_x_bstr.len;
	}

	if (c->ead_1.len != 0) {
		/* ead_1 unprotected opaque auxiliary data */
		m1._message_1_ead_1.value = c->ead_1.ptr;
		m1._message_1_ead_1.len = c->ead_1.len;
		m1._message_1_ead_1_present = true;
	} else {
		m1._message_1_ead_1_present = 0;
	}

	uint32_t payload_len_out;
	TRY_EXPECT(cbor_encode_message_1(msg1, *msg1_len, &m1,
					 &payload_len_out),
		   true);
	*msg1_len = payload_len_out;

	PRINT_ARRAY("message_1 (CBOR Sequence)", msg1, *msg1_len);
	return ok;
}

enum err edhoc_initiator_run(const struct edhoc_initiator_context *c,
			     struct other_party_cred *cred_r_array,
			     uint16_t num_cred_r, uint8_t *err_msg,
			     uint32_t *err_msg_len, uint8_t *ead_2,
			     uint64_t *ead_2_len, uint8_t *ead_4,
			     uint64_t *ead_4_len, uint8_t *prk_4x3m,
			     uint8_t prk_4x3m_len, uint8_t *th4,
			     uint8_t th4_len,
			     enum err (*tx)(uint8_t *data, uint32_t data_len),
			     enum err (*rx)(uint8_t *data, uint32_t *data_len))
{
	struct suite suite;
	bool static_dh_i = false, static_dh_r = false;
	TRY(get_suite((enum suite_label)c->suites_i.ptr[c->suites_i.len - 1],
		      &suite));
	TRY(authentication_type_get(c->method, &static_dh_i, &static_dh_r));

	uint8_t msg1[MSG_1_DEFAULT_SIZE];
	uint32_t msg1_len = sizeof(msg1);
	uint8_t msg2[MSG_2_DEFAULT_SIZE];
	uint32_t msg2_len = sizeof(msg2);
	uint8_t msg4[MSG_4_DEFAULT_SIZE];
	uint32_t msg4_len = sizeof(msg2);
	uint8_t g_y[G_Y_DEFAULT_SIZE];
	uint64_t g_y_len = get_ecdh_pk_len(suite.edhoc_ecdh);

	uint8_t c_r_buf[C_R_DEFAULT_SIZE];
	struct c_x c_r;
	c_x_init(&c_r, c_r_buf, sizeof(c_r_buf));

	uint8_t ciphertext2[CIPHERTEXT2_DEFAULT_SIZE];
	uint64_t ciphertext2_len = sizeof(ciphertext2);

	/********************* encode and send message 1***********************/

	TRY(msg1_encode(c, (uint8_t *)&msg1, &msg1_len));
	TRY(tx(msg1, msg1_len));

	/*******************receive and process message 2**********************/

	TRY(rx(msg2, &msg2_len));
	PRINT_ARRAY("message_2 (CBOR Sequence)", msg2, msg2_len);

	/* If an error message is received msg2_parse will return 
    error_message_received. If this hapens edhoc_initiator_run will return. Then
    the caller needs to examine SUITES_R in err_msg re-initialize the initiator 
    and call edhoc_initiator_run again*/
	TRY(msg2_parse(c, msg2, msg2_len, g_y, g_y_len, &c_r,
		       (uint8_t *)&ciphertext2, &ciphertext2_len));
	// if (r == error_message_received) {
	// 	/*provide the error message to the caller*/
	// 	r = _memcpy_s(err_msg, *err_msg_len, msg2, msg2_len);
	// 	if (r != ok) {
	// 		return r;
	// 	}
	// 	*err_msg_len = msg2_len;
	// 	return error_message_received;
	// }

	/*calculate the DH shared secret*/
	uint8_t g_xy[ECDH_SECRET_DEFAULT_SIZE];
	TRY(shared_secret_derive(suite.edhoc_ecdh, c->x.ptr, c->x.len, g_y,
				 g_y_len, g_xy));
	PRINT_ARRAY("G_XY (ECDH shared secret) ", g_xy, sizeof(g_xy));

	/*calculate th2*/
	uint8_t th2[SHA_DEFAULT_SIZE];
	TRY(th2_calculate(suite.edhoc_hash, msg1, msg1_len, g_y, g_y_len, &c_r,
			  th2));

	/*calculate PRK_2e*/
	uint8_t PRK_2e[PRK_DEFAULT_SIZE];
	TRY(hkdf_extract(suite.edhoc_hash, NULL, 0, g_xy, sizeof(g_xy),
			 PRK_2e));
	PRINT_ARRAY("PRK_2e", PRK_2e, sizeof(PRK_2e));

	uint8_t sign_or_mac[SGN_OR_MAC_DEFAULT_SIZE];
	uint32_t sign_or_mac_len = sizeof(sign_or_mac);
	uint8_t id_cred_r[ID_CRED_DEFAULT_SIZE];
	uint32_t id_cred_r_len = sizeof(id_cred_r);
	TRY(ciphertext_decrypt_split(
		CIPHERTEXT2, &suite, PRK_2e, sizeof(PRK_2e), th2, sizeof(th2),
		ciphertext2, ciphertext2_len, id_cred_r, &id_cred_r_len,
		sign_or_mac, &sign_or_mac_len, ead_2, (uint32_t *)ead_2_len));

	/*check the authenticity of the responder*/
	uint8_t cred_r[CRED_DEFAULT_SIZE];
	uint16_t cred_r_len = sizeof(cred_r);
	uint8_t pk[PK_DEFAULT_SIZE];
	uint16_t pk_len = sizeof(pk);
	uint8_t g_r[G_R_DEFAULT_SIZE];
	uint16_t g_r_len = sizeof(g_r);

	TRY(retrieve_cred(static_dh_r, cred_r_array, num_cred_r, id_cred_r,
			  id_cred_r_len, cred_r, &cred_r_len, pk, &pk_len, g_r,
			  &g_r_len));
	PRINT_ARRAY("CRED_R", cred_r, cred_r_len);
	PRINT_ARRAY("pk", pk, pk_len);
	PRINT_ARRAY("g_r", g_r, g_r_len);

	/*derive prk_3e2m*/
	uint8_t PRK_3e2m[PRK_DEFAULT_SIZE];
	TRY(prk_derive(static_dh_r, suite, PRK_2e, sizeof(PRK_2e), g_r, g_r_len,
		       c->x.ptr, c->x.len, PRK_3e2m));
	PRINT_ARRAY("prk_3e2m", PRK_3e2m, sizeof(PRK_3e2m));
	//todo why static_dh_r?
	TRY(signature_or_mac(VERIFY, static_dh_r, &suite, NULL, 0, pk, pk_len,
			     PRK_3e2m, sizeof(PRK_3e2m), th2, sizeof(th2),
			     id_cred_r, id_cred_r_len, cred_r, cred_r_len,
			     ead_2, *(uint32_t *)ead_2_len, "MAC_2",
			     sign_or_mac, &sign_or_mac_len));

	/********msg3 create and send**************************************/
	uint8_t th3[32];
	TRY(th3_calculate(suite.edhoc_hash, (uint8_t *)&th2, sizeof(th2),
			  ciphertext2, ciphertext2_len, th3));

	/*derive prk_4x3m*/
	TRY(prk_derive(static_dh_i, suite, (uint8_t *)&PRK_3e2m,
		       sizeof(PRK_3e2m), g_y, g_y_len, c->i.ptr, c->i.len,
		       prk_4x3m));
	PRINT_ARRAY("prk_4x3m", prk_4x3m, prk_4x3m_len);

	/*calculate Signature_or_MAC_3*/
	uint32_t sign_or_mac_3_len = get_signature_len(suite.edhoc_sign);
	uint8_t sign_or_mac_3[SIGNATURE_DEFAULT_SIZE];

	TRY(signature_or_mac(GENERATE, static_dh_i, &suite, c->sk_i.ptr,
			     c->sk_i.len, c->pk_i.ptr, c->pk_i.len, prk_4x3m,
			     prk_4x3m_len, th3, sizeof(th3), c->id_cred_i.ptr,
			     c->id_cred_i.len, c->cred_i.ptr, c->cred_i.len,
			     c->ead_3.ptr, c->ead_3.len, "MAC_3", sign_or_mac_3,
			     &sign_or_mac_3_len));

	uint8_t ciphertext_3[CIPHERTEXT3_DEFAULT_SIZE];
	uint32_t ciphertext_3_len = sizeof(ciphertext_3);
	TRY(ciphertext_gen(CIPHERTEXT3, &suite, c->id_cred_i.ptr,
			   c->id_cred_i.len, sign_or_mac_3, sign_or_mac_3_len,
			   c->ead_3.ptr, c->ead_3.len, PRK_3e2m,
			   sizeof(PRK_3e2m), th3, sizeof(th3), ciphertext_3,
			   &ciphertext_3_len));

	/*massage 3 create and send*/
	uint64_t msg3_len = ciphertext_3_len + 2;
	TRY(check_buffer_size(CIPHERTEXT3_DEFAULT_SIZE, msg3_len));
	uint8_t msg3[CIPHERTEXT3_DEFAULT_SIZE];

	TRY(encode_byte_string(ciphertext_3, ciphertext_3_len, msg3,
			       &msg3_len));
	PRINT_ARRAY("msg3", msg3, msg3_len);
	TRY(tx(msg3, msg3_len));

	/*TH4*/
	TRY(th4_calculate(suite.edhoc_hash, th3, sizeof(th3), ciphertext_3,
			  ciphertext_3_len, th4));

	/*******************receive and process message 4**********************/
	if (c->msg4) {
		TRY(rx(msg4, &msg4_len));
		PRINT_ARRAY("message_4 (CBOR Sequence)", msg4, msg4_len);

		uint8_t ciphertext_4[CIPHERTEXT4_DEFAULT_SIZE];
		uint32_t ciphertext_4_len = sizeof(ciphertext_4);
		TRY(decode_byte_string(msg4, msg4_len, ciphertext_4,
				       &ciphertext_4_len));
		PRINT_ARRAY("ciphertext_4", ciphertext_4, ciphertext_4_len);

		TRY(ciphertext_decrypt_split(
			CIPHERTEXT4, &suite, prk_4x3m, prk_4x3m_len, th4,
			th4_len, ciphertext_4, ciphertext_4_len, NULL, 0, NULL,
			0, ead_4, (uint32_t *)ead_4_len));
	}
	return ok;
}
