/*
 * apps/examples/timloop/timloop_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * One-command closed-loop self test for the CH32H417 timer I/O path:
 *
 *   TIM1_CH1 (PA8) runs 1 kHz / 50 % PWM.  The board jumper PA8 -> PA0
 *   feeds that signal into TIM2_CH1, whose capture driver measures the
 *   frequency and duty.  The jumper must be in place before running this
 *   command.
 *
 * Requires: CONFIG_CH32H41X_HAVE_TIM1_PWM and
 *           CONFIG_CH32H41X_HAVE_TIM2_CAPTURE.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <nuttx/timers/pwm.h>
#include <nuttx/timers/capture.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LOOP_FREQ_HZ      1000
#define LOOP_DUTY_PERCENT 50
#define LOOP_SAMPLES      10
#define LOOP_DUTY_TOL     5   /* duty tolerance, percent */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct pwm_info_s info;
  struct cap_all_s cap;
  int pfd;
  int cfd;
  int i;
  int ret;
  int pass = 0;
  unsigned long prev_edges = 0;

  /* Optional duty override: timloop [<duty_percent>] */

  int duty_pct = LOOP_DUTY_PERCENT;
  FAR const char *devname = "/dev/pwm0";

  if (argc > 1)
    {
      duty_pct = atoi(argv[1]);
      if (duty_pct < 1 || duty_pct > 99)
        {
          printf("timloop: duty %d out of range (1..99)\n", duty_pct);
          return 1;
        }
    }

  if (argc > 2)
    {
      devname = argv[2];
    }

  /* Duty check window around the requested duty */

  int duty_lo = duty_pct - LOOP_DUTY_TOL;
  int duty_hi = duty_pct + LOOP_DUTY_TOL;

  /* Start the PWM output: 1 kHz at the requested duty on PA8 (TIM1_CH1) */

  pfd = open(devname, O_RDONLY);
  if (pfd < 0)
    {
      printf("timloop: open /dev/pwm0 failed: %d\n", errno);
      return 1;
    }

  memset(&info, 0, sizeof(info));
  info.frequency = LOOP_FREQ_HZ;
#ifdef CONFIG_PWM_MULTICHAN
  info.channels[0].channel = 1;
  info.channels[0].duty = (duty_pct * 65536) / 100;
#else
  info.duty = (duty_pct * 65536) / 100;
#endif

  ret = ioctl(pfd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)(uintptr_t)&info);
  if (ret < 0)
    {
      printf("timloop: SETCHARACTERISTICS failed: %d\n", errno);
      close(pfd);
      return 1;
    }

  ret = ioctl(pfd, PWMIOC_START, 0);
  if (ret < 0)
    {
      printf("timloop: PWMIOC_START failed: %d\n", errno);
      close(pfd);
      return 1;
    }

  printf("timloop: PWM started: %d Hz %d %% on %s\n",
         LOOP_FREQ_HZ, duty_pct, devname);

  /* Arm the capture */

  cfd = open("/dev/capture0", O_RDONLY);
  if (cfd < 0)
    {
      printf("timloop: open /dev/capture0 failed: %d\n", errno);
      ioctl(pfd, PWMIOC_STOP, 0);
      close(pfd);
      return 1;
    }

  /* Let the first edges settle, then sample */

  sleep(1);
  sleep(1);

  for (i = 0; i < LOOP_SAMPLES; i++)
    {
      sleep(1);

      memset(&cap, 0, sizeof(cap));
      ret = ioctl(cfd, CAPIOC_ALL, (unsigned long)(uintptr_t)&cap);
      if (ret < 0)
        {
          printf("timloop: CAPIOC_ALL failed: %d\n", errno);
          continue;
        }

      unsigned long delta = cap.edges - prev_edges;

      prev_edges = cap.edges;

      printf("timloop: sample %d: freq=%lu Hz duty=%u %% edges=%lu (+%lu/s)\n",
             i, (unsigned long)cap.freq, cap.duty,
             (unsigned long)cap.edges, delta);

      /* ~1000 rising edges per second at ~50 % duty means the whole
       * PWM -> jumper -> capture loop is working, both edges (frequency)
       * and pulse width (duty). */

      /* Sample 0 only establishes the edges baseline (its window also
       * contains edges produced before the loop started), so judge
       * frequency and duty from sample 1 on. */

      if (i > 0 &&
          delta > LOOP_FREQ_HZ - LOOP_FREQ_HZ / 10 &&
          delta < LOOP_FREQ_HZ + LOOP_FREQ_HZ / 10 &&
          (int)cap.duty >= duty_lo && (int)cap.duty <= duty_hi)
        {
          pass++;
        }
    }

  close(cfd);

  ioctl(pfd, PWMIOC_STOP, 0);
  close(pfd);

  if (pass >= LOOP_SAMPLES - 1)
    {
      printf("timloop: PASS (%d/%d samples at ~%d Hz ~%d %% on %s)\n",
             pass, LOOP_SAMPLES - 1, LOOP_FREQ_HZ, duty_pct, devname);
      return 0;
    }

  printf("timloop: FAIL (%d/%d samples); check the PA8 -> PA0 jumper\n",
         pass, LOOP_SAMPLES);
  return 1;
}
