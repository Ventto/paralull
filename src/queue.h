#ifndef _PLL_QUEUE_H
#define _PLL_QUEUE_H

#include <stdint.h>

#define CELLS_NUMBER	1024


union queue_reqstate {
	uint64_t u64;
	struct {
		uint8_t pending	: 1;
		uint64_t id		: 63;
	} s;
};

struct queue_enqreq {
	void *val;
	union queue_reqstate state;
};

struct queue_deqreq {
	uint64_t id;
	union queue_reqstate state;
};

struct queue_cell {
	void *val;
	struct queue_enqreq *enq;
	struct queue_deqreq *deq;
};

struct queue_segment {
	uint64_t id;
	struct queue_segment *next;
	struct queue_cell cells[CELLS_NUMBER];
};

struct pll_queue {
	struct queue_segment *q;
	uint64_t tail;
	uint64_t head;
	int64_t oldseg;
	struct queue_handle *hndl_ring;
	pthread_key_t hndlk;
};

struct queue_enqueue {
	struct queue_enqreq req;
	struct queue_handle *peer;
};

struct queue_dequeue {
	struct queue_deqreq req;
	struct queue_handle *peer;
};

struct queue_handle {
	struct queue_segment *tail, *head;
	struct queue_handle *next;
	struct queue_enqueue enq;
	struct queue_dequeue deq;
	struct queue_segment *hzdp;
};

#endif /* _PLL_QUEUE_H */
