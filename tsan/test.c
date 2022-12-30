#include <stdio.h>
#include <threads.h>

mtx_t m;
int print_num = 0;

#define N 200

int thread(void *data) {
    for (int i = 0; i < N; ++i) {
        mtx_lock(&m);
        ++print_num;
        printf("thread %ld: %d\n", (long int)data, i);
        mtx_unlock(&m);
    }

    thrd_exit(0);
}

int main(void) {
    mtx_init(&m, mtx_plain);
    thrd_t tid1, tid2, tid3, tid4;
    thrd_create(&tid1, thread, (void *)1L);
    thrd_create(&tid2, thread, (void *)2L);
    thrd_create(&tid3, thread, (void *)3L);
    thrd_create(&tid4, thread, (void *)4L);

    for (int i = 0; i < N; ++i) {
        mtx_lock(&m);
        ++print_num;
        printf("thread %d: %d\n", 0, i);
        mtx_unlock(&m);
    }

    thrd_join(tid1, NULL);
    thrd_join(tid2, NULL);
    thrd_join(tid3, NULL);
    thrd_join(tid4, NULL);
    printf("Printed %d messages\n", print_num);
}
