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
#include "qesa.h"

/*-------------------------- MAIN program ----------------------------------
 */


int main( int argc, char *argv[] )
{
   printf("-------Creating qesa q1.\n");
   qesa *q1 = qesa_alloc();

   printf("-------Inserting elements into qesa q1.\n");
   for ( int i = 0; i < 100; i++ ) {
      int *e = (int *)malloc(sizeof(int));
      *e = i*i;
      qesa_write(q1, e);
   }

   printf("-------Printing qesa q1.\n");
   qesa_open(q1);
   while (!qesa_eos(q1)) {
      int *r = (int *)qesa_read(q1);
      printf("%d ", *r);
   }
   printf("\n");
   
   printf("-------Reset and insert elements into qesa q1.\n");
   qesa_reset(q1);
   for ( int i = 0; i < 2000; i++ ) {
      int *e = (int *)malloc(sizeof(int));
      *e = i*i;
      qesa_write(q1, e);
   }

   printf("-------Printing qesa q1.\n");
   qesa_open(q1);
   while (!qesa_eos(q1)) {
      int *r = (int *)qesa_read(q1);
      printf("%d\n", *r);
   }
   printf("\n");

}



