/*
 *  File: connector.h
 *  Author: Iztok Savnik
 *    
 *  Copyright (c) 2023-24, FAMNIT, University of Primorska
 */				        	  
 
#ifndef CONNECTOR_H
#define CONNECTOR_H

/* Global constants, types, ... */ 

/* Declaration of circular typedef references. */
//typedef struct set2_node set2_node;
/* Removed: kv store of arbitrarily objects ref by (void *) 18/7/24

/* A type kv_pair is a structure composed of two fields. */
typedef struct link {
  int key;           /* a key is an element of a set */
  void *val;         /* value is of type (void *) */
} link;

/* A connector is a structure composed of a sequence of key-value
   pairs sorted by keys. The length of a sequence and the index of the
   currently accessed key-value pair are included. */
typedef struct connector {
  int length;        // length of the array seq
  int last;          // inx of last occupied element
  int cursor;        // indx of the last pair 
  link *seq;         // sorted array of links 
} connector;

/*---------------------------- Exported functions ------------------------------
 */

extern connector* con_alloc();
extern boolean con_free( connector *sp );
extern boolean con_sort( connector *sp );
extern int     con_size( connector *sp );
extern void    con_print_keys( connector *sp, FILE *f );

extern boolean con_member( connector *sp, int key );
extern link*   con_lookup( connector *sp, int key );
extern boolean con_open( connector* sp );
extern boolean con_open_at( connector* sp, int key );
extern link*   con_peek( connector *sp );
extern link*   con_read( connector *sp );
extern link*   con_current( connector* sp );
extern link*   con_peek_prev( connector* sp );
extern link*   con_read_prev( connector* sp );
extern boolean con_eos( connector *sp );
extern boolean con_write( connector *sp, int key, void* val );
extern boolean con_insert( connector *sp, int key, void* val );

extern int  con_get_cursor( connector *sp );
extern void con_set_cursor( connector *sp, int cur );

/* Export keys into an integer buffer. Returns number of keys written (<= max). */
extern int con_export_keys( connector *sp, int *out_buf, int max );

#endif /* CONNECTOR_H */
