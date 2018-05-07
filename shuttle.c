// Simulation of the Hertz airport shuttle bus, which picks up passengers
// from the airport terminal building going to the Hertz rental car lot.

#include <iostream>
#include "cpp.h"
#include <string.h>
using namespace std;

#define NUM_SEATS 6      // number of seats available on shuttle
#define TINY 1.e-20      // a very small time period
int PLACES_NUM = 2;

facility_set *buttons;  // customer queues at each stop
facility rest ("rest");           // dummy facility indicating an idle shuttle
facility_set *drop_off;
facility_set *pick_up;

event_set *get_off_now;
event_set *hop_on;
event boarded ("boarded");             // one customer responds after taking a seat

event_set *shuttle_called;

void make_passengers(long whereami);       // passenger generator

string *places;
long group_size();

void passenger(long whoam);                // passenger trajectory

void shuttle(long i);                  // trajectory of the shuttle bus consists of...
void loop_around_airport(long & seats_used, long ID);      // ... repeated trips around airport
void load_shuttle(long whereami, long & on_board, long ID); // posssibly loading passengers
void drop_passengers(long whereami, long & on_board, long ID);
qtable shuttle_occ("bus occupancy");  // time average of how full is the bus

void shuttle_control(long shuttle_number);

long modP;
mailbox_set *busnum_to_passenger;
mailbox_set *stopnum;

extern "C" void sim(int argc, char** argv)      // main process
{
//  string filename = "CS177Lab4_";
//  filename += *argv[1];
 // filename += "_Places.txt";
//  freopen (filename.c_str(), "w", stdout);
  PLACES_NUM = *argv[1] - '0';
 modP =  100;
cout << "STUFF" << endl;
  create("sim");
cout << "STUFF" << endl;
  buttons = new facility_set("Curb", PLACES_NUM);
  get_off_now = new event_set ("get_off_now", PLACES_NUM);
  hop_on = new event_set ("board_shuttle", PLACES_NUM);
  shuttle_called = new event_set ("call button", PLACES_NUM);
  drop_off = new facility_set("drop_off", PLACES_NUM);
  pick_up = new facility_set("pick_up", PLACES_NUM);
  places = new string[PLACES_NUM];
  busnum_to_passenger = new mailbox_set("shuttle_num_transfer", PLACES_NUM);
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
  shuttle_control(1);
  for (int i = 0; i < PLACES_NUM; i++)
  {
      make_passengers(i);
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
    hold(expntl(10));           // exponential interarrivals, mean 10 minutes
    long group = group_size();
    for (long i=0;i<group;i++)  // create each member of the group
      passenger(whereami);      // new passenger appears at this location
  }
}

// Model segment 2: activities followed by an individual passenger

void passenger(long whoami)
{
  //const char* myName=people[whoami].c_str(); // hack because CSIM wants a char*
  const char* myName;
  if (whoami < PLACES_NUM - 1)
  {
      char temp[3];
      char p[] = "T";
      sprintf(temp, "%d", whoami + 1);
      strcat(p, temp);
      myName = p;
  }
  else
  {
      myName = "Lot";
  }
  create(myName);
  long destination, bus_num;
  if (whoami == PLACES_NUM - 1) //if person spawned at car lot
  {
      destination = uniform_int(0, PLACES_NUM - 2);
  }
  else //if person spawned at a terminal
  {
      destination = PLACES_NUM - 1;
  }
  (*buttons)[whoami].reserve();     // join the queue at my starting location
  (*shuttle_called)[whoami].set();  // head of queue, so call shuttle
  (*hop_on)[whoami].queue();        // wait for shuttle and invitation to board
  (*busnum_to_passenger)[whoami].receive(&bus_num);
  (*shuttle_called)[whoami].clear();// cancel my call; next in line will push 
  hold(uniform(0.5,1.0));        // takes time to get seated
  boarded.set();                 // tell driver you are in your seat
  (*buttons)[whoami].release();     // let next person (if any) access buttona
  (*get_off_now)[destination].wait();            // everybody off when shuttle reaches next stop
}

// Model segment 3: the shuttle bus

void shuttle_control(long shuttle_number) {
  create ("Shuttle Control");
  for (int i = 0; i <= shuttle_number; i++)
    shuttle(i + PLACES_NUM);
}
void shuttle(long i) {
  create ("shuttle");
  while(1) {  // loop forever
    // start off in idle state, waiting for the first call...
    rest.reserve();                   // relax at garage till called from somewhere
    long who_pushed = (*shuttle_called).wait_any();
    (*shuttle_called)[who_pushed].set(); // loop exit needs to see event
    rest.release();                   // and back to work we go!

    long seats_used = 0;              // shuttle is initially empty
    shuttle_occ.note_value(seats_used);

    hold(2);  // 2 minutes to reach car lot stop

    // Keep going around the loop until there are no calls waiting
    while (((*shuttle_called)[0].state()==OCC)||
           ((*shuttle_called)[PLACES_NUM - 1].state()==OCC)  )
      loop_around_airport(seats_used, i);
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

void loop_around_airport(long & seats_used, long ID) { // one trip around the airport
  (*pick_up)[PLACES_NUM - 1].reserve();
  int i = PLACES_NUM - 1;
  load_shuttle(i, seats_used, ID);
  shuttle_occ.note_value(seats_used);
  (*pick_up)[PLACES_NUM - 1].release();

  hold (uniform(3,5));

  for (int j = 0; j < PLACES_NUM - 1; j++) {
      (*drop_off)[j].reserve();
      if (seats_used > 0) {
          drop_passengers(j, seats_used, ID);
          shuttle_occ.note_value(seats_used);
      }
      (*drop_off)[j].release();
      (*pick_up)[j].reserve();
      load_shuttle(j, seats_used, ID);
      shuttle_occ.note_value(seats_used);
      (*pick_up)[j].release();
      hold (uniform(3,5));
  }
  (*drop_off)[PLACES_NUM - 1].reserve();
  if (seats_used > 0)
  {
      drop_passengers(PLACES_NUM - 1, seats_used, ID);
      shuttle_occ.note_value(seats_used);
  }
  (*drop_off)[PLACES_NUM - 1].release();
}

void load_shuttle(long whereami, long & on_board, long ID)  // manage passenger loading
{
  // invite passengers to enter, one at a time, until all seats are full
  while((on_board < NUM_SEATS) &&
    ((*buttons)[whereami].num_busy() + (*buttons)[whereami].qlength() > 0))
  {
    (*hop_on)[whereami].set();// invite one person to board
    (*busnum_to_passenger)[whereami].send(ID);
    boarded.wait();  // pause until that person is seated
    on_board++;
    hold(TINY);  // let next passenger (if any) reset the button
  }
}

void drop_passengers(long whereami, long & on_board, long ID)
{
   while(on_board > 0)
   {
     (*get_off_now)[whereami].set();
     on_board--;
   }
}
