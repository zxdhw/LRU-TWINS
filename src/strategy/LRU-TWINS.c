#include <stdio.h>
#include <stdlib.h>

#include "../global.h"
#include "../cache.h"
#include "lru.h"
#include "lru_twins.h"
#include "../shmlib.h"

// 块数量信息
long blknum_CB  = 0;
long blknum_SB  = 0;
long blknum_ES  = 0;
long max_blknum_CB = 7000000;
long max_blknum_SB = 300000;
long max_blknum_ES = 700000;

// band定位
int evict_band = 0;

// 暂未使用阈值
// long threshold_CB = 7128000; 
// long threshold_SB = 18000;
// long threshold_ES = 54000;

// LRU三个链表信息
static StrategyCtrl_LRU_private *self_strategy_ctrl;
static StrategyCtrl_LRU_private *self_strategy_ctrl_SB;
static StrategyCtrl_LRU_private *self_strategy_ctrl_ES;
// 块描述符信息
static StrategyDesp_LRU_private	*strategy_desp;
//band信息
static band_ctrl *BandCtrlArray;
static unsigned long *BandSortArray;

static volatile void *add_to_CB_head(StrategyDesp_LRU_private * ssd_buf_hdr_for_lru, long offset);
static volatile void *delete_from_LRU(StrategyDesp_LRU_private * ssd_buf_hdr_for_lru);
static volatile long delete_from_ES(long * out_despid_array, int max_n_batch);
static volatile int from_CB_to_SB(int max_n_batch);
static volatile int from_SB_to_ES();
static unsigned long get_band_num(size_t offset);
static long extractNonEmptyBandId();
static void qsort_band(long start, long end);
static void add2band(StrategyDesp_LRU_private *desp, band_ctrl *bandCtrl);

/*
 * init buffer hash table, Strategy_control, buffer, work_mem
 */
int
Init_SSDBuf_For_LRU_private()
{
 
    //初始化栈空间
    strategy_desp = (StrategyDesp_LRU_private *)multi_SHM_alloc(SHM_SSDBUF_STRATEGY_DESP, sizeof(StrategyDesp_LRU_private) * NBLOCK_SSD_CACHE);

    StrategyDesp_LRU_private *ssd_buf_hdr_for_lru = strategy_desp;
    long i;
    for (i = 0; i <  NBLOCK_SSD_CACHE; ssd_buf_hdr_for_lru++, i++)
    {
        ssd_buf_hdr_for_lru->serial_id = i;
        ssd_buf_hdr_for_lru->user_id = 0;
        ssd_buf_hdr_for_lru->bandID = 0;
        // LRU 链表
        ssd_buf_hdr_for_lru->next_self_lru = -1;
        ssd_buf_hdr_for_lru->pre_self_lru = -1;
        // band 链表
        ssd_buf_hdr_for_lru->next_self_band = -1;
        ssd_buf_hdr_for_lru->pre_self_band = -1;       
    }  

    //初始化三个链表（CB、SB、ES）
    self_strategy_ctrl = (StrategyCtrl_LRU_private *)malloc(sizeof(StrategyCtrl_LRU_private));
    self_strategy_ctrl->first_self_lru = -1;
    self_strategy_ctrl->last_self_lru = -1;

    self_strategy_ctrl_SB = (StrategyCtrl_LRU_private *)malloc(sizeof(StrategyCtrl_LRU_private));
    self_strategy_ctrl_SB->first_self_lru = -1;
    self_strategy_ctrl_SB->last_self_lru = -1;

    self_strategy_ctrl_ES = (StrategyCtrl_LRU_private *)malloc(sizeof(StrategyCtrl_LRU_private));
    self_strategy_ctrl_ES->first_self_lru = -1;
    self_strategy_ctrl_ES->last_self_lru = -1;

    // 初始化band信息
    BandCtrlArray = (band_ctrl *)malloc(sizeof(band_ctrl) * NZONES);
    BandSortArray = (unsigned long *)malloc(sizeof(unsigned long) * NZONES);    

    for( i = 0; i < NZONES; i++)
    {
        band_ctrl *ctrl = BandCtrlArray + i;
        ctrl->bandId = i;
        ctrl->heat = ctrl->pagecnt = 0;
        ctrl->head = ctrl->tail = -1;
        ctrl->score = -1;
        ctrl->activate_after_n_cycles = 1;
        BandSortArray[i] = 0;
    }

    return 0;
}


//插入前对三个组件状态进行检查
int
Unload_Buf_LRU_private(long * out_despid_array,int max_n_batch)
{
    //SB已满将其排序移至ES; *0.9保证不会出现SB未满但是CB淘汰的块SB装不下的情况。
    if (blknum_SB >= max_blknum_SB * 0.9)
    {
        // 需要更改user_id
        from_SB_to_ES();
    }
    
    //将块从CB中淘汰加入SB中。
    if (blknum_CB == max_blknum_CB)
    {
        //将块从ES中淘汰，需要添加ES块计数
        if (blknum_ES > max_blknum_ES * 0.5)  // 取值根据ES与SB大小设定。
        {
            evict_ES = delete_from_ES(out_despid_array, max_n_batch);
        }
        //需要更改user_id
        from_CB_to_SB(int max_n_batch);
    }

    return 0;
}


//将块插入到CB头部
int
Insert_Buf_LRU_private(long serial_id,long * out_despid_array,int max_n_batch,long offset)
{
    StrategyDesp_LRU_private* ssd_buf_hdr_for_lru = &strategy_desp[serial_id];
    //插入前先检查各个块的状态
    Unload_Buf_LRU_private(out_despid_array, max_n_batch)；
    //将该块插入到CB链表
    add_to_CB_head(ssd_buf_hdr_for_lru, offset);

    return 0;
}


// 块命中
int
HitIn_Buf_LRU_private(long serial_id,long * out_despid_array,int max_n_batch)
{
    StrategyDesp_LRU_private* ssd_buf_hdr_for_lru = &strategy_desp[serial_id];
    // 将该块从链表中清除
    delete_from_LRU(ssd_buf_hdr_for_lru);
    //将该块插入到CB链表中
    Insert_Buf_LRU_private(serial_id, out_despid_array, max_n_batch);
    return 0;
}


/******************
**** Utilities*****
*******************/

// 获得band号
static unsigned long
get_band_num(size_t offset)
{
    return offset / ZONESZ;
}


// 按照每个band包含的块数量，对band进行排序
static void
qsort_band(long start, long end)
{
    long i = start;
    long j = end;

    long S = BandSortArray[start];
    band_ctrl *curCtrl = BandCtrlArray + S;
    unsigned long Weight = curCtrl->pagecnt;
    while (i < j)
    {
        while (!(BandCtrlArray[BandSortArray[j]].pagecnt > Weight) && i < j)
        {
            j--;
        }
        BandSortArray[i] = BandSortArray[j];

        while (!(BandCtrlArray[BandSortArray[i]].pagecnt < Weight) && i < j)
        {
            i++;
        }
        BandSortArray[j] = BandSortArray[i];
    }

    BandSortArray[i] = S;
    if (i - 1 > start)
        qsort_band(start, i - 1);
    if (j + 1 < end)
        qsort_band(j + 1, end);
}


//确定非空band的个数
static long
extractNonEmptyBandId()
{
    int BandId = 0, cnt = 0;
    while (BandId < NZONES)
    {
        band_ctrl *band = BandCtrlArray + BandId;
        if (band->pagecnt > 0 )
        {
            if(band->activate_after_n_cycles = 1){
                BandSortArray[cnt] = BandId;
                cnt++;
            }else
            {
                band->activate_after_n_cycles--;
            }   
        }
        BandId++;
    }
    return cnt;
}

// 移动到相应的band链表
static void
add2band(StrategyDesp_LRU_private *ssd_buf_hdr_for_lru, band_ctrl *bandCtrl)
{
    if (bandCtrl->head < 0)
    {
        //empty
        bandCtrl->head = bandCtrl->tail = ssd_buf_hdr_for_lru->serial_id;
    }
    else
    {
        //unempty
        StrategyDesp_LRU_private *headDesp =  &strategy_desp[bandCtrl->head];
        ssd_buf_hdr_for_lru->pre_self_band = -1;
        ssd_buf_hdr_for_lru->next_self_band = bandCtrl->head;
        headDesp->pre_self_band = ssd_buf_hdr_for_lru->serial_id;
        bandCtrl->head = ssd_buf_hdr_for_lru->serial_id;
    }
}


// 从相应的band链表删除
static void
delete_from_band(StrategyDesp_LRU_private *ssd_buf_hdr_for_lru, band_ctrl *bandCtrl)
{
    if (bandCtrl->pagecnt == 0)
    {
        //empty
        bandCtrl->head = bandCtrl->tail = -1;
    }
    else
    {
        //unempty
        
    }
}


//将块从SB向ES转移
static volatile int
from_SB_to_ES()
{
    // 计算所有SB中的块的band号
    long now_evcit_id = self_strategy_ctrl_SB->first_self_lru;
    long band_num = 0;
    for (now_evcit_id != -1){

        StrategyDesp_LRU_private * now_evict_SB = &strategy_desp[now_evict_id];
        band_num = get_band_num(now_evict_SB->ssd_buf_tag);
        // 记录band内块数量
        band_ctrl *myband = BandCtrlArray + band_num;
        myband->pagecnt ++ ;
        add2band(now_evict_SB, myband);
        //修改SB块的desp信息
        now_evict_SB->bandID = band_num;
        now_evict_SB->user_id = 3;

        now_evict_SB = &strategy_desp[now_evict_SB->next_self_lru]；
    }

    // 对band进行排序, 并重置band定位
    long nonEmptyBandCnt = extractNonEmptyBandId();
    qsort_band(0, nonEmptyBandCnt - 1);
    evict_band = 0;

    //插入到ES中,修改头尾指针即可
    if(self_strategy_ctrl_ES->last_self_lru < 0)
    {

        //若链表为空，修改ES链表
        self_strategy_ctrl_ES->first_self_lru = self_strategy_ctrl_SB->first_self_lru;
        self_strategy_ctrl_ES->last_self_lru = self_strategy_ctrl_SB->last_self_lru;
    }
    else
    {
        //若链表不为空，修改SB链表
        strategy_desp[self_strategy_ctrl_SB->last_self_lru].next_self_lru = self_strategy_ctrl_ES->first_self_lru;
        strategy_desp[self_strategy_ctrl_ES->first_self_lru].pre_self_lru = self_strategy_ctrl_SB->last_self_lru;
        self_strategy_ctrl_ES->first_self_lru = self_strategy_ctrl_SB->first_self_lru;
    }
   
    //修改SB链表
    self_strategy_ctrl_SB->first_self_lru = -1;
    self_strategy_ctrl_SB->last_self_lru = -1;

    // 块数信息统计
    evict_SB = blknum_SB;
    blknum_ES = blknum_ES + evict_SB;
    blknum_SB = 0;

    return evict_SB;
}


// 将块从ES中删除
static volatile long
delete_from_ES(long * out_despid_array, int max_n_batch)
{

    // 确定淘汰哪些band
    int evict_block = 0;
    band_ctrl * now_band = BandCtrlArray + BandSortArray[evict_band];
    // 选择合适的band进行块淘汰
    for (evict_band = 1; evict_block + now_band->pagecnt < EVICT_GRAIN_ES && evict_block + now_band->pagecnt < max_n_batch; evict_band++){
        StrategyDesp_LRU_private* ssd_buf_hdr_for_lru = &strategy_desp[now_band->head];
        evict_block = evict_block + now_band->pagecnt;
        // 将块从ES链表中删除 
        int cnt = 0;
        while(cnt <now_band->pagecnt)  // 并不是简单的删除EVICT_GRAIN_ES 个块，需要根据其序列号选择合适的大小
        {
            long frozen_id = ssd_buf_hdr_for_lru->serial_id;
            delete_from_LRU(&strategy_desp[frozen_id]);
            // 查找下一个快位置
            ssd_buf_hdr_for_lru = &strategy_desp[ssd_buf_hdr_for_lru->next_self_band];
            // 记录删除块的位置  
            out_despid_array[cnt] = frozen_id;
            cnt ++ ;
        }
        // 块信息清除
        BandSortArray[evict_band-1] = 0;
        now_band->pagecnt = 0;
        now_band->head = -1;
        now_band->tail = -1;
        // 定位下一个band
        now_band = BandCtrlArray + BandSortArray[evict_band];
    }

    // 块数信息统计
    evict_ES = evict_block - now_band->pagecnt;
    evict_band--;
    blknum_ES = blknum_ES - evict_ES;

    return evict_ES;
}


// 将块从CB中向SB中转移
static volatile int
from_CB_to_SB(int max_n_batch)
{
  
    // 首先找到最末尾的块
    int cnt = 0;
    long frozen_id = self_strategy_ctrl->last_self_lru;
    StrategyDesp_LRU_private* ssd_buf_hdr_for_lru = &strategy_desp[frozen_id];
    //修改location 
    ssd_buf_hdr_for_lru->user_id = 2;

    //然后找到倒数EVICT_GRAIN_CB块的位置
    while(cnt < EVICT_GRAIN_CB && cnt < max_n_batch)
    {
        // 找到上一个块
        frozen_id = ssd_buf_hdr_for_lru->pre_self_lru;
        ssd_buf_hdr_for_lru = &strategy_desp[frozen_id];
        
        //修改location 
        ssd_buf_hdr_for_lru->user_id = 2;
        cnt ++ ;
    }

    // 修改链表
    if(self_strategy_ctrl_SB->last_self_lru < 0)
    {

        //若链表为空，修改SB链表
        self_strategy_ctrl_SB->first_self_lru = ssd_buf_hdr_for_lru->serial_id;
        self_strategy_ctrl_SB->last_self_lru = self_strategy_ctrl->last_self_lru;

        //修改CB链表
        self_strategy_ctrl->last_self_lru = strategy_desp[ssd_buf_hdr_for_lru->serial_id].pre_self_lru;
        strategy_desp[self_strategy_ctrl->last_self_lru].next_self_lru = -1;
        
        // 最后将新表头上一链表置为-1；
        strategy_desp[ssd_buf_hdr_for_lru->serial_id].pre_self_lru = -1;
    }
    else
    {
        //若链表不为空，修改SB链表
        strategy_desp[self_strategy_ctrl->last_self_lru].next_self_lru = self_strategy_ctrl_SB->first_self_lru;
        strategy_desp[self_strategy_ctrl_SB->first_self_lru].pre_self_lru = self_strategy_ctrl->last_self_lru;
        self_strategy_ctrl_SB->first_self_lru = ssd_buf_hdr_for_lru->serial_id;

        // 修改CB链表
        self_strategy_ctrl->last_self_lru = strategy_desp[ssd_buf_hdr_for_lru->serial_id].pre_self_lru;
        strategy_desp[self_strategy_ctrl->last_self_lru].next_self_lru = -1;

        // 最后将新表头上一链表置为-1；
        strategy_desp[ssd_buf_hdr_for_lru->serial_id].pre_self_lru = -1;
    }

    //块数统计
    evict_CB = cnt;
    blknum_CB = blknum_CB - evict_CB;
    blknum_SB = blknum_SB + evict_CB; 

    return 0;
}


//未命中，将新块添加进CB顶部
static volatile void *
add_to_CB_head(StrategyDesp_LRU_private* ssd_buf_hdr_for_lru,long offset)
{
    //修改位置标志及偏置
    ssd_buf_hdr_for_lru->user_id = 1;
    ssd_buf_hdr_for_lru->ssd_buf_tag = offset;

    //deal with self LRU queue
    if(self_strategy_ctrl->last_self_lru < 0)
    {
        self_strategy_ctrl->first_self_lru = ssd_buf_hdr_for_lru->serial_id;
        self_strategy_ctrl->last_self_lru = ssd_buf_hdr_for_lru->serial_id;
    }
    else
    {
        ssd_buf_hdr_for_lru->next_self_lru = strategy_desp[self_strategy_ctrl->first_self_lru].serial_id;
        ssd_buf_hdr_for_lru->pre_self_lru = -1;
        strategy_desp[self_strategy_ctrl->first_self_lru].pre_self_lru = ssd_buf_hdr_for_lru->serial_id;
        self_strategy_ctrl->first_self_lru =  ssd_buf_hdr_for_lru->serial_id;
    }

    // 统计块信息
    blknum_CB = blknum_CB + 1;

    return NULL;
}

//将块从原来的链表中删除(命中从原列表删除)
static volatile void *
delete_from_LRU(StrategyDesp_LRU_private * ssd_buf_hdr_for_lru)
{
    //deal with self queue

    // 判断该块在哪个链表中
    int location = ssd_buf_hdr_for_lru->user_id;

    // 若该块在CB中，则将其从CB中删除
    if ( location = 1)
    {
        if(ssd_buf_hdr_for_lru->pre_self_lru>=0)
        {
            strategy_desp[ssd_buf_hdr_for_lru->pre_self_lru].next_self_lru = ssd_buf_hdr_for_lru->next_self_lru;
        }
        else
        {
            self_strategy_ctrl->first_self_lru = ssd_buf_hdr_for_lru->next_self_lru;
        }

        if(ssd_buf_hdr_for_lru->next_self_lru>=0)
        {
            strategy_desp[ssd_buf_hdr_for_lru->next_self_lru].pre_self_lru = ssd_buf_hdr_for_lru->pre_self_lru;
        }
        else
        {
            self_strategy_ctrl->last_self_lru = ssd_buf_hdr_for_lru->pre_self_lru;
        }
        // CB链表块数量减一
        blknum_CB = blknum_CB - 1;
    }
    // 若该块在SB中，则将该块从SB链表中删除
    else if ( location = 2)
    {
        if(ssd_buf_hdr_for_lru->pre_self_lru>=0)
        {
            strategy_desp[ssd_buf_hdr_for_lru->pre_self_lru].next_self_lru = ssd_buf_hdr_for_lru->next_self_lru;
        }
        else
        {
            self_strategy_ctrl_SB->first_self_lru = ssd_buf_hdr_for_lru->next_self_lru;
        }

        if(ssd_buf_hdr_for_lru->next_self_lru>=0)
        {
            strategy_desp[ssd_buf_hdr_for_lru->next_self_lru].pre_self_lru = ssd_buf_hdr_for_lru->pre_self_lru;
        }
        else
        {
            self_strategy_ctrl_SB->last_self_lru = ssd_buf_hdr_for_lru->pre_self_lru;
        }
        // SB链表中块数量减一
        blknum_SB = blknum_SB - 1;
    }
    // 若该块在ES中，则从ES链表中删除
    else if ( location = 3)
    {
        if(ssd_buf_hdr_for_lru->pre_self_lru>=0)
        {
            strategy_desp[ssd_buf_hdr_for_lru->pre_self_lru].next_self_lru = ssd_buf_hdr_for_lru->next_self_lru;
        }
        else
        {
            self_strategy_ctrl_ES->first_self_lru = ssd_buf_hdr_for_lru->next_self_lru;
        }

        if(ssd_buf_hdr_for_lru->next_self_lru>=0)
        {
            strategy_desp[ssd_buf_hdr_for_lru->next_self_lru].pre_self_lru = ssd_buf_hdr_for_lru->pre_self_lru;
        }
        else
        {
            self_strategy_ctrl_ES->last_self_lru = ssd_buf_hdr_for_lru->pre_self_lru;
        }
        // 该链表数量减一
        blknum_ES = blknum_ES - 1;

        // !!!! band 等级改变
        band_ctrl * myband = BandCtrlArray + ssd_buf_hdr_for_lru->bandID;
        myband->pagecnt = myband->pagecnt - 1 ;
        if(myband->pagecnt == 0){
            myband->activate_after_n_cycles = 1;
        }else{
            myband->activate_after_n_cycles ++;
        }
        // band链表信息改变
        ssd_buf_hdr_for_lru->bandID = 0;
        delete_from_band(ssd_buf_hdr_for_lru , myband);


    }
    

    // 重置该块的上下链接为-1
    ssd_buf_hdr_for_lru->pre_self_lru = ssd_buf_hdr_for_lru->next_self_lru = -1;
    // 重置该块的所属链表及bandID
    ssd_buf_hdr_for_lru->user_id = 0;
    

    return NULL;
}
