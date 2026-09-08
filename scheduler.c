#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

static Task tasks[MAX_TASKS];
static int num_tasks = 0;
static int tempo_total = 0;

int le_arquivo(const char *nome_arquivo) {
    FILE *f = fopen(nome_arquivo, "r");

    fscanf(f, "%d", &tempo_total);

    char nome[MAX_NOME];
    int p, d, b;
    while (fscanf(f, "%s %d %d %d", nome, &p, &d, &b) == 4) {
        strcpy(tasks[num_tasks].nome, nome);
        tasks[num_tasks].periodo = p;
        tasks[num_tasks].deadline = d;
        tasks[num_tasks].burst = b;
        tasks[num_tasks].ordem = num_tasks;

        tasks[num_tasks].completadas = 0;
        tasks[num_tasks].perdidas = 0;
        tasks[num_tasks].killed = 0;

        num_tasks++;
    }

    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    le_arquivo(argv[2]);  // ./scheduler rate voo.txt -> argv[2] e o arquivo

    printf("tempo_total = %d\n", tempo_total);
    printf("num_tasks = %d\n\n", num_tasks);

    for (int i = 0; i < num_tasks; i++) {
        Task *t = &tasks[i];
        printf("ordem=%d nome=%s periodo=%d deadline=%d burst=%d\n",
               t->ordem, t->nome, t->periodo, t->deadline, t->burst);
    }

    return 0;
}
