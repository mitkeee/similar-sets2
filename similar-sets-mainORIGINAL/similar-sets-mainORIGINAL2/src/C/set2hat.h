/*--------------------------------------------------------------------------
 *
 * File: set2hat.h
 *    
 * Copyright (c) 2024, FAMNIT, University of Primorska
 *--------------------------------------------------------------------------
 */

#ifndef SET2HAT_H
#define SET2HAT_H

/*
A hat part of a set-trie. A set of sets is split into partitions on
the basis of the set length. To be able to obtain all similar sets for
a given set from a single partition, the sets that are either smaller
by a Hamming distance or less than the smallest set in a partition, or
larger by a Hamming distance or less than the largest set in a
partition, are also included.

At the top level the search for similars sets is based on the length
of the searched set. Partitioning is defined by a sorted sequence of
integer numbers representing the largest sets in subsequent
partitions. Each integer number has associated a link to the
corresponding set-trie. 
*/

/* A top node of a set-trie. Statistics of set lengths is generated in
   an array-based key-value store implemented in qesa. */
typedef struct set2_hat {
   qesa *stats;         // statistics of sets by size
   connector *tries;    // refs to set-tries defined for ranges
   int min;             // min set that goes through this node 
   int max;             // max set that goes through this node
} set2_hat;

/*---------------------- Exported functions ------------------------------*/

extern set2_hat* s2h_alloc();
extern void s2h_free( set2_hat *sh );

extern void s2h_insert( set2_hat *sh, set *se, int hmg );
extern void s2h_simsearch_hmg( set2_hat *sh, set *se, set *sp, int *hmg, qesa *qt );

extern set2_hat* s2h_load( FILE *f, int psize, int hmg );
extern void s2h_store( set2_hat *sh, FILE *f );

#endif /*SET2HAT_H*/
