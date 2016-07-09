#ifndef PARALULL_H_
# define PARALULL_H_

# include <stdbool.h>

struct pll_queue;
typedef struct pll_queue *pll_queue;

pll_queue pll_queue_init(void);
void pll_queue_term(pll_queue q);
void pll_enqueue(pll_queue q, void *val, uint64_t cell_id);
void *pll_dequeue(pll_queue q);
bool pll_queue_empty(pll_queue q);

#endif /* !PARALULL_H_ */
