/* 
Author: Benjamin Runco
A small test for mymalloc.c
*/

#include <stdio.h>
#include <unistd.h>
#include "mymalloc.h"

void test1()
{
	int* a = my_malloc(24);
	int* b = my_malloc(2000);
	int* c = my_malloc(56);
	int* d = my_malloc(64);
	int* e = my_malloc(200);
	int* f = my_malloc(16);
	int* g = my_malloc(64);
	int* h = my_malloc(40);
	int* i = my_malloc(800);
	int* j = my_malloc(512);



	my_free(f);
	my_free(a);
	my_free(c);
	my_free(j);
	my_free(g);
	my_free(e);
	my_free(h);
	my_free(i);
	my_free(b);
	my_free(d);
}

int main()
{
	void* heap_at_start = sbrk(0);

	test1();

	// The code below and at the beginning of the function checks
	// that you contracted the heap properly (assuming you've freed
	// everything that you allocated).

	void* heap_at_end = sbrk(0);
	unsigned int heap_size_diff = (unsigned int)(heap_at_end - heap_at_start);

	if(heap_size_diff)
		printf("Hmm, the heap got bigger by %u (0x%X) bytes...\n", heap_size_diff, heap_size_diff);

	return 0;
}
