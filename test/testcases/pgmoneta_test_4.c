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
 *
 */

#include <tsclient.h>
#include <brt.h>
#include <wal.h>
#include <walfile/wal_reader.h>
#include <art.h>

#include "pgmoneta_test_4.h"

START_TEST(test_pgmoneta_single_chunk_brt_insert_array_representation)
{
    int found = 1;
    block_ref_table* brt;

    struct rel_file_locator *rlocator = NULL;
    rlocator = (struct rel_file_locator*)malloc(sizeof(struct rel_file_locator));
    rlocator->spcOid = 1663;
    rlocator->dbOid = 234;
    rlocator->relNumber = 345;
    enum fork_number frk = MAIN_FORKNUM;

    const struct rel_file_locator *crlocator = rlocator;

    /* Between 0 and 0xFFFFFFFE */
    block_number blk = 0x123;

    pgmoneta_brt_create_empty(&brt);
    pgmoneta_brt_mark_block_modified(brt, crlocator, frk, blk);
    for (int i=18;i>=0;i--)
    {
        printf("%d\n", i);
        int f = 0;
        f = !pgmoneta_brt_mark_block_modified(brt, crlocator, frk, blk + i);
        if (!f) {
            found = f;
        }
    }
    pgmoneta_brt_destroy(brt);
    free(rlocator);

    ck_assert_msg(found, "success status not found");
}
END_TEST

START_TEST(test_pgmoneta_single_chunk_brt_insert_switch_representation)
{
    int found = 1;
    block_ref_table* brt;

    struct rel_file_locator *rlocator = NULL;
    rlocator = (struct rel_file_locator*)malloc(sizeof(struct rel_file_locator));
    rlocator->spcOid = 1663;
    rlocator->dbOid = 234;
    rlocator->relNumber = 345;
    enum fork_number frk = MAIN_FORKNUM;

    const struct rel_file_locator *crlocator = rlocator;

    /* Between 0 and 0xFFFFFFFE */
    block_number blk = 0x123;

    pgmoneta_brt_create_empty(&brt);
    pgmoneta_brt_mark_block_modified(brt, crlocator, frk, blk);
    for (int i=MAX_ENTRIES_PER_CHUNK + 1;i>=0;i--)
    {
        printf("%d\n", i);
        int f = 0;
        f = !pgmoneta_brt_mark_block_modified(brt, crlocator, frk, blk + i);
        if (!f) {
            found = f;
            break;
        }
    }
    pgmoneta_brt_destroy(brt);
    free(rlocator);

    ck_assert_msg(found, "success status not found");
}
END_TEST

START_TEST(test_pgmoneta_multiple_chunk_brt_insert_switch_representation)
{
    int found = 1;
    block_ref_table* brt;

    struct rel_file_locator *rlocator = NULL;
    rlocator = (struct rel_file_locator*)malloc(sizeof(struct rel_file_locator));
    rlocator->spcOid = 1663;
    rlocator->dbOid = 234;
    rlocator->relNumber = 345;
    enum fork_number frk = MAIN_FORKNUM;

    const struct rel_file_locator *crlocator = rlocator;

    /* Between 0 and 0xFFFFFFFE */
    block_number blk1 = 0x123;
    block_number blk2 = 3*BLOCKS_PER_CHUNK + blk1;

    pgmoneta_brt_create_empty(&brt);
    pgmoneta_brt_mark_block_modified(brt, crlocator, frk, blk1);
    // Bitmap Representation
    for (int i=MAX_ENTRIES_PER_CHUNK + 1;i>=0;i--)
    {
        int f = 0;
        f = !pgmoneta_brt_mark_block_modified(brt, crlocator, frk, blk1 + i);
        if (!f) {
            found = f;
            break;
        }
    }

    // Array Representation
    for (int i=1000;i>=0;i--)
    {
        int f = 0;
        f = !pgmoneta_brt_mark_block_modified(brt, crlocator, frk, blk2 + i);
        if (!f) {
            found = f;
            break;
        }
    }

    pgmoneta_brt_destroy(brt);
    free(rlocator);

    ck_assert_msg(found, "success status not found");
}
END_TEST

Suite*
pgmoneta_test4_suite()
{
   Suite* s;
   TCase* tc_core;
   s = suite_create("pgmoneta_test4");

   tc_core = tcase_create("Core");

   tcase_set_timeout(tc_core, 60);
   tcase_add_test(tc_core, test_pgmoneta_multiple_chunk_brt_insert_switch_representation);
   suite_add_tcase(s, tc_core);

   return s;
}