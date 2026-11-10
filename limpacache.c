/*
 * limpacache - Limpa o cache de memória do sistema
 *
 * Baseado em:
 *   https://helpsysadmin.com.br/blog/limpar-cache-memoria-linux-guia-completo/
 *
 * O que o guia descreve e este programa executa (Linux):
 *   1. Mostrar o uso de memória (equivalente a `free -m -t`)       [secao 6]
 *   2. Executar `sync` para gravar "dirty pages" no disco          [passo 1]
 *   3. Escrever 1, 2 ou 3 em /proc/sys/vm/drop_caches como root    [secao 2 e passo 2]
 *        1 = PageCache | 2 = Dentries e Inodes | 3 = tudo
 *   4. Mostrar o uso de memória novamente e o que foi liberado
 *   5. Opcional: consultar/ajustar vm.swappiness                   [secao 7]
 *   6. Opcional: reiniciar o swap (swapoff -a && swapon -a)        [FAQ]
 *   7. Avisos: limpar todo dia nao e recomendado; o sistema fica
 *      mais lento por alguns instantes apos a limpeza              [secao 5 e FAQ]
 *
 * Plataformas:
 *   - Linux (Linux Mint, Ubuntu, Debian, Fedora, Arch, openSUSE...): usa /proc
 *     do kernel, portanto o MESMO codigo serve para todas as distros.
 *   - Windows: o Windows nao possui drop_caches. O equivalente e feito com
 *     NtSetSystemInformation (flush da lista modificada, purge da standby list
 *     e esvaziamento dos working sets). Requer Administrador.
 *
 * Compilar (Linux):    gcc -O2 -Wall -Wextra -o limpacache limpacache.c
 * Compilar (Windows):  x86_64-w64-mingw32-gcc -O2 -o limpacache.exe limpacache.c -lpsapi
 *                      (ou, no MSVC: cl limpacache.c psapi.lib advapi32.lib)

 *# Linux Mint (e Ubuntu/Debian): sudo apt install build-essential
 *make
 *sudo make install      # instala em /usr/local/bin

 *limpacache             # limpa tudo (modo 3), pede confirmação
 *limpacache -m 1 -y     # só PageCache, sem perguntar
 *limpacache --status    # só mostra a memória, não precisa de root
 *limpacache --no-clean --swappiness 10
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSAO "1.0.0"

#if defined(_WIN32)
  #define PLATAFORMA_WINDOWS 1
  #include <windows.h>
  #include <psapi.h>
#elif defined(__linux__)
  #define PLATAFORMA_LINUX 1
  #include <unistd.h>
  #include <errno.h>
#else
  #error "Plataforma nao suportada: use Linux ou Windows."
#endif

/* ------------------------------------------------------------------ */
/* Tipos e utilitarios comuns                                          */
/* ------------------------------------------------------------------ */

typedef unsigned long long u64;

typedef struct {
    u64 total;       /* RAM total                          (bytes) */
    u64 livre;       /* RAM livre                          (bytes) */
    u64 disponivel;  /* RAM disponivel p/ aplicativos      (bytes) */
    u64 cache;       /* buffers + cache (page cache etc.)  (bytes) */
    u64 swap_total;  /* swap total                         (bytes) */
    u64 swap_livre;  /* swap livre                         (bytes) */
} MemInfo;

typedef struct {
    int modo;             /* 1, 2 ou 3 (padrao 3)               */
    int sim;              /* -y : nao pedir confirmacao         */
    int somente_status;   /* --status                           */
    int sem_limpeza;      /* --no-clean                         */
    int reset_swap;       /* --reset-swap                       */
    int swappiness;       /* --swappiness N (-1 = nao alterar)  */
} Opcoes;

static double mib(u64 bytes) { return (double)bytes / (1024.0 * 1024.0); }

static void imprime_mem(const char *titulo, const MemInfo *m)
{
    printf("%s\n", titulo);
    printf("  RAM total      : %10.1f MiB\n", mib(m->total));
    printf("  RAM livre      : %10.1f MiB\n", mib(m->livre));
    printf("  RAM disponivel : %10.1f MiB\n", mib(m->disponivel));
    printf("  buff/cache     : %10.1f MiB\n", mib(m->cache));
#ifdef PLATAFORMA_LINUX
    printf("  Swap           : %10.1f MiB usados de %.1f MiB\n",
           mib(m->swap_total - m->swap_livre), mib(m->swap_total));
#endif
}

static void imprime_ajuda(const char *prog)
{
    printf(
"limpacache %s - limpa o cache de memoria do sistema\n\n"
"Uso: %s [opcoes]\n\n"
"Opcoes:\n"
"  -m, --mode N        1 = PageCache | 2 = Dentries e Inodes | 3 = tudo (padrao)\n"
"  -y, --yes           nao pedir confirmacao\n"
"  -s, --status        apenas mostra memoria%s, sem alterar nada\n"
"      --no-clean      nao limpar o cache (util com --swappiness/--reset-swap)\n"
"      --swappiness N  ajusta vm.swappiness (0-100) ate o proximo boot (Linux)\n"
"      --reset-swap    swapoff -a && swapon -a, se houver RAM suficiente (Linux)\n"
"  -h, --help          mostra esta ajuda\n"
"  -V, --version       mostra a versao\n\n"
"Exemplos:\n"
"  %s                  limpa tudo (modo 3)\n"
"  %s -m 1             limpa apenas o PageCache\n"
"  %s -s               mostra o estado da memoria\n"
"  %s --no-clean --swappiness 10\n",
        VERSAO, prog,
#ifdef PLATAFORMA_LINUX
        " e swappiness",
#else
        "",
#endif
        prog, prog, prog, prog);
}

static int parse_int(const char *s, int min, int max, int *out)
{
    char *fim = NULL;
    long v;
    if (!s || !*s) return 0;
    v = strtol(s, &fim, 10);
    if (*fim != '\0' || v < min || v > max) return 0;
    *out = (int)v;
    return 1;
}

static int parse_args(int argc, char **argv, Opcoes *o)
{
    int i;
    o->modo = 3; o->sim = 0; o->somente_status = 0;
    o->sem_limpeza = 0; o->reset_swap = 0; o->swappiness = -1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            imprime_ajuda(argv[0]); exit(0);
        } else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
            printf("limpacache %s\n", VERSAO); exit(0);
        } else if (!strcmp(a, "-y") || !strcmp(a, "--yes")) {
            o->sim = 1;
        } else if (!strcmp(a, "-s") || !strcmp(a, "--status")) {
            o->somente_status = 1;
        } else if (!strcmp(a, "--no-clean")) {
            o->sem_limpeza = 1;
        } else if (!strcmp(a, "--reset-swap")) {
            o->reset_swap = 1;
        } else if (!strcmp(a, "-m") || !strcmp(a, "--mode")) {
            if (i + 1 >= argc || !parse_int(argv[++i], 1, 3, &o->modo)) {
                fprintf(stderr, "Erro: --mode aceita 1, 2 ou 3.\n"); return 0;
            }
        } else if (!strcmp(a, "--swappiness")) {
            if (i + 1 >= argc || !parse_int(argv[++i], 0, 100, &o->swappiness)) {
                fprintf(stderr, "Erro: --swappiness aceita valores de 0 a 100.\n"); return 0;
            }
        } else {
            fprintf(stderr, "Opcao desconhecida: %s (use --help)\n", a); return 0;
        }
    }
    return 1;
}

static int confirma(const Opcoes *o)
{
    char buf[16];
    if (o->sim) return 1;
    printf("\nAviso: o cache e seu amigo. Limpa-lo com frequencia (ex.: todo dia) nao e\n"
           "recomendado: o sistema ficara mais lento por alguns instantes enquanto\n"
           "reconstroi o cache a partir do disco.\n"
           "Continuar? [s/N] ");
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin)) return 0;
    return buf[0] == 's' || buf[0] == 'S' || buf[0] == 'y' || buf[0] == 'Y';
}

static const char *descricao_modo(int modo)
{
#ifdef PLATAFORMA_LINUX
    switch (modo) {
        case 1:  return "PageCache";
        case 2:  return "Dentries e Inodes";
        default: return "PageCache + Dentries + Inodes";
    }
#else
    switch (modo) {
        case 1:  return "cache de arquivos (modified/standby list)";
        case 2:  return "working sets dos processos";
        default: return "cache de arquivos + working sets";
    }
#endif
}

/* ================================================================== */
/*                              LINUX                                  */
/* ================================================================== */
#ifdef PLATAFORMA_LINUX

#define PROC_DROP_CACHES "/proc/sys/vm/drop_caches"
#define PROC_SWAPPINESS  "/proc/sys/vm/swappiness"

static int le_meminfo(MemInfo *m)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char linha[256];
    u64 v, buffers = 0, cached = 0, sreclaim = 0;

    if (!f) { perror("Nao foi possivel ler /proc/meminfo"); return 0; }
    memset(m, 0, sizeof *m);

    while (fgets(linha, sizeof linha, f)) {
        if      (sscanf(linha, "MemTotal: %llu kB", &v) == 1)        m->total      = v * 1024;
        else if (sscanf(linha, "MemFree: %llu kB", &v) == 1)         m->livre      = v * 1024;
        else if (sscanf(linha, "MemAvailable: %llu kB", &v) == 1)    m->disponivel = v * 1024;
        else if (sscanf(linha, "Buffers: %llu kB", &v) == 1)         buffers  = v * 1024;
        else if (sscanf(linha, "Cached: %llu kB", &v) == 1)          cached   = v * 1024;
        else if (sscanf(linha, "SReclaimable: %llu kB", &v) == 1)    sreclaim = v * 1024;
        else if (sscanf(linha, "SwapTotal: %llu kB", &v) == 1)       m->swap_total = v * 1024;
        else if (sscanf(linha, "SwapFree: %llu kB", &v) == 1)        m->swap_livre = v * 1024;
    }
    fclose(f);

    /* mesma definicao de "buff/cache" usada pelo comando `free` */
    m->cache = buffers + cached + sreclaim;
    if (m->disponivel == 0) m->disponivel = m->livre + m->cache;
    return 1;
}

static void mostra_distro(void)
{
    FILE *f = fopen("/etc/os-release", "r");
    char linha[256];
    if (!f) return;
    while (fgets(linha, sizeof linha, f)) {
        if (!strncmp(linha, "PRETTY_NAME=", 12)) {
            char *p = linha + 12;
            size_t n = strlen(p);
            while (n && (p[n-1] == '\n' || p[n-1] == '"')) p[--n] = '\0';
            if (*p == '"') p++;
            printf("Sistema: %s\n", p);
            break;
        }
    }
    fclose(f);
}

static int le_swappiness(void)
{
    int v = -1;
    FILE *f = fopen(PROC_SWAPPINESS, "r");
    if (f) { if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f); }
    return v;
}

/* Escreve diretamente no /proc (ja somos root): evita o problema do
 * redirecionamento `>` com sudo citado no guia. */
static int escreve_proc(const char *caminho, const char *valor)
{
    FILE *f = fopen(caminho, "w");
    if (!f) { fprintf(stderr, "Erro ao abrir %s: %s\n", caminho, strerror(errno)); return 0; }
    if (fputs(valor, f) == EOF || fflush(f) == EOF) {
        fprintf(stderr, "Erro ao escrever em %s: %s\n", caminho, strerror(errno));
        fclose(f); return 0;
    }
    if (fclose(f) == EOF) {
        fprintf(stderr, "Erro ao gravar em %s: %s\n", caminho, strerror(errno));
        return 0;
    }
    return 1;
}

/* Se nao for root, reexecuta o proprio programa com sudo. */
static void garante_root(int argc, char **argv)
{
    char exe[4096];
    char **nargv;
    ssize_t n;
    int i;

    if (geteuid() == 0) return;

    n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) exe[n] = '\0'; else snprintf(exe, sizeof exe, "%s", argv[0]);

    printf("Privilegios de root necessarios. Reexecutando com sudo...\n");
    nargv = calloc((size_t)argc + 3, sizeof *nargv);
    if (!nargv) { perror("calloc"); exit(1); }
    nargv[0] = "sudo";
    nargv[1] = exe;
    for (i = 1; i < argc; i++) nargv[i + 1] = argv[i];
    nargv[argc + 1] = NULL;

    execvp("sudo", nargv);
    fprintf(stderr, "Nao foi possivel executar o sudo (%s).\n"
                    "Execute como root:  su -c '%s'\n", strerror(errno), exe);
    exit(1);
}

static int limpa_cache(int modo)
{
    char valor[4];

    /* Passo 1 do guia: sync - grava as "dirty pages" antes de limpar. */
    printf("-> sync: gravando dados pendentes no disco...\n");
    sync();

    /* Passo 2 do guia: echo N > /proc/sys/vm/drop_caches */
    printf("-> drop_caches=%d (%s)...\n", modo, descricao_modo(modo));
    snprintf(valor, sizeof valor, "%d\n", modo);
    return escreve_proc(PROC_DROP_CACHES, valor);
}

static int ajusta_swappiness(int novo)
{
    char valor[16];
    int antes = le_swappiness();
    snprintf(valor, sizeof valor, "%d\n", novo);
    if (!escreve_proc(PROC_SWAPPINESS, valor)) return 0;
    printf("-> vm.swappiness: %d -> %d (temporario; para persistir use\n"
           "   /etc/sysctl.d/99-swappiness.conf com \"vm.swappiness=%d\")\n",
           antes, novo, novo);
    return 1;
}

/* FAQ do guia: swapoff -a && swapon -a, so se a RAM comportar o conteudo do swap */
static int reinicia_swap(void)
{
    MemInfo m;
    u64 usado;

    if (!le_meminfo(&m)) return 0;
    if (m.swap_total == 0) { printf("-> Nenhum swap ativo; nada a fazer.\n"); return 1; }

    usado = m.swap_total - m.swap_livre;
    if (usado >= m.disponivel) {
        fprintf(stderr, "Abortado: swap em uso (%.1f MiB) >= RAM disponivel (%.1f MiB).\n"
                        "Desativar o swap agora poderia acionar o OOM Killer.\n",
                mib(usado), mib(m.disponivel));
        return 0;
    }
    printf("-> Reiniciando swap (swapoff -a && swapon -a)... pode demorar.\n");
    if (system("swapoff -a && swapon -a") != 0) {
        fprintf(stderr, "Falha ao reiniciar o swap.\n");
        return 0;
    }
    return 1;
}

static int executa(int argc, char **argv, const Opcoes *o)
{
    MemInfo antes, depois;
    int ok = 1;

    mostra_distro();
    if (!le_meminfo(&antes)) return 1;
    imprime_mem("\nMemoria ANTES:", &antes);
    printf("  vm.swappiness  : %d\n", le_swappiness());

    if (o->somente_status) return 0;

    garante_root(argc, argv);

    if (!o->sem_limpeza || o->reset_swap || o->swappiness >= 0) {
        if (!o->sem_limpeza && !confirma(o)) { printf("Cancelado.\n"); return 0; }
    }

    printf("\n");
    if (!o->sem_limpeza)        ok &= limpa_cache(o->modo);
    if (o->swappiness >= 0)     ok &= ajusta_swappiness(o->swappiness);
    if (o->reset_swap)          ok &= reinicia_swap();

    if (le_meminfo(&depois)) {
        imprime_mem("\nMemoria DEPOIS:", &depois);
        if (!o->sem_limpeza) {
            double liberado = mib(antes.cache) - mib(depois.cache);
            printf("\nCache liberado: %.1f MiB\n", liberado > 0 ? liberado : 0.0);
            printf("Obs.: o sistema pode ficar um pouco mais lento enquanto o cache e reconstruido.\n");
        }
    }
    return ok ? 0 : 1;
}

#endif /* PLATAFORMA_LINUX */

/* ================================================================== */
/*                             WINDOWS                                 */
/* ================================================================== */
#ifdef PLATAFORMA_WINDOWS

/* Comandos de SYSTEM_MEMORY_LIST_COMMAND (SystemMemoryListInformation = 80) */
#define SYS_MEMORY_LIST_INFORMATION 80
#define MEM_EMPTY_WORKING_SETS      2
#define MEM_FLUSH_MODIFIED_LIST     3
#define MEM_PURGE_STANDBY_LIST      4

typedef LONG (NTAPI *NtSetSystemInformation_t)(ULONG, PVOID, ULONG);

static int le_meminfo(MemInfo *m)
{
    MEMORYSTATUSEX ms;
    PERFORMANCE_INFORMATION pi;

    memset(m, 0, sizeof *m);
    ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    m->total = ms.ullTotalPhys;
    m->livre = m->disponivel = ms.ullAvailPhys;

    pi.cb = sizeof pi;
    if (GetPerformanceInfo(&pi, sizeof pi))
        m->cache = (u64)pi.SystemCache * pi.PageSize;
    return 1;
}

static int eh_administrador(void)
{
    HANDLE tok = NULL;
    TOKEN_ELEVATION el;
    DWORD n = 0;
    int admin = 0;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenElevation, &el, sizeof el, &n))
            admin = el.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    return admin;
}

static int habilita_privilegio(const char *nome)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return 0;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueA(NULL, nome, &tp.Privileges[0].Luid)) { CloseHandle(tok); return 0; }
    AdjustTokenPrivileges(tok, FALSE, &tp, sizeof tp, NULL, NULL);
    {
        int ok = (GetLastError() == ERROR_SUCCESS);
        CloseHandle(tok);
        return ok;
    }
}

/* Equivalente ao `sync`: descarrega os buffers de todos os volumes fixos. */
static void flush_volumes(void)
{
    DWORD mascara = GetLogicalDrives();
    char raiz[] = "A:\\", dev[] = "\\\\.\\A:";
    int i;

    for (i = 0; i < 26; i++) {
        HANDLE h;
        if (!(mascara & (1u << i))) continue;
        raiz[0] = (char)('A' + i);
        if (GetDriveTypeA(raiz) != DRIVE_FIXED) continue;
        dev[4] = (char)('A' + i);
        h = CreateFileA(dev, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) { FlushFileBuffers(h); CloseHandle(h); }
    }
}

static int nt_comando(NtSetSystemInformation_t fn, ULONG cmd, const char *nome)
{
    LONG st = fn(SYS_MEMORY_LIST_INFORMATION, &cmd, sizeof cmd);
    if (st != 0) {
        fprintf(stderr, "Falha em %s (NTSTATUS 0x%08lX)\n", nome, (unsigned long)st);
        return 0;
    }
    return 1;
}

static int limpa_cache(int modo)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    NtSetSystemInformation_t fn;
    int ok = 1;

    fn = nt ? (NtSetSystemInformation_t)(void *)GetProcAddress(nt, "NtSetSystemInformation") : NULL;
    if (!fn) { fprintf(stderr, "NtSetSystemInformation indisponivel.\n"); return 0; }

    if (!habilita_privilegio("SeProfileSingleProcessPrivilege"))
        fprintf(stderr, "Aviso: nao foi possivel habilitar SeProfileSingleProcessPrivilege.\n");

    printf("-> Descarregando buffers dos volumes (equivalente ao sync)...\n");
    flush_volumes();

    printf("-> Limpando: %s...\n", descricao_modo(modo));
    if (modo & 1) {
        ok &= nt_comando(fn, MEM_FLUSH_MODIFIED_LIST, "flush da modified list");
        ok &= nt_comando(fn, MEM_PURGE_STANDBY_LIST,  "purge da standby list");
    }
    if (modo & 2)
        ok &= nt_comando(fn, MEM_EMPTY_WORKING_SETS,  "esvaziar working sets");
    return ok;
}

static int executa(int argc, char **argv, const Opcoes *o)
{
    MemInfo antes, depois;
    int ok;
    (void)argc; (void)argv;

    SetConsoleOutputCP(CP_UTF8);
    printf("Sistema: Windows\n");

    if (!le_meminfo(&antes)) { fprintf(stderr, "Falha ao ler a memoria.\n"); return 1; }
    imprime_mem("\nMemoria ANTES:", &antes);
    if (o->somente_status) return 0;

    if (o->swappiness >= 0 || o->reset_swap)
        printf("\nObs.: --swappiness e --reset-swap existem apenas no Linux; ignorados.\n");
    if (o->sem_limpeza) return 0;

    if (!eh_administrador()) {
        fprintf(stderr, "\nExecute este programa como Administrador.\n");
        return 1;
    }
    if (!confirma(o)) { printf("Cancelado.\n"); return 0; }

    printf("\n");
    ok = limpa_cache(o->modo);

    if (le_meminfo(&depois)) {
        imprime_mem("\nMemoria DEPOIS:", &depois);
        printf("\nObs.: o sistema pode ficar um pouco mais lento enquanto o cache e reconstruido.\n");
    }
    return ok ? 0 : 1;
}

#endif /* PLATAFORMA_WINDOWS */

/* ================================================================== */

int main(int argc, char **argv)
{
    Opcoes o;
    if (!parse_args(argc, argv, &o)) return 2;
    return executa(argc, argv, &o);
}
