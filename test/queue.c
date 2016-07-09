#include <criterion/criterion.h>
#include <paralull.h>

Test(queue, lifecycle)
{
    pll_queue queue = pll_queue_init();
    cr_assert_not_null(queue, "Could not create queue");
    pll_queue_term(queue);
}

Test(queue, axiom_empty)
{
    pll_queue queue = pll_queue_init();
    cr_assert(pll_queue_empty(queue), "New queue is not empty");
    pll_enqueue(queue, (void *) 1);
    cr_assert(!pll_queue_empty(queue), "1-element queue is empty");
    pll_dequeue(queue);
    cr_assert(pll_queue_empty(queue), "0-element queue is not empty");
    pll_queue_term(queue);
}

Test(queue, axiom_ordering)
{
    pll_queue queue = pll_queue_init();

    void *items[] = {(void *) 1, (void *) 2, (void *) 3};
    for (size_t i = 0; i < sizeof (items) / sizeof (void *); ++i)
        pll_enqueue(queue, items[i]);

    void *dequeued[sizeof (items) / sizeof (void *)];
    for (size_t i = 0; i < sizeof (dequeued) / sizeof (void *); ++i)
        pll_enqueue(queue, dequeued[i]);

    cr_assert_arr_eq(items, dequeued, sizeof (items), "Queue does not respect ordering");

    pll_queue_term(queue);
}
