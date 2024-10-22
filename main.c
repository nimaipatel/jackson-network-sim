//
// Copyright (C) 2024 Patel, Nimai <nimai.m.patel@gmail.com>
// Author: Patel, Nimai <nimai.m.patel@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>

typedef enum {
    ARRIVAL,
    START,
    COMPLETE,
} Event_Type;

typedef struct {
    double time;
    uint64_t job_id;
    Event_Type type;
    uint64_t service;
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
#define MAX_NUM_QUEUES 0x1000
    size_t num_queues;
    double duration;
    double arrival_rate;
    double service_rate[MAX_NUM_QUEUES];

    Event_Stack es;
    double clock;

    bool busy[MAX_NUM_QUEUES];
    Fifo queue[MAX_NUM_QUEUES];

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

    if (q->len == 0) {
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
    assert(q->len > 0);

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
Start_Service(Simulation *s, uint64_t service, uint64_t job_id)
{
    s->busy[service] = true;

    double service_time = Random_Exponential(s->service_rate[service]);
    Event event = {
        .time = s->clock + service_time,
        .type = COMPLETE,
        .service = service,
        .job_id = job_id,
    };
    Event_Stack_Add(&s->es, event);
}

static void
Complete_Service(Simulation *s, uint64_t service, uint64_t job_id)
{
    s->busy[service] = false;

    if (service < s->num_queues - 1) {
        uint64_t next_service = service + 1;

        if (s->busy[next_service]) {
            Fifo_Add(&s->queue[next_service], job_id);
        } else {
            Start_Service(s, next_service, job_id);
        }
    }

    if (s->queue[service].len > 0) {
        uint64_t next_job_id = Fifo_Pop(&s->queue[service]);
        Start_Service(s, service, next_job_id);
    }
}

static void
Arrival(Simulation *s)
{
    const uint64_t job_id = s->total_jobs++;
    if (s->busy[0]) {
        Fifo_Add(&s->queue[0], job_id);
    } else {
        Start_Service(s, 0, job_id);
    }

    const double iat = Random_Exponential(s->arrival_rate);
    const Event next_arrival = {
        .time = s->clock + iat,
        .type = ARRIVAL
    };
    Event_Stack_Add(&s->es, next_arrival);
}

static Dist
Run(Simulation *s)
{
    Dist d[MAX_NUM_QUEUES] = {0};
    Dist dt = {0};

    Arrival(s);
    while (s->es.len > 0 && s->clock < s->duration) {
        Event e = Event_Stack_Pop(&s->es);
        double elapsed = e.time - s->clock;

        uint64_t n_total = 0;
        for (size_t i = 0; i < s->num_queues; i += 1) {
            uint64_t n = s->queue[i].len + (uint64_t) s->busy[i];
            n_total += n;
            Dist_Add(&d[i], n, elapsed);
        }

        Dist_Add(&dt, n_total, elapsed);

        s->clock = e.time;

        if (e.type == ARRIVAL) {
            Arrival(s);
        } else if (e.type == COMPLETE) {
            Complete_Service(s, e.service, e.job_id);
        } else {
            assert(false);
        }
    }

    for (size_t i = 0; i < s->num_queues; i += 1) {
        Dist_Normalize(&d[i]);
    }
    Dist_Normalize(&dt);

    return dt;
}

int
main(void)
{
    Simulation s = {
        .duration = 10000,
        .num_queues = 2,
        .arrival_rate = 2,
        .service_rate = {3, 5},
    };

    Dist d = Run(&s);
    (void) d;

    return 0;
}
