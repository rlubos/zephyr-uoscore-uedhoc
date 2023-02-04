/*
   Copyright (c) 2021 Fraunhofer AISEC. See the COPYRIGHT
   file at the top-level directory of this distribution.

   Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
   http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
   <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
   option. This file may not be copied, modified, or distributed
   except according to those terms.
*/
#ifndef SIGNATURE_OR_MAC_MSG_H
#define SIGNATURE_OR_MAC_MSG_H

#include <stdbool.h>

#include "suites.h"

#include "common/oscore_edhoc_error.h"

enum sgn_or_mac_op { VERIFY, GENERATE };

/**
 * @brief Computes or verify a signature or a mac
 * 
 * @param op VERIFY or GENERATE
 * @param static_dh true if static DH keys are used
 * @param suite the cipher suite
 * @param sk secret key
 * @param sk_len length of the secret key
 * @param pk public key 
 * @param pk_len the length of pk
 * @param prk pseudo random key to be used in key iv derivation 
 * @param prk_len length of prk
 * @param th transcript hash
 * @param th_len length of th
 * @param id_cred ID_CRED of the calling party
 * @param id_cred_len length of id_cred
 * @param cred CRED of the calling party
 * @param cred_len length of cred
 * @param ead external authorization data
 * @param ead_len external authorization data
 * @param mac_label MAC label, see figure 7 in the specification V15
 * @param signature_or_mac the computed signature or mac
 * @param signature_or_mac_len the length of signature_or_mac
 * @return enum err 
 */
enum err signature_or_mac(enum sgn_or_mac_op op, bool static_dh,
			  struct suite *suite, 
			  const struct byte_array *sk, 
			  const struct byte_array *pk, 
			  const struct byte_array *prk,
			  const struct byte_array *th, 
			  const struct byte_array *id_cred, 
			  const struct byte_array *cred, 
			  const struct byte_array *ead, 
			  enum info_label mac_label, 
			  struct byte_array *signature_or_mac);

#endif
