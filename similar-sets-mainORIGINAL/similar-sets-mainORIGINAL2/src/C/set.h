/*
 * File: set.h
 * 
 * Copyright (c) 2023, FAMNIT, University of Primorska
 * Author: Iztok Savnik
*/				        	  
 
#ifndef SET_H
#define SET_H

/* Global constants, types, ... */ 

/*
  A set is a structure composed of the length of a set, the index of
  the last element in set, the index of the current element in the
  set, and a sorted array of integer numbers. The current element,
  thogether with the functions set_open(), set_read(), set_write() and
  set_eos(), implements a file-like access to sets.
*/
typedef struct set {
  int length;
  int last;
  int cursor ;
  int *arr;
} set;

/* Exported functions */

extern set*    set_alloc();
extern boolean set_free( set *sp );
extern int     set_size( set *sp );

extern boolean set_member( set* sp, int el );
extern void    set_sort( set* sp );

extern boolean set_open( set *sp );
extern boolean set_open_at( set *sp, int el );
extern boolean set_reset( set *sp );
extern int     set_peek( set *sp );
extern int     set_read( set *sp );
extern void    set_unread( set *sp, int n );
extern boolean set_write( set *sp, int el );
extern boolean set_insert( set *sp, int el );
extern boolean set_eos( set *sp );

extern boolean set_push( set *sp, int el );
extern int     set_pop( set *sp );
extern set*    set_copy( set *sp );
extern void    set_put( set *sp, int ix, int el );
extern int     set_get( set *sp, int ix );
extern void    set_print( FILE *f, set *sp );

extern int     set_get_cursor( set *sp );
extern void    set_restore_cursor( set *sp, int cur );

extern int     set_tl_size( set *sp );
extern void    set_tl_print( FILE *f, set *sp );
extern boolean set_tl_similar_lcs( set *sp, set *se, int *skp, int *add );
extern boolean set_tl_similar_hmg( set *sp, set *se, int *hmg );
extern boolean set_tl_similar_rev_hmg( set *sp, set *se, int *hmg );

#endif /* SET_H */
