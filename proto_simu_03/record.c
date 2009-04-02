/* 
**  File:   record.c
**  Author: Maurizio Munafo'
**  Description: Functions for the managements of a FIFO list (queue)
*/

#include <stdio.h>
#include <stdlib.h>
#include "record.h"

Record *free_records = NULL;

/* Insert a record at the end of the list */
void in_list(Record **handle, Record *element)
{
 if (*handle==NULL)
  {
    *handle = element;
    element->next = element;
  }
 else
  {
    element->next = (*handle)->next;
    (*handle)->next = element;
    *handle = element;
  }
}

/* Returns the record at the top of the list */
Record *out_list(Record **handle)
{
  Record *p;
  p = *handle;
  if (p==NULL) return(NULL);
  if (p->next==p)
    *handle = NULL;
  else
   {
    p = (*handle)->next;
    (*handle)->next = p->next;
   }
  return(p);
}

/* Returns a new record, recycling one from the free_list or 
** allocating it if needed
*/
Record *new_record(void)
{
 extern Record *free_records;
 Record *p;
 int i;
 if (free_records==NULL)
  {
    if ((p=(Record *)malloc(sizeof(Record)))==NULL)
         { printf("Error  : Memory allocation error\n");
           exit(1);
         }
    in_list(&free_records,p);
  }
 return(out_list(&free_records));
}

/* Releases a record, inserting it in the free_list */
void release_record(Record *element)
{
 extern Record *free_records;
 in_list(&free_records,element);
}
