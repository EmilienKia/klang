/* References a symbol defined nowhere: dlopen(RTLD_NOW) must fail. */
extern int modundef_nowhere_xyz(void);
int modundef_fn(void) { return modundef_nowhere_xyz(); }
