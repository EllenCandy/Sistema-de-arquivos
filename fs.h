#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdio.h>

// --- Constantes do Sistema ---
#define TAMANHO_BLOCO 4096      // Tamanho padrão de alocação (4KB)
#define STATUS_LIVRE   0x00     // 0000 0000: Espaço vazio
#define STATUS_USADO   0x01     // 0000 0001: Espaço ocupado
#define STATUS_APAGADO 0xE5     // 1110 0101: Arquivo deletado logicamente
#define TIPO_ARQUIVO   0        // Identificador para arquivo comum
#define TIPO_DIRETORIO 1        // Identificador para pasta/diretório

// --- Estrutura do Superbloco (Bloco 1) ---
typedef struct {
    uint32_t tamanho_bloco;
    uint64_t total_blocos;
    uint64_t inicio_bitmap;
    uint64_t inicio_raiz;
    uint64_t inicio_dados;
    uint8_t  padding[4060];
} SuperBloco;

// --- Estrutura da Entrada de Diretório (64 bytes) ---
typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t tipo;
    uint32_t bloco_inicial;
    uint32_t tamanho_bytes;
    char nome_arquivo[50];
    uint8_t reservado[4];
} EntradaDiretorio;

// --- Variáveis Globais (Externas) ---
extern FILE *arquivo_disco;
extern uint64_t total_blocos_disco;
extern uint64_t bloco_inicio_dados;
extern uint64_t bloco_inicio_bitmap;
extern uint64_t bloco_inicio_raiz;
extern uint64_t bloco_diretorio_atual;

// --- Protótipos das Funções ---
void formatar_disco(int quantidade_setores);
int montar_disco();
void inicializar_diretorio_atual();

int criar_arquivo(const char *nome, uint32_t tamanho_solicitado, uint8_t tipo);
int remover_arquivo(const char *nome);
int ler_arquivo(const char *nome, uint32_t deslocamento_inicial, uint32_t tamanho_leitura, void *buffer_saida);
int escrever_arquivo(const char *nome, uint32_t deslocamento_inicial, const void *buffer_entrada, uint32_t tamanho_escrita);
int mudar_diretorio(const char *nome);

// Auxiliares
void definir_status_blocos_bitmap(uint64_t bloco_inicial, int quantidade, int status);
int verificar_se_bloco_esta_livre(uint64_t indice_bloco);
EntradaDiretorio ler_entrada_diretorio(int indice);
void salvar_entrada_diretorio(int indice, EntradaDiretorio *entrada);

#endif