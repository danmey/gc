#include "gc.c"
// Basic boundary check for allocation of different sizes of chunks 
BEGIN_TEST (01) assert (minor_alloc (0) == 0);
assert (minor_alloc (GC_MINOR_RAW_CHUNK_SIZE) != 0);
assert (minor_alloc (GC_MINOR_CHUNK_SIZE) == 0);
assert (minor_alloc (GC_MINOR_RAW_CHUNK_SIZE - 2) != 0);
assert (minor_alloc (GC_MINOR_RAW_CHUNK_SIZE - sizeof (int)) != 0);
assert (minor_alloc (1) != 0);
assert (minor_alloc (3) != 0);
assert (minor_alloc (4) != 0);
assert (minor_alloc (5) != 0);
gc_print_minor ();
gc_reset ();
END_TEST ()
// Basic collection test wihtout referencing elements 
BEGIN_TEST (02)
unsigned int *ptr1 = minor_alloc (4);
unsigned int *ptr2 = minor_alloc (4);
unsigned int *ptr3 = minor_alloc (4);
gc_add_single_ref (&ptr1);
gc_add_single_ref (&ptr3);
mark_minor ();
gc_print_minor ();
gc_reset ();
END_TEST ()
// Linked list test 
BEGIN_TEST (03)
unsigned int *ptr1 = minor_alloc (4);
unsigned int *ptr2 = minor_alloc (4);
unsigned int *ptr3 = minor_alloc (4);
*ptr1 = (unsigned int) ptr2;
*ptr2 = (unsigned int) ptr3;
gc_add_single_ref (&ptr1);
mark_minor ();
gc_print_minor ();
gc_reset ();
END_TEST ()
// Circular list
BEGIN_TEST (04)
unsigned int *ptr1 = minor_alloc (4);
unsigned int *ptr2 = minor_alloc (4);
unsigned int *ptr3 = minor_alloc (4);
gc_add_single_ref (&ptr1);
*ptr1 = (unsigned int) ptr2;
*ptr2 = (unsigned int) ptr3;
*ptr3 = (unsigned int) ptr1;
mark_minor ();
gc_print_minor ();
gc_reset ();
END_TEST ()
// Various values stored in the chunk
BEGIN_TEST (05)
unsigned int *ptr1 = minor_alloc (252);
unsigned int *ptr2 = minor_alloc (252);
unsigned int *ptr3 = minor_alloc (252);
unsigned int *ptr4 = minor_alloc (252);
gc_add_single_ref (&ptr1);
ptr1[0] = (unsigned int) ptr2;
ptr1[3] = 3;
ptr1[4] = 5;
ptr1[32] = (unsigned int) ptr3;
ptr1[60] = -1;
ptr1[61] = -3;
ptr1[62] = (unsigned int) ptr4;
mark_minor ();
gc_print_minor ();
gc_reset ();
END_TEST ()
// Test for exceeding allocation memory of first heap
BEGIN_TEST (06) gc_reset ();
int
i;
for (i = 0; i < GC_MINOR_CHUNKS + 4; i++)
  minor_alloc (4);
gc_print_minor ();
gc_print_major ();
END_TEST ()
// Checking initial layout of elder heap
BEGIN_TEST (07) gc_reset ();
gc_print_major ();
END_TEST ()
// Checking allocation on major heap
BEGIN_TEST (08) gc_reset ();
assert (major_alloc (0) == 0);
assert (major_alloc (WITHOUT_HEADER (GC_MAJOR_HEAP_SIZE)) != 0);
gc_print_major ();
assert (major_alloc (4) == 0);
gc_reset ();
assert (major_alloc (WITHOUT_HEADER (GC_MAJOR_HEAP_SIZE) + 1) == 0);
assert (major_alloc
	(WITHOUT_HEADER (GC_MAJOR_HEAP_SIZE) - 2 * sizeof (unsigned int)) !=
	0);
assert (major_alloc (4) != 0);
gc_print_major ();
END_TEST ()
// Refs test
BEGIN_TEST (09) gc_reset ();
// Initial empty ref table
gc_print_refs ();
// Adding invalid reference, shouldnt alter the table
gc_add_single_ref (0);
gc_print_refs ();

#define ADD_REF(name, ptr) byte* name = ptr; gc_add_single_ref(&name);
// adding one reference from minor heap
ADD_REF (ptr1, &gc_minor_heap[0]) gc_print_refs ();

// lets assign a value so printing will yield result
*((void **) &gc_minor_heap[0]) = (void *) 0x123456;
// invalid minor pointer
ADD_REF (ptr2, &gc_minor_heap[GC_MINOR_HEAP_SIZE]);
// last pointer on minor heap
ADD_REF (ptr3, &gc_minor_heap[GC_MINOR_HEAP_SIZE - sizeof (int)]);
// invalid major heap pointer
gc_add_single_ref (&ptr2);
gc_add_single_ref (&ptr3);
gc_print_refs ();

// remove last pointer
ADD_REF (ptr6, &gc_major_heap[GC_MAJOR_HEAP_SIZE - sizeof (int)]);
gc_remove_single_ref (&ptr6);
// replace with minor heap ref (which is already, does nothing)
gc_add_single_ref (&ptr3);
gc_print_refs ();
// add pointer at the end
ADD_REF (ptr8,
	 &gc_minor_heap[GC_MINOR_HEAP_SIZE - sizeof (int) - sizeof (int)]);
gc_print_refs ();
// remove last pointer
gc_remove_single_ref (&gc_minor_heap
	       [GC_MINOR_HEAP_SIZE - sizeof (int) - sizeof (int)]);
gc_print_refs ();
gc_remove_single_ref (&gc_minor_heap[0]);
gc_print_refs ();
ADD_REF (ptr9, &gc_minor_heap[0]);
gc_print_refs ();

END_TEST ()
#define MIN_OFFS(ptr) (PTR_INDEX(((byte*)ptr-&gc_minor_heap[0])))
#define MAJ_OFFS(ptr) (PTR_INDEX(((byte*)ptr-&gc_major_heap[0])))
// Test for backaprthing one pass
BEGIN_TEST (10) gc_reset ();
unsigned int *
ptr1 = minor_alloc (4);
unsigned int *
ptr2 = minor_alloc (4);
unsigned int *
ptr3 = minor_alloc (4);
unsigned int *
ptr4 = minor_alloc (4);
unsigned int *
ptr5 = minor_alloc (4);
ptr1[0] = (unsigned int) ptr2;
ptr2[0] = (unsigned int) ptr3;
ptr3[0] = (unsigned int) ptr4;
ptr4[0] = (unsigned int) ptr5;
ptr5[0] = (unsigned int) ptr1;

printf ("**Printing referenced minor heap offsets\n");
printf ("\t%d\n", MIN_OFFS (ptr1[0]));
printf ("\t%d\n", MIN_OFFS (ptr2[0]));
printf ("\t%d\n", MIN_OFFS (ptr3[0]));
printf ("\t%d\n", MIN_OFFS (ptr4[0]));
printf ("\t%d\n", MIN_OFFS (ptr5[0]));
gc_add_single_ref (&ptr1);
mark_minor ();
copy_minor_heap ();
printf ("**Printing referenced major heap offsets\n");
printf ("\t%d\n", MAJ_OFFS (ptr1[0]));
printf ("\t%d\n", MAJ_OFFS (ptr2[0]));
printf ("\t%d\n", MAJ_OFFS (ptr3[0]));
printf ("\t%d\n", MAJ_OFFS (ptr4[0]));
printf ("\t%d\n", MAJ_OFFS (ptr5[0]));
gc_print_major ();
//gc_print_backpatch();
gc_reset ();
END_TEST ()
// Test for backpatching circular list, with more slots
BEGIN_TEST (11) gc_reset ();
unsigned int *
ptr1 = minor_alloc (8);
unsigned int *
ptr2 = minor_alloc (8);
ptr1[0] = (unsigned int) ptr2;
ptr2[0] = (unsigned int) ptr1;
ptr1[1] = (unsigned int) ptr1;
ptr2[1] = (unsigned int) ptr2;
printf ("**Printing referenced minor heap offsets\n");
printf ("\t%d\t%d\n", MIN_OFFS (ptr1[0]), MIN_OFFS (ptr1[1]));
printf ("\t%d\t%d\n", MIN_OFFS (ptr2[0]), MIN_OFFS (ptr2[1]));
gc_add_single_ref (&ptr1);
mark_minor ();
copy_minor_heap ();
printf ("**Printing referenced major heap offsets\n");
printf ("\t%d\t%d\n", MAJ_OFFS (ptr1[0]), MAJ_OFFS (ptr1[1]));
printf ("\t%d\t%d\n", MAJ_OFFS (ptr2[0]), MAJ_OFFS (ptr2[1]));
gc_print_major ();
//gc_print_backpatch();
gc_reset ();
END_TEST ()
// Test for exceeding minor collection (linked list)
BEGIN_TEST (12) gc_reset ();
int
i;
unsigned int *
prev;
unsigned int *
beg_list = 0;
for (i = 0; i < GC_MINOR_CHUNKS; i++)
  {
    unsigned int *ptr = minor_alloc (4);
    if (i >= GC_MINOR_CHUNKS - 4)
      {
	prev[0] = (unsigned int) ptr;
	if (beg_list == 0)
	  {
	    beg_list = prev;
	    gc_add_single_ref (&beg_list);
	  }
      }
    prev = ptr;
  }

minor_alloc (4);
gc_print_minor ();
gc_print_major ();
printf ("\t%d\n", MAJ_OFFS (beg_list));
END_TEST ()
// Simple test for darkening roots
BEGIN_TEST (13) gc_reset ();
ADD_REF (ptr1, major_alloc (100));
major_alloc (100);
ADD_REF (ptr2, major_alloc (100));
darken_roots ();
gc_print_major ();
END_TEST ()BEGIN_TEST (14) gc_reset ();
ADD_REF (ptr1, major_alloc (100));
unsigned int *ptr2 = major_alloc (100);
unsigned int *ptr3 = major_alloc (100);
unsigned int *ptr4 = major_alloc (100);
major_alloc (100);
major_alloc (100);
ptr1[0] = (unsigned int) ptr2;
ptr2[0] = (unsigned int) ptr3;
ptr3[0] = (unsigned int) ptr4;
darken_roots ();
darken_major ();
gc_print_major ();
END_TEST ()
int main ()
{
  printf ("%p\n", &gc_minor_heap[0]);
  test_01 ();
  test_02 ();
  test_03 ();
  test_04 ();
  test_05 ();
  test_06 ();
  test_07 ();
  test_08 ();
  test_09 ();
  test_10 ();
  test_11 ();
  test_12 ();
  test_13 ();
  test_14 ();
  return 0;
}
