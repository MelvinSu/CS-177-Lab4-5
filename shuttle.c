// Simulation of the Hertz airport shuttle bus, which picks up passengers
// from the airport terminal building going to the Hertz rental car lot.

#include <iostream>
#include "cpp.h"
#include <string.h>
#include <cmath>
#include <sstream>
using namespace std;

#define NUM_SEATS 6      // number of seats available on shuttle
#define TINY 1.e-20      // a very small time period
#define TERMNL 0         // named constants for labelling event set
#define CARLOT 1
long PLACES_NUM, SHUTTLE_NUM, MEAN_INTERVAL;

facility_set *buttons;  // customer queues at each stop
facility_set *rest;
event_set *shuttle_called;

event_set *get_off_now; //tell person to get off shuttle

event_set *hop_on; //tell person to get on shuttle
event boarded ("boarded");             // one customer responds after taking a seat

//event_set *shuttle_called; //call button control

void make_passengers(long whereami);       // passenger generator

string *places;
long group_size();

void passenger(long whoami);                // passenger trajectory

void shuttle(int number);                  // trajectory of the shuttle bus consists of...
void loop_around_airport(long & seats_used, long ID, int * wheretogo);      // ... repeated trips around airport
void load_shuttle(long whereami, long & on_board, long ID, int * wheretogo); // posssibly loading passengers
void drop_passengers(long whereami, long & on_board, long ID, int * wheretogo);
qtable shuttle_occ("bus occupancy");  // time average of how full is the bus

facility_set *drop_off;
facility_set *pick_up;
mailbox_set *busnum_to_passenger;
mailbox_set *passenger_destination;

event_set *just_stopped;
long *shuttle_loc;
long inactive_shuttles = 0;
int *deliver_count;
event shuttle_line ("Shuttle_queue");
facility readying_shuttle("readying shuttle");

extern "C" void sim(int argc, char** argv)      // main process
{
  string filename = "ShuttleSim_";
  stringstream str, str2, str3;
  char **temp = argv;
  long ltemp , ltemp2, ltemp3;
  ++temp;
  str << *temp;
  str >> ltemp;
  filename += *temp;
  filename += "_P_";
  PLACES_NUM = ltemp;
  ++temp;
  str2 << *temp;
  str2 >> ltemp2;
  filename += *temp;
  filename += "_S_";
  SHUTTLE_NUM = ltemp2;
  ++temp;
  str3 << *temp;
  str3 >> ltemp3;
  filename += *temp;
  filename += "_M.txt";
  MEAN_INTERVAL = ltemp3;
  //freopen (filename.c_str(), "w", stdout);

  create("sim");
  buttons = new facility_set("Curb", PLACES_NUM);
  get_off_now = new event_set ("get_off_now", PLACES_NUM);
  hop_on = new event_set ("board_shuttle", PLACES_NUM);
  shuttle_called = new event_set ("call button", PLACES_NUM);
  places = new string[PLACES_NUM];
  busnum_to_passenger = new mailbox_set("bus to passenger",PLACES_NUM);
  passenger_destination = new mailbox_set("passenger destination",SHUTTLE_NUM);
  drop_off = new facility_set("drop off", PLACES_NUM);
  pick_up = new facility_set("pick up", PLACES_NUM);
  just_stopped = new event_set("shuttle has stopped", SHUTTLE_NUM);
  shuttle_loc = new long[SHUTTLE_NUM];
  rest = new facility_set("resting", SHUTTLE_NUM);
  deliver_count = new int[SHUTTLE_NUM];
  for (int i = 0; i < PLACES_NUM; i++)
  {
     if (i != PLACES_NUM - 1)
     {
         string s = "Terminal";
         s.append(to_string(i+1));
         places[i] = s;
     }
     else
     {
         places[i] = "CarLot";
     }
  }
  shuttle_occ.add_histogram(NUM_SEATS+1,0,NUM_SEATS);
  for (int i = 0; i < PLACES_NUM; i++)
  {
      make_passengers(i);
  }
  for (int i = 0; i < SHUTTLE_NUM; i++)
  {
    shuttle(i);                // create a single shuttle
    deliver_count[i] = 0;
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
    hold(expntl(MEAN_INTERVAL));           // exponential interarrivals, mean 10 minutes
    long group = group_size();
    for (long i=0;i<group;i++)  // create each member of the group
      passenger(whereami);      // new passenger appears at this location
  }
}

// Model segment 2: activities followed by an individual passenger

void passenger(long whoami)
{
  const char* myName = "Person";
  create(myName);
  long destination, bus_num;
  long current = whoami;
  if (whoami == PLACES_NUM - 1) //if person spawned at car lot
  {
      destination = uniform_int(0, PLACES_NUM - 2);
  }
  else //if person spawned at a terminal
  {
      destination = PLACES_NUM - 1;
  }
  
  (*buttons)[whoami].reserve();     // join the queue at my starting location
  (*shuttle_called)[whoami].set();
  (*hop_on)[whoami].queue();        // wait for shuttle and invitation to board
  (*shuttle_called)[whoami].clear();
  (*busnum_to_passenger)[whoami].receive(&bus_num);
  (*passenger_destination)[bus_num].send(destination);
  hold(uniform(0.5,1.0));        // takes time to get seated
  boarded.set();                 // tell driver you are in your seat
  (*buttons)[whoami].release();     // let next person (if any) access button
  while(current != destination) {
    (*just_stopped)[bus_num].wait();
    current = shuttle_loc[bus_num];
  }
  (*get_off_now)[destination].wait();            // everybody off when shuttle reaches next stop
}

// Model segment 3: the shuttle bus
//Shuttle
//when shuttle is created, it goes into its reseting state, increments inactive_shuttles and initializes
//other variables. When a person presses the buttons to signals for the shuttles, a random shuttle that has reserved the readying_shuttle facility will prepare to leave
void shuttle(int number) {
  create ("shuttle");
  while(1) {  // loop forever
    // start off in idle state, waiting for the first call...
    (*rest)[number].reserve();                   // relax at garage till called from somewhere
    ++inactive_shuttles;
    int wheretogo[PLACES_NUM];
    long holding;
    long seats_used = 0;
    for (int i = 0; i < PLACES_NUM; i++)
    {
      wheretogo[i] = 0;
    }
    readying_shuttle.reserve();
    (*rest)[number].release();                   // and back to work we go!

    long who_pushed = (*shuttle_called).wait_any();
    (*shuttle_called)[who_pushed].set();
    shuttle_occ.note_value(seats_used);
    --inactive_shuttles;
    
    long count = 0;
    for (int i = 0; i < PLACES_NUM; i++)
      count += (*hop_on)[i].queue_cnt();
    if (count > 0) {
    //  holding = ((MEAN_INTERVAL) / (abs(count - (inactive_shuttles * NUM_SEATS)))) + .01;
    }
    else
     // holding = abs(SHUTTLE_NUM - MEAN_INTERVAL);
    holding = 0;
    hold(holding);
    
    readying_shuttle.release();
    hold(2);  // 2 minutes to reach car lot stop
    
    loop_around_airport(seats_used, number, wheretogo);
    while (seats_used > 0) {
      loop_around_airport(seats_used, number, wheretogo);
    }
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

void loop_around_airport(long & seats_used, long ID, int * wheretogo) { // one trip around the airport
  // Start by picking up departing passengers at car lot
  int i = PLACES_NUM - 1;
  (*drop_off)[i].reserve();
  if (wheretogo[i] > 0)
  {
      drop_passengers(i, seats_used, ID, wheretogo);
      shuttle_occ.note_value(seats_used);
  }
  (*drop_off)[i].release();
  (*pick_up)[i].reserve();
  load_shuttle(i, seats_used, ID, wheretogo);
  shuttle_occ.note_value(seats_used);
  (*pick_up)[i].release();
  hold (uniform(3,5));
  for (int j = 0; j < PLACES_NUM - 1; j++) {
      (*drop_off)[j].reserve();
      if (wheretogo[j] > 0) {
          drop_passengers(j, seats_used, ID, wheretogo);
          shuttle_occ.note_value(seats_used);
      }
      (*drop_off)[j].release();
      (*pick_up)[j].reserve();
      load_shuttle(j, seats_used, ID, wheretogo);
      shuttle_occ.note_value(seats_used);
      (*pick_up)[j].release();
      hold (uniform(3,5));
  }
}

void load_shuttle(long whereami, long & on_board, long ID, int * wheretogo)  // manage passenger loading
{
  // invite passengers to enter, one at a time, until all seats are full
  long temp;
  while((on_board < NUM_SEATS) &&
    ((*buttons)[whereami].num_busy() + (*buttons)[whereami].qlength() > 0))
  {
    (*hop_on)[whereami].set();// invite one person to board
    (*busnum_to_passenger)[whereami].send(ID);
    (*passenger_destination)[ID].receive(&temp);
    boarded.wait();  // pause until that person is seated
    ++wheretogo[temp];
    on_board++;
    hold(TINY);  // let next passenger (if any) reset the button
  }
}

void drop_passengers(long whereami, long & on_board, long ID, int * wheretogo)
{
   long temp;
   if(wheretogo[whereami] > 0)
   {
     shuttle_loc[ID] = whereami;
     (*just_stopped)[ID].set();
     (*get_off_now)[whereami].set();
     on_board -= wheretogo[whereami];
     deliver_count += on_board;
     wheretogo[whereami] = 0;
   }
}
// Simulation of the Hertz airport shuttle bus, which picks up passengers
// from the airport terminal building going to the Hertz rental car lot.
// Simulation of the Hertz airport shuttle bus, which picks up passengers
// from the airport terminal building going to the Hertz rental car lot.
