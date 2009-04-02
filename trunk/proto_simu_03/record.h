/* 
**  File:   record.h
**  Author: Maurizio Munafo'
**  Description: Definition, data structures and prototypes for the 
**               management of a FIFO list (queue) of records 
*/

#include "simtime.h"

typedef struct record_unit Record;

struct record_unit
  { 
    Record *next;
    Time  arrival;
    int key;
    int gen_peer;
  };

/* Prototipi */
void in_list(Record **, Record *);	/* Insert a record in the list */
Record *out_list(Record **);		/* Returns the first element in the list*/
Record *new_record(void );		/* Returns a new record */
void release_record(Record *);		/* Releases a record */

