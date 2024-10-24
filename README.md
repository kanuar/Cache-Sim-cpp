# Cache-sim-cpp
## Cache simulator for L1 cache

* the above simulator can run for upto 8 cores where each core gets a private cache
* the information from one cache to another is shared using an interconnection network which is assumed to be pre-programmed into the CPU 

### Architecture definition

* the above simulator uses tries to emulate a multi-core architecture that is enabled to run Read (RD) and Write (WR) functions only which can however be expanded to a larger ISA with ease
* the caches in consideration here are of the following composition
	* each cache is divided into NUM_LINES cache lines
	* each line of the cache is represented in a custom data structure which has the following information provided
		* base address of the cache line 
		* state of the line (for MESI)
		* values stored in the cache line 
* the architecture also comprises of a RAM that can store upto NUM_BLOCKS x SIZE_BLOCK amounts of numerical data (of datatype short)

### Problem definition
* the given assignment requires one to run MESI protocol on the caches which enables them to share data across cores 
* in case of a read miss (across all caches) -> the protocol dictates that the address reference be sent to a lower level memory (a dummy RAM in this case)
* in case of a write miss (across all caches) -> the protocol dictates that the address reference be first retrieved from a lower level memory (a dummy RAM in this case) and then be written onto the cache line

### Solution definition
* the cache is represented as a data structure with an array of cache lines (the definition can be found above in architecture definition)
* the RAM is a dummy RAM and stores Random Values (for testing read/write miss on shared caches)
* the main processing is done on a CPU block
	* each CPU comprises of NUM_CORES cores and thereby NUM_CORES caches
	* each CPU has a common dummy RAM as mentioned above
	* each CPU core completes processing in three stages
		* intruction fetch
		* instruction decode
		* instruction execute
	* the various cores of the CPU have a private cache each
	* the number of cores decides the number of threads that will be running at a given instance (in this case 8 is the upper limit)
* there are various functions in the CPU that deal with various scenarios and ensure that the protocol MESI is strictly followed

### Running the code and manipulating it for experimentation and better understanding 
* the key variables which define the architecture can be found in the #define segment 
* the main() function of the program initializes the RAM using the CPU constructor
* there are three data structures defined 
	* decoded instruction
	* cache line
	* cache block
* there are three enumerations defined for better understanding 
	* cache_status -> to keep track of which state amongst MESI is stored on a cache line
	* instruction type -> to keep track of which instruction is being executed (read and write are the current ones, in order to expand it further, this enum and the switch case that deals with this enum can be modified to expand the ISA)
	* core status -> to keep track if the core is busy or not because there cant be more than one files running on each core 
* there are functions made for accessing and writing data 
	* request read is used for reading data from the cache (shared / not )
	* replace and read is used for reading data from the ram
	* request write is used for writing data from the cache (shared / not )
	* replace and write is used for writing data after replacing the line from the ram	onto the cache
* there are various functions to check for shared access and cache hits as well 
* a function called display cache is defined to view all the caches with their line states in a pretty manner 
* a function called get files is used to retrive the files that are to be run on various cores from a source file (the source.txt file provides the path of the files that will run on each core)
* the variables line_no and offset define how much to travel in the cache to obtain a value / address that is being referenced
