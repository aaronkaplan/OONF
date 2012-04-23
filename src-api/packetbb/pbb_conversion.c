/*
 * PacketBB handler library (see RFC 5444)
 * Copyright (c) 2010 Henning Rogge <hrogge@googlemail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org/git for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 */

#include <assert.h>

#include "common/common_types.h"
#include "packetbb/pbb_conversion.h"

uint8_t
pbb_encode_timetlv(uint32_t decoded) {
  uint32_t a, b;
  /*
   * t = (1 + a/8) * 2^b * 1000 / 1024
   *   = (1000 + 125 * a) * (2^b / 2^10)
   *   = (1000 + 125 * a) * 2 ^ (b-10)
   */

  assert (decoded >= PBB_TIMETLV_MIN && decoded <= PBB_TIMETLV_MAX);

  b = 10;
  if (decoded >= 1000) {
    /* this means b >= 10 */
    while (decoded > 1875) {
      decoded++;
      decoded >>= 1;
      b++;
    }
  }
  else { /* decoded < 1000 */
    /* b < 10 */
    while (decoded < 1000) {
      decoded <<= 1;
      b--;
    }
  }

  a = (decoded - 1000 + 124) / 125;
  return a + (b << 3);
}

uint32_t
pbb_decode_timetlv(uint8_t encoded) {
  /*
   * time-value := (1 + a/8) * 2^b * C
   * time-code := 8 * b + a
   */
  uint8_t a,b;

  assert (encoded > 0);

  a = encoded & 0x07;
  b = encoded >> 3;

  /*
   * C is 1000/1024 for us, because we calculate in ms
   *
   * t = (1 + a/8) * 2^b * 1000 / 1024
   *   = (1000 + 125 * a) * 2^b / 2^10
   *
   * Case 1: b <= 10
   *   = (1000 + 125 * a) >> (10 - b)
   *
   * Case 2: b > 10
   *   = (1000 + 125 * a) << (b - 10)
   */

  if (b <= 10) {
    return (1000 + 125 * a) >> (10 - b);
  }
  return (1000 + 125 * a) << (b - 10);
}