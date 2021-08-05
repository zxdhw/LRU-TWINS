/*
  This is a updated versoin (v2) based on log-simulater.
  There are two most important new designs:
  1. FIFO Write adopts APPEND_ONLY, instead of in-place updates.
  2. When the block choosing to evict out of FIFO is a "old version",
     then drop it off，won't activate write-back, and move head pointer to deal with next one.
*/
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <memory.h>
#include <aio.h>

#include "../global.h"
#include "../statusDef.h"
#include "../report.h"
#include "../cache.h"
#include "../timerUtils.h"
#include "hashtb_pb.h"


#define OFF_BAND_TMP_PERSISIT   0 // The head 80MB of FIFO for temp persistence band needed to clean.
#define OFF_PB                80*1024*1024
#define OFF_BAND_REGION         20*1024*1000*1000 // 20GB

#ifdef EMU_NO_DISK_IO
#define DISK_READ(fd,buf,size,offset) (size)
#define DISK_WRITE(fd,buf,size,offset) (size)
#else
#define DISK_READ(fd,buf,size,offset) (pread(fd,buf,size,offset))
#define DISK_WRITE(fd,buf,size,offset) (pwrite(fd,buf,size,offset))
#endif

static FIFOCtrl global_fifo_ctrl;
static FIFODesc* fifo_desp_array;
static char* BandBuffer;
static blksize_t NSMRBands = 266600;		// smr band cnt = 266600; almost 8TB data size.
static unsigned long BNDSZ = 40*1000*1024;      // bandsize =40MB  (20MB~40MB)


int ACCESS_FLAG = 1;

pthread_mutex_t simu_smr_fifo_mutex;

static long	band_size_num;
static long	num_each_size;

static FIFODesc* getFIFODesp();
// static void* smr_fifo_monitor_thread();
static void flushFIFO();

static long simu_read_smr_bands;
static long simu_flush_bands;
static long simu_flush_band_size;

static blkcnt_t simu_n_collect_fifo;
static blkcnt_t simu_n_read_fifo;
static blkcnt_t simu_n_write_fifo;
static blkcnt_t simu_n_read_smr;

static blkcnt_t simu_n_fifo_write_HIT = 0;
static double simu_time_read_fifo;
static double simu_time_read_smr;
static double simu_time_write_smr;
static double simu_time_write_fifo;

static double simu_time_collectFIFO = 0; /** To monitor the efficent of read all blocks in same band **/


static int invalidDespInFIFO(FIFODesc* desp);
#define isFIFOEmpty (global_fifo_ctrl.head == global_fifo_ctrl.tail)
#define isFIFOFull  ((global_fifo_ctrl.tail + 1) % (NBLOCK_SMR_PB + 1) == global_fifo_ctrl.head)

static unsigned long getBandSize(off_t offset);
static off_t getBandOffset(off_t blk_off);

/** AIO related for blocks collected in FIFO **/
#define max_aio_count 6000
struct aiocb aiolist[max_aio_count];
struct aiocb* aiocb_addr_list[max_aio_count];/* >= band block size */


/*
 * init inner ssd buffer hash table, strategy_control, buffer, work_mem
 */
void InitEmulator()
{
    /* initialliz related constants */
    band_size_num = (BNDSZ / 1024000) / 2 + 1;  //一共21种不同大小的band
    num_each_size = NSMRBands / band_size_num;  //每种大小band对应的平均数量

    global_fifo_ctrl.n_used = 0;  //已经使用为0
    global_fifo_ctrl.head = global_fifo_ctrl.tail = 0; // 队列为空

    posix_memalign((void**) &fifo_desp_array, 1024,sizeof(FIFODesc) * (NBLOCK_SMR_PB + 1));  // 分配内存


    FIFODesc* fifo_hdr = fifo_desp_array;
    long i;
    for (i = 0; i < (NBLOCK_SMR_PB + 1); fifo_hdr++, i++)  //初始化fifo_desp_array
    {
        fifo_hdr->despId = i;
        fifo_hdr->isValid = 0;
    }

    posix_memalign((void**) &BandBuffer, 512, sizeof(char) * BNDSZ * 50);

    pthread_mutex_init(&simu_smr_fifo_mutex, NULL);   // 初始化互斥锁  

    /** AIO related **/

    /** statistic related **/
    simu_read_smr_bands = 0;
    simu_flush_bands = 0;
    simu_flush_band_size = 0;

    simu_n_collect_fifo = 0;
    simu_n_read_fifo = 0;
    simu_n_write_fifo = 0;
    simu_n_read_smr = 0;
    simu_time_read_fifo = 0;
    simu_time_read_smr = 0;
    simu_time_write_smr = 0;
    simu_time_write_fifo = 0;

    initSSDTable(NBLOCK_SMR_PB + 1);

    Log_emu = fopen(Log_emu_path, "w+");
    if(Log_emu == NULL)
        sac_error_exit("cannot open log: Log_emu.");
}

/** \brief
 *  LEGACY: To monitor the FIFO in SMR, and do cleanning operation when idle status.
 */
// static void*
// smr_fifo_monitor_thread()
// {
//     pthread_t th = pthread_self();
//     pthread_detach(th);
//     int interval = 10;
//     printf("Simulator Auto-clean thread [%lu], clean interval %d seconds.\n",th,interval);

//     while (1)
//     {
//         pthread_mutex_lock(&simu_smr_fifo_mutex);
//         if (!ACCESS_FLAG)
//         {
//             flushFIFO();
//             pthread_mutex_unlock(&simu_smr_fifo_mutex);
//             if (DEBUG)
//                 printf("[INFO] freeStrategySSD():--------after clean\n");
//         }
//         else
//         {
//             ACCESS_FLAG = 0;
//             pthread_mutex_unlock(&simu_smr_fifo_mutex);
//             sleep(interval);
//         }
//     }
//     return NULL;
// }

int
simu_smr_read(char *buffer, size_t size, off_t offset)
{
    pthread_mutex_lock(&simu_smr_fifo_mutex);
    DespTag		tag;
    FIFODesc    *ssd_hdr;
    size_t		i;
    int	        returnCode;
    long		ssd_hash;
    long		despId;
    struct timeval	tv_start,tv_stop;

    for (i = 0; i * BLKSZ < size; i++)
    {
        tag.offset = offset + i * BLKSZ;
        ssd_hash = ssdtableHashcode(tag);
        despId = ssdtableLookup(tag, ssd_hash);

        if (despId >= 0)
        {
            /* read from fifo */
            simu_n_read_fifo++;
            ssd_hdr = fifo_desp_array + despId;

            _TimerLap(&tv_start);
            returnCode = DISK_READ(smr_fd, buffer, BLKSZ, ssd_hdr->despId * BLKSZ + OFF_PB);
            if (returnCode < 0)
            {
                printf("[ERROR] smrread():-------read from PB: fd=%d, errorcode=%d, offset=%lu\n", smr_fd, returnCode, ssd_hdr->despId * BLKSZ);
                exit(-1);
            }
            _TimerLap(&tv_stop);
            simu_time_read_fifo += TimerInterval_SECOND(&tv_start,&tv_stop);
        }
        else
        {
            /* read from actual smr */
            simu_n_read_smr++;
            _TimerLap(&tv_start);

            returnCode = DISK_READ(smr_fd, buffer, BLKSZ, offset + i * BLKSZ + OFF_BAND_REGION);
            if (returnCode < 0)
            {
                printf("[ERROR] smrread():-------read from smr disk: fd=%d, errorcode=%d, offset=%lu\n", smr_fd, returnCode, offset + i * BLKSZ);
                exit(-1);
            }
            _TimerLap(&tv_stop);
            simu_time_read_smr += TimerInterval_SECOND(&tv_start,&tv_stop);
        }
    }
    ACCESS_FLAG = 1;
    pthread_mutex_unlock(&simu_smr_fifo_mutex);
    return 0;
}

int
simu_smr_write(char *buffer, size_t size, off_t offset)
{
    pthread_mutex_lock(&simu_smr_fifo_mutex);
    DespTag		tag;
    FIFODesc        *ssd_hdr;
    size_t		i;
    int		returnCode = 0;
    long		ssd_hash;
    static struct timeval	tv_start,tv_stop;

    for (i = 0; i * BLKSZ < size; i++)
    {
        tag.offset = offset + i * BLKSZ;

        /* APPEND_ONLY */
        ssd_hdr = getFIFODesp();//获取一个FIFO描述符
        ssd_hdr->tag = tag;

        /* Update HashTable and Descriptor array */
        ssd_hash = ssdtableHashcode(tag);
        long old_despId = ssdtableUpdate(tag, ssd_hash, ssd_hdr->despId);
        if(old_despId>=0){
          FIFODesc* oldDesp = fifo_desp_array + old_despId;
          invalidDespInFIFO(oldDesp); ///invalid the old desp
        }

        _TimerLap(&tv_start);
        returnCode = DISK_WRITE(smr_fd, buffer, BLKSZ, ssd_hdr->despId * BLKSZ + OFF_PB);
        if (returnCode < 0)
        {
            printf("[ERROR] smrwrite():-------write to smr disk: fd=%d, errorcode=%d, offset=%lu\n", smr_fd, returnCode, offset + i * BLKSZ);
            exit(-1);
        }
        _TimerLap(&tv_stop);
        simu_time_write_fifo += TimerInterval_SECOND(&tv_start,&tv_stop);
        simu_n_write_fifo ++;
    }
    ACCESS_FLAG = 1;
    pthread_mutex_unlock(&simu_smr_fifo_mutex);


    return 0;
}

static int
invalidDespInFIFO(FIFODesc* desp)
{
    desp->isValid = 0;
    global_fifo_ctrl.n_used--;
    int isHeadChanged = 0;
    while(!fifo_desp_array[global_fifo_ctrl.head].isValid && !isFIFOEmpty)
    {
        global_fifo_ctrl.head = (global_fifo_ctrl.head + 1) % (NBLOCK_SMR_PB + 1);
        isHeadChanged = 1;
    }
    return isHeadChanged;
}

static FIFODesc *
getFIFODesp()
{
    FIFODesc* newDesp;
    if(isFIFOFull)
    {
        /* Log structure array is full fill */
        flushFIFO();
    }

    /* Append to tail */
    newDesp = fifo_desp_array + global_fifo_ctrl.tail;
    newDesp->isValid = 1;
    global_fifo_ctrl.tail = (global_fifo_ctrl.tail + 1) % (NBLOCK_SMR_PB + 1);

    return newDesp;
}

/** Persistent Buffer write-back dirty blocks using RMW Model: Read->Modify->Write. **/
static void
flushFIFO()
{
    if(global_fifo_ctrl.head == global_fifo_ctrl.tail) // Log structure array is empty.
        return;

    int     returnCode;
    struct  timeval	tv_start, tv_stop;
    long    dirty_n_inBand = 0;
    double  wtrAmp;

    FIFODesc* leader = fifo_desp_array + global_fifo_ctrl.head;

    /* Create a band-sized buffer for readind and flushing whole band bytes */
    long		thisBandSize = getBandSize(leader->tag.offset);
    off_t       thisBandOff = getBandOffset(leader->tag.offset);

    /** R **/
    /* read whole band from smr to buffer*/
    _TimerLap(&tv_start);
    returnCode = DISK_READ(smr_fd, BandBuffer, thisBandSize, thisBandOff + OFF_BAND_REGION);
    if((returnCode) != thisBandSize)
    {
        printf("[ERROR] flushFIFO():---------read from smr: fd=%d, errorcode=%d, offset=%lu\n",
               smr_fd, returnCode, thisBandOff);
        exit(-1);
    }
    _TimerLap(&tv_stop);
    simu_time_read_smr += TimerInterval_SECOND(&tv_start, &tv_stop);
    simu_read_smr_bands++;

    /** M **/
    /* Combine cached pages from FIFO which are belong to the same band */

    /** ---------------DEBUG BLOCK 1----------------------- **/
    struct timeval	tv_collect_start, tv_collect_stop;
    double collect_time;
    _TimerLap(&tv_collect_start);
    /**--------------------------------------------------- **/
    long curPos = leader->despId;
    while(curPos != global_fifo_ctrl.tail)
    {
        FIFODesc* curDesp = fifo_desp_array + curPos;
        long nextPos = (curDesp->despId + 1) % (NBLOCK_SMR_PB + 1);

        /* If the block belongs the same band with the header of fifo. */
        if (curDesp->isValid && (curDesp->tag.offset - thisBandOff) < thisBandSize && curDesp->tag.offset >= thisBandOff)
        {
#ifdef EMULATION_AIO
            off_t offset_inband = curDesp->tag.offset - thisBandOff;
            static int aio_read_cnt = 0;

            struct aiocb* aio_n = aiolist + aio_read_cnt;
            aio_n->aio_fildes = fd_fifo_part;
            aio_n->aio_offset = curPos * BLKSZ;
            aio_n->aio_buf = BandBuffer + offset_inband * BLKSZ;
            aio_n->aio_nbytes = BLKSZ;
            aio_n->aio_lio_opcode = LIO_READ;
            aiocb_addr_list[aio_read_cnt] = aio_n;
            aio_read_cnt++;
#else
            _TimerLap(&tv_start);
            returnCode = DISK_READ(smr_fd, BandBuffer + (curDesp->tag.offset - thisBandOff) * BLKSZ, BLKSZ, curPos * BLKSZ + OFF_PB); //ff_t offset_inband = curDesp->tag.offset - thisBandOff;
            if (returnCode < 0)
            {
                printf("[ERROR] flushFIFO():-------read from PB: fd=%d, errorcode=%d, offset=%lu\n", smr_fd, returnCode, curPos * BLKSZ);
                exit(-1);
            }
            _TimerLap(&tv_stop);
            simu_time_read_fifo += TimerInterval_SECOND(&tv_start,&tv_stop);
#endif // EMULATION_AIO
            /* clean the meta data */
            dirty_n_inBand++;
            unsigned long hash_code = ssdtableHashcode(curDesp->tag);
            ssdtableDelete(curDesp->tag, hash_code);

            int isHeadChanged = invalidDespInFIFO(curDesp); // Invalidate the current block and check if the head block get changed.
            if(isHeadChanged)
            {
                curPos = global_fifo_ctrl.head;
                continue;
            }
        }
        curPos = nextPos;
    }
    simu_n_collect_fifo += dirty_n_inBand;
#ifdef EMULATION_AIO
#ifndef EMU_NO_DISK_IO
    _TimerLap(&tv_start);
    static int cnt = 0;
    printf("start aio read [%d]...\n",++cnt);
    int ret_aio = lio_listio(LIO_WAIT,aiocb_addr_list,aio_read_cnt,NULL);
    printf("end aio\n",++cnt);
    if(ret_aio < 0)
    {
        char log[128];
        sprintf(log,"Flush [%ld] times ERROR: AIO read list from FIFO Failure[%d].\n",simu_flush_bands+1,ret_aio);
        sac_log(log, Log_emu);
    }
    _TimerLap(&tv_stop);
    simu_time_read_fifo += TimerInterval_SECOND(&tv_start,&tv_stop);
#endif // EMU_NO_DISK_IO
#endif // EMULATION_AIO
    /**--------------------------------------------------- **/
    _TimerLap(&tv_collect_stop);
    collect_time = TimerInterval_SECOND(&tv_collect_start, &tv_collect_stop);
    simu_time_collectFIFO += collect_time;
    /** ---------------DEBUG BLOCK 1----------------------- **/

    /* flush whole band to smr */
    _TimerLap(&tv_start);

    /** W **/
    returnCode = DISK_WRITE(smr_fd, BandBuffer, thisBandSize, thisBandOff + OFF_BAND_REGION);
    if (returnCode < thisBandSize)
    {
        printf("[ERROR] flushFIFO():-------write to smr: fd=%d, errorcode=%d, offset=%lu\n", smr_fd, returnCode, thisBandOff);
        exit(-1);
    }
    _TimerLap(&tv_stop);

    simu_time_write_smr += TimerInterval_SECOND(&tv_start,&tv_stop);
    simu_flush_bands++;
    simu_flush_band_size += thisBandSize;

    wtrAmp = (double)thisBandSize / (dirty_n_inBand * BLKSZ);
    STT->wtrAmp_cur = wtrAmp;
    STT->WA_sum += wtrAmp;
    STT->n_RMW ++;

    char log[256];
    sprintf(log,"[Emulator]: RMW number:%lu, write amplifcation:%f\n",STT->n_RMW, wtrAmp);
    sac_log(log, Log_emu);
    sprintf(log,"[Emulator]: dirty blocks in band colect=%ld, bandsize=%ld\n", dirty_n_inBand, thisBandSize/BLKSZ);
    sac_log(log, Log_emu);
}

static unsigned long
getBandSize(off_t offset) // 获取当前blk所在band的大小
{
    long		i, size, total_size = 0;
    for (i = 0; i < band_size_num; i++)
    {
        size = BNDSZ / 2 + i * 1024000;
        if (total_size + size * num_each_size >= offset)
            return size;
        total_size += size * num_each_size;
    }
    return 0;
}
static off_t
getBandOffset(off_t blk_off)       //获取当前块所在band的起始
{
    long i, size, total_size = 0;
    for (i = 0; i < band_size_num; i++)
    {
        size = BNDSZ / 2 + i * 1024000;
        if (total_size + size * num_each_size > blk_off)
            return total_size + ((blk_off - total_size) / size) * size;
        total_size += size * num_each_size;
    }
    return 0;
}

void Emu_PrintStatistic()
{   
    printf("----------------EMULATION------------\n");
#ifndef EMU_NO_DISK_IO
    printf("Time:\n");
    printf("PB_read_blks:\t%lf\nPB_write_blks:\t%lf\nSMR_read_blks:\t%lf\nFlush SMR:\t%lf\n",simu_time_read_fifo, simu_time_write_fifo, simu_time_read_smr, simu_time_write_smr);
    printf("Total I/O:\t%lf\n", simu_time_read_fifo+simu_time_write_fifo+simu_time_read_smr+simu_time_write_smr);
    printf("FIFO Collect:\t%lf\n",simu_time_collectFIFO);
#endif
    printf("Block/Band Count:\n");
    printf("PB_read_blks:\t%ld\nPB_write_blks:\t%ld\nGC_collect_blks:\t%ld\nSMR_read_blks:\t%ld\nPB_write_hits:\t%ld\n",simu_n_read_fifo, simu_n_write_fifo,simu_n_collect_fifo, simu_n_read_smr, simu_n_fifo_write_HIT);
    printf("RMWs:\t%ld\nGC_bandsize(Byte):\t%ld\n", simu_flush_bands, simu_flush_band_size);
    printf("WA_avg:\t%lf\n",(float)(simu_flush_band_size / BLKSZ) / STT->flush_hdd_blocks);
}

void Emu_ResetStatisic()
{
    simu_n_read_fifo = simu_n_write_fifo = simu_n_collect_fifo = simu_n_read_smr = simu_n_fifo_write_HIT =
    simu_read_smr_bands = simu_flush_bands = simu_flush_band_size =
    STT->flush_hdd_blocks = 0;
}

void CloseSMREmu()
{
    fclose(Log_emu);
}
