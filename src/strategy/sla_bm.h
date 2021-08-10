#ifndef _SLA_BM_H_
#define _SLA_BM_H_
#include "../cache.h"
#include "../global.h"

extern int Init_SlaBm();
extern int LogIn_SlaBm(long despId, SSDBufTag tag, unsigned flag);
extern int Hit_SlaBm(long despId, unsigned flag);
extern int LogOut_SlaBm(long * out_despid_array, int max_n_batch);
#endif // _PORE_H
