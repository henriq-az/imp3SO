#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_TASKS 64
#define MAX_NOME 64

typedef struct Task {
    char nome[MAX_NOME];
    int periodo;
    int deadline;
    int burst;

    int ordem;

    int completadas;
    int perdidas;
    int killed;
} Task;

typedef struct Instancia {
    Task *task;
    int chegada;
    int deadline_absoluto;
    int burst_restante;
} Instancia;

#endif
