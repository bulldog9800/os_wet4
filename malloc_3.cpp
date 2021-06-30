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

static MallocMetadata* listGetTail(){
    if ( list_head == nullptr ){
        return nullptr;
    }

    MallocMetadata* it = list_head;
    while (it->next){
        it = it->next;
    }

    return it;
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
    if (!entry->is_free){
        return;
    }
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

    MallocMetadata* split = (MallocMetadata*) ( ( (char*)  block + size_of_metadata + size) );
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
static bool mergeNextBlock(MallocMetadata* block) {
    assert(block);
    if (block->next == nullptr){
        return false;
    }
    if (!block->next->is_free){
        return false;
    }
    block->next = block->next->next;
    if(block->next){
        block->next->prev = block;
    }

    block->size += size_of_metadata + block->next->size;
    return true;
}

void* smalloc(size_t size){
    if(size==0||size>100000000){
        return nullptr ;
    }
    if (size < 128*KILO) {
       MallocMetadata* free_block = hist_search(size);
       if ( !free_block ) {
           /******** No free large enough block was found ********/

           /******** Wilderness block *************/
           MallocMetadata* last_block = listGetTail();
           if ( last_block && last_block->is_free ) {
               size_t diff = size - last_block->size;
               void* addr = sbrk(diff);
               if (addr == (void*) -1){
                   return nullptr;
               }
               last_block->is_free = false;
               last_block->size = size;
               return  (((char*) last_block) + size_of_metadata);
           }

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

       if ( (free_block->size - size) >= (size_of_metadata + 128) ){
           /******** Need to split the block ***********/
           splitBlock(free_block, size);
           free_block->is_free = false;
           return (((char*) free_block) + size_of_metadata);
       } else {
           /******** No need to split the block ***********/
           free_block->is_free = false;
           return  (((char*) free_block) + size_of_metadata);
       }
    } else {
        void* mmap_addr = mmap(nullptr, size + size_of_metadata, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if(mmap_addr == (void*)(-1)){
            return nullptr;
        }
        if(mmap_list_head == nullptr){
            mmap_list_head = (MallocMetadata*)mmap_addr;
            mmap_list_head->is_free = false;
            mmap_list_head->size = size;
            mmap_list_head->next = nullptr;
            mmap_list_head->prev = nullptr;
            return (((char*) mmap_list_head) + size_of_metadata);
        }
        MallocMetadata* it = mmap_list_head;
        while(it->next){
            it = it->next;
        }
        MallocMetadata* new_block = (MallocMetadata*)mmap_addr;
        it->next = new_block;
        new_block->next = nullptr;
        new_block->prev = it;
        new_block->is_free = false;
        new_block->size = size;
        return (((char*) new_block) + size_of_metadata);
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
    if (p == nullptr){
        return;
    }
    MallocMetadata *metadata = (MallocMetadata *) ((char *) p - size_of_metadata);
    if (metadata->is_free){
        return;
    }
    if(metadata->size <= 128 * KILO) {
        metadata->is_free = true;
        if (metadata->next->is_free){
            hist_remove(metadata->next);
        }
        if (metadata->prev->is_free){
            hist_remove(metadata->prev);
        }
        mergeNextBlock(metadata);
        if ( mergeNextBlock(metadata->prev) ){
            hist_insert(metadata->prev);
        } else{
            hist_insert(metadata);
        }
    }else{
        metadata->is_free = true;
        MallocMetadata* next_meta = metadata->next;
        MallocMetadata* prev_meta = metadata->prev;
        if(metadata == mmap_list_head){
            mmap_list_head = next_meta;
        }
        if(next_meta != nullptr){
            next_meta->prev = prev_meta;
        }
        if(prev_meta != nullptr){
            prev_meta->next = next_meta;
        }
        void* block_address = (void*)((char *) p - size_of_metadata);
        munmap(block_address , metadata->size + size_of_metadata);
    }
}

void* srealloc(void* oldp, size_t size){
    if(size==0 || size>100000000){
        return nullptr;
    }

    if (oldp == nullptr) {
        return smalloc(size);
    }

    MallocMetadata* metadata = (MallocMetadata*) (((char*) oldp) - size_of_metadata);
    hist_remove(metadata);
    if (metadata->size <= KILO * 128) {
        if (size <= metadata->size ){
            metadata->is_free = false;
            //return oldp;
        }
        else if (!(metadata->next) && metadata->size < size) { // wilderness
            size_t diff = size - metadata->size;
            void* addr = sbrk(diff);
            if (addr == (void*) -1) {
                return nullptr;
            }
            metadata->size = size;
        }
        else if((metadata->prev) && (metadata->prev->is_free) &&
                ((metadata->prev->size + metadata->size + size_of_metadata) >= size) ){
            hist_remove(metadata->prev);
            metadata->prev->is_free = false;
            metadata->prev->next = metadata->next;
            metadata->prev->size = metadata->prev->size + size_of_metadata + metadata->size;
            if (metadata->next){
                metadata->next->prev = metadata->prev; //TODO: may be wrong
            }
            std::memmove(((char*)metadata->prev + size_of_metadata), ((char*)metadata + size_of_metadata), metadata->size);
            metadata = metadata->prev;
        }
        else if ((metadata->next) && (metadata->next->is_free) &&
                 ((metadata->next->size + metadata->size + size_of_metadata) >= size)){
            hist_remove(metadata->next);
            metadata->is_free = false;
            metadata->size = metadata->next->size + metadata->size + size_of_metadata;
            metadata->next = metadata->next->next;
            if (metadata->next){
                metadata->next->prev = metadata;
            }
        }
        else if ((metadata->next) && (metadata->next->is_free) && (metadata->prev) && (metadata->prev->is_free)
        && ((metadata->prev->size + size_of_metadata + metadata->size + size_of_metadata + metadata->next->size) >= size) ){
            hist_remove(metadata->prev);
            hist_remove(metadata->next);
            metadata->prev->is_free = false;
            metadata->prev->size = metadata->prev->size + size_of_metadata + metadata->size + size_of_metadata + metadata->next->size;
            std::memmove(((char*)metadata->prev + size_of_metadata), ((char*)metadata + size_of_metadata), metadata->size);
            metadata->prev->next = metadata->next->next;
            if (metadata->prev->next) {
                metadata->prev->next->prev = metadata->prev;
            }
            metadata = metadata->prev;
        } else{
            void* addr = smalloc(size);
            if (!addr){
                return nullptr;
            }
            std::memmove(addr, (char*)metadata + size_of_metadata, size);
            sfree((char*)metadata + size_of_metadata);
            return addr;
        }

        if (metadata->size >= size + size_of_metadata + 128){
            splitBlock(metadata, size);
        }

        return (char *)metadata + size_of_metadata;
    } else{
        void* mmapp_address = smalloc(size);
        if(mmapp_address == nullptr){
            return nullptr;
        }
        MallocMetadata* mmap_metadata = (MallocMetadata*)((char*)mmapp_address - size_of_metadata);
        if(size < metadata->size){
            std::memmove(mmapp_address, (char *)metadata + size_of_metadata, size);
        }else{
            std::memmove(mmapp_address, (char *)metadata + size_of_metadata, metadata->size);
        }
        sfree(oldp);
        return (char*)mmap_metadata + size_of_metadata;
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

