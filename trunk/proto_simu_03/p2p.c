#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event.h"
#include "record.h"
#include "simtime.h"
#include "math.h"


#define MaxID 1000
#define ContentRate 1
#define MinLife 40
#define MaxLife 60
#define CacheSize 20
#define NumPeers 30
#define NumPublish 5
#define NumSearch 2
#define SearchTimeOut 15
#define MaxRelay 4
#define MinContactTime 0.1 /* secs */
#define MaxContactTime 0.5 /* secs */
#define SearchInterval 2  /* avg search interval, secs */
#define DownloadTime 5

typedef struct parameters_set Parameters;

struct parameters_set
  { 
    int key;
    int gen_peer; /* Who generated the content */
    int gen_search; /* Who generated the result */
    int prev_search; /* Who sent me the search */
    int search_ID; /* Global ID of the search */
    int content_ID; /* ID of the content (generated or searched) */
    int num_relay; /* Number of relays I can do, 0 stop relaying */
  };

enum {GENERATION, REMOVAL, PUBLISH, LOCAL_SEARCH, RELAY_SEARCH, END_SEARCH, TIMEOUT_SEARCH, END_DOWNLOAD};
typedef enum {FALSE, TRUE} boolean;

long seme1 = 14123451;
Event *event_list = NULL;
int num_content[NumPeers]; // Counter for the peer memory
boolean memory [NumPeers][MaxID]; // Structure for the peer memory
Record * cache[NumPeers]; // Structure for the announcement cache
int cache_counter[NumPeers]; // Counter for the announcement cache

Record *searches; //Structure for the search objects

//double lambda,mu;
int total_users;
double cumulative_time_user;
Time total_delay,last_event_time;
int number_of_samples;
Time current_time;
int last_search, total_search, good_search, completed_search;
int good_download, bad_download;

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
	struct Random_Peers *next;
} Random_Peers;

boolean is_present (int peer, Random_Peers* list) {
	Random_Peers* l = list;
	Random_Peers* last;
	while (l != NULL) {
		if (l->peer == peer) {
			return TRUE;
		}
		if ( l->next == NULL) last = l;
		l = l->next;
	}
	Random_Peers* new = (Random_Peers*) malloc(sizeof(Random_Peers));
	new->peer = peer;
	new->next = NULL;
	last->next = new;
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

/* Procedure to publish content to other peers
 * The parameters are the ID to publish and the peer generating the publishment */

void publish_procedure(int ID, int peer){
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
}

void end_of_download(int peer, Parameters * par){
	// Update the statistics
	printf("Download ID: %d, from peer: %d \n", par->content_ID, peer);
	// Check if already in memory (i.e. generation during download time)
	if (!memory[peer][par->content_ID]){ // if present do nothing
		memory[peer][par->content_ID] = TRUE; // Save in the local memory
		// Publish procedure
		publish_procedure(par->content_ID, peer);
	}
	
	// Schedule new search
	schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1) , peer, NULL);
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
	  publish_procedure(ID, peer);
	  
	  // Statistical evaluation: avg number of contents in the queue
	  
	    
	} else printf("Peer %d reaches maximum memory size!!!!\n", peer);
	Time delta_time = current_time - last_time[peer];
	total_area[peer] += 1.0 * old_sample[peer] * delta_time;
	old_sample[peer] = num_content[peer];
	last_time[peer] = current_time;
	
	
	
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


void local_search(int peer) {
	total_search++;
	//What am I looking for ?
	int wanted_ID = uniform(0, MaxID, &seme1);
	printf("[%f] Searching %d\n", current_time, wanted_ID);
	
	//Do I have it ?
	if ( memory[peer][wanted_ID] ) {
		completed_search++;
		good_search++;
		
		schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
		return;
	}
	
	//Is it in the cache?
	Record *rec = search_record( &cache[peer], wanted_ID);
	if ( rec != NULL ) {
		//Good search
		completed_search++;
		good_search++;
		//Start download
		if ( memory[rec->gen_peer][wanted_ID] ) {
			good_download++;
			Parameters* par = (Parameters*) malloc(sizeof(Parameters));
			par->content_ID = wanted_ID;
			schedule(END_DOWNLOAD, current_time + negexp(DownloadTime, &seme1), peer, par);
		} else {
			bad_download++;
			schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
		}
		return;
	}
	
	//Generate a search record
	Record* search = new_record();
	search->key = last_search++;
	search->arrival = current_time;
	search->gen_peer = peer;
	// Store the pending search
	in_list(&searches, search);
	
	//Relay the search
	int i=0, next_peer;
	Random_Peers rnd;
	rnd.peer = peer;
	rnd.next = NULL;
	Parameters* par[NumSearch];
	for (i=0; i<=NumSearch; i++) {
		//Generate a new search
		par[i] = (Parameters*) malloc(sizeof(Parameters));
		par[i]->gen_search = peer;
		par[i]->prev_search = peer;
		par[i]->search_ID = search->key;
		par[i]->content_ID = wanted_ID;
		par[i]->num_relay = MaxRelay;
		//Take a random peer
		do {
			next_peer = floor(uniform(0, NumPeers, &seme1));
			printf(".%d ", next_peer);
		} while ( is_present(next_peer, &rnd) );
		
		double contact_time = uniform(MinContactTime, MaxContactTime, &seme1);
		schedule(RELAY_SEARCH, current_time + contact_time, next_peer, par[i]);
		//printf("Relay to %d\n", next_peer);
	}
	
	//Set timeout
	Parameters* search_par = (Parameters*) malloc(sizeof(Parameters));
	search_par->search_ID = search->key;
	schedule(TIMEOUT_SEARCH, current_time + SearchTimeOut, peer, search_par);
	
	return;
}


void relay_search(int peer, Parameters* par) {
	printf("Searching %d at relay %d from %d hop count %d\n", par->content_ID, peer, par->gen_search, par->num_relay);
	
	//Do I have it?
	if ( memory[peer][par->content_ID] ) {
		printf("%d has %d\n", peer, par->content_ID);
		//Tell to who started the search
		par->gen_peer = peer;
		schedule(END_SEARCH, current_time + uniform(MinContactTime, MaxContactTime, &seme1), par->gen_search, par);
		return; 
	}
	
	//Is it in the cache?
	Record *rec = search_record( &cache[peer], par->content_ID);
	if ( rec != NULL ) {
		//Tell him
		par->gen_peer = rec->gen_peer;
		
		//printf("%d cached %d owned by %d\n", peer, par->content_ID, par->gen_peer);
		schedule(END_SEARCH, current_time + uniform(MinContactTime, MaxContactTime, &seme1), par->gen_search, par);
		
		return;
	}
	printf("Searching %d at relay %d from %d hop count %d\n", par->content_ID, peer, par->gen_search, par->num_relay);
	//Keep relaying
	if ( par->num_relay > 0 ) {
		int i=0, next_peer;
		Random_Peers* rnd = (Random_Peers*)malloc(sizeof(Random_Peers));
		rnd->peer = peer;
		rnd->next = NULL;
		//Exclude who generated the search
		is_present(par->gen_search, rnd);
		//exclude who sent the search
		is_present(par->prev_search, rnd);
		//Update this search
		par->prev_search = peer;
		par->num_relay--;
		
		for (i=0; i<NumSearch; i++) {
			//Take a random peer
			do {
				next_peer = floor(uniform(0, NumPeers, &seme1));
			} while ( is_present(next_peer, rnd) );
			
			schedule(RELAY_SEARCH, current_time + uniform(MinContactTime, MaxContactTime, &seme1), next_peer, par);
		}
	}
}


void timeout_search(int peer, Parameters* par ) {
	printf("[%f] Timeout expired", current_time);
	Record* search = search_record(&searches, par->search_ID);
	if (search==NULL) {
		printf(" do nothing\n");
		//I already replyed to this search
		return;
	}
	completed_search++;
	printf(" schedule new\n");
	//Remove the record
	remove_record(&searches, search);
	
	//Schedule next search
	schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
	
	return; 
}

void end_search(int peer, Parameters* par) {
	Record* rec = search_record(&searches, par->search_ID);
	if ( rec == NULL ) {
		printf("Banana \n");
		//This search is not pending, either is completed or timedout
		return;
	}
	
	//Is the content still there
	if ( memory[par->gen_peer][par->content_ID] ) {
		//Update stats
		completed_search++;
		good_search++;
		
		//Start the download
		schedule(END_DOWNLOAD, current_time + negexp(DownloadTime, &seme1), peer, par);
	} else {
		//Search failed
		completed_search++;
		//schedule a new one
		schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
	}
	
	//Remove this search from the pending
	remove_record(&searches, rec);
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
  */
 printf("Terminato!!!!\n");
 int i;
 for (i = 0 ; i<NumPeers; i++) {
 	int delta_time = current_time - last_time[i];
	total_area[i] += 1.0 * old_sample[i] * delta_time;
	old_sample[i] = num_content[i];
	last_time[i] = current_time;
	
 	printf("Peers: %d, Avg occupation: %f area: %f current time: %f\n", i, total_area[i] / current_time, total_area[i], current_time  );
 }
 printf("Theoretical number of content %f\n",(MinLife+MaxLife)/2.0*ContentRate);
 exit(0);
}

int main()
{
	Event *ev;
	Time maximum;
	int i,j;

	last_search=0;
	total_search=0;
	good_search=0;
	completed_search=0;
	good_download=0;
	bad_download=0;
  
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
 
	//printf("Insert the maximum simulation time: ");
	//get_input("%lf",&maximum);
	maximum=1000;
	printf("Max Time  = %f\n",maximum);
	
	/* Schedule the first GENERATION and SEARCH for all the peers in the system */
	for (i=0; i<NumPeers; i++){
		schedule(GENERATION , negexp(1.0/ContentRate, &seme1), i, NULL);
		schedule(LOCAL_SEARCH, negexp(SearchInterval, &seme1), i, NULL);
	}
	
	while (current_time<maximum)
	{
		ev = get_event(&event_list);
		last_event_time = current_time;
		//printf("Evento: %d, tempo: %f\n", ev->type, current_time);
		current_time = ev->time;
		
		
		switch (ev->type)
		{
			case GENERATION:  generation(ev->peer);
				break;
			case REMOVAL: removal((Parameters*)ev->pointer, ev->peer);
				free(ev->pointer);
				break;
			case PUBLISH: publish((Parameters*)ev->pointer, ev->peer);
				break;
			case LOCAL_SEARCH: local_search(ev->peer);
				break;
			case RELAY_SEARCH: relay_search(ev->peer, (Parameters*)ev->pointer );
				break;
			case TIMEOUT_SEARCH: timeout_search(ev->peer, (Parameters*)ev->pointer );
				break;
			case END_SEARCH: end_search(ev->peer, (Parameters*)ev->pointer );
				break;
			case END_DOWNLOAD: end_of_download(ev->peer, (Parameters*)ev->pointer );
				break;
			default: printf("[%d] The Horror! The Horror!\n", ev->type);
		      exit(1);
     }
    release_event(ev);
  }
 results();
 return 0;
}
