// main.c
// Sistema de Save/Load para a Fazendinha 3D - PPSSPP
// Compatível com PSPSDK

#include <pspkernel.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include "Boiwood.h"   // objetivos e estado do mundo

PSP_MODULE_INFO("Fazendinha3D", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SAVE_PATH "ms0:/PSP/GAME/Fazendinha/save.dat"
#define SAVE_VERSION 1

typedef struct {
    int version;
    float playerX, playerY, playerZ;
    float playerRotY;
    int dinheiro;
    int dia;
    int hora;
    int objetivosCompletos[MAX_OBJETIVOS];
    // adicione mais campos aqui se precisar (inventário, animais, etc.)
} SaveData;

SaveData g_save;

// Inicializa save com valores padrão
void InitSaveDefault(void) {
    memset(&g_save, 0, sizeof(SaveData));
    g_save.version = SAVE_VERSION;
    g_save.playerX = 0.0f;
    g_save.playerY = 0.0f;
    g_save.playerZ = 5.0f;
    g_save.playerRotY = 0.0f;
    g_save.dinheiro = 100;
    g_save.dia = 1;
    g_save.hora = 6; // 6h da manhã
}

// Salva o jogo
int SaveGame(void) {
    SceUID fd = sceIoOpen(SAVE_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return 0;

    // Copia estado atual dos objetivos
    for (int i = 0; i < MAX_OBJETIVOS; i++) {
        g_save.objetivosCompletos[i] = Objetivos_GetStatus(i);
    }

    int written = sceIoWrite(fd, &g_save, sizeof(SaveData));
    sceIoClose(fd);

    return (written == sizeof(SaveData));
}

// Carrega o jogo
int LoadGame(void) {
    SceUID fd = sceIoOpen(SAVE_PATH, PSP_O_RDONLY, 0777);
    if (fd < 0) {
        InitSaveDefault();
        return 0;
    }

    int read = sceIoRead(fd, &g_save, sizeof(SaveData));
    sceIoClose(fd);

    if (read != sizeof(SaveData) || g_save.version != SAVE_VERSION) {
        InitSaveDefault();
        return 0;
    }

    // Aplica os objetivos salvos
    for (int i = 0; i < MAX_OBJETIVOS; i++) {
        Objetivos_SetStatus(i, g_save.objetivosCompletos[i]);
    }

    return 1;
}

// Menu rápido de Save/Load (chama com SELECT + START ou botão dedicado)
void MenuSaveLoad(void) {
    // Aqui você pode desenhar um menu simples na tela
    // Por enquanto só as funções estão prontas para serem chamadas
    // Exemplo de uso:
    // if (botao_salvar) SaveGame();
    // if (botao_carregar) LoadGame();
}

// Exemplo de como integrar no loop principal (você adapta no seu main)
/*
int main(void) {
    // ... inicialização 3D, SCE, etc ...

    InitSaveDefault();
    LoadGame();   // tenta carregar ao iniciar

    while (1) {
        // atualiza controles
        // atualiza player position → grava em g_save.playerX etc.
        // Objetivos_Update();

        // se apertar o botão de save:
        // SaveGame();

        // render 3D...
    }
    return 0;
}
*/