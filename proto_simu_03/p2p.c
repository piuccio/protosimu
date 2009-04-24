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
boolean global_memory [MaxID]; // Structure for the peer memory
Record * cache[NumPeers]; // Structure for the announcement cache
int cache_counter[NumPeers]; // Counter for the announcement cache
int next_search; //globally unique ID of the next search

Record *searches; //Structure for the search objects

/* Statistics */
//probability that a search fails
int failed_search, total_search, relayed_search;
//prob that a download fails
int failed_down, total_down;
//distribution of content in the local memory
Time histo_content[MaxID];  //Time spent with that number of contents
Time last_memory_update[NumPeers];
//avg copies of the same content in the system
int generated_content, unique_content;
//avg downloads in the time unit
int completed_down;
//avg time to complete a search phase
Time total_search_time;
//probability that a content is not in the system
//???
// MINE: average memory occupancy
double area_memory[NumPeers];


Time current_time;


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
	// Generate the message
	Parameters * rec = (Parameters*) malloc (sizeof(Parameters));
	rec->content_ID = ID;
	rec->gen_peer = peer;
	// Extract the list of peers
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
	completed_down++;
	
	//printf("Download ID: %d, from peer: %d \n", par->content_ID, peer);
	// Check if already in memory (i.e. generation during download time)
	if (!memory[peer][par->content_ID]){ // if present do nothing
		// Save in the local memory
		memory[peer][par->content_ID] = TRUE;
		//Update memory distribution
		histo_content[num_content[peer]] += current_time - last_memory_update[peer];
		//and memory occupancy
		area_memory[peer] += num_content[peer] * (current_time - last_memory_update[peer]);
		last_memory_update[peer] = current_time; 
		num_content[peer]++;
		
		// Publish procedure
		publish_procedure(par->content_ID, peer);
		
		//Schedule the removal
		schedule(REMOVAL, current_time + uniform(MinLife, MaxLife, &seme1), peer, par);
	}
	
	// Schedule new search
	schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1) , peer, NULL);
}

void generation(int peer){
	
	schedule(GENERATION, current_time + negexp(1.0/ContentRate, &seme1) , peer , NULL );
	
	// Generate always a new content
	int ID;
	// Check if some memory is still available
	if (num_content[peer] < MaxID) {
		do {
			ID = uniform(0, MaxID, &seme1);
		} while (memory[peer][ID]); 
		  
		// Now I can store the content in the memory
		memory[peer][ID] = TRUE;
		//update histogram
		histo_content[num_content[peer]] += current_time - last_memory_update[peer];
		//and memory occupancy
		area_memory[peer] += num_content[peer] * ( current_time - last_memory_update[peer] );
		last_memory_update[peer] = current_time;
		num_content[peer]++;
		
		//Check if it is unique in the system
		if ( global_memory[ID] == FALSE ) {
			unique_content++;
			global_memory[ID] = TRUE;
		}
		generated_content++;
	  
	  
		// Evaluate the lifetime and schedule the removal of the inserted content
		Parameters * par = (Parameters*) malloc (sizeof(Parameters));
		par->content_ID = ID; 
		schedule(REMOVAL, current_time + uniform(MinLife, MaxLife, &seme1), peer, par);
		
		// Publish content to other peers
		publish_procedure(ID, peer);
	    
	} else printf("Peer %d reaches maximum memory size!!!!\n", peer);
	
}

void removal(Parameters* par, int peer){
	// Remove the ID
	memory[peer][par->content_ID] = FALSE;
	// Update the memory
	histo_content[num_content[peer]] += current_time - last_memory_update[peer];
	//and memory occupancy
	area_memory[peer] += num_content[peer] * ( current_time - last_memory_update[peer] );
	last_memory_update[peer] = current_time;
	num_content[peer]--;
	
	//printf("Remove ID: %d peer: %d\n",par->content_ID, peer );
	//free(par);
	return;
}

void publish(Parameters *par, int peer){
	Record * rec;
	rec=new_record();
	rec->gen_peer = par->gen_peer;
	rec->key = par->content_ID;

	
	// Insert the record in the announcement cache
	in_list(&cache[peer], rec);
	if (cache_counter[peer] < CacheSize){
		cache_counter[peer]++;
	} else {
		//Cache full... free the last element
		release_record(out_list(&cache[peer]));
	}
	
	return;
}


void local_search(int peer) {
	//What am I looking for ?
	int wanted_ID = uniform(0, MaxID, &seme1);
	//printf("[%f] Searching %d\n", current_time, wanted_ID);
	
	//Do I have it ?
	if ( memory[peer][wanted_ID] ) {
		//Search successful, a new search can start
		total_search++;
		schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
		return;
	}
	
	//Is it in the cache?
	Record *rec = search_record( &cache[peer], wanted_ID);
	if ( rec != NULL ) {
		//Good search
		total_search++;
		
		//Start download
		total_down++;
		if ( memory[rec->gen_peer][wanted_ID] ) {
			Parameters* par = (Parameters*) malloc(sizeof(Parameters));
			par->content_ID = wanted_ID;
			schedule(END_DOWNLOAD, current_time + negexp(DownloadTime, &seme1), peer, par);
		} else {
			failed_down++;
			//Search is over
			schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
		}
		return;
	}
	
	//Generate a search record
	Record* search = new_record();
	search->key = next_search;
	next_search++;
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
			//printf(".%d ", next_peer);
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
	//printf("Searching %d at relay %d from %d hop count %d\n", par->content_ID, peer, par->gen_search, par->num_relay);
	
	//Do I have it?
	if ( memory[peer][par->content_ID] ) {
		//printf("%d has %d\n", peer, par->content_ID);
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
	//printf("Searching %d at relay %d from %d hop count %d\n", par->content_ID, peer, par->gen_search, par->num_relay);
	//Keep relaying
	Parameters* new_par[NumSearch];
	if ( par->num_relay > 0 ) {
		int i=0, next_peer;
		Random_Peers* rnd = (Random_Peers*)malloc(sizeof(Random_Peers));
		rnd->peer = peer;
		rnd->next = NULL;
		//Exclude who generated the search
		is_present(par->gen_search, rnd);
		//exclude who sent the search
		is_present(par->prev_search, rnd);
		
		for (i=0; i<NumSearch; i++) {
			//Take a random peer
			do {
				next_peer = floor(uniform(0, NumPeers, &seme1));
			} while ( is_present(next_peer, rnd) );
			
			//Generate a new search
			new_par[i] = (Parameters*) malloc(sizeof(Parameters));
			new_par[i]->gen_search = par->gen_search;
			new_par[i]->prev_search = peer;
			new_par[i]->search_ID = par->search_ID;
			new_par[i]->content_ID = par->content_ID;
			new_par[i]->num_relay = par->num_relay-1;
			schedule(RELAY_SEARCH, current_time + uniform(MinContactTime, MaxContactTime, &seme1), next_peer, new_par[i]);
		}
	}
	//I don't need this parameters anymore
	free(par);
}


void timeout_search(int peer, Parameters* par ) {
	//printf("[%f] Timeout expired", current_time);
	Record* search = search_record(&searches, par->search_ID);
	if (search==NULL) {
		//I already replyed to this search because it's not in the pending list
		return;
	}
	
	//I've got no answer for this content
	total_search++;
	failed_search++;

	//Remove the record
	remove_record(&searches, search);
	
	//Schedule next search
	schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
	
	free(par);
	return; 
}

void end_search(int peer, Parameters* par) {
	Record* rec = search_record(&searches, par->search_ID);
	if ( rec == NULL ) {
		//This search is not pending, either is completed or timed-out
		return;
	}
	
	total_search++;
	relayed_search++;
	total_search_time += current_time - rec->arrival;
	//Is the content still there
	if ( memory[par->gen_peer][par->content_ID] ) {
		//Start the download
		total_down++;
		schedule(END_DOWNLOAD, current_time + negexp(DownloadTime, &seme1), peer, par);
	} else {
		//Search failed
		//failed_search++;
		failed_down++;
		//schedule a new one
		schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
	}
	
	//Remove this search from the pending list
	remove_record(&searches, rec);
	free(par);
}

void results(void)
{
 	int i;
 	double total=0.0;
 	for (i = 0 ; i<NumPeers; i++) {
 		area_memory[i] += num_content[i] * ( current_time - last_memory_update[i]);
 		histo_content[num_content[i]] += current_time - last_memory_update[i];
		//printf("Peer: %d, Avg occupation: %f area: %f current time: %f\n", i, area_memory[i] / current_time, area_memory[i], current_time);
		total += area_memory[i] / current_time;
	}
	//How many downloads each peer has in one second
	printf("Avg number of content in memory: %f\n", total / NumPeers );
	double DownloadRate = (double)completed_down/current_time/NumPeers;
	printf("\tTheoretical including download rate %f\n",(MinLife+MaxLife)/2.0*(ContentRate+DownloadRate));
	printf("\tTransient period %f\n",(MinLife+MaxLife)/2.0*(ContentRate+DownloadRate)/2.0/current_time);
	
printf("Prob. search fails %f%%\n", (double)failed_search/total_search*100.0);
	printf("\tTheoretical : %f\n",0.0);
printf("Prob. download fails %f%%\n", (double)failed_down/total_down*100.0);
printf("Distribution of content in the local memory \n\t");

total=0.0;
for (i=0; i<MaxID; i++) {
	printf("%f ", histo_content[i]/current_time/NumPeers);
	total+=histo_content[i]/current_time/NumPeers;
}
printf("\n\tTotal : %f\n", total);

printf("Avg copies of the same content: %f\n", (double)generated_content/unique_content);
printf("Avg downloads in the time unit: %f\n", (double)completed_down/current_time);
printf("Avg time to complete a search: %f\n", total_search_time/total_search);
printf("Avg time to complete a relayed search: %f \n", total_search_time/relayed_search);
printf("Prob. that a content is not in the system: ???\n");


	exit(0);
}

int main()
{
	Event *ev;
	Time maximum;
	int i,j;

	failed_search=0;
	total_search=0;
	failed_down=0;
	total_down=0;
	for (i=0; i<MaxID; i++) {
		histo_content[i]=0;
	}
	generated_content=0;
	unique_content=0;
	completed_down=0;
	total_search_time=0;
	next_search=0;
	
	current_time = 0.0;
	
	
	// Variables initialization
	for (i=0 ; i<NumPeers ; i++){
		num_content[i] = 0;
		cache_counter[i] = 0;
		last_memory_update[i]=0.0;
		global_memory[i]=FALSE;
		for (j=0 ; j<MaxID ; j++)
			memory[i][j] = FALSE;
			
		/* Schedule the first GENERATION and SEARCH for all the peers in the system */
		schedule(GENERATION , negexp(1.0/ContentRate, &seme1), i, NULL);
		schedule(LOCAL_SEARCH, negexp(SearchInterval, &seme1), i, NULL);
	}

	
 
	//printf("Insert the maximum simulation time: ");
	//get_input("%lf",&maximum);
	maximum=2000.0;
	printf("Max Time  = %f\n",maximum);
	
	
	while (current_time<maximum)
	{
		ev = get_event(&event_list);
		//last_event_time = current_time;
		current_time = ev->time;
		//printf("[%f] Evento: %d, Peer: %d\n", current_time, ev->type, ev->peer);
		
		switch (ev->type)
		{
			case GENERATION: generation(ev->peer);
				break;
			case REMOVAL: removal((Parameters*)ev->pointer, ev->peer);
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