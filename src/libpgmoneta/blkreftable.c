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

#include <art.h>
#include <blkreftable.h>
#include <pgmoneta.h>
#include <utils.h>
#include <wal.h>
#include <walfile/wal_reader.h>

static void generate_art_key_from_brt_key(block_ref_table_key brt_key, char** art_key);
static int brt_insert(block_ref_table* brt, block_ref_table_key key, block_ref_table_entry** brt_entry, bool* found);

static void brt_set_limit_block(block_ref_table_entry* entry, block_number limit_block);
static void brt_mark_block_modified(block_ref_table_entry* entry, enum fork_number forknum, block_number blocknum);

int
pgmoneta_brt_create_empty(block_ref_table** brt)
{
    block_ref_table* brtab = (block_ref_table*)malloc(sizeof(block_ref_table));
    if (pgmoneta_art_create(&brtab->table))
    {
        goto error;
    }
    *brt = brtab;
    return 0;
error:

    pgmoneta_art_destroy(brtab->table);
    free(brtab);
    return 1;
}

int
pgmoneta_brt_destroy(block_ref_table* brt)
{
    if (brt == NULL) return 0;

    pgmoneta_art_destroy(brt->table);
    free(brt);
    return 0;
}

int
pgmoneta_brt_set_limit_block(block_ref_table* brt, const struct rel_file_locator* rlocator, 
								enum fork_number forknum, block_number limit_block)
{
    block_ref_table_entry* brt_entry = NULL;
    block_ref_table_key key = {0};

    bool found = false;

    memcpy(&key.rlocator, rlocator, sizeof(struct rel_file_locator));
    key.forknum = forknum;

    /**
     * Find the entry in the table, if found, return its pointer
     * if not found, insert a new empty entry and return its pointer
     */
    if (brt_insert(brt, key, &brt_entry, &found))
    {
        goto error;
    }

    if (!found)
    {
        /* No existing entry, set the limit block for this relation block and initialize other fields */
        brt_entry->limit_block = limit_block;
        brt_entry->chunk_size = 0;
        brt_entry->chunk_size = NULL;
		brt_entry->chunk_usage = NULL;
		brt_entry->chunk_data = NULL;
        return 0;
    }
    
    brt_set_limit_block(brt_entry, limit_block);
    return 0;

error:
    return 1;
}

int
pgmoneta_brt_mark_block_modified(block_ref_table *brt, const struct rel_file_locator *rlocator, 
                                    enum fork_number forknum, block_number blknum)
{
    block_ref_table_entry* brt_entry = NULL;
    block_ref_table_key key = {0};

    bool found = false;

    memcpy(&key.rlocator, rlocator, sizeof(struct rel_file_locator));
    key.forknum = forknum;

    /**
     * Find the entry in the table, if found, return its pointer
     * if not found, insert a new empty entry and return its pointer
     */
    if (brt_insert(brt, key, &brt_entry, &found))
    {
        goto error;
    }

    if (!found)
	{
		/*
		 * We want to set the initial limit block value to something higher
		 * than any legal block number. InvalidBlockNumber fits the bill.
		 */
		brt_entry->limit_block = InvalidBlockNumber;
		brt_entry->nchunks = 0;
		brt_entry->chunk_size = NULL;
		brt_entry->chunk_usage = NULL;
		brt_entry->chunk_data = NULL;
	}

	brt_mark_block_modified(brt_entry, forknum, blknum);
    return 0;

error:
    return 1;
}

static void
generate_art_key_from_brt_key(block_ref_table_key brt_key, char** art_key)
{
    char* k = NULL;
    k = pgmoneta_append_int(k, brt_key.rlocator.spcOid);
    k = pgmoneta_append_char(k, '_');
    k = pgmoneta_append_int(k, brt_key.rlocator.dbOid);
    k = pgmoneta_append_char(k, '_');
    k = pgmoneta_append_int(k, brt_key.rlocator.relNumber);
    k = pgmoneta_append_char(k, '_');
    k = pgmoneta_append_int(k, brt_key.forknum);

    *art_key = k;
}

static int
brt_insert(block_ref_table* brt, block_ref_table_key key, block_ref_table_entry** brt_entry, bool* found)
{
    char* art_key = NULL;
    block_ref_table_entry* e = NULL;

    generate_art_key_from_brt_key(key, &art_key);

    if ((e = pgmoneta_art_search(brt->table, art_key)) != NULL)
    {
        *brt_entry = e;
        *found = true;
        return;
    }

    /* Create an empty entry and insert it into the table */
    e = (block_ref_table_entry*)malloc(sizeof(block_ref_table_entry));
    e->key = key;

    if (pgmoneta_art_insert(brt->table, art_key, (uintptr_t)e, ValueRef))
    {
        goto error;
    }
    *brt_entry = e;

    free(art_key);
    return 0;

error:
    free(art_key);
    return 1;
}

static void
brt_set_limit_block(block_ref_table_entry* entry, block_number limit_block)
{
	unsigned	chunkno;
	unsigned	limit_chunkno;
	unsigned	limit_chunkoffset;
	block_ref_table_chunk limit_chunk;

    /* do nothing if current limit_block is less than or equal to limit_block */
    if (entry->limit_block <= limit_block) return;

    entry->limit_block = limit_block;

    /* Now discard the chunks and blocks with block_number > limit_block */

    /* get the chunk and offset on which the limit_block resides */
    limit_chunk = limit_block / BLOCKS_PER_CHUNK;
    limit_chunkoffset = limit_block % BLOCKS_PER_CHUNK;

    if (limit_chunkno >= entry->nchunks) return; /* Safety check */

    /* Discard entire contents of any higher-numbered chunks. */
	for (chunkno = limit_chunkno + 1; chunkno < entry->nchunks; ++chunkno)
		entry->chunk_usage[chunkno] = 0;

    /* get actual chunk data */
    limit_chunk = entry->chunk_data[limit_chunkno];

    if (entry->chunk_usage[limit_chunkno] == MAX_ENTRIES_PER_CHUNK) /* bitmap representation */ 
    {
        unsigned	chunkoffset;
		for (chunkoffset = limit_chunkoffset; chunkoffset < BLOCKS_PER_CHUNK;
			 ++chunkoffset)
			limit_chunk[chunkoffset / BLOCKS_PER_ENTRY] &=
				~(1 << (chunkoffset % BLOCKS_PER_ENTRY));
	}
    else /* array representation */
    {
        unsigned	i,
					j = 0;

		/* It's an offset array. Filter out large offsets. */
		for (i = 0; i < entry->chunk_usage[limit_chunkno]; ++i)
		{
			if (limit_chunk[i] < limit_chunkoffset)
				limit_chunk[j++] = limit_chunk[i];
		}
		entry->chunk_usage[limit_chunkno] = j;
    }
}

static void
brt_mark_block_modified(block_ref_table_entry* entry, enum fork_number forknum, block_number blknum)
{
    unsigned	chunkno;
	unsigned	chunkoffset;
	unsigned	i;

	/* get the chunk and offset on which the modified block resides */
	chunkno = blknum / BLOCKS_PER_CHUNK;
	chunkoffset = blknum % BLOCKS_PER_CHUNK;

	/*
	 * If 'nchunks' isn't big enough for us to be able to represent the state
	 * of this block, we need to enlarge our arrays.
	 */
	if (chunkno >= entry->nchunks)
	{
		unsigned	max_chunks;
		unsigned	extra_chunks;

		/*
		 * New array size is a power of 2, at least 16, big enough so that
		 * chunkno will be a valid array index.
		 */
		max_chunks = MAX(16, entry->nchunks);
		while (max_chunks < chunkno + 1)
			max_chunks *= 2;
		extra_chunks = max_chunks - entry->nchunks;

		if (entry->nchunks == 0)
		{
			entry->chunk_size = (uint16_t*)malloc(sizeof(uint16_t) * max_chunks);
			entry->chunk_usage = (uint16_t*)malloc(sizeof(uint16_t) * max_chunks);
			entry->chunk_data = (block_ref_table_chunk*)malloc(sizeof(block_ref_table_chunk) * max_chunks);
		}
		else
		{
			entry->chunk_size = (uint16_t*)realloc(entry->chunk_size, sizeof(uint16_t)*max_chunks);
			memset(&entry->chunk_size[entry->nchunks], 0, extra_chunks * sizeof(uint16_t));
			entry->chunk_usage = (uint16_t*)realloc(entry->chunk_usage, sizeof(uint16_t) * max_chunks);
			memset(&entry->chunk_usage[entry->nchunks], 0, extra_chunks * sizeof(uint16_t));
			entry->chunk_data = (block_ref_table_chunk*)realloc(entry->chunk_data, sizeof(block_ref_table_chunk) * max_chunks);
			memset(&entry->chunk_data[entry->nchunks], 0, extra_chunks * sizeof(block_ref_table_chunk));
		}
		entry->nchunks = max_chunks;
	}

	/*
	 * If the chunk that covers this block number doesn't exist yet, create it
	 * as an array and add the appropriate offset to it. We make it pretty
	 * small initially, because there might only be 1 or a few block
	 * references in this chunk and we don't want to use up too much memory.
	 */
	if (entry->chunk_size[chunkno] == 0)
	{
		entry->chunk_data[chunkno] = (uint16_t*)malloc(sizeof(uint16_t) * INITIAL_ENTRIES_PER_CHUNK);
		entry->chunk_size[chunkno] = INITIAL_ENTRIES_PER_CHUNK;
		entry->chunk_data[chunkno][0] = chunkoffset;
		entry->chunk_usage[chunkno] = 1;
		return;
	}

	/*
	 * If the number of entries in this chunk is already maximum, it must be a
	 * bitmap. Just set the appropriate bit.
	 */
	if (entry->chunk_usage[chunkno] == MAX_ENTRIES_PER_CHUNK)
	{
		block_ref_table_chunk chunk = entry->chunk_data[chunkno];

		chunk[chunkoffset / BLOCKS_PER_ENTRY] |=
			1 << (chunkoffset % BLOCKS_PER_ENTRY);
		return;
	}

	/*
	 * There is an existing chunk and it's in array format. Let's find out
	 * whether it already has an entry for this block. If so, we do not need
	 * to do anything.
	 */
	for (i = 0; i < entry->chunk_usage[chunkno]; ++i)
	{
		if (entry->chunk_data[chunkno][i] == chunkoffset)
			return;
	}

	/*
	 * If the number of entries currently used is one less than the maximum,
	 * it's time to convert to bitmap format.
	 */
	if (entry->chunk_usage[chunkno] == MAX_ENTRIES_PER_CHUNK - 1)
	{
		block_ref_table_chunk newchunk;
		unsigned	j;

		/* Allocate a new chunk. */
		newchunk = (uint16_t*)malloc(MAX_ENTRIES_PER_CHUNK * sizeof(uint16_t));

		/* Set the bit for each existing entry. */
		for (j = 0; j < entry->chunk_usage[chunkno]; ++j)
		{
			unsigned	coff = entry->chunk_data[chunkno][j];

			newchunk[coff / BLOCKS_PER_ENTRY] |=
				1 << (coff % BLOCKS_PER_ENTRY);
		}

		/* Set the bit for the new entry. */
		newchunk[chunkoffset / BLOCKS_PER_ENTRY] |=
			1 << (chunkoffset % BLOCKS_PER_ENTRY);

		/* Swap the new chunk into place and update metadata. */
		free(entry->chunk_data[chunkno]);
		entry->chunk_data[chunkno] = newchunk;
		entry->chunk_size[chunkno] = MAX_ENTRIES_PER_CHUNK;
		entry->chunk_usage[chunkno] = MAX_ENTRIES_PER_CHUNK;
		return;
	}

	/*
	 * OK, we currently have an array, and we don't need to convert to a
	 * bitmap, but we do need to add a new element. If there's not enough
	 * room, we'll have to expand the array.
	 */
	if (entry->chunk_usage[chunkno] == entry->chunk_size[chunkno])
	{
		unsigned	newsize = entry->chunk_size[chunkno] * 2;
		entry->chunk_data[chunkno] = (uint16_t*)realloc(entry->chunk_data[chunkno], newsize * sizeof(uint16_t));
		entry->chunk_size[chunkno] = newsize;
	}

	/* Now we can add the new entry. */
	entry->chunk_data[chunkno][entry->chunk_usage[chunkno]] =
		chunkoffset;
	entry->chunk_usage[chunkno]++;
}