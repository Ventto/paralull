#include <criterion/criterion.h>
#include <paralull.h>
#include <pthread.h>

#define NB_THREADS 100
#define NB_ITEMS 100000

static size_t counter;
static char marks[NB_ITEMS];

static void *worker_enq(void *ctx)
{
    pll_queue queue = ctx;

    for (size_t i = 0; i < NB_ITEMS; ++i) {
        pll_enqueue(queue, (void *) i);
        __sync_fetch_and_add(&marks[i], 1);
        __sync_fetch_and_add(&counter, 1);
    }
    return NULL;
}

static void *worker_deq(void *ctx)
{
    pll_queue queue = ctx;

    for (size_t i = 0; i < NB_ITEMS; ++i) {
        for (;;) {
            size_t c = counter;
            if (c == 0)
                continue;
            if (!__sync_bool_compare_and_swap(&counter, c, c - 1))
                continue;

            size_t val = (size_t) pll_dequeue(queue);
            __sync_fetch_and_sub(&marks[val], 1);
            break;
        }
    }
    return NULL;
}

Test(queue, stress, .timeout = 3)
{
    pll_queue queue = pll_queue_init();

    int rc = 0;
    pthread_t threads[NB_THREADS];

    size_t i = 0;
    for (; i < NB_THREADS / 2; ++i)
        rc |= pthread_create(&threads[i], NULL, worker_enq, queue);
    for (; i < NB_THREADS; ++i)
        rc |= pthread_create(&threads[i], NULL, worker_deq, queue);

    cr_assert(!rc, "Could not create worker threads");

    for (size_t i = 0; i < NB_THREADS; ++i)
        rc |= pthread_join(threads[i], NULL);

    cr_assert(!rc, "Could not join all worker threads");

    int empty = 1;
    for (size_t i = 0; i < NB_ITEMS; ++i)
        empty &= marks[i] == 0;

    cr_assert(empty, "Result set is non-empty");
    cr_assert(pll_queue_empty(queue), "Resulting queue is not empty");

    pll_queue_term(queue);
}
