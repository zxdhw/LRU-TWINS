# From SAC to LRU-TWINS

LRU-TWINS is a cache algorithm used in SSD-SMR hybrid storage systems.  This project is a prototype of user-mode cache system used to verify the performance of different cache algorithms, in which we have 5 built-in algorithms -  LRU-Twins, SAC, LRU, MOST, MOST+CDC(a module of SAC). 

##  LRU-TWINS V1.0 introduction
### Add
LRU-TWINS.c LRU-TWINS.h 
1. The basic idea remains the same
2. After a cache block is hit in ES, there is no need to delete it and add it to CB

### change
cache.c cache.h global.h
1. Write back a cache block


## Preparation

### Hardware dependencies

- There is no dedicated hardware required if just to play with the SAC. We provide a pure emulation way including to emulate the cache and SMR device so that you can quickly see and compare the characteristic for each algorithm. However, you won't see any information of the I/O time. 

- If you want to verify the cache algorithm on real device, we suggest the hardware configuration as follow: 

  - The Seagate 8TB SMR drive model ST0008AS0002 is recommended. 

  - An SSD device is required to as the cache layer of the SMR, or use a memory file or Ramdisk instead, but it is recommended to be at least 40GiB. 

### Software dependencies

- The LRU-TWINS project has been tested on CentOS Linux release 7.6.1810 (Core) based on Kernel 4.20.13-1 environment and is expected to run correctly in other Linux distributions. 
- FIO benchmark required for real SMR drive tests.

## Installation


## Experiment 

 

### Contact: 

Author: Zheng, Xuda

Affiliation: Xi'anjiaotong University of China 

Email: zhengxdhw@gmail.com
