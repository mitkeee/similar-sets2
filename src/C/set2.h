/*--------------------------------------------------------------------------
 *
 * File: set2.h
 *    
 * Copyright (c) 2023, FAMNIT, University of Primorska
 *--------------------------------------------------------------------------
 */

#ifndef SET2_H
#define SET2_H

/*
A set trie is a trie composed of nodes represented with a struct
s2_node. Conceptually, each s2_node includes a store of
element/pointer (key/value) pairs including elements that lead to
sub-tries (children nodes) of a given s2_node.

The links from a s2_node to its children s2_nodes are implemented with
skip lists of memory blocks. Each memory block includes a sorted array
of key/value pairs. The key can only be an element of a set that
follows (in a sorted order) the element represented b a current node.
The value represents the reference to the children s2_node-s.

The s2_node thus has 3 ...
*/

/* Declaration of circular typedef references. */
//typedef struct connector connector;
/* Removed: kv store of arbitrarily objects ref by (void *) 18/7/24
   
/* A node of a set-trie. */
typedef struct set2_node {
   boolean isset;    // path represents a set
   boolean istail;   // path is a prefix of a tail set 
   set *ndset;       // node set stored if isset
   union {
      connector *link; // reference to an instance of a kvstore 
      struct {
         set *set;    // reference to set; tail of a set sequence
  	 int cursor;  // saved tail cursor (since a set is in multiple
		      // tries)
      } tail;
   } sub;
   int min;   // min set that goes through this node 
   int max;   // max set that goes through this node
   int cnt;   // number of sets in trie with a given prefix */	
} set2_node;

/*---------------------- Exported functions ------------------------------*/

extern set2_node* set2_alloc();
extern void set2_free( set2_node *st );

extern void set2_insert( set2_node *st, set *se );
extern void set2_simsearch_lcs( set2_node *st, set *se, set *sp, int *skp, int *add, qesa *qt );
extern void set2_simsearch_hmg( set2_node *st, set *se, set *sp, int *hmg, qesa *qt );

extern set2_node* set2_load( FILE *f );
extern void set2_store( set2_node *st, FILE *f );

#endif /*SET2_H*/
