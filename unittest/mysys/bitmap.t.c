
#include <tap.h>

#include <my_global.h>
#include "my_bitmap.h"

#include <string.h>

static void bitmap_print(MY_BITMAP *map)
{
  uint32 *to= map->bitmap, *end= map->last_word_ptr;
  while (to <= end)
  {
    fprintf(stderr,"0x%x ", *to++);
  }
  fprintf(stderr,"\n");    
}

uint get_rand_bit(uint bitsize)
{
  return (rand() % bitsize);
}

bool test_set_get_clear_bit(MY_BITMAP *map, uint bitsize)
{
  uint i, test_bit;
  uint no_loops= bitsize > 128 ? 128 : bitsize;
  for (i=0; i < no_loops; i++)
  {
    test_bit= get_rand_bit(bitsize);
    bitmap_set_bit(map, test_bit);
    if (!bitmap_is_set(map, test_bit))
      goto error1;
    bitmap_clear_bit(map, test_bit);
    if (bitmap_is_set(map, test_bit))
      goto error2;
  }
  return FALSE;
error1:
  printf("Error in set bit, bit %u, bitsize = %u", test_bit, bitsize);
  return TRUE;
error2:
  printf("Error in clear bit, bit %u, bitsize = %u", test_bit, bitsize);
  return TRUE;
}

bool test_flip_bit(MY_BITMAP *map, uint bitsize)
{
  uint i, test_bit;
  uint no_loops= bitsize > 128 ? 128 : bitsize;
  for (i=0; i < no_loops; i++)
  {
    test_bit= get_rand_bit(bitsize);
    bitmap_flip_bit(map, test_bit);
    if (!bitmap_is_set(map, test_bit))
      goto error1;
    bitmap_flip_bit(map, test_bit);
    if (bitmap_is_set(map, test_bit))
      goto error2;
  }
  return FALSE;
error1:
  printf("Error in flip bit 1, bit %u, bitsize = %u", test_bit, bitsize);
  return TRUE;
error2:
  printf("Error in flip bit 2, bit %u, bitsize = %u", test_bit, bitsize);
  return TRUE;
}

bool test_operators(MY_BITMAP *map, uint bitsize)
{
  return FALSE;
}

bool test_get_all_bits(MY_BITMAP *map, uint bitsize)
{
  uint i;
  bitmap_set_all(map);
  if (!bitmap_is_set_all(map))
    goto error1;
  if (!bitmap_is_prefix(map, bitsize))
    goto error5;
  bitmap_clear_all(map);
  if (!bitmap_is_clear_all(map))
    goto error2;
  if (!bitmap_is_prefix(map, 0))
    goto error6;
  for (i=0; i<bitsize;i++)
    bitmap_set_bit(map, i);
  if (!bitmap_is_set_all(map))
    goto error3;
  for (i=0; i<bitsize;i++)
    bitmap_clear_bit(map, i);
  if (!bitmap_is_clear_all(map))
    goto error4;
  return FALSE;
error1:
  diag("Error in set_all, bitsize = %u", bitsize);
  return TRUE;
error2:
  diag("Error in clear_all, bitsize = %u", bitsize);
  return TRUE;
error3:
  diag("Error in bitmap_is_set_all, bitsize = %u", bitsize);
  return TRUE;
error4:
  diag("Error in bitmap_is_clear_all, bitsize = %u", bitsize);
  return TRUE;
error5:
  diag("Error in set_all through set_prefix, bitsize = %u", bitsize);
  return TRUE;
error6:
  diag("Error in clear_all through set_prefix, bitsize = %u", bitsize);
  return TRUE;
}

bool test_compare_operators(MY_BITMAP *map, uint bitsize)
{
  uint i, j, test_bit1, test_bit2, test_bit3,test_bit4;
  uint no_loops= bitsize > 128 ? 128 : bitsize;
  MY_BITMAP map2_obj, map3_obj;
  MY_BITMAP *map2= &map2_obj, *map3= &map3_obj;
  uint32 map2buf[1024];
  uint32 map3buf[1024];
  bitmap_init(&map2_obj, map2buf, bitsize, FALSE);
  bitmap_init(&map3_obj, map3buf, bitsize, FALSE);
  bitmap_clear_all(map2);
  bitmap_clear_all(map3);
  for (i=0; i < no_loops; i++)
  {
    test_bit1=get_rand_bit(bitsize);
    bitmap_set_prefix(map, test_bit1);
    test_bit2=get_rand_bit(bitsize);
    bitmap_set_prefix(map2, test_bit2);
    bitmap_intersect(map, map2);
    test_bit3= test_bit2 < test_bit1 ? test_bit2 : test_bit1;
    bitmap_set_prefix(map3, test_bit3);
    if (!bitmap_cmp(map, map3))
      goto error1;
    bitmap_clear_all(map);
    bitmap_clear_all(map2);
    bitmap_clear_all(map3);
    test_bit1=get_rand_bit(bitsize);
    test_bit2=get_rand_bit(bitsize);
    test_bit3=get_rand_bit(bitsize);
    bitmap_set_prefix(map, test_bit1);
    bitmap_set_prefix(map2, test_bit2);
    test_bit3= test_bit2 > test_bit1 ? test_bit2 : test_bit1;
    bitmap_set_prefix(map3, test_bit3);
    bitmap_union(map, map2);
    if (!bitmap_cmp(map, map3))
      goto error2;
    bitmap_clear_all(map);
    bitmap_clear_all(map2);
    bitmap_clear_all(map3);
    test_bit1=get_rand_bit(bitsize);
    test_bit2=get_rand_bit(bitsize);
    test_bit3=get_rand_bit(bitsize);
    bitmap_set_prefix(map, test_bit1);
    bitmap_set_prefix(map2, test_bit2);
    bitmap_xor(map, map2);
    test_bit3= test_bit2 > test_bit1 ? test_bit2 : test_bit1;
    test_bit4= test_bit2 < test_bit1 ? test_bit2 : test_bit1;
    bitmap_set_prefix(map3, test_bit3);
    for (j=0; j < test_bit4; j++)
      bitmap_clear_bit(map3, j);
    if (!bitmap_cmp(map, map3))
      goto error3;
    bitmap_clear_all(map);
    bitmap_clear_all(map2);
    bitmap_clear_all(map3);
    test_bit1=get_rand_bit(bitsize);
    test_bit2=get_rand_bit(bitsize);
    test_bit3=get_rand_bit(bitsize);
    bitmap_set_prefix(map, test_bit1);
    bitmap_set_prefix(map2, test_bit2);
    bitmap_subtract(map, map2);
    if (test_bit2 < test_bit1)
    {
      bitmap_set_prefix(map3, test_bit1);
      for (j=0; j < test_bit2; j++)
        bitmap_clear_bit(map3, j);
    }
    if (!bitmap_cmp(map, map3))
      goto error4;
    bitmap_clear_all(map);
    bitmap_clear_all(map2);
    bitmap_clear_all(map3);
    test_bit1=get_rand_bit(bitsize);
    bitmap_set_prefix(map, test_bit1);
    bitmap_invert(map);
    bitmap_set_all(map3);
    for (j=0; j < test_bit1; j++)
      bitmap_clear_bit(map3, j);
    if (!bitmap_cmp(map, map3))
      goto error5;
    bitmap_clear_all(map);
    bitmap_clear_all(map3);
  }
  return FALSE;
error1:
  diag("intersect error  bitsize=%u,size1=%u,size2=%u", bitsize,
  test_bit1,test_bit2);
  return TRUE;
error2:
  diag("union error  bitsize=%u,size1=%u,size2=%u", bitsize,
  test_bit1,test_bit2);
  return TRUE;
error3:
  diag("xor error  bitsize=%u,size1=%u,size2=%u", bitsize,
  test_bit1,test_bit2);
  return TRUE;
error4:
  diag("subtract error  bitsize=%u,size1=%u,size2=%u", bitsize,
  test_bit1,test_bit2);
  return TRUE;
error5:
  diag("invert error  bitsize=%u,size=%u", bitsize,
  test_bit1);
  return TRUE;
}

bool test_count_bits_set(MY_BITMAP *map, uint bitsize)
{
  uint i, bit_count=0, test_bit;
  uint no_loops= bitsize > 128 ? 128 : bitsize;
  for (i=0; i < no_loops; i++)
  {
    test_bit=get_rand_bit(bitsize);
    if (!bitmap_is_set(map, test_bit))
    {
      bitmap_set_bit(map, test_bit);
      bit_count++;
    }
  }
  if (bit_count==0 && bitsize > 0)
    goto error1;
  if (bitmap_bits_set(map) != bit_count)
    goto error2;
  return FALSE;
error1:
  diag("No bits set  bitsize = %u", bitsize);
  return TRUE;
error2:
  diag("Wrong count of bits set, bitsize = %u", bitsize);
  return TRUE;
}

bool test_get_first_bit(MY_BITMAP *map, uint bitsize)
{
  uint i, j, test_bit;
  uint no_loops= bitsize > 128 ? 128 : bitsize;
  for (i=0; i < no_loops; i++)
  {
    test_bit=get_rand_bit(bitsize);
    bitmap_set_bit(map, test_bit);
    if (bitmap_get_first_set(map) != test_bit)
      goto error1;
    bitmap_set_all(map);
    bitmap_clear_bit(map, test_bit);
    if (bitmap_get_first(map) != test_bit)
      goto error2;
    bitmap_clear_all(map);
  }
  return FALSE;
error1:
  diag("get_first_set error bitsize=%u,prefix_size=%u",bitsize,test_bit);
  return TRUE;
error2:
  diag("get_first error bitsize= %u, prefix_size= %u",bitsize,test_bit);
  return TRUE;
}

bool test_get_next_bit(MY_BITMAP *map, uint bitsize)
{
  uint i, j, test_bit;
  uint no_loops= bitsize > 128 ? 128 : bitsize;
  for (i=0; i < no_loops; i++)
  {
    test_bit=get_rand_bit(bitsize);
    for (j=0; j < test_bit; j++)
      bitmap_set_next(map);
    if (!bitmap_is_prefix(map, test_bit))
      goto error1;
    bitmap_clear_all(map);
  }
  return FALSE;
error1:
  diag("get_next error  bitsize= %u, prefix_size= %u", bitsize,test_bit);
  return TRUE;
}

bool test_prefix(MY_BITMAP *map, uint bitsize)
{
  uint i, j, test_bit;
  uint no_loops= bitsize > 128 ? 128 : bitsize;
  for (i=0; i < no_loops; i++)
  {
    test_bit=get_rand_bit(bitsize);
    bitmap_set_prefix(map, test_bit);
    if (!bitmap_is_prefix(map, test_bit))
      goto error1;
    bitmap_clear_all(map);
    for (j=0; j < test_bit; j++)
      bitmap_set_bit(map, j);
    if (!bitmap_is_prefix(map, test_bit))
      goto error2;
    bitmap_set_all(map);
    for (j=bitsize - 1; ~(j-test_bit); j--)
      bitmap_clear_bit(map, j);
    if (!bitmap_is_prefix(map, test_bit))
      goto error3;
    bitmap_clear_all(map);
  }
  return FALSE;
error1:
  diag("prefix1 error  bitsize = %u, prefix_size = %u", bitsize,test_bit);
  return TRUE;
error2:
  diag("prefix2 error  bitsize = %u, prefix_size = %u", bitsize,test_bit);
  return TRUE;
error3:
  diag("prefix3 error  bitsize = %u, prefix_size = %u", bitsize,test_bit);
  return TRUE;
}


bool do_test(uint bitsize)
{
  MY_BITMAP map;
  uint32 buf[1024];
  if (bitmap_init(&map, buf, bitsize, FALSE))
  {
    diag("init error for bitsize %d", bitsize);
    goto error;
  }
  if (test_set_get_clear_bit(&map,bitsize))
    goto error;
  bitmap_clear_all(&map);
  if (test_flip_bit(&map,bitsize))
    goto error;
  bitmap_clear_all(&map);
  if (test_operators(&map,bitsize))
    goto error;
  bitmap_clear_all(&map);
  if (test_get_all_bits(&map, bitsize))
    goto error;
  bitmap_clear_all(&map);
  if (test_compare_operators(&map,bitsize))
    goto error;
  bitmap_clear_all(&map);
  if (test_count_bits_set(&map,bitsize))
    goto error;
  bitmap_clear_all(&map);
  if (test_get_first_bit(&map,bitsize))
    goto error;
  bitmap_clear_all(&map);
  if (test_get_next_bit(&map,bitsize))
    goto error;
  if (test_prefix(&map,bitsize))
    goto error;
  return FALSE;
error:
  return TRUE;
}

int main()
{
  int i;
  plan(4095);
  for (i= 1; i < 4096; i++)
    ok(do_test(i) == 0, "bitmap size %d", i);
  return exit_status();
}
