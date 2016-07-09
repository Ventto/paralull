#include <stdlib.h>

#include "paralull.h"
#include "queue.h"

static struct queue_segment *new_segment(int id)
{
	struct queue_segment *seg = malloc(sizeof(struct queue_segment));

	seg->id = id;
	seg->next = NULL;
	for (int i = 0; i < CELLS_NUMBER; ++i) {
		seg->cells[i] = (struct queue_cell) {
			.val = NULL,
			.enq = NULL,
			.deq = NULL
		};
	}
}

pll_queue pll_queue_init(void)
{
	return NULL;
}

void pll_queue_term(pll_queue q)
{
}

void pll_enqueue(pll_queue q, void *val)
{
}

void *pll_dequeue(pll_queue q)
{
	return NULL;
}

bool pll_queue_empty(pll_queue q)
{
	return 0;
}

