# Handoff: pendências da versão definitiva (PRD 4, Guia, CI)

Estado em 2026-11-13: `origin/main` = `2d0f647`, um fast-forward de 56 commits sobre `ca5867d`. Os três últimos:

- `a342706` as respostas do dono ao PRD 4 (corrente da caçada certa em 10, agentes DRI com a fala deles, Tamers);
- `3d3f2e8` o Guia no jogo (missão principal, missões, leilões de Asuka);
- `2d0f647` o CI (Linux e Windows, release por tag).

Build, `tests/port/definitive*.py`, `text_override.py`, `run.py` e o check de entrada PS1 passam no lab, inclusive com
o CI aplicado. Os testes no jogo do DRI e do Guia deram PASS (scripts em `.dumps/tasks/ingame/`, ver o fim).

Antes de 2d0f647 o `main` do GitHub tinha levado um force-push (`841521a` → `ca5867d`). Como `ca5867d` já estava no
histórico local, o push foi só fast-forward e não apagou nada. Se o `841521a` tinha algo seu, ele não está no `main`.

## 1. GitHub Actions (o primeiro a olhar)

- **O push não disparou nenhum workflow.** O repo tinha 0 runs no total, com o Actions habilitado
  (`allowed_actions: all`) e os dois workflows (`CI`, `Release`) `active`. A API de eventos ainda não listava o push
  de `2d0f647` (o último `PushEvent` era o de `ca5867d`, de 11/11), então pode ser atraso do GitHub.
- Disparei os dois na mão (`workflow_dispatch`) no `2d0f647`:
  - CI: https://github.com/al4xdev/dw3/actions/runs/43527703415
  - Release sem tag: https://github.com/al4xdev/dw3/actions/runs/43527708190 (só os artefatos, o AppImage e o zip do
    Windows; não publica nada).

  Parei de acompanhar com os dois na fila. Para ver:

  ```fish
  gh run watch 43527708190 -R al4xdev/dw3 --exit-status
  gh run view 43527703415 -R al4xdev/dw3
  gh run view <id> -R al4xdev/dw3 --log-failed   # se falhar
  ```

- Confirmar que o próximo push no `main` que mexa em código dispara o CI sozinho (`gh run list -R al4xdev/dw3`, o
  evento deve ser `push`). `ci.yml` ignora pushes só de `docs/**`, `**/*.md`, `LICENSE`, `.gitignore`. Se ainda assim
  não disparar, olhar Settings → Actions → General do repo.
- **Secrets e variáveis: nenhum configurado.**
  - `GAMEDATA_DEPLOY_KEY` (secret, opcional): com ela o CI e o release rodam os testes com o disco; sem ela, só os
    smoke tests sem disco.
  - `PSXSTACK_TOKEN`: não precisa, o `al4xdev/dw3-psxstack` é público.
  - `RELEASE_DRAFT=true` (variável): defina antes da primeira tag se quiser o release como rascunho.
- **Primeiro release:** o fluxo está em `docs/RELEASE.md`. Em resumo: `git tag v0.1.0; and git push origin v0.1.0`
  (um sufixo, como `v0.1.0-rc1`, sai como pre-release).
- **O conserto do psxstack.** O pin atual (`5e88c08`, no `dw3-handheld` do `al4xdev/dw3-psxstack`) não compila os
  alvos de PC como está, e por isso há um bloco de contorno no `port/CMakeLists.txt`. O bloco faz três coisas:
  `-Wno-error=unused-variable` no `runtime/video.c` sem EGL, `fbdev_present.c` fora do build sem SDL2 e um
  `sys/syscall.h` vazio no Windows. O build do lab (SDL2 e EGL desligados) já passa por esse contorno e os testes
  passam. O conserto de verdade é o patch abaixo, no repo do psxstack (`dw3-handheld`). Depois de atualizar o pin,
  remova o bloco do `port/CMakeLists.txt`.

  Antes de aplicar, confira o que o patch assume: ele tira `fbdev_present.c` e `render_gles.c` do glob do runtime e
  diz que eles voltam ao build com `PSXSTACK_SDL2`/`PSXSTACK_EGL` mais abaixo no `psxstack.cmake`. O patch não mostra
  essa parte, e ela não foi verificada. Se essas opções não adicionarem os arquivos de volta, o build do portátil
  perde o presenter.

  ```diff
  diff --git a/cmake/psxstack.cmake b/cmake/psxstack.cmake
  --- a/cmake/psxstack.cmake
  +++ b/cmake/psxstack.cmake
  @@ -293,6 +293,9 @@
   
       # ---- The binary: the runtime, the adapter, the tables, the shim, the units (and on PE the bracket symbols).
       file(GLOB RUNTIME_SRCS CONFIGURE_DEPENDS "${PSXSTACK_ROOT}/runtime/*.c")
  +    # The handheld's presenter and renderer join the build with their options (PSXSTACK_SDL2, PSXSTACK_EGL, below):
  +    # fbdev_present.c includes <linux/fb.h> and the Linux ioctls, which break the Windows build.
  +    list(FILTER RUNTIME_SRCS EXCLUDE REGEX "/runtime/(fbdev_present|render_gles)\\.c$")
       set(PLATFORM_SRCS "")
       if(WIN32)
           _psxstack_run("${PSXSTACK_PORT_GEN}" markers --id "${ID}" --overlays "${G_OVERLAYS}" --out "${GEN}/markers.c")
  diff --git a/runtime/main.c b/runtime/main.c
  --- a/runtime/main.c
  +++ b/runtime/main.c
  @@ -4,7 +4,9 @@
   #include <errno.h>
   #include <stdlib.h>
   #include <string.h>
  +#ifdef __linux__ /* SYS_sched_setaffinity (below); Windows has no such header */
   #include <sys/syscall.h>
  +#endif
   #include <unistd.h>
   
   #include "platform.h"
  diff --git a/runtime/video.c b/runtime/video.c
  --- a/runtime/video.c
  +++ b/runtime/video.c
  @@ -76,7 +76,9 @@
   static long video_converted_frame = -1; /* the frame video_pixels was converted at (the debug channel's screenshot) */
   static int video_gpu_wanted;            /* --renderer gpu / video.renderer "gpu" */
   static int video_gles_wanted;           /* --renderer gles: the offscreen GLES renderer (render_gles.c) */
  +#ifdef PSXSTACK_EGL /* only the GLES paths read it: unused (-Werror) in the other builds */
   static int video_gles;                  /* the offscreen GLES renderer draws; the present still comes out through fbdev */
  +#endif
   static int video_internal_scale = 1;    /* --internal-scale / video.internal_scale: the rasteriser's, 1..8 */
   static PortFilter video_filter;         /* --filter / video.filter: the hardware renderer's present (render_gpu_present.c) */
   static struct {
  ```

  O mesmo patch está em `.dumps/tasks/ci/psxstack-pc-build.patch`.

## 2. Widescreen

- Batalhas: o mod já existia (`port/mods/widescreen`, 0.1) e vai nos dois pacotes.
- Campo: o mod 0.2 do fork original (`7c47270`) precisa de mudanças no runtime que o pin não tem. Para trazê-lo,
  primeiro essas mudanças precisam entrar no `dw3-psxstack`.

## 3. Não testado no jogo

- **A fala real de um agente DRI.** O teste no jogo (`dri_talk_ingame.py`) simulou a fala segurando `event_running`
  (offset `0x78` no host). Falta conversar de verdade com um agente e ver três coisas: a fala dele é a original, a
  lista do DRI abre só depois que ela acaba, e a quest de DDNA dele continua normal. Cada agente tem uma quest só no
  jogo inteiro, e ela não pode quebrar. Os agentes são os pesquisadores da quest de DDNA:
  - Kazuki (sprite 43, mapa `0x21D`, Patamon)
  - Michael (44, `0x21D`, Renamon)
  - Bob (43, `0x226`, Veemon)
  - Johann (43, `0x22B`, Agumon)
  - Jimmy (43, `0x22E`, Guilmon)
  - Ken (43, `0x258`, Kotemon)
  - Yuji (44, `0x258`, Kumamon)
  - Yang (163, `0x258`, Monmon)

  As regras de agente são por (mapa, sprite), e o mesmo sprite pode ser de outro NPC no mesmo mapa: vale ver se um
  NPC comum com o sprite 43/44 não abre o DRI.
- **O registro HUNT no memory card** (as contagens dos raros e o progresso do DRI): salvar e carregar não foi testado.
  Precisa de um save point do Guardromon, porque o menu de campo não tem SAVE. Para testar: salvar, recarregar e
  conferir no bestiário as linhas "Hunt" e "Rare Cores" e o "Best" do DRI.
- **A linha de requisitos da forja na loja.** O teste unitário passa, mas no jogo mudar `stitshop_goods_N` não mudou a
  lista da loja (provavelmente os dados do overlay são recarregados ao abrir). A recusa de compra sem material e o
  limite de quantidade passam pelo mesmo caminho, e também só têm o teste unitário.
- **O aviso do Guia ao chegar.** O log registra o aviso (`notice 1 (4) at progress 4`), mas a captura 30 frames depois
  mostra só o título do mapa ("Central Sector"). Os avisos do passo 5 ("NEW OBJECTIVE", "AUCTION OPEN") aparecem
  normalmente. Vale ver se o aviso de chegada aparece depois que o título do mapa some, ou se ele é perdido.

## 4. Ajustes pequenos

- **Debrief de vitória do DRI:** a linha da recompensa passa da borda direita do painel ("Metal Power 1 - Metal
  Power 2 - Metal Pow…"). Está em `port/game/definitive_dri_screen.c`, no debrief (recompensa em +70). Quebrar em duas
  linhas ou encurtar. Captura: `.dumps/tasks/dri/talk_sheet.ppm` (a última, `e_won`).
- **`def_hunt_battle_end`** (`port/game/definitive_hunt.c`) conta o resultado 2 (derrota) como vitória. Hoje é
  inofensivo porque a derrota vai para a tela de título. Corrigir se isso mudar.
- **Rush flutuante:** os stats do level-up são sorteados em outro momento que no jogo original. A regra é a mesma, mas
  os números podem sair diferentes.
- **Documentação:** `definitive_ui.png` é anterior às telas por família e precisa de captura nova. O `MECHANICS.md`
  tem a faixa de crítico errada (diz +148%); corrigir pelo código.

## 5. Para revisar

As decisões tomadas por você estão nas seções "Feito" de cada camada nos PRDs (`.plan/tasks/definitive-*.md`). As
listas geradas, no lab, em `.dumps/tasks/`:

- `persona/fraquezas.md`
- `hunt/alvos.md`, `hunt/materiais.md`
- `prices/precos.md`
- `forge/receitas.md`
- `equipment/variantes.md`
- `dri/desafios.md`

## 6. Estado do lab

- `wl-copy` (não rastreado) ficou como estava.
- O backup do trabalho do Fable no Guia está em `.dumps/fable-guide-wip/`. Já está todo em `3d3f2e8`, então pode ir
  para `/tmp` quando quiser.
- Os testes no jogo, copiados de `/tmp`, rodam da raiz do checkout com `tools/venv/bin/python`:
  - `.dumps/tasks/ingame/dri_talk_ingame.py` (fala, Tamer, derrota, vitória);
  - `.dumps/tasks/ingame/guide_ingame.py` (aviso, menu, abas, passo 5, viagem).

  Eles gravam as capturas em `.dumps/tasks/dri/` e `.dumps/tasks/guide/`.
