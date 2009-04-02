/* 
**  File:   Event.c
**  Author: Maurizio Munafo'
**  Description: Functions for the management of the FES
*/

#include <stdio.h>
#include <stdlib.h>
#include "event.h"

Event *free_events = NULL;  /* Pointer to my free_list of events */

/* 
**  Function:    void insert_event(Event **last, Event *elem)
**  Parameters:  Event **last  - Reference pointer for the list (the last 
**                               element)
**               Event  *elem  - Record of type Event to be inserted
**  Return:      none
**  Description: Inserts 'elem' in the Event List referenced by **last
*/
void insert_event(Event **last, Event *elem)
{
  Event *p;
  if (elem==NULL) return;       /* Nothing to do */

  /* The Event List is empty: elem becomes the Event List */
  if ((*last)==NULL)
    {
       (*last) = elem;
       (*last)->next = elem;
       (*last)->prev = elem;
       return;
    }

  /* 
  ** elem is scheduled later than the last event in the list:
  ** it is inserted at the end and becomes the new last
  */
  if (elem->time >= (*last)->time)
    {
       elem->prev = (*last);
       elem->next = (*last)->next;
       ((*last)->next)->prev = elem;
       (*last)->next = elem;
       (*last) = elem;
    }
  else
  /* 
  ** lookup the correct position for elem, starting from the end
  ** of the list, i.e. following the prev pointer
  */
    {
       p = (*last);
       while (elem->time < p->time && p->prev!=(*last))
         p = p->prev;
	   if (elem->time < p->time)
         { 
		   /* p is the first element scheduled after elem: 
		      insert elem immediately before it*/
           elem->prev = p->prev;
           elem->next = p;
           (p->prev)->next = elem;
           p->prev = elem;
         }
       else
         {
		   /* elem must be the first event in the list:
		      insert elem after p (that is *last) */
           elem->prev = p;
           elem->next = p->next;
           (p->next)->prev = elem;
           p->next = elem;
         }
    }
  return;
}

/* 
**  Function:    Event *get_event(Event **last)
**  Parameters:  Event **last  - Reference pointer for the list (the last 
**                               element)
**  Return:      Pointer to the extracted event 
**  Description: Returns the next scheduled event, removing it from the top of
**               the FES. Returns NULL if the FES was empty
*/
Event *get_event(Event **last)
{
  Event *p;
  if ((*last)==NULL) return NULL;   /* The Event List is empty */
  if ((*last)->next==(*last))
    {
	  /* I'm removing the only element from the Event List */
      p = (*last);
      (*last) = NULL;
    }
  else
    {
	  /* Extract the first element (the successor of last) */
      p = (*last)->next;
      (p->next)->prev = (*last);
      (*last)->next = p->next;
    }
  return p;
}

/* 
**  Function:	 Event *new_event(void)
**  Parameters:  none
**  Return:      Pointer to a new event
**  Description: Returns a new event either allocating it or extracting it 
**               from the Free List
**  Side Effect: Extracts elements from the global Free List
*/
Event *new_event(void)
{
 extern Event *free_events;
 Event *p;
 int i;
 if (free_events==NULL)
  {
    if ((p=(Event *)malloc(sizeof(Event)))==NULL)
      { printf("Error  : Memory allocation error\n");
        exit(1);
      }
    p->time = (Time)0;
    insert_event(&free_events,p);
  }
 return(get_event(&free_events));  /* Returns the first element of the
                                      Free List */
}

/* 
**  Function:	 void release_event(Event *elem)
**  Parameters:  Event *elem - Event to be released
**  Return:      None
**  Description: Releases the record of the event, inserting it in the
**               Free List
**  Side Effect: Inserts elements in the global Free List
*/
void release_event(Event *elem)
{
 extern Event *free_events;
 elem->time = (Time)MAX_TIME;    /* In this way the record is at the 
                                    bottom of the Free List */
 insert_event(&free_events,elem);
}
