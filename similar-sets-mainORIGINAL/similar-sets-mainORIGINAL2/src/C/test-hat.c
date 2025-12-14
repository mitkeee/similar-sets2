/*--------------------------------------------------------------------------
 *  Testing, testing, ... 
 *
 *  Copyright (c) 2015-2020, FAMNIT, University of Primorska
 *--------------------------------------------------------------------------
 */

#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <malloc.h>
#include <string.h>
#include "config.h"
#include "set.h"
#include "qesa.h"
#include "connector.h"
#include "set2.h"
#include "set2hat.h"

/*-------------------------- MAIN program ----------------------------------
 */

/*
 * Apply test join using LCS measure.
 */
void apply_tests_to_strie_lcs( FILE *f, set2_node *st, int *add, int *skp ) {

   // save simsearch params
   int d1 = *add;
   int d2 = *skp;
   printf("# add=%d, skp=%d\n", d1, d2);

   // init main vars
   int el = -1;
   char *lin = (char *)malloc(MAX_STRING_SIZE);
   char *tok = (char *)malloc(INIT_STRING_SIZE);

   // define time stuff
   uint64_t elap;
   struct timespec start={0,0}, end={0,0};
   
   // set of integers to store sets read from file
   set *s1 = set_alloc();
   set *sp = set_alloc();

   // create qesa for storing the results of queries
   void *q1 = qesa_alloc();

   // read lines from input 
   while (fgets(lin, MAX_STRING_SIZE, f) != NULL) {

      // reset s1 to act as an empty set
      set_reset(s1);
   
      // read next token from lin
      tok = (char *)strtok(strtrm(lin)," \n\f\r");
      do {

	 el = atoi(tok);
         set_insert(s1, el);

      } while ((tok = (char *)strtok(NULL," \n\f\r")) != NULL);

      // print the query
      printf("? ");
      set_print(stdout, s1);
      printf("\n");
      
      // reset set sp and simsearch params
      set_open(s1);
      set_reset(sp);
      qesa_reset(q1);
      *add = d1;
      *skp = d2;
      
      // measure elapsed time
      clock_gettime(CLOCK_MONOTONIC, &start);

      // find in st the sets that are similar to s1
      set2_simsearch_lcs(st, s1, sp, add, skp, q1);

      clock_gettime(CLOCK_MONOTONIC, &end);
      elap = 1000000000L * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;

      // print qesax
      qesa_print(q1, stdout);

      // print elapsed time
      printf("= %llu\n", (long long unsigned int) elap);
   }

   // free the set s1
   set_free(s1);
   set_free(sp);
   
   // free allocated structures
   free(lin);
   free(tok);
   
} /*apply_tests_to_strie_lcs*/

/*
 * Apply test join using Hamming measure.
 */
void apply_tests_to_strie_hmg( FILE *f, set2_hat *sh, int *hmg ) {

   // save simsearch params
   int d1 = *hmg;
   printf("# hamming=%d\n", d1);

   // init main vars
   int el = -1;
   char *lin = (char *)malloc(MAX_STRING_SIZE);
   char *tok = (char *)malloc(INIT_STRING_SIZE);

   // define time stuff
   uint64_t elap;
   struct timespec start={0,0}, end={0,0};
   
   // set of integers to store sets read from file
   set *s1 = set_alloc();
   set *sp = set_alloc();

   // create qesa for storing the results of queries
   void *q1 = qesa_alloc();

   // read lines from input 
   while (fgets(lin, MAX_STRING_SIZE, f) != NULL) {

      // reset s1 to act as an empty set
      set_reset(s1);
   
      // read next token from lin
      tok = (char *)strtok(strtrm(lin)," \n\f\r");
      do {

	 el = atoi(tok);
         set_insert(s1, el);

      } while ((tok = (char *)strtok(NULL," \n\f\r")) != NULL);

      // print the query
      printf("? ");
      set_print(stdout, s1);
      printf("\n");
      
      // skip if length of se is smaller or eqal to hmg.
      //if (set_size(s1) < *hmg) continue;
   
      // reset set sp and simsearch params
      set_open(s1);
      set_reset(sp);
      qesa_reset(q1);
      *hmg = d1;
      
      // measure elapsed time
      clock_gettime(CLOCK_MONOTONIC, &start);

      // find in st the sets that are similar to s1
      s2h_simsearch_hmg(sh, s1, sp, hmg, q1);

      clock_gettime(CLOCK_MONOTONIC, &end);
      elap = 1000000000L * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;

      // print qesax
      qesa_print(q1, stdout);

      // print elapsed time
      printf("= %llu\n", (long long unsigned int) elap);
   }

   // free the set s1
   set_free(s1);
   set_free(sp);
   
   // free allocated structures
   free(lin);
   free(tok);
   
} /*apply_tests_to_strie_hmg*/

/*
 */
int main( int argc, char *argv[] )
{  

   // simserach params
   int hmg = atoi(argv[1]);
   int part_size = atoi(argv[2]);
   char *fnam = argv[3];
		      
   // reading a dataset from a file
   FILE *infile = fopen(fnam, "r");
   set2_hat *sh = s2h_load(infile, part_size, hmg);
   fclose(infile);

   // printing statistics of the lengths of sets from a dataset
   //printf("Statistics of set lengths.\n");
   //qesa_print_inxs(sh->stats, stdout);
   
   // printing the mapping based on set length
   //printf("Print keys of a kvs (mapping).\n");
   //con_print_keys(sh->tries, stdout);
   
   // printing a dataset from set-trie sh
   //printf("Print sets stored in set-trie.\n");
   //s2h_store(sh, stdout);
   
   // foreach set from testset search simsets in st
   //printf("Print results of tests.\n");
   apply_tests_to_strie_hmg(stdin, sh, &hmg);
   
} /*main*/



int old_main( int argc, char *argv[] )
{  

   printf("-------Reading a dataset from a file.\n");
   FILE *infile = fopen(argv[1], "r");
   set2_node *st = set2_load(infile);

   //printf("-------Printing a dataset from set-trie st.\n");
   //set2_store(stdout, st);
   fclose(infile);

   printf("-------Creating set s1.\n");
   set *s1 = set_alloc(10);

   //int a1[] = {3,4,5,6,8,9,10,14,15,18};
   //int a1[] = {0,1,4,5,6};
   //int a1[] = {1,4,8};
   //int a1[] = {1,3,5};
   //int a1[] = {0,1,6,287,551,579,600,743,2204,6872,10224,14095,15417,50611,59152};
   //int a1[] = {0,1,6,287,551,579,600,743};
   //int a1[] = {0,1,15};
   //int a1[] = {0,1,2,3,4,5,18,20,32,49,87,109,195,227,330,906,1767,2011,3048,3521,5974,19771,22470,29289,35522,57714,89070,105592};
   //int a1[] = {0, 1, 2, 3, 4, 5, 11,13,156,185,254,374,2709,4393,26323};
   int a1[] = {0,1,2,3,907,18341};
   
   printf("-------Inserting elms in s1.\n");
   int n = sizeof(a1)/sizeof(a1[0]);
   for (int i=0; i < n; i++) {
       set_write(s1, a1[i]);
   }

   printf("-------Printing s1.\n");
   set_print(stdout, s1); printf("\n");

   printf("-------Creating set sp.\n");
   set *sp = set_alloc(10);

   int add = 1;
   int skp = 1;

   printf("-------Find in st sets similar to s1: add=%d, skp=%d.\n", add, skp);
   //set2_simsearch(st, s1, sp, &skp, &add);
   
} /*main*/
