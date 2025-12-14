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
 
/*-------------------------- MAIN program ----------------------------------
 */

typedef struct kv_pair {
  int    key;   /* a key is an element of a set */
  int    val;   /* value is, in this case, a ptr to a s2_node */
} kv_pair;


int main( int argc, char *argv[] )
{  

  printf("Creating array of kv_pair-s.\n");
  int size = sizeof(kv_pair);
  kv_pair *kvs = (kv_pair *)calloc(10, sizeof(kv_pair));

  for (int i=0; i<10; i++) {
    kvs[i].key = i;
    kvs[i].val = i*10;
  }

  for (int i=0; i<10; i++) {
    printf("kvs[%d].key=%d\n", i, kvs[i].key);
    printf("kvs[%d].val=%d\n", i, kvs[i].val);
  }

  printf("size of int=%d\n", sizeof(int));
  printf("size of kv_pair=%d\n", sizeof(kv_pair));
  printf("size of kvs=%d (bytes)\n", 10*sizeof(kv_pair));
  printf("size of kvs=%d (?)\n", sizeof(kvs));
  printf("size of kvs=%d (*?)\n", sizeof(*kvs));
  printf("size of kvs=%d (*?.key)\n", sizeof((*kvs).key));

  kvs = (kv_pair *)realloc(kvs, 20*sizeof(kv_pair));

  for (int i=10; i<20; i++) {
    kvs[i].key = i;
    kvs[i].val = i*10;
  }

  for (int i=0; i<20; i++) {
    printf("kvs[%d].key=%d\n", i, kvs[i].key);
    printf("kvs[%d].val=%d\n", i, kvs[i].val);
  }

  free(kvs);
				   
} /*main*/

