#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdint.h>

#include "atomic.h"
#include "paralull.h"
#include "queue.h"
#include "atomic.h"

#define PATIENCE    42
#define MAX_GARBAGE 16

#define DEQUEUE_TOP     ((void *)-1)
#define DEQUEUE_BOTTOM  ((void *)-2)
#define ENQUEUE_TOP     ((void *)-3)
#define ENQUEUE_BOTTOM  ((void *)-4)
#define QUEUE_TOP       ((void *)-5)
#define QUEUE_BOTTOM    ((void *)-6)
#define QUEUE_EMPTY     ((void *)-7)

static struct queue_segment *new_segment(uint64_t id)
{
	struct queue_segment *seg = malloc(sizeof(struct queue_segment));

	seg->id = id;
	seg->next = NULL;
	for (int i = 0; i < CELLS_NUMBER; ++i) {
		seg->cells[i] = (struct queue_cell) {
			.val = QUEUE_BOTTOM,
			.enq = ENQUEUE_BOTTOM,
			.deq = DEQUEUE_BOTTOM
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
		.enq = { .peer = h },
		.deq = { .peer = h },
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
	if (rc)
		pthread_key_delete(key);
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

static struct queue_handle *get_handle(pll_queue q)
{
	return pthread_getspecific(q->hndlk);
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

static void enq_slow(pll_queue q,
						struct queue_handle *h,
						void *val,
						uint64_t cell_id)
{
	struct queue_segment *tmp_tail = h->tail;
	struct queue_enqreq *req = &h->enq.req;

	req->val = val;
	req->state = (union queue_reqstate) { .s.pending = 1, .s.id = cell_id };

	do {
		uint64_t i = pll_faa(&q->tail, 1);
		struct queue_cell *cell = find_cell(&tmp_tail, i);

		if (pll_cas(&cell->enq, ENQUEUE_BOTTOM, req) && cell->val == QUEUE_BOTTOM) {
			try_to_claim_req(&req->state.u64, cell_id, i);
			break;
		}
	} while (req->state.s.pending);

	uint64_t id = req->state.s.id;
	struct queue_cell *cell = find_cell(&h->tail, id);

	enq_commit(q, cell, val, id);
}

static inline bool enq_fast(pll_queue q,
								struct queue_handle *h,
								void *val,
								uint64_t *cell_id)
{
	uint64_t i = pll_faa(&q->tail, 1);
	struct queue_cell *cell = find_cell(&h->tail, i);

	if (pll_cas(&cell->val, QUEUE_BOTTOM, val))
		return true;

	*cell_id = i;
	return false;
}

void pll_enqueue(pll_queue q, void *val)
{
	uint64_t cell_id;
	struct queue_handle *h = get_handle(q);

	h->hzdp = h->tail;
	for (int p = PATIENCE; p >= 0; --p)
		if (enq_fast(q, h, val, &cell_id))
			return;
	enq_slow(q, h, val, cell_id);
	h->hzdp = NULL;
}

static void verify(struct queue_segment **seg, struct queue_segment *hzdp)
{
	if (hzdp && hzdp->id < (*seg)->id)
		*seg = hzdp;
}

static void update(struct queue_segment **from, struct queue_segment **to,
		struct queue_handle *h)
{
	struct queue_segment *n = *from;
	if (n->id < (*to)->id) {
		if (!pll_cas(from, n, *to)) {
			n = *from;
			if (n->id < (*to)->id)
				*to = n;
		}
		verify(to, h->hzdp);
	}
}

static void cleanup(pll_queue q, struct queue_handle *h)
{
	uint64_t i = q->oldseg;
	struct queue_segment *e = h->head;

	/* if cleaning is in progress, abort */
	if (i == (uint64_t)-1)
		return;
	if (e->id - i < MAX_GARBAGE)
		return;

	/* try to claim cleaning state, abort otherwise */
	if (!pll_cas(&q->oldseg, i, -1))
		return;

	struct queue_segment *s = q->q;

	size_t numhds = 0;
	for (struct queue_handle *p = h->next; p != h && e->id > i; p = p->next)
		++numhds;

	struct queue_handle **hds = malloc (sizeof (*hds) * numhds);
	size_t j = 0;
	for (struct queue_handle *p = h->next; p != h && e->id > i; p = p->next) {
		verify(&e, p->hzdp);
		update(&p->head, &e, p);
		update(&p->tail, &e, p);
		hds[j++] = p;
	}
	while (e->id > i && j > 0)
		verify(&e, hds[--j]->hzdp);
	if (e->id <= i) {
		q->q = s;
		return;
	}
	q->q = e;
	q->oldseg = e->id;

	// TODO: Check for correctness
#if 0
	for (struct queue_segment *seg = s; seg != e;) {
		struct queue_segment *next = seg->next;
		free(seg);
		seg = next;
	}
#endif
}

static void *help_enq(pll_queue q,
						struct queue_handle *h,
						struct queue_cell *cell,
						uint64_t i)
{
	if (!pll_cas(&cell->val, QUEUE_BOTTOM, QUEUE_TOP)
			&& cell->val != QUEUE_TOP)
		return cell->val;

	struct queue_handle *peer = NULL;
	struct queue_enqreq *req = NULL;
	union queue_reqstate state;

	if (cell->enq == ENQUEUE_BOTTOM) {
		do {
			h = get_handle(q);
			peer = h->enq.peer;
			req = &peer->enq.req;
			state = req->state;

			if (h->enq.req.state.s.id == 0
					|| h->enq.req.state.s.id == state.s.id)
				break;

			h->enq.req.state.s.id = 0;
			h->enq.peer = peer->next;
		} while (1);

		if (state.s.pending && state.s.id <= i
				&& !pll_cas(&cell->enq, ENQUEUE_BOTTOM, req))
			h->enq.req.state.s.id = state.s.id;
		else
			h->enq.peer = peer->next;

		if (cell->enq == ENQUEUE_BOTTOM)
			pll_cas(&cell->enq, ENQUEUE_BOTTOM, ENQUEUE_TOP);
	}

	if (cell->enq == ENQUEUE_TOP)
		return (q->tail <= i ? QUEUE_EMPTY : QUEUE_TOP);

	req = cell->enq;
	state = req->state;

	void *val = req->val;
	union queue_reqstate s_val = { .s.pending = 0, .s.id = i };

	if (state.s.id > i) {
		if (cell->val == QUEUE_TOP && q->tail <= i)
			return QUEUE_EMPTY;
	} else if (try_to_claim_req(&req->state.u64, state.s.id, i)
				|| (state.u64 == s_val.u64 && cell->val == QUEUE_TOP)) {
		enq_commit(q, cell, val, i);
	}

	return cell->val;
}

static void *deq_fast(pll_queue q, struct queue_handle *h, uint64_t *cell_id)
{
	uint64_t i = pll_faa(&q->head, 1);
	struct queue_cell *cell = find_cell(&h->head, i);
	void *val = help_enq(q, h, cell, i);

	if (val != QUEUE_TOP && pll_cas(&cell->deq, DEQUEUE_BOTTOM, DEQUEUE_TOP))
		return val;

	*cell_id = i;
	return QUEUE_TOP;
}

static void help_deq(pll_queue q,
						struct queue_handle *h,
						struct queue_handle *h_help)
{
	struct queue_deqreq *req = &h_help->deq.req;
	union queue_reqstate state = req->state;
	uint64_t id = req->id;

	if (!state.s.pending || state.s.id < id)
		return;

	struct queue_segment *head = h_help->head;

	pll_barrier();
	h->hzdp = head;

	state = req->state;

	uint64_t prior = id;
	uint64_t i = id;
	uint64_t cand = 0;

	while (true) {
		struct queue_cell *cell = NULL;

		for (struct queue_segment *c_seg = head;
				!cand && state.s.id == prior;) {
			cell = find_cell(&c_seg, ++i);

			void *val = help_enq(q, h, cell, i);

			if (val == QUEUE_EMPTY
					|| (val != QUEUE_TOP
						&& cell->deq == DEQUEUE_BOTTOM))
				cand = i;
			else
				state = req->state;
		}

		if (cand) {
			union queue_reqstate prior_s = { .s.pending = 1, .s.id = prior };
			union queue_reqstate cand_s = { .s.pending = 1, .s.id = cand };

			pll_cas(&req->state.u64, prior_s.u64, cand_s.u64);
			state = req->state;
		}

		if (!state.s.pending || req->id != id)
			return;

		cell = find_cell(&head, state.s.id);

		if (cell->val == QUEUE_TOP
				|| pll_cas(&cell->deq, DEQUEUE_BOTTOM, req)
				|| cell->deq == req) {
			union queue_reqstate s = { .s.pending = 0, .s.id = id };

			pll_cas(&req->state.u64, state.u64, s.u64);
			return;
		}

		prior = state.s.id;

		if (state.s.id >= i) {
			cand = 0;
			i = state.s.id;
		}
	}
}

static void *deq_slow(pll_queue q, struct queue_handle *h, uint64_t cell_id)
{
	struct queue_deqreq *req = &h->deq.req;

	req->id = cell_id;
	req->state = (union queue_reqstate) { .s.pending = 1, .s.id = cell_id };

	help_deq(q, h, h);

	uint64_t i = req->state.s.id;
	struct queue_cell *cell = find_cell(&h->head, i);
	void *val = cell->val;

	advance_end_for_linearizability(&q->head, i + 1);

	return (val == QUEUE_TOP ? QUEUE_EMPTY : val);
}

void *pll_dequeue(pll_queue q)
{
	struct queue_handle *h = get_handle(q);
	h->hzdp = h->head;

	void *val = NULL;
	uint64_t cell_id;

	for (int p = PATIENCE; p >= 0; p--) {
		val = deq_fast(q, h, &cell_id);
		if (val != QUEUE_TOP)
			break;
	}

	if (val == QUEUE_TOP)
		val = deq_slow(q, h, cell_id);

	if (val != QUEUE_EMPTY) {
		help_deq(q, h, h->deq.peer);
		h->deq.peer = h->deq.peer->next;
	}

	h->hzdp = NULL;
	cleanup(q, h);
	return val;
}

bool pll_queue_empty(pll_queue q)
{
	return q->head == q->tail;
}
