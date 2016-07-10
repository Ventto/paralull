#ifndef ATOMIC_H_
# define ATOMIC_H_

# define pll_cas(Ptr, Val, Newval) (__sync_bool_compare_and_swap((Ptr), (Val), (Newval)))
# define pll_faa(Ptr, Val) (__sync_fetch_and_add((Ptr), (Val)))
# define pll_fas(Ptr, Val) (__sync_fetch_and_sub((Ptr), (Val)))
# define pll_barrier() (__sync_synchronize())

# define pll_aset(Var, Val) __extension__ ({        \
		__typeof__(Var) tmp;                        \
		do {                                        \
			tmp = (Var);                            \
		} while (!pll_cas(&(Var), tmp, (Val)));     \
		tmp;                                        \
	})

#endif /* !ATOMIC_H_ */
