#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"

void comando_formatar() {
    int quantidade_setores;
    printf("Digite o tamanho do disco em setores de 512 bytes: ");
    scanf("%d", &quantidade_setores);

    if (quantidade_setores < 32) {
        printf("Erro: Minimo de 32 setores necessario.\n");
        return;
    }

    uint64_t total_bytes = (uint64_t)quantidade_setores * 512;
    total_blocos_disco = total_bytes / TAMANHO_BLOCO;
    
    printf("Formatando...\n");
    printf("Total Bytes: %llu\n", (unsigned long long)total_bytes);
    printf("Total Blocos (4KB): %llu\n", (unsigned long long)total_blocos_disco);

    // Cria/Limpa arquivo
    fseek(arquivo_disco, total_bytes - 1, SEEK_SET);
    fputc(0, arquivo_disco);
    rewind(arquivo_disco);

    calcular_geometria_disco();

    // Zera metadados
    char *buffer_zeros = calloc(TAMANHO_BLOCO, 1);
    for(uint64_t i = 0; i < bloco_inicio_dados; i++) {
        fseek(arquivo_disco, i * TAMANHO_BLOCO, SEEK_SET);
        fwrite(buffer_zeros, TAMANHO_BLOCO, 1, arquivo_disco);
    }
    free(buffer_zeros);

    // Marca sistema como usado
    definir_status_blocos_bitmap(0, bloco_inicio_dados, STATUS_USADO);

    printf("Formatacao concluida.\n");
    printf("- Dados iniciam no bloco: %llu\n", (unsigned long long)bloco_inicio_dados);
}

void comando_listar() {
    printf("\n%-20s | %-10s | %-10s\n", "Nome", "Tamanho", "Bloco Ini");
    printf("----------------------------------------------------\n");
    int contador_arquivos = 0;
    for (int i = 0; i < 64; i++) {
        EntradaDiretorio entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO) {
            printf("%-20s | %10u | %10u\n", entrada.nome_arquivo, entrada.tamanho_bytes, entrada.bloco_inicial);
            contador_arquivos++;
        }
    }
    if (contador_arquivos == 0) printf("(diretorio vazio)\n");
}

void comando_importar(const char *caminho_origem, const char *nome_destino) {
    FILE *arquivo_host = fopen(caminho_origem, "rb");
    if (!arquivo_host) { perror("Erro abrir origem"); return; }

    fseek(arquivo_host, 0, SEEK_END);
    uint32_t tamanho = ftell(arquivo_host);
    rewind(arquivo_host);

    if (criar_arquivo(nome_destino, tamanho) != 0) {
        printf("Erro: Sem espaco ou nome duplicado.\n");
        fclose(arquivo_host);
        return;
    }

    uint8_t *buffer = malloc(TAMANHO_BLOCO);
    uint32_t bytes_lidos, total_escrito = 0;
    
    while ((bytes_lidos = fread(buffer, 1, TAMANHO_BLOCO, arquivo_host)) > 0) {
        escrever_arquivo(nome_destino, total_escrito, buffer, bytes_lidos);
        total_escrito += bytes_lidos;
    }
    free(buffer);
    fclose(arquivo_host);
    printf("Arquivo importado.\n");
}

void comando_exportar(const char *nome_origem_fs, const char *caminho_destino_pc) {
    EntradaDiretorio entrada;
    int encontrado = 0;
    
    for(int i = 0; i < 64; i++) {
        entrada = ler_entrada_diretorio(i);
        if(entrada.status == STATUS_USADO && strcmp(entrada.nome_arquivo, nome_origem_fs) == 0) {
            encontrado = 1; break;
        }
    }
    if(!encontrado) { printf("Arquivo nao encontrado.\n"); return; }

    FILE *arquivo_host = fopen(caminho_destino_pc, "wb");
    if (!arquivo_host) { perror("Erro criar destino"); return; }

    uint8_t *buffer = malloc(TAMANHO_BLOCO);
    uint32_t restante = entrada.tamanho_bytes;
    uint32_t deslocamento = 0;
    
    while(restante > 0) {
        uint32_t tamanho_pedaco = (restante > TAMANHO_BLOCO) ? TAMANHO_BLOCO : restante;
        ler_arquivo(nome_origem_fs, deslocamento, tamanho_pedaco, buffer);
        fwrite(buffer, 1, tamanho_pedaco, arquivo_host);
        deslocamento += tamanho_pedaco;
        restante -= tamanho_pedaco;
    }
    free(buffer);
    fclose(arquivo_host);
    printf("Arquivo exportado.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: sudo %s <dispositivo_ou_imagem>\n", argv[0]);
        return 1;
    }

    arquivo_disco = fopen(argv[1], "r+b");
    if (!arquivo_disco) {
        printf("Arquivo nao existe. Criando novo...\n");
        arquivo_disco = fopen(argv[1], "w+b");
        if(!arquivo_disco) { perror("Erro fatal"); return 1; }
    }

    calcular_geometria_disco();

    printf("Disco: %s\n", argv[1]);
    if (total_blocos_disco > 0) {
        printf("Tamanho: %llu blocos (%llu MB)\n", 
               (unsigned long long)total_blocos_disco, 
               (unsigned long long)((total_blocos_disco * TAMANHO_BLOCO)/(1024*1024)));
    } else {
        printf("Disco nao formatado.\n");
    }

    char comando[20], arg1[100], arg2[100];
    while (1) {
        printf("\nFS> ");
        if (scanf("%s", comando) == EOF) break;

        if (strcmp(comando, "exit") == 0) break;
        else if (strcmp(comando, "format") == 0) comando_formatar();
        else if (strcmp(comando, "ls") == 0) comando_listar();
        else if (strcmp(comando, "remove") == 0) {
            scanf("%s", arg1);
            if(remover_arquivo(arg1) == 0) printf("Removido.\n");
            else printf("Erro.\n");
        }
        else if (strcmp(comando, "create") == 0) {
            scanf("%s %s", arg1, arg2);
            comando_importar(arg1, arg2);
        }
        else if (strcmp(comando, "export") == 0) {
            scanf("%s %s", arg1, arg2);
            comando_exportar(arg1, arg2);
        }
        else printf("Comando invalido.\n");
    }
    fclose(arquivo_disco);
    return 0;
}