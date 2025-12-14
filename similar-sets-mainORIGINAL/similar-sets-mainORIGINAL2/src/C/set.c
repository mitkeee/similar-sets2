/*
  File: set.c

  Copyright (c) 2023, FAMNIT, University of Primorska
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "config.h"
#include "set.h"

/* Local variables */

/*
  Creating a new set, i.e. sequence of int numbers on heap.
*/
set *set_alloc()
{
   set *sp = (set *)malloc(sizeof(set));
   if (sp == NULL) {
      printf("error: (set_alloc) malloc failed.\n");
      return NULL;
   }

   sp->length = INIT_SET_SIZE;
   sp->last = -1;
   sp->cursor = -1;
   sp->arr = (int *)malloc(INIT_SET_SIZE * sizeof(int));
   if (sp->arr == NULL) {
      printf("error: (set_alloc) malloc failed.\n");
      return NULL;
   }

   return sp;
} /*set_alloc*/

/*
  Reset the set to the state such that the space remains as it is and
  the set is prepared for loading the elements.
 */
boolean set_reset(set *sp)
{
  // reset the last and cursor
   sp->last = -1;
   sp->cursor = -1;
   return true;
   
} /*set_reset*/

/*
  Dispose the set *sp.
 */
boolean set_free(set *sp)
{
   free(sp->arr);
   free(sp);
   return true;
} /*set_free*/

/*
  Sort an array of integers *arr of length len.
*/
void quicksort(int *a, int len) {
   if (len < 2) return;

   int pivot = a[len / 2];

   int i, j;
   for (i = 0, j = len - 1; ; i++, j--) {
      while (a[i] < pivot) i++;
      while (a[j] > pivot) j--;

      if (i >= j) break;

      int temp = a[i];
      a[i] = a[j];
      a[j] = temp;
   }

   quicksort(a, i);
   quicksort(a + i, len - i);

} /*quicksort*/

/*
  Sort elements of a set stored in an array of integer numbers. Use
  quicksort.
 */
void set_sort(set *sp)
{
  quicksort(sp->arr, (sp->last)+1);

} /*set_sort*/

/*
  Returns the actual size of the set. 
 */
int set_size(set *sp )
{
   return ((sp->last) + 1);

} /*set_size*/

/*
  Returns the complete length of the array. 
 */
int set_length(set *sp )
{
   return sp->length;

} /*set_length*/

/*
  Get an element of the parameter set sp with the index ix.
  No model, not safe.
 */
int set_get(set *sp, int ix )
{
   return sp->arr[ix];
   
} /*set_get*/

/*
  Put the element el in the set sp at the index ix. 
  No model, not safe.
 */
void set_put(set *sp, int ix, int el)
{
   // check index ?
   sp->arr[ix] = el;

} /*set_put*/

/*
  Search for an element el in the set *sp. Return true if element is a
  member of *sp, and false if el is not found. Use binary search.

  If an element el is found, the function set_member() sets the cursor
  to el. The function set_read() can be used to read the elements
  following el in the increasing order of the set elements.
 */
boolean set_member(set* sp, int el)
{
   int low, high, mid;
   low = 0;
   high = sp->last;
   
   while (low <= high) {
      mid = (low+high)/2;
      if (el < sp->arr[mid])
         high = mid - 1;
      else if (el > sp->arr[mid])
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

} /*set_member*/
  
/*
  Initialize the cursor to start reading from the beginning of the set
  s. The set is to be read from the smallest set element to the
  largest element by using operation set_read(). The end of the
  sequence can be recorded when set_read() returns -1, or set_eos()
  returns true.
 */
boolean set_open(set *sp)
{
   sp->cursor = -1;
   return true;
} /*set_open*/

/*
  Initialize the cursor to start reading from the element el (if el is
  in s), or from the least element following el (if el in not in s).
  Use set_read() to read elements following el until the end of the
  sequence is reached. The end of the sequence can be recorded when
  set_read() returns -1, or set_eos() returns true.
 */
boolean set_open_at(set *sp, int el)
{
  // set_member() sets cursor to point to el if it exists in set
  if (set_member(sp, el)) {
     // kvs_read() reads el first and then larger elements
     sp->cursor--;
     return true; 
  } else
     // kvs_read() reads the first element larger than el
     return false;
} /*set_open_at*/

/*
  Return the next element from the set referenced by sp without moving
  the cursor. The value -1 is returned if the cursor is at the last
  element.
 */
int set_peek(set *sp)
{
   if (sp->cursor >= sp->last)
      return -1;
   else 
      return sp->arr[(sp->cursor) + 1];
} /*set_peek*/

/*
  Return the next element from the set referenced by sp and advance
  the cursor. The value -1 is returned if the cursor is at the last
  element.
 */
int set_read(set *sp)
{
   if (sp->cursor >= sp->last)
      return -1;
   else 
      return sp->arr[++(sp->cursor)];
} /*set_read*/

/*
  Decreases the cursor by integer value n.
 */
void set_unread(set *sp, int n)
{
  if ((sp->cursor - n) >= -1) {
     sp->cursor -= n;
  }
} /*set_unread*/

/*
  Check if current index of a sequence is at the last element in an
  array representing a set.
 */
boolean set_eos(set *sp)
{
   if (sp->cursor >= sp->last)
      return true;
   else
      return false;
   
} /*set_eos*/

/*
  Add a new element at the end of the array representing a set.
 */
boolean set_write(set *sp, int el)
{
   // check for space
   if (sp->last >= (sp->length - 1)) {
      sp->length *= 2;
      sp->arr = (int *)realloc(sp->arr, sp->length * sizeof(int));
      if (sp->arr == NULL) {
         printf("error: (set_add) realloc failed.\n");
         return false;
      }
   }

   // insert el at the end of int sequence
   sp->arr[++(sp->last)] = el;
   return true;
  
} /*set_write*/

/*
  Push the element el at the end of the set.
 */
boolean set_push(set *sp, int el)
{
   set_write(sp, el);
   return true;
   
} /*set_push*/

/*
  Pop and return the last element from the set sp. The size of set is
  decreented by one. Return -1 if the set is empty.
 */
int set_pop(set *sp)
{
   // if set empty return -1
   if (sp->last < 0) {
      return -1;
   }

   // return the last element and decrement size of set
   return sp->arr[(sp->last)--];
  
} /*set_pop*/

/*
  Insert a key el into an ordered sequence of integer numbers.
*/
boolean set_insert( set *sp, int el )
{
   // last points to the last allocated element
   if (sp->last >= (sp->length - 1)) {
      sp->length *= 2;
      sp->arr = (int *)realloc(sp->arr, sp->length * sizeof(int));
      if (sp->arr == NULL) {
         printf("error: (set_insert) realloc failed.\n");
         return false;
      }
   }
 
   int ix = (sp->last)++;
   // while not at begining and el less than curr array element
   while ((ix >= 0) && (el < sp->arr[ix])) {
      sp->arr[ix+1] = sp->arr[ix];
      ix--;
   }

   sp->arr[++ix] = el;
   sp->cursor = ix;    // cursor on inserted  
   return true;
  
} /*set_insert*/

/*
  Make a copy of a set sp and return the pointer to a copy.
*/
set *set_copy(set *sp)
{
   // alloc new set and copy content
   set *ns = set_alloc();
   //*ns = *sp;

   // insert elements in new set and return
   for (int i = 0; i <= sp->last; i++) {
     set_write(ns, sp->arr[i]);
   }
   return ns;
   
} /*set_copy*/

/*
  Prints a set st to file f with the spaces in between the elements.
*/
void set_print(FILE *f, set *sp)
{
   boolean first = true;
  
   // print all elements from the beginning to the end of set
   for (int i = 0; i <= sp->last; i++) {
      if (first) {
          fprintf(f, "%d", sp->arr[i]);
	  first = false;
      } else {
          fprintf(f, " %d", sp->arr[i]);
      }
   }
   //fprintf(f, ", %d", set_size(sp));
   
} /*set_print*/

/*
  Retrieves the current value of sp's cursor.
*/
int set_get_cursor(set *sp)
{
   return sp->cursor;
   
} /*set_get_cursor*/

/*
  Restores sp's cursor to the value of the parameter cur.
*/
void set_restore_cursor(set *sp, int cur)
{
   sp->cursor = cur;
   
} /*set_restore_cursor*/

/*
  Returns the size of tail: from cursor+1 to (including) last.
 */
int set_tl_size(set *sp )
{
   return (sp->last - sp->cursor);

} /*set_tl_size*/

/*
  Prints the tail, i.e., from cursor+1 to (including) last, to file f.
 */
void set_tl_print( FILE *f, set *sp )
{
   boolean first = true;
  
   // print all elements from the beginning to the end of set
   for (int i = (sp->cursor + 1); i <= sp->last; i++) {
      if (first) {
          fprintf(f, "%d", sp->arr[i]);
	  first = false;
      } else {
          fprintf(f, " %d", sp->arr[i]);
      }
   }
} /*set_tail_print*/

/*
  Compares two sets for similarity using LCS (longest common
  subsequence) distance. Returns true if similar and false
  otherwise. After the function completes execution the cursor in both
  sets stays at the same position as before function call.
 */
boolean set_tl_similar_lcs( set *sp, set *se, int *skp, int *add )
{
   // elements from sp and se
   int esp = 0;           // element from sp
   int ese = 0;           // element from se
   int csp = sp->cursor;  // remember original positions 
   int cse = se->cursor;  // of cursors
   boolean rtval;         // return value
   
   while (!set_eos(sp) && !set_eos(se)) {

      esp = set_peek(sp);
      ese = set_peek(se);

      if (esp == ese) {

  	 // move the cursors
	 set_read(sp);
	 set_read(se);
	 continue;

      } else {

	 // add an element to result sp if permitted
 	 if (esp < ese) {

	    if (*add > 0) {
	       set_read(sp);
	       (*add)--;
	       continue;
	       
	    } else {

	       // no more adding; restore cursors
	       sp->cursor = csp;
	       se->cursor = cse;
	       return false;   
	    }
	   
	 } else {

	    // skip an element from se if still possible
	    if (*skp > 0) {
	       set_read(se);
	       (*skp)--;
	       continue;
	       
	    } else {

	       // no more skipping; restore the cursors
	       sp->cursor = csp;
	       se->cursor = cse;
	       return false;   
	    }
         }
      } 
   } // while

   // handle tails
   if (set_eos(sp) && set_eos(se)) {

      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;

      // skp and add are >= 0.
      return true;
   }
   
   if (set_eos(sp)) {
    
      // true only if remaining elems from se can be skipped
      *skp -= set_tl_size(se);
      rtval = (*skp) >= 0; 

      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;
      return rtval;

   } else { // set_eos(se) && !set_eos(sp)

      // true only if remaining elems from sp can be added
      *add -= set_tl_size(sp);
      rtval = (*add) >= 0;
      
      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;
      return rtval;
   }   
} /*set_tl_similar_lcs*/

/*
  Compares two sets for similarity using Hamming distance. Returns
  true if similar and false otherwise. After the function completes
  execution the cursor in both sets stays at the same position as
  before function call.
 */
boolean set_tl_similar_hmg( set *sp, set *se, int *hmg )
{
   // elements from sp and se
   int esp = 0;           // element from sp
   int ese = 0;           // element from se
   int csp = sp->cursor;  // remember original positions 
   int cse = se->cursor;  // of cursors
   boolean rtval;         // return value
   
   while (!set_eos(sp) && !set_eos(se)) {

      esp = set_peek(sp);
      ese = set_peek(se);

      if (esp == ese) {

  	 // move the cursors
	 set_read(sp);
	 set_read(se);
	 continue;

      } else {

	 // add an element to result sp if permitted
 	 if (esp < ese) {

	    if (*hmg > 0) {
	       set_read(sp);
	       (*hmg)--;
	       continue;
	       
	    } else {

	       // no more adding; restore cursors
	       sp->cursor = csp;
	       se->cursor = cse;
	       return false;   
	    }
	   
	 } else {

	    // skip an element from se if still possible
	    if (*hmg > 0) {
	       set_read(se);
	       (*hmg)--;
	       continue;
	       
	    } else {

	       // no more skipping; restore the cursors
	       sp->cursor = csp;
	       se->cursor = cse;
	       return false;   
	    }
         }
      } 
   } // while

   // handle tails
   if (set_eos(sp) && set_eos(se)) {

      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;

      // skp and add are >= 0.
      return true;
   }
   
   if (set_eos(sp)) {
    
      // true only if remaining elems from se can be skipped
      *hmg -= set_tl_size(se);
      rtval = (*hmg) >= 0; 

      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;
      return rtval;

   } else { // set_eos(se) && !set_eos(sp)

      // true only if remaining elems from sp can be added
      *hmg -= set_tl_size(sp);
      rtval = (*hmg) >= 0;
      
      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;
      return rtval;
   }   
} /*set_tl_similar_hmg*/

/*
  Compares two sets for similarity using Hamming distance in reverse
  order. Set elements are sorted from the most frequent towards less
  frequent. Starting from the least frequent element gincreases the probability to identify the 

  Returns true if similar and false otherwise. After the function
  completes execution the cursor in both sets stays at the same
  position as before function call.
 */
boolean set_tl_similar_rev_hmg( set *sp, set *se, int *hmg )
{
   // elements from sp and se
   int esp = 0;           // element from sp
   int ese = 0;           // element from se
   int csp = sp->cursor;  // remember original positions 
   int cse = se->cursor;  // of cursors
   int isp = sp->last;
   int ise = se->last;
   boolean rtval;         // return value
   
   while ((isp > csp) && (ise > cse)) {

      esp = set_get(sp, isp);
      ese = set_get(se, ise);

      if (esp == ese) {

  	 // move the cursors
	 isp--;;
	 ise--;
	 continue;

      } else {

	 // add an element to result sp if permitted
 	 if (esp > ese) {

	    if (*hmg > 0) {
	       isp--;
	       (*hmg)--;
	       continue;
	       
	    } else {

	       // no more adding; restore cursors
	       sp->cursor = csp;
	       se->cursor = cse;
	       return false;   
	    }
	   
	 } else {

	    // skip an element from se if still possible
	    if (*hmg > 0) {
	       ise--;
	       (*hmg)--;
	       continue;
	       
	    } else {

	       // no more skipping; restore the cursors
	       sp->cursor = csp;
	       se->cursor = cse;
	       return false;   
	    }
         }
      } 
   } // while

   // handle tails
   if ((isp <= csp) && (ise <= cse)) {

      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;

      // hmg >= 0.
      return true;
   }
   
   if (isp <= csp) {
    
      // true only if remaining elems from se can be skipped
      *hmg -= ise - cse;
      rtval = (*hmg) >= 0; 

      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;
      return rtval;

   } else { // ise <= cse

      // true only if remaining elems from sp can be added
      *hmg -= isp - csp;
      rtval = (*hmg) >= 0;
      
      // restore cursors
      sp->cursor = csp;
      se->cursor = cse;
      return rtval;
   }   
} /*set_tl_similar_rev_hmg*/


