#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event.h"
#include "record.h"
#include "simtime.h"
#include "math.h"


#define MaxID 1000
#define ContentRate 0.5
#define MinLife 40
#define MaxLife 60
#define CacheSize 30
#define NumPeers 30
#define NumPublish 2
#define NumSearch 3
#define SearchTimeOut 10
#define MaxRelay 2
#define MinContactTime 0.1
#define MaxContactTime 0.5
#define SearchInterval 3  /* avg search interval */
#define DownloadTime 5

#define MinBatches 10 /* Minum number of batches required */
#define MaxBatches 20 /* If confidence interval is not satisfying run up to Max batches */
#define WarmUpSample 5  /* How often to check for the end of warm-up */
#define WarmUpTolerance 0.01 /* % fluctuation of the metric */
#define WarmUpReference 10 /* Chek with the last x samples */
#define RatioBatchWarmUp 5 /* Ratio between the length of the WarmUp and that of a batch */
#define TargetConfidenceInterval 0.05 /* 10% of the avgNumContent */ 

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
int global_memory [MaxID]; // Structure for the peer memory, number of that content in the system
Record * cache[NumPeers]; // Structure for the announcement cache
int cache_counter[NumPeers]; // Counter for the announcement cache
int next_search; //globally unique ID of the next search

Record *searches=NULL; //Structure for the search objects

/* Statistics */
//probability that a search fails
int failed_search, total_search, relayed_search;
//prob that a download fails
int failed_down, total_down;
//distribution of content in the local memory
Time histo_content[MaxID];  //Time spent with that number of contents
Time last_memory_update[NumPeers];
//avg copies of the same content in the system
double area_global_memory;
Time last_global_update;
int total_copies;
//avg downloads in the time unit
int completed_down;
//avg time to complete a search phase
Time total_search_time;
//probability that a content is not in the system
Time time_no_content[MaxID];
Time last_no_content[MaxID];

// MINE: average memory occupancy
double area_memory[NumPeers];
//Prob content is not there when I start the search
int search_should_succeed, started_search;

// RESULTS
typedef struct result_unit Result;
struct result_unit {
	//Aux
	double notExisting; //Prob. content doesn't exist when I start search
	double avgRelayTime; //Avg time to complete a relayed search
	//Metrics
	double avgNumContent; // Avg number of content in memory
	double searchFail; //Prob. search fails
	double downloadFail; //Prob. download fails
	double histoContent[MaxID]; //Distribution of contents in the local memory
	double avgCopies; //Avg copies of the same content (total / MaxID)
	double downloadRate; //Avg downloads in the time unit
	double avgSearchTime; //Avg time to complete a search
	double notInSystem; //Prob. that a content is not in the system
};

Result results[MaxBatches];
Result warmup;
Result avgBatch, squaredSum;

//Tstudent distribution
double tstudent[20];

Time warmup_time;

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
		
		//And in the global memory
		area_global_memory += (double)total_copies/MaxID * (current_time - last_global_update);
		last_global_update = current_time;
		total_copies++;
		if ( global_memory[par->content_ID] == 0 ) {
			//I'm adding a unique content in the system
			time_no_content[par->content_ID] += current_time - last_no_content[par->content_ID];
			//printf("D[%.0f - %.0f] %d is updating %f\n", current_time, last_no_content[par->content_ID], par->content_ID, time_no_content[par->content_ID]);
		}
		global_memory[par->content_ID]++;
		
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
		
		//Update the avg number of copies
		area_global_memory += (double)total_copies/MaxID * (current_time - last_global_update);
		last_global_update = current_time;
		total_copies++;
		if ( global_memory[ID] == 0 ) {
			time_no_content[ID] += current_time - last_no_content[ID];
			//printf("G[%.0f - %.0f] %d is updating %f\n", current_time, last_no_content[ID], ID, time_no_content[ID]);
		}
		global_memory[ID]++;
	  
	  
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
	
	//Global memory
	area_global_memory += (double)total_copies/MaxID * (current_time - last_global_update);
	last_global_update = current_time;
	total_copies--;
	if ( global_memory[par->content_ID] == 1 ) {
		//this is the last one
		last_no_content[par->content_ID] = current_time;
	}
	global_memory[par->content_ID]--;
	
	free(par);
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
	
	//Should the search succeed ?
	started_search++;
	if ( global_memory[wanted_ID] > 0 ) {
		search_should_succeed++;
	}
	 
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
	if ( MaxRelay <= 0 ) {
		//If i don't want to relay this search is failed
		total_search++;
		failed_search++;
		schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
		return;
	}
	int i=0, next_peer;
	Random_Peers rnd;
	rnd.peer = peer;
	rnd.next = NULL;
	Parameters* par[NumSearch];
	for (i=0; i<NumSearch; i++) {
		//Generate a new search
		par[i] = (Parameters*) malloc(sizeof(Parameters));
		par[i]->gen_search = peer;
		par[i]->prev_search = peer;
		par[i]->search_ID = search->key;
		par[i]->content_ID = wanted_ID;
		par[i]->num_relay = MaxRelay-1; //One hop has already been done
		//Take a random peer
		do {
			next_peer = floor(uniform(0, NumPeers, &seme1));
		} while ( is_present(next_peer, &rnd) );
		
		double contact_time = uniform(MinContactTime, MaxContactTime, &seme1);
		schedule(RELAY_SEARCH, current_time + contact_time, next_peer, par[i]);
	}
	
	//Set timeout
	Parameters* search_par = (Parameters*) malloc(sizeof(Parameters));
	search_par->search_ID = search->key;
	schedule(TIMEOUT_SEARCH, current_time + SearchTimeOut, peer, search_par);
	
	return;
}


void relay_search(int peer, Parameters* par) {
	//Do I have it?
	if ( memory[peer][par->content_ID] ) {
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
		
		schedule(END_SEARCH, current_time + uniform(MinContactTime, MaxContactTime, &seme1), par->gen_search, par);
		
		return;
	}

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
		//Search is still considered good
		failed_down++;
		//schedule a new one
		schedule(LOCAL_SEARCH, current_time + negexp(SearchInterval, &seme1), peer, NULL);
		free(par);
	}
	
	//Remove this search from the pending list
	remove_record(&searches, rec);
}

/**
 * At the end of a batch fills produce the statistic results
 * Accepts
 * - Result pointer, write here the results 
 * - Time duration, the whole duration of the batch
 */
void end_batch(Result *res, Time duration) {
 	int i;
 	double total=0.0;
 	for (i = 0 ; i<NumPeers; i++) {
 		//Border effect for memory occupancy and distributions
 		area_memory[i] += num_content[i] * ( current_time - last_memory_update[i]);
 		histo_content[num_content[i]] += current_time - last_memory_update[i];
 		last_memory_update[i] = current_time;
		total += area_memory[i] / duration;
	}
	//Number of copies of the same content (time avg)
	area_global_memory += (double)total_copies/MaxID * (current_time - last_global_update);
	
	//Fill the statistics
	res->avgNumContent = (double)total/NumPeers;
	res->searchFail = (double)failed_search/total_search*100.0;
		res->notExisting = 100.0*(1.0-(double)search_should_succeed/started_search);
	res->downloadFail = (double)failed_down/total_down*100.0;
	//distribution
	total=0.0;
	double total_no_content=0.0;
	for (i=0; i<MaxID; i++) {
		res->histoContent[i] = histo_content[i]/duration/NumPeers;
		total+=res->histoContent[i];
	
		//Border effect for Prob content is not in the system
		if ( global_memory[i] == 0 ) {
			time_no_content[i] += current_time - last_no_content[i];
			last_no_content[i] = current_time;
		}
		total_no_content += time_no_content[i]/MaxID; 
	}
	if (total > 1.0001 || total < 0.9999 ) printf("Ops! distribution different from 1.0: %f", total);
	res->avgCopies = (double)area_global_memory/duration;
	res->downloadRate = (double)completed_down/duration;
	res->avgSearchTime = (double)total_search_time/total_search;
		res->avgRelayTime = (double)total_search_time/relayed_search;
	res->notInSystem = 100.0*total_no_content/duration;
}


/**
 * Start a batch contains the main loop that manages the events
 * Accepts
 * - Result pointer, write here the results for the batch
 * - Time start, when the batch starts (it's not current_time when i'm in warmup) 
 * - Time end, when the batch is expected to end
 * It modifyes the global variable current_time
 */
void start_batch(Result *res, Time start, Time end) {
	Event *ev;
	
	while (current_time<end) {
		ev = get_event(&event_list);
		current_time = ev->time;
		
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
	end_batch(res, current_time-start);
}


void print_batch(Result *res) {
	int i;
	printf("[Batch results at time %f]\n", current_time);
	printf("Avg number of content in memory: %f\n", res->avgNumContent );
	printf("Prob. search fails %f%%\n", res->searchFail );
		printf("\tProb. content doesn't exist: %f%%\n", res->notExisting );
	printf("Prob. download fails %f%%\n", res->downloadFail);
	printf("Distribution of content in the local memory \n\t");
	for (i=0; i<MaxID; i++) {
		printf("%f ", res->histoContent[i]);
	}
	printf("\n");
	printf("Avg copies of the same content: %f\n", res->avgCopies );
	printf("Avg downloads in the time unit (in the system): %f\n", res->downloadRate );
		printf("\tAvg download rate (per peer): %f\n", res->downloadRate/NumPeers );
	printf("Avg time to complete a search: %f\n", res->avgSearchTime );
		printf("\tAvg time for a relayed search: %f\n",  res->avgRelayTime );
	printf("Prob. that a content is not in the system: %f%%\n", res->notInSystem);
	printf("\n\n");
}


void init_variables(void) {
	int i,j;
	//Variables initialization
	next_search=0;
	failed_search=0;
	total_search=0;
	relayed_search=0;
	failed_down=0;
	total_down=0;
	for (i=0; i<MaxID; i++) {
		histo_content[i]=0.0;
	}
	area_global_memory=0.0;
	last_global_update=0.0;
	total_copies=0;
	completed_down=0;
	total_search_time=0.0;
	search_should_succeed=0;
	started_search=0;
	
	current_time = 0.0;
	warmup_time = 0.0;

	
	for (i=0 ; i<NumPeers ; i++){
		num_content[i] = 0;
		cache_counter[i] = 0;
		last_memory_update[i]=0.0;
		global_memory[i]=0;
		time_no_content[i]=0.0;
		last_no_content[i]=0.0;
		area_memory[i]=0.0;
		for (j=0 ; j<MaxID ; j++)
			memory[i][j] = FALSE;
	}
	
	//Fill the tstudent distribution 1%
	tstudent[0] = 63.66;
	tstudent[1] = 9.92;
	tstudent[2] = 5.84;
	tstudent[3] = 4.6;
	tstudent[4] = 4.03;
	tstudent[5] = 3.71;
	tstudent[6] = 3.50;
	tstudent[7] = 3.36;
	tstudent[8] = 3.25;
	tstudent[9] = 3.17;
	tstudent[10] = 3.11;
	tstudent[11] = 3.06;
	tstudent[12] = 3.01;
	tstudent[13] = 2.98;
	tstudent[14] = 2.95;
	tstudent[15] = 2.92;
	tstudent[16] = 2.90;
	tstudent[17] = 2.88;
	tstudent[18] = 2.86;
	tstudent[19] = 2.84;
}


void batch_reset(void) {
	int i;
	//Variables initialization
	//next_search=0;
	failed_search=0;
	total_search=0;
	relayed_search=0;
	failed_down=0;
	total_down=0;
	for (i=0; i<MaxID; i++) {
		histo_content[i]=0.0;
		time_no_content[i]=0.0;
		last_no_content[i]=current_time;
	}
	area_global_memory=0.0;
	last_global_update=current_time;
	//total_copies=0;
	//unique_copies=0;
	completed_down=0;
	total_search_time=0.0;
	search_should_succeed=0;
	started_search=0;
	
	
	for (i=0 ; i<NumPeers ; i++){
		//num_content[i] = 0;
		//cache_counter[i] = 0;
		last_memory_update[i]=current_time;
		//global_memory[i]=0;
		area_memory[i]=0.0;
		//for (j=0 ; j<MaxID ; j++) memory[i][j] = FALSE;
	}
	
	//Remove all pending searches, peers with pending search must be reactivated
	Record* s = out_list(&searches);
	while (s != NULL ) {
		schedule(LOCAL_SEARCH, negexp(SearchInterval, &seme1), s->gen_peer, NULL);
		release_record(s);
		
		//Iterate
		s = out_list(&searches);
	}
}

void average_batches(int Max) {
	int i,j;
	//Reset and compute the averages and the squared sums
	avgBatch.avgNumContent = 0.0;
	avgBatch.searchFail = 0.0;
		avgBatch.notExisting = 0.0;
	avgBatch.downloadFail = 0.0;
	for (j=0; j<MaxID; j++) {
		avgBatch.histoContent[j] = 0.0;
	}
	avgBatch.avgCopies = 0.0;
	avgBatch.downloadRate = 0.0;
	avgBatch.avgSearchTime = 0.0;
		avgBatch.avgRelayTime = 0.0;
	avgBatch.notInSystem = 0.0;
	squaredSum.avgNumContent = 0.0;
	squaredSum.searchFail = 0.0;
		squaredSum.notExisting = 0.0;
	squaredSum.downloadFail = 0.0;
	/* Not for the histograms
	for (j=0; j<MaxID; j++) {
		squaredSum.histoContent[j] = 0.0;
	}*/
	squaredSum.avgCopies = 0.0;
	squaredSum.downloadRate = 0.0;
	squaredSum.avgSearchTime = 0.0;
		squaredSum.avgRelayTime = 0.0;
	squaredSum.notInSystem = 0.0;
	
	for (i=0; i<Max; i++) {
		avgBatch.avgNumContent += results[i].avgNumContent/Max;
		avgBatch.searchFail += results[i].searchFail/Max;
			avgBatch.notExisting += results[i].notExisting/Max;
		avgBatch.downloadFail += results[i].downloadFail/Max;
		for (j=0; j<MaxID; j++) {
			avgBatch.histoContent[j] += results[i].histoContent[j]/Max;
		}
		avgBatch.avgCopies += results[i].avgCopies/Max;
		avgBatch.downloadRate += results[i].downloadRate/Max;
		avgBatch.avgSearchTime += results[i].avgSearchTime/Max;
			avgBatch.avgRelayTime += results[i].avgRelayTime/Max;
		avgBatch.notInSystem += results[i].notInSystem/Max;
	}
	
	//Confidence interval
	for (i=0; i<Max; i++) {
		squaredSum.avgNumContent += pow(avgBatch.avgNumContent-results[i].avgNumContent, 2);
		squaredSum.searchFail += pow(avgBatch.searchFail-results[i].searchFail, 2);
			squaredSum.notExisting += pow(avgBatch.notExisting-results[i].notExisting, 2);
		squaredSum.downloadFail += pow(avgBatch.downloadFail-results[i].downloadFail, 2);
		/* Not for the histograms
		for (j=0; j<MaxID; j++) {
			squaredSum.histoContent[j] += pow(avgBatch.histoContent[j]-results[i].histoContent[j], 2);
		}*/
		squaredSum.avgCopies += pow(avgBatch.avgCopies-results[i].avgCopies, 2);
		squaredSum.downloadRate += pow(avgBatch.downloadRate-results[i].downloadRate, 2);
		squaredSum.avgSearchTime += pow(avgBatch.avgSearchTime-results[i].avgSearchTime, 2);
			squaredSum.avgRelayTime += pow(avgBatch.avgRelayTime-results[i].avgRelayTime, 2);
		squaredSum.notInSystem += pow(avgBatch.notInSystem-results[i].notInSystem, 2);
	}
}

int main()
{
	int i,j;
	//Variables initialization
	init_variables();
	
	for (i=0 ; i<NumPeers ; i++){
		/* Schedule the first GENERATION and SEARCH for all the peers in the system */
		schedule(GENERATION , 0.0, i, NULL);
		schedule(LOCAL_SEARCH, negexp(SearchInterval, &seme1), i, NULL);
	}
	/*
	//Write all the 10sec samples in a file
	FILE* f;
	f=fopen("/tmp/avgNumContent.dat", "w");
	if (f == NULL) {
		fprintf(stderr, "Error opening the file");
		exit(1);
	}
	fprintf(f,"#Average Number of content in the local memory over the time\n0 0");
	*/
	
	//Compute warm up transient (stabilize avgNumContent)
	double avgSample=0.0, relativeVariation=0.0;
	double prevNumContent[WarmUpReference];
	for (i=0; i<WarmUpReference; i++) {
		prevNumContent[i]=0.0;
	}
	
	//boolean decreasing=FALSE;
	i=0;
	do {
		//The warmup always starts at 0.0, but I recursively update the end
		start_batch(&warmup, 0.0, current_time+WarmUpSample);
		
		//Take the last sample of number of content in the system
		prevNumContent[i] = warmup.avgNumContent;
			
		/* Last samples (istantaneous)
		for (j=0; j<NumPeers; j++) {
			prevNumContent[i] += (double)num_content[j]/NumPeers;
		}*/
		
		//Compute the average of the last WarmUpReference samples 
		avgSample = 0.0;
		for (j=0; j<WarmUpReference; j++) {
			avgSample += prevNumContent[j]/WarmUpReference;
		}
		
		
		//difference from total avg and last k samples
		relativeVariation = fabs( (warmup.avgNumContent-avgSample)/warmup.avgNumContent );
		//printf("[%f]Relative Variation: %f\n", current_time, relativeVariation);
		
		//Write to file the current sample
		//fprintf(f,"%.0f %f\n", current_time, warmup.avgNumContent);
		
		//Iterate
		i = (i+1)%WarmUpReference;
		
	} while (relativeVariation > WarmUpTolerance );  //Stop if averages doesn't much too much
	//} while (current_time < 2000 ); exit(1);
	//print_batch(&warmup);
	
	//Here the warmup ends
	warmup_time = current_time;
	printf("Warm Up end at time %f\n", current_time); 
	
	
	int current_batch;
	double confidence_width;
	for (current_batch=0; current_batch<MaxBatches; current_batch++) {
		//Reset all the metrics and start computing batches
		batch_reset();
		
		Time start_current_batch=current_time;
		start_batch(&results[current_batch], start_current_batch, current_time+RatioBatchWarmUp*warmup_time);
		
		//Results of this batch
		printf("Batch: %d\n", current_batch);
		print_batch(&results[current_batch]);
		
		//Check if batches are enough
		if ( current_batch >= MinBatches-1 ) {
			//Confidence interval | current_batch+1 is the number of batches
			average_batches(current_batch+1);
			// width = 2 * t_0.005 * s / sqrt(n)
			confidence_width = 2.0*tstudent[current_batch]*sqrt(squaredSum.avgNumContent/current_batch)/sqrt(current_batch+1);
			
			printf("[%f] Confidence interval width after %d batches: %f\n", current_time, current_batch+1, confidence_width);
			printf("\tRatio: %f\n", confidence_width/avgBatch.avgNumContent);
			
			if ( confidence_width/avgBatch.avgNumContent < TargetConfidenceInterval ) {
				//Stop here
				current_batch++;
				break;
			}
		}
	}
	
	//Average the results for all the collected batches
	average_batches(current_batch);
	printf("\nAverage Batch\n");
	print_batch(&avgBatch);
	printf("Squared Sum\n");
	print_batch(&squaredSum);
	printf("Confidence interval on NumContent: %f\n", confidence_width);
	//fclose(f);
return 1;
}