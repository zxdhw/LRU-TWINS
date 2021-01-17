#ifndef SMR_SSD_CACHE_SMR_EMULATION_H
#define SMR_SSD_CACHE_SMR_EMULATION_H

#include "../global.h"
#include "../statusDef.h"

#define DEBUG 0
/* ---------------------------smr simulator---------------------------- */
#include <pthread.h>

typedef struct
{
    off_t offset;
} DespTag;

typedef struct
{
    DespTag tag;
    long    despId;
    int     isValid;
} FIFODesc;

typedef struct
{
	unsigned long	n_used;  //已经使用
             long   head, tail;  // 头 尾
} FIFOCtrl;

extern int  fd_fifo_part;
extern int  fd_smr_part;
extern void InitEmulator();
extern int simu_smr_read(char *buffer, size_t size, off_t offset);
extern int simu_smr_write(char *buffer, size_t size, off_t offset);
extern void Emu_PrintStatistic();
extern void Emu_ResetStatisic();
extern void CloseSMREmu();
#endif
