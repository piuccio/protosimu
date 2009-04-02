#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event.h"
#include "record.h"
#include "simtime.h"
#include "math.h"


#define MaxID 10000
#define ContentRate 2
#define MinLife 50
#define MaxLife 50
#define CacheSize 20
#define NumPeers 20
#define NumPublish 15
#define MinContactTime 0.1
#define MaxContactTime 0.5

typedef struct parameters_set Parameters;

struct parameters_set
  { 
    int key;
    int gen_peer;
    int gen_search;
    int prev_search;
    int search_ID;
    int content_ID;
    int num_relay;
  };

enum {GENERATION, REMOVAL, PUBLISH};
typedef enum {FALSE, TRUE} boolean;
long seme1 = 14123451,
     seme2 =57645805;
Event *event_list = NULL;
Record *queue=NULL;
Record *in_service=NULL;
int num_content[NumPeers]; // Counter for the peer memory
boolean memory [NumPeers][MaxID]; // Structure for the peer memory
Record * cache[NumPeers]; // Structure for the announcement cache
int cache_counter[NumPeers]; // Counter for the announcement cache

double lambda,mu;
int total_users;
double cumulative_time_user;
Time total_delay,last_event_time;
int number_of_samples;
Time current_time;

// Statistical variables
double avg_memory[NumPeers]; // In order to evaluate avg memory occupation
double total_area[NumPeers];
int old_sample[NumPeers];
double last_sample[NumPeers];
Time last_time[NumPeers];

extern double negexp(double,long *);
extern double uniform(double , double , long *);

void schedule(int type, Time time, int peer, Parameters * rec)
{
   Event *ev;
   ev = new_event();
   ev->type = type;
   ev->time = time;
   ev->peer = peer;
   ev->pointer = (char*)rec;
   insert_event(&event_list,ev);
   return;
}

typedef struct Random_Peers {
       int peer;
       struct Random_Peers * next;
} Random_Peers;

boolean is_present (int peer, Random_Peers* list) {
       Random_Peers* l = list;
       while (l != NULL) {
               if (l->peer == peer) {
                       return TRUE;
               }
               l = l->next;
       }
       Random_Peers* new = (Random_Peers*) malloc(sizeof(Random_Peers));
       new->peer = peer;
       new->next = list;
       list = new;
       return FALSE;
}
 


/*
**  Function : void get_input(char *format,void *variable)
**  Return   : None
**  Remarks  : To be used instead of scanf. 'format' is a scanf format,
**             'variable' the pointer to the variable in which the input
**             is written.
*/

void get_input(char *format,void *variable)
{
    static char linebuffer[255];
    char *pin;

    fgets(linebuffer, 255, stdin);	/*  Gets a data from stdin without  */
    pin = strrchr (linebuffer, '\n');	/*  flushing problems		    */
    if (pin!=NULL) *pin = '\0';

    sscanf(linebuffer,format,variable);	/* Read only the beginning of the   */
					/* line, letting the user to write  */
					/* all the garbage he prefer on the */
					/* input line.			    */
}



void generation(int peer){
	
	schedule(GENERATION, current_time + negexp(1.0/ContentRate, &seme1) , peer , NULL );
	
	// Se c'è già l'ID che genero?
	int ID; 
	ID= uniform(0, MaxID, &seme1);
	//printf("peer: %d ID: %d time: %f\n", peer, ID, current_time);
	// Check if the ID has been already generated
	
	if (num_content[peer] < MaxID-1) { // Check if some memory is still available
	  while (memory[peer][ID]) ID = uniform(0, MaxID, &seme1);
	  
	  // Now I can store the content in the memory
	  memory[peer][ID] = TRUE;
	  num_content[peer]++;
	  // Evaluate the lifetime and schedule the removal of the inserted content
	  Parameters * par = (Parameters*) malloc (sizeof(Parameters));
	  par->content_ID = ID; 
	  schedule(REMOVAL, current_time + uniform(MinLife, MaxLife, &seme1), peer, par);
	  
	  // Publish content to other peers
	  
	  Parameters * rec = (Parameters*) malloc (sizeof(Parameters));
	  rec->key = ID;
	  rec->gen_peer = peer;
	  Random_Peers * list = (Random_Peers*) malloc (sizeof(Random_Peers));
	  list->peer = peer;
	  list->next= NULL;
	  int i, rnd_peer;
	  boolean present;
	  for (i=0 ; i<NumPublish; i++){
	  	  present = TRUE;
	  	  while (present){
	  	     rnd_peer = floor(uniform(0,NumPeers,&seme1));
	  	     present = is_present(rnd_peer, list); 
	  	  }
	  	schedule(PUBLISH, current_time + uniform(MinContactTime, MaxContactTime, &seme1), rnd_peer, rec);
	  }
	  // Statistical evaluation: avg number of contents in the queue
	  
	    
	} else printf("Peer %d reaches maximum memory size!!!!\n", peer);
	Time delta_time = current_time - last_time[peer];
	total_area[peer] += 1.0 * old_sample[peer] * delta_time;
	old_sample[peer] = num_content[peer];
	last_time[peer] = current_time;
	if (peer==0) {
	 //printf("delta_time: %f old_sample: %d current: %f \n", delta_time, old_sample[peer], current_time);
	}
	
	
	
}

void removal(Parameters* par, int peer){
	memory[peer][par->content_ID] = FALSE;
	num_content[peer]--;
	Time delta_time = current_time - last_time[peer];
	total_area[peer] += 1.0 * old_sample[peer] * delta_time;
	old_sample[peer] = num_content[peer];
	last_time[peer] = current_time;
	//printf("Remove ID: %d peer: %d\n",par->content_ID, peer );
	return;
}

void publish(Parameters *par, int peer){
	Record * rec;
	rec=new_record();
	rec->gen_peer = par->gen_peer;
	rec->key = par->key;

	
	// Insert the record in the queue (announcement cache)
	in_list(&cache[peer], rec);
	if (cache_counter[peer] < CacheSize){
		cache_counter[peer]++;
	} else { //Full cache... free the last element
		//printf("CACHE PIENA NEL PEER: %d\n", peer);
		release_record(out_list(&cache[peer]));
	}
	
	
	return;
	
}

void arrival(void)
{ Time delta;
  Record *rec;
  delta = negexp(1.0/lambda,&seme1);
  //schedule(ARRIVAL,current_time+delta);
  rec = new_record();
  rec->arrival = current_time;
  cumulative_time_user+=total_users*(current_time-last_event_time);
  total_users++;
  if (in_service == NULL)
    {
      in_service = rec;
      //schedule(DEPARTURE,current_time+negexp(1/mu,&seme2));
    }
  else
    in_list(&queue,rec);
  return;
}

void departure(void)
{
  Record *rec;
  rec = in_service;
  in_service = NULL;
  cumulative_time_user+=total_users*(current_time-last_event_time);
  total_users--;
  number_of_samples++;
  total_delay+=current_time - rec->arrival;
  release_record(rec);
  if (queue!=NULL)
   {
    in_service = out_list(&queue);
    //schedule(DEPARTURE,current_time+negexp(1.0/mu,&seme2));
   }
  return;
}



void results(void)
{
	/*
 cumulative_time_user+=total_users*(current_time-last_event_time);
 printf("End time %f\n",current_time);
 printf("Number of services %d\n",number_of_samples);
 printf("Average delay     %f\n",total_delay/number_of_samples);
 printf("Theoretical delay %f\n",1.0/(mu-lambda));
 printf("Average number of users     %f\n",cumulative_time_user/current_time);
 printf("Theoretical number of users %f\n",lambda/(mu-lambda)); */
 printf("Terminato!!!!");
 int i;
 for (i = 0 ; i<NumPeers; i++) {
 	int delta_time = current_time - last_time[i];
	total_area[i] += 1.0 * old_sample[i] * delta_time;
	old_sample[i] = num_content[i];
	last_time[i] = current_time;
	
 	printf("Peers: %d, Avg occupation: %f area: %f current time: %f\n", i, total_area[i] / current_time, total_area[i], current_time  );
 }
 exit(0);
}

int main()
{
 Event *ev;
 Time maximum;
 int i,j;
 
 cumulative_time_user =0.0;
 total_delay=0.0;
 last_event_time=0.0;
 number_of_samples=0.0;
  
  // Variables initialization
 for (i=0 ; i<NumPeers ; i++){
 	num_content[i] = 0;
 	cache_counter[i] = 0;
 	avg_memory[i] = 0;
 	total_area[i] = 0;
 	old_sample[i] = 0;
 	last_time[i] = 0; 
 	for (j=0 ; j<MaxID ; j++)
 		memory[i][j] = FALSE;
 }
 
  
 current_time = 0.0;
 /*total_users = 0;
 printf("Insert the value of lambda: ");
 get_input("%lf",&lambda);
 printf("lambda = %f\n",lambda);
 printf("Insert the value of mu: ");
 get_input("%lf",&mu);
 printf("mu     = %f\n",mu);
 */
 printf("Insert the maximum simulation time: ");
 get_input("%lf",&maximum);
 printf("Max Time  = %f\n",maximum);
/* Schedule the first GENERATION for all the peers in the system 
 */
 
 
 for (i=0; i<NumPeers; i++){
 	//printf("generazione\n");
 	schedule(GENERATION , negexp(1.0/ContentRate, &seme1) , i , NULL);
 	//negexp(1.0/ContentRate ,&seme1)
 }

// schedule(ARRIVAL,negexp(1.0/lambda,&seme1));

 while (current_time<maximum)
  {
    ev = get_event(&event_list);
    last_event_time = current_time;
    current_time = ev->time;
     //printf(".\n");
    
    //printf("Evento: %d, tempo: %f\n", ev->type, current_time);
    switch (ev->type)
     {
      
       case GENERATION:  generation(ev->peer);
		      break;
       case REMOVAL: removal((Parameters*)ev->pointer, ev->peer);
              free(ev->pointer);
		      break;
	   case PUBLISH: publish((Parameters*)ev->pointer, ev->peer);
	          Parameters* par_debug = (Parameters*)ev->pointer;
	   		  //printf("gen_peer: %d ID: %d peer: %d \n", par_debug->gen_peer, par_debug->key, ev->peer);	
		      break;	      
       default:       printf("The Horror! The Horror!\n");
		      exit(1);
     }
    release_event(ev);
  }
 results();
 return 0;
}
