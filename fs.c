#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fs.h"

// Macro para compatibilidade Windows/Linux
#ifdef _WIN32
    #define fseek_64 _fseeki64
#else
    #define fseek_64 fseeko
#endif

// Definição das variáveis globais
FILE *arquivo_disco = NULL;
uint64_t total_blocos_disco = 0;
uint64_t bloco_inicio_bitmap = 0;
uint64_t bloco_inicio_raiz = 0;
uint64_t bloco_inicio_dados = 0;
uint64_t bloco_diretorio_atual = 0;

// --- Funções Auxiliares ---

void inicializar_diretorio_atual() {
    bloco_diretorio_atual = bloco_inicio_raiz;
}

void definir_status_blocos_bitmap(uint64_t bloco_inicial, int quantidade, int status) {
    for(int i = 0; i < quantidade; i++) {
        uint64_t bloco_atual = bloco_inicial + i;
        uint64_t deslocamento_byte = bloco_atual / 8;
        int deslocamento_bit = bloco_atual % 8;
        uint64_t endereco_fisico = (bloco_inicio_bitmap * TAMANHO_BLOCO) + deslocamento_byte;

        fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
        uint8_t byte_lido;
        fread(&byte_lido, 1, 1, arquivo_disco);

        if(status == STATUS_USADO) 
            byte_lido |= (1 << deslocamento_bit);
        else                       
            byte_lido &= ~(1 << deslocamento_bit);

        fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
        fwrite(&byte_lido, 1, 1, arquivo_disco);
    }
    fflush(arquivo_disco);
}

int verificar_se_bloco_esta_livre(uint64_t indice_bloco) {
    uint64_t deslocamento_byte = indice_bloco / 8;
    int deslocamento_bit = indice_bloco % 8;
    uint64_t endereco_fisico = (bloco_inicio_bitmap * TAMANHO_BLOCO) + deslocamento_byte;
    
    fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
    uint8_t byte_lido;
    fread(&byte_lido, 1, 1, arquivo_disco);
    
    return !(byte_lido & (1 << deslocamento_bit));
}

EntradaDiretorio ler_entrada_diretorio(int indice) {
    EntradaDiretorio entrada = {0};
    uint64_t endereco_fisico = (bloco_diretorio_atual * TAMANHO_BLOCO) + (indice * sizeof(EntradaDiretorio));
    
    fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
    fread(&entrada, sizeof(EntradaDiretorio), 1, arquivo_disco);
    return entrada;
}

void salvar_entrada_diretorio(int indice, EntradaDiretorio *entrada) {
    uint64_t endereco_fisico = (bloco_diretorio_atual * TAMANHO_BLOCO) + (indice * sizeof(EntradaDiretorio));
    fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
    fwrite(entrada, sizeof(EntradaDiretorio), 1, arquivo_disco);
    fflush(arquivo_disco);
}

// --- Funções Principais ---

void formatar_disco(int quantidade_setores) {
    uint64_t total_bytes = (uint64_t)quantidade_setores * 512;
    uint64_t total_blocos = total_bytes / TAMANHO_BLOCO;
    
    uint64_t bytes_mapa = (total_blocos + 7) / 8;
    uint64_t blocos_mapa = (bytes_mapa + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;

    uint64_t inicio_bitmap = 2;
    uint64_t inicio_raiz = inicio_bitmap + blocos_mapa;
    uint64_t inicio_dados = inicio_raiz + 1;

    SuperBloco sb = {0};
    sb.tamanho_bloco = TAMANHO_BLOCO;
    sb.total_blocos = total_blocos;
    sb.inicio_bitmap = inicio_bitmap;
    sb.inicio_raiz = inicio_raiz;
    sb.inicio_dados = inicio_dados;

    fseek_64(arquivo_disco, total_bytes - 1, SEEK_SET);
    fputc(0, arquivo_disco); 
    
    char *zeros = calloc(TAMANHO_BLOCO, 1);
    for(uint64_t i=0; i < inicio_dados; i++) {
        fseek_64(arquivo_disco, i * TAMANHO_BLOCO, SEEK_SET);
        fwrite(zeros, TAMANHO_BLOCO, 1, arquivo_disco);
    }
    free(zeros);

    fseek_64(arquivo_disco, 1 * TAMANHO_BLOCO, SEEK_SET);
    fwrite(&sb, sizeof(SuperBloco), 1, arquivo_disco);
    fflush(arquivo_disco);

    total_blocos_disco = total_blocos;
    bloco_inicio_bitmap = inicio_bitmap;
    bloco_inicio_raiz = inicio_raiz;
    bloco_inicio_dados = inicio_dados;
    
    inicializar_diretorio_atual();
    definir_status_blocos_bitmap(0, inicio_dados, STATUS_USADO);
}

int montar_disco() {
    if (!arquivo_disco) return 0;

    SuperBloco sb;
    fseek_64(arquivo_disco, 1 * TAMANHO_BLOCO, SEEK_SET);
    if (fread(&sb, sizeof(SuperBloco), 1, arquivo_disco) != 1) return 0;

    total_blocos_disco = sb.total_blocos;
    bloco_inicio_bitmap = sb.inicio_bitmap;
    bloco_inicio_raiz = sb.inicio_raiz;
    bloco_inicio_dados = sb.inicio_dados;
    
    inicializar_diretorio_atual();
    return 1;
}

int criar_arquivo(const char *nome, uint32_t tamanho_solicitado, uint8_t tipo) {
    if (tipo == TIPO_DIRETORIO && bloco_diretorio_atual != bloco_inicio_raiz) {
        return -EPERM;
    }

    if (tipo == TIPO_DIRETORIO) {
        tamanho_solicitado = TAMANHO_BLOCO;
    }

    int blocos_necessarios = (tamanho_solicitado + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;
    if (blocos_necessarios == 0 && tamanho_solicitado > 0) blocos_necessarios = 1; 
    
    int indice_primeiro_bloco_livre = -1;
    uint64_t contador_blocos_livres_consecutivos = 0;
    
    for (uint64_t i = 0; i < total_blocos_disco - bloco_inicio_dados; i++) { 
        uint64_t indice_absoluto = bloco_inicio_dados + i;
        if (indice_absoluto >= total_blocos_disco) break;

        if(verificar_se_bloco_esta_livre(indice_absoluto)) {
            contador_blocos_livres_consecutivos++;
            if (contador_blocos_livres_consecutivos == blocos_necessarios) {
                indice_primeiro_bloco_livre = indice_absoluto - blocos_necessarios + 1;
                break;
            }
        } else {
            contador_blocos_livres_consecutivos = 0;
        }
    }

    if (indice_primeiro_bloco_livre < 0) return -ENOSPC;

    int indice_diretorio_livre = -1;
    for (int i = 0; i < 64; i++) {
        EntradaDiretorio entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_LIVRE || entrada.status == STATUS_APAGADO) {
            indice_diretorio_livre = i;
            break;
        }
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) return -EEXIST;
    }

    if (indice_diretorio_livre < 0) return -ENOSPC;

    if (tamanho_solicitado > 0) {
        definir_status_blocos_bitmap(indice_primeiro_bloco_livre, blocos_necessarios, STATUS_USADO);
    } else {
        indice_primeiro_bloco_livre = 0;
    }

    if (tipo == TIPO_DIRETORIO) {
        void *buffer_zeros = calloc(1, TAMANHO_BLOCO);
        if (buffer_zeros) {
            uint64_t endereco_fisico = (uint64_t)indice_primeiro_bloco_livre * TAMANHO_BLOCO;
            fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
            fwrite(buffer_zeros, TAMANHO_BLOCO, 1, arquivo_disco);
            free(buffer_zeros);
        }
    }

    EntradaDiretorio nova_entrada = {0};
    strncpy(nova_entrada.nome_arquivo, nome, 49);
    nova_entrada.tamanho_bytes = tamanho_solicitado;
    nova_entrada.bloco_inicial = indice_primeiro_bloco_livre;
    nova_entrada.status        = STATUS_USADO;
    nova_entrada.tipo          = tipo;
    
    salvar_entrada_diretorio(indice_diretorio_livre, &nova_entrada);
    return 0;
}

int remover_arquivo(const char *nome) {
    EntradaDiretorio entrada;
    int indice_encontrado = -1;

    for (int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) {
            indice_encontrado = i;
            break;
        }
    }

    if (indice_encontrado < 0) return -ENOENT;
    
    if (entrada.tamanho_bytes > 0) {
        int blocos_ocupados = (entrada.tamanho_bytes + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;
        definir_status_blocos_bitmap(entrada.bloco_inicial, blocos_ocupados, STATUS_LIVRE);
    }

    entrada.status = STATUS_APAGADO;
    salvar_entrada_diretorio(indice_encontrado, &entrada);

    return 0;
}

int mudar_diretorio(const char *nome) {
    if (strcmp(nome, "..") == 0 || strcmp(nome, "/") == 0) {
        inicializar_diretorio_atual();
        return 0;
    }

    EntradaDiretorio entrada;
    int encontrado = 0;

    for (int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO && 
            strcmp(entrada.nome_arquivo, nome) == 0 && 
            entrada.tipo == TIPO_DIRETORIO) {
            encontrado = 1;
            break;
        }
    }

    if (!encontrado) return -ENOENT;

    bloco_diretorio_atual = entrada.bloco_inicial;
    return 0;
}

int ler_arquivo(const char *nome, uint32_t deslocamento_inicial, uint32_t tamanho_leitura, void *buffer_saida) { 
    EntradaDiretorio entrada;
    int arquivo_encontrado = 0;
    
    for (int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) {
            arquivo_encontrado = 1; break;
        }
    }
    if (!arquivo_encontrado) return -ENOENT;
    if (deslocamento_inicial + tamanho_leitura > entrada.tamanho_bytes) return -EINVAL;

    uint8_t *ponteiro_buffer = (uint8_t*)buffer_saida;
    uint32_t bytes_restantes = tamanho_leitura;
    uint32_t posicao_atual = deslocamento_inicial;

    while (bytes_restantes > 0) {
        uint32_t indice_bloco_relativo = posicao_atual / TAMANHO_BLOCO;
        uint32_t bloco_alvo = entrada.bloco_inicial + indice_bloco_relativo;
        uint32_t deslocamento_dentro_bloco = posicao_atual % TAMANHO_BLOCO;
        uint64_t endereco_absoluto = (uint64_t)bloco_alvo * TAMANHO_BLOCO + deslocamento_dentro_bloco;

        uint32_t bytes_neste_bloco = TAMANHO_BLOCO - deslocamento_dentro_bloco;
        if (bytes_neste_bloco > bytes_restantes) bytes_neste_bloco = bytes_restantes;

        fseek_64(arquivo_disco, endereco_absoluto, SEEK_SET);
        fread(ponteiro_buffer, bytes_neste_bloco, 1, arquivo_disco);

        ponteiro_buffer += bytes_neste_bloco;
        posicao_atual += bytes_neste_bloco;
        bytes_restantes -= bytes_neste_bloco;
    }
    return 0;
}

int escrever_arquivo(const char *nome, uint32_t deslocamento_inicial, const void *buffer_entrada, uint32_t tamanho_escrita) { 
    EntradaDiretorio entrada;
    int indice_diretorio = -1;

    for (int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) {
            indice_diretorio = i;
            break;
        }
    }
    if (indice_diretorio < 0) return -ENOENT;

    uint32_t novo_tamanho_total = deslocamento_inicial + tamanho_escrita;
    if (novo_tamanho_total < entrada.tamanho_bytes) novo_tamanho_total = entrada.tamanho_bytes;

    int quantidade_blocos_atuais = (entrada.tamanho_bytes + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO; 
    if (entrada.tamanho_bytes == 0) quantidade_blocos_atuais = 0;
    
    int quantidade_blocos_necessarios = (novo_tamanho_total + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;
    if (quantidade_blocos_necessarios == 0 && novo_tamanho_total > 0) quantidade_blocos_necessarios = 1;

    if (quantidade_blocos_necessarios > quantidade_blocos_atuais) {
        int blocos_extras = quantidade_blocos_necessarios - quantidade_blocos_atuais;
        int existe_espaco_adjacente = 1;

        if (quantidade_blocos_atuais > 0) {
            for (int i = 0; i < blocos_extras; i++) {
                uint64_t proximo_bloco = entrada.bloco_inicial + quantidade_blocos_atuais + i;
                if (proximo_bloco >= total_blocos_disco || !verificar_se_bloco_esta_livre(proximo_bloco)) {
                    existe_espaco_adjacente = 0;
                    break;
                }
            }
        } else {
            existe_espaco_adjacente = 0;
        }

        if (existe_espaco_adjacente) {
            definir_status_blocos_bitmap(entrada.bloco_inicial + quantidade_blocos_atuais, blocos_extras, STATUS_USADO);
            entrada.tamanho_bytes = novo_tamanho_total;
            salvar_entrada_diretorio(indice_diretorio, &entrada);
        } 
        else {
            int novo_bloco_inicio = -1;
            uint64_t contagem_livres = 0;

            for (uint64_t i = 0; i < total_blocos_disco - bloco_inicio_dados; i++) {
                uint64_t bloco_candidato = bloco_inicio_dados + i;
                if (verificar_se_bloco_esta_livre(bloco_candidato)) {
                    contagem_livres++;
                    if (contagem_livres == quantidade_blocos_necessarios) {
                        novo_bloco_inicio = bloco_candidato - quantidade_blocos_necessarios + 1;
                        break;
                    }
                } else {
                    contagem_livres = 0;
                }
            }

            if (novo_bloco_inicio < 0) return -ENOSPC;

            if (entrada.tamanho_bytes > 0) {
                void *buffer_temporario = malloc(TAMANHO_BLOCO);
                for(int k = 0; k < quantidade_blocos_atuais; k++) {
                    fseek_64(arquivo_disco, (uint64_t)(entrada.bloco_inicial + k) * TAMANHO_BLOCO, SEEK_SET);
                    fread(buffer_temporario, TAMANHO_BLOCO, 1, arquivo_disco);
                    fseek_64(arquivo_disco, (uint64_t)(novo_bloco_inicio + k) * TAMANHO_BLOCO, SEEK_SET);
                    fwrite(buffer_temporario, TAMANHO_BLOCO, 1, arquivo_disco);
                }
                free(buffer_temporario);
                definir_status_blocos_bitmap(entrada.bloco_inicial, quantidade_blocos_atuais, STATUS_LIVRE);
            }

            definir_status_blocos_bitmap(novo_bloco_inicio, quantidade_blocos_necessarios, STATUS_USADO);
            entrada.bloco_inicial = novo_bloco_inicio;
            entrada.tamanho_bytes = novo_tamanho_total;
            salvar_entrada_diretorio(indice_diretorio, &entrada);
        }
    } 
    else {
        if (novo_tamanho_total > entrada.tamanho_bytes) {
            entrada.tamanho_bytes = novo_tamanho_total;
            salvar_entrada_diretorio(indice_diretorio, &entrada);
        }
    }

    uint8_t *ponteiro_entrada = (uint8_t*)buffer_entrada;
    uint32_t bytes_a_escrever = tamanho_escrita;
    uint32_t posicao_escrita = deslocamento_inicial;

    while (bytes_a_escrever > 0) {
        uint32_t indice_relativo = posicao_escrita / TAMANHO_BLOCO;
        uint32_t bloco_fisico_alvo = entrada.bloco_inicial + indice_relativo;
        uint32_t deslocamento_no_bloco = posicao_escrita % TAMANHO_BLOCO;
        uint64_t endereco_disco = (uint64_t)bloco_fisico_alvo * TAMANHO_BLOCO + deslocamento_no_bloco;

        uint32_t bytes_parcial = TAMANHO_BLOCO - deslocamento_no_bloco;
        if (bytes_parcial > bytes_a_escrever) bytes_parcial = bytes_a_escrever;

        fseek_64(arquivo_disco, endereco_disco, SEEK_SET);
        fwrite(ponteiro_entrada, bytes_parcial, 1, arquivo_disco);

        ponteiro_entrada += bytes_parcial;
        posicao_escrita += bytes_parcial;
        bytes_a_escrever -= bytes_parcial;
    }

    return 0;
}