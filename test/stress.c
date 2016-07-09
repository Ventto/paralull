#include <criterion/criterion.h>
#include <paralull.h>

#define NB_THREADS 10
#define NB_ITEMS 1000

static char marks[NB_ITEMS];

static void *worker_enq(void *ctx)
{
    pll_queue queue = ctx;

    for (size_t i = 0; i < NB_ITEMS; ++i) {
        pll_enqueue(queue, (void *) i);
        __sync_fetch_and_add(&marks[i], 1);
    }
}

static void *worker_deq(void *ctx)
{
    pll_queue queue = ctx;

    for (size_t i = 0; i < NB_ITEMS; ++i) {
        size_t val = (size_t) pll_dequeue(queue);
        __sync_fetch_and_sub(&marks[val], 1);
    }
}

Test(queue, stress, .timeout = 3)
{
    pll_queue queue = pll_queue_init();

    int rc;
    pthread_t threads[NB_THREADS];

    size_t i = 0;
    for (; i < NB_THREADS / 2; ++i)
        rc |= pthread_create(&threads[i], NULL, worker_end, NULL, queue);
    for (; i < NB_THREADS; ++i)
        rc |= pthread_create(&threads[i], NULL, worker_deq, NULL, queue);

    cr_assert(!rc, "Could not create worker threads");

    for (size_t i = 0; i < NB_THREADS; ++i)
        rc |= pthread_join(threads[i], NULL);

    cr_assert(!rc, "Could not join all worker threads");

    int empty = 1;
    for (size_t i = 0; i < NB_ITEMS; ++i)
        empty &= marks[i] == 0;

    cr_assert(empty, "Result set is non-empty");
    cr_assert(pll_queue_empty(queue), "Resulting queue is not empty");

    for (size_t i = 0; i < NB_THREADS; ++i)
        pthread_destroy(threads[i], NULL, worker, NULL, (void*)i);

    pll_queue_term(queue);
    return 0;
}
