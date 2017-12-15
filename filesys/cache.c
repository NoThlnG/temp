#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "devices/timer.h"

void cache_init (void)
{
  list_init(&cache_list);
  cache_size = 0;
  lock_init(&CACHELOCK);
  thread_create("write_back_thread", PRI_MAX, thread_func_write_back, NULL);
}

struct cache_entry* get_cache (block_sector_t sector)
{
  struct cache_entry *c;
  struct list_elem *e;
  for (e = list_begin(&cache_list); e != list_end(&cache_list);
      e = list_next(e))
  {
    c = list_entry(e, struct cache_entry, elem);
    if (c->sector == sector)
      return c;
  }
  return NULL;
}

struct cache_entry* add_cache (block_sector_t sector, bool dirty)
{
  struct cache_entry *c;
  if (cache_size < 64)
  {
    cache_size++;
    c = malloc(sizeof(struct cache_entry));
    if (!c)
      return NULL;
    c->open_cnt=0;
    list_push_back(&cache_list, &c->elem);
  }
  else
  {
    while(1)
    {
      c=evict_cache(); 
      if(c)
        break;
    }
  }

  c->open_cnt++;
  c->sector = sector;
  block_read(fs_device, c->sector, &c->block);
  c->dirty = dirty;
  c->accessed = true;
  return c;
}

struct cache_entry* evict_cache(void) {
  struct list_elem *e;
  struct cache_entry *c;
  for (e = list_begin(&cache_list); e != list_end(&cache_list);
       e = list_next(e))
  {
    c = list_entry(e, struct cache_entry, elem);
    if (c->open_cnt>0)
      continue;
    if (c->accessed)
      c->accessed = false;
    else
    {
      if (c->dirty)
        block_write(fs_device, c->sector, &c->block);
      return c;
    }
  }
  return NULL;
}

struct cache_entry* check_cache (block_sector_t sector, bool dirty)
{
  lock_acquire(&CACHELOCK);  
  struct cache_entry *c = get_cache(sector);

  if(c) {
    c->open_cnt++;
    c->dirty |= dirty;
    c->accessed = true;
  }
  else {
    c = add_cache(sector, dirty);
    if (!c)
      PANIC("Not enough memory for buffer cache.");
  }
  lock_release(&CACHELOCK);
  return c;
}

void cache_write_all (bool clear)
{
  struct list_elem *next, *e;
  for(e= list_begin(&cache_list); e != list_end(&cache_list);)
  {
    next = list_next(e);
    struct cache_entry *c = list_entry(e, struct cache_entry, elem);
    if (c->dirty)
    {
      block_write (fs_device, c->sector, &c->block);
      c->dirty = false;
    }
    if (clear)
    {
      list_remove(&c->elem);
      free(c);
    }
    e = next;
  }
}

void thread_func_write_back(void *aux UNUSED)
{
  while(true)
  {
    timer_sleep(5*TIMER_FREQ);
    cache_write_all(false);
  }
}

