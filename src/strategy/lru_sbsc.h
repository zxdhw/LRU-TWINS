#ifndef _LRU_SBSC_H_
#define _LRU_SBSC_H_
#define DEBUG 0
/* ---------------------------lru_sbsc---------------------------- */
#include <sys/types.h>

#define EVICT_DITRY_GRAIN 64

typedef struct
{
	long 		serial_id;	// the corresponding descriptor serial number.
    long        next_self_lru;
    long        last_self_lru;
    int         user_id;
    SSDBufTag       off_set;
    pthread_mutex_t lock;
    long     	    stamp;
} StrategyDesp_LRU_SBSC;

typedef struct
{
    long        first_self_lru;          // Head of list of LRU
    long        last_self_lru;           // Tail of list of LRU
    pthread_mutex_t lock;
    blkcnt_t    count;
} StrategyCtrl_LRU_SBSC;

typedef struct
{   
    int             bandId;
    long            serial_id;
    SSDBufTag       off_set;
} Band_Dscptr;

extern int initSSDBufferFor_LRU_SBSC();
extern int Unload_Buf_LRU_SBSC(long * out_despid_array, int max_n_batch);
extern int hitInBuffer_LRU_SBSC(long serial_id);
extern int insertBuffer_LRU_SBSC(long serial_id, SSDBufTag off_set);
#endif // _LRU_PRIVATE_H_
