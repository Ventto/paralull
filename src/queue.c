#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

#include "atomic.h"
#include "paralull.h"
#include "queue.h"
#include "atomic.h"

#define PATIENCE	42

#define DEQUEUE_TOP		-1
#define DEQUEUE_BOTTOM	-2
#define ENQUEUE_TOP		-3
#define ENQUEUE_BOTTOM	-4
#define QUEUE_TOP		-5
#define QUEUE_BOTTOM	-6
#define QUEUE_EMPTY		-7

static struct queue_segment *new_segment(uint64_t id)
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
	return seg;
}

static int handle_init(struct pll_queue *q)
{
	struct queue_handle *h = malloc(sizeof (*h));
	if (!h)
		goto err;

	*h = (struct queue_handle) {
		.tail = q->q,
		.head = q->q,
		.next = h,
	};

	if (pthread_setspecific(q->hndlk, h))
		goto err;

	if (!q->hndl_ring) {
		q->hndl_ring = h;
	} else {
		for (;;) {
			struct queue_handle *next = q->hndl_ring->next;
			h->next = next;
			if (pll_cas(&q->hndl_ring->next, next, h))
				break;
		}
	}
	return 0;
err:
	if (h)
		free(h);
	return -errno;
}

pll_queue pll_queue_init(void)
{
	int rc;

	pll_queue queue = malloc(sizeof (*queue));
	if (!queue)
		goto err;

	pthread_key_t key;
	if ((rc = pthread_key_create(&key, NULL)))
		goto err;

	*queue = (struct pll_queue) {
		.q = new_segment(0),
		.hndlk = key,
	};

	if (handle_init(queue) < 0)
		goto err;

	return queue;

err:
	if (queue) {
		free(queue->q);
		free(queue);
	}
	if (rc) {
		pthread_key_delete(key);
	}
	return NULL;
}

void pll_queue_term(pll_queue q)
{
	for (struct queue_handle *h = q->hndl_ring->next; h != q->hndl_ring; ) {
		struct queue_handle *next = h->next;
		free(h);
		h = next;
	}
	free(q->hndl_ring);
	for (struct queue_segment *s = q->q; s; ) {
		struct queue_segment *next = s->next;
		free(s);
		s = next;
	}
	free(q);
}

static struct queue_handle *get_handle()
{
	return NULL;
}

static void advance_end_for_linearizability(uint64_t *E, uint64_t cell_id)
{
	uint64_t e = *E;
	do e = *E; while (e < cell_id && !pll_cas(E, e, cell_id));
}

static void enq_commit(pll_queue q,
						struct queue_cell *cell,
						void *val,
						uint64_t cell_id)
{
	advance_end_for_linearizability(&q->tail, cell_id + 1);
	cell->val = val;
}

static void *find_cell(struct queue_segment **sp, uint64_t cell_id)
{
	struct queue_segment *seg = *sp;
	struct queue_segment *next;

	for (uint64_t i = seg->id; i < cell_id / CELLS_NUMBER; ++i) {
		next = seg->next;
		if (next == NULL) {
			struct queue_segment *tmp = new_segment(i + 1);

			if (!pll_cas(&seg->next, NULL, tmp))
				free(tmp);
			next = seg->next;
		}
		seg = next;
	}
	*sp = seg;
	return &seg->cells[cell_id % CELLS_NUMBER];
}

static bool try_to_claim_req(uint64_t *state, uint64_t id, uint64_t cell_id)
{
	union queue_reqstate s_val1 = { .s.pending = 1, .s.id = id };
	union queue_reqstate s_val2 = { .s.pending = 0, .s.id = cell_id };

	return pll_cas(state, s_val1.u64, s_val2.u64);
}

static void enq_slow(pll_queue q, void *val, uint64_t cell_id)
{
	struct queue_segment *tmp_tail = get_handle()->tail;
	struct queue_enqreq *req = &get_handle()->enq.req;

	req->val = val;
	req->state = (union queue_reqstate) { .s.pending = 1, .s.id = cell_id };

	do {
		uint64_t i = pll_faa(&q->tail, 1);
		struct queue_cell *cell = find_cell(&tmp_tail, i);

		if (pll_cas(&cell->enq, NULL, req) && cell->val == NULL) {
			try_to_claim_req(&req->state.u64, cell_id, i);
			break;
		}
	} while (req->state.s.pending);

	uint64_t id = req->state.s.id;
	struct queue_cell *cell = find_cell(&get_handle()->tail, id);

	enq_commit(q, cell, val, id);
}

static inline bool enq_fast(pll_queue q, void *val, uint64_t *cell_id)
{
	uint64_t i = pll_faa(&q->tail, 1);
	struct queue_cell *cell = find_cell(&get_handle()->tail, i);

	if (pll_cas(&cell->val, NULL, val))
		return true;

	*cell_id = i;
	return false;
}

void pll_enqueue(pll_queue q, void *val)
{
	uint64_t cell_id;

	for (int p = PATIENCE; p >= 0; --p)
		if (enq_fast(q, val, &cell_id))
			return;
	enq_slow(q, val, cell_id);
}

void *pll_dequeue(pll_queue q)
{
	return NULL;
}

bool pll_queue_empty(pll_queue q)
{
	return 0;
}
