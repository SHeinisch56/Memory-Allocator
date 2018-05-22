#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "mem.h"
#include "stdlib.h"

/*
 * This structure serves as the header for each allocated and free block
 * It also serves as the footer for each free block
 * The blocks are ordered in the increasing order of addresses 
 */
typedef struct block_tag{

  int size_status;
  
 /*
  * Size of the block is always a multiple of 4
  * => last two bits are always zero - can be used to store other information
  *
  * LSB -> Least Significant Bit (Last Bit)
  * SLB -> Second Last Bit 
  * LSB = 0 => free block
  * LSB = 1 => allocated/busy block
  * SLB = 0 => previous block is free
  * SLB = 1 => previous block is allocated/busy
  * 
  * When used as the footer the last two bits should be zero
  */

 /*
  * Examples:
  * 
  * For a busy block with a payload of 24 bytes (i.e. 24 bytes data + an additional 4 bytes for header)  
  * Header:
  * If the previous block is allocated, size_status should be set to 31
  * If the previous block is free, size_status should be set to 29
  * 
  * For a free block of size 28 bytes (including 4 bytes for header + 4 bytes for footer)
  * Header:
  * If the previous block is allocated, size_status should be set to 30
  * If the previous block is free, size_status should be set to 28
  * Footer:
  * size_status should be 28
  * 
  */

} block_tag;

/* Global variable - This will always point to the first block
 * i.e. the block with the lowest address */
block_tag *first_block = NULL;

/* Global variable - Total available memory */
int total_mem_size = 0;

/*
 * Function for allocating 'size' bytes
 * Returns address of the payload in the allocated block on success 
 * Returns NULL on failure 
 * Here is what this function should accomplish 
 * - If size is less than equal to 0 - Return NULL
 * - Round up size to a multiple of 4 
 * - Traverse the list of blocks and allocate the best free block which can accommodate the requested size 
 * - Also, when allocating a block - split it into two blocks when possible 
 * Tips: Be careful with pointer arithmetic 
 */
void* Mem_Alloc(int size){
	
	//size must be positive
	if(size <= 0) {
		return NULL;
	}

	//size must be a multiple of 4
	if(size % 4 == 1) {
		size += 3;
	}
	else if(size % 4 == 2) {
		size += 2;
	}
	else if(size % 4 == 3) {
		size += 1;
	}
	
	//traverse the memory

	//variables for size_status
	int t_size;	
	int is_busy = 1;
	int prev_block_allocated = 2;
	int header_size = 4;

	//size of allocation is requested size plus a header
	size = size + header_size;
	
	//block pointers to track best fitting block while traversing
	block_tag *best_slot = NULL;
	block_tag *current = first_block;
	
	//iterate through each block (like how Mem_Dump does)
	while(current < (block_tag*)((char*)first_block + total_mem_size)){
		
		//update current's size_status
		t_size = current->size_status;
		
		//if size is odd, the block is busy, so adjust down 1 for business and 2 for previous busy block to get raw size and iterate to next block
		if(t_size & 1){
			t_size -= 3;
		}
		//if size is even, the block is free, so adjust down 2 for previous busy block and check to see if this free block is best fit and update best_slot if it is
		else{
			t_size -= 2;
			if(size <= current->size_status && (best_slot == NULL || current->size_status < best_slot->size_status)) {				
				best_slot = current;
			}
		}
		//iterate to next block until the end of total_mem_size
		current = (block_tag*)((char*)current + t_size);
	}
	//Return Null if there is no room for he requested allocation	
	if(best_slot == NULL) {
		//printf("\nNot enough space for allocation!\n");
		return NULL;
	}

	//we've found a free block for the allocation, save the size before splitting the block
	int preSplitSize = best_slot->size_status - prev_block_allocated;
	
	//this block points to the newly allocated memory
	block_tag *newBlock = best_slot + header_size;

	//store the size in the header
	newBlock->size_status = size + is_busy + prev_block_allocated;
	(newBlock-header_size)->size_status = newBlock->size_status;
	
	//this block points the the extra free memory that is split from the allocation
	block_tag *splitBlock = best_slot + (size/4);
	splitBlock->size_status = (preSplitSize - size) + prev_block_allocated;

	//setting up the footer for the split block
	block_tag *footer = splitBlock + (splitBlock->size_status - 2)/4 - prev_block_allocated/4 - 1;
 	footer->size_status = (preSplitSize - size) + prev_block_allocated;
	
	return newBlock;
}

/*
 * Function for freeing up a previously allocated block 
 * Argument - ptr: Address of the payload of the allocated block to be freed up 
 * Returns 0 on success 
 * Returns -1 on failure 
 * Here is what this function should accomplish 
 * - Return -1 if ptr is NULL
 * - Return -1 if ptr is not within the range of memory allocated by Mem_Init()
 * - Return -1 if ptr is not 4 byte aligned
 * - Mark the block as free 
 * - Coalesce if one or both of the immediate neighbours are free 
 */
int Mem_Free(void *ptr){

	//Return -1 if ptr is NULL	
	if(ptr == NULL) {
		return -1;
	}

	//Return -1 if ptr is not within the range of memory allocated by Mem_Init()
 	if((int)ptr < (int)first_block || (int)ptr > (int)first_block + total_mem_size) {
		return -1;
	}

	//Return -1 if ptr is not 4 byte aligned
	if((int)ptr % 4 != 0) {
		return -1;
	}

	//This points the block that the user wants to free
	block_tag *blockToFree = (block_tag *)ptr;

	//This points to the header of the block the user wants to free 
	block_tag *coalescedBlock = blockToFree - 4;

	//mark as free
	coalescedBlock->size_status = coalescedBlock->size_status - 1;
	
	//If next block is busy and previous block is also busy
	if((coalescedBlock + (coalescedBlock->size_status - 2)/4)->size_status & 1 && coalescedBlock->size_status % 4 != 0){		
		
		//Add a footer that holds the size_status		
		(coalescedBlock + (coalescedBlock->size_status - 2)/4 - 4)->size_status = coalescedBlock->size_status;

		//By freeing this block, the next block's previous block is no longer busy, so subtract 2 from he next blocks size_status		
		(coalescedBlock + (coalescedBlock->size_status - 2)/4)->size_status -= 2;
	}
	
	//If next block is busy and previous block is free
	else if((coalescedBlock + (coalescedBlock->size_status)/4)->size_status & 1 && coalescedBlock->size_status % 4 == 0) {

		//Add a footer an set its size_status to the current block + the next block
		block_tag *footer = coalescedBlock + (coalescedBlock->size_status)/4 - 1;
		footer->size_status = coalescedBlock->size_status + (coalescedBlock - 4)->size_status;

		//Move the current block's header to the previous block's header and update the size_status
		coalescedBlock = coalescedBlock - ((coalescedBlock - 4)->size_status)/4;
		coalescedBlock->size_status = footer->size_status;

		//By freeing this block, the next block's previous block is no longer busy, so subtract 2 from he next blocks size_status
		(footer + 1)->size_status -= 2;
	}

	//If the next block is free but the previous block is busy
	else if(((coalescedBlock + (coalescedBlock->size_status - 2)/4)->size_status) % 2 == 0 && coalescedBlock->size_status % 4 != 0) {

		//Update the header size_status to be the current block's size status + the previous block's size_status
		coalescedBlock->size_status = coalescedBlock->size_status + (coalescedBlock + (coalescedBlock->size_status - 2)/4)->size_status - 2;

		//Update the size_status of the footer
		(coalescedBlock + (coalescedBlock->size_status - 2)/4 - 4)->size_status = coalescedBlock->size_status;			
	}
	
	//If next block is free and previous block is also free
	else if(((coalescedBlock + (coalescedBlock->size_status)/4)->size_status) % 2 == 0 && coalescedBlock->size_status % 4 == 0) {		
		
		//We need to coalesce on both sides, so create temp variables to hold the current size_status and next size_status
		int temp = coalescedBlock->size_status;
		
		coalescedBlock->size_status = (coalescedBlock + (coalescedBlock->size_status)/4)->size_status - 2;
		
		//Create a footer
		block_tag *footer = (coalescedBlock + (coalescedBlock->size_status)/4 - 4);
	
		int temp2 = coalescedBlock->size_status;	

		//Move the pointer to he header of the previous block and update the size_status of the header and footer
		coalescedBlock = coalescedBlock - ((coalescedBlock - 4)->size_status - 2)/4;
		coalescedBlock->size_status = coalescedBlock->size_status + temp + temp2;
		footer->size_status = coalescedBlock->size_status;
		}
	//Returns 0 on success
	return 0;
}

/*
 * Function used to initialize the memory allocator
 * Not intended to be called more than once by a program
 * Argument - sizeOfRegion: Specifies the size of the chunk which needs to be allocated
 * Returns 0 on success and -1 on failure 
 */
int Mem_Init(int sizeOfRegion){
  int pagesize;
  int padsize;
  int fd;
  int alloc_size;
  void* space_ptr;
  static int allocated_once = 0;
  
  if(0 != allocated_once){
    fprintf(stderr,"Error:mem.c: Mem_Init has allocated space during a previous call\n");
    return -1;
  }
  if(sizeOfRegion <= 0){
    fprintf(stderr,"Error:mem.c: Requested block size is not positive\n");
    return -1;
  }

  // Get the pagesize
  pagesize = getpagesize();

  // Calculate padsize as the padding required to round up sizeOfRegion to a multiple of pagesize
  padsize = sizeOfRegion % pagesize;
  padsize = (pagesize - padsize) % pagesize;

  alloc_size = sizeOfRegion + padsize;

  // Using mmap to allocate memory
  fd = open("/dev/zero", O_RDWR);
  if(-1 == fd){
    fprintf(stderr,"Error:mem.c: Cannot open /dev/zero\n");
    return -1;
  }
  space_ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (MAP_FAILED == space_ptr){
    fprintf(stderr,"Error:mem.c: mmap cannot allocate space\n");
    allocated_once = 0;
    return -1;
  }
  
  allocated_once = 1;
  
  // Intialising total available memory size
  total_mem_size = alloc_size;

  // To begin with there is only one big free block
  first_block = (block_tag*) space_ptr;
  
  // Setting up the header
  first_block->size_status = alloc_size;
  // Marking the previous block as busy
  first_block->size_status += 2;

  // Setting up the footer
  block_tag *footer = (block_tag*)((char*)first_block + alloc_size - 4);
  footer->size_status = alloc_size;
  
  return 0;
}

/* 
 * Function to be used for debugging 
 * Prints out a list of all the blocks along with the following information for each block 
 * No.      : serial number of the block 
 * Status   : free/busy 
 * Prev     : status of previous block free/busy
 * t_Begin  : address of the first byte in the block (this is where the header starts) 
 * t_End    : address of the last byte in the block 
 * t_Size   : size of the block (as stored in the block header)(including the header/footer)
 */ 
void Mem_Dump() {
  int counter;
  char status[5];
  char p_status[5];
  char *t_begin = NULL;
  char *t_end = NULL;
  int t_size;

  block_tag *current = first_block;
  counter = 1;

  int busy_size = 0;
  int free_size = 0;
  int is_busy = -1;

  fprintf(stdout,"************************************Block list***********************************\n");
  fprintf(stdout,"No.\tStatus\tPrev\tt_Begin\t\tt_End\t\tt_Size\n");
  fprintf(stdout,"---------------------------------------------------------------------------------\n");
  
  while(current < (block_tag*)((char*)first_block + total_mem_size)){

    t_begin = (char*)current;
    
    t_size = current->size_status;
    
    if(t_size & 1){
      // LSB = 1 => busy block
      strcpy(status,"Busy");
      is_busy = 1;
      t_size = t_size - 1;
    }
    else{
      strcpy(status,"Free");
      is_busy = 0;
    }

    if(t_size & 2){
      strcpy(p_status,"Busy");
      t_size = t_size - 2;
    }
    else strcpy(p_status,"Free");

    if (is_busy) busy_size += t_size;
    else free_size += t_size;

    t_end = t_begin + t_size - 1;
    
    fprintf(stdout,"%d\t%s\t%s\t0x%08lx\t0x%08lx\t%d\n",counter,status,p_status,
                    (unsigned long int)t_begin,(unsigned long int)t_end,t_size);
    
    current = (block_tag*)((char*)current + t_size);
    counter = counter + 1;
  }
  fprintf(stdout,"---------------------------------------------------------------------------------\n");
  fprintf(stdout,"*********************************************************************************\n");

  fprintf(stdout,"Total busy size = %d\n",busy_size);
  fprintf(stdout,"Total free size = %d\n",free_size);
  fprintf(stdout,"Total size = %d\n",busy_size+free_size);
  fprintf(stdout,"*********************************************************************************\n");
  fflush(stdout);
  return;
}
