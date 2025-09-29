// cfs_showcase.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <sys/resource.h>

static double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// 简单自校准：尽量让 busy_loop 运行约 target_secs
static void busy_for_seconds(double target_secs) {
    volatile unsigned long long acc = 0;
    double start = now_sec();
    // 先热身估算迭代成本
    unsigned long long probe_iters = 20000000ULL;
    for (unsigned long long i = 0; i < probe_iters; i++) acc += i;
    double probe_elapsed = now_sec() - start;
    if (probe_elapsed <= 0) probe_elapsed = 1e-3;
    double iters_per_sec = probe_iters / probe_elapsed;

    unsigned long long need_iters = (unsigned long long)(iters_per_sec * target_secs);
    double t0 = now_sec();
    for (unsigned long long i = 0; i < need_iters; i++) acc += i;
    (void)acc;
    double t1 = now_sec();
    // 若偏差大，再补或提前结束（简单处理）
    while (t1 - t0 < target_secs) {
        for (unsigned long long i = 0; i < (unsigned long long)(iters_per_sec * 0.05); i++) acc += i;
        t1 = now_sec();
    }
}

static void pin_to_cpu0_or_die() {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        exit(1);
    }
}

static const char* policy_name(int p) {
    switch (p) {
        case SCHED_OTHER: return "SCHED_OTHER(CFS)";
        case SCHED_FIFO:  return "SCHED_FIFO";
        case SCHED_RR:    return "SCHED_RR";
#ifdef SCHED_BATCH
        case SCHED_BATCH: return "SCHED_BATCH";
#endif
#ifdef SCHED_IDLE
        case SCHED_IDLE:  return "SCHED_IDLE";
#endif
        default:          return "UNKNOWN";
    }
}

static void set_fifo(int prio) {
    struct sched_param sp = {.sched_priority = prio};
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        perror("sched_setscheduler FIFO");
        // 不退出，让你在无权限/WSL 上也能继续看 CFS 效果
    }
}

int main() {
    // 目标运行时长（秒），默认 3s，可用环境变量调节
    double work_secs = 3.0;
    const char* env = getenv("WORK_SECS");
    if (env) work_secs = atof(env);

    pin_to_cpu0_or_die();

    const int N = 3;
    pid_t pids[N];

    double t0 = now_sec();

    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程
            pin_to_cpu0_or_die();

            // 设置不同策略
            if (i == 0) {
                // 高优先级 FIFO（需要 root；失败会保持在 CFS）
                set_fifo(80);
            } else if (i == 1) {
                // CFS, nice = 0（默认）
                // 不设
            } else if (i == 2) {
                // CFS, nice = +10（低优先级）
                if (nice(10) == -1 && errno != 0) {
                    perror("nice(+10)");
                }
            }

            // 记录开始
            int pol = sched_getscheduler(0);
            int niceval = getpriority(PRIO_PROCESS, 0);
            double s = now_sec();
            fprintf(stderr,
                    "[child %d] start=%.6f policy=%s nice=%d\n",
                    i, s - t0, policy_name(pol), niceval);

            busy_for_seconds(work_secs);

            double e = now_sec();
            fprintf(stderr,
                    "[child %d] end=%.6f elapsed=%.3f sec policy=%s nice=%d\n",
                    i, e - t0, e - s, policy_name(pol), niceval);
            _exit(0);
        } else if (pid > 0) {
            pids[i] = pid;
        } else {
            perror("fork");
            exit(1);
        }
    }

    for (int i = 0; i < N; i++) waitpid(pids[i], NULL, 0);
    double t1 = now_sec();
    printf("Total wall time: %.3f sec\n", t1 - t0);
    return 0;
}
