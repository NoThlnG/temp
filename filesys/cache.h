#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include <list.h>

struct list cache_list;
uint32_t cache_size;

struct cache_entry {
  uint8_t block[BLOCK_SECTOR_SIZE];
  block_sector_t sector;
  bool dirty;
  bool accessed;
  int open_cnt;
  struct list_elem elem;
};

void cache_init (void);
struct cache_entry* get_cache (block_sector_t);
struct cache_entry* add_cache (block_sector_t, bool);
struct cache_entry* evict_cache (void);
struct cache_entry* check_cache (block_sector_t, bool);
void cache_write_all (bool);
void thread_func_write_back (void *aux);

#endif /* filesys/cache.h */
