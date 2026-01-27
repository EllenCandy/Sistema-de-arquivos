#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdio.h>

// Definições Globais
#define TAMANHO_BLOCO 4096
#define STATUS_LIVRE   0x00
#define STATUS_USADO   0x01
#define STATUS_APAGADO 0xE5
#define TAMANHO_DIRETORIO 64

// Estrutura Compactada (64 bytes) com nomes em português
typedef struct __attribute__((packed)) {
    uint8_t status;             // 0x01=Usado, 0xE5=Apagado
    uint8_t tipo;               // Tipo do arquivo
    uint32_t bloco_inicial;     // Onde começa (índice absoluto)
    uint32_t tamanho_bytes;     // Tamanho em bytes
    char nome_arquivo[50];      // Nome
    uint8_t reservado[4];       // Padding
} EntradaDiretorio;

// Variáveis globais (definidas em fs.c)
extern FILE *arquivo_disco;
extern uint64_t total_blocos_disco;
extern uint64_t bloco_inicio_dados;
extern uint64_t bloco_inicio_bitmap;
extern uint64_t bloco_inicio_raiz;

// Funções do Kernel do FS (Nomes descritivos)
int criar_arquivo(const char *nome, uint32_t tamanho_solicitado);
int remover_arquivo(const char *nome);
int ler_arquivo(const char *nome, uint32_t deslocamento_inicial, uint32_t tamanho_leitura, void *buffer_saida);
int escrever_arquivo(const char *nome, uint32_t deslocamento_inicial, const void *buffer_entrada, uint32_t tamanho_escrita);

// Funções Auxiliares
void definir_status_blocos_bitmap(uint64_t bloco_inicial, int quantidade, int status);
int verificar_se_bloco_esta_livre(uint64_t indice_bloco);
EntradaDiretorio ler_entrada_diretorio(int indice);
void salvar_entrada_diretorio(int indice, EntradaDiretorio *entrada);
void calcular_geometria_disco(); 

#endif