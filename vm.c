#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    POLITICA_INVALIDA = 0,
    POLITICA_FIFO,
    POLITICA_LRU
} Politica;


typedef struct {
    const char *caminho_enderecos;
    Politica troca_paginas;
    Politica troca_tlb;
} Configuracao;

static Politica identificar_politica(const char *texto){
    if (strcmp(texto, "fifo") == 0) return POLITICA_FIFO;
    if (strcmp(texto, "lru") == 0)  return POLITICA_LRU;
    return POLITICA_INVALIDA;
}

static void exibir_ajuda(const char *executavel){
    fprintf(stderr, "Forma de uso: %s <arquivo_de_enderecos> <fifo|lru> <fifo|lru>\n" "  2o parametro: politica de substituicao de paginas\n" "  3o parametro: politica de substituicao da TLB\n" "Exemplo: %s address.txt lru fifo\n", executavel, executavel);
}

static int carregar_configuracao(int qtd, char *param[], Configuracao *cfg){
    if (qtd != 4) {
        fprintf(stderr, "Erro: foram informados %d parametro(s), mas sao necessarios 3.\n", qtd - 1);
        return 0;
    }

    cfg->caminho_enderecos = param[1];
    cfg->troca_paginas = identificar_politica(param[2]);
    cfg->troca_tlb = identificar_politica(param[3]);

    if (cfg->troca_paginas == POLITICA_INVALIDA) {
        fprintf(stderr, "Erro: politica de paginas desconhecida: '%s' (aceito: fifo ou lru).\n", param[2]);
        return 0;
    }
    if (cfg->troca_tlb == POLITICA_INVALIDA) {
        fprintf(stderr, "Erro: politica da TLB desconhecida: '%s' (aceito: fifo ou lru).\n", param[3]);
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[]){
    Configuracao cfg;
    if (!carregar_configuracao(argc, argv, &cfg)) {
        exibir_ajuda(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}