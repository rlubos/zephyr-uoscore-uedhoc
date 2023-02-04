/*
   Copyright (c) 2021 Fraunhofer AISEC. See the COPYRIGHT
   file at the top-level directory of this distribution.

   Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
   http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
   <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
   option. This file may not be copied, modified, or distributed
   except according to those terms.
*/
#ifndef OKM_H
#define OKM_H

#include <stdint.h>

#include "hkdf_info.h"
#include "suites.h"

#include "common/oscore_edhoc_error.h"

/**
 * @brief   Derives output keying material.
 * 
 * @param   hash_alg HASH algorithm 
 * @param   prk pseudorandom key
 * @param   prk_len length of prk
 * @param   label predefined integer value
 * @param   context relevant only for MAC_2 and MAC_3
 * @param   context_len length of context
 * @param   okm_len length of okm
 * @param   okm ouput pointer
 */
enum err edhoc_kdf(
   enum hash_alg hash_alg, 
   const struct byte_array *prk,
	uint8_t label, 
   struct byte_array *context,
   struct byte_array *okm);

#endif
