/* queue.h */

#include <xeroskernel.h>

#ifndef QUEUE_H
#define QUEUE_H

void init_queue(Queue *queue);
void enqueue(Queue *queue, pcb_t *proc);
pcb_t *dequeue(Queue *queue);
pcb_t *peek_tail(Queue *queue);
int is_empty(Queue *queue);
int size(Queue *queue);
void print_queue(Queue *queue);
void remove(Queue *queue, pcb_t *proc);

#endif
