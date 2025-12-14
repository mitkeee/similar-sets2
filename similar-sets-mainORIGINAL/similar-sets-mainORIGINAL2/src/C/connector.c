/*
 * File: connector.c
 * Author: I.Savnik
 *
 * Description: A key value store where the key is an integer number
 * and a value is a pointer to 
 *
 * Copyright (c) 2023-24, FAMNIT, University of Primorska
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "config.h"
#include "set.h"
//#include "qesa.h"
//#include "set2.h"
#include "connector.h"

/* Local variables */

/*
  Creating a new sequence on heap.
*/
connector *con_alloc()
{
   connector *sp = (connector *)malloc(sizeof(connector));
   if (sp == NULL) {
      printf("error: (con_create) mealloc failed.\n");
      return NULL;
   }

   sp->length = INIT_CONNECT_SIZE;
   sp->last = -1;
   sp->cursor = -1;
   sp->seq = (link *)malloc(sp->length * sizeof(link));
   if (sp->seq == NULL) {
      printf("error: (con_create) mealloc failed.\n");
      return NULL;
   }

   return sp;
} /*con_alloc*/

/*
  Dispose a sequence of key-value pairs.
 */
boolean con_free(connector *sp)
{
   free(sp->seq);
   free(sp);
   return true;
} /*con_free*/

/*
  Sort an array of link-s *arr of length len.
*/
void quicksort1(link *a, int len) {
   if (len < 2) return;

   int pivot = a[len / 2].key;

   int i, j;
   link temp;
   for (i = 0, j = len - 1; ; i++, j--) {
      while (a[i].key < pivot) i++;
      while (a[j].key > pivot) j--;

      if (i >= j) break;

      temp = a[i];
      a[i] = a[j];
      a[j] = temp;
   }

   quicksort1(a, i);
   quicksort1(a + i, len - i);

} /*quicksort1*/

/*
  Sort a sequence of key-value pairs. Use quicksort.
 */
boolean con_sort(connector *sp)
{
   quicksort1(sp->seq, (sp->last)+1);
   return true;

}/*con_sort*/

/*
  Returns the actual size of kvs. 
 */
int con_size(connector *sp)
{
   return ((sp->last) + 1);

} /*con_size*/

/*
  Print the keys of the kv-store sp t file f. 
 */
void con_print_keys(connector *cp, FILE *f)
{
   // local vars
   link *li = NULL;

   // go through the kv-store frccom the beginning to the end
   con_open(cp);
   while (!con_eos(cp)) {
     
      li = con_read(cp);
      fprintf(f, "%d\n", li->key);
   }

} /*con_print_keys*/

 /*
  Find a reference to a link with the given paremeter key. Use
  binary search. Return NULL if key not found.
 */
link* con_lookup(connector* sp, int key)
{
   int low, high, mid;
   low = 0;
   high = sp->last;
   
   while (low <= high) {
      mid = (low+high)/2;
      if (key < sp->seq[mid].key)
         high = mid - 1;
      else if (key > sp->seq[mid].key)
         low = mid + 1;
      else {
	 // found match
	 sp->cursor = mid;
         return &(sp->seq[mid]);  
      }
   }
   
   // no match
   sp->cursor = high;
   return NULL;   

} /*con_lookup*/
  
/*
  Return true if a given parameter key exists in a key-value store,
  and return false if key not found. Use binary search. The cursor is
  set to point to the found member or is NULL if key not found..
 */
boolean con_member(connector* sp, int key)
{
   int low, high, mid;
   low = 0;
   high = sp->last;
   
   while (low <= high) {
      mid = (low+high)/2;
      if (key < sp->seq[mid].key)
         high = mid - 1;
      else if (key > sp->seq[mid].key)
         low = mid + 1;
      else {
	 // found match
	 sp->cursor = mid;
         return true;  
      }
   }
   
   // no match
   sp->cursor = high;
   return false;   

} /*con_member*/
  
/*
  Initialize the cursor to start reading from the beginning of the
  sequence. Use con_read() to read kv pairs until the end of the
  sequence is reached. The end of the sequence is recorded when
  con_read() returns NULL, or con_eos() returns true.
 */
boolean con_open(connector* sp)
{
   sp->cursor = -1;
   return true;

} /*con_open*/

/*
  Initialize the cursor to start reading the kv pairs after a pair
  with the given key. Use con_read() to read kv pairs following the
  selected pair until the end of the sequence is reached. The end of
  the sequence is recorded when con_read() returns NULL, or con_eos()
  returns true.
 */
boolean con_open_at(connector *sp, int key)
{
   // find first element >=key in sp
   if (con_member(sp, key)) {
      // cursor is set to k
      sp->cursor--;
      return true; 
   } else {
      // cursor set to the begining of array
      //sp->cursor = -1;
      return false;
   }
} /*con_open_at*/

/*
  Peek the next reference to object (value part of kv entry) using
  cursor position. Cursor is not changed.
 */
link* con_peek(connector* sp)
{
   if (sp->cursor >= sp->last)
      return NULL;
   else 
      return &(sp->seq[(sp->cursor)+1]);
} /*con_peek*/

/*
  Read the reference to the next key-value entry using cursor
  position.
 */
link* con_read(connector* sp)
{
   if (sp->cursor >= sp->last)
      return NULL;
   else 
      return &(sp->seq[++(sp->cursor)]);
} /*con_read*/

/*
  Read the reference to the current key-value entry. Function returns
  NULL if current position is out of bounds.
 */
link* con_current(connector* sp)
{
   if ((sp->cursor > sp->last) || (sp->cursor < 0))
      return NULL;
   else 
      return &(sp->seq[sp->cursor]);
} /*con_current*/

/*
  Peek the reference to the predecessor of current key-value
  entry. Function returns NULL if position of the predecessor is out
  of bounds. The cursor is not altered.
 */
link* con_peek_prev(connector* sp)
{
  if (((sp->cursor - 1) > sp->last) || ((sp->cursor - 1) < 0))
      return NULL;
   else 
      return &(sp->seq[sp->cursor - 1]);
} /*con_peek_prev*/

/*
  Read the reference to the predecessor of current key-value
  entry. Function returns NULL if position of the predecessor is out
  of bounds. The cursor is not altered.
 */
link* con_read_prev(connector* sp)
{
  if (((sp->cursor - 1) > sp->last) || ((sp->cursor - 1) < 0))
      return NULL;
   else 
      return &(sp->seq[--(sp->cursor)]);
} /*con_read_prev*/

/*
  Test if current index of a sequence is at the end of sequence.
 */
boolean con_eos(connector *sp)
{
   if (sp->cursor >= sp->last)
      return true;
   else
      return false;
} /*con_eos*/


/*
  Add a new key-value pair at the end of a sequence.
 */
boolean con_write( connector *sp, int key, void* val )
{
   // check for space
   if (sp->last >= (sp->length - 1)) {
      sp->length *= 2;
      sp->seq = (link *)realloc(sp->seq, sp->length * sizeof(link));
      if (sp->seq == NULL) {
         printf("error: (con_insert) realloc failed.\n");
         return false;
      }
   }

   // insert key-value pair at the end of sequence
   sp->seq[++(sp->last)].key = key;
   sp->seq[sp->last].val = val;
   return true;
  
} /*con_write*/

/*
  Insert a key-value pair into an ordered sequence of key-value pairs.
*/
boolean con_insert( connector *sp, int key, void* pvl )
{
   // last_ix always points to the last accessed element
   if (sp->last >= (sp->length - 1)) {
      sp->length *= 2;
      sp->seq = (link *)realloc(sp->seq, sp->length * sizeof(link));
      if (sp->seq == NULL) {
         printf("error: (con_insert) realloc failed.\n");
         return false;
      }
   }
 
   // ix is assigned last, then last++
   int ix = (sp->last)++;
   // while not at begining and key less than curr array key
   while ((ix >= 0) && (key < sp->seq[ix].key)) {
      sp->seq[ix + 1] = sp->seq[ix];
      ix--;
   }

   // insert the key-value pair 
   sp->seq[++ix].key = key;
   sp->seq[ix].val = pvl;
   sp->cursor = ix;    // cursor on inserted  
   return true;
  
} /*con_insert*/

/*
  Return the current cursor.
 */
int con_get_cursor(connector* sp)
{
   return sp->cursor;
} /*con_get_cursor*/

/*
  Set the cursor to parameter cur.
 */
void con_set_cursor(connector* sp, int cur)
{
   sp->cursor = cur;
} /*con_set_cursor*/

