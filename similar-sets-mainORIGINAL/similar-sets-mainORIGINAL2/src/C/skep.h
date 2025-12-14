/*
  File: skips.h
     
  Copyright (c) 2023, FAMNIT, University of Primorska
 */

#ifndef SKEP_H
#define SKEP_H

/*
  An in-memory key-value store *skep* is used to store and maintain
  links between the nodes and their children in a set-trie. The design
  objectives of the kv-store are as follows.
  
  - To minimize the space usage for the case of a very small kv-store
    and to have a minimal space overhead in the case a large number
    of pairs is stored.
  - In the case of a small kv data store, we use a simple sorted
    array of keys stored in the form of a memory block. The size of
    memory block is adaptable for different types of data.
  - In the case of a very large kv-store, we use large memory blocks
    that are linked by skip lists organizing data into fast
    hierarchical in-memory index.

  Each memory block includes a sorted array of key/value pairs
  (elems). In the context of a set-trie, the key can only be an
  element of a set that follows (in a sorted order) the element
  represented by a current node. The value represents the reference to
  the children s2_nodes of a given s2_node.

  Each memory block stores besides an array of kv_pairs also: 1) a
  minimal key from the block, and 2) an array of pointers (skips),
  both for the representation of the in-memory index. The skip lists
  of memory blocks implement a binary search tree where the leafs of a
  binary tree are the memory blocks.

  Two separated sets of functions is provided to allow either direct
  block access, if we have a single block, or index-based access, if
  we have a complete index built.
*/

/* A block of memory for storing links from a node to other set-trie nodes. */
typedef struct mem_block {
  int    min_key;                      /* min key in block; rest are >=! */
  struct kv_sequence seq; /* a kv-array linking a node to sub-tries */
  struct mem_block *skips[INX_HEIGHT]; /* skip lists */
} mem_block;

/* Access descriptor for working with the in-memory kv-store. */
/* The protocol for working with mem-blocks and skip lists is as
   follows. The access is first opened, obtaining a descriptor through
   which we can work with kv-store. Then follows a session including
   reading and inserting data from kv-store. The access is closed by
   calling the function skip_free() that also frees the space occupied
   by a descriptor. */
typedef struct access_descr {
  short int status;     /* status of acc descriptor: 0-closed;
			   1-opened; 2-error */
  short int mode;       /* *skep* mode can be either: 0 = single-mode
			   (single mem_block); 1 = skips-mode (using
			   skip lists) */
  mem_block *first_mbl; /* ptr to first mem_block of skip lists */
  mem_block *mblock;    /* ptr to the currently accessed mem_block */
  int key_inx;          /* current key of access session; the index of
			   the array elems for current operation. */
};

/* An array of access descriptors organized as a stack. */
/* Note: When traversing the tree, the descriptors can only be opened
   in a stack-wise manner. Therefore, the operations open/close
   push/pop access descriptors to/from the acd_stack. */
access_descr *ad_stack[ACD_ARR_SIZE];
int ad_sp;   /* current position of stack pointer */

/* Counters ... */

int memb_cnt;       /* number of mem_block instances */
int xxx_cnt;        /* number of xxx ... */

/* Global variables */

/* Exported functions */

extern mem_block *skep_alloc( int size_skips, int size_memb ); 
extern void skep_free( mem_block *mb ); 
extern int skep_open( mem_block *mb );
extern void skep_close( int ad );

extern void memb_insert( int ad, kv_pair *pp );
extern void memb_insert_end( int ad, kv_pair *pp ); 
extern void memb_delete( int ad, int el );
extern boolean memb_member( int ad, int el );
extern kv_pair *memb_read( int ad, int el );
extern kv_pair *memb_next( int ad, int el );
extern void memb_update( int ad, kv_pair *pp );

extern boolean memb_single( mem_block *mb );
extern boolean memb_full( mem_block *mb );
extern void memb_sort( mem_block *mb );

extern void skep_insert( int ad, kv_pair *pp );
extern void skep_insert_end( int ad, kv_pair *pp ); 
extern void skep_delete( int ad, int el );  
extern boolean skep_member( int ad, int el );  
extern kv_pair *skep_read( int ad, int el );
extern kv_pair *skep_next( int ad );
extern void skep_update( int ad, kv_pair *pp );

extern void skep_rebuild( mem_block *sl, int el, s2_node *val ); 

#endif /*SKEP_H*/
