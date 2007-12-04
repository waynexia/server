/* Copyright (C) 2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <tap.h>

#include <my_global.h>
#include <my_sys.h>
#include <my_atomic.h>
#include <lf.h>
#include <m_string.h>
#include "../trnman.h"

pthread_mutex_t rt_mutex;
pthread_attr_t attr;
size_t stacksize= 0;
#define STACK_SIZE (((int)stacksize-2048)*STACK_DIRECTION)

int rt_num_threads;
int litmus;

/*
  create and end (commit or rollback) transactions randomly
*/
#define MAX_ITER 100
pthread_handler_t test_trnman(void *arg)
{
  uint   x, y, i, n;
  TRN    *trn[MAX_ITER];
  pthread_mutex_t mutexes[MAX_ITER];
  pthread_cond_t conds[MAX_ITER];
  int    m= (*(int *)arg);

  for (i= 0; i < MAX_ITER; i++)
  {
    pthread_mutex_init(&mutexes[i], MY_MUTEX_INIT_FAST);
    pthread_cond_init(&conds[i], 0);
  }

  for (x= ((int)(intptr)(&m)); m > 0; )
  {
    y= x= (x*LL(3628273133) + LL(1500450271)) % LL(9576890767); /* three prime numbers */
    m-= n= x % MAX_ITER;
    for (i= 0; i < n; i++)
    {
      trn[i]= trnman_new_trn(&mutexes[i], &conds[i], &m + STACK_SIZE);
      if (!trn[i])
      {
        diag("trnman_new_trn() failed");
        litmus++;
      }
    }
    for (i= 0; i < n; i++)
    {
      y= (y*19 + 7) % 31;
      trnman_end_trn(trn[i], y & 1);
    }
  }
  for (i= 0; i < MAX_ITER; i++)
  {
    pthread_mutex_destroy(&mutexes[i]);
    pthread_cond_destroy(&conds[i]);
  }
  pthread_mutex_lock(&rt_mutex);
  rt_num_threads--;
  pthread_mutex_unlock(&rt_mutex);

  return 0;
}
#undef MAX_ITER

void run_test(const char *test, pthread_handler handler, int n, int m)
{
  pthread_t *threads;
  ulonglong now= my_getsystime();
  int i;

  litmus= 0;

  threads= (pthread_t *)my_malloc(sizeof(void *)*n, MYF(0));
  if (!threads)
  {
    diag("Out of memory");
    abort();
  }

  diag("Testing %s with %d threads, %d iterations... ", test, n, m);
  rt_num_threads= n;
  for (i= 0; i < n ; i++)
    if (pthread_create(threads+i, &attr, handler, &m))
    {
      diag("Could not create thread");
      abort();
    }
  for (i= 0 ; i < n ; i++)
    pthread_join(threads[i], 0);
  now= my_getsystime()-now;
  ok(litmus == 0, "Tested %s in %g secs (%d)", test, ((double)now)/1e7, litmus);
  my_free((void*)threads, MYF(0));
}

#define ok_read_from(T1, T2, RES)                       \
  i= trnman_can_read_from(trn[T1], trn[T2]->trid);       \
  ok(i == RES, "trn" #T1 " %s read from trn" #T2, i ? "can" : "cannot")
#define start_transaction(T)                            \
  trn[T]= trnman_new_trn(&mutexes[T], &conds[T], &i + STACK_SIZE)
#define commit(T)               trnman_commit_trn(trn[T])
#define abort(T)                trnman_abort_trn(trn[T])

#define Ntrns 4
void test_trnman_read_from()
{
  TRN *trn[Ntrns];
  pthread_mutex_t mutexes[Ntrns];
  pthread_cond_t conds[Ntrns];
  int i;

  for (i= 0; i < Ntrns; i++)
  {
    pthread_mutex_init(&mutexes[i], MY_MUTEX_INIT_FAST);
    pthread_cond_init(&conds[i], 0);
  }

  start_transaction(0);                    /* start trn1 */
  start_transaction(1);                    /* start trn2 */
  ok_read_from(1, 0, 0);
  commit(0);                               /* commit trn1 */
  start_transaction(2);                    /* start trn4 */
  abort(2);                                /* abort trn4 */
  start_transaction(3);                    /* start trn5 */
  ok_read_from(3, 0, 1);
  ok_read_from(3, 1, 0);
  ok_read_from(3, 2, 0);
  commit(1);                               /* commit trn2 */
  ok_read_from(3, 1, 0);
  commit(3);                               /* commit trn5 */

  for (i= 0; i < Ntrns; i++)
  {
    pthread_mutex_destroy(&mutexes[i]);
    pthread_cond_destroy(&conds[i]);
  }
}

int main(int argc __attribute__((unused)), char **argv)
{
  MY_INIT(argv[0]);
  my_init();

  plan(6);

  if (my_atomic_initialize())
    return exit_status();

  pthread_mutex_init(&rt_mutex, 0);
  pthread_attr_init(&attr);
#ifdef HAVE_PTHREAD_ATTR_GETSTACKSIZE
  pthread_attr_getstacksize(&attr, &stacksize);
  if (stacksize == 0)
#endif
    stacksize= PTHREAD_STACK_MIN;

#define CYCLES 10000
#define THREADS 10

  trnman_init(0);

  test_trnman_read_from();
  run_test("trnman", test_trnman, THREADS, CYCLES);

  diag("mallocs: %d", trnman_allocated_transactions);
  {
    ulonglong now= my_getsystime();
    trnman_destroy();
    now= my_getsystime()-now;
    diag("trnman_destroy: %g", ((double)now)/1e7);
  }

  pthread_mutex_destroy(&rt_mutex);
  my_end(0);
  return exit_status();
}

