#include <stdio.h>
#include <stdlib.h>
#include "shmlib.h"
#include "cache.h"
#include "bandhash.h"
#include "global.h"
#include "mcheck.h"
#include "lru_sbsc.h"

#define GetBandHashBucket(hash_code) ((BandHashBucket *) (band_hashtable + (unsigned) (hash_code)))
#define isSameTag(tag1,tag2) (tag1.BandId == tag2.BandId)
extern void _LOCK(pthread_mutex_t* lock);
extern void _UNLOCK(pthread_mutex_t* lock);

BandHashBucket* band_hashtable;

static BandHashBucket* hashitem_freelist;
static BandHashBucket* topfree_ptr;
static BandHashBucket* buckect_alloc();

<<<<<<< HEAD
static long insertCnt_total,deleteCnt_times;
=======
static long insertCnt_total,deleteCnt_total;
>>>>>>> test
static long insertCnt,deleteCnt;


static void freebucket(BandHashBucket* bucket);
int BandHashTab_Init()
{
<<<<<<< HEAD
    insertCnt_total = deleteCnt_times = 0;
    band_hashtable = (BandHashBucket*)malloc(sizeof(BandHashBucket)*NTABLE_SSD_CACHE);
    hashitem_freelist = (BandHashBucket*)malloc(sizeof(BandHashBucket)*NTABLE_SSD_CACHE);
=======
    insertCnt_total = deleteCnt_total = 0;
    band_hashtable = (BandHashBucket*)malloc(sizeof(BandHashBucket)*NZONES);
    hashitem_freelist = (BandHashBucket*)malloc(sizeof(BandHashBucket)*NZONES);
>>>>>>> test
    topfree_ptr = hashitem_freelist;

    if(band_hashtable == NULL || hashitem_freelist == NULL)
        return -1;

    BandHashBucket* bucket = band_hashtable;
    BandHashBucket* freebucket = hashitem_freelist;
    int i = 0;
<<<<<<< HEAD
    for(i = 0; i < NTABLE_SSD_CACHE; bucket++, freebucket++, i++)
=======
    for(i = 0; i < NZONES; bucket++, freebucket++, i++)
>>>>>>> test
    {
        bucket->desp_serial_id = freebucket->desp_serial_id = -1;
        bucket->hash_key.BandId = freebucket->hash_key.BandId = -1;
        bucket->next_item = NULL;
        freebucket->next_item = freebucket + 1;
    }
<<<<<<< HEAD
    hashitem_freelist[NTABLE_SSD_CACHE - 1].next_item = NULL;
=======
    hashitem_freelist[NZONES - 1].next_item = NULL;
>>>>>>> test
    return 0;
}


long BandHashTab_Lookup(BandTag band_tag)
{
    if (DEBUG)
        printf("[INFO] Lookup band_tag: %lu\n",band_tag.BandId);
    BandHashBucket *nowbucket = GetBandHashBucket(band_tag.BandId);
    while (nowbucket != NULL)
    {
        if (isSameTag(nowbucket->hash_key, band_tag))
        {
            return nowbucket->desp_serial_id;
        }
        nowbucket = nowbucket->next_item;
    }

    return -1;
}

long BandHashTab_Insert(BandTag band_tag, long desp_serial_id)
{
    if (DEBUG)
        printf("[INFO] Insert buf_tag: %lu\n",band_tag.BandId);

    insertCnt_total++;
    //printf("hashitem alloc times:%d\n",insertCnt_total);

    BandHashBucket *nowbucket = GetBandHashBucket(band_tag.BandId);
    if(nowbucket == NULL)
    {
        printf("[ERROR] Insert HashBucket: Cannot get HashBucket.\n");
        exit(1);
    }
    while (nowbucket->next_item != NULL)
    {
        nowbucket = nowbucket->next_item;
    }

    BandHashBucket* newitem;
    if((newitem  = buckect_alloc()) == NULL)
    {
        printf("hash bucket alloc failure\n");
        exit(-1);
    }
    newitem->hash_key = band_tag;
    newitem->desp_serial_id = desp_serial_id;
    newitem->next_item = NULL;

    nowbucket->next_item = newitem;
    return 0;
}

<<<<<<< HEAD
long BandHashTab_Delete(BandTag band_tag, long* out_despid_array, int cnt)
=======
long BandHashTab_Delete(BandTag band_tag, long * out_despid_array, int cnt)
>>>>>>> test
{
    if (DEBUG)
        printf("[INFO] Delete buf_tag: %lu\n",band_tag.BandId);

    deleteCnt = 0;
<<<<<<< HEAD
    deleteCnt_times++;
    //printf("hashitem free times:%d\n",deleteCnt_times++);
=======
    deleteCnt_total++;
    //printf("hashitem free times:%d\n",deleteCnt_total++);
>>>>>>> test

    long del_id;
    BandHashBucket *delitem;
    BandHashBucket *nowbucket = GetBandHashBucket(band_tag.BandId);
<<<<<<< HEAD
    while (nowbucket->next_item != NULL) {
        if (isSameTag(nowbucket->next_item->hash_key, band_tag) && deleteCnt < EVICT_DITRY_GRAIN) {
=======

    while (nowbucket->next_item != NULL)
    {
        if (isSameTag(nowbucket->next_item->hash_key, band_tag) && deleteCnt <= EVICT_DITRY_GRAIN)
        {
>>>>>>> test
            delitem = nowbucket->next_item;
            del_id = delitem->desp_serial_id;
            nowbucket->next_item = delitem->next_item;
            // stat serial_id
            out_despid_array[cnt++] = del_id;

            freebucket(delitem);
            deleteCnt++;
<<<<<<< HEAD
            // printf("deletecnt = %d, deletecnt_times = %d\n", deleteCnt, deleteCnt_times);
        } else {
            // printf("[ERROR] band = %lu, hash_key = %lu, deletecnt = %lu \n",
                        // band_tag.BandId,nowbucket->next_item->hash_key, deleteCnt);
            break;
        }
    }
    // printf("[INFO] band %lu total delete block %lu\n", band_tag.BandId, deleteCnt);
=======
        }
        nowbucket = nowbucket->next_item;
    }
    
    printf("[INFO] band %lu total delete block %lu\n", band_tag.BandId, deleteCnt);
>>>>>>> test

    return (EVICT_DITRY_GRAIN > deleteCnt ? deleteCnt : EVICT_DITRY_GRAIN);
}

static BandHashBucket* buckect_alloc()
{
    if(topfree_ptr == NULL)
        return NULL;
    BandHashBucket* freebucket = topfree_ptr;
    topfree_ptr = topfree_ptr->next_item;
    return freebucket;
}

static void freebucket(BandHashBucket* bucket)
{
    bucket->next_item = topfree_ptr;
    topfree_ptr = bucket;
}
