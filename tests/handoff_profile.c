#include <assert.h>
#include <string.h>
#include <stdio.h>
enum { TELA_HOME, TELA_ESCOLHA_PERFIL, MENU_INICIO };
static int tela, logged, unlocked, profile, kept, continued, synced;
static const char *account;
static int sessao_logada(void) { return logged; }
static const char *perfis_dono(void) { return account; }
static int perfis_ativo(void) { return profile; }
static int perfis_pode_dispensar(void) { return unlocked; }
static void perfis_manter_ativo(void) { kept++; }
static void perfilsel_continuar_ativo(void) { continued++; }
static void sync_iniciar(void) { synced++; }
static void menu_definir_destino(int m) { assert(m == MENU_INICIO); }
#include "handoff_profile.inc"
static void reset(void) { tela=TELA_ESCOLHA_PERFIL; logged=unlocked=1; profile=2; account="owner"; kept=continued=synced=0; }
int main(void) {
 reset(); assert(app_handoff_restore_profile("owner",2)); assert(tela==TELA_HOME && kept==1 && continued==1 && synced==1);
 reset(); unlocked=0; assert(!app_handoff_restore_profile("owner",2)); assert(tela==TELA_ESCOLHA_PERFIL && !kept);
 reset(); assert(!app_handoff_restore_profile("other",2)); assert(!kept);
 reset(); assert(!app_handoff_restore_profile("owner",1)); assert(!kept);
 reset(); logged=0; assert(!app_handoff_restore_profile("owner",2)); assert(!kept);
 reset(); tela=TELA_HOME; assert(!app_handoff_restore_profile("owner",2)); assert(!kept);
 reset(); assert(!app_handoff_restore_profile(NULL,2)); assert(!app_handoff_restore_profile("",2));
 puts("handoff profile: PASS (saved matching unlocked profile only)");
}
