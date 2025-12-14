/*
 *  File: set2hat.c
 *  Author: Iztok Savnik
 * 
 *  Copyright (c) 2024, FAMNIT, University of Primorska
 */

#include <ctype.h>
#include <stdio.h>
#include <memory.h>
#include <malloc.h>
#include "config.h"
#include "set.h"
#include "qesa.h"
#include "connector.h"
#include "set2.h"
#include "set2hat.h"

/*
  Create a new set-trie.
 */
set2_hat *s2h_alloc()
{
   set2_hat *sh = (set2_hat *)malloc(sizeof(set2_hat));
   sh->stats = NULL;
   sh->tries = NULL;
   sh->min = -1;
   sh->max = -1;
   return sh;
   
} /*s2h_alloc*/

/*
  Dispose a set-trie referenced by sh.
 */
void s2h_free(set2_hat *sh)
{
   qesa_free(sh->stats);
   con_free(sh->tries);
   free(sh);
   return;
   
} /*s2h_free*/

/*
  Insert a parameter set se into a set-trie sh.
 */
void s2h_insert(set2_hat *sh, set *se, int hmg)
{
   int len = set_size(se);
   link *lprv = NULL;
   link *lcur = NULL;
   link *lnxt = NULL;
   int prv_cur = -1;
   
   // find exact position of len in a list of keys
   link *lfnd = con_lookup(sh->tries, len);

   // the first interval does not have a beginning and ends with the
   // element that has associated link to a trie. This trie stores the
   // sets from the first interval. All further intervals start after
   // an element and end with (including) the next element. The last
   // element of the last interval represents the largest sets from the
   // dataset.

   // two cases: element len found or not
   if (lfnd != NULL) {

      // to insert in current range
      prv_cur = con_get_cursor(sh->tries) - 1;
      lprv = con_peek_prev(sh->tries);
      lcur = con_current(sh->tries);
      lnxt = con_read(sh->tries);

   } else {

      // to insert in next range
      prv_cur = con_get_cursor(sh->tries);
      lprv = con_current(sh->tries);        
      lcur = con_read(sh->tries);   // lcur must exist!
      lnxt = con_read(sh->tries);
   }

   // insert se first in main range of sequence lens
   set_open(se);
   set2_insert((set2_node *)(lcur->val), se);

   // now add to the upper neighboring range if needed 
   while ((lnxt != NULL) && ((lcur->key - len + 1) <= hmg)) {
      //set *s1 = set_copy(se);
      set_open(se);
      set2_insert((set2_node *)(lnxt->val), se);

      // now move to next range
      lcur = lnxt;
      lnxt = con_read(sh->tries);
   }

   // add to the lower neighboring range if needed 
   con_set_cursor(sh->tries, prv_cur);
   while ((lprv != NULL) && ((len - lprv->key) <= hmg)) {
      //set *s2 = set_copy(se);
      set_open(se);
      set2_insert((set2_node *)(lprv->val), se);

      // now move to previous range
      lprv = con_read_prev(sh->tries);
   }
      
} /*s2h_insert*/

/*
  Search in set-trie sh the sets that are similar to the set se
  using the Hamming distance.
 */
void s2h_simsearch_hmg(set2_hat *sh, set *se, set *sp, int *hmg, qesa *qp)
{
   // local vars
   int len = set_size(se);
   //set *sp = set_alloc();
   link *lcur = NULL;
   
   // find the range of len and then take the tree associated to the
   // last element in the range.
   link *lfnd = con_lookup(sh->tries, len);
   if (lfnd != NULL)
      // hit, so current eqals cursor
      lcur = lfnd;
   else
      // in between, current is low bound and next represents a
      // range. note that lfnd is now previous link.
      lcur = con_read(sh->tries);

   // lcur should either be below max length or above
   if (lcur != NULL) {

      // lcur now represents the range with se length
      set2_simsearch_hmg((set2_node *)(lcur->val), se, sp, hmg, qp);

   }  // else se length is above the max length of sets from index, but
      // there can still be a match in index if
      // len < (lfnd->key + *hmg). Implement?

} /*s2h_simsearch_hmg*/

/*
  Store sets from set-trie st in left-deep first order to the file f.
 */
void s2h_store(set2_hat *sh, FILE *f)
{
   // function variables
   link *lprv = NULL;
   link *lcur = NULL;

   // open kv store and read sequentially the kv-pairs.
   con_open(sh->tries);

   while (!con_eos(sh->tries)) {

       // read next link
      lcur = con_read(sh->tries);
      if (lprv == NULL)
         fprintf(f, "Range = 1 - %d\n", lcur->key);
      else
         fprintf(f, "Range = %d - %d\n", lprv->key + 1, lcur->key);
      set2_store((set2_node *)(lcur->val), f);
        
      // move to next range
      lprv = lcur;
   }
  
} /*s2h_store*/

/*
  Compute statistic of the lenths of sets from a dataset.
*/
void compute_statistics(set2_hat *sh, FILE *f)
{
   // init main vars
   int el = -1;
   char *lin = (char *)malloc(MAX_STRING_SIZE);
   char *tok = (char *)malloc(INIT_STRING_SIZE);

   // prepare the root of set-trie 
   sh->stats = qesa_alloc();

   // read lines from input 
   while (fgets(lin, MAX_STRING_SIZE, f) != NULL) {

      // set of integers
      set *s1 = set_alloc();
   
      // read next token from lin
      tok = (char *)strtok(strtrm(lin)," \n\f\r");
      do {

	 el = atoi(tok);
         set_insert(s1, el);

      } while ((tok = (char *)strtok(NULL," \n\f\r")) != NULL);
         
      // increment the counter for the given set size in statistics
      qesa_increment(sh->stats, set_size(s1));
   }

   // free allocated structures
   free(lin); 
   free(tok);

} /*compute_statistics*/

/*
  Generate a mapping from the statistics of set lengths stored in
  sh->stats. The mapping is stored in sh->tries, a key-value store.
 */
void generate_mapping(set2_hat *sh, int part_size)
{
   // local vars
   int part_cnt = 1;  // partition counter
   int part_sum = 0;  // num of sets so far
   int act_size = 0;  // num of sets so far
   int *pint = NULL;  // to be pntr to stat of one set length
   boolean pcnt_inc = false;  // catch last range 

   // create a new kv-store
   sh->tries = con_alloc();
   
   // open stats for sequential reading 
   qesa_open(sh->stats);

   // go through all sizes of sets 
   while (!qesa_eos(sh->stats)) {

      // if pint is more than part_size then ajust pint
      // add num of sets of current length (pint) to sum
      pint = (int *)qesa_read(sh->stats);
      if (pint != NULL) {
	 if (*pint >= part_size) part_sum += part_size;
         else part_sum += *pint;
      }

      // index incremented by one at least
      pcnt_inc = false;

      // if treshold is crossed, insert the least recently used key to
      // kv-store. the corresponding value is an empty set-trie.  the
      // current range of keys is then associated with the last key in
      // the range.
      if (part_sum >= part_cnt * part_size) {
 	 set2_node *st = set2_alloc();
         con_write(sh->tries, qesa_cursor(sh->stats), (void *)st);
	 part_cnt++;
	 pcnt_inc = true;   // inx incremented
      }
   }

   // the rest is stored with the last key
   if (!pcnt_inc) {
      set2_node *st = set2_alloc();
      con_write(sh->tries, qesa_cursor(sh->stats), (void *)st);
   }

   // free qesa,it is not needed any more
   //printf("Statistics of set lengths.\n");
   //qesa_print_inxs(sh->stats, stdout);
   qesa_free(sh->stats); 
   
} /*generate_mapping*/

/*
  Load a dataset to a set-trie.
 */
void load_dataset(set2_hat *sh, FILE *f, int hmg)
{
   // about to read the dataset again
   int el = -1;
   char *lin = (char *)malloc(MAX_STRING_SIZE);
   char *tok = (char *)malloc(INIT_STRING_SIZE);

   // read lines from input 
   while (fgets(lin, MAX_STRING_SIZE, f) != NULL) {

      // set of integers
      set *s1 = set_alloc();
   
      // read next token from lin
      tok = (char *)strtok(strtrm(lin)," \n\f\r");
      do {

	 el = atoi(tok);
         set_insert(s1, el);

      } while ((tok = (char *)strtok(NULL," \n\f\r")) != NULL);
         
      // reset access to s1 for reading and insert s1 into set-trie
      set_open(s1);
      s2h_insert(sh, s1, hmg);
   }

   // free allocated structures
   free(lin);
   free(tok);
  
} /*load_dataset*/

/*
  Load set-trie strie from file f.
 */
set2_hat *s2h_load(FILE *f, int psize, int hmg)
{
   // create a new set-trie with hat
   set2_hat *sh = s2h_alloc();

   // compute statistics from input dataset
   compute_statistics(sh, f);

   // generate a mapping from sets to ranges of set lengths. each
   // range is represented by one trie.
   generate_mapping(sh, psize);

   // load sets from a dataset into a range of tries
   rewind(f);
   load_dataset(sh, f, hmg);

   // return set-trie with hat
   return sh;
   
}/*s2h_load*/


