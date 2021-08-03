#include <stdio.h>
#include <stdlib.h>

#include "../global.h"
#include "../cache.h"
#include "lru_sbsc.h"
#include "../shmlib.h"
#include "hashtable_utils.h"
#include "bandhash.h"


/********
 ** SHM**
 ********/

static StrategyCtrl_LRU_SBSC *self_strategy_ctrl;
static StrategyDesp_LRU_SBSC	*strategy_desp;
static Band_Dscptr *sbsc_band_dscptr;
static Band_Dscptr *sbsc_hash_band = NULL;

static volatile void *addToLRUHead(StrategyDesp_LRU_SBSC * ssd_buf_hdr_for_lru_sbsc);
static volatile void *deleteFromLRU(StrategyDesp_LRU_SBSC * ssd_buf_hdr_for_lru_sbsc);
static volatile void *moveToLRUHead(StrategyDesp_LRU_SBSC * ssd_buf_hdr_for_lru_sbsc);

/*
 * init buffer hash table, Strategy_control, buffer, work_mem
 */
int
initSSDBufferFor_LRU_SBSC()
{
    strategy_desp = (StrategyDesp_LRU_SBSC *)malloc(sizeof(StrategyDesp_LRU_SBSC) * NBLOCK_SSD_CACHE);
    if(strategy_desp == NULL){
        sac_info("no memory");
        return -1;
    }
    StrategyDesp_LRU_SBSC *ssd_buf_hdr_for_lru_sbsc = strategy_desp;
    long i;
    for (i = 0; i < NBLOCK_SSD_CACHE; ssd_buf_hdr_for_lru_sbsc++, i++)
    {
        ssd_buf_hdr_for_lru_sbsc->serial_id = i;
        ssd_buf_hdr_for_lru_sbsc->next_self_lru = -1;
        ssd_buf_hdr_for_lru_sbsc->last_self_lru = -1;
        multi_SHM_mutex_init(&ssd_buf_hdr_for_lru_sbsc->lock);
    }

    sbsc_band_dscptr = (Band_Dscptr *)malloc(sizeof(Band_Dscptr) * NBLOCK_SSD_CACHE);
    if(sbsc_band_dscptr == NULL){
        sac_info("no memory");
        return -1;
    }
    Band_Dscptr *band_info = sbsc_band_dscptr;
    for (i = 0; i < NBLOCK_SSD_CACHE; band_info++, i++)
    {
        band_info->serial_id = i;
        band_info->bandId= -1;
        band_info->off_set.offset= -1;
    }

    self_strategy_ctrl = (StrategyCtrl_LRU_SBSC *)malloc(sizeof(StrategyCtrl_LRU_SBSC));
    if(sbsc_band_dscptr == NULL){
        sac_info("no memory");
        return -1;
    }
    self_strategy_ctrl->first_self_lru = -1;
    self_strategy_ctrl->last_self_lru = -1;

    int ret = HashTab_Init();
    if(ret != 0){
        sac_info("no memory for bandhash");
        exit(EXIT_FAILURE);
    }
    
    return 0;
}

int
Unload_Buf_LRU_SBSC(long * out_despid_array, int max_n_batch)
{
    int cnt = 0;
    BandTag Now_BandID;

    while(cnt < EVICT_DITRY_GRAIN && cnt < max_n_batch)
    {   
        int cur_cnt, i;
        long frozen_id = self_strategy_ctrl->last_self_lru;
        Now_BandID.BandId  = strategy_desp[frozen_id].off_set.offset / ZONESZ;

        // cleaning all block belong to the same band
        cur_cnt = BandHashTab_Delete(Now_BandID, out_despid_array, cnt);
        
        for (i = cnt; i < cnt + cur_cnt; i++) {
            frozen_id = out_despid_array[i];
            deleteFromLRU(&strategy_desp[frozen_id]);
        }
        cnt += cur_cnt;
    }

    return cnt;
}

int
hitInBuffer_LRU_SBSC(long serial_id)
{
    StrategyDesp_LRU_SBSC* ssd_buf_hdr_for_lru_sbsc = &strategy_desp[serial_id];
    moveToLRUHead(ssd_buf_hdr_for_lru_sbsc);
    return 0;
}

int
insertBuffer_LRU_SBSC(long serial_id , SSDBufTag off_set)
{
    
    addToLRUHead(&strategy_desp[serial_id]);
    strategy_desp[serial_id].off_set.offset = off_set.offset;

    // according offset get bandid
    BandTag Now_BandId;
    Band_Dscptr *band_info = sbsc_band_dscptr + serial_id;
    band_info->bandId = off_set.offset / ZONESZ;
    band_info->off_set.offset = off_set.offset;

    // build hashtable according bandid and serial_id
    Now_BandId.BandId = band_info->bandId;
    BandHashTab_Insert(Now_BandId,serial_id);
    return 0;
}

static volatile void *
addToLRUHead(StrategyDesp_LRU_SBSC* ssd_buf_hdr_for_lru_sbsc)
{
    //deal with self LRU queue
    if(self_strategy_ctrl->last_self_lru < 0)
    {
        // list is empty
        self_strategy_ctrl->first_self_lru = ssd_buf_hdr_for_lru_sbsc->serial_id;
        self_strategy_ctrl->last_self_lru = ssd_buf_hdr_for_lru_sbsc->serial_id;
    } else {
        ssd_buf_hdr_for_lru_sbsc->next_self_lru = strategy_desp[self_strategy_ctrl->first_self_lru].serial_id;
        ssd_buf_hdr_for_lru_sbsc->last_self_lru = -1;
        strategy_desp[self_strategy_ctrl->first_self_lru].last_self_lru = ssd_buf_hdr_for_lru_sbsc->serial_id;
        self_strategy_ctrl->first_self_lru =  ssd_buf_hdr_for_lru_sbsc->serial_id;
    }
    return NULL;
}

static volatile void *
deleteFromLRU(StrategyDesp_LRU_SBSC * ssd_buf_hdr_for_lru_sbsc)
{
    //deal with self queue
    if(ssd_buf_hdr_for_lru_sbsc->last_self_lru>=0)
    {
        strategy_desp[ssd_buf_hdr_for_lru_sbsc->last_self_lru].next_self_lru = ssd_buf_hdr_for_lru_sbsc->next_self_lru;
    } else {
        self_strategy_ctrl->first_self_lru = ssd_buf_hdr_for_lru_sbsc->next_self_lru;
    }

    if(ssd_buf_hdr_for_lru_sbsc->next_self_lru>=0)
    {
        strategy_desp[ssd_buf_hdr_for_lru_sbsc->next_self_lru].last_self_lru = ssd_buf_hdr_for_lru_sbsc->last_self_lru;
    }
    else
    {
        self_strategy_ctrl->last_self_lru = ssd_buf_hdr_for_lru_sbsc->last_self_lru;
    }

    ssd_buf_hdr_for_lru_sbsc->last_self_lru = ssd_buf_hdr_for_lru_sbsc->next_self_lru = -1;

    return NULL;
}

static volatile void *
moveToLRUHead(StrategyDesp_LRU_SBSC * ssd_buf_hdr_for_lru_sbsc)
{
    deleteFromLRU(ssd_buf_hdr_for_lru_sbsc);
    addToLRUHead(ssd_buf_hdr_for_lru_sbsc);
    return NULL;
}

