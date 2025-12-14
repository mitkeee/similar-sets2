/*
 *  File: set2.c
 *  Author: Iztok Savnik
 * 
 *  Description: A data structure for storing and querying large sets
 *  of sets.
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
 
/*
  Create a new set-trie.
 */
set2_node *set2_alloc()
{
   set2_node *st = (set2_node *)malloc(sizeof(set2_node));
   st->isset = false;
   st->istail = false;
   st->ndset = NULL;
   st->sub.link = NULL;
   st->min = -1;
   st->max = -1;
   st->cnt = 0;
   return st;
   
} /*set2_alloc*/

/*
  Dispose a set-trie referenced by st.
 */
void set2_free( set2_node *st )
{
} /*set2_free*/


/*
  Update set length bounds for a given set2_node.
 */
void update_bounds( set2_node *st, set *su )
{
   int sulen = set_tl_size(su);
   if ((sulen < (st->min)) || ((st->min) == -1)) {
      st->min = sulen;
   }
   if ((sulen > (st->max)) || ((st->max) == -1)) {
      st->max = sulen;
   }
   //printf("selen=%d, st-min=%d, st-max=%d\n", sulen, (st->min), (st->max));

} /*update_bounds*/

/*
  Inserts elements from two sets from their cursor on to the set-trie
  st by merging them in common prefix
 */
void set2_insert_merge( set2_node *st, set *u1, set *u2 )
{
   int el;
   link *lp = NULL;
   set *sp = NULL;
   set2_node *s2p = st;
   
   while (!set_eos(u1) && !set_eos(u2)) {

      int el1 = set_read(u1);
      int el2 = set_read(u2);

      // there is no connector in s2p; for both cases
      s2p->sub.link = con_alloc();

      if (el1 != el2) {
 	 // create and set set2-node for u1
	 set2_node *sn1 = set2_alloc();

	 // update min-max set length bounds
         update_bounds(sn1, u1);           

	 if (set_eos(u1)) {
	    sn1->isset = true;
 	    sn1->ndset = u1;
	 } else {
	    sn1->istail = true;
	    sn1->sub.tail.set = u1;
	    sn1->sub.tail.cursor = set_get_cursor(u1);
	 }
	 con_insert(s2p->sub.link, el1, (void *)sn1);

 	 // create and set set2-node for u2
	 set2_node *sn2 = set2_alloc();

	 // update min-max set length bounds
         update_bounds(sn2, u2);           

	 if (set_eos(u2)) {
	    sn2->isset = true;
	    sn2->ndset = u2;
	 } else {
	    sn2->istail = true;
	    sn2->sub.tail.set = u2;
	    sn2->sub.tail.cursor = set_get_cursor(u2);
	 }
	 con_insert(s2p->sub.link, el2, (void *)sn2);
	 
         // nothing more to do
	 return;
	 
      } else /* (el1 == el2) */ {
	
 	 // create new set node for e1=e2.
	 set2_node *sn1 = set2_alloc();

	 // update min-max set length bounds
         update_bounds(sn1, u1);           
         update_bounds(sn1, u2);           

	 // link s2p to sn1 through el1.
	 con_insert(s2p->sub.link, el1, (void *)sn1);
	 s2p = sn1;
      }
   }

   // the only case when s2p->sub.link stays NULL
   // u1 = u2;
   // !!! one of sets should be disposed
   if (set_eos(u1) && set_eos(u2)) {
      s2p->isset = true;
      s2p->ndset = u2;
      set_free(u1);   
      return;
   }
   // end of u1
   if (set_eos(u1)) {
      s2p->isset = true;
      s2p->ndset = u1;
      s2p->istail = true;
      s2p->sub.tail.set = u2;
      s2p->sub.tail.cursor = set_get_cursor(u2);

   // end of u2
   } else {
      s2p->isset = true;
      s2p->ndset = u2;
      s2p->istail = true;
      s2p->sub.tail.set = u1;
      s2p->sub.tail.cursor = set_get_cursor(u1);
      
   }
} /*set2_insert_merge*/

/*
  Insert a parameter set se into a set-trie st.
 */
void set2_insert( set2_node *st, set *se )
{
   int el;
   link *lp = NULL;
   set *sp = NULL;

   // set tmp pointer to root; update min-max bounds
   set2_node *s2p = st;
   update_bounds(s2p, se);
   
   // go through elements of se
   while (!set_eos(se)) {

      // inserting into tail set
      if (s2p->istail) {
	 sp = s2p->sub.tail.set;   // these are in union    
         set_restore_cursor(sp, s2p->sub.tail.cursor);
	 s2p->sub.link = NULL;

	 // no more tail & merge sp and se in sub-trie
	 s2p->istail = false;
         set2_insert_merge(s2p, sp, se);
	 return;
      }
      
      // newly created set2-node?
      if (s2p->sub.link == NULL) {

 	 // create tail set
 	 s2p->istail = true;
	 s2p->sub.tail.set = se;
	 s2p->sub.tail.cursor = set_get_cursor(se);
         return;
      }

      // read next element 
      el = set_read(se);
      if ((lp = con_lookup(s2p->sub.link, el)) == NULL) {

	 // child for el does not exist; create new one
	 set2_node *new_s2p = set2_alloc();

	 con_insert(s2p->sub.link, el, (void *)new_s2p);
	 s2p = new_s2p;
	 
      } else {

	 // child for el exists; just move there
  	 s2p = (set2_node *)(lp->val);
      }

      // update min-max bounds
      update_bounds(s2p, se);

   }

   // save set se and mark the end of set
   s2p->ndset = se;
   s2p->isset = true;
   return;
} /*set2_insert*/

/*
  Search in set-trie st the sets that are similar to the set se using
  the Hamming distance. The current path from root to active node is
  stored in the set sp.
 */
void set2_simsearch_hmg( set2_node *st, set *se, set *sp, int *hmg, qesa *qp )
{
   int nel = 0;           // next element
   int cnl = 0;           // count delete operations
   link *li = NULL;
   int selen = 0;
   int sslen = 0;
   
   // check the length of se tail against the min-max bounds
   selen = set_tl_size(se);
   //printf("selen=%d, st-min=%d, st-max=%d, hmg=%d\n", selen, (st->min), (st->max), (*hmg));
   if ( ((selen + (*hmg)) < st->min) || (selen > (st->max + (*hmg)))) {
      // too small even all hmg used || too big even if all hmg used 
      //printf("hit\n"); 
      return;
   }
   
   // are we at the end of a set?
   if (st->isset) {

      // sp is similar to se if length of se's tail is less than or
      // equal to number of skipped elements in se.
      if (((*hmg) - set_tl_size(se)) >= 0) {

 	 qesa_write(qp, (void *)(st->ndset));
      }

      // return if connector was not created
      // check again !!!
      //if (st->sub.link == NULL) {
      //
      //   // nothing else to do
      //   return;
      //}
   }

   // are we in a tail?
   if (st->istail) {

      // save hmg
      int tmphmg = *hmg;

      // restore cursor in st->sub.tail.set
      set_restore_cursor(st->sub.tail.set, st->sub.tail.cursor);

      // check the lengths of sets
      sslen = set_tl_size(st->sub.tail.set);
      if (abs(selen - sslen) > tmphmg) {
	 // printf("hit\n");
	 return;
      }
		  
      // check if tail in st is similar to the rest of se
      if (set_tl_similar_rev_hmg(st->sub.tail.set, se, hmg)) {

	 qesa_write(qp, (void *)(st->sub.tail.set));
      }

      // restire hmg
      *hmg = tmphmg;
      return;
   }

   // return if connector was not created
   if (st->sub.link == NULL) {
      // nothing else to do
      return;
   }

   // open access to links
   con_open(st->sub.link);

   while (!set_eos(se) && !con_eos(st->sub.link)) {

      // peek heads of both sets
      nel = set_peek(se);   // peek the next elm in se
      li  = con_peek(st->sub.link); // peek the next link li

      if (nel > li->key) {
	
  	 // more elements can be added?
         if (*hmg > 0) {
      
            // add elem from link, search in sub-tree then get next one
	    do {
 	       con_read(st->sub.link);

	       // descend only with li->key
	       set_push(sp, li->key);
	       (*hmg)--;
               set2_simsearch_hmg((set2_node *)(li->val), se, sp, hmg, qp);
	       (*hmg)++;
               set_pop(sp);

               // check next link in connector
	       li = con_peek(st->sub.link);
	    
	    } while ((li != NULL) && (nel > li->key));

            continue;
	    
	 } else {

            // (nel > li->key) && (hmg = 0) ==> try to descend in st
            // and se with nel. for now, read a link at the position
            // nel from the connector.
            con_open_at(st->sub.link, nel);
            li = con_peek(st->sub.link);

            continue;
         }

      } else if (nel == li->key) {

 	 // link and se are valid; no need to check.
	 // descend in both, se and st.
	 nel = set_read(se);
         set_push(sp, nel);
         set2_simsearch_hmg((set2_node *)(li->val), se ,sp, hmg, qp);
	 set_pop(sp);
	 set_unread(se, 1);

	 // descend also in tree set with li->key
         li = con_read(st->sub.link); 

	 // if possible skip element from se
	 if (*hmg > 0) {

 	    // descend only in se, ie., skip one in se, or delete one
	    // in se.  note: we delete nel from the solution.
	    // advancement to next elements possible ONLY while hmg>0.
	    set_read(se);
            // st.sub.link is already at the next position. by using
	    // the same link (used to descend) we come to the same
	    // situation as with "equality" descent, with one add and
	    // one skip. no need for another con_read.
 	    cnl++;
	    (*hmg)--;
            continue;
	    
	 } else {

	    // can not skip nel so no more adding is possible in the
	    // given position.

	    // restore cursor in se (skips) to the position when
	    // function entered. restore the state of hmg to previous
	    // position.
            if (cnl > 0) {
               *hmg += cnl;
               set_unread(se, cnl);
            }
	    return;
	 }
	 
      } else /* nel < li->key */ {
	
	 // if possible skip element from se
	 if (*hmg > 0) {

 	    // descend only in se, ie., skip one in se, or delete one
	    // in se. note: we delete nel from the solution.
	    // advancement to next elements possible only if skp>0.
	    set_read(se);
 	    cnl++;
	    (*hmg)--;
            continue;
	    
         } else {
	   
	    // hmg = 0 and therefore nel can not be skipped. otherwise
	    // the selected set is not similar any more with se.
	    // therefore we can not add more elements from a tree set.

            // restore cursor in se and update skp accordingly.
            if (cnl > 0) {
               *hmg += cnl;
               set_unread(se, cnl);
            }
	    return;
	 }
      }
      
   } // while

   // if (set_eos(se) && con_eos(st->sub.link)) {
         // nothing more to do in this node. 
   // } else if (con_eos(st->sub.link)) /* && !set_eos(se) */ { 
         // can skip remaining elms from se if skp >=
         // set_tl_lnegth(se). and if st->isset is true then sp is the
         // result. this is the same situation as when st->isset is true
         // at the beginning of this function.
   // } else /* set_eos(se) && !con_eos(st->sub.link) */ {

   if (set_eos(se) && !con_eos(st->sub.link)) {

      if (*hmg > 0) {
      
         // add elem from link, search in sub-tree then get next one
	 do {
 	    li = con_read(st->sub.link);

	    // descend only with li->key
	    set_push(sp, li->key);
	    (*hmg)--;
            set2_simsearch_hmg((set2_node *)(li->val), se, sp, hmg, qp);
	    (*hmg)++;
            set_pop(sp);

	    // check next link in connector
	    li = con_peek(st->sub.link);
	    
	 } while (li != NULL);
      }
   }

   // restore cursor in se and update skp accordingly.
   if (cnl > 0) {
      *hmg += cnl;
      set_unread(se, cnl);
   }
   return;
   
} /*set2_simsearch_hmg*/

/*
  Search in set-trie st the sets that are similar to the set se. The
  current path from root to active node is stored in the set sp. 
 */
void set2_simsearch_lcs( set2_node *st, set *se, set *sp, int *skp, int *add, qesa *qp )
{
   int nel = 0;           // next element
   int cnl = 0;           // count delete operations
   link *li = NULL;

   // are we at the end of a set?
   if (st->isset) {

      // sp is similar to se if length of se's tail is less than or
      // equal to number of skipped elements in se.
      int tmp_skp = (*skp) - set_tl_size(se);
      if (tmp_skp >= 0) {

	 qesa_write(qp, (void *)set_copy(sp));
	 //set_print(stdout, sp);
	 //fprintf(stdout, " ");
         //set_tl_print(stdout, se);
	 //fprintf(stdout, " (%d,%d)\n", *add, tmp_skp);
         //fprintf(stdout, "\n");        
      }

      // return if connector was not created
      if (st->sub.link == NULL) {

	 // nothing else to do
	 return;
      }
   }

   // are we in a tail?
   if (st->istail) {

      // save skp and add
      int tmp_skp = *skp;
      int tmp_add = *add;

      // check if tail in st is similar to the rest of se
      if (set_tl_similar_lcs(st->sub.tail.set, se, skp, add)) {

	 qesa_write(qp, (void *)(st->sub.tail.set));
         // left for testing. should be the same as st->sub.tail
         //set_print(stdout, sp);
         //fprintf(stdout, " ");
         //set_tl_print(stdout, st->sub.tail);
	 //fprintf(stdout, " (%d,%d)\n", *add, *skp);
         //fprintf(stdout, "\n");        
      }

      // restire skp and add
      *skp = tmp_skp;
      *add = tmp_add;
      return;
   }

   // open access to links
   con_open(st->sub.link);

   while (!set_eos(se) && !con_eos(st->sub.link)) {

      // peek heads of both sets
      nel = set_peek(se);   // peek the next elm in se
      li  = con_peek(st->sub.link); // peek the next link li

      if (nel > li->key) {
	
         // nothing more to skip in se if nel=-1!
  	 // one more can be added?
         if (*add > 0) {
      
            // add elem from link, search in sub-tree then get next one
	    do {
 	       con_read(st->sub.link);

	       // descend only with li->key
	       set_push(sp, li->key);
	       (*add)--;
               set2_simsearch_lcs((set2_node *)(li->val), se, sp, skp, add, qp);
	       (*add)++;
               set_pop(sp);

               // check next link in connector
	       li = con_peek(st->sub.link);
	    
	    } while ((li != NULL) && (nel > li->key));

            continue;
	    
	 } else {

            // (nel > li->key) && (add = 0) ==> try to descend in
            // se with (skip) nel only, if skp > 0 is true.
     	    // for now just read from the connector a link at the
	    // position nel.
            con_open_at(st->sub.link, nel);
            li = con_peek(st->sub.link);

            continue;
         }

      } else if (nel == li->key) {

 	 // link and se are valid; no need to check.
	 // descend in both, se and st.
	 nel = set_read(se);
         set_push(sp, nel);
         set2_simsearch_lcs((set2_node *)(li->val), se ,sp, skp, add, qp);
	 set_pop(sp);
	 set_unread(se, 1);

	 // descend also in tree set with li->key
         li  = con_read(st->sub.link); 

	 // if possible skip element from se
	 if (*skp > 0) {

 	    // descend only in se, ie., skip one in se, or delete one
	    // in se.  note: we delete nel from the solution.
	    // advancement to next elements possible ONLY while skp>0.
	    set_read(se);
            // st.sub.link is already at the next position. by using
	    // the same link (used to descend) we come to the same
	    // situation as with "equality" descent, with one add and
	    // one skip. no need for another con_read.
 	    cnl++;
	    (*skp)--;
            continue;
	    
	 } else {

	    // can not skip nel so no more adding is possible in the
	    // given position.

	    // restore cursor in se (skips) to the position when
	    // function entered. restore the state of skp to the same
	    // position.
            if (cnl > 0) {
               *skp += cnl;
               set_unread(se, cnl);
            }
	    return;
	 }
	 
      } else /* nel < li->key */ {
	
	 // if possible skip element from se
	 if (*skp > 0) {

 	    // descend only in se, ie., skip one in se, or delete one
	    // in se. note: we delete nel from the solution.
	    // advancement to next elements possible only if skp>0.
	    set_read(se);
 	    cnl++;
	    (*skp)--;
            continue;
	    
         } else {
	   
	    // skp = 0 and therefore nel can not be skipped. otherwise
	    // the selected set is not similar any more with se.
	    // therefore we can not add more elements from a tree set.

            // restore cursor in se and update skp accordingly.
            if (cnl > 0) {
               *skp += cnl;
               set_unread(se, cnl);
            }
	    return;
	 }
      }
      
   } // while

   // if (set_eos(se) && con_eos(st->sub.link)) {
         // nothing more to do in this node. 
   // } else if (con_eos(st->sub.link)) /* && !set_eos(se) */ { 
         // can skip remaining elms from se if skp >=
         // set_tl_lnegth(se). and if st->isset is true then sp is the
         // result. this is the same situation as when st->isset is true
         // at the beginning of this function.
   // } else /* set_eos(se) && !con_eos(st->sub.link) */ {

   if (set_eos(se) && !con_eos(st->sub.link)) {

      if (*add > 0) {
      
         // add elem from link, search in sub-tree then get next one
	 do {
 	    li = con_read(st->sub.link);

	    // descend only with li->key
	    set_push(sp, li->key);
	    (*add)--;
            set2_simsearch_lcs((set2_node *)(li->val), se, sp, skp, add, qp);
	    (*add)++;
            set_pop(sp);

	    // check next link in connector
	    li = con_peek(st->sub.link);
	    
	 } while (li != NULL);
      }
   }

   // restore cursor in se and update skp accordingly.
   if (cnl > 0) {
      *skp += cnl;
      set_unread(se, cnl);
   }
   return;
   
} /*set2_simsearch_lcs*/

/*
  Write a set-trie to file in left-deep first order to the file f.
 */
void set2_wtf( FILE *f, set2_node *st, set *s1 )
{
   // common pointer to link instances in set-trie
   link *li;

   // end of set in set2 node
   if (st->isset) {
      set_print(f, s1);
      fprintf(f, "\n");
      // no return: set may be followed by other supersets
   } 

   // end of set with tail
   if (st->istail) {
      set_print(f, st->sub.tail.set);
      /*set_print(f, s1);    
      fprintf(f, " ");
      set_tl_print(f, st->sub.tail.set);*/
      fprintf(f, "\n");
      return;
   } 

   // nothing more to do if the leaf reached
   if (st->sub.link == NULL) {
      return;
   }
   
   // open read access to connector
   con_open(st->sub.link);

   // go through all elements
   for (li = con_read(st->sub.link); li != NULL; li = con_read(st->sub.link)) {

      set_push(s1, li->key);
      set2_wtf(f, (set2_node *)(li->val), s1);
      set_pop(s1);
   }

}/*set2_wtf*/

/*
  Store sets from set-trie st in left-deep first order to the file f.
 */
void set2_store( set2_node *st, FILE *f )
{
   // set used to trace the descent path
   set *s1 = set_alloc();

   // write a set-trie to file
   set2_wtf(f, st, s1);

   // free the allocated set
   set_free(s1);
} /*set2_store*/

/*
  Load set-trie strie from file f.
 */
set2_node* set2_load( FILE *f )
{
   // init main vars
   int el = -1;
   char *lin = (char *)malloc(MAX_STRING_SIZE);
   char *tok = (char *)malloc(INIT_STRING_SIZE);

   // prepare the root of set-trie 
   set2_node *s2p = set2_alloc();

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
      set2_insert(s2p, s1);
   }

   // free allocated structures
   free(lin);
   free(tok);

   // return ptr to set-trie root
   return s2p;
   
}/*set2_load*/

