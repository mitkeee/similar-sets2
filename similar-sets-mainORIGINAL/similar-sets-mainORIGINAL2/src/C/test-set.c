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
#include <memory.h>
#include <malloc.h>
#include <string.h>
#include "config.h"
#include "set.h"
 
/*-------------------------- MAIN program ----------------------------------
 */


int main( int argc, char *argv[] )
{
   printf("-------Creating set s1.\n");
   set *s1 = set_alloc(10);

   printf("-------Creating set s2.\n");
   set *s2 = set_alloc(10);

   int a1[] = {3,4,5,6,8,9,10,14,15,18};
   int a2[] = {3,4,5,8,9,10,14,16,19,21};
   
   printf("-------Inserting elms in s1.\n");
   int n = sizeof(a1)/sizeof(a1[0]);
   for (int i=0; i < n; i++) {
       set_write(s1, a1[i]);
   }

   printf("-------Inserting elms in s2.\n");
   n = sizeof(a2)/sizeof(a2[0]);
   for (int i=0; i < n; i++) {
       set_write(s2, a2[i]);
   }
   
   printf("-------Printing s1 and s2.\n");
   set_print(stdout, s1); printf("\n");
   set_print(stdout, s2); printf("\n");

   printf("-------Check if s1 similar to s2.\n");
   int skp = 3;
   int add = 3;
   printf("skp=%d,add=%d\n", skp, add);
   if (set_tl_similar(s1, s2, &skp, &add)) {
      printf("s1 similar to s2\n");
   } else {
      printf("s1 not similar to s2\n");
   }
   printf("skp=%d,add=%d\n", skp, add);
}



void previous_tests()
{  
   printf("-------Creating set.\n");
   set *sp = set_alloc(10);

   printf("-------Inserting 30-39.\n");
   for (int i=0; i<10; i++)
      set_write(sp, i+30);

   printf("-------Printing set.\n");
   int iv;
   set_open(sp);
   while ((iv = set_read(sp)) != -1)
      printf("element=%d\n", iv);

   printf("-------Sorting set.\n");
   set_sort(sp);
   
   printf("-------Printing set.\n");
   set_open(sp);
   while ((iv = set_read(sp)) != -1)
      printf("element=%d\n", iv);

   printf("-------Seach for 30 and printing all that follow.\n");
   set_member(sp, 30);
   while ((iv = set_read(sp)) != -1)
      printf("element=%d\n", iv);

   printf("-------Freeing set.\n");
   set_free(sp);

   printf("-------Creating set of 10 elements.\n");
   sp = set_alloc(10);

   int d = 100000;
   printf("-------Inserting %d random elements.\n", d);
   int i = d;
   int k = 0;
   while (i-- > 0) {
      k = rand() % d;
      if (!set_member(sp, k)) 
         set_insert(sp, k);
   }
   printf("Set size=%d.\n", set_size(sp));

   printf("-------Printing set.\n");
   set_open(sp);
   while ((iv = set_read(sp)) != -1)
      printf("element=%d\n", iv);

   printf("-------Sorting set.\n");
   set_sort(sp);

   i = 100;
   d = 61;
   printf("-------Printing 100 set elms from element %d to the end of sequence.\n", d);
   set_open_at(sp, d);
   while (((iv = set_read(sp)) != -1) && (i-- > 0))
      printf("element=%d\n", iv);

   i = 100;
   d = 2140;
   printf("-------Printing 100 set elms from element %d.\n", d);
   set_open_at(sp, d);
   while (((iv = set_read(sp)) != -1) && (i-- > 0))
      printf("element=%d\n", iv);

   i = 100;
   d = 32456;
   printf("-------Printing 100 set elms from element %d.\n", d);
   set_open_at(sp, d);
   while (((iv = set_read(sp)) != -1) && (i-- > 0))
      printf("element=%d\n", iv);

   i = 100;
   d = 30165;
   printf("-------Printing 100 set elms from element %d.\n", d);
   set_open_at(sp, d);
   while (((iv = set_read(sp)) != -1) && (i-- > 0))
      printf("element=%d\n", iv);

   i = 100;
   d = 98955;
   printf("-------Printing 100 set elms from element %d.\n", d);
   set_open_at(sp, d);
   while (((iv = set_read(sp)) != -1) && (i-- > 0))
      printf("element=%d\n", iv);

   printf("-------Freeing set.\n");
   set_free(sp);
}





