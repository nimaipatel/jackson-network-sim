#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>

typedef enum {
    ARRIVAL,
    START_1,
    COMPLETE_1,
    START_2,
    COMPLETE_2,
} Event_Type;

typedef struct {
    double time;
    uint64_t job_id;
    Event_Type type;
} Event;

typedef struct {
#define FIFO_INIT_SIZE    0x10
#define FIFO_SHRINK_FRAC  0x04
#define FIFO_EXPAND_FRAC  0x02
    uint64_t *jobs;
    size_t start;
    size_t len;
    size_t cap;
} Fifo;

typedef struct {
#define EVENT_STACK_INIT_SIZE 0x10
    Event *events;
    size_t len;
    size_t cap;
} Event_Stack;

typedef struct {
#define DURATION       10000
#define ARRIVAL_RATE   2
#define SERVICE_RATE_1 3
#define SERVICE_RATE_2 5

    Event_Stack es;
    double clock;

    bool server_1_busy;
    Fifo q1;

    bool server_2_busy;
    Fifo q2;

    uint64_t total_jobs;
} Simulation;

typedef struct
{
#define DIST_INIT_SIZE 0x10
    double *items;
    size_t cap;
} Dist;

static inline size_t
max_size_t(const size_t a, const size_t b)
{
    return a > b ? a : b;
}

static void
Dist_Add(Dist *dist, size_t index, double amount)
{
    if (index + 1 > dist->cap) {
        size_t cap = max_size_t(DIST_INIT_SIZE, 2 * index);
        dist->items = realloc(dist->items, sizeof(dist->items[0]) * cap);

        for (size_t i = dist->cap; i < cap; i += 1) {
            dist->items[i] = 0;
        }

        dist->cap = cap;
    }

    dist->items[index] += amount;
}

static void
Dist_Normalize(Dist *dist)
{
    double total = 0;
    for (size_t i = 0; i < dist->cap; i += 1) {
        total += dist->items[i];
    }

    for (size_t i = 0; i < dist->cap; i += 1) {
        dist->items[i] /= total;
    }
}

static bool
Fifo_Full(Fifo *q)
{
    return q->len == q->cap;
}

static bool
Fifo_Empty(Fifo *q)
{
    return q->len == 0;
}

static void
Fifo_Add(Fifo *q, uint64_t job)
{
    if(Fifo_Full(q)) {
        size_t cap = max_size_t(FIFO_INIT_SIZE, q->cap * FIFO_EXPAND_FRAC);
        uint64_t *jobs = malloc(sizeof(jobs[0]) * cap);

        for (size_t i = 0; i < q->len; i += 1) {
            jobs[i] = q->jobs[(q->start + i) % q->cap];
        }

        if (q->jobs) {
            free(q->jobs);
        }

        q->cap = cap;
        q->start = 0;
        q->jobs = jobs;
    }

    if (Fifo_Empty(q)) {
        q->start = 0;
        q->len = 1;
        q->jobs[0] = job;
    } else {
        size_t index = (q->start + q->len++) % q->cap;
        q->jobs[index] = job;
    }
}

static uint64_t
Fifo_Pop(Fifo *q)
{
    assert(!Fifo_Empty(q));

    const uint64_t result = q->jobs[q->start];

    q->start += 1;
    q->start %= q->cap;
    q->len -= 1;

    if (q->len < q->cap / FIFO_SHRINK_FRAC) {
        size_t cap = q->cap / FIFO_SHRINK_FRAC;
        uint64_t *jobs = malloc(sizeof(jobs[0]) * cap);

        for (size_t i = 0; i < q->len; i += 1) {
            jobs[i] = q->jobs[(q->start + i) % q->cap];
        }

        free(q->jobs);

        q->cap = cap;
        q->start = 0;
        q->jobs = jobs;
    }

    return result;
}

static void
Event_Swap(Event *a, Event *b)
{
    Event x = *a;
    Event y = *b;

    *a = y;
    *b = x;
}

static void
Event_Stack_Sift_Up(Event_Stack *es, size_t index)
{
    size_t parent = (index - 1) / 2;
    while (index > 0 && es->events[parent].time > es->events[index].time) {
        Event_Swap(&es->events[parent], &es->events[index]);
        index = parent;
        parent = (index - 1) / 2;
    }
}

static void
Event_Stack_Sift_Down(Event_Stack *es, size_t index)
{
    for (;;) {
        const size_t left = 2 * index + 1;
        const size_t right = left + 1;
        size_t min = index;

        if (left < es->len && es->events[left].time < es->events[min].time) {
            min = left;
        }

        if (right < es->len && es->events[right].time < es->events[min].time) {
            min = right;
        }

        if (min == index) {
            break;
        } else {
            Event_Swap(&es->events[min], &es->events[index]);
            index = min;
        }
    }
}

static void
Event_Stack_Add(Event_Stack *es, Event e)
{
    if (es->len == es->cap) {
        es->cap = max_size_t(EVENT_STACK_INIT_SIZE, es->cap * 2);
        es->events = realloc(es->events, sizeof(es->events[0]) * es->cap);
    }

    es->events[es->len++] = e;
    Event_Stack_Sift_Up(es, es->len - 1);
}

static Event
Event_Stack_Pop(Event_Stack *es)
{
    assert(es->len > 0);

    const Event result = es->events[0];
    es->events[0] = es->events[--es->len];
    Event_Stack_Sift_Down(es, 0);
    return result;
}

static double
Random_Exponential(double lambda)
{
    double u = rand() / (RAND_MAX + 1.0);
    return -log(1 - u) / lambda;
}

static void
Start_Service_1(Simulation *s, uint64_t job_id)
{
    s->server_1_busy = true;

    double service_time = Random_Exponential(SERVICE_RATE_1);
    Event event = {
        .time = s->clock + service_time,
        .type = COMPLETE_1,
        .job_id = job_id
    };
    Event_Stack_Add(&s->es, event);
}

static void
Start_Service_2(Simulation *s, uint64_t job_id)
{
    s->server_2_busy = true;

    double service_time = Random_Exponential(SERVICE_RATE_2);
    Event event = {
        .time = s->clock + service_time,
        .type = COMPLETE_2,
        .job_id = job_id
    };
    Event_Stack_Add(&s->es, event);
}

static void
Complete_Service_1(Simulation *s, uint64_t job_id)
{
    s->server_1_busy = false;

    if (s->server_2_busy) {
        Fifo_Add(&s->q2, job_id);
    } else {
        Start_Service_2(s, job_id);
    }

    if (s->q1.len > 0) {
        uint64_t next_job_id = Fifo_Pop(&s->q1);
        Start_Service_1(s, next_job_id);
    }
}

static void
Complete_Service_2(Simulation *s, uint64_t job_id)
{
    s->server_2_busy = false;

    if (s->q2.len > 0) {
        uint64_t next_job_id = Fifo_Pop(&s->q2);
        Start_Service_2(s, next_job_id);
    }
}

static void
Arrival(Simulation *s)
{
    const uint64_t job_id = s->total_jobs++;
    if (s->server_1_busy) {
        Fifo_Add(&s->q1, job_id);
    } else {
        Start_Service_1(s, job_id);
    }

    const double iat = Random_Exponential(ARRIVAL_RATE);
    const Event next_arrival = {
        .time = s->clock + iat,
        .type = ARRIVAL
    };
    Event_Stack_Add(&s->es, next_arrival);
}

static Dist
Run(Simulation *s)
{
    Dist d1 = {0};
    Dist d2 = {0};
    Dist dt = {0};

    Arrival(s);
    while (s->es.len > 0 && s->clock < DURATION) {
        Event e = Event_Stack_Pop(&s->es);
        double elapsed = e.time - s->clock;

        uint64_t n1 = s->q1.len + (uint64_t) s->server_1_busy;
        Dist_Add(&d1, n1, elapsed);

        uint64_t n2 = s->q2.len + (uint64_t) s->server_2_busy;
        Dist_Add(&d2, n2, elapsed);

        Dist_Add(&dt, n1 + n2, elapsed);

        s->clock = e.time;

        if (e.type == ARRIVAL) {
            Arrival(s);
        } else if (e.type == COMPLETE_1) {
            Complete_Service_1(s, e.job_id);
        } else if (e.type == COMPLETE_2) {
            Complete_Service_2(s, e.job_id);
        } else {
            assert(false);
        }
    }

    Dist_Normalize(&d1);
    Dist_Normalize(&d2);
    Dist_Normalize(&dt);

    return dt;
}

int
main(void)
{
    Simulation s = {0};
    Dist d = Run(&s);
    (void) d;
    return 0;
}
