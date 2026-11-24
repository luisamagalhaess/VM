# Gerenciador de Memória Virtual

Implementação em C do projeto *Designing a Virtual Memory Manager*
(Silberschatz et al., *Operating System Concepts*, 10ª ed., p. P-51), com as
modificações pedidas na disciplina:

- memória física com **128 quadros** (frames) de 256 bytes, preenchidos do 0 ao 127;
- substituição de página **FIFO** ou **LRU**;
- TLB de 16 entradas com substituição **FIFO** ou **LRU**;
- busca na TLB com **uma thread por entrada**;
- saída no arquivo `correct.txt`;
- validação dos parâmetros e do arquivo de endereços.

## Requisitos

- Linux, Unix ou macOS (no Windows, via WSL)
- `gcc` e `make`
- Arquivos do professor na mesma pasta do executável:
  `BACKING_STORE.bin` e o arquivo de endereços (ex.: `addresses.txt`)

## Compilação

```sh
make          # gera o executável ./vm (versão final)
make clean    # remove ./vm e correct.txt
```

## Execução

```sh
./vm <arquivo_de_enderecos> <fifo|lru> <fifo|lru>
```

| Parâmetro | Significado |
|---|---|
| 1º | arquivo com um endereço lógico (0 a 65535) por linha |
| 2º | política de substituição de **páginas** (`fifo` ou `lru`) |
| 3º | política de substituição da **TLB** (`fifo` ou `lru`) |

Exemplo (página LRU, TLB FIFO):

```sh
./vm addresses.txt lru fifo
```

Atalho: `make run` executa exatamente esse exemplo.

## Formato da saída (`correct.txt`)

```
Virtual address: 16916 TLB: 0 Physical address: 20 Value: 0
...
Number of Translated Addresses = 1000
Page Faults = 539
Page Fault Rate = 0.539
TLB Hits = 54
TLB Hit Rate = 0.054
```

O campo `TLB` indica a posição da TLB onde a tradução foi encontrada (hit) ou
inserida (miss). Nas versões sem TLB, aparece `-1`.

## Funcionamento

1. **Tradução**: o endereço lógico de 16 bits é dividido em página
   (`endereco / 256`) e deslocamento (`endereco % 256`).
2. **TLB**: a página é procurada primeiro na TLB. Em caso de acerto (hit),
   o quadro sai direto dela.
3. **Tabela de páginas**: em caso de falha na TLB, consulta-se a tabela de
   páginas, que guarda o número do quadro de cada página (ou `SEM_QUADRO`).
4. **Page fault**: se a página não estiver na memória, ela é lida do
   `BACKING_STORE.bin` para o próximo quadro livre (0, 1, ..., 127). Com a
   memória cheia, um quadro é escolhido para substituição:
   - **FIFO**: ponteiro circular que percorre os quadros na ordem de chegada;
   - **LRU**: um relógio lógico avança a cada endereço traduzido, e cada quadro
     anota o valor do relógio no seu último acesso. Sai o quadro com o menor valor.

   A página expulsa é invalidada na tabela de páginas e removida da TLB.
5. **Atualização da TLB**: a nova tradução ocupa uma posição livre ou, com a
   TLB cheia, substitui uma entrada por FIFO (ponteiro circular) ou LRU
   (mesmo relógio lógico).
6. **Resultado**: endereço físico = `quadro * 256 + deslocamento`. O valor é o
   byte com sinal armazenado nessa posição.

Todo o estado da memória (tabela de páginas, quadros, TLB, contadores e
relógio) fica reunido na estrutura `Mmu`, passada por ponteiro às funções.

### Busca na TLB com threads

Para cada endereço são criadas 16 threads (`pthread_create`), uma por entrada
da TLB. Cada thread compara a sua entrada com a página procurada e grava a
resposta na **sua própria** estrutura `TarefaBusca`. Como nenhuma thread escreve
no mesmo lugar que outra, não há condição de corrida e não é preciso mutex.
Depois do `pthread_join` de todas, a thread principal verifica qual entrada
encontrou a página. Se alguma thread não puder ser criada, a comparação
daquela entrada é feita pela thread principal.

## Tratamento de erros

O programa mostra uma mensagem explicativa e encerra com código 1 quando:

- o número de parâmetros é diferente de 3;
- o 2º ou o 3º parâmetro não é `fifo` nem `lru`;
- o arquivo de endereços não existe ou não pode ser lido;
- o arquivo de endereços está vazio ou fora do formato esperado: linha com
  texto, número negativo ou maior que 65535, mais de um número na mesma linha,
  linha em branco no meio do arquivo ou linha longa demais;
- o `BACKING_STORE.bin` não existe ou tem menos de 65536 bytes;
- o `correct.txt` não pode ser criado.

O arquivo de endereços é validado por inteiro antes de qualquer tradução, então
o `correct.txt` nunca é gerado pela metade. Arquivos salvos no Windows (com
`\r\n`) são aceitos.

## Fluxos da avaliação

Cada alvo do Makefile gera o executável `vm` com uma configuração diferente:

| Fluxo | Compilação |
|---|---|
| Sem TLB, 256 quadros | `make sem_tlb_256` |
| Sem TLB, 128 quadros (FIFO / LRU) | `make sem_tlb` |
| TLB sem thread (FIFO / LRU) | `make sem_thread` |
| TLB com thread (**versão final**) | `make` |

## Resultados com o `addresses.txt` (1000 endereços)

| Configuração | Página | TLB | Page Faults | TLB Hits |
|---|---|---|---|---|
| Sem TLB, 256 quadros | — | — | 244 | 0 |
| Sem TLB, 128 quadros | fifo | — | 538 | 0 |
| Sem TLB, 128 quadros | lru | — | 539 | 0 |
| TLB, 128 quadros | fifo | fifo | 538 | 54 |
| TLB, 128 quadros | fifo | lru | 538 | 55 |
| TLB, 128 quadros | lru | fifo | 539 | 54 |
| TLB, 128 quadros | lru | lru | 539 | 55 |

As versões com e sem threads produzem exatamente a mesma saída. A configuração
`lru fifo` gera um `correct.txt` idêntico ao fornecido pelo professor.

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `vm.c` | código-fonte |
| `Makefile` | compilação (`make`), variantes e limpeza (`make clean`) |
| `evidencias.log` | saída do terminal com os testes de cada fluxo e dos erros |
| `README.md` | este documento |