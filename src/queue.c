#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

#include <tizzy.h>

#include "atomic.h"
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

static void handle_term(void *hndl)
{
	struct queue_handle *h = hndl;
	struct queue_handle *hnext;

	for (;;) {
retry:
		hnext = h->next;
		/* if the next ptr is marked as iterated, retry */
		if ((intptr_t)hnext & 1)
			continue;

		/* if we are by ourselves, proceed */
		if (hnext == h) {
			/* try to mark the hnext ptr as iterated, or retry */
			if (!pll_cas(&h->next, hnext, (intptr_t)hnext | 1))
				continue;
			break;
		}

		struct queue_handle *prev = hnext;
		struct queue_handle *next = prev->next;
		for (;;) {
			/* if the next ptr is marked as iterated, retry */
			if ((intptr_t)next & 1)
				goto retry;

			/* try to mark the next ptr as iterated, or retry */
			if (!pll_cas(&prev->next, next, (intptr_t)next | 1))
				goto retry;

			if (next == h) {
				/* try to mark the hnext ptr as iterated, or retry */
				if (!pll_cas(&h->next, hnext, (intptr_t)hnext | 1)) {
					/* unmark the next ptr */
					if (!pll_cas(&prev->next, (intptr_t)next | 1, next))
						tzy_panic("Unexpected out-of-order operation");
					goto retry;
				}
				/* We found our node in the ring, process it */
				break;
			}

			struct queue_handle *newprev = next;
			struct queue_handle *newnext = next->next;
			/* unmark the next ptr */
			if (!pll_cas(&prev->next, next, (intptr_t)next & (intptr_t)~1))
				tzy_panic("Unexpected out-of-order operation");
			prev = newprev;
			next = newnext;
		}
		/* Remove the node from the ring */
		if (!pll_cas(&prev->next, (intptr_t)next | 1, hnext))
			tzy_panic("Unexpected out-of-order operation");
		break;
	}

	if (!pll_cas(&h->next, (intptr_t)hnext | 1, NULL))
		tzy_panic("Unexpected out-of-order operation");
	free(h);
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
			if ((intptr_t) next & 1)
				continue;

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
	if ((rc = pthread_key_create(&key, handle_term)))
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

