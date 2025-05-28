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

#ifndef PGMONETA_BLKREFTABLE_H
#define PGMONETA_BLKREFTABLE_H

#include <pgmoneta.h>
#include <art.h>
#include <wal.h>
#include <walfile/wal_reader.h>

/**
 * A block reference table is used to keep track of which blocks have been modified by WAL records within a 
 * certain LSN range.
 *
 * For every relation fork, we track all the blocks that have been mentioned in the WAL 
 * (Write-Ahead Logging). Along with that, we also record a "limit block," which represents the smallest 
 * size (in blocks) that the relation has had during that range of WAL records. This limit block should 
 * be set to 0 if the relation fork was either created or deleted, or to the new size after a truncation 
 * has occurred.
 * 
 * We have to store the blocks that have been modified for each relation file, to make it a bit efficient
 * we have two different representations of each block table entry.
 * 
 * Firstly we will divide the relation into chunks of 2^16 blocks and choose between an array representation
 * if the number of modified block in a chunk are less and a bitmap representtaion if nearly all the blocks are modified
 * 
 * In array representation, we don't need to store the entire block number instead we store each block number as a 2-byte
 * offset from the start of the chunk.
 * 
 * These same basic representational choices are used both when a block reference table is stored in memory 
 * and when it is serialized to disk.
 * 
 */
#define BITS_PER_BYTE               8
#define BLOCKS_PER_CHUNK		    (1 << 16)
#define BLOCKS_PER_ENTRY		    (BITS_PER_BYTE * sizeof(uint16_t))
#define MAX_ENTRIES_PER_CHUNK	    (BLOCKS_PER_CHUNK / BLOCKS_PER_ENTRY)
#define INITIAL_ENTRIES_PER_CHUNK	16

typedef uint16_t *block_ref_table_chunk;

/**
 * A block reference table monitors and records the state of each fork separately.
 * The key is used to search for the block entry in the ART
 */
typedef struct block_ref_table_key
{
	struct rel_file_locator rlocator;
	enum fork_number	    forknum;
} block_ref_table_key;

/**
 * State for one relation fork.
 * 
 * 'rlocator' and 'forknum' identify the relation fork to which this entry
 * pertains.
 * 
 * 'limit_block' represents the smallest known size (in blocks) of a relation during the range of LSNs (log sequence numbers) that a specific block reference table covers.
 * - If the relation fork is either created or dropped, this value should be set to 0.
 * - If the relation is truncated, it should be set to the number of blocks remaining after the truncation.
 * 
 * 'nchunks' is the allocated length of each of the three arrays that follow. We can only represent the 
 * status of block numbers less than nchunks * BLOCKS_PER_CHUNK.
 * 
 * 'chunk_size' is an array storing the allocated size of each chunk.
 *
 * 'chunk_usage' is an array storing the number of elements used in each chunk. If that value is less 
 * than MAX_ENTRIES_PER_CHUNK, the corresponding chunk is used as an array; else the corresponding 
 * chunk is used as a bitmap. When used as a bitmap, the least significant bit of the first array element
 * is the status of the lowest-numbered block covered by this chunk.
 *
 * 'chunk_data' is the array of chunks.
 */
typedef struct block_ref_table_entry
{
	block_ref_table_key   key;
	block_number          limit_block;
	char		          status;
	uint32_t		      nchunks;
	uint16_t	          *chunk_size;
	uint16_t	          *chunk_usage;
	block_ref_table_chunk *chunk_data;
} block_ref_table_entry;

/**
 *  Collection of block reference table entries
 * 	The idea is to convert the 
 */
typedef struct block_ref_table {
    struct art* table;
} block_ref_table;

/****  BRT MANIPULATION APIs *****/

/**
 * Create an empty block reference table
 * @returns 0 if success, otherwise 1
 */
int
pgmoneta_brt_create_empty(block_ref_table** brt);

/**
 * Set the 'limit block' for a relation fork
 * Mark any modified block with equal or higher block number as unused
 * @param brt pointer to the block reference table
 * @param rlocator pointer to the relfilelocator for the relation fork
 * @param forknum the fork number of the relation fork
 * @param limit_block the block number to be set as limit block
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_brt_set_limit_block(block_ref_table* brt, const struct rel_file_locator* rlocator, 
								enum fork_number forknum, block_number limit_block);

/**
 * Mark a block in a given relation fork as known to have been modified.
 * @param brt pointer to the block reference table
 * @param rlocator pointer to the relfilelocator for the relation fork
 * @param forknum the fork number of the relation fork
 * @param blknum the block number to be set as modified/used
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_brt_mark_block_modified(block_ref_table *brtab, const struct rel_file_locator *rlocator, 
                                    enum fork_number forknum, block_number blknum);

/**
 * Get an entry from the block reference table
 * @param brt pointer to the block reference table
 * @param rlocator pointer to the relfilelocator for the relation fork
 * @param forknum the fork number of the relation fork
 * @param limit_block [out] set the limit_block from the value of entry
 * @return entry [out] The block reference entry if found, otherwise NULL
 */
block_ref_table_entry*
pgmoneta_brt_get_entry(block_ref_table *brtab, const struct rel_file_locator *rlocator,
					  enum fork_number forknum, block_number *limit_block);

/**
 * Get block numbers from a table entry.
 * @param entry pointer to the brt entry
 * @param start_blkno start processing from this block number
 * @param stop_blkno stop processing, beyond this block number
 * @param blocks [out] Block array which contain all the modified blocks in the brt entry between [start_blkno, stop_blkno)
 * @param nblocks The lower bound on the number of blocks that can be stored
 * @return The return value is the number of block numbers actually written.
 */
int
pgmoneta_brt_entry_get_blocks(block_ref_table_entry *entry, block_number start_blkno,
							block_number stop_blkno, block_number *blocks, int nblocks);

/**
 * Destroy the brt
 * @param brt The table to be destroyed
 */
int
pgmoneta_brt_destroy(block_ref_table* brt);

/**
 * Destroy the block entry (used as a callback)
 * @param entry The entry to be destroyed
 */
void
pgmoneta_brt_entry_destroy(block_ref_table_entry* entry);

/****  BRT SERIALIZATION APIs *****/

#endif