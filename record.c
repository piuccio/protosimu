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
 if (free_records==NULL)
  {
    if ((p=(Record *)malloc(sizeof(Record)))==NULL)
         { printf("Error  : Memory allocation error\n");
           exit(1);
         }
    return(p);
  }
 else
    return(out_list(&free_records));
}

/* Releases a record, inserting it in the free_list */
void release_record(Record *element)
{
 extern Record *free_records;
 in_list(&free_records,element);
}

/* Search a record with key 'key' */
Record *search_record(Record **last,int key)
{
  Record *p;
  
  if ((*last)==NULL) return NULL;

  if ((*last)->next==(*last))
   {
     /* Only one record in the list */
     if ((*last)->key==key)
        return (*last);
     else
        return NULL;
   }
  else
   {
     p = (*last);
     do
     {
       p = p->next;
     }
     while (p->key!=key && p!=(*last));

  if (p==(*last) && p->key!=key)
   return NULL;
  else
   return p;
   }
}

/* Remove the record *elem (that must exist) from the list */
void remove_record(Record **last,Record *elem)
{
  Record *p;

  if (elem==NULL) return;
  if ((*last)==NULL) return;

  if ((*last)->next==(*last))
   {
     /* Only one record in the list */
     if ((*last)==elem)
      {
        (*last)=NULL;
      }
     else
      {
        printf("Error 1\n");
      }
   }
  else
   {
     p = (*last);
     do
      {
        p = p->next;
      }
     while (p->next!=elem && p!=(*last));
     
     if (p==(*last) && p->next!=elem) 
      {
      
        printf("AAA\n");
        return;
      }
     p->next = elem->next;
     if ((*last)==elem)
      {
        (*last) = p;
      }

   }

  /* Remove the any record reference to the list */
   elem->next = NULL;
  
  return;
}