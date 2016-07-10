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

static int handle_init(struct pll_queue *q, struct queue_handle **out)
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
	if (out)
		*out = h;
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

	if (handle_init(queue, NULL) < 0)
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
	struct queue_handle *h = pthread_getspecific(q->hndlk);
	if (!h) {
		if (handle_init(q, &h) < 0)
			abort();
	}
	return h;
}

static void advance_end_for_linearizability(uint64_t *E, uint64_t cell_id)
{
	uint64_t e = *E;
	do e = *E; while (e < cell_id && !pll_cas(E, e, cell_id));
}

static void enq_commit(pll_queue q, struct queue_cell *cell, void *val,
                       uint64_t cell_id)
{
	advance_end_for_linearizability(&q->tail, cell_id + 1);
	cell->val = val;
}

static void *find_cell(struct queue_segment **sp, uint64_t cell_id)
{
	/* Invariant: sp points to a valid segment*/
	struct queue_segment *seg = *sp;
	struct queue_segment *next;

	/* Traverse list to target segment with id and cell_id/CELL_NUMBER */
	for (uint64_t i = seg->id; i < cell_id / CELLS_NUMBER; ++i) {
		next = seg->next;
		if (next == NULL) {
			/*
			 * The list needs another segment. Allocate one and try to extend
			 * the list.
			 */
			struct queue_segment *tmp = new_segment(i + 1);

			if (!pll_cas(&seg->next, NULL, tmp))
				free(tmp);
			/* Invariant: a successor segment exists. */
			next = seg->next;
		}
		seg = next;
	}
	/* Invariant: seg is the target segment (cell_id/CELL_NUMBER) */
	*sp = seg;
	/* Return the target segment */
	return &seg->cells[cell_id % CELLS_NUMBER];
}

static bool try_to_claim_req(uint64_t *state, uint64_t id, uint64_t cell_id)
{
	union queue_reqstate s_val1 = { .s.pending = 1, .s.id = id };
	union queue_reqstate s_val2 = { .s.pending = 0, .s.id = cell_id };

	return pll_cas(state, s_val1.u64, s_val2.u64);
}

static void enq_slow(pll_queue q, struct queue_handle *h, void *val,
                     uint64_t cell_id)
{
	/* Publish enqueue request */
	struct queue_enqreq *req = &h->enq.req;
	/*
	 * Use a local tail pointer to traverse because later we may need to find
	 * an earlier cell.
	 */
	struct queue_segment *tmp_tail = h->tail;

	req->val = val;
	req->state = (union queue_reqstate) { .s.pending = 1, .s.id = cell_id };

	do {
		/* Obtain a new cell index and locate candidate cell */
		uint64_t i = pll_faa(&q->tail, 1);
		struct queue_cell *cell = find_cell(&tmp_tail, i);

		/* Dijkstra's protocol */
		if (pll_cas(&cell->enq, ENQUEUE_BOTTOM, req)
				&& cell->val == QUEUE_BOTTOM) {
			try_to_claim_req(&req->state.u64, cell_id, i);
			/* Invariant: request claimed (even if CAS failed) */
			break;
		}
	} while (req->state.s.pending);
	/* Invariant: req claimed for a cell and find that cell */
	uint64_t id = req->state.s.id;
	struct queue_cell *cell = find_cell(&h->tail, id);

	enq_commit(q, cell, val, id);
}

static inline bool enq_fast(pll_queue q, struct queue_handle *h, void *val,
		                    uint64_t *cell_id)
{
	/* Obtain cell index and locate candidate cell */
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
	/* Use id from last attempt */
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

static void *help_enq(pll_queue q, struct queue_handle *h,
                      struct queue_cell *cell,
                      uint64_t i)
{
	if (!pll_cas(&cell->val, QUEUE_BOTTOM, QUEUE_TOP)
			&& cell->val != QUEUE_TOP)
		return cell->val;

	/* cell->val is QUEUE_TOP, so help slow-path enqueues */
	struct queue_handle *peer = NULL;
	struct queue_enqreq *req = NULL;
	union queue_reqstate state;

	if (cell->enq == ENQUEUE_BOTTOM) {
		do {
			/* Two iterations at most */
			h = get_handle(q);
			peer = h->enq.peer;
			req = &peer->enq.req;
			state = req->state;

			/* Break if I haven't helped this peer complete */
			if (h->enq.req.state.s.id == 0
					|| h->enq.req.state.s.id == state.s.id)
				break;

			/* Peer request completed, move to next peer */
			h->enq.req.state.s.id = 0;
			h->enq.peer = peer->next;
		} while (1);

		/*
		 * If peer enqueue is pending and can use this cell,
		 * try to reserve cell by noting request in cell.
		 */
		if (state.s.pending && state.s.id <= i
				&& !pll_cas(&cell->enq, ENQUEUE_BOTTOM, req))
			/* Failed to reserve cell for req, remember req id */
			h->enq.req.state.s.id = state.s.id;
		else
			/* Peer doesn't need help, I can't help, or I helped */
			h->enq.peer = peer->next;

		/*
		 * If can't find a pending request, write ENQUEUE_TOP to prevent other
		 * enq helpers from using 'cell'
		 */
		if (cell->enq == ENQUEUE_BOTTOM)
			pll_cas(&cell->enq, ENQUEUE_BOTTOM, ENQUEUE_TOP);
	}

	/* Invariant: cell's enq is either a request or ENQUEUE_TOP */
	if (cell->enq == ENQUEUE_TOP)
		/* QUEUE_EMPTY if not enough enqueues linearized before i */
		return (q->tail <= i ? QUEUE_EMPTY : QUEUE_TOP);

	/* Invariant: cell's enq is a request */
	req = cell->enq;
	state = req->state;

	void *val = req->val;
	union queue_reqstate s_val = { .s.pending = 0, .s.id = i };

	if (state.s.id > i) {
		/*
		 * Request is unsuitable for this cell,
		 * QUEUE_EMPTY if not enough enqueues linearized before i
		 */
		if (cell->val == QUEUE_TOP && q->tail <= i)
			return QUEUE_EMPTY;
	} else if (try_to_claim_req(&req->state.u64, state.s.id, i)
				|| (state.u64 == s_val.u64 && cell->val == QUEUE_TOP)) {
		/* Someone claimed this request; not committed */
		enq_commit(q, cell, val, i);
	}

	/* cell->val is QUEUE_TOP or a value */
	return cell->val;
}

static void *deq_fast(pll_queue q, struct queue_handle *h, uint64_t *cell_id)
{
	/* Obtain cell index and locate candidate cell */
	uint64_t i = pll_faa(&q->head, 1);
	struct queue_cell *cell = find_cell(&h->head, i);
	void *val = help_enq(q, h, cell, i);

	if (val == QUEUE_EMPTY)
		return QUEUE_EMPTY;

	/* The cell has a value and I claimed it */
	if (val != QUEUE_TOP && pll_cas(&cell->deq, DEQUEUE_BOTTOM, DEQUEUE_TOP))
		return val;

	/* Otherwise fail, returning cell id */
	*cell_id = i;
	return QUEUE_TOP;
}

static void help_deq(pll_queue q, struct queue_handle *h,
                     struct queue_handle *h_help)
{
	/* Inspect a dequeue request */
	struct queue_deqreq *req = &h_help->deq.req;
	union queue_reqstate state = req->state;
	uint64_t id = req->id;

	/* If this request doesn't need help, return */
	if (!state.s.pending || state.s.id < id)
		return;

	/* head: a local segment pointer for announced cells */
	struct queue_segment *head = h_help->head;

	pll_barrier();
	h->hzdp = head;

	/* Must read after reading h_help->head */
	state = req->state;

	uint64_t prior = id;
	uint64_t i = id;
	uint64_t cand = 0;

	while (true) {
		struct queue_cell *cell = NULL;

		/*
		 * Find a candidate cell, if I don't have one
		 * loop breaks when either find a candidate
		 * or a candidate is announced.
		 * c_seg: a local segment pointer for candidate cells
		 */
		for (struct queue_segment *c_seg = head;
				!cand && state.s.id == prior;) {
			cell = find_cell(&c_seg, ++i);

			void *val = help_enq(q, h, cell, i);

			/*
			 * It is a candidate if it help_enq return QUEUE_EMPTY,
			 * or a value that is not claimed by dequeues.
			 */
			if (val == QUEUE_EMPTY
					|| (val != QUEUE_TOP
						&& cell->deq == DEQUEUE_BOTTOM))
				cand = i;
			/* Inspect request state again */
			else
				state = req->state;
		}

		if (cand) {
			union queue_reqstate prior_s = { .s.pending = 1, .s.id = prior };
			union queue_reqstate cand_s = { .s.pending = 1, .s.id = cand };
			/* Found a candidate cell, try to announce it */
			pll_cas(&req->state.u64, prior_s.u64, cand_s.u64);
			state = req->state;
		}

		/*
		 * Invariant: some candidate announced in state.s.id
		 * quit if request is complete
		 */
		if (!state.s.pending || req->id != id)
			return;

		/* Find the announced candidate */
		cell = find_cell(&head, state.s.id);

		/*
		 * If candidate permits returning QUEUE_EMPTY (cell->val == QUEUE_TOP)
		 * or this helper claimed the value for req with CAS
		 * or another helper claimed the value for req.
		 */
		if (cell->val == QUEUE_TOP
				|| pll_cas(&cell->deq, DEQUEUE_BOTTOM, req)
				|| cell->deq == req) {
			union queue_reqstate s = { .s.pending = 0, .s.id = id };
			/* Request is complete, try to clear pending bit */
			pll_cas(&req->state.u64, state.u64, s.u64);
			/* Invariant: req is complete; req->state.s.pending = 0 */
			return;
		}
		/* Prepare for next iteration */
		prior = state.s.id;
		/*
		 * If annouced candidate is newer than visiited cell
		 * abandon "cand" (if any); bump i
		 */
		if (state.s.id >= i) {
			cand = 0;
			i = state.s.id;
		}
	}
}

static void *deq_slow(pll_queue q, struct queue_handle *h, uint64_t cell_id)
{
	struct queue_deqreq *req = &h->deq.req;

	/* Publish dequeue request */
	req->id = cell_id;
	req->state = (union queue_reqstate) { .s.pending = 1, .s.id = cell_id };

	help_deq(q, h, h);

	/* Find the destination cell & read its value */
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
