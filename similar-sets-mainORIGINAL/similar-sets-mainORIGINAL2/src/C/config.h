/*
 *  File: config.h
 *  Author: I.Savnik
 *    
 *  Copyright (c) 2023-24, FAMNIT, University of Primorska
 */				        	  

#include <stdlib.h>
#include <stdio.h>

#ifndef CONFIG_H
#define CONFIG_H

/* Global constants, types, ... */ 

#define true 		1
#define false		0

#define MAX_STRING_SIZE    100000000
#define INIT_STRING_SIZE   10000
#define INIT_SET_SIZE      2
#define INIT_QESA_SIZE     10
#define INIT_CONNECT_SIZE  2

#define max(a,b)  ((a) > (b) ? (a) : (b))
#undef	DEBUG_FDEP

/* sorts of set2 */
#define NORMAL 0;
#define ISSET 1:
#define TAILSET 2;

// types 
typedef int boolean;           
//typedef int set2_node;

/* Files */

extern FILE *testf;  
extern boolean testf_rewind;

/* Config parameters */

extern int ST_display; 	
extern int ST_help; 	
extern int ST_print_input; 	
extern int ST_do_subseteq; 	
extern int ST_do_supseteq; 	
extern int ST_do_get_subseteq; 	
extern int ST_do_get_supseteq; 	

/*---------------------------- Exported functions ------------------------------
 */

extern void init_params( int parc, char *param[] );
extern int  interpret( char opt[] );
extern char *strtrm( char *S );


#endif /* CONFIG_H */

