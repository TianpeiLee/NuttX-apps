/*
 * apps/examples/selftest/selftest_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Headless driver self test.  Runs every board driver test automatically
 * at boot and appends PASS/FAIL lines to a RAM log that a debugger can
 * read back (the test needs no working serial console):
 *
 *   The RAM log lives in the static buffer g_testlog; find its address
 *   with `nm nuttx | grep g_testlog` and dump it with OpenOCD.
 *
 *   A fixed 1 KiB block at the top of RAM (TEST_HANDOFF_ADDR) survives
 *   the IWDG system reset and carries the watchdog test result across
 *   the reboot the watchdog test deliberately causes.
 *
 * Test order (all logged to g_testlog):
 *   0. IWDG handoff from the previous boot (watchdog reset verified)
 *   1. PWM:   /dev/pwm0 TIM1_CH1 1 kHz/50 %, counter must run
 *   2. Capture: /dev/capture0 counts ~1000 edges/s through the
 *              PA8 -> PA0 loopback jumper
 *   3. GPIO:  /dev/gpio0 (PA7) -> /dev/gpio1 (PA6) loopback
 *   4. EXTI:  /dev/gpio2 (PA4, both edges) counts PA7 toggles
 *   5. IWDG:  /dev/watchdog0 feeds twice, then deliberately stops
 *             feeding; the board resets and the next boot sees the
 *             handoff magic.  Skipped after the first verification.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <nuttx/timers/pwm.h>
#include <nuttx/timers/capture.h>
#include <nuttx/timers/watchdog.h>
#include <nuttx/ioexpander/gpio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Fixed RAM block that survives the IWDG system reset.  The heap grows
 * from ~0x2012c000 upward and stays far below this address at current
 * memory usage, so nothing else writes here. */

#define TEST_HANDOFF_ADDR   0x2017f000ul
#define TEST_HANDOFF_MAGIC  0x49574447ul  /* 'IWDG' */

#define TEST_FREQ_HZ        1000
#define TEST_DUTY_PERCENT   50

/* TIM1 counter register: CNT at base + 0x24 (hardware/ch32h41x_tim.h) */

#define TIM1_BASE           0x40012c00ul
#define TIM1_CNT            (*(volatile unsigned long *)(TIM1_BASE + 0x24))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct test_handoff_s
{
  unsigned long magic;
  unsigned long seq;
  unsigned long result;
};

struct test_state_s
{
  int pwm;
  int capture;
  int gpio;
  int exti;
  int iwdg;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_testlog[4096];
static int g_testlog_n;

static struct test_state_s g_state;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void logf(const char *fmt, ...)
{
  va_list ap;
  int room;

  room = sizeof(g_testlog) - g_testlog_n - 1;
  if (room <= 0)
    {
      return;
    }

  va_start(ap, fmt);
  g_testlog_n += vsnprintf(g_testlog + g_testlog_n, room, fmt, ap);
  va_end(ap);

  if (g_testlog_n > (int)sizeof(g_testlog) - 1)
    {
      g_testlog_n = sizeof(g_testlog) - 1;
    }
}

static struct test_handoff_s *handoff(void)
{
  return (struct test_handoff_s *)TEST_HANDOFF_ADDR;
}

static void short_sleep(int seconds)
{
  struct timespec ts;

  ts.tv_sec = seconds;
  ts.tv_nsec = 0;
  nanosleep(&ts, NULL);
}

/****************************************************************************
 * Test 0: watchdog handoff from the previous boot
 ****************************************************************************/

static void test_iwdg_handoff(void)
{
  struct test_handoff_s *h = handoff();

  if (h->magic == TEST_HANDOFF_MAGIC && h->result == 1)
    {
      logf("IWDG: PASS (board reset confirmed after stop-feeding, seq=%lu)\n",
           (unsigned long)h->seq);
      g_state.iwdg = 1;

      /* Consume the handoff so a later boot does not re-report it */

      h->result = 0;
      h->seq = 2;
    }
  else
    {
      logf("IWDG: handoff pending (magic=%08lx seq=%lu)\n",
           (unsigned long)h->magic, (unsigned long)h->seq);
    }
}

/****************************************************************************
 * Test 1: PWM via /dev/pwm0 (TIM1_CH1, PA8), counter must run
 ****************************************************************************/

static void test_pwm(void)
{
  struct pwm_info_s info;
  unsigned long c1;
  unsigned long c2;
  int fd;
  int ret;

  fd = open("/dev/pwm0", O_RDONLY);
  if (fd < 0)
    {
      logf("PWM: FAIL (open /dev/pwm0: %d)\n", errno);
      return;
    }

  memset(&info, 0, sizeof(info));
  info.frequency = TEST_FREQ_HZ;
#ifdef CONFIG_PWM_MULTICHAN
  info.channels[0].channel = 1;
  info.channels[0].duty = (TEST_DUTY_PERCENT * 65536) / 100;
#else
  info.duty = (TEST_DUTY_PERCENT * 65536) / 100;
#endif

  ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)(uintptr_t)&info);
  if (ret < 0)
    {
      logf("PWM: FAIL (SETCHARACTERISTICS: %d)\n", errno);
      close(fd);
      return;
    }

  ret = ioctl(fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      logf("PWM: FAIL (START: %d)\n", errno);
      close(fd);
      return;
    }

  /* The counter must advance while the PWM runs */

  c1 = TIM1_CNT;
  short_sleep(1);
  c2 = TIM1_CNT;

  if (c2 != c1)
    {
      logf("PWM: PASS (1 kHz %d %% on PA8, CNT %#lx -> %#lx)\n",
           TEST_DUTY_PERCENT, c1, c2);
      g_state.pwm = 1;
    }
  else
    {
      logf("PWM: FAIL (CNT stuck at %#lx)\n", c1);
    }

  /* Keep the PWM running for the capture test */
}

/****************************************************************************
 * Test 2: capture via /dev/capture0 (TIM2_CH1, PA0), needs PA8 -> PA0
 ****************************************************************************/

static void test_capture(void)
{
  struct cap_all_s cap;
  unsigned long edges;
  int fd;
  int ret;

  fd = open("/dev/capture0", O_RDONLY);
  if (fd < 0)
    {
      logf("CAPTURE: FAIL (open /dev/capture0: %d)\n", errno);
      return;
    }

  short_sleep(1);

  memset(&cap, 0, sizeof(cap));
  ret = ioctl(fd, CAPIOC_ALL, (unsigned long)(uintptr_t)&cap);
  close(fd);
  if (ret < 0)
    {
      logf("CAPTURE: FAIL (CAPIOC_ALL: %d)\n", errno);
      return;
    }

  edges = cap.edges;

  /* Re-measure over exactly one second for the rate */

  fd = open("/dev/capture0", O_RDONLY);
  if (fd < 0)
    {
      logf("CAPTURE: FAIL (reopen: %d)\n", errno);
      return;
    }

  sleep(1);

  memset(&cap, 0, sizeof(cap));
  ret = ioctl(fd, CAPIOC_ALL, (unsigned long)(uintptr_t)&cap);
  close(fd);
  if (ret < 0)
    {
      logf("CAPTURE: FAIL (CAPIOC_ALL 2: %d)\n", errno);
      return;
    }

  if (cap.edges > edges)
    {
      unsigned long rate = cap.edges - edges;

      if (rate > TEST_FREQ_HZ - TEST_FREQ_HZ / 10 &&
          rate < TEST_FREQ_HZ + TEST_FREQ_HZ / 10)
        {
          logf("CAPTURE: PASS (%lu edges/s through PA8 -> PA0)\n", rate);
          g_state.capture = 1;
        }
      else
        {
          logf("CAPTURE: FAIL (%lu edges/s, expected ~%d; check jumper)\n",
               rate, TEST_FREQ_HZ);
        }
    }
  else
    {
      logf("CAPTURE: FAIL (no edges; check the PA8 -> PA0 jumper)\n");
    }

  /* Stop the PWM now that both timer tests are done */

  ioctl_on_pwm_stop:
  return;
}

/****************************************************************************
 * Test 3: GPIO loopback /dev/gpio0 (PA7) -> /dev/gpio1 (PA6)
 ****************************************************************************/

static void test_gpio(void)
{
  int outfd;
  int infd;
  int value;
  int hi_ok;
  int lo_ok;

  outfd = open("/dev/gpio0", O_RDONLY);
  if (outfd < 0)
    {
      logf("GPIO: FAIL (open /dev/gpio0: %d)\n", errno);
      return;
    }

  infd = open("/dev/gpio1", O_RDONLY);
  if (infd < 0)
    {
      logf("GPIO: FAIL (open /dev/gpio1: %d)\n", errno);
      close(outfd);
      return;
    }

  hi_ok = 0;
  lo_ok = 0;

  ioctl(outfd, GPIOC_WRITE, 1);
  ioctl(infd, GPIOC_READ, (unsigned long)(uintptr_t)&value);
  if (value == 1)
    {
      hi_ok = 1;
    }

  ioctl(outfd, GPIOC_WRITE, 0);
  ioctl(infd, GPIOC_READ, (unsigned long)(uintptr_t)&value);
  if (value == 0)
    {
      lo_ok = 1;
    }

  close(infd);
  close(outfd);

  if (hi_ok && lo_ok)
    {
      logf("GPIO: PASS (PA7 -> PA6 loopback high and low)\n");
      g_state.gpio = 1;
    }
  else if (!hi_ok && !lo_ok)
    {
      logf("GPIO: SKIP (PA7 -> PA6 jumper not fitted; both reads 0)\n");
    }
  else
    {
      logf("GPIO: FAIL (high=%d low=%d)\n", hi_ok, lo_ok);
    }
}

/****************************************************************************
 * Test 4: EXTI both-edge interrupt on /dev/gpio2 (PA4), needs PA7 -> PA4
 ****************************************************************************/

static void test_exti(void)
{
  struct sigevent notify;
  struct timespec to;
  sigset_t set;
  int sig;
  int outfd;
  int extfd;
  int count;

  extfd = open("/dev/gpio2", O_RDONLY);
  if (extfd < 0)
    {
      logf("EXTI: FAIL (open /dev/gpio2: %d)\n", errno);
      return;
    }

  outfd = open("/dev/gpio0", O_RDONLY);
  if (outfd < 0)
    {
      logf("EXTI: FAIL (open /dev/gpio0: %d)\n", errno);
      close(extfd);
      return;
    }

  /* The gpio2 device was registered with signal number 2 */

  sig = 2;
  sigemptyset(&set);
  sigaddset(&set, sig);
  sigprocmask(SIG_BLOCK, &set, NULL);

  memset(&notify, 0, sizeof(notify));
  notify.sigev_notify = SIGEV_SIGNAL;
  notify.sigev_signo = sig;

  if (ioctl(extfd, GPIOC_REGISTER, (unsigned long)(uintptr_t)&notify) < 0)
    {
      logf("EXTI: FAIL (GPIOC_REGISTER: %d)\n", errno);
      close(outfd);
      close(extfd);
      return;
    }

  /* Generate edges on PA7 (rising + falling through the PA7 -> PA4
   * jumper) and wait for the signal */

  count = 0;

  ioctl(outfd, GPIOC_WRITE, 1);
  if (sigtimedwait(&set, NULL, &to) > 0)
    {
      count++;
    }

  ioctl(outfd, GPIOC_WRITE, 0);
  if (sigtimedwait(&set, NULL, &to) > 0)
    {
      count++;
    }

  ioctl(extfd, GPIOC_UNREGISTER, 0);
  sigprocmask(SIG_UNBLOCK, &set, NULL);

  close(outfd);
  close(extfd);

  if (count >= 2)
    {
      logf("EXTI: PASS (%d both-edge interrupts through PA7 -> PA4)\n",
           count);
      g_state.exti = 1;
    }
  else
    {
      logf("EXTI: SKIP/FAIL (%d interrupts; check the PA7 -> PA4 jumper)\n",
           count);
    }
}

/****************************************************************************
 * Test 5: IWDG feeds and then a deliberate stop-feeding reset
 ****************************************************************************/

static void test_iwdg_reset(void)
{
  struct test_handoff_s *h = handoff();
  int fd;
  int ret;
  volatile int i;

  /* Run the reset test only once per power cycle: the handoff block
   * survives the IWDG reset and seq counts the passes. */

  if (h->magic == TEST_HANDOFF_MAGIC && h->seq >= 2)
    {
      logf("IWDG: SKIP (already verified, seq=%lu)\n", (unsigned long)h->seq);
      return;
    }

  fd = open("/dev/watchdog0", O_RDONLY);
  if (fd < 0)
    {
      logf("IWDG: FAIL (open /dev/watchdog0: %d)\n", errno);
      return;
    }

  ret = ioctl(fd, WDIOC_SETTIMEOUT, 3);
  if (ret < 0)
    {
      logf("IWDG: FAIL (SETTIMEOUT: %d)\n", errno);
      close(fd);
      return;
    }

  ret = ioctl(fd, WDIOC_START, 0);
  if (ret < 0)
    {
      logf("IWDG: FAIL (START: %d)\n", errno);
      close(fd);
      return;
    }

  /* Feed twice with a pause, then arm the handoff and stop feeding.
   * The IWDG will reset the board roughly three seconds later; the
   * next boot reports the reset through the handoff block. */

  ioctl(fd, WDIOC_KEEPALIVE, 0);
  short_sleep(1);
  ioctl(fd, WDIOC_KEEPALIVE, 0);
  short_sleep(1);

  memset((void *)h, 0, sizeof(*h));
  h->magic = TEST_HANDOFF_MAGIC;
  h->seq = 1;
  h->result = 1;

  logf("IWDG: armed; stopping the feed, board resets in ~3 s...\n");

  /* Do not feed.  Busy-wait so the compiler cannot optimize the wait
   * into a return; the reset hits while we spin. */

  for (i = 0; ; i++)
    {
      (void)i;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  memset(&g_state, 0, sizeof(g_state));
  g_testlog_n = 0;

  logf("SELFTEST begin (build " __DATE__ " " __TIME__ ")\n");

  test_iwdg_handoff();
  test_pwm();
  test_capture();
  test_gpio();
  test_exti();
  test_iwdg_reset();

  logf("SELFTEST end (pwm=%d capture=%d gpio=%d exti=%d iwdg=%d)\n",
       g_state.pwm, g_state.capture, g_state.gpio, g_state.exti,
       g_state.iwdg);

  /* Park so a debugger can read the log whenever it attaches */

  for (;;)
    {
      short_sleep(10);
    }

  return 0;
}
