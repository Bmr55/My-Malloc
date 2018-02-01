#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "mymalloc.h"

/*Ooooh, pretty colors...*/
#define CRESET   "\e[0m"
#define CRED     "\e[31m"
#define CGREEN   "\e[32m"
#define CYELLOW  "\e[33m"
#define CBLUE    "\e[34m"
#define CMAGENTA "\e[35m"
#define CCYAN    "\e[36m"

/*You can use these in printf format strings like:

	printf("This is " RED("red") " text.\n");

NO COMMAS!!*/
#define RED(X)     CRED X CRESET
#define GREEN(X)   CGREEN X CRESET
#define YELLOW(X)  CYELLOW X CRESET
#define BLUE(X)    CBLUE X CRESET
#define MAGENTA(X) CMAGENTA X CRESET
#define CYAN(X)    CCYAN X CRESET

#define PTR_ADD_BYTES(ptr, byte_offs) ((void*)(((char*)(ptr)) + (byte_offs)))

void check_heap_size(const char* where, void* heap_at_start)
{
	void* heap_at_end = sbrk(0);
	unsigned int heap_size_diff = (unsigned int)(heap_at_end - heap_at_start);

	if(heap_size_diff)
		printf(RED("After %s the heap got bigger by %u (0x%X) bytes...\n"),
			where, heap_size_diff, heap_size_diff);
	else
		printf(GREEN("Yay, after %s, everything was freed!\n"), where);
}

void fill_array(int* arr, int length)
{
	int i;

	for(i = 0; i < length; i++)
		arr[i] = i + 1;
}

int* make_array(int length)
{
	int* ret = my_malloc(sizeof(int) * length);
	fill_array(ret, length);
	return ret;
}

uint64_t get_usec()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000000 + tv.tv_usec;
}

void test_basic_binning()
{
	void* heap_at_start = sbrk(0);

	void* a = make_array(4);
	void* b = make_array(4);
	void* c = make_array(4);
	void* d = make_array(4);

	my_free(a);
	my_free(c);

	void* heap_after_frees = sbrk(0);

	/* AT THIS POINT, your 16-byte bin should have two blocks: c at the head, and a after it.

	These next two mallocs should give me the blocks c and then a, in that order.*/

	void* cc = make_array(4);
	void* aa = make_array(4);

	void* heap_after_mallocs = sbrk(0);

	if(heap_after_mallocs != heap_after_frees)
		printf(RED("You didn't reuse the freed blocks.\n"));
	else
	{
		if(cc == c && aa == a)
			printf(GREEN("test_basic_binning checks out.\n"));
		else if(cc == a && aa == c)
			printf(YELLOW("You did reuse the blocks, but in the wrong order.\n"
				"Make sure you're not trying to find the tail of the bin's free list.\n"));
		else
			printf(RED("You reused the blocks but returned some really wrong pointers...\n"));
	}

	my_free(b);
	my_free(d);
	my_free(cc);
	my_free(aa);

	check_heap_size("test_basic_binning", heap_at_start);
}

void test_speed(long num_iter)
{
	void* blocks[num_iter * 2]; /*biiiig stack array...*/
	void* heap_at_start = sbrk(0);

	/* Step 1: allocate a buncha blocks, and free every other one, so we have a ton of
	stuff in one bin. (Freeing every other one to avoid coalescing) */
	uint64_t start_time = get_usec();

	int i;
	for(i = 0; i < num_iter; i++)
	{
		blocks[2 * i] = make_array(8);
		blocks[2 * i + 1] = make_array(8);
	}

	for(i = 0; i < num_iter; i++)
		my_free(blocks[2 * i]);

	uint64_t step1_time = get_usec() - start_time;

	/* Step 2: reallocate half as many blocks, which should reuse every freed block from
	step 1. */
	void* heap_before_step2 = sbrk(0);
	start_time = get_usec();

	for(i = 0; i < num_iter; i++)
	{
		blocks[2 * i] = make_array(8);
	}

	uint64_t step2_time = get_usec() - start_time;

	void* heap_after_step2 = sbrk(0);

	/* Step 3: free everything. */
	start_time = get_usec();

	for(i = 0; i < num_iter; i++)
	{
		my_free(blocks[2 * i]);
		my_free(blocks[2 * i + 1]);
	}

	uint64_t step3_time = get_usec() - start_time;

	/* Now to check everything. */
	check_heap_size("test_speed", heap_at_start);

	unsigned int heap_size_diff = (unsigned int)(heap_after_step2 - heap_before_step2);

	/* if the heap grew by 48000000 (0x2DC6C00) bytes, you didn't reuse anything. */
	if(heap_size_diff)
		printf(RED("The heap grew after step 2 by %u (0x%X) bytes, but you should've reused"
			" every block instead.\n"), heap_size_diff, heap_size_diff);
	else
		printf(GREEN("The heap didn't grow during step 2!\n"));

	/*
	doing naive, searching-entire-phys-list allocation, with num_iter == 10,000:
		The original allocation + filling bins took 1.46 seconds
		Reusing the free blocks took 0.73 seconds
		Freeing everything took 0.00 seconds

	doing proper binning/quick-fit, with num_iter == 1,000,000:
		The original allocation + filling bins took 1.60 seconds
		Reusing the free blocks took 0.13 seconds
		Freeing everything took 0.25 seconds
	*/
	printf("The original allocation + filling bins took %.2f seconds\n", step1_time / 1000000.0);
	printf("Reusing the free blocks took %.2f seconds\n", step2_time / 1000000.0);
	printf("Freeing everything took %.2f seconds\n", step3_time / 1000000.0);
}

/*
Run this program like:

	./bin_stress 1000

The larger the number, the more blocks it will allocate in the speed test.

IF YOU DIDN'T IMPLEMENT ANY FREE LISTS AT ALL, and your "find a free block" algorithm is "look
through the entire heap", then you will only be able to use about 10000. Bigger numbers will take
forever.

IF YOU IMPLEMENTED BINNING CORRECTLY, you will be able to do 1000000 with no problem. It should take
about 1-4 seconds for the first step and < 1 second for each steps 2 and 3.

IF YOU IMPLEMENTED THE OVERFLOW BIN, but none of the other bins, it will still complete pretty
quickly, but only cause you got lucky and managed to "best-fit" on the first block every time. Be
sure to test your code more thoroughly in your driver.
*/
int main(int argc, char** argv)
{
	long num_iter;;

	if(argc == 1)
	{
		printf("Usage:\n\t./bin_stress <num_iter>\nWhere num_iter is an int >= 1000.\n");
		return 1;
	}
	else
	{
		num_iter = strtol(argv[1], NULL, 10);

		if(num_iter < 1000)
		{
			printf("You gotta use at least 1000 iterations.\n");
			return 1;
		}
	}

	void* heap_at_start = sbrk(0);

	test_basic_binning();
	test_speed(num_iter);

	check_heap_size("main", heap_at_start);
	return 0;
}
