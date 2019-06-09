#include <xeroskernel.h>

#ifndef DELTALIST_H
#define DELTALIST_H

typedef struct delta_list {
    size_t size;
    pcb_t *head;
} DeltaList;

void init_delta_list(DeltaList *list);
void insert(DeltaList *list, pcb_t *proc, int delay);
pcb_t *poll(DeltaList *list);
pcb_t *delta_peek(DeltaList *list);
int delta_is_empty(DeltaList *list);
int delta_size(DeltaList *list);
void delta_print_list(DeltaList *list);
int delta_remove(DeltaList *list, pcb_t *proc);

#endif