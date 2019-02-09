// Simulation of the Hertz airport shuttle bus, which picks up passengers
// from the airport terminal building going to the Hertz rental car lot.

#include <iostream>
#include "cpp.h"
#include <string.h>
using namespace std;

#define NUM_SEATS 6      // number of seats available on shuttle
#define TINY 1.e-20      // a very small time period
#define TERMNL 0         // named constants for labelling event set
#define CARLOT 1

facility_set *buttons;  // customer queues at each stop
facility *rest;           // dummy facility indicating an idle shuttle



mailbox_set *hop_on;  // invite one customer to board at this stop
event_set *boarded;             // one customer responds after taking a seat
mailbox_set *get_off_now;  // all customers can get off shuttle
mailbox_set *got_off;  // all customers can get off shuttle

mailbox *shuttle_called; // call buttons at each location

facility_set *placeCurbs;

string places[2] = {"Terminal", "CarLot"}; // where to generate

void make_passengers(long whereami);       // passenger generator
long group_size();

void passenger(long whoami);                // passenger trajectory
string people[2] = {"arr_cust","dep_cust"}; // who was generated

void shuttle(long id );                  // trajectory of the shuttle bus consists of...
void loop_around_airport(long & seats_used, long &id);      // ... repeated trips around airport
void load_shuttle(long whereami, long & on_board, long &id); // posssibly loading passengers
qtable shuttle_occ("bus occupancy");  // time average of how full is the bus

int t, s, m;

extern "C" void sim()      // main process
{
  cout << "Number of terminals: "; 
  cin >> t; 
  cout << "Number of shuttle busses: "; 
  cin >> s; 
  cout << "Interarrival mean time: "; 
  cin >> m;
  
  //0 = carlot, 1->t = terminals
  buttons = new facility_set("Curb",t+1);
  rest = new facility("rest");
  get_off_now = new mailbox_set("get_off_now", s); // each shuttle has a mailbox for get off now
  got_off = new mailbox_set("got_off", s); // each shuttle has a mailbox to see if they get off
  hop_on = new mailbox_set("board shuttle", t+1); 
  boarded = new event_set("boarded", s);
  shuttle_called = new mailbox("call button");
  placeCurbs = new facility_set("shuttle spots", t+1);

  places = new string[t+1];
  places[0] = "Car lot";
  for(int i = 1; i < t+1; i++) {
    places[i] = "Terminal " + to_string(i);
  }
  
  
	
  create("sim");
  shuttle_occ.add_histogram(NUM_SEATS+1,0,NUM_SEATS);
  
  for(int i = 0; i < t+1; i++) {
    make_passengers(i);  // generate streams of customers
  }
  for(int i = 0; i < s; i++) {
    shuttle(i);  // create a single shuttle
  }
  hold (1440);              // wait for a whole day (in minutes) to pass
  report();
  status_facilities();
}

// Model segment 1: generate groups of new passengers at specified location

void make_passengers(long whereami)
{
  const char* myName=places[whereami].c_str(); // hack because CSIM wants a char*
  create(myName);

  while(clock < 1440.)          // run for one day (in minutes)
  {
    hold(expntl(m));           // exponential interarrivals, mean 10 minutes
    long group = group_size();
    for (long i=0;i<group;i++)  // create each member of the group
      passenger(whereami);      // new passenger appears at this location
  }
}

// Model segment 2: activities followed by an individual passenger

void passenger(long whoami)
{
  const char* myName=people[whoami].c_str(); // hack because CSIM wants a char*
  long wheretogo = -1;
  long myShuttleID = -1;
  long dest = -2;
  while(wheretogo == -1 && wheretogo == whoami && wheretogo == whoami + 1) 
    wheretogo = uniform(0, t);

  create(myName);
	

  (*buttons)[whoami].reserve();     // join the queue at my starting location
  shuttle_called->send(whoami);  // head of queue, so call shuttle
  (*hop_on)[whoami].receive((long*) &myShuttleID);        // wait for shuttle and invitation to board
  hold(uniform(0.5,1.0));        // takes time to get seated
  (*boarded)[myShuttleID].set();                 // tell driver you are in your seat
  (*buttons)[whoami].release();     // let next person (if any) access button
  while(dest != wheretogo) {
    (*get_off_now)[myShuttleID].receive((long*) &dest);            // everybody off when shuttle reaches next stop
    if(dest == wheretogo)
      (*got_off)[myShuttleID].send((long)"yes");
    else
      (*got_off)[myShuttleID].send((long)"no");
  }
  
}

// Model segment 3: the shuttle bus

void shuttle(long id) {
  create ("shuttle");
  long who_pushed = -1;
  while(1) {  // loop forever
    // start off in idle state, waiting for the first call...
    
    rest->reserve();                   // relax at garage till called from somewhere
    shuttle_called->receive((long*) &who_pushed);
    rest->release();                   // and back to work we go!

    long seats_used = 0;              // shuttle is initially empty
    shuttle_occ.note_value(seats_used);

    hold(2);  // 2 minutes to reach car lot stop

    // Keep going around the loop until there are no calls waiting
    while (who_pushed != -1 && seats_used > 0)
      loop_around_airport(seats_used, id);
  }
}

long group_size() {  // calculates the number of passengers in a group
  double x = prob();
  if (x < 0.3) return 1;
  else {
    if (x < 0.7) return 2;
    else return 5;
  }
}

void loop_around_airport(long &seats_used, long &id) { // one trip around the airport
  // Start by picking up departing passengers at car lot
  long didGetOff;
  
  
  
  for(int i = 0; i < t+1; i++) { // loop through all places
    (*placeCurbs)[i].reserve();
    
    load_shuttle(i, seats_used, id);
    shuttle_occ.note_value(seats_used);
  
  
  
    // drop off all departing passengers at airport terminal
    for(int j = 0; j < seats_used; j++) {
      (*get_off_now)[id].send(i); // open door and let them off
      (*got_off)[id].receive((long*) &didGetOff);
      if(didGetOff == (long)"yes")
    	  seats_used = seats_used - 1;
    }
    shuttle_occ.note_value(seats_used);

    (*placeCurbs)[i].release();
    hold (uniform(3,5));  // drive to next airport terminal or rest
  }
  // Back to starting point. Bus is empty. Maybe I can rest...
}

void load_shuttle(long whereami, long &on_board, long &id)  // manage passenger loading
{
  // invite passengers to enter, one at a time, until all seats are full
  while((on_board < NUM_SEATS) &&
    ((*buttons)[whereami].num_busy() + (*buttons)[whereami].qlength() > 0))
  {
    (*hop_on)[whereami].send(id);// invite one person to board
    (*boarded)[id].wait();  // pause until that person is seated
    on_board++;
    hold(TINY);  // let next passenger (if any) reset the button
  }
}
