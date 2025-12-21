#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define HEX_DIGITS 32

typedef struct {
    char **numbers;
    size_t start;
    size_t end;
    int thread_id;
    size_t count;
    __uint128_t sum;
} ThreadData;


void uint128_to_dec(__uint128_t num, char *buffer, size_t size) {
    buffer[size - 1] = '\0';
    int pos = size - 2;

    if (num == 0) {
        buffer[pos--] = '0';
    } else {
        while (num > 0 && pos >= 0) {
            buffer[pos--] = '0' + (num % 10);
            num /= 10;
        }
    }
    memmove(buffer, &buffer[pos + 1], size - pos - 1);
}


void *sum_thread(void *arg) {
    ThreadData *d = (ThreadData*)arg;

    d->sum = 0;
    d->count = 0;

    for (size_t i = d->start; i < d->end; i++) {
        __uint128_t num = 0;

        for (int j = 0; j < HEX_DIGITS; j++) {
            char c = d->numbers[i][j];
            uint8_t v;

            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else v = 0;

            num = (num << 4) | v;
        }

        d->sum += num;
        d->count++;
    }

    return NULL;
}


void sequential_compute(char **numbers, size_t count,
                        __uint128_t *sum_out)
{
    *sum_out = 0;

    for (size_t i = 0; i < count; i++) {
        __uint128_t num = 0;

        for (int j = 0; j < HEX_DIGITS; j++) {
            char c = numbers[i][j];
            uint8_t v;

            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else v = 0;

            num = (num << 4) | v;
        }

        *sum_out += num;
    }
}

long get_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


void write_csv(int threads, size_t memory, long par, long seq, double speedup, double eff)
{
    int file_exists = 0;

    struct stat st;
    if (stat("result.csv", &st) == 0) file_exists = 1;

    FILE *f = fopen("result.csv", "a");
    if (!f) {
        perror("Ошибка открытия result.csv");
        return;
    }

    if (!file_exists) {
        fprintf(f, "threads,memory,parallel_ms,serial_ms,speed_up,efficiency\n");
    }

    fprintf(f, "%d,%zu,%ld,%ld,%.6f,%.6f\n",
            threads, memory, par, seq, speedup, eff);

    fclose(f);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Использование: %s <threads> <memory_bytes>\n", argv[0]);
        return 1;
    }

    int threads = atoi(argv[1]);
    size_t memory = atol(argv[2]);

    if (threads <= 0) {
        fprintf(stderr, "Ошибка: количество потоков должно быть > 0\n");
        return 1;
    }

    size_t max_numbers = memory / (HEX_DIGITS + 1);
    if (max_numbers == 0) {
        fprintf(stderr, "Ошибка: слишком мало памяти для хранения даже одной строки\n");
        return 1;
    }

    char **numbers = malloc(max_numbers * sizeof(char*));
    if (!numbers) {
        perror("Ошибка выделения памяти под массив указателей (numbers)");
        return 1;
    }

    for (size_t i = 0; i < max_numbers; i++) {
        numbers[i] = malloc(HEX_DIGITS + 1);
        if (!numbers[i]) {
            perror("Ошибка выделения памяти под строку");
            for (size_t k = 0; k < i; k++) free(numbers[k]);
            free(numbers);
            return 1;
        }
    }

    FILE *file = fopen("numbers.txt", "r");
    if (!file) {
        perror("Ошибка открытия numbers.txt");
        for (size_t i = 0; i < max_numbers; i++) free(numbers[i]);
        free(numbers);
        return 1;
    }

    size_t count = 0;
    while (count < max_numbers && fscanf(file, "%32s", numbers[count]) == 1)
        count++;

    fclose(file);

    printf("Считано строк: %zu\n", count);

    long p_start = get_ms();

    pthread_t tid[threads];
    ThreadData td[threads];

    size_t chunk = count / threads;
    size_t rem = count % threads;

    size_t start = 0;

    for (int i = 0; i < threads; i++) {
        size_t end = start + chunk + (i < rem ? 1 : 0);

        td[i].numbers = numbers;
        td[i].start = start;
        td[i].end = end;
        td[i].thread_id = i + 1;
        td[i].sum = 0;

        int err = pthread_create(&tid[i], NULL, sum_thread, &td[i]);
        if (err != 0) {
            fprintf(stderr, "Ошибка создания потока %d: %s\n", i, strerror(err));
            for (size_t k = 0; k < max_numbers; k++) free(numbers[k]);
            free(numbers);
            return 1;
        }

        start = end;
    }

    __uint128_t total_sum = 0;
    size_t total_count = 0;

    for (int i = 0; i < threads; i++) {
        int err = pthread_join(tid[i], NULL);
        if (err != 0) {
            fprintf(stderr, "Ошибка ожидания потока %d: %s\n", i, strerror(err));
        } else {
            total_sum += td[i].sum;
            total_count += td[i].count;
        }
    }

    long p_end = get_ms();
    long parallel_ms = p_end - p_start;

    char sum_buf[40], avg_buf[40];
    uint128_to_dec(total_sum, sum_buf, sizeof(sum_buf));

    if (total_count > 0) {
        __uint128_t avg_val = total_sum / total_count;
        uint128_to_dec(avg_val, avg_buf, sizeof(avg_buf));
    } else {
        strcpy(avg_buf, "0");
    }

    printf("\n[Параллельно] Count = %zu, Sum = %s, Avg = %s\n",
           total_count, sum_buf, avg_buf);
    printf("Время: %ld ms\n", parallel_ms);


    long s_start = get_ms();
    __uint128_t seq_sum;
    sequential_compute(numbers, count, &seq_sum);
    long s_end = get_ms();
    long sequential_ms = s_end - s_start;

    char seq_sum_buf[40];
    uint128_to_dec(seq_sum, seq_sum_buf, sizeof(seq_sum_buf));

    char seq_avg_buf[40];
    if (count > 0) {
        __uint128_t seq_avg = seq_sum / count;
        uint128_to_dec(seq_avg, seq_avg_buf, sizeof(seq_avg_buf));
    } else {
        strcpy(seq_avg_buf, "0");
    }

    printf("\n[Последовательно] Count = %zu, Sum = %s, Avg = %s\n",
           count, seq_sum_buf, seq_avg_buf);
    printf("Время: %ld ms\n", sequential_ms);


    double speed_up = (parallel_ms > 0) ? (double)sequential_ms / parallel_ms : 0.0;
    double efficiency = (threads > 0) ? speed_up / threads : 0.0;

    printf("\nSpeed-Up = %.4f\n", speed_up);
    printf("Efficiency = %.4f\n", efficiency);


    write_csv(threads, memory, parallel_ms, sequential_ms, speed_up, efficiency);


    for (size_t i = 0; i < max_numbers; i++)
        free(numbers[i]);
    free(numbers);

    return 0;
}
