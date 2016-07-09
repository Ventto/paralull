#ifndef _PLL_QUEUE_H
#define _PLL_QUEUE_H

#include <stdint.h>

#define CELLS_NUMBER	1024

struct pll_enqreq {
	void *val;
	struct { int pending : 1; uint64_t id : 63; } state;
};

struct pll_deqreq {
	uint64_t id : 64;
	struct { int pending : 1; uint64_t id : 63; } state;
};

struct pll_cell {
	void *val;
	struct pll_enqreq *enq;
	struct pll_deqreq *deq;
};

struct pll_segment {
	uint64_t id : 64;
	struct pll_segment *next;
	struct pll_cell cells[CELLS_NUMBER];
};

struct pll_queue {
	struct pll_segment *q;
	uint64_t tail : 64;
	uint64_t head : 64;
};

struct pll_handle {
	struct pll_segment *tail, *head;
	struct pll_handle *next;
	struct { struct pll_enqreq *req; struct pll_handle *peer; } enq;
	struct { struct pll_deqreq *req; struct pll_handle *peer; } deq;
};

#endif /* _PLL_QUEUE_H */
