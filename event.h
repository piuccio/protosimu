/* 
**  File:   event.h
**  Author: Maurizio Munafo'
**  Description: Definition, data structures and prototypes for the 
**               management of the Future Event Set as a list
*/

#include "simtime.h"

typedef struct event_unit Event;

/*
  L'Event List is a circular bilinked list, referenced by the pointer
  to the last element
*/
struct event_unit
      {
	Event *next;	/* Next event in the list */
	Event *prev;    /* Previous event in the list */
	Time  time;     /* Time at which the event is scheduled to 
	                   happen */
	int   type;     /* Type of event, to be defined */
	/* Optional fields, depending on the events to be simulated */
	int   peer;   /* Peer number */
	int   param2;   /* Generic integer parameter */
	double param3;  /* Generic double  parameter */
	char  *pointer; /* Generic pointer parameter, to be used 
	                   through a cast to the specific type   */
      };

/* Prototypes */
void insert_event(Event **, Event *);	/* Inserts an event in the  FES */
Event *get_event(Event **);		/* Returns the first event from the FES */
Event *new_event(void );		/* Returns a new event */
void release_event(Event *);		/* Releases an event */
