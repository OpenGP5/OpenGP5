#ifndef GP5_CACHE_H
#define GP5_CACHE_H

void gp5_cache_on(void);    /* memory regions + both caches on (start-up) */
void gp5_cache_off(void);   /* both caches off and emptied (hand-over to the factory loader) */

#endif
