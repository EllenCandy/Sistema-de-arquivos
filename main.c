#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fs.h"

void comando_ajuda() {
    printf("\n--- Comandos Disponiveis ---\n");
    printf("formatar           : Formata o disco (APAGA TUDO!)\n");
    printf("ls                 : Lista arquivos e pastas do diretorio atual\n");
    printf("cd <nome>          : Entra em uma pasta (use .. para voltar)\n");
    printf("crpasta <nm>       : Cria uma nova pasta (mkdir)\n");
    printf("rm <nome>          : Remove arquivo ou pasta (vazia)\n");
    printf("importar <PC> <FS> : Copia arquivo do PC para o seu sistema\n");
    printf("exportar <FS> <PC> : Copia arquivo do seu sistema para o PC\n");
    printf("ajuda              : Mostra esta lista\n");
    printf("sair               : Sai do simulador\n");
}

void comando_formatar() {
    int quantidade_setores;
    printf("Digite o tamanho do disco em setores de 512 bytes: ");
    scanf("%d", &quantidade_setores);

    if (quantidade_setores < 40) {
        printf("Erro: Minimo de 40 setores necessario.\n");
        return;
    }

    printf("Formatando...\n");
    formatar_disco(quantidade_setores);

    printf("Total Blocos (4KB): %llu\n", (unsigned long long)total_blocos_disco);
    printf("Formatacao concluida.\n");
}

void comando_listar() {
    printf("\n%-20s | %-4s | %-10s | %-10s\n", "Nome", "Tipo", "Tamanho", "Bloco Ini");
    printf("---------------------------------------------------------------\n");
    int contador_arquivos = 0;
    
    for (int i = 0; i < 64; i++) {
        EntradaDiretorio entrada = ler_entrada_diretorio(i);
        if (entrada.status == STATUS_USADO) {
            char *tipo_str = (entrada.tipo == TIPO_DIRETORIO) ? "DIR" : "ARQ";
            
            printf("%-20s | %-4s | %10u | %10u\n", 
                   entrada.nome_arquivo, tipo_str, 
                   entrada.tamanho_bytes, entrada.bloco_inicial);
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

    if (criar_arquivo(nome_destino, tamanho, TIPO_ARQUIVO) != 0) {
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

void comando_crpasta(const char *nome_pasta) {
    int res = criar_arquivo(nome_pasta, 0, TIPO_DIRETORIO);
    
    if (res == 0) {
        printf("Pasta '%s' criada com sucesso.\n", nome_pasta);
    } else if (res == -EPERM) {
        printf("Erro: Nao permitido criar pasta dentro de pasta.\n");
    } else {
        printf("Erro ao criar pasta (Disco cheio ou nome duplicado).\n");
    }
}

void comando_cd(const char *nome) {
    if (mudar_diretorio(nome) == 0) {
        printf("Entrou em: %s\n", nome);
    } else {
        printf("Pasta nao encontrada.\n");
    }
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

    if (montar_disco()) {
        printf("Disco: %s (Montado)\n", argv[1]);
        printf("Tamanho: %llu blocos\n", (unsigned long long)total_blocos_disco);
        printf("Digite 'ajuda' para ver os comandos.\n");
    } else {
        printf("Disco: %s (NAO FORMATADO)\n", argv[1]);
        printf("Digite 'formatar' para iniciar.\n");
    }

    char comando[20], arg1[100], arg2[100];
    while (1) {
        printf("\nFS> ");
        if (scanf("%s", comando) == EOF) break;

        if (strcmp(comando, "sair") == 0) break;
        else if (strcmp(comando, "ajuda") == 0) comando_ajuda();
        else if (strcmp(comando, "formatar") == 0) comando_formatar();
        else if (strcmp(comando, "ls") == 0) comando_listar();
        else if (strcmp(comando, "rm") == 0) {
            scanf("%s", arg1);
            if(remover_arquivo(arg1) == 0) printf("Removido.\n");
            else printf("Erro.\n");
        }
        else if (strcmp(comando, "importar") == 0) {
            scanf("%s %s", arg1, arg2);
            comando_importar(arg1, arg2);
        }
        else if (strcmp(comando, "exportar") == 0) {
            scanf("%s %s", arg1, arg2);
            comando_exportar(arg1, arg2);
        }
        else if (strcmp(comando, "crpasta") == 0) {
            scanf("%s", arg1);
            comando_crpasta(arg1);
        }
        else if (strcmp(comando, "cd") == 0) {
            scanf("%s", arg1);
            comando_cd(arg1);
        }
        else printf("Comando invalido. Digite 'ajuda'.\n");
    }
    fclose(arquivo_disco);
    return 0;
}