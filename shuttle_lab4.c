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
int PLACES_NUM, SHUTTLE_NUM;

facility_set *buttons;  // customer queues at each stop
facility rest ("rest");           // dummy facility indicating an idle shuttle

event_set *get_off_now; //tell person to get off shuttle

event_set *hop_on; //tell person to get on shuttle
event boarded ("boarded");             // one customer responds after taking a seat

event_set *shuttle_called; //call button control

void make_passengers(long whereami);       // passenger generator

string *places;
long group_size();

void passenger(long whoami);                // passenger trajectory

void shuttle(int number);                  // trajectory of the shuttle bus consists of...
void loop_around_airport(long & seats_used, long ID, int * wheretogo);      // ... repeated trips around airport
void load_shuttle(long whereami, long & on_board, long ID, int * wheretogo); // posssibly loading passengers
void drop_passengers(long whereami, long & on_board, long ID, int * wheretogo)
qtable shuttle_occ("bus occupancy");  // time average of how full is the bus

int *list;
facility_set *drop_off;
facility_set *pick_up;
mailbox_set *busnum_to_passenger;
mailbox_set *passenger_destination;

extern "C" void sim(int argc, char** argv)      // main process
{
  string filename = "CS177Lab4_";
  filename += *argv[1];
  filename += "_Places.txt";
  //freopen (filename.c_str(), "w", stdout);
  PLACES_NUM = *argv[1] - '0';
  SHUTTLE_NUM = *argv[2] - '0';
  cout << PLACES_NUM << endl;
  create("sim");
  buttons = new facility_set("Curb", PLACES_NUM);
  get_off_now = new event_set ("get_off_now", PLACES_NUM);
  hop_on = new event_set ("board_shuttle", PLACES_NUM);
  shuttle_called = new event_set ("call button", PLACES_NUM);
  places = new string[PLACES_NUM];
  list = new int[PLACES_NUM];
  busnum_to_passenger = new mailbox_set[PLACES_NUM];
  passenger_destination = new mailbox_set[SHUTTLE_NUM];
  drop_off = new facility_set[PLACES_NUM];
  pick_ip = new facility_set[PLACES_NUM];
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
     list[i]=0;
  }
  shuttle_occ.add_histogram(NUM_SEATS+1,0,NUM_SEATS);
  for (int i = 0; i < PLACES_NUM; i++)
  {
      make_passengers(i);
  }
  for (int i = 0; i < SHUTTLE_NUM; i++)
  {
    shuttle(i);                // create a single shuttle
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
  ++list[destination];
  (*buttons)[whoami].reserve();     // join the queue at my starting location
  (*shuttle_called)[whoami].set();  // head of queue, so call shuttle
  (*hop_on)[whoami].queue();        // wait for shuttle and invitation to board
  (*busnum_to_passenger)[whoami].recieve(&bus_num);
  (*passenger_destination)[bus_num].send(destination);
  (*shuttle_called)[whoami].clear();// cancel my call; next in line will push 
  hold(uniform(0.5,1.0));        // takes time to get seated
  boarded.set();                 // tell driver you are in your seat
  (*buttons)[whoami].release();     // let next person (if any) access buttona
  (*get_off_now)[destination].wait();            // everybody off when shuttle reaches next stop
}

// Model segment 3: the shuttle bus

void shuttle(int number) {
  create ("shuttle");
  while(1) {  // loop forever
    // start off in idle state, waiting for the first call...
    rest.reserve();                   // relax at garage till called from somewhere
    long who_pushed = (*shuttle_called).wait_any();
    int wheretogo[PLACES_NUM];
    for (int i = 0; i < PLACES_NUM; i++)
    {
      wheretogo[i] = 0;
    }
    (*shuttle_called)[who_pushed].set(); // loop exit needs to see event
    rest.release();                   // and back to work we go!

    long seats_used = 0;              // shuttle is initially empty
    shuttle_occ.note_value(seats_used);

    hold(2);  // 2 minutes to reach car lot stop

    // Keep going around the loop until there are no calls waiting
    while (((*shuttle_called)[TERMNL].state()==OCC)||
           ((*shuttle_called)[CARLOT].state()==OCC)  )
      loop_around_airport(seats_used, i, wheretogo);
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
  load_shuttle(i, seats_used);
  shuttle_occ.note_value(seats_used);

  hold (uniform(3,5));

  for (int j = 0; j < PLACES_NUM - 1; j++) {
      if (seats_used > 0) {
          drop_passengers(j, seats_used);
          shuttle_occ.note_value(seats_used);
      }
      load_shuttle(j, seats_used);
      shuttle_occ.note_value(seats_used);
      hold (uniform(3,5));
  }
  if (seats_used > 0)
  {
      drop_passengers(PLACES_NUM - 1, seats_used);
      shuttle_occ.note_value(seats_used);
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
    (*passenger_list)[whereami].send(ID);
    (*passenger_destination)[ID].recieve(&temp);
    boarded.wait();  // pause until that person is seated
    ++wheretogo[temp];
    on_board++;
    hold(TINY);  // let next passenger (if any) reset the button
  }
}

void drop_passengers(long whereami, long & on_board, long ID, int * wheretogo)
{
   long temp;
   while(wheretogo[whereami] > 0 && on_board > 0)
   {
     (*get_off_now)[whereami].set();
     (*passenger_destination)[ID].recieve(&temp);
     on_board--;
     --wheretogo[temp];
     --list[whereami];
   }
}
