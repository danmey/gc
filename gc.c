#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
//#define LOGGING
#ifdef LOGGING
#define LOG(...) do {printf("gc_log: ");printf( __VA_ARGS__ );printf("\n");} while(0)
#else
#define LOG(...)
#endif
// Let's assume constant size of younger heap chunks
#define GC_MINOR_CHUNK_SIZE 256
#define GC_MINOR_HEAP_SIZE (1024*64)
#define GC_MAJOR_HEAP_SIZE (1024*1024*32)
// Let's assume that every data structure is pointer size aligned
#define GC_MINOR_CHUNKS (GC_MINOR_HEAP_SIZE/GC_MINOR_CHUNK_SIZE)
#define GC_MAX_REF_COUNT 65536
#define GC_MINOR_RAW_CHUNK_SIZE (WITHOUT_HEADER((GC_MINOR_CHUNK_SIZE)))
#define GC_MINOR_BITS_SIZE (WITHOUT_HEADER(GC_MINOR_CHUNK_SIZE))
#define GC_FLAG_FREE 0
#define GC_COL_WHITE 1
#define GC_COL_GREY 2
#define GC_COL_BLACK 3
#define GC_MARKED 1
// We need to sacrifice first 4 bytes flag/size
// let's first bit to be set if chunk has been marked during collection
// initialy 0
#define WITH_HEADER(size) ((size) + sizeof(int))
#define WITHOUT_HEADER(size) ((size) - sizeof(int))
#define PTR_INDEX(ofs) ((ofs)/sizeof(void*))
#define BYTE_INDEX(ofs) ((ofs)*sizeof(void*))
#define CHUNK_AT(i) (&gc_minor_heap[(i)*GC_MINOR_CHUNK_SIZE])
#define BITS(ch) (WITH_HEADER(ch))
#define BITS_AT(ch, idx) ((((void**)(BITS((ch))+(idx)*sizeof(void*)))))
#define FLAGS(ch) (*((unsigned int*) (ch)))
// Mark chunk with least significant bit set to 1
#define MARK_CHUNK(ch,col) ((FLAGS(ch)) = CHUNK_SIZE(ch)|col);
// Extract chunk size from flag/size field
#define CHUNK_SIZE(ch) (FLAGS((ch)) & (~3))
#define CHUNK_FLAGS(ch) (FLAGS((ch)) & (3))
// Align by 4 bytes boundary (TODO: make it more portable)
#define ALIGN(ptr) (((ptr)+3)&~3)
// if it's not marked
#define MEM_TAG(ptr) (!(((unsigned int)(ptr))&1))
// If the pointer is pointing the first generation heap
#define POINTS_MINOR(ptr) (((byte*)(ptr)) >= &gc_minor_heap[0] && ((byte*)(ptr)) < &gc_minor_heap[GC_MINOR_HEAP_SIZE])
#define POINTS_MAJOR(ptr) (((byte*)(ptr)) >= &gc_major_heap[0] && ((byte*)(ptr)) < &gc_major_heap[GC_MAJOR_HEAP_SIZE])
// Calculate address of begining of chunk
#define MINOR_CHUNK(ptr) (((((byte*)(ptr) - &gc_minor_heap[0])/GC_MINOR_CHUNK_SIZE)*GC_MINOR_CHUNK_SIZE)+&gc_minor_heap[0])
// Find if the chunk points to another chunk
#define REF_PTR(ptr) (MEM_TAG((ptr)) && POINTS_MINOR((ptr)))
#define REF_PTR_MJ(ptr) (MEM_TAG((ptr)) && POINTS_MAJOR((ptr)))
// Is it marked?
#define MARKED(ch) (FLAGS(ch) & 1)
#define CHUNK_OFFSET(ch) ((((ch))-&gc_minor_heap[0])/GC_MINOR_CHUNK_SIZE)
#define FOR_EACH_MINCH(ch)  for(i=0; i < gc_cur_min_chunk; ++i) {	\
  byte *ch = CHUNK_AT(i); do { } while(0)
#define END_FOR_EACH_MINCH() }

#define FOR_EACH_REF(ref,counter)					\
  int counter;								\
  for( counter=0; counter<gc_ref_count;++counter) {			\
    void** ref;								\
  for(ref = gc_ref_tab[counter][0]; ref < gc_ref_tab[counter][1]; ref++) { \
  if ( ref != 0 ) {

#define END_FOR_EACH_REF() } } }

void **gc_ref_tab[GC_MAX_REF_COUNT][2];
int gc_ref_count = 0;
// need to be aligned!
typedef char byte;
// Our first generation heap
byte gc_minor_heap[GC_MINOR_HEAP_SIZE];
// Elder generation heap
byte gc_major_heap[GC_MAJOR_HEAP_SIZE];
// Points to free chunk
int gc_cur_min_chunk = 0;
void *gc_backpatch_table[GC_MINOR_CHUNKS];


static void
set_ref_block(int index, void * start, void * end)
{
  gc_ref_tab[index][0] = start;
  gc_ref_tab[index][1] = end;
}


void
gc_add_ref (void **mem_begin, void** mem_end)
{
  if ( mem_begin == mem_end ) return;
  if ( mem_begin > mem_end ) {
    void** t = mem_begin;
    mem_begin = mem_end;
    mem_end = t;
  }
  //  void **ref = r;
  //  if (r == 0 || (!POINTS_MINOR (*ref) && !POINTS_MAJOR (*ref)))
  //    return;
  int i;
  // if teh reference is already there, discard it
  for (i = 0; i < gc_ref_count; ++i)
    if (mem_begin >= gc_ref_tab[i][0] && mem_end <= gc_ref_tab[i][1])
      return;

  for (i = 0; i < gc_ref_count; ++i)
    if (gc_ref_tab[i][0] == 0)
      {
	set_ref_block(i, mem_begin, mem_end);
	return;
      }
  assert (gc_ref_count < GC_MAX_REF_COUNT);
  set_ref_block(gc_ref_count++, mem_begin, mem_end);
  return;
}

void
gc_add_single_ref(void* ref) { gc_add_ref(ref, ref+sizeof(void*)); }

void
gc_remove_ref (void* mem_begin, void* mem_end)
{
  if ( mem_begin == mem_end ) return;
  if ( mem_begin > mem_end ) {
    void** t = mem_begin;
    mem_begin = mem_end;
    mem_end = t;
  }
  //  if ( !POINTS_MINOR(ref) && !POINTS_MAJOR(ref) ) return;
  int i;
  for (i = 0; i < gc_ref_count - 1; ++i)
    if ( (void**)mem_begin >= gc_ref_tab[i][0] && (void**)mem_end <= gc_ref_tab[i][1] )
      {
	gc_ref_tab[i][0] = 0;
	return;
      }

  if ((void**)mem_begin >= gc_ref_tab[i][0] && (void**)mem_end <= gc_ref_tab[i][1] )
    gc_ref_count--;
}

void gc_remove_single_ref(void* ref) { gc_remove_ref(ref, ref+sizeof(void*)); }

static void
gc_print_refs ()
{
  printf ("**List of stored references. %d references.\n", gc_ref_count);
  FOR_EACH_REF(ref, i)
    if ( gc_ref_tab[i][0] != 0 ) {
      char *points_to = "??";
	void *heap = 0;
	if (POINTS_MINOR (*gc_ref_tab[i][0]))
	  {
	    points_to = "minor";
	    heap = gc_minor_heap;
	  }
	if (POINTS_MAJOR (*gc_ref_tab[i][0]))
	  {
	    points_to = "major";
	    heap = gc_major_heap;
	}
	printf ("\tReference pointing to %.8x(%s)\t\n",
		*gc_ref_tab[i][0] - heap, points_to, *gc_ref_tab[i][0]);
    }
    else
      printf ("\tEmpty slot\n");
  END_FOR_EACH_REF()
  printf ("**End of stored ref list\n");
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Marks ref
static void
mark_chunk (byte * ch)
{
  MARK_CHUNK (ch, GC_MARKED);
  LOG ("Starting marking chunk: %d", ch - &gc_minor_heap[0]);
  int i;
  for (i = 0; i < PTR_INDEX (CHUNK_SIZE (ch)); ++i)
    {
      if (REF_PTR (*BITS_AT (ch, i)))
	{
	  byte *ptr = *BITS_AT (ch, i);
	  byte *ref_ch = MINOR_CHUNK (ptr);
	  int idx = (ch - &gc_minor_heap[0]);
	  //      gc_backpatch_table[PTR_INDEX(idx)][0] = (void*)ptr;
	  if (!MARKED (ref_ch))
	    mark_chunk (ref_ch);
	}
    }
}

static byte *
find_major_chunk (byte * ptr)
{
  byte *cur;
  for (cur = &gc_major_heap[0];
       !(ptr >= cur && (ptr < (cur + CHUNK_SIZE (cur)))) 
	 && cur < &gc_major_heap[GC_MAJOR_HEAP_SIZE];
       cur = cur + CHUNK_SIZE (cur))
    ;
  if (cur < &gc_major_heap[GC_MAJOR_HEAP_SIZE])
    return cur;
  return 0;
}

static void
mark_major_chunk (byte * ch)
{
  if (CHUNK_FLAGS (ch) != GC_COL_BLACK)
    {
      MARK_CHUNK (ch, CHUNK_FLAGS (ch) + 1);
      LOG ("Starting marking chunk: %d", ch - &gc_major_heap[0]);
      int i;
      for (i = 0; i < PTR_INDEX (CHUNK_SIZE (ch)); ++i)
	{
	  if (REF_PTR_MJ (*BITS_AT (ch, i)))
	    {
	      byte *ptr = *BITS_AT (ch, i);
	      byte *ref_ch = find_major_chunk (ptr);
	      mark_major_chunk (ref_ch);
	    }
	}
    }
}

static void
mark_minor ()
{
  memset (gc_backpatch_table, 0, sizeof (gc_backpatch_table));
  FOR_EACH_REF(ref, i)
    {
      if (REF_PTR (*ref))
	{
	  byte *ch = MINOR_CHUNK (*ref);
	  mark_chunk (ch);
	}
    }
  END_FOR_EACH_REF()
}

static void *
major_alloc (int size)
{
  if (size == 0)
    return 0;
  size = WITH_HEADER (ALIGN (size));
  byte *cur;
  for (cur = &gc_major_heap[0];
       (CHUNK_FLAGS (cur) != GC_FLAG_FREE 
	|| size > CHUNK_SIZE (cur)) 
	 && cur < &gc_major_heap[GC_MAJOR_HEAP_SIZE]; 
       cur = cur + CHUNK_SIZE (cur))
    ;

  if (cur >= &gc_major_heap[GC_MAJOR_HEAP_SIZE])
    return 0;

  byte *free_chunk = cur;
  unsigned int prev_size = CHUNK_SIZE (free_chunk);
  FLAGS (free_chunk) = size;
  MARK_CHUNK (free_chunk, GC_COL_WHITE);
  byte *next_chunk = (cur + size);
  FLAGS (next_chunk) = prev_size - size;
  return (void *) WITH_HEADER (free_chunk);
}

static void
backpatch_chunk (byte * ch)
{
  int i;
  for (i = 0; i < PTR_INDEX (CHUNK_SIZE (ch)); ++i)
    {
      if (REF_PTR (*BITS_AT (ch, i)))
	{
	  byte *ptr = *BITS_AT (ch, i);
	  byte *ref_ch = MINOR_CHUNK (ptr);
	  if (MARKED (ref_ch))
	    {
	      byte *new_ptr =
		(byte *) gc_backpatch_table[CHUNK_OFFSET (ref_ch)];
	      assert (new_ptr != 0);
	      int delta = new_ptr - ref_ch;
	      *BITS_AT (ch, i) += delta;
	    }
	}
    }
}

static void
add_minor_chunk_refs (byte * ch)
{
  int i;
  for (i = 0; i < PTR_INDEX (CHUNK_SIZE (ch)); ++i)
    {
      if (REF_PTR (*BITS_AT (ch, i)))
	{
	  void **ptr = BITS_AT (ch, i);
	  byte *ref_ch = MINOR_CHUNK (*ptr);
	  if (MARKED (ref_ch))
	    gc_add_single_ref (ptr);
	}
    }
}

static void
backpatch_refs ()
{
  FOR_EACH_REF(ptr, i)
    {
      if (POINTS_MINOR (*ptr))
	{
	  byte *ch = MINOR_CHUNK (*ptr);
	  if (MARKED (ch))
	    {
	      byte *new_ptr =
		(byte *) gc_backpatch_table[CHUNK_OFFSET (ch)];
		  if (new_ptr != 0)
		    {
		      int delta = new_ptr - ch;
		      *ptr += delta;
		    }
	    }
	}
    }
  END_FOR_EACH_REF();
}

void gc_print_major ();
void
copy_minor_heap ()
{
  int i;
  FOR_EACH_MINCH (ch);
  {
    if (MARKED (ch))
      {
	void *ptr =
	  WITHOUT_HEADER (major_alloc (WITHOUT_HEADER (CHUNK_SIZE (ch))));
	assert (ptr != 0);
	gc_backpatch_table[i] = ptr;
	//      add_minor_chunk_refs(ch);
      }
  }
  //  gc_print_major();
  END_FOR_EACH_MINCH ();

  backpatch_refs ();

  FOR_EACH_MINCH (ch);
  {
    backpatch_chunk (ch);
  }
  END_FOR_EACH_MINCH ();

  FOR_EACH_MINCH (ch);
  {
    if (MARKED (ch))
      {
	byte *new_ptr = (byte *) gc_backpatch_table[i];
	assert (new_ptr != 0);
	//      memcpy(new_ptr, WITH_HEADER(ch), WITHOUT_HEADER(CHUNK_SIZE(ch)));
	memcpy (new_ptr, ch, CHUNK_SIZE (ch));
      }
  }
  END_FOR_EACH_MINCH ();
}

static void
darken_chunk (byte * ptr)
{
  byte *cur = find_major_chunk (ptr);
  if (cur != 0 && CHUNK_FLAGS (cur) == GC_COL_WHITE)
    {
      MARK_CHUNK (cur, GC_COL_GREY);
    }
}

static void
darken_roots ()
{
  FOR_EACH_REF(ptr, i)
      darken_chunk (*ptr);
  END_FOR_EACH_REF();
}

static void
darken_major ()
{
  byte *cur;
  for (cur = &gc_major_heap[0];
       cur < &gc_major_heap[GC_MAJOR_HEAP_SIZE]; cur = cur + CHUNK_SIZE (cur))
    {
      int fl = CHUNK_FLAGS (cur);
      switch (fl)
	{
	case GC_COL_GREY:
	  mark_major_chunk (cur);
	  break;
	}
    }
}

static void
collect_major ()
{
  darken_roots ();
  darken_major ();
  byte *cur;
  for (cur = &gc_major_heap[0];
       cur < &gc_major_heap[GC_MAJOR_HEAP_SIZE]; cur = cur + CHUNK_SIZE (cur))
    {
      int fl = CHUNK_FLAGS (cur);
      switch (fl)
	{
	case GC_COL_WHITE:
	  MARK_CHUNK (cur, GC_FLAG_FREE);
	  break;
	}
    }
}

static void
collect_minor ()
{
  LOG ("Starting minor collection.\n");
  mark_minor ();
  copy_minor_heap ();
  gc_cur_min_chunk = 0;
}

static void *
minor_alloc (int size)
{
  if (size == 0)
    return 0;
  //  assert(size != 0);
  size = WITH_HEADER (ALIGN (size));

  if (size > GC_MINOR_CHUNK_SIZE)
    return 0;			//major_alloc(size);

  if (gc_cur_min_chunk >= GC_MINOR_CHUNKS)
    {
      collect_minor ();
      return minor_alloc (WITHOUT_HEADER (size));
    }
  FLAGS (CHUNK_AT (gc_cur_min_chunk)) = size;
  return BITS (CHUNK_AT (gc_cur_min_chunk++));
}

void *
gc_alloc (int size)
{
  void *ptr = minor_alloc (size);
  if (ptr == 0)
    return major_alloc (size);
  return ptr;
}

void
gc_print_major ()
{
  printf ("**List of major heap allocated chunks\n");
  int i;
  byte *cur;
  for (cur = &gc_major_heap[0];
       cur < &gc_major_heap[GC_MAJOR_HEAP_SIZE]; cur = cur + CHUNK_SIZE (cur))
    {
      int fl = CHUNK_FLAGS (cur);
      char *ch_type = 0;
      switch (fl)
	{
	case GC_FLAG_FREE:
	  ch_type = "free";
	  break;
	case GC_COL_WHITE:
	  ch_type = "white";
	  break;
	case GC_COL_GREY:
	  ch_type = "grey";
	  break;
	case GC_COL_BLACK:
	  ch_type = "black";
	  break;
	}
      printf ("\tsize %.3d\tmarked %s\n", CHUNK_SIZE (cur), ch_type);
    }
  printf ("**End of major chunks list\n");
}

void
gc_print_minor ()
{
  printf ("**List of minor heap allocated %d chunks\n", gc_cur_min_chunk);
  int i;
  FOR_EACH_MINCH (ch);
  {
    printf ("\tchunk %.4d\tsize %.3d\tmarked %c\n", CHUNK_OFFSET (ch),
	    CHUNK_SIZE (ch), (MARKED (ch) ? 'y' : 'n'));
  }
  END_FOR_EACH_MINCH ();
  printf ("**End of minor chunks list\n");
}

void
gc_reset ()
{
  gc_cur_min_chunk = 0;
  FLAGS (&gc_major_heap[0]) = GC_MAJOR_HEAP_SIZE;
  memset (gc_backpatch_table, 0, sizeof (gc_backpatch_table));
  gc_ref_count = 0;
}

