# elfldr (cópia modificada)

Origem: [ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr),
commit `2a8da00` (2026-10-21), de John Törnblom. Licença GPLv3+ (ver `LICENSE`).

Ficheiros copiados: `elfldr.c`, `elfldr.h`, `pt.c`, `pt.h`, `log.h`, `asset.h`.

## Modificações (BrowserPS5, 2026-11-02)

- Nova função `elfldr_spawn_ipc()`, que lança um processo e duplica um
  descritor do processo pai (`ipc_fd`) para o filho com um número fixo
  (`child_ipc_fd`). É o equivalente ao que o `ProcessLauncher` do WebKit
  precisa para entregar o socket IPC ao WebProcess e ao NetworkProcess.
- `elfldr_exec()` recebe dois parâmetros novos (`ipc_fd`, `child_ipc_fd`).
- `elfldr_spawn()` mantém o comportamento original (chama `elfldr_spawn_ipc`
  com `-1`).

## Modificações (BrowserPS5, 2026-11-03)

- `elfldr_spawn_ipc()` também acorda com `NOTE_EXIT` e verifica que o
  filho parou antes do `execve()`. Antes, um filho que morresse cedo
  deixava o pai à espera para sempre (aconteceu dentro de uma app, onde
  `sys_budget_set(0)` falha com `ESRCH`).
- **Não lançar processos a partir de uma app.** Continuar com o budget da
  app fez a PS5 deixar de responder. Ver `docs/02-desenho-sem-gpu.md`.
