# Elevator Simulation
This project introduces you to the nuts and bolts of system calls, kernel programming, concurrency, and synchronization in the kernel.

## Contents
- **Makefile**: utility file that compiles the module created in elevator.c
- **syscalls_wrapper.c**: creates the wrappers for the system call functions used in elevator.c
- **elevator.c**: creates a module that simulates an elevator along with a proc file called 'elevator'

## How to Compile
- run ```make``` command
- insert the module using ```sudo insmod elevator.ko```
- print out the proc file using ```cat /proc/elevator``` or ```watch -n1 cat /proc/elevator```
- issue systems calls using the consumer.c and produce.c files from Canvas under the Testing folder
- remove the module using ```sudo rmmod elevator```

## Known Bugs
None.
