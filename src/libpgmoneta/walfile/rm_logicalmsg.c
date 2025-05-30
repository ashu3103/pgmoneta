/*
 * Copyright (C) 2025 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <utils.h>
#include <walfile/rm.h>
#include <walfile/rm_logicalmsg.h>
#include <walfile/wal_reader.h>

#include <assert.h>

char*
pgmoneta_wal_logicalmsg_desc(char* buf, struct decoded_xlog_record* record)
{
   char* rec = XLOG_REC_GET_DATA(record);
   uint8_t info = XLOG_REC_GET_INFO(record) & ~XLR_INFO_MASK;

   if (info == XLOG_LOGICAL_MESSAGE)
   {
      struct xl_logical_message* xlrec = (struct xl_logical_message*) rec;
      char* prefix = xlrec->message;
      char* message = xlrec->message + xlrec->prefix_size;
      char* sep = "";

      assert(prefix[xlrec->prefix_size - 1] == '\0');

      buf = pgmoneta_format_and_append(buf, "%s, prefix \"%s\"; payload (%zu bytes): ",
                                       xlrec->transactional ? "transactional" : "non-transactional",
                                       prefix, xlrec->message_size);
      /* Write message payload as a series of hex bytes */
      for (size_t cnt = 0; cnt < xlrec->message_size; cnt++)
      {
         buf = pgmoneta_format_and_append(buf, "%s%02X", sep, (unsigned char) message[cnt]);
         sep = " ";
      }
   }
   return buf;
}
