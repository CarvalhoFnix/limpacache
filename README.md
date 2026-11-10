# limpacache

Utilitário de linha de comando, escrito em C, para **limpar o cache de memória** do sistema de forma segura. Ele automatiza o procedimento descrito no guia [Como Limpar Cache de Memória no Linux: O Guia Definitivo](https://helpsysadmin.com.br/blog/limpar-cache-memoria-linux-guia-completo/) (HelpSysAdmin).

Desenvolvido e testado no **Linux Mint 22.3**, com suporte a **Ubuntu/Debian, Fedora, Arch Linux, openSUSE** e **Windows**.

---

## Sumário

- [O que ele faz](#o-que-ele-faz)
- [Quando usar (e quando não usar)](#quando-usar-e-quando-não-usar)
- [Plataformas suportadas](#plataformas-suportadas)
- [Instalação](#instalação)
- [Uso](#uso)
- [Opções](#opções)
- [Exemplo de saída](#exemplo-de-saída)
- [Como funciona](#como-funciona)
- [Windows](#windows)
- [Segurança](#segurança)
- [Créditos](#créditos)

---

## O que ele faz

No Linux, o programa executa o fluxo recomendado pelo guia:

1. Mostra o uso de memória **antes** (RAM, buff/cache, swap e `vm.swappiness`), equivalente ao `free -m -t`.
2. Executa o `sync` para gravar no disco as *dirty pages* pendentes, evitando perda de dados.
3. Escreve em `/proc/sys/vm/drop_caches`:

   | Modo | O que libera |
   |------|--------------|
   | `1`  | PageCache |
   | `2`  | Dentries e Inodes |
   | `3`  | Tudo (padrão) |

4. Mostra o uso de memória **depois** e quanto cache foi liberado.
5. Opcionalmente ajusta o `vm.swappiness` e reinicia o swap (`swapoff -a && swapon -a`).

Se você não estiver como root, o programa se reexecuta automaticamente com `sudo`. A escrita no `/proc` é feita diretamente pelo programa, o que evita o problema do redirecionamento `sudo echo 3 > ...`.

## Quando usar (e quando não usar)

O kernel Linux usa a RAM livre como cache para acelerar o acesso ao disco e libera esse cache automaticamente quando um aplicativo precisa de memória. **Na maioria dos casos, você não precisa limpar o cache.**

Casos em que faz sentido:

- **Benchmark de I/O:** para medir a velocidade real do disco, e não a da RAM.
- **Após backups massivos:** quando a RAM fica cheia de metadados que não serão lidos de novo tão cedo.
- **Testes em desenvolvimento:** quando bibliotecas ou bancos de dados locais mudaram e o cache pode confundir.
- **Diagnóstico de swap:** quando o sistema usa swap mesmo havendo cache que poderia ser descartado.

Evite usar em rotina diária (por exemplo, em um cron). O sistema fica mais lento por alguns instantes depois da limpeza, porque o cache precisa ser reconstruído a partir do disco.

## Plataformas suportadas

| Sistema | Suporte |
|---------|---------|
| Linux Mint / Ubuntu / Debian | Sim (testado no Linux Mint 22.3) |
| Fedora | Sim (mesmo código, usa `/proc`) |
| Arch Linux | Sim (mesmo código, usa `/proc`) |
| openSUSE | Sim (mesmo código, usa `/proc`) |
| Windows | Sim, com equivalente nativo (veja [Windows](#windows)) |

Como o `/proc/sys/vm/drop_caches` é uma interface do próprio kernel, o mesmo código funciona em qualquer distribuição Linux.

## Instalação

### 1. Instale as ferramentas de compilação

| Distribuição | Comando |
|--------------|---------|
| Linux Mint / Ubuntu / Debian | `sudo apt install build-essential` |
| Fedora | `sudo dnf install gcc make` |
| Arch Linux | `sudo pacman -S base-devel` |
| openSUSE | `sudo zypper install gcc make` |

### 2. Baixe, compile e instale

```bash
git clone https://github.com/CarvalhoFnix/limpacache
cd limpacache
make
sudo make install     # instala em /usr/local/bin
```

Para testar sem instalar, use `./limpacache` dentro da pasta.

Para compilar sem o Makefile:

```bash
gcc -O2 -Wall -Wextra -o limpacache limpacache.c
```

### Desinstalar

```bash
sudo make uninstall
```

## Uso

```bash
limpacache                          # limpa tudo (modo 3), pede confirmação
limpacache -m 1                     # limpa só o PageCache
limpacache -m 2                     # limpa só Dentries e Inodes
limpacache -y                       # não pede confirmação
limpacache --status                 # só mostra o estado da memória (não precisa de root)
limpacache --no-clean --swappiness 10   # só ajusta o swappiness
limpacache --reset-swap             # limpa o cache e reinicia o swap
```

## Opções

| Opção | Descrição |
|-------|-----------|
| `-m`, `--mode N` | Modo de limpeza: `1`, `2` ou `3` (padrão `3`) |
| `-y`, `--yes` | Não pedir confirmação |
| `-s`, `--status` | Apenas mostra a memória, sem alterar nada |
| `--no-clean` | Não limpa o cache (útil com `--swappiness` ou `--reset-swap`) |
| `--swappiness N` | Define `vm.swappiness` de 0 a 100 (Linux; vale até o próximo boot) |
| `--reset-swap` | Executa `swapoff -a && swapon -a`, se houver RAM suficiente (Linux) |
| `-h`, `--help` | Mostra a ajuda |
| `-V`, `--version` | Mostra a versão |

## Exemplo de saída

```text
Sistema: Linux Mint 22.3

Memoria ANTES:
  RAM total      :    15869.2 MiB
  RAM livre      :     9109.4 MiB
  RAM disponivel :    11704.0 MiB
  buff/cache     :     3402.0 MiB
  Swap           :        0.0 MiB usados de 0.0 MiB
  vm.swappiness  : 60
```

Depois de confirmar a limpeza, o programa mostra a memória novamente e o total de cache liberado.

## Como funciona

- A memória é lida de `/proc/meminfo`. O valor de *buff/cache* usa a mesma definição do comando `free` (Buffers + Cached + SReclaimable).
- O `sync()` é chamado antes da limpeza.
- Em `--reset-swap`, o programa **recusa** reiniciar o swap se o swap em uso for maior ou igual à RAM disponível, para evitar o acionamento do OOM Killer.
- O ajuste de `swappiness` é temporário. Para torná-lo permanente, crie `/etc/sysctl.d/99-swappiness.conf` com `vm.swappiness=10`.

## Windows

O Windows não tem um `drop_caches`. O programa usa o equivalente nativo, a chamada `NtSetSystemInformation`, que:

- descarrega os buffers dos volumes fixos (equivalente ao `sync`);
- limpa a *modified list* e a *standby list* (o cache de arquivos), no modo `1`;
- esvazia os *working sets* dos processos, no modo `2`.

O modo `3` faz as duas coisas. Diferenças em relação ao Linux:

- O modo `2` não corresponde a Dentries e Inodes, que não existem no Windows. É apenas uma aproximação.
- `--swappiness` e `--reset-swap` são ignorados.
- Requer **Administrador**.

Compilação com MinGW-w64 (a partir do Linux):

```bash
make windows
```

Ou diretamente no Windows com MinGW:

```bash
gcc -O2 -o limpacache.exe limpacache.c -lpsapi
```

Ou com MSVC:

```bat
cl limpacache.c psapi.lib advapi32.lib
```

> A versão Windows ainda precisa de mais testes. Relatos e correções são bem-vindos.

## Segurança

- O programa só altera a memória e o `vm.swappiness`. Não apaga nem modifica arquivos.
- O `sync` é executado antes de qualquer limpeza.
- Sem `-y`, ele pede confirmação.
- Use por sua conta e risco, de preferência em ambiente de teste antes de usar em servidores de produção.

## Créditos

Procedimento baseado no guia [Como Limpar Cache de Memória no Linux: O Guia Definitivo](https://helpsysadmin.com.br/blog/limpar-cache-memoria-linux-guia-completo/), do blog HelpSysAdmin.

## Licença

Defina a licença do seu projeto aqui (por exemplo, MIT) e adicione o arquivo `LICENSE` ao repositório.
