#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

#define ARRIVAL_RATE   2
#define SERVICE_RATE_1 3
#define SERVICE_RATE_2 5

typedef enum {
    ARRIVAL,
    START_1,
    COMPLETE_1,
    START_2,
    COMPLETE_2,
} Event_Type;

typedef struct {
    uint64_t clock;
    uint64_t job_id;
    Event_Type type;
} Event;

#define FIFO_MAX_SIZE 0x100
typedef struct {
    uint64_t jobs[FIFO_MAX_SIZE];
    size_t start;
    size_t len;
} Fifo;

static inline size_t
max_size_t(const size_t a, const size_t b)
{
    return a > b ? a : b;
}

static bool
Fifo_Full(Fifo *q)
{
    return q->len == FIFO_MAX_SIZE;
}

static bool
Fifo_Empty(Fifo *q)
{
    return q->len == 0;
}

static void
Fifo_Add(Fifo *q, uint64_t job)
{
    assert(!Fifo_Full(q));

    if (Fifo_Empty(q)) {
        q->start = 0;
        q->len = 1;
        q->jobs[0] = job;
    } else {
        size_t index = (q->start + q->len++) % FIFO_MAX_SIZE;
        q->jobs[index] = job;
    }
}

static uint64_t
Fifo_Pop(Fifo *q)
{
    assert(!Fifo_Empty(q));

    const uint64_t result = q->jobs[q->start];

    q->start += 1;
    q->start %= FIFO_MAX_SIZE;
    q->len -= 1;

    return result;
}

int
main(void)
{
    int i = 0;
    Fifo q = {0};
    for (; i < 20; i += 1) {
        Fifo_Add(&q, i);
    }

    for (; i < 30; i += i) {
        Fifo_Add(&q, i);
    }

    for (int _ = 0; _ < 5; _ += 1) {
        printf("%llu\n", Fifo_Pop(&q));
    }

    while (!Fifo_Empty(&q)) {
        printf("%llu\n", Fifo_Pop(&q));
    }

    return 0;
}
