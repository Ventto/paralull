#ifndef _PLL_QUEUE_H
#define _PLL_QUEUE_H

#include <stdint.h>

#define CELLS_NUMBER	1024

struct queue_enqreq {
	void *val;
	struct { int pending : 1; uint64_t id : 63; } state;
};

struct queue_deqreq {
	uint64_t id : 64;
	struct { int pending : 1; uint64_t id : 63; } state;
};

struct queue_cell {
	void *val;
	struct queue_enqreq *enq;
	struct queue_deqreq *deq;
};

struct queue_segment {
	uint64_t id : 64;
	struct queue_segment *next;
	struct queue_cell cells[CELLS_NUMBER];
};

struct pll_queue {
	struct queue_segment *q;
	uint64_t tail : 64;
	uint64_t head : 64;
};

struct queue_handle {
	struct queue_segment *tail, *head;
	struct queue_handle *next;
	struct { struct queue_enqreq *req; struct queue_handle *peer; } enq;
	struct { struct queue_deqreq *req; struct queue_handle *peer; } deq;
};

#endif /* _PLL_QUEUE_H */
