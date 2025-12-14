/*
 * File: questa.h
 * 
 * Copyright (c) 2024, FAMNIT, University of Primorska
 * Author: Iztok Savnik
*/				        	  
 
#ifndef QESA_H
#define QESA_H

/*
  A collection data structure for storing pointers to data structures
  of a given type but possibly different types. Qesa is intended to
  provide whatever access to a collection of the elements is needed. 

  The current implementation of module is intended to store a sequence
  of references acting as a queue. The elements are added to the end
  of sequence. The elements are read from the beginning towards the
  end of sequence.
 */
typedef struct qesa {
  int length;
  int last;
  int cursor ;
  void **arr;
} qesa;

/* Exported functions */

extern qesa*   qesa_alloc();
extern boolean qesa_free( qesa *qp );
extern int     qesa_size( qesa *qp );
extern int     qesa_length( qesa *qp );

extern boolean qesa_open( qesa *qp );
extern void*   qesa_read( qesa *qp );
extern boolean qesa_write( qesa *qp, void *ptr );
extern boolean qesa_eos( qesa *qp );
extern boolean qesa_reset( qesa *qp );
extern int     qesa_cursor(qesa *qp);

extern void*   qesa_retrieve(qesa *qp, int ky);
extern boolean qesa_update(qesa *qp, int ky, void *ptr);
extern boolean qesa_increment(qesa *qp, int ky);

extern void    qesa_print( qesa *qp, FILE *f );
extern void    qesa_print_inxs( qesa *qp, FILE *f );

#endif /* QESA_H */
