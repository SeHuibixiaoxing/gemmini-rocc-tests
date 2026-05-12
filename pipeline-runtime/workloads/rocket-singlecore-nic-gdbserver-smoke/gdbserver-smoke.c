#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

volatile int smoke_counter = 0;
volatile int smoke_stop = 0;
volatile int smoke_worker_counters[4] = {0, 0, 0, 0};
volatile unsigned long smoke_memory_probe[8] = {
    0x11110000UL, 0x22220000UL, 0x33330000UL, 0x44440000UL,
    0x55550000UL, 0x66660000UL, 0x77770000UL, 0x88880000UL,
};

static int smoke_sleep_iters(void) {
  const char *env = getenv("GDBSERVER_SMOKE_SLEEP_ITERS");
  if (env == NULL || *env == '\0') {
    return 45;
  }

  char *end = NULL;
  long value = strtol(env, &end, 10);
  if (end == env || value < 0 || value > 600) {
    return 45;
  }
  return (int)value;
}

__attribute__((noinline)) void smoke_iteration_hook(int iteration) {
  smoke_memory_probe[iteration & 7] ^= (unsigned long)(0x1000 + iteration);
}

__attribute__((noinline)) void smoke_worker_heartbeat(int worker_id) {
  smoke_worker_counters[worker_id] += worker_id + 1;
  smoke_memory_probe[worker_id & 7] += (unsigned long)smoke_worker_counters[worker_id];
}

static void *smoke_worker_main(void *arg) {
  int worker_id = (int)(long)arg;

  while (!smoke_stop) {
    smoke_worker_heartbeat(worker_id);
    usleep(100000);
  }

  return NULL;
}

int main(void) {
  pthread_t workers[4];

  puts("gdbserver-smoke: begin");
  fflush(stdout);

  for (int i = 0; i < 4; ++i) {
    int rc = pthread_create(&workers[i], NULL, smoke_worker_main, (void *)(long)i);
    if (rc != 0) {
      printf("gdbserver-smoke: pthread_create failed worker=%d rc=%d\n", i, rc);
      fflush(stdout);
      return 2;
    }
  }

  int iters = smoke_sleep_iters();
  for (int i = 0; i < iters; ++i) {
    smoke_counter += i + 1;
    smoke_iteration_hook(i);
    sleep(1);
  }

  smoke_stop = 1;
  for (int i = 0; i < 4; ++i) {
    pthread_join(workers[i], NULL);
  }

  printf("gdbserver-smoke: done counter=%d\n", smoke_counter);
  fflush(stdout);

  int expected = iters * (iters + 1) / 2;
  return smoke_counter == expected ? 0 : 1;
}
