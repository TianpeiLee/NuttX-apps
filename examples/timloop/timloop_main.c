/*
 * apps/examples/timloop/timloop_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * One-command closed-loop self test for the CH32H417 timer I/O path:
 *
 *   TIM1_CH2 (PE11) runs 1 kHz / 50 % PWM.  The board jumper PE11 -> PA0
 *   feeds that signal into TIM2_CH1, whose capture driver measures the
 *   frequency.  The jumper must be in place before running this command.
 *
 * Requires: CONFIG_CH32H41X_HAVE_TIM1_PWM and
 *           CONFIG_CH32H41X_HAVE_TIM2_CAPTURE.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
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
#define LOOP_SAMPLES      60

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

  /* Start the PWM output: 1 kHz, 50 % on hardware channel 2 (PE11) */

  pfd = open("/dev/pwm0", O_RDONLY);
  if (pfd < 0)
    {
      printf("timloop: open /dev/pwm0 failed: %d\n", errno);
      return 1;
    }

  memset(&info, 0, sizeof(info));
  info.frequency = LOOP_FREQ_HZ;
#ifdef CONFIG_PWM_MULTICHAN
  info.channels[0].channel = 1;
  info.channels[0].duty = (LOOP_DUTY_PERCENT * 65536) / 100;
#else
  info.duty = (LOOP_DUTY_PERCENT * 65536) / 100;
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

  printf("timloop: PWM started: %d Hz %d %% on PA8 (TIM1_CH1)\n",
         LOOP_FREQ_HZ, LOOP_DUTY_PERCENT);

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

      printf("timloop: sample %d: edges=%lu (+%lu/s)\n",
             i, (unsigned long)cap.edges, delta);

      /* The measured frequency within tolerance of the nominal 1 kHz
       * means the whole PWM -> jumper -> capture loop is working. */

      /* ~1000 rising edges per second means the whole PWM ->
       * jumper -> capture loop is working. */

      if (delta > LOOP_FREQ_HZ - LOOP_FREQ_HZ / 10 &&
          delta < LOOP_FREQ_HZ + LOOP_FREQ_HZ / 10)
        {
          pass++;
        }
    }

  close(cfd);

  ioctl(pfd, PWMIOC_STOP, 0);
  close(pfd);

  if (pass > LOOP_SAMPLES / 2)
    {
      printf("timloop: PASS (%d/%d samples measured ~%d Hz)\n",
             pass, LOOP_SAMPLES, LOOP_FREQ_HZ);
      return 0;
    }

  printf("timloop: FAIL (%d/%d samples); check the PA8 -> PA0 jumper\n",
         pass, LOOP_SAMPLES);
  return 1;
}
