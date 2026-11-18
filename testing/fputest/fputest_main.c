/****************************************************************************/
/* apps/testing/fputest/fputest_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * FPU verification for the QingKe V5F (RV32IMAFC, single precision):
 *
 *   1. Single-thread arithmetic checks - volatile inputs defeat constant
 *      folding; every result is compared bit-exact against its IEEE 754
 *      expected value.
 *   2. Concurrent FP context-switch integrity - CONFIG_TESTING_FPUTEST_
 *      THREADS threads each keep an independent set of FP accumulators
 *      and add exact constants to them in a long loop.  All values stay
 *      below 2^24, so every sum is bit-exact in float32: any FPU state
 *      corruption across a context switch shows up as a wrong value.
 *
 * Output: one line per check group plus a final "FPUTEST: PASS" or
 * "FPUTEST: <n> FAILURES".  The builtin exits with the failure count so
 * it can be scripted.
 ****************************************************************************/

/****************************************************************************/
/* Included Files
/****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

/****************************************************************************/
/* Pre-processor Definitions
/****************************************************************************/

#define NACC      8
#define CHECK_EVERY 50000u

/****************************************************************************/
/* Private Types
/****************************************************************************/

struct fputest_thread_s
{
  int  id;
  int  failures;
};

/****************************************************************************/
/* Private Data
/****************************************************************************/

static unsigned g_iterations;
static volatile int g_threads_done;

/****************************************************************************/
/* Private Functions
/****************************************************************************/

/* Single-thread arithmetic checks.  volatile keeps the compiler from
 * constant folding; expectations are exact IEEE 754 values. */

static int fputest_basic(void)
{
  volatile float a = 1.5f;
  volatile float b = 2.25f;
  volatile float c = -3.0f;
  volatile double da = 1.5;
  volatile double db = 2.25;
  int fails = 0;

  if (a + b != 3.75f)                       /* add */
    {
      fails++;
    }

  if (a * b != 3.375f)                      /* mul */
    {
      fails++;
    }

  if (b / a != 1.5f)                        /* div */
    {
      fails++;
    }

  if (a - b != -0.75f)                      /* sub + exact negative */
    {
      fails++;
    }

  if (-c != 3.0f)                           /* neg */
    {
      fails++;
    }

  if ((a + b) * c != -11.25f)               /* compound */
    {
      fails++;
    }

  if ((float)(int)(a * b * 4.0f) != 13.0f)  /* f2i truncation: 13.5 -> 13 */
    {
      fails++;
    }

  if ((int)(a * -b * 4.0f) != -13)          /* negative f2i: -13.5 -> -13 */
    {
      fails++;
    }

  if ((double)(a + b) != 3.75)              /* f2d exact widening */
    {
      fails++;
    }

  if (da * db != 3.375)                     /* soft-float double mul */
    {
      fails++;
    }

  if (da / db != 0.6666666666666666)        /* soft-float double div */
    {
      fails++;
    }

  if (!(a < b) || !(c < a) || !(a > 0.0f))  /* comparisons incl. sign */
    {
      fails++;
    }

  printf("fputest: basic arithmetic ... %s\n",
         fails == 0 ? "PASS" : "FAIL");
  return fails;
}

/* FP context-switch integrity: each thread owns NACC accumulators and
 * adds a thread-specific exact constant to each of them per iteration.
 * Every intermediate value is an integer multiple of 0.5 below 2^24, so
 * all sums are exact in float32 and bit-exact against the expectation. */

static FAR void *fputest_thread(FAR void *arg)
{
  FAR struct fputest_thread_s *self = (FAR struct fputest_thread_s *)arg;
  float acc[NACC];
  float add[NACC];
  unsigned i;
  int j;

  for (j = 0; j < NACC; j++)
    {
      acc[j] = 0.0f;
      add[j] = 0.5f * (float)(self->id * NACC + j + 1);
    }

  for (i = 0; i < g_iterations; i++)
    {
      for (j = 0; j < NACC; j++)
        {
          acc[j] += add[j];
        }

      /* Periodically validate against the exact running expectation:
       * the expected value of acc[j] is add[j] * (i + 1). */

      if ((i + 1) % CHECK_EVERY == 0)
        {
          for (j = 0; j < NACC; j++)
            {
              float want = add[j] * (float)(i + 1);

              if (acc[j] != want)
                {
                  self->failures++;
                  printf("fputest: thread %d iter %u acc %d: "
                         "got 0x%08lx want 0x%08lx\n",
                         self->id, i + 1, j,
                         (unsigned long)*(volatile int *)&acc[j],
                         (unsigned long)*(volatile int *)&want);
                }
            }
        }
    }

  for (j = 0; j < NACC; j++)
    {
      float want = add[j] * (float)g_iterations;

      if (acc[j] != want)
        {
          self->failures++;
          printf("fputest: thread %d final acc %d: "
                 "got 0x%08lx want 0x%08lx\n",
                 self->id, j,
                 (unsigned long)*(volatile int *)&acc[j],
                 (unsigned long)*(volatile int *)&want);
        }
    }

  printf("fputest: thread %d (8 FP accumulators, %u iters) ... %s\n",
         self->id, g_iterations, self->failures == 0 ? "PASS" : "FAIL");

  g_threads_done++;
  return NULL;
}

/****************************************************************************/
/* Public Functions
/****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct fputest_thread_s threads[CONFIG_TESTING_FPUTEST_THREADS];
  pthread_attr_t attr;
  pthread_t tid[CONFIG_TESTING_FPUTEST_THREADS];
  int nthreads = CONFIG_TESTING_FPUTEST_THREADS;
  int failures = 0;
  int i;
  int ret;

  g_iterations = CONFIG_TESTING_FPUTEST_ITERATIONS;

  printf("fputest: RV32IMAFC single precision FPU verification\n");

  failures += fputest_basic();

  /* Concurrent FP context-switch stress */

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      printf("fputest: pthread_attr_init failed: %d\n", ret);
      return failures + 1;
    }

  ret = pthread_attr_setstacksize(&attr, 4096);
  if (ret != 0)
    {
      printf("fputest: pthread_attr_setstacksize failed: %d\n", ret);
      return failures + 1;
    }

  for (i = 0; i < nthreads; i++)
    {
      threads[i].id = i;
      threads[i].failures = 0;

      ret = pthread_create(&tid[i], &attr, fputest_thread,
                           (FAR void *)&threads[i]);
      if (ret != 0)
        {
          printf("fputest: thread %d create failed: %d\n", i, ret);
          nthreads = i;
          failures++;
          break;
        }
    }

  for (i = 0; i < nthreads; i++)
    {
      FAR void *value;

      pthread_join(tid[i], &value);
      failures += threads[i].failures;
    }

  pthread_attr_destroy(&attr);

  printf("fputest: %d threads x %u iterations done\n",
         nthreads, g_iterations);
  printf("FPUTEST: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures;
}
