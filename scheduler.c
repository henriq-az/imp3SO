#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include "scheduler.h"

#define EXIT_USO         1
#define EXIT_MODO        2
#define EXIT_ARQUIVO     3
#define EXIT_MALFORMADO  4
#define EXIT_RESTRICAO   5

static Task tasks[MAX_TASKS];
static int num_tasks = 0;
static int tempo_total = 0;

static Instancia prontas[MAX_TASKS];
static int num_prontas = 0;

static FILE *saida = NULL;
static Task *bloco_task = NULL;
static int bloco_inicio = 0;
static int bloco_ativo = 0;

static void fecha_bloco(int fim, const char *sufixo) {
    int duracao = fim - bloco_inicio;
    if (duracao <= 0) return;

    if (bloco_task == NULL) {
        fprintf(saida, "idle for %d units\n", duracao);
    } else if (sufixo != NULL) {
        fprintf(saida, "[%s] for %d units - %s\n", bloco_task->nome, duracao, sufixo);
    } else {
        fprintf(saida, "[%s] for %d units\n", bloco_task->nome, duracao);
    }
}

static int parse_inteiro_positivo(const char *texto, const char *campo,
                                   const char *nome_tarefa, int linha,
                                   const char *arquivo) {
    char *fim;
    errno = 0;
    long valor = strtol(texto, &fim, 10);

    if (fim == texto || *fim != '\0' || errno == ERANGE) {
        fprintf(stderr,
                "Erro: tarefa '%s' (linha %d de '%s') com %s nao numerico ('%s')\n",
                nome_tarefa, linha, arquivo, campo, texto);
        exit(EXIT_MALFORMADO);
    }
    if (valor <= 0) {
        fprintf(stderr,
                "Erro: tarefa '%s' (linha %d de '%s') com %s nao positivo (%ld)\n",
                nome_tarefa, linha, arquivo, campo, valor);
        exit(EXIT_MALFORMADO);
    }

    return (int)valor;
}

int le_arquivo(const char *nome_arquivo) {
    FILE *f = fopen(nome_arquivo, "r");
    if (f == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s': %s\n",
                nome_arquivo, strerror(errno));
        exit(EXIT_ARQUIVO);
    }

    char linha1[256];
    if (fgets(linha1, sizeof(linha1), f) == NULL) {
        fprintf(stderr, "Erro: '%s' esta vazio (esperava tempo_total na linha 1)\n",
                nome_arquivo);
        fclose(f);
        exit(EXIT_MALFORMADO);
    }

    char *fim;
    errno = 0;
    long tt = strtol(linha1, &fim, 10);
    while (*fim == ' ' || *fim == '\t' || *fim == '\n' || *fim == '\r') fim++;
    if (fim == linha1 || *fim != '\0' || errno == ERANGE) {
        fprintf(stderr, "Erro: tempo_total invalido (nao numerico) na linha 1 de '%s'\n",
                nome_arquivo);
        fclose(f);
        exit(EXIT_MALFORMADO);
    }
    if (tt <= 0) {
        fprintf(stderr, "Erro: tempo_total deve ser positivo (lido %ld) na linha 1 de '%s'\n",
                tt, nome_arquivo);
        fclose(f);
        exit(EXIT_MALFORMADO);
    }
    tempo_total = (int)tt;

    char linha[256];
    char nome[MAX_NOME];
    char campo_p[64], campo_d[64], campo_b[64];
    char extra[64];
    int linha_num = 1;

    while (fgets(linha, sizeof(linha), f) != NULL) {
        linha_num++;

        char *c = linha;
        while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') c++;
        if (*c == '\0') continue;

        int lidos = sscanf(linha, "%63s %63s %63s %63s %63s",
                            nome, campo_p, campo_d, campo_b, extra);

        if (lidos < 4) {
            fprintf(stderr,
                    "Erro: linha %d de '%s' com campo(s) faltando (esperado 4, lido %d)\n",
                    linha_num, nome_arquivo, lidos);
            fclose(f);
            exit(EXIT_MALFORMADO);
        }
        if (lidos > 4) {
            fprintf(stderr,
                    "Erro: linha %d de '%s' com campo(s) sobrando (esperado 4, lido %d)\n",
                    linha_num, nome_arquivo, lidos);
            fclose(f);
            exit(EXIT_MALFORMADO);
        }

        if (num_tasks >= MAX_TASKS) {
            fprintf(stderr, "Erro: numero maximo de tarefas (%d) excedido em '%s'\n",
                    MAX_TASKS, nome_arquivo);
            fclose(f);
            exit(EXIT_MALFORMADO);
        }

        int p = parse_inteiro_positivo(campo_p, "periodo", nome, linha_num, nome_arquivo);
        int d = parse_inteiro_positivo(campo_d, "deadline", nome, linha_num, nome_arquivo);
        int b = parse_inteiro_positivo(campo_b, "burst", nome, linha_num, nome_arquivo);

        if (d > p) {
            fprintf(stderr,
                    "Erro: tarefa '%s' (linha %d de '%s') com deadline > periodo (D=%d, P=%d)\n",
                    nome, linha_num, nome_arquivo, d, p);
            fclose(f);
            exit(EXIT_RESTRICAO);
        }
        if (b > d) {
            fprintf(stderr,
                    "Erro: tarefa '%s' (linha %d de '%s') com burst > deadline (C=%d, D=%d)\n",
                    nome, linha_num, nome_arquivo, b, d);
            fclose(f);
            exit(EXIT_RESTRICAO);
        }

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

    if (num_tasks == 0) {
        fprintf(stderr, "Erro: '%s' nao contem nenhuma tarefa\n", nome_arquivo);
        exit(EXIT_MALFORMADO);
    }

    return 0;
}

void checa_chegadas(int t) {
    for (int i = 0; i < num_tasks; i++) {
        Task *task = &tasks[i];
        if (t % task->periodo == 0) {
            if (num_prontas >= MAX_TASKS) continue;

            prontas[num_prontas].task = task;
            prontas[num_prontas].chegada = t;
            prontas[num_prontas].deadline_absoluto = t + task->deadline;
            prontas[num_prontas].burst_restante = task->burst;
            num_prontas++;
        }
    }
}

void checa_deadlines(int t) {
    int i = 0;
    while (i < num_prontas) {
        if (prontas[i].deadline_absoluto == t) {
            prontas[i].task->perdidas++;

            if (bloco_ativo && bloco_task == prontas[i].task) {
                fecha_bloco(t, "L");
                bloco_ativo = 0;
            }

            prontas[i] = prontas[num_prontas - 1];
            num_prontas--;
        } else {
            i++;
        }
    }
}

int escolhe_prontas_rate(void) {
    int melhor = -1;

    for (int i = 0; i < num_prontas; i++) {
        if (melhor == -1) {
            melhor = i;
            continue;
        }

        Task *a = prontas[i].task;
        Task *b = prontas[melhor].task;

        if (a->periodo < b->periodo ||
            (a->periodo == b->periodo && a->ordem < b->ordem)) {
            melhor = i;
        }
    }

    return melhor;
}

int escolhe_prontas_edf(void) {
    int melhor = -1;

    for (int i = 0; i < num_prontas; i++) {
        if (melhor == -1) {
            melhor = i;
            continue;
        }

        if (prontas[i].deadline_absoluto < prontas[melhor].deadline_absoluto ||
            (prontas[i].deadline_absoluto == prontas[melhor].deadline_absoluto &&
             prontas[i].task->ordem < prontas[melhor].task->ordem)) {
            melhor = i;
        }
    }

    return melhor;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <rate|edf> <arquivo_de_entrada>\n", argv[0]);
        return EXIT_USO;
    }

    const char *modo = argv[1];
    if (strcmp(modo, "rate") != 0 && strcmp(modo, "edf") != 0) {
        fprintf(stderr, "Erro: modo '%s' invalido (use 'rate' ou 'edf')\n", modo);
        return EXIT_MODO;
    }

    le_arquivo(argv[2]);

    int eh_rate = (strcmp(modo, "rate") == 0);

    char nome_saida[64];
    snprintf(nome_saida, sizeof(nome_saida), "%s_hac2.out", eh_rate ? "rate" : "edf");

    saida = fopen(nome_saida, "w");
    if (saida == NULL) {
        fprintf(stderr, "Erro: nao foi possivel criar '%s': %s\n",
                nome_saida, strerror(errno));
        return EXIT_ARQUIVO;
    }

    fprintf(saida, "EXECUTION BY %s\n", eh_rate ? "RATE" : "EDF");

    for (int t = 0; t < tempo_total; t++) {
        checa_deadlines(t);
        checa_chegadas(t);

        int idx = eh_rate ? escolhe_prontas_rate() : escolhe_prontas_edf();
        Task *escolhido = (idx == -1) ? NULL : prontas[idx].task;

        if (!bloco_ativo) {
            bloco_task = escolhido;
            bloco_inicio = t;
            bloco_ativo = 1;
        } else if (bloco_task != escolhido) {
            fecha_bloco(t, bloco_task == NULL ? NULL : "H");
            bloco_task = escolhido;
            bloco_inicio = t;
        }

        if (escolhido != NULL) {
            Instancia *inst = &prontas[idx];
            inst->burst_restante--;

            if (inst->burst_restante == 0) {
                inst->task->completadas++;
                prontas[idx] = prontas[num_prontas - 1];
                num_prontas--;

                fecha_bloco(t + 1, "F");
                bloco_ativo = 0;
            }
        }
    }

    if (bloco_ativo) {
        fecha_bloco(tempo_total, NULL);
    }

    for (int i = 0; i < num_prontas; i++) {
        prontas[i].task->killed++;
    }

    fprintf(saida, "LOST DEADLINES\n");
    for (int i = 0; i < num_tasks; i++) {
        fprintf(saida, "[%s] %d\n", tasks[i].nome, tasks[i].perdidas);
    }

    fprintf(saida, "COMPLETE EXECUTION\n");
    for (int i = 0; i < num_tasks; i++) {
        fprintf(saida, "[%s] %d\n", tasks[i].nome, tasks[i].completadas);
    }

    fprintf(saida, "KILLED\n");
    for (int i = 0; i < num_tasks; i++) {
        fprintf(saida, "[%s] %d\n", tasks[i].nome, tasks[i].killed);
    }

    double utilizacao = 0.0;
    for (int i = 0; i < num_tasks; i++) {
        utilizacao += (double)tasks[i].burst / tasks[i].periodo;
    }
    double bound = num_tasks * (pow(2.0, 1.0 / num_tasks) - 1.0);

    fprintf(saida, "SCHEDULABILITY\n");
    fprintf(saida, "UTILIZATION %.3f\n", utilizacao);
    fprintf(saida, "RM BOUND %.3f\n", bound);

    fclose(saida);

    return 0;
}
