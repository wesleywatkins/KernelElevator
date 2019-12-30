/*
	Module imports, license, and description
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/linkage.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simulation of an elevator");


/* 
	Global variables 
*/

#define ENTRY_NAME "elevator"
#define ENTRY_SIZE 1024
#define PERMS 0644
#define PARENT NULL
#define DEFAULT_SLEEP_TIME 1
static struct file_operations fops;

// Elevator details
#define MAX_PASSENGERS 10
#define MAX_WEIGHT 30
#define TIME_BETWEEN_FLOORS 2
#define TIME_FOR_LOADING 1
#define FLOORS 10

// Passenger units for elevator
#define ADULT 1
#define CHILD 1
#define ROOM_SERVICE 2
#define BELLHOP 2

// Weight units for elevator
#define ADULT_WEIGHT 2
#define CHILD_WEIGHT 1
#define ROOM_SERVICE_WEIGHT 4
#define BELLHOP_WEIGHT 8

/*
	Defintions for structs (elevator, passengers, passenger, & thread)
*/

// Struct for elevator status (for proc file use)
typedef struct elevator_status {
	int state; // 0 = offline, 1 = idle, 2 = loading, 3 = up, 4 = down
	int current_floor;
	int next_floor;
	int passengers;
	int weight;
	int deactiving;
	int started;
} Elevator;

// Struct representing a passenger
typedef struct passenger_data {
	int type;
	int start;
	int destination;
	struct list_head list;
} Passenger;

// Struct for storing two passenger lists
typedef struct passenger_lists {
	struct list_head waiting;
	struct list_head onElevator;
	int serviced;
} Passengers;

// Struct for storing thread data
typedef struct thread_parameter {
	int id;
	int cnt;
	struct task_struct *kthread;
	struct mutex mutex;
} Thread;


/* 
	Global variable declarations
*/

Elevator elevator;
Passengers passengers;
Thread main_thread;
static char *message;
static int read_p;
static int serviced_on_floor[FLOORS];
static int waiting_on_floor[FLOORS];


/*
	System call imports
*/

extern long (*STUB_start_elevator)(void);
extern long (*STUB_issue_request)(int,int,int);
extern long (*STUB_stop_elevator)(void);


/* 
	System Calls
*/

// System Call To Start Elevator
long start_elevator(void) {
	if (elevator.started)
		return 1;
	else {
		elevator.started = 1;
		elevator.deactiving = 0;
	}
	return 0;
}

// System Call To Issue Request
long issue_request(int passenger_type, int start_floor, int destination_floor) {
	// Error Checking
	if (!elevator.started) return 1;
	if (passenger_type < 1 || passenger_type > 4) return 1;
	if (start_floor < 1 || start_floor > FLOORS) return 1;
	if (destination_floor < 1 || destination_floor > FLOORS) return 1;
	// Add passenger to waiting list
	if (mutex_lock_interruptible(&main_thread.mutex) == 0) {
		if (!elevator.deactiving) {
			Passenger * p;
			p = kmalloc(sizeof(Passenger), __GFP_RECLAIM);
			p->type = passenger_type;
			p->start = start_floor;
			p->destination = destination_floor;
			list_add_tail(&p->list, &passengers.waiting);
			waiting_on_floor[p->start - 1]++;
		}
	}
	mutex_unlock(&main_thread.mutex);
	return 0;
}

// System Call To Stop Elevator
long stop_elevator(void) {
	struct list_head *temp;
	struct list_head *dummy;
	struct list_head delete_list;
	Passenger *p;
	int i;
	INIT_LIST_HEAD(&delete_list);
	if(!elevator.started) return 1; 
	if (elevator.deactiving) return 1;
	elevator.deactiving = 1;
	elevator.started = 0;
	if (mutex_lock_interruptible(&main_thread.mutex) == 0) {
		list_for_each_safe(temp, dummy, &passengers.waiting) {
			list_move_tail(temp, &delete_list);
		}
	}
	mutex_unlock(&main_thread.mutex);
	list_for_each_safe(temp, dummy, &delete_list) {
		p = list_entry(temp, Passenger, list);
		list_del(temp);
		kfree(p);
	}
	for (i = 0; i < FLOORS; i++)
		waiting_on_floor[i] = 0;
	return 0;
}


/*
	Return passenger unit based off passenger type
*/

int getPassengerUnit(Passenger * p) {
	if      (p->type == 1) return ADULT;
	else if (p->type == 2) return CHILD;
	else if (p->type == 3) return ROOM_SERVICE;
	else if (p->type == 4) return BELLHOP;
	else return 0;
}


/*
	Return passenger weight based off passenger type
*/

int getPassengerWeight(Passenger * p) {
	if      (p->type == 1) return ADULT_WEIGHT;
	else if (p->type == 2) return CHILD_WEIGHT;
	else if (p->type == 3) return ROOM_SERVICE_WEIGHT;
	else if (p->type == 4) return BELLHOP_WEIGHT;
	else return 0;
}


/*
	Remove passengers from elevator if destination is current floor
*/

int unloadPassengers(void) {
	// List variables
	struct list_head *temp;
	struct list_head *dummy;
	struct list_head delete_list;
	Passenger *p;
	int unloaded = 0;

	// Initialize delete list
	INIT_LIST_HEAD(&delete_list);

	// Lock, remove passengers from elevator if their
	// destination is the current floor
	if (mutex_lock_interruptible(&main_thread.mutex) == 0) {
		list_for_each_safe(temp, dummy, &passengers.onElevator) {
			p = list_entry(temp, Passenger, list);
			if (p->destination == elevator.current_floor) {
				elevator.passengers -= getPassengerUnit(p);
				elevator.weight -= getPassengerWeight(p);
				list_move_tail(temp, &delete_list);
				passengers.serviced++;
				serviced_on_floor[p->start - 1]++;
				unloaded = 1;
			}
		}
	}
	mutex_unlock(&main_thread.mutex);

	// delete passengers from delete list
	list_for_each_safe(temp, dummy, &delete_list) {
		p = list_entry(temp, Passenger, list);
		list_del(temp);
		kfree(p);
	}
	return unloaded;
}


/*
	Load passengers if start floor = elevator floor
*/

int loadPassengers(void) {
	// List variables
	struct list_head *temp;
	struct list_head *dummy;
	Passenger *p;
	int loaded = 0;
	// Lock, remove passengers from elevator if their
	// destination is the current floor
	if (mutex_lock_interruptible(&main_thread.mutex) == 0) {
		list_for_each_safe(temp, dummy, &passengers.waiting) {
			p = list_entry(temp, Passenger, list);
			if (p->start == elevator.current_floor) {
				int units = getPassengerUnit(p);
				int weight = getPassengerWeight(p);
				if (weight + elevator.weight <= MAX_WEIGHT && units + elevator.passengers <= MAX_PASSENGERS) {
					list_move_tail(temp, &passengers.onElevator);
					elevator.passengers += units;
					elevator.weight += weight;
					waiting_on_floor[p->start - 1]--;
					loaded = 1;
				}
			}
		}
	}
	mutex_unlock(&main_thread.mutex);
	return loaded;
}


/*
	Set the state of the elevator based on
	current and next floor
*/

void changeElevatorState(void) {
	if (elevator.next_floor > elevator.current_floor)
		elevator.state = 3;
	else
		elevator.state = 4;
}


/*
	Move the elevator based on the state
*/

void moveElevator(void) {
	if (elevator.state == 3) {
		elevator.current_floor++;
		if (elevator.current_floor == FLOORS)
			elevator.next_floor = elevator.current_floor - 1;
		else
			elevator.next_floor = elevator.current_floor + 1;
	}
	if (elevator.state == 4) {
		elevator.current_floor--;
		if (elevator.current_floor == 1)
			elevator.next_floor = elevator.current_floor + 1;
		else
			elevator.next_floor = elevator.current_floor - 1;
	}
}


/*
	Thread function that runs constantly in the background
*/

int thread_run(void *data) {
	while (!kthread_should_stop()) {
		// If people are waiting or on the elevator
		// load and unload passengers, then move the elevator
		if (!list_empty(&passengers.waiting) || !list_empty(&passengers.onElevator))
		{
			// variables for determining whether to pause for loading
			int unload_pause = 0;
			int load_pause = 0;

			// unload and load passengers
			unload_pause = unloadPassengers();
			if (elevator.passengers != MAX_PASSENGERS && elevator.weight != MAX_WEIGHT)
				load_pause = loadPassengers();

			// pause for loading if passengers got on or off
			if (unload_pause || load_pause) {
				elevator.state = 2;
				ssleep(TIME_FOR_LOADING);
			}

			// Only move elevator if there are more passengers
			if (!list_empty(&passengers.waiting) || !list_empty(&passengers.onElevator)) {
				changeElevatorState();
				ssleep(TIME_BETWEEN_FLOORS);
				moveElevator();
			}
			else if (elevator.deactiving) {
				elevator.state = 0;
				elevator.deactiving = 0;
			}
		}
		// if no passengers and the elevator is started,
		// set the elevator to IDLE
		else if (elevator.started) {
			elevator.state = 1;
			ssleep(DEFAULT_SLEEP_TIME); // prevents busy-waiting
		}
		// if no passengers and the elevator is not started,
		// set the elevator to OFFLINE
		else {
			elevator.state = 0;
			ssleep(DEFAULT_SLEEP_TIME); // prevents busy-waiting
		}
	}
	return 0;
}


/*
	Sets up data within the thread struct
*/

void thread_init_parameter(struct thread_parameter *parm) {
	static int id = 1;
	parm->id = id++;
	parm->cnt = 0;
	mutex_init(&parm->mutex);
	parm->kthread = kthread_run(thread_run, parm, "thread example %d", parm->id);
}


/*
	Prints the information to 'message' variable that will be printed to the proc file
*/

int elevator_proc_open(struct inode *sp_inode, struct file *sp_file) {

	// Temporary string to help print to 'message'
	char * tempMsg = kmalloc(sizeof(char) * 100, __GFP_RECLAIM | __GFP_IO | __GFP_FS);
	int real_weight = 0;
	int i;

	read_p = 1;
	message = kmalloc(sizeof(char) * ENTRY_SIZE, __GFP_RECLAIM | __GFP_IO | __GFP_FS);
	if (message == NULL) {
		printk(KERN_WARNING "hello_proc_open");
		return -ENOMEM;
	}

	//print elevator state
	if (elevator.state == 0) sprintf(tempMsg,"OFFLINE\n");
	else if (elevator.state == 1) sprintf(tempMsg,"IDLE\n");
	else if (elevator.state == 2) sprintf(tempMsg,"LOADING\n");
	else if (elevator.state == 3) sprintf(tempMsg,"UP\n");
	else if (elevator.state == 4) sprintf(tempMsg,"DOWN\n");
	sprintf(message, "Elevator state: "); 
	strcat(message, tempMsg);
	
	//print current floor
	sprintf(tempMsg, "Current floor: %d\n", elevator.current_floor); 
	strcat(message, tempMsg);

	//print next floor
	sprintf(tempMsg, "Next floor: %d\n", elevator.next_floor); 
	strcat(message, tempMsg);

	//print passenger
	sprintf(tempMsg, "Passengers load: %d\n", elevator.passengers); 
	strcat(message, tempMsg);

	//print weight
	real_weight = elevator.weight/2;
	if (elevator.weight % 2 == 0)
		sprintf(tempMsg, "Weight load: %d\n", real_weight);
	else
		sprintf(tempMsg, "Weight load: %d.5\n", real_weight);
	strcat(message, tempMsg);

	//print passengers servied
	sprintf(tempMsg, "Total Passengers Serviced: %d\n", passengers.serviced);
	strcat(message, tempMsg);

	// Print waiting passengers on each floor
	for (i = 0; i < FLOORS; i++) {
		int floor = i + 1;
		sprintf(tempMsg, "Waiting Passengers on Floor %d: %d\n", floor, waiting_on_floor[i]);
		strcat(message, tempMsg);
	}
	for (i = 0; i < FLOORS; i++) {
		int floor = i + 1;
		sprintf(tempMsg, "Passengers Serviced on Floor %d: %d\n", floor, serviced_on_floor[i]);
		strcat(message, tempMsg);
	}

	kfree(tempMsg);

	return 0;
}


/*
	Prints data stored in 'message' to proc file
*/

ssize_t elevator_proc_read(struct file *sp_file, char __user *buf, size_t size, loff_t *offset) {
	int len = strlen(message);
	read_p = !read_p;
	if (read_p)
		return 0;
	copy_to_user(buf, message, len);
	return len;
}


/*
	Free up memory allocated for 'message' when proc file is removed
*/

int elevator_proc_release(struct inode *sp_inode, struct file *sp_file) {
	kfree(message);
	return 0;
}


/*
	Initialize elevator data and lists, declare a passenger for testing purposes
	* THIS FUNCTION IS FOR TESTING PURPOSE ONLY *
*/

void setup_elevator(void) 
{
	int i;
	elevator.state = 0;
	elevator.current_floor = 1;
	elevator.next_floor = 2;
	elevator.passengers = 0;
	elevator.weight = 0;
	elevator.deactiving = 0;
	elevator.started = 0;
	INIT_LIST_HEAD(&passengers.waiting);
	INIT_LIST_HEAD(&passengers.onElevator);
	passengers.serviced = 0;
	// Initalize Floor Service to 0
	for (i = 0; i < FLOORS; i++) {
		serviced_on_floor[i] = 0;
		waiting_on_floor[i] = 0;
	}
}


/*
	Set up proc file, start thread
*/

static int elevator_init(void) {

	// Set Initial Elevator Settings
	setup_elevator();

	// setup system calls
	STUB_start_elevator = start_elevator;
	STUB_issue_request = issue_request;
	STUB_stop_elevator = stop_elevator;

	// Set up proc file
	fops.open = elevator_proc_open;
	fops.read = elevator_proc_read;
	fops.release = elevator_proc_release;
	if (!proc_create(ENTRY_NAME, PERMS, NULL, &fops)) {
		printk(KERN_WARNING "thread_init");
		remove_proc_entry(ENTRY_NAME, NULL);
		return -ENOMEM;
	}

	// start thread
	thread_init_parameter(&main_thread);
	if (IS_ERR(main_thread.kthread)) {
		printk(KERN_WARNING "error spawning thread");
		remove_proc_entry(ENTRY_NAME, NULL);
		return PTR_ERR(main_thread.kthread);
	}
	return 0;
}
module_init(elevator_init);


/*
	Function for removing all passengers (elevator
	and waiting) used by module exit
*/

void removeAllPassengers(void) {
	// List variables
	struct list_head *temp;
	struct list_head *dummy;
	struct list_head delete_list;
	Passenger *p;
	// Initalize Delete List
	INIT_LIST_HEAD(&delete_list);
	// Add waiting and elevator passengers to delete list
	if (mutex_lock_interruptible(&main_thread.mutex) == 0) {
		list_for_each_safe(temp, dummy, &passengers.waiting) {
			list_move_tail(temp, &delete_list);
		}
		list_for_each_safe(temp, dummy, &passengers.onElevator) {
			list_move_tail(temp, &delete_list);
		}
	}
	mutex_unlock(&main_thread.mutex);
	// Delete from delete list
	list_for_each_safe(temp, dummy, &delete_list) {
		p = list_entry(temp, Passenger, list);
		list_del(temp);
		kfree(p);
	}
}


/*
	Stop the thread and remove the proc file
*/

static void elevator_exit(void) {
	// NULL out system calls
	STUB_start_elevator = NULL;
	STUB_issue_request = NULL;
	STUB_stop_elevator = NULL;
	// stop thread
	kthread_stop(main_thread.kthread);
	// remove any remaining passengers
	removeAllPassengers();
	// destroy mutex
	mutex_destroy(&main_thread.mutex);
	// remove proc entry
	remove_proc_entry(ENTRY_NAME, NULL);
}
module_exit(elevator_exit);
