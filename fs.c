#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fs.h"

// Garante compatibilidade do fseek para arquivos grandes (>2GB) em Windows/Linux
#ifdef _WIN32
    #define fseek_64 _fseeki64
#else
    #define fseek_64 fseeko
#endif

// Inicialização das variáveis globais
FILE *arquivo_disco = NULL;
uint64_t total_blocos_disco = 0;
uint64_t bloco_inicio_bitmap = 0;
uint64_t bloco_inicio_raiz = 0;
uint64_t bloco_inicio_dados = 0;

// --- Funções Auxiliares (Matemática e Acesso ao Disco) ---

void calcular_geometria_disco() {
    if (!arquivo_disco) return; // Segurança: se não tem arquivo aberto, sai.

    // 1. Descobrir tamanho do arquivo:
    // fseek: Move a "agulha" de leitura para o final (SEEK_END)
    fseek_64(arquivo_disco, 0, SEEK_END);
    
    // ftell: Pergunta "em qual byte a agulha está agora?". Isso dá o tamanho total.
    uint64_t total_bytes_disco = ftell(arquivo_disco);
    
    // rewind: Rebobina a agulha para o byte 0 (início), senão não conseguimos ler mais nada.
    rewind(arquivo_disco);

    if (total_bytes_disco < TAMANHO_BLOCO * 4) { 
        total_blocos_disco = 0; return; // Disco muito pequeno/inválido
    }

    // Calcula quantos blocos de 4KB cabem no disco
    total_blocos_disco = total_bytes_disco / TAMANHO_BLOCO;
    
    // Cálculos de layout conforme especificação (Quantos blocos o Bitmap precisa?)
    uint64_t bytes_necessarios_bitmap = (total_blocos_disco + 7) / 8; // Divisão por 8 arredondada para cima
    uint64_t blocos_para_bitmap = (bytes_necessarios_bitmap + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;

    // Define os endereços iniciais de cada área
    bloco_inicio_bitmap = 2; // Pula Boot(0) e Super(1)
    bloco_inicio_raiz = bloco_inicio_bitmap + blocos_para_bitmap;
    bloco_inicio_dados = bloco_inicio_raiz + 1; // Raiz ocupa fixo 1 bloco
}

// Manipula bits individuais no Bitmap (Marca blocos como USADO ou LIVRE)
void definir_status_blocos_bitmap(uint64_t bloco_inicial, int quantidade, int status) {
    for(int i = 0; i < quantidade; i++) {
        uint64_t bloco_atual = bloco_inicial + i;
        
        // Matemática de bits: Em qual byte e em qual bit desse byte está o bloco?
        uint64_t deslocamento_byte = bloco_atual / 8;
        int deslocamento_bit = bloco_atual % 8;
        
        // Calcula endereço físico no arquivo onde está esse byte do bitmap
        uint64_t endereco_fisico = (bloco_inicio_bitmap * TAMANHO_BLOCO) + deslocamento_byte;

        // Passo 1: Ler o byte atual (Read)
        fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
        uint8_t byte_lido;
        fread(&byte_lido, 1, 1, arquivo_disco);

        // Passo 2: Modificar o bit específico (Modify)
        if(status == STATUS_USADO) 
            byte_lido |= (1 << deslocamento_bit);  // Operador OR liga o bit (1)
        else                       
            byte_lido &= ~(1 << deslocamento_bit); // Operador AND com NOT desliga o bit (0)

        // Passo 3: Escrever de volta (Write)
        fseek_64(arquivo_disco, endereco_fisico, SEEK_SET); // Volta para a posição certa
        fwrite(&byte_lido, 1, 1, arquivo_disco);            // Grava o byte alterado
    }
    fflush(arquivo_disco); // Força salvar no disco físico agora
}

// Verifica se um bit é 0 (Livre)
int verificar_se_bloco_esta_livre(uint64_t indice_bloco) {
    uint64_t deslocamento_byte = indice_bloco / 8;
    int deslocamento_bit = indice_bloco % 8;
    uint64_t endereco_fisico = (bloco_inicio_bitmap * TAMANHO_BLOCO) + deslocamento_byte;
    
    fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
    uint8_t byte_lido;
    fread(&byte_lido, 1, 1, arquivo_disco);
    
    // Retorna verdadeiro (1) se o bit for 0
    return !(byte_lido & (1 << deslocamento_bit));
}

// Lê uma estrutura de 64 bytes da tabela de diretórios
EntradaDiretorio ler_entrada_diretorio(int indice) {
    EntradaDiretorio entrada = {0};
    uint64_t endereco_fisico = (bloco_inicio_raiz * TAMANHO_BLOCO) + (indice * sizeof(EntradaDiretorio));
    
    fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
    fread(&entrada, sizeof(EntradaDiretorio), 1, arquivo_disco);
    
    return entrada;
}

// Salva uma estrutura de 64 bytes na tabela
void salvar_entrada_diretorio(int indice, EntradaDiretorio *entrada) {
    uint64_t endereco_fisico = (bloco_inicio_raiz * TAMANHO_BLOCO) + (indice * sizeof(EntradaDiretorio));
    
    fseek_64(arquivo_disco, endereco_fisico, SEEK_SET);
    fwrite(entrada, sizeof(EntradaDiretorio), 1, arquivo_disco);
    fflush(arquivo_disco);
}

// --- Funções Principais (Lógica do FS) ---

int criar_arquivo(const char *nome, uint32_t tamanho_solicitado) {
    // 1. Arredonda tamanho para cima para saber quantos blocos precisa (teto)
    int blocos_necessarios = (tamanho_solicitado + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;
    if (blocos_necessarios == 0 && tamanho_solicitado > 0) blocos_necessarios = 1; 
    
    int indice_primeiro_bloco_livre = -1;
    uint64_t contador_blocos_livres_consecutivos = 0;
    
    // 2. Algoritmo FIRST FIT (Primeiro Encaixe)
    // Varre o disco procurando N blocos livres seguidos
    for (uint64_t i = 0; i < total_blocos_disco - bloco_inicio_dados; i++) { 
        uint64_t indice_absoluto = bloco_inicio_dados + i; // Pula área do sistema
        
        if (indice_absoluto >= total_blocos_disco) break;

        if(verificar_se_bloco_esta_livre(indice_absoluto)) {
            contador_blocos_livres_consecutivos++;
            // Se achou espaço contíguo suficiente, para a busca
            if (contador_blocos_livres_consecutivos == blocos_necessarios) {
                indice_primeiro_bloco_livre = indice_absoluto - blocos_necessarios + 1;
                break;
            }
        } else {
            contador_blocos_livres_consecutivos = 0; // Resetar contador se achar bloco ocupado
        }
    }

    if (indice_primeiro_bloco_livre < 0) return -ENOSPC; // Erro: Disco cheio ou fragmentado

    // 3. Procura uma "gaveta" vazia no diretório para guardar os metadados
    int indice_diretorio_livre = -1;
    for (int i = 0; i < 64; i++) {
        EntradaDiretorio entrada = ler_entrada_diretorio(i);
        // Recicla gavetas marcadas como LIVRE ou APAGADO (0xE5)
        if (entrada.status == STATUS_LIVRE || entrada.status == STATUS_APAGADO) {
            indice_diretorio_livre = i;
            break;
        }
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) return -EEXIST;
    }

    if (indice_diretorio_livre < 0) return -ENOSPC; // Erro: Diretório (64 arquivos) cheio

    // 4. Marca os blocos como OCUPADOS no bitmap
    if (tamanho_solicitado > 0) {
        definir_status_blocos_bitmap(indice_primeiro_bloco_livre, blocos_necessarios, STATUS_USADO);
    } else {
        indice_primeiro_bloco_livre = 0;
    }

    // 5. Preenche a ficha do arquivo e salva no disco
    EntradaDiretorio nova_entrada = {0};
    strncpy(nova_entrada.nome_arquivo, nome, 49);
    nova_entrada.tamanho_bytes = tamanho_solicitado;
    nova_entrada.bloco_inicial = indice_primeiro_bloco_livre;
    nova_entrada.status        = STATUS_USADO; // 0x01
    
    salvar_entrada_diretorio(indice_diretorio_livre, &nova_entrada);
    return 0;
}

int remover_arquivo(const char *nome) {
    EntradaDiretorio entrada;
    int indice_encontrado = -1;

    // Procura o arquivo pelo nome
    for (int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) {
            indice_encontrado = i;
            break;
        }
    }

    if (indice_encontrado < 0) return -ENOENT;

    // Passo importante: SOFT DELETE
    // 1. Libera o Bitmap (diz que os blocos estão livres para uso futuro)
    // Mas NÃO zeramos os dados físicos (para ser rápido)
    if (entrada.tamanho_bytes > 0) {
        int blocos_ocupados = (entrada.tamanho_bytes + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;
        definir_status_blocos_bitmap(entrada.bloco_inicial, blocos_ocupados, STATUS_LIVRE);
    }

    // 2. Marca a entrada como APAGADA (0xE5)
    entrada.status = STATUS_APAGADO;
    salvar_entrada_diretorio(indice_encontrado, &entrada);

    return 0;
}

// Lê dados do sistema de arquivos para um buffer na memória
int ler_arquivo(const char *nome, uint32_t deslocamento_inicial, uint32_t tamanho_leitura, void *buffer_saida) { 
    EntradaDiretorio entrada;
    int arquivo_encontrado = 0;
    
    // Busca arquivo
    for (int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) {
            arquivo_encontrado = 1; break;
        }
    }
    if (!arquivo_encontrado) return -ENOENT;
    // Verifica se usuário tentou ler além do fim do arquivo
    if (deslocamento_inicial + tamanho_leitura > entrada.tamanho_bytes) return -EINVAL;

    uint8_t *ponteiro_buffer = (uint8_t*)buffer_saida;
    uint32_t bytes_restantes = tamanho_leitura;
    uint32_t posicao_atual = deslocamento_inicial;

    // Loop de Leitura: Lê pedaço por pedaço
    while (bytes_restantes > 0) {
        // Matemática para descobrir qual bloco físico ler
        uint32_t indice_bloco_relativo = posicao_atual / TAMANHO_BLOCO;
        uint32_t bloco_alvo = entrada.bloco_inicial + indice_bloco_relativo;
        
        uint32_t deslocamento_dentro_bloco = posicao_atual % TAMANHO_BLOCO;
        uint64_t endereco_absoluto = (uint64_t)bloco_alvo * TAMANHO_BLOCO + deslocamento_dentro_bloco;

        uint32_t bytes_neste_bloco = TAMANHO_BLOCO - deslocamento_dentro_bloco;
        if (bytes_neste_bloco > bytes_restantes) bytes_neste_bloco = bytes_restantes;

        // Pula para o lugar certo e lê
        fseek_64(arquivo_disco, endereco_absoluto, SEEK_SET);
        fread(ponteiro_buffer, bytes_neste_bloco, 1, arquivo_disco);

        // Avança ponteiros
        ponteiro_buffer += bytes_neste_bloco;
        posicao_atual += bytes_neste_bloco;
        bytes_restantes -= bytes_neste_bloco;
    }
    return 0;
}

// Lógica Complexa: Escrever com possibilidade de Expansão (Sem Goto)
int escrever_arquivo(const char *nome, uint32_t deslocamento_inicial, const void *buffer_entrada, uint32_t tamanho_escrita) { 
    EntradaDiretorio entrada;
    int indice_diretorio = -1;

    // 1. Acha o arquivo
    for (int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome) == 0) {
            indice_diretorio = i;
            break;
        }
    }
    if (indice_diretorio < 0) return -ENOENT;

    // 2. Calcula novo tamanho
    uint32_t novo_tamanho_total = deslocamento_inicial + tamanho_escrita;
    if (novo_tamanho_total < entrada.tamanho_bytes) novo_tamanho_total = entrada.tamanho_bytes; // Mantém se for menor

    // 3. Verifica blocos atuais vs blocos necessários
    int quantidade_blocos_atuais = (entrada.tamanho_bytes + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO; 
    if (entrada.tamanho_bytes == 0) quantidade_blocos_atuais = 0;
    
    int quantidade_blocos_necessarios = (novo_tamanho_total + TAMANHO_BLOCO - 1) / TAMANHO_BLOCO;
    if (quantidade_blocos_necessarios == 0 && novo_tamanho_total > 0) quantidade_blocos_necessarios = 1;

    // DECISÃO: O arquivo precisa crescer fisicamente?
    if (quantidade_blocos_necessarios > quantidade_blocos_atuais) {
        
        // Tenta Expansão Adjacente (Vizinhos livres?)
        int blocos_extras = quantidade_blocos_necessarios - quantidade_blocos_atuais;
        int existe_espaco_adjacente = 1;

        if (quantidade_blocos_atuais > 0) {
            for (int i = 0; i < blocos_extras; i++) {
                // Checa o bloco logo após o final do arquivo atual
                uint64_t proximo_bloco = entrada.bloco_inicial + quantidade_blocos_atuais + i;
                if (proximo_bloco >= total_blocos_disco || !verificar_se_bloco_esta_livre(proximo_bloco)) {
                    existe_espaco_adjacente = 0;
                    break;
                }
            }
        } else {
            existe_espaco_adjacente = 0; // Arquivo vazio começa do zero
        }

        if (existe_espaco_adjacente) {
            // CENÁRIO B: Sim, vizinhos livres! Apenas "estica" o arquivo.
            definir_status_blocos_bitmap(entrada.bloco_inicial + quantidade_blocos_atuais, blocos_extras, STATUS_USADO);
            entrada.tamanho_bytes = novo_tamanho_total;
            salvar_entrada_diretorio(indice_diretorio, &entrada);
        } 
        else {
            // CENÁRIO C: Não cabe aqui. MOVE FILE (Realocação Total).
            // Procura um novo "buraco" no disco que caiba o arquivo INTEIRO novo.
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

            if (novo_bloco_inicio < 0) return -ENOSPC; // Disco cheio

            // Mover Dados Antigos: Copia bloco a bloco para o novo lugar
            if (entrada.tamanho_bytes > 0) {
                void *buffer_temporario = malloc(TAMANHO_BLOCO); // Buffer de 4KB
                for(int k = 0; k < quantidade_blocos_atuais; k++) {
                    // Lê do antigo
                    fseek_64(arquivo_disco, (uint64_t)(entrada.bloco_inicial + k) * TAMANHO_BLOCO, SEEK_SET);
                    fread(buffer_temporario, TAMANHO_BLOCO, 1, arquivo_disco);
                    
                    // Escreve no novo
                    fseek_64(arquivo_disco, (uint64_t)(novo_bloco_inicio + k) * TAMANHO_BLOCO, SEEK_SET);
                    fwrite(buffer_temporario, TAMANHO_BLOCO, 1, arquivo_disco);
                }
                free(buffer_temporario);
                
                // Libera blocos antigos no Bitmap
                definir_status_blocos_bitmap(entrada.bloco_inicial, quantidade_blocos_atuais, STATUS_LIVRE);
            }

            // Marca novos blocos como OCUPADO
            definir_status_blocos_bitmap(novo_bloco_inicio, quantidade_blocos_necessarios, STATUS_USADO);
            
            // Atualiza ponteiro no diretório para o novo local
            entrada.bloco_inicial = novo_bloco_inicio;
            entrada.tamanho_bytes = novo_tamanho_total;
            salvar_entrada_diretorio(indice_diretorio, &entrada);
        }
    } 
    else {
        // CENÁRIO A: Sobrescrita (O arquivo não precisa de novos blocos)
        // Apenas atualiza metadados se o tamanho em bytes tiver aumentado
        if (novo_tamanho_total > entrada.tamanho_bytes) {
            entrada.tamanho_bytes = novo_tamanho_total;
            salvar_entrada_diretorio(indice_diretorio, &entrada);
        }
    }

    // --- Rotina de Escrita Final (Direct Write) ---
    // Agora que garantimos que o espaço existe (seja no mesmo lugar ou novo), gravamos os dados novos.
    
    uint8_t *ponteiro_entrada = (uint8_t*)buffer_entrada;
    uint32_t bytes_a_escrever = tamanho_escrita;
    uint32_t posicao_escrita = deslocamento_inicial;

    while (bytes_a_escrever > 0) {
        // Calcula endereço físico exato
        uint32_t indice_relativo = posicao_escrita / TAMANHO_BLOCO;
        uint32_t bloco_fisico_alvo = entrada.bloco_inicial + indice_relativo;
        uint32_t deslocamento_no_bloco = posicao_escrita % TAMANHO_BLOCO;
        
        uint64_t endereco_disco = (uint64_t)bloco_fisico_alvo * TAMANHO_BLOCO + deslocamento_no_bloco;

        // Escreve até o fim do bloco ou até acabar os dados
        uint32_t bytes_parcial = TAMANHO_BLOCO - deslocamento_no_bloco;
        if (bytes_parcial > bytes_a_escrever) bytes_parcial = bytes_a_escrever;

        // Pula para o local e grava (Direct Write)
        fseek_64(arquivo_disco, endereco_disco, SEEK_SET);
        fwrite(ponteiro_entrada, bytes_parcial, 1, arquivo_disco);

        // Atualiza ponteiros
        ponteiro_entrada += bytes_parcial;
        posicao_escrita += bytes_parcial;
        bytes_a_escrever -= bytes_parcial;
    }

    return 0;
}