
#ifndef _LRU_TWINS_H_
#define _LRU_TWINS_H_
#define DEBUG 0

#include "../cache.h"
/* ---------------------------lru---------------------------- */

// 负责记录责记录缓存块的信息，如：位置，上下块，偏移等。
typedef struct
{
	long 		serial_id;			// the corresponding descriptor serial number.
    int         user_id;            //暂定为确定该块属于那一个链表：00，01，10, 11(00表示 块未使用)
    SSDBufTag 	ssd_buf_tag;        // 块的偏移
    pthread_mutex_t lock;
    long     	bandID;
    // LRU 链表
    long        next_self_lru;
    long        pre_self_lru;
    // BAND 链表
    long        next_self_band;
    long        pre_self_band;

} StrategyDesp_LRU_private;

//负责记录链表的信息，如链表头位置，链表尾位置，块数等。
typedef struct
{
    long        first_self_lru;          // Head of list of LRU
    long        last_self_lru;           // Tail of list of LRU
    pthread_mutex_t lock;
    blkcnt_t    count;
} StrategyCtrl_LRU_private;

//负责管理缓存块所属band的信息，如：id，头尾，分数等。
typedef struct 
{
    unsigned long bandId;
    long pagecnt;
    long head, tail;
    int activate_after_n_cycles; // 初始为1，排序前自减一，若为1则加入排序。
    unsigned long score;   // 计算分数，脏块与干净块 ,暂时未用（可以同权重，也可以不同权重）
} band_ctrl;


// 定义三个链表的块数，以及最大值
extern int blknum_CB;
extern int blknum_SB;
extern int blknum_ES;

extern int max_blknum_CB;
extern int max_blknum_SB;
extern int max_blknum_ES;



extern int Init_SSDBuf_For_LRU_private();
extern int Unload_Buf_LRU_private(long * out_despid_array, int max_n_batch);
extern int HitIn_Buf_LRU_private(long serial_id,long * out_despid_array, int max_n_batch);
extern int Insert_Buf_LRU_private(long serial_id,long * out_despid_array, int max_n_batch);
#endif // _LRU_TWINS_H_
