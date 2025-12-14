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
#include "connector.h"

/*-------------------------- MAIN program ----------------------------------
 */

int main( int argc, char *argv[] )
{  
   printf("-------Creating a sequence of a given length.\n");
   connector *sp = con_alloc(2000);

   // used variables
   void *ip = (void *)malloc(sizeof(int));
   int i = 0;
   int k = -1;

   printf("-------Initialization of a sequence.\n");
   for (int i = 1; i<50; i++) {
     void *ip = (void *)malloc(sizeof(int));
     *((int *)ip) = (i+1)*10;
     if (!con_write(sp, i*10, ip)) {
        printf("Problem...\n");
     }
   }

   printf("-------Testing con_lookup.\n");
   link* lfnd = NULL;
   link* lnxt = NULL;
   link* lcur = NULL;
   link* kp = NULL;

   lfnd = con_lookup(sp, 5);
   if (lfnd == NULL) printf("Lookup %d: no such key\n", 5);
   else printf("Lookup: key=%d, val=%d\n", lfnd->key, *(int *)(lfnd->val));

   lcur = con_current(sp);
   if (lcur == NULL) printf("Current: no such key\n");
   else printf("Current: key=%d, val=%d\n", lcur->key, *(int *)(lcur->val));

   lnxt = con_peek(sp);
   if (lnxt == NULL) printf("Next: no such key\n");
   else printf("Next: key=%d, val=%d\n", lnxt->key, *(int *)(lnxt->val));

   printf("-----\n");
   lfnd = con_lookup(sp, 10);
   if (lfnd == NULL) printf("Lookup %d: no such key\n", 10);
   else printf("Lookup: key=%d, val=%d\n", lfnd->key, *(int *)(lfnd->val));

   lcur = con_current(sp);
   if (lcur == NULL) printf("Current: no such key\n");
   else printf("Current: key=%d, val=%d\n", lcur->key, *(int *)(lcur->val));

   lnxt = con_peek(sp);
   if (lnxt == NULL) printf("Next: no such key\n");
   else printf("Next: key=%d, val=%d\n", lnxt->key, *(int *)(lnxt->val));

   printf("-----\n");
   lfnd = con_lookup(sp, 42);
   if (lfnd == NULL) printf("Lookup %d: no such key\n", 42);
   else printf("Lookup: key=%d, val=%d\n", lfnd->key, *(int *)(lfnd->val));

   lcur = con_current(sp);
   if (lcur == NULL) printf("Current: no such key\n");
   else printf("Current: key=%d, val=%d\n", lcur->key, *(int *)(lcur->val));

   lnxt = con_peek(sp);
   if (lnxt == NULL) printf("Next: no such key\n");
   else printf("Next: key=%d, val=%d\n", lnxt->key, *(int *)(lnxt->val));

   printf("-----\n");
   lfnd = con_lookup(sp, 450);
   if (lfnd == NULL) printf("Lookup %d: no such key\n", 450);
   else printf("Lookup: key=%d, val=%d\n", lfnd->key, *(int *)(lfnd->val));

   lcur = con_current(sp);
   if (lcur == NULL) printf("Current: no such key\n");
   else printf("Current: key=%d, val=%d\n", lcur->key, *(int *)(lcur->val));

   lnxt = con_peek(sp);
   if (lnxt == NULL) printf("Next: no such key\n");
   else printf("Next: key=%d, val=%d\n", lnxt->key, *(int *)(lnxt->val));

   printf("-----\n");
   lfnd = con_lookup(sp, 155);
   if (lfnd == NULL) printf("Lookup %d: no such key\n", 155);
   else printf("Lookup: key=%d, val=%d\n", lfnd->key, *(int *)(lfnd->val));

   lcur = con_current(sp);
   if (lcur == NULL) printf("Current: no such key\n");
   else printf("Current: key=%d, val=%d\n", lcur->key, *(int *)(lcur->val));

   lnxt = con_peek(sp);
   if (lnxt == NULL) printf("Next: no such key\n");
   else printf("Next: key=%d, val=%d\n", lnxt->key, *(int *)(lnxt->val));

   printf("-----\n");
   lfnd = con_lookup(sp, 495);
   if (lfnd == NULL) printf("Lookup %d: no such key\n", 495);
   else printf("Lookup: key=%d, val=%d\n", lfnd->key, *(int *)(lfnd->val));

   lcur = con_current(sp);
   if (lcur == NULL) printf("Current: no such key\n");
   else printf("Current: key=%d, val=%d\n", lcur->key, *(int *)(lcur->val));

   lnxt = con_peek(sp);
   if (lnxt == NULL) printf("Next: no such key\n");
   else printf("Next: key=%d, val=%d\n", lnxt->key, *(int *)(lnxt->val));

   printf("-----\n");
   lfnd = con_lookup(sp, 490);
   if (lfnd == NULL) printf("Lookup %d: no such key\n", 490);
   else printf("Lookup: key=%d, val=%d\n", lfnd->key, *(int *)(lfnd->val));

   lcur = con_current(sp);
   if (lcur == NULL) printf("Current: no such key\n");
   else printf("Current: key=%d, val=%d\n", lcur->key, *(int *)(lcur->val));

   lnxt = con_peek(sp);
   if (lnxt == NULL) printf("Next: no such key\n");
   else printf("Next: key=%d, val=%d\n", lnxt->key, *(int *)(lnxt->val));

   printf("-------Print sequence from the beginning to the end.\n");
   con_open(sp);
   i = 0;
   while ((kp = con_read(sp)) != NULL) {
     printf("cnt=%d, key=%d, val=%d\n", i++, kp->key, *(int *)(kp->val));
   }
 

   printf("-------Disposing the sequence.\n");
   con_free(sp);
				   
} /*main*/

