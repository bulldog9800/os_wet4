#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <cstring>

#define KILO 1024
#define HIST_SIZE 128

struct MallocMetadata {
    size_t size ;
    bool is_free ;
    MallocMetadata* next ;
    MallocMetadata* prev ;
    MallocMetadata* next2;
    MallocMetadata* prev2;
};

MallocMetadata* hist[128] = {};
MallocMetadata* list_head = nullptr;
MallocMetadata* mmap_list_head = nullptr;
size_t size_of_metadata = sizeof(MallocMetadata);

static void listInsertToTail(MallocMetadata* entry){
    if ( list_head == nullptr ){
        list_head = entry;
        entry->next = nullptr;
        entry->prev = nullptr;
        return;
    }

    MallocMetadata* it = list_head;
    while (it->next){
        it = it->next;
    }

    it->next = entry;
    entry->prev = it;
    entry->next = nullptr;
}


/***
 * Insert an entry into the histogram.
 *
 * Inserts the entry in index size/1024 (example: an entry of size 800 will go in index 0, an entry of size
 * 2000 will go in index 1).
 *
 * @param entry: The entry.
 */
void hist_insert( MallocMetadata* entry ){
    assert(entry->is_free);
    int index = entry->size / KILO;
    MallocMetadata* it = hist[index];
    if (!it) {
        hist[index] = entry;
        entry->prev2 = nullptr;
        entry->next2 = nullptr;
        return;
    }
    while (it->next2) {
        if (entry->size > it->size){
            MallocMetadata* tmp = it->next2;
            it->next2 = entry;
            entry->prev2 = it;
            entry->next2 = tmp;
            tmp->prev2 = entry;
            break;
        }
        it = it->next2;
    }
    if (entry->size > it->size){
        MallocMetadata* tmp = it->next2;
        it->next2 = entry;
        entry->prev2 = it;
        entry->next2 = tmp;
        tmp->prev2 = entry;
    } else{
        it->next2 = entry;
        entry->prev2 = it;
        entry->next2 = nullptr;
    }
}

/***
 * Removes an entry from the histogram. This function assumes that the argument
 * provided is a valid address to a metadata struct and that it is already in the hist.
 *
 * @param entry: the entry.
 */
void hist_remove( MallocMetadata* entry ){
    int index = entry->size / KILO;
    if ( !(entry->prev2) ) {
        hist[index] = entry->next2;
        entry->next2 = nullptr;
    }
    else{
        MallocMetadata* tmp = entry->prev2;
        tmp->next2 = entry->next2;
        if ( entry->next2 ) {
            entry->next2->prev2 = tmp;
        }
        entry->next2 = nullptr;
        entry->prev2 = nullptr;
    }
}

/***
 * Finds and removes a block of the given size. This function does not change the metadata
 * that it returns to mark it as not free, it should be done outside the function.
 *
 * @param size
 * @return A metadata block of at least size or NULL if no block was found.
 */
MallocMetadata* hist_search(size_t size) {
    size_t index = size / KILO;
    while (index < HIST_SIZE){
        MallocMetadata* it = hist[index];

        while ( it ){
            if (it->size >= size){
                hist_remove(it);
                return it;
            }

            it = it->next2;
        }

        index++;
    }

    return nullptr;
}

/************* CHALLENGE 1 *************/
static void splitBlock(MallocMetadata* block, size_t size) {
    assert(block);

    MallocMetadata* split = (MallocMetadata*) ( ( (char*)  block + size_of_metadata + block->size) );
    split->size = block->size - size - size_of_metadata;

    split->is_free = true;
    split->prev = block;
    split->next = block->next;
    if (split->next) {
        split->next->prev = split;
    }
    hist_insert(split);

    block->next = split;
    block->size = size;
}

/************* CHALLENGE 2 *************/
static void mergeNextBlock(MallocMetadata* block) {
    assert(block);
    if (block->next == nullptr){
        return;
    }
    if (!block->next->is_free){
        return;
    }
    block->next = block->next->next;
    if(block->next){
        block->next->prev = block;
    }

    block->size += size_of_metadata + block->next->size;
}

void* smalloc(size_t size){
    if(size==0||size>100000000){
        return nullptr ;
    }
    if (size < 128*KILO) {
       MallocMetadata* free_block = hist_search(size);
       if ( !free_block ) {
           /******** No free large enough block was found ********/
           //TODO: Wilderness block

           void* block_start = sbrk(size + size_of_metadata);
           if ( block_start == (void*) -1 ){
               return nullptr;
           }
           MallocMetadata* metadata = (MallocMetadata*) block_start;
           metadata->size = size;
           metadata->is_free = false;
           listInsertToTail(metadata);
           return (((char*)block_start) + size_of_metadata);
       }
       /***** A free block large enough was found *****/

       /******** Need to split the block ***********/
       if ( (free_block->size - size) >= (size_of_metadata + 128) ){

       }

    }

}



void* scalloc(size_t num, size_t size){
    size_t size_num=num*size;
    if(size_num==0||size_num>100000000){
        return nullptr ;
    }
    void* address = smalloc(size_num);
    if(address== nullptr){
        return nullptr ;
    }
    std::memset(address,0,size_num);
    return address ;
}



void sfree(void* p){
    if(p==nullptr){
        return;
    }
    MallocMetadata* to_free =(MallocMetadata*)((char*) p - size_of_metadata);
    to_free->is_free=true ;
    return;
}

void* srealloc(void* oldp, size_t size){
    if(size==0||size>100000000){
        return nullptr;
    }

    if (oldp == nullptr) {
        return smalloc(size);
    }

    MallocMetadata* old_metadata = (MallocMetadata*) ( ((char*) oldp) - size_of_metadata);
    if (old_metadata->size < size) {
        void* new_data = smalloc(size);
        if (!new_data){
            return nullptr;
        }
        std::memcpy(new_data, oldp, size);
        sfree(oldp);
        return new_data;
    }
    else {
        return oldp;
    }
}


size_t _num_free_blocks(){
    if(!list_head){
        return 0;
    }
    MallocMetadata* it=list_head;
    size_t num_free = 0;
    while (it){
        if(it->is_free){
            num_free++;
        }
        it=it->next;
    }
    return  num_free ;
}


size_t _num_free_bytes(){
    if(!list_head){
        return 0;
    }
    MallocMetadata* it=list_head;
    size_t num_free_bytes = 0;
    while (it){
        if(it->is_free){
            num_free_bytes+=it->size;
        }
        it=it->next;
    }
    return num_free_bytes ;
}

size_t _num_allocated_blocks(){
    if(!list_head){
        return 0;
    }
    MallocMetadata* it=list_head;
    size_t num_alo = 0;
    while (it){
        num_alo++;
        it=it->next;
    }
    return  num_alo ;
}


size_t _num_allocated_bytes(){
    if(!list_head){
        return 0;
    }
    MallocMetadata* it=list_head;
    size_t num_alo_bytes = 0;
    while (it){
        num_alo_bytes+=it->size;
        it=it->next;
    }
    return  num_alo_bytes ;
}

size_t _num_meta_data_bytes(){
    return _num_allocated_blocks()*size_of_metadata ;
}



size_t _size_meta_data(){
    return size_of_metadata ;
}

