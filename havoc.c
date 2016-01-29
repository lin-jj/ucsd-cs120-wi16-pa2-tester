#include "test.h"

static struct{
  int active;
  int start_tick;
  int tick_count;
  int request;
} processes[MAXPROCS+1];

static int t = 0;
static int active_procs = 0;
static int last_event = 0;
static int remaining_allocation = 100;

static int get_random_active(){
  // There are (active_procs) PIDs to pick from
  // Of these, pick the nth
  int n = rand() % active_procs;
  for(int pid = 1; pid <= MAXPROCS; pid++){
    if(!processes[pid].active) continue;
    if(!n--) return pid;
  }
  return 0;
}

static int get_random_inactive(){
  // There are (MAXPROCS - active_procs) PIDs to pick from
  // Of these, pick the nth
  int n = rand() % (MAXPROCS - active_procs);
  for(int pid = 1; pid <= MAXPROCS; pid++){
    if(processes[pid].active) continue;
    if(!n--) return pid;
  }
  return 0;
}

// start a process with pid pid; return 0 on success
static int start_process(int pid){
  if(!StartingProc(pid)) {
    Printf("HAVOC ERR: StartingProc failed\n");
    return 1;
  }

  active_procs++;
  processes[pid].active = 1;
  processes[pid].start_tick = t;
  processes[pid].tick_count = 0;

  //Printf("Started new process %d\n", pid);
  return 0;
}

// end a process with pid pid; return 0 on success
static int end_process(int pid){
  if(!EndingProc(pid)) {
    Printf("HAVOC ERR: EndingProc failed\n");
    return 1;
  }

  remaining_allocation += processes[pid].request;
  active_procs--;
  processes[pid].active = 0;
  processes[pid].request = 0;

  // Printf("Ended process %d after %d ticks\n", pid, t - processes[pid].start_tick);
  return 0;
}


int test_havoc(){
  int errors = 0;
  int last_event = 0;
  int totals[MAXPROCS+1];

  SetSchedPolicy(PROPORTIONAL);
  InitSched();

  memset(processes, 0, sizeof(processes));

  for(t = 0; t < 1000000; t++) {
    // Start a new process?
    if(active_procs < MAXPROCS && !(rand() % 1100)) {
      if(start_process(get_random_inactive())) return ++errors;
      last_event = t;
    }

    // Start as many new processes as possible?
    if(!(rand() % 50000)) {
      while(active_procs < MAXPROCS) {
        if(start_process(get_random_inactive())) return ++errors;
      }
      last_event = t;
    }

    // End a random process?
    if(active_procs && !(rand() % 1000)) {
      if(end_process(get_random_active())) return ++errors;
      last_event = t;
    }

    // Change the priority of a random process?
    if(active_procs && !(rand() % 500)) {
      int pid = get_random_active();
      int max_allocation = remaining_allocation + processes[pid].request;

      if(MyRequestCPUrate(pid, max_allocation + 1) != -1) {
        Printf("HAVOC ERR: Failed to reject overallocation request of %d for process %d\n",
               max_allocation + 1, pid);
        return ++errors;
      }

      if(max_allocation) {
        int new_allocation = 1 + rand() % max_allocation;

        // Half the time, try to allocate the maximum available first
        if(!(rand() % 2) && MyRequestCPUrate(pid, max_allocation) != 0){
          Printf("HAVOC ERR: Failed to accept valid request of %d for process %d; should have been able to request up to %d\n",
                 new_allocation, pid, max_allocation);
          return ++errors;
        }

        // Printf("Changing allocation of %d: %d -> %d\n", pid, processes[pid].request, new_allocation);

        if(MyRequestCPUrate(pid, new_allocation) != 0){
          Printf("HAVOC ERR: Failed to accept valid request of %d for process %d; should have been able to request up to %d\n",
                 new_allocation, pid, max_allocation);
          return ++errors;
        }

        remaining_allocation += processes[pid].request - new_allocation;
        processes[pid].request = new_allocation;

        // We reset the start of a process when we make a new rate request
        processes[pid].start_tick = t;
	processes[pid].tick_count = 0;
        last_event = t;
      }
    }

    if(t - last_event >= 100) {
      // of the leftover processes (which did not request a specific CPU rate),
      // the minimum and maximum number of ticks allocated
      int leftover_min = 100, leftover_max = 0;
      int min_leftover, max_leftover;

      // If there was at least one leftover process, check that
      // the most-scheduled leftover process is scheduled at most
      // once more than the least-scheduled leftover process
      // across the last 100 ticks

      // This is the closest we can get to verifying that the processes
      // are scheduled equally, given our 100-tick ring buffer

      if(leftover_max && leftover_max - leftover_min > 1) {
        Printf("HAVOC ERR: Leftover processes %d and %d were not scheduled equally (received %d and %d ticks respectively)\n",
               min_leftover, max_leftover, leftover_min, leftover_max);
	errors++;
      }
    }

    for(int i = 1; i <= MAXPROCS; i++){
      if(!processes[i].active) continue;
      if(!processes[i].request) continue;
      int age = t - processes[i].start_tick;
      if(age < 100) continue;
      int expected = processes[i].request * age / 100;
      if(!inSlackRange(expected, processes[i].tick_count)) {
	float actual_percent = 100.0 * processes[i].tick_count / age;
	Printf("HAVOC ERR: Process %d requested %d%% but only received %d of the %d ticks since its request (%2.2f%%)\n",
	       i, processes[i].request, processes[i].tick_count, age, actual_percent);
	errors++;
      }
    }
    processes[get_next_sched()].tick_count++;
  }

  while(active_procs) end_process(get_random_active());

  totalFailCounter += errors;
  return errors;
}
