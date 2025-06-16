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

#include <pgmoneta.h>
#include <wal.h>
#include <walfile/walsummary.h>

int
pgmoneta_summarize_wal(int srv, uint64_t start_lsn, uint64_t end_lsn, bool exact, uint32_t timeline)
{
    /** Sanity Checks
     * - start and end lsn belongs to same timeline
     * 
     */
    struct timeline_history* tlis_history = NULL;
    struct timeline_history* curr_tli_history = NULL;
    uint64_t switch_lsn = 0;

    if (pgmoneta_get_timeline_history(srv, timeline, &tlis_history))
    {
        goto error;
    }
    // Sanity Check (1)
    curr_tli_history = tlis_history;
    while (curr_tli_history)
    {
        switch_lsn = ((uint64_t)curr_tli_history->switchpos_hi) << 32 | (uint64_t)curr_tli_history->switchpos_lo;
        if (!(start_lsn > switch_lsn && end_lsn > switch_lsn))
        {
            goto error;
        }
        curr_tli_history = curr_tli_history->next;
    }
    pgmoneta_free_timeline_history(tlis_history);

    

    return 0;
error:
    if (tlis_history)
    {
        pgmoneta_free_timeline_history(tlis_history);
    }
    return 1;
}

