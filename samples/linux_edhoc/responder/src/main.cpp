/*
   Copyright (c) 2021 Fraunhofer AISEC. See the COPYRIGHT
   file at the top-level directory of this distribution.

   Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
   http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
   <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
   option. This file may not be copied, modified, or distributed
   except according to those terms.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {
#include "edhoc.h"
#include "sock.h"
#include "edhoc_test_vectors_v14.h"
#include "edhoc_test_vectors_p256_v14.h"
}
#include "cantcoap.h"

#define USE_IPV4
//#define TEST_SUITE2_METHOD3
#define TEST_SUITE0_METHOD0

CoapPDU *txPDU = new CoapPDU();

char buffer[MAXLINE];
CoapPDU *rxPDU;

/*comment this out to use DH keys from the test vectors*/
//#define USE_RANDOM_EPHEMERAL_DH_KEY

#ifdef USE_IPV6
struct sockaddr_in6 client_addr;
#endif
#ifdef USE_IPV4
struct sockaddr_in client_addr;
#endif
socklen_t client_addr_len;

/**
 * @brief	Initializes socket for CoAP server.
 * @param	
 * @retval	error code
 */
static int start_coap_server(void)
{
	int err;
#ifdef USE_IPV4
	struct sockaddr_in servaddr;
	//struct sockaddr_in client_addr;
	client_addr_len = sizeof(client_addr);
	memset(&client_addr, 0, sizeof(client_addr));
	const char IPV4_SERVADDR[] = { "127.0.0.1" };
	err = sock_init(SOCK_SERVER, IPV4_SERVADDR, IPv4, &servaddr,
			sizeof(servaddr));
	if (err < 0) {
		printf("error during socket initialization (error code: %d)",
		       err);
		return -1;
	}
#endif
#ifdef USE_IPV6
	struct sockaddr_in6 servaddr;
	//struct sockaddr_in6 client_addr;
	client_addr_len = sizeof(client_addr);
	memset(&client_addr, 0, sizeof(client_addr));
	const char IPV6_SERVADDR[] = { "::1" };
	err = sock_init(SOCK_SERVER, IPV6_SERVADDR, IPv6, &servaddr,
			sizeof(servaddr));
	if (err < 0) {
		printf("error during socket initialization (error code: %d)",
		       err);
		return -1;
	}
#endif

	return 0;
}
/**
 * @brief	Sends CoAP packet over network.
 * @param	pdu pointer to CoAP packet
 * @retval	error code
 */
static int send_coap_reply(void *sock, CoapPDU *pdu)
{
	int r;

	r = sendto(*((int *)sock), pdu->getPDUPointer(), pdu->getPDULength(), 0,
		   (struct sockaddr *)&client_addr, client_addr_len);
	if (r < 0) {
		printf("Error: failed to send reply (Code: %d, ErrNo: %d)\n", r,
		       errno);
		return r;
	}

	printf("CoAP reply sent!\n");
	return 0;
}

enum err tx(void *sock, uint8_t *data, uint32_t data_len)
{
	txPDU->setCode(CoapPDU::COAP_CHANGED);
	txPDU->setPayload(data, data_len);
	send_coap_reply(sock, txPDU);
	return ok;
}

enum err rx(void *sock, uint8_t *data, uint32_t *data_len)
{
	int n;

	/* receive */
	client_addr_len = sizeof(client_addr);
	memset(&client_addr, 0, sizeof(client_addr));

	n = recvfrom(*((int *)sock), (char *)buffer, sizeof(buffer), 0,
		     (struct sockaddr *)&client_addr, &client_addr_len);
	if (n < 0) {
		printf("recv error");
	}

	rxPDU = new CoapPDU((uint8_t *)buffer, n);

	if (rxPDU->validate()) {
		rxPDU->printHuman();
	}

	PRINT_ARRAY("CoAP message", rxPDU->getPayloadPointer(),
		    rxPDU->getPayloadLength());

	uint32_t payload_len = rxPDU->getPayloadLength();
	if (*data_len >= payload_len) {
		memcpy(data, rxPDU->getPayloadPointer(), payload_len);
		*data_len = payload_len;
	} else {
		printf("insufficient space in buffer");
	}

	txPDU->reset();
	txPDU->setVersion(rxPDU->getVersion());
	txPDU->setMessageID(rxPDU->getMessageID());
	txPDU->setToken(rxPDU->getTokenPointer(), rxPDU->getTokenLength());

	if (rxPDU->getType() == CoapPDU::COAP_CONFIRMABLE) {
		txPDU->setType(CoapPDU::COAP_ACKNOWLEDGEMENT);
	} else {
		txPDU->setType(CoapPDU::COAP_NON_CONFIRMABLE);
	}

	delete rxPDU;
	return ok;
}

int main()
{
	uint8_t prk_exporter[32];
	uint8_t oscore_master_secret[16];
	uint8_t oscore_master_salt[8];

	/* edhoc declarations */
	uint8_t PRK_out[PRK_DEFAULT_SIZE];
	uint8_t err_msg[ERR_MSG_DEFAULT_SIZE];
	uint32_t err_msg_len = sizeof(err_msg);
	uint8_t ad_1[AD_DEFAULT_SIZE];
	uint32_t ad_1_len = sizeof(ad_1);
	uint8_t ad_3[AD_DEFAULT_SIZE];
	uint32_t ad_3_len = sizeof(ad_1);

	/* test vector inputs */
	// const uint8_t TEST_VEC_NUM = 1;
	uint16_t cred_num = 1;
	struct other_party_cred cred_i;
	struct edhoc_responder_context c_r;

	// uint8_t vec_num_i = TEST_VEC_NUM - 1;

#ifdef USE_RANDOM_EPHEMERAL_DH_KEY
	uint32_t seed;
	uint8_t G_Y_random[32];
	uint8_t Y_random[32];
	c_r.g_y.ptr = G_Y_random;
	c_r.g_y.len = sizeof(G_Y_random);
	c_r.y.ptr = Y_random;
	c_r.y.len = sizeof(Y_random);
#endif

	TRY_EXPECT(start_coap_server(), 0);

	c_r.sock = &sockfd;
#ifdef TEST_SUITE0_METHOD0
	c_r.suites_r.ptr = T1_SUITE_R;
	c_r.suites_r.len = sizeof(T1_SUITE_R);

	c_r.c_r.ptr = T1_C_R;
	c_r.c_r.len = sizeof(T1_C_R);

	c_r.y.ptr = T1_Y;
	c_r.y.len = sizeof(T1_Y);

	c_r.g_y.ptr = T1_G_Y;
	c_r.g_y.len = sizeof(T1_G_Y);

	c_r.sk_r.ptr = T1_SK_R;
	c_r.sk_r.len = sizeof(T1_SK_R);

	c_r.pk_r.ptr = T1_PK_R;
	c_r.pk_r.len = sizeof(T1_PK_R);

	c_r.id_cred_r.ptr = T1_ID_CRED_R;
	c_r.id_cred_r.len = sizeof(T1_ID_CRED_R);

	c_r.cred_r.ptr = T1_CRED_R;
	c_r.cred_r.len = sizeof(T1_CRED_R);

	c_r.ead_2.ptr = T1_EAD_2;
	c_r.ead_2.len = sizeof(T1_EAD_2);

	c_r.ead_4.ptr = T1_EAD_4;
	c_r.ead_4.len = sizeof(T1_EAD_4);

	cred_i.id_cred.len = sizeof(T1_ID_CRED_I);
	cred_i.id_cred.ptr = T1_ID_CRED_I;
	cred_i.cred.len = sizeof(T1_CRED_I);
	cred_i.cred.ptr = T1_CRED_I;
	cred_i.pk.len = sizeof(T1_PK_I);
	cred_i.pk.ptr = T1_PK_I;
#endif
#ifdef TEST_SUITE2_METHOD3
	c_r.suites_r.ptr = T2_SUITE_R;
	c_r.suites_r.len = sizeof(T2_SUITE_R);

	c_r.c_r.ptr = T2_C_R;
	c_r.c_r.len = sizeof(T2_C_R);

	c_r.y.ptr = T2_Y;
	c_r.y.len = sizeof(T2_Y);

	c_r.g_y.ptr = T2_G_Y;
	c_r.g_y.len = sizeof(T2_G_Y);

	c_r.g_r.ptr = T2_G_R;
	c_r.g_r.len = sizeof(T2_G_R);

	c_r.r.ptr = T2_R;
	c_r.r.len = sizeof(T2_R);

	c_r.id_cred_r.ptr = T2_ID_CRED_R;
	c_r.id_cred_r.len = sizeof(T2_ID_CRED_R);

	c_r.cred_r.ptr = T2_CRED_R;
	c_r.cred_r.len = sizeof(T2_CRED_R);

	c_r.ead_2.ptr = T2_EAD_2;
	c_r.ead_2.len = sizeof(T2_EAD_2);

	c_r.ead_4.ptr = T2_EAD_4;
	c_r.ead_4.len = sizeof(T2_EAD_4);

	cred_i.id_cred.len = sizeof(T2_ID_CRED_I);
	cred_i.id_cred.ptr = T2_ID_CRED_I;
	cred_i.cred.len = sizeof(T2_CRED_I);
	cred_i.cred.ptr = T2_CRED_I;
	cred_i.g.len = sizeof(T2_G_I);
	cred_i.g.ptr = T2_G_I;
#endif
	// if (test_vectors[vec_num_i].c_r_raw != NULL) {
	// 	c_r.c_r.type = BSTR;
	// 	c_r.c_r.mem.c_x_bstr.len = test_vectors[vec_num_i].c_r_raw_len;
	// 	c_r.c_r.mem.c_x_bstr.ptr =
	// 		(uint8_t *)test_vectors[vec_num_i].c_r_raw;
	// } else {
	// 	c_r.c_r.type = INT;
	// 	c_r.c_r.mem.c_x_int = *test_vectors[vec_num_i].c_r_raw_int;
	// }
	// c_r.msg4 = true; /*we allways test message 4 */
	// c_r.suites_r.len = test_vectors[vec_num_i].suites_r_len;
	// c_r.suites_r.ptr = (uint8_t *)test_vectors[vec_num_i].suites_r;
	// c_r.ead_2.len = test_vectors[vec_num_i].ead_2_len;
	// c_r.ead_2.ptr = (uint8_t *)test_vectors[vec_num_i].ead_2;
	// c_r.ead_4.len = test_vectors[vec_num_i].ead_4_len;
	// c_r.ead_4.ptr = (uint8_t *)test_vectors[vec_num_i].ead_4;
	// c_r.id_cred_r.len = test_vectors[vec_num_i].id_cred_r_len;
	// c_r.id_cred_r.ptr = (uint8_t *)test_vectors[vec_num_i].id_cred_r;
	// c_r.cred_r.len = test_vectors[vec_num_i].cred_r_len;
	// c_r.cred_r.ptr = (uint8_t *)test_vectors[vec_num_i].cred_r;
	// c_r.g_y.len = test_vectors[vec_num_i].g_y_raw_len;
	// c_r.g_y.ptr = (uint8_t *)test_vectors[vec_num_i].g_y_raw;
	// c_r.y.len = test_vectors[vec_num_i].y_raw_len;
	// c_r.y.ptr = (uint8_t *)test_vectors[vec_num_i].y_raw;
	// c_r.g_r.len = test_vectors[vec_num_i].g_r_raw_len;
	// c_r.g_r.ptr = (uint8_t *)test_vectors[vec_num_i].g_r_raw;
	// c_r.r.len = test_vectors[vec_num_i].r_raw_len;
	// c_r.r.ptr = (uint8_t *)test_vectors[vec_num_i].r_raw;
	// c_r.sk_r.len = test_vectors[vec_num_i].sk_r_raw_len;
	// c_r.sk_r.ptr = (uint8_t *)test_vectors[vec_num_i].sk_r_raw;
	// c_r.pk_r.len = test_vectors[vec_num_i].pk_r_raw_len;
	// c_r.pk_r.ptr = (uint8_t *)test_vectors[vec_num_i].pk_r_raw;

	while (1) {
#ifdef USE_RANDOM_EPHEMERAL_DH_KEY
		/*create ephemeral DH keys from seed*/
		/*create a random seed*/
		FILE *fp;
		fp = fopen("/dev/urandom", "r");
		uint32_t G_Y_random_len = sizeof(G_Y_random);
		uint64_t seed_len =
			fread((uint8_t *)&seed, 1, sizeof(seed), fp);
		fclose(fp);
		PRINT_ARRAY("seed", (uint8_t *)&seed, seed_len);

		TRY(ephemeral_dh_key_gen(X25519, seed, Y_random, G_Y_random,
					 &G_Y_random_len));
		PRINT_ARRAY("secret ephemeral DH key", c_r.g_y.ptr,
			    c_r.g_y.len);
		PRINT_ARRAY("public ephemeral DH key", c_r.y.ptr, c_r.y.len);
#endif
		TRY(edhoc_responder_run(&c_r, &cred_i, cred_num, err_msg,
					&err_msg_len, (uint8_t *)&ad_1,
					&ad_1_len, (uint8_t *)&ad_3, &ad_3_len,
					PRK_out, sizeof(PRK_out), tx, rx));
		PRINT_ARRAY("PRK_out", PRK_out, sizeof(PRK_out));

		TRY(prk_out2exporter(SHA_256, PRK_out, sizeof(PRK_out),
				     prk_exporter));
		PRINT_ARRAY("prk_exporter", prk_exporter, sizeof(prk_exporter));

		TRY(edhoc_exporter(SHA_256, OSCORE_MASTER_SECRET, prk_exporter,
				   sizeof(prk_exporter), oscore_master_secret,
				   sizeof(oscore_master_secret)));
		PRINT_ARRAY("OSCORE Master Secret", oscore_master_secret,
			    sizeof(oscore_master_secret));

		TRY(edhoc_exporter(SHA_256, OSCORE_MASTER_SALT, prk_exporter,
				   sizeof(prk_exporter), oscore_master_salt,
				   sizeof(oscore_master_salt)));
		PRINT_ARRAY("OSCORE Master Salt", oscore_master_salt,
			    sizeof(oscore_master_salt));
	}

	close(sockfd);
	return 0;
}
