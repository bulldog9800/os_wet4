#include <unistd.h>
#include <stdio.h>
#include <assert.h>

void* smalloc(size_t size){
	if (size == 0){
		return nullptr;
	}

	if (size > 100000000){
		return nullptr;
	}

	int status = sbrk(size);
	if (status == (void*)-1){
		return nullptr;
	}

	return status;
}