#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <cstring>

struct MallocMetadata {
    size_t size ;
    bool is_free ;
    MallocMetadata* next ;
    MallocMetadata* prev ;

};

MallocMetadata* list_head = nullptr;
size_t size_of_metadata = sizeof(MallocMetadata);

void* smalloc(size_t size){
    if(size==0||size>=100000000) {
        return nullptr;
    }
    if(list_head== nullptr){
        void* head= sbrk(size + size_of_metadata);
        if(head == (void *) (-1)){
            return nullptr ;
        }
        list_head = (MallocMetadata*) head;
        list_head->next = nullptr;
        list_head->prev = nullptr;
        list_head->size = size;
        list_head->is_free = false;
        return  (char*)head + size_of_metadata;

    }
    else{
        MallocMetadata* it =list_head ;
        while (it->next){
            if(it->size>=size && it->is_free){
                it->is_free=false;
                return (char*)it + size_of_metadata ;
            }
            it=it->next;

        }
        if (it->size>=size && it->is_free){
            it->is_free=false;
            return (char*)it + size_of_metadata ;

        }
        else{
            void* head= sbrk(size + size_of_metadata);
            if(head == (void *) (-1)){
                return nullptr ;
            }
            it->next=(MallocMetadata*)head;
            it->next->next= nullptr;
            it->next->size=size;
            it->next->is_free= false;
            it->next->prev=it ;
            return (char*)it->next+size_of_metadata ;

        }

    }

}



void* scalloc(size_t num, size_t size){
    size_t size_num=num*size;
    if(size_num==0||size_num>=100000000){
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

