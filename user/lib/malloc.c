#include <lib.h>

#define MALLOC_MAGIC 0x12345678
#define ALIGN 8
#define ALIGNED(x) (((x) + ALIGN - 1) & ~(ALIGN - 1))

struct malloc_header {
    u_int magic;
    u_int size;
    struct malloc_header *next;
    struct malloc_header *prev;
};

static struct malloc_header *free_list = NULL;
static void *heap_start = NULL;
static void *heap_end = NULL;
static u_int heap_size = 0;

void *malloc(size_t size) {
    if (size == 0) return NULL;
    
    size = ALIGNED(size + sizeof(struct malloc_header));
    
    if (!heap_start) {
        heap_size = PTMAP;
        if (syscall_mem_alloc(0, (void*)UTEMP, PTE_D) < 0) {
            return NULL;
        }
        heap_start = (void*)UTEMP;
        heap_end = heap_start + heap_size;
        
        struct malloc_header *h = (struct malloc_header*)heap_start;
        h->magic = MALLOC_MAGIC;
        h->size = heap_size - sizeof(struct malloc_header);
        h->next = NULL;
        h->prev = NULL;
        free_list = h;
    }
    
    struct malloc_header *h = free_list;
    while (h) {
        if (h->size >= size) {
            if (h->size > size + sizeof(struct malloc_header) + ALIGN) {
                struct malloc_header *new_h = (struct malloc_header*)((char*)h + size);
                new_h->magic = MALLOC_MAGIC;
                new_h->size = h->size - size;
                new_h->next = h->next;
                new_h->prev = h->prev;
                
                if (h->prev) h->prev->next = new_h;
                else free_list = new_h;
                if (h->next) h->next->prev = new_h;
                
                h->size = size - sizeof(struct malloc_header);
            } else {
                if (h->prev) h->prev->next = h->next;
                else free_list = h->next;
                if (h->next) h->next->prev = h->prev;
            }
            
            h->next = NULL;
            h->prev = NULL;
            return (char*)h + sizeof(struct malloc_header);
        }
        h = h->next;
    }
    
    return NULL;
}

void free(void *ptr) {
    if (!ptr) return;
    
    struct malloc_header *h = (struct malloc_header*)((char*)ptr - sizeof(struct malloc_header));
    if (h->magic != MALLOC_MAGIC) {
        user_panic("free: bad magic");
    }
    
    h->next = free_list;
    h->prev = NULL;
    if (free_list) free_list->prev = h;
    free_list = h;
}