/*
  File: skep.c
    
  Copyright (c) 2023, FAMNIT, University of Primorska
 */

#include <ctype.h>
#include <stdio.h>
#include <memory.h>
#include <malloc.h>
#include "config.h"
#include "sequence.h"
#include "queue.h"
#include "set2.h"
#include "skep.h"
 
/*
  A flexible in-memory key-value store *skep* for storing values
  based on keys is in set-trie used for linking nodes with their
  children nodes. *skep* reflects the requirements for the in-memory
  key-value store: efficient representation and management of small
  sets of key-value pairs (2-10) elements as well as very large sets
  of key-value pairs (1K-100M).

  The kv-store *skep* is a list of mem-blocks that include an array of
  sorted key-value pairs. The efficient access to a list of key-value
  mem-blocks is enabled by means of deterministic skip lists. The skip
  lists implement an index using solely the lists linking mem-blocks.
  
  Handling mem_blocks.

  Each mem_block includes an array of kv_pairs storing the keys in a
  given range (defined by min_key values of mem_blocks). The number of
  kv_pairs stored in a mem_block is limited by MEM_BLOCK_SIZE. Final
  version of the array of kv_pairs is sorted by the increasing value
  of keys. Intermediate instances of the array, used for fast
  insertion operation, may not be sorted.

  Handling skip lists of mem-blocks.

  Each mem-block includes a variable min_key storing the minimal value
  of keys from the given mem-block. The mem-blocks are organized in a
  linked list such that the keys of a given mem_block are larger than
  or equal to their min_key and smaller than the min_key of the next
  mem-block from the list.

  The hierarchy of skip lists are build on top of the base list of
  mem_blocks. The skip lists form a binary tree. Each mem_block
  includes an array named *skips* of length INX_HEIGHT. The index of
  the array *skips* represents the level of the binary tree
  implemented by skip lists of mem_blocks at that level. The index i
  represents the skip list with the skip distance 2^i. Therefore, the
  first level with index i=0 implements a skip list with a skipping
  distance 1. This level organizes mem-blocks into a simple list. The
  second skip list where i=1 has a skipping distance 2, the third skip
  list with i=2 has a skipping distance 4, then the next one 8, 16,
  etc.
 */

/* General functions that handle a *skep*. Functions allocate, free,
   open and close a *skep*. */

/*
  The function skep_alloc() allocates the space used for the
  representation of *skep* in the main memory. Initially *skep*
  includes only one mem-block.
 */
mem_block *skep_alloc( int size_skips, int size_memb ) {
}

/*
  The function skep_free() frees the memory space allocated for the
  set of key-value pairs stored in a given *skep*.
 */
void skep_free( mem_block *mb ) {
}

/*
  The function skep_open() opens a session with a given *skep*. The
  function returns an ID (a stack pointer) of an access-descriptor.
 */
int skep_open( mem_block *mb ) {
}

/*
  The function skep_close() closes a session with a given *skep*
  identified by an access descriptor ID.
 */
void skep_close( int ad ) {
}


/* Functions that work with *skep* in a single-block mode (*skep* has
   one block) via an identifier of an access descriptor (ad). */

/*
  Insert a kv_pair *pp in a sorted mem_block. The mem_block is sorted
  after the insertion.
 */
void memb_insert( int ad, kv_pair *pp ) {
}

/*
  Store a kv_pair to the end of the mem_block. The array elems can be
  temporary unordered. It is expected that the array is sorted after
  the loading process is over.
 */
void memb_insert_end( int ad, kv_pair *pp ) {
}

/*
  Delete a kv_pair with the key el from the mem_block. The mem_block
  is sorted after the operation delete.
 */
void memb_delete( int ad, int el ) {
}

/*
  Function returns true if a key el is found in the mem_block and
  false otherwise.
 */
boolean memb_member( int ad, int el ) {
}

/*
  Search for a given key el in a mem_block. If the key is found then
  return a pointer to a kv_pair with a key el.
 */
kv_pair *memb_read( int ad, int el ) {
}

/*
  Function returns the next kv_pair that follows the kv_pair obtained
  from the last operation which can be memb_member(), memb_read(), or
  memb_next().
 */
kv_pair *memb_next( int ad, int el ) {
}

/*
  Function updates a key-value pair identified by a key from *pp* with
  the new value.
 */
void memb_update( int ad, kv_pair *pp ) {
}

/*
  Function checks if the memory block *mb is a single memory block and
  returns a boolean value. Alternative is that the mem_block *mb is
  part of index organized with skip lists.
 */
boolean memb_single( mem_block *mb ) {
}

/*
  Checks if the memory block of a *skep* in a single mode is full. It
  returns a boolean value.
 */
boolean memb_full( mem_block *mb ) {
}

/*
  Sort the key-value pairs stored in a single-mode *skep*.
*/
void memb_sort( mem_block *md ) {
}


/* Functions that work with *skep* in a skip-lists mode (*skep* has
   an index based on deterministic skip lists) via an identifier of an
   access descriptor (ad). */

/*
  Function inserts a key-value pair *pp into a *skep* that is in a
  skip-lists mode.
 */
void skep_insert( int ad, kv_pair *pp ) {
}

/*
  We assume that the kv_pairs are provided in the increasing order of
  keys. The provided parameter *pp is added after the last record in
  skip lists of mem_blocks.
 */
void skip_insert_end( int ad, kv_pair *pp ) {
}

/*
 */
void skep_delete( int ad, int el ) {
}

/*
  Search for a given key in a *skep*. Function returns true if key is
  found and false otherwise. 
 */
boolean skep_member( int ad, int el ) {
}

/*
  Search for a given key el in a *skep*. If the key is found then
  return a pointer to the selected kv_pair (with a key el).
 */
kv_pair *skep_read( int ad, int el ) {
}

/*
  Function returns the next kv_pair that follows the kv_pair obtained
  from the last operation which can be skep_member(), skep_read(), or
  skep_next().
 */
kv_pair *skep_next( int ad ) {
}

/*
  Function updates a key-value pair identified by a key from the
  kv-pair *pp with the new value (from *pp).
 */
void skep_update( int ad, kv_pair *pp ) {
}

/*
  Assume that the data in mem_blocks are ordered by the increasing
  value of the key, and the blocks are ordered by the increasing value
  of min_key. Bulid the hierarchy of skipping lists for the given
  data.
 */
void skep_rebuild( mem_block *sl, int el, s2_node *val ) {
}


  
 
