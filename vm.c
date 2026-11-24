#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAIOR_ENDERECO   65535u
#define TAM_BUFFER_LINHA 64
#define TAM_PAGINA  256
#define QTD_PAGINAS  256
#ifndef QTD_QUADROS
#define QTD_QUADROS 128
#endif
#define SEM_QUADRO (-1)
#define ARQUIVO_DISCO "BACKING_STORE.bin"
#define ARQUIVO_SAIDA "correct.txt"

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

typedef struct {
    unsigned short *valores;
    size_t qtd; 
    size_t espaco;
} ListaEnderecos;

typedef enum {
    LINHA_OK,
    LINHA_VAZIA,
    LINHA_NAO_NUMERICA,
    LINHA_FORA_DA_FAIXA
} SituacaoLinha;

typedef struct {
    int tabela_paginas[QTD_PAGINAS];
    int dono_do_quadro[QTD_QUADROS];
    signed char ram[QTD_QUADROS][TAM_PAGINA];
    int quadros_ocupados;
    FILE *disco;
    unsigned long faltas_de_pagina;
    unsigned long acertos_tlb;
    int p_fifo;
} Mmu;

typedef struct {
    unsigned logico;
    unsigned pagina;
    unsigned deslocamento;
    int quadro;
    int posicao_tlb;
    unsigned fisico;
    int conteudo;
} Traducao;

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

static int eh_espaco(char c){
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static SituacaoLinha analisar_linha(const char *linha, unsigned *saida){
    const char *p = linha;
    unsigned long acumulado = 0;
    int estourou = 0;

    while (eh_espaco(*p)) p++;
    if (*p == '\0') return LINHA_VAZIA;
    if (*p < '0' || *p > '9') return LINHA_NAO_NUMERICA;

    while (*p >= '0' && *p <= '9') {
        acumulado = acumulado * 10 + (unsigned long)(*p - '0');
        if (acumulado > MAIOR_ENDERECO) {
            estourou = 1;
            acumulado = MAIOR_ENDERECO + 1;
        }
        p++;
    }

    while (eh_espaco(*p)) p++;
    if (*p != '\0') return LINHA_NAO_NUMERICA;
    if (estourou) return LINHA_FORA_DA_FAIXA;

    *saida = (unsigned)acumulado;
    return LINHA_OK;
}

static int anexar_endereco(ListaEnderecos *lista, unsigned valor){
    if (lista->qtd == lista->espaco) {
        size_t novo_espaco = lista->espaco ? lista->espaco * 2 : 512;
        unsigned short *maior = realloc(lista->valores, novo_espaco * sizeof *maior);
        if (maior == NULL) return 0;
        lista->valores = maior;
        lista->espaco  = novo_espaco;
    }
    lista->valores[lista->qtd++] = (unsigned short)valor;
    return 1;
}

static void descartar_enderecos(ListaEnderecos *lista)
{
    free(lista->valores);
    lista->valores    = NULL;
    lista->qtd = 0;
    lista->espaco     = 0;
}

static int importar_enderecos(const char *caminho, ListaEnderecos *lista){
    char linha[TAM_BUFFER_LINHA];
    unsigned numero_linha = 0;
    unsigned primeira_em_branco = 0;
    FILE *arq;

    lista->valores = NULL;
    lista->qtd = 0;
    lista->espaco = 0;

    arq = fopen(caminho, "r");
    if (arq == NULL) {
        fprintf(stderr, "Erro: nao consegui abrir '%s' (%s).\n", caminho, strerror(errno));
        return 0;
    }

    while (fgets(linha, sizeof linha, arq) != NULL) {
        unsigned valor = 0;
        numero_linha++;

        if (strchr(linha, '\n') == NULL && !feof(arq)) {
            fprintf(stderr, "Erro em '%s': a linha %u e comprida demais.\n", caminho, numero_linha);
            goto falhou;
        }

        switch (analisar_linha(linha, &valor)) {
        case LINHA_VAZIA:
            if (primeira_em_branco == 0) primeira_em_branco = numero_linha;
            continue;
        case LINHA_NAO_NUMERICA:
            fprintf(stderr, "Erro em '%s', linha %u: esperado um unico numero inteiro "
                    "entre 0 e %u.\n", caminho, numero_linha, MAIOR_ENDERECO);
            goto falhou;
        case LINHA_FORA_DA_FAIXA:
            fprintf(stderr, "Erro em '%s', linha %u: endereco maior que %u.\n",
                    caminho, numero_linha, MAIOR_ENDERECO);
            goto falhou;
        case LINHA_OK:
            break;
        }

        if (primeira_em_branco != 0) {
            fprintf(stderr, "Erro em '%s': linha %u esta em branco no meio do arquivo.\n",
                    caminho, primeira_em_branco);
            goto falhou;
        }
        if (!anexar_endereco(lista, valor)) {
            fprintf(stderr, "Erro: memoria insuficiente para guardar os enderecos.\n");
            goto falhou;
        }
    }

    if (ferror(arq)) {
        fprintf(stderr, "Erro: falha ao ler '%s' (e um arquivo de texto?).\n", caminho);
        goto falhou;
    }
    if (lista->qtd == 0) {
        fprintf(stderr, "Erro: '%s' nao contem nenhum endereco.\n", caminho);
        goto falhou;
    }

    fclose(arq);
    return 1;

falhou:
    fclose(arq);
    descartar_enderecos(lista);
    return 0;
}

static void preparar_mmu(Mmu *mmu){
    int i;
    for (i = 0; i < QTD_PAGINAS; i++) mmu->tabela_paginas[i] = SEM_QUADRO;
    for (i = 0; i < QTD_QUADROS; i++) mmu->dono_do_quadro[i] = -1;
    mmu->quadros_ocupados = 0;
    mmu->p_fifo = 0;
    mmu->disco = NULL;
    mmu->faltas_de_pagina = 0;
    mmu->acertos_tlb = 0;
}

static int conectar_disco(Mmu *mmu){
    long tam;
    mmu->disco = fopen(ARQUIVO_DISCO, "rb");
    if (mmu->disco == NULL) {
        fprintf(stderr, "Erro: nao consegui abrir '%s' (%s). Ele precisa estar na pasta " "de onde o programa e executado.\n", ARQUIVO_DISCO, strerror(errno));
        return 0;
    }
    fseek(mmu->disco, 0, SEEK_END);
    tam = ftell(mmu->disco);
    if (tam < (long)QTD_PAGINAS * TAM_PAGINA){
        fprintf(stderr, "Erro: '%s' tem %ld bytes, mas precisa de pelo menos %d.\n", ARQUIVO_DISCO, tam, QTD_PAGINAS * TAM_PAGINA);
        fclose(mmu->disco);
        mmu->disco = NULL;
        return 0;
    }
    return 1;
}

static int trazer_do_disco(Mmu *mmu, unsigned pagina, int quadro){
    if (fseek(mmu->disco, (long)pagina * TAM_PAGINA, SEEK_SET) != 0 || fread(mmu->ram[quadro], 1, TAM_PAGINA, mmu->disco) != TAM_PAGINA){
        fprintf(stderr, "Erro: leitura da pagina %u em '%s' falhou.\n", pagina, ARQUIVO_DISCO);
        return 0;
    }
    return 1;
}

static int vitima_fifo(Mmu *mmu){
    int alvo = mmu->p_fifo;
    mmu->p_fifo = (mmu->p_fifo + 1) % QTD_QUADROS;
    return alvo;
}

static int atender_falta(Mmu *mmu, unsigned pagina){
    int quadro;
    if (mmu->quadros_ocupados < QTD_QUADROS){
        quadro = mmu->quadros_ocupados++;
    } else {
        int antiga = 0;
        quadro = vitima_fifo(mmu);
        antiga = mmu->dono_do_quadro[quadro];
        if (antiga != -1){
            mmu->tabela_paginas[antiga] = SEM_QUADRO;
        }
    }

    if (!trazer_do_disco(mmu, pagina, quadro)){
        return SEM_QUADRO;
    }

    mmu->tabela_paginas[pagina] = quadro;
    mmu->dono_do_quadro[quadro] = (int)pagina;
    mmu->faltas_de_pagina++;
    return quadro;
}

static int traduzir(Mmu *mmu, unsigned logico, Traducao *t){
    t->logico = logico;
    t->pagina = logico / TAM_PAGINA;
    t->deslocamento = logico % TAM_PAGINA;
    t->posicao_tlb  = -1;
    t->quadro = mmu->tabela_paginas[t->pagina];
    if (t->quadro == SEM_QUADRO){
        t->quadro = atender_falta(mmu, t->pagina);
        if (t->quadro == SEM_QUADRO){
            return 0;
        }
    }
    t->fisico = (unsigned)t->quadro * TAM_PAGINA + t->deslocamento;
    t->conteudo = mmu->ram[t->quadro][t->deslocamento];
    return 1;
}

static void escrever_traducao(FILE *saida, const Traducao *t){
    fprintf(saida, "Virtual address: %u TLB: %d Physical address: %u Value: %d\n", t->logico, t->posicao_tlb, t->fisico, t->conteudo);
}

static void escrever_estatisticas(FILE *saida, const Mmu *mmu, size_t total){
    fprintf(saida, "Number of Translated Addresses = %zu\n", total);
    fprintf(saida, "Page Faults = %lu\n", mmu->faltas_de_pagina);
    fprintf(saida, "Page Fault Rate = %.3f\n", (double)mmu->faltas_de_pagina / (double)total);
    fprintf(saida, "TLB Hits = %lu\n", mmu->acertos_tlb);
    fprintf(saida, "TLB Hit Rate = %.3f\n", (double)mmu->acertos_tlb / (double)total);
}

static int simular(Mmu *mmu, const ListaEnderecos *lista){
    FILE *saida = fopen(ARQUIVO_SAIDA, "w");
    size_t i;
    if (saida == NULL){
        fprintf(stderr, "Erro: nao consegui criar '%s' (%s).\n", ARQUIVO_SAIDA, strerror(errno));
        return 0;
    }
    for (i = 0; i < lista->qtd; i++){
        Traducao t;
        if (!traduzir(mmu, lista->valores[i], &t)){
            fclose(saida);
            return 0;
        }
        escrever_traducao(saida, &t);
    }
    escrever_estatisticas(saida, mmu, lista->qtd);
    fclose(saida);
    return 1;
}

int main(int argc, char *argv[]){
    Configuracao cfg;
    ListaEnderecos enderecos;
    static Mmu mmu;
    int deu_certo;
    if (!carregar_configuracao(argc, argv, &cfg)){
        exibir_ajuda(argv[0]);
        return EXIT_FAILURE;
    }

    if (!importar_enderecos(cfg.caminho_enderecos, &enderecos)){
        return EXIT_FAILURE;
    }

    preparar_mmu(&mmu);
    if (!conectar_disco(&mmu)){
        descartar_enderecos(&enderecos);
        return EXIT_FAILURE;
    }
    deu_certo = simular(&mmu, &enderecos);
    fclose(mmu.disco);
    descartar_enderecos(&enderecos);
    return deu_certo ? EXIT_SUCCESS : EXIT_FAILURE;
}