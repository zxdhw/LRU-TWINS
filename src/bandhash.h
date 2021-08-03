#ifndef _BANDHASH_H
#define _BANDHASH_H 1

#include "cache.h"

typedef struct
{
    int	 BandId;
} BandTag;

typedef struct BandHashBucket
{
    BandTag 			    hash_key;
    long    				desp_serial_id;
    struct BandHashBucket 	*next_item;
} BandHashBucket;

extern BandHashBucket* band_hashtable;
extern int BandHashTab_Init();
extern unsigned long BandHashTab_GetHashCode(BandTag band_tag);
extern long BandHashTab_Lookup(BandTag band_tag);
extern long BandHashTab_Insert(BandTag band_tag, long desp_serial_id);
extern long BandHashTab_Delete(BandTag band_tag, long * out_despid_array, int cnt);
#endif   /* SSDBUFTABLE_H */
