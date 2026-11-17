#ifndef SIMULATION_EVENTS_H
#define SIMULATION_EVENTS_H

// предметные результаты операций TCP-режима
typedef enum SimulationEvent
{
    EVENT_ARRIVAL = 1,
    EVENT_BOOK_REQUEST,
    EVENT_BOOK_GIVEN,
    EVENT_BOOK_UNAVAILABLE,
    EVENT_REQUEST_CREATED,
    EVENT_BOOK_RETURNED,
    EVENT_NOTIFICATION,
    EVENT_BOOK_RESERVED,
    EVENT_SIMULATION_START,
    EVENT_DAY_START,
    EVENT_LIBRARIAN_START,
    EVENT_LIBRARIAN_DONE,
    EVENT_READER_START,
    EVENT_READER_DONE,
    EVENT_DAY_DONE,
    EVENT_SIMULATION_DONE
} SimulationEvent;

/** @brief Получатель предметного события; reader_id — предметный id, value — срок/номер/признак резерва. */
typedef void (*SimulationEventHandler)(
    void *context, SimulationEvent event, int reader_id, int book_id, int day, int value);

#endif
