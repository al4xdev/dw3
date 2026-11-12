# Handoff: levar o build da versão definitiva para o console (via ssh)

Para um agente local. Tarefa: **fazer backup do que está no console e depois copiar só os arquivos do port**. Não mexa em
nada do jogador (disco, saves, configurações). Pare e pergunte se algo não bater com o que está escrito aqui.

## Dados que o dono precisa dar (não adivinhe)

- Endereço e usuário do console: `<USUARIO>@<IP>` (ssh). Senha ou chave: o dono fornece.
- Pasta do PortMaster no console: normalmente `/roms/ports` (ou `/roms2/ports`, `/storage/roms/ports`). O launcher
  `Digimon World 3.sh` e a pasta `dw3/` ficam lá. Descubra com `ssh <console> 'ls -d /roms*/ports /storage/roms/ports 2>/dev/null'`.
  Abaixo, `PORTS` é essa pasta.

## O que copiar (origem, na máquina local)

Origem: `/home/alex/git/my/dw3/portmaster/` (o espelho do dispositivo; o build novo é gravado aí por
`tools/build_arm.sh`).

| Origem | Destino no console |
|---|---|
| `portmaster/dw3/dw2003` (o binário ARM 32-bit) | `PORTS/dw3/dw2003` |
| `portmaster/dw3/mods/` (manifestos dos mods) | `PORTS/dw3/mods/` |
| `portmaster/dw3/dw3-menu.py`, `ledd.py`, `probe.sh`, `README.txt` | `PORTS/dw3/` (só se mudaram) |
| `portmaster/Digimon World 3.sh` | `PORTS/Digimon World 3.sh` (só se mudou) |

**Nunca sobrescreva nem apague no console:** `PORTS/dw3/game/` (o disco do jogador), `card1.mcd`, `card2.mcd`,
`settings.json`, `pad.map`, `tui.state`, `logs/`, `log.txt`, `menu.log`. Não use `rsync --delete` nem `scp -r` da pasta
inteira. Copie arquivo por arquivo (ou `rsync` sem `--delete` e com a lista acima).

Antes de copiar, confirme que o binário local é o novo: `file portmaster/dw3/dw2003` deve dizer `ELF 32-bit LSB ... ARM`
e a data deve ser de hoje (12 nov 2026) ou depois. Se a data for 11 nov ou antes, o build novo ainda não foi gerado:
rode `tools/build_arm.sh` em `/home/alex/git/my/dw3/dw2003recomp` (precisa do Docker, a imagem `dw3-armhf-builder` já existe)
ou peça ao dono.

## Passo 1 — backup (obrigatório, antes de qualquer cópia)

No console (via ssh), crie uma pasta de backup com data e guarde a instalação antiga **e** os saves:

```sh
ssh <console> '
  set -e
  PORTS=<PORTS>
  B=$PORTS/dw3-backup-$(date +%Y%m%d-%H%M%S)
  mkdir -p "$B"
  cp -a "$PORTS/dw3/card1.mcd" "$PORTS/dw3/card2.mcd" "$B/" 2>/dev/null || true   # os saves
  cp -a "$PORTS/dw3/settings.json" "$PORTS/dw3/pad.map" "$PORTS/dw3/tui.state" "$B/" 2>/dev/null || true
  cp -a "$PORTS/dw3/dw2003" "$B/dw2003.old"
  [ -d "$PORTS/dw3/mods" ] && cp -a "$PORTS/dw3/mods" "$B/mods.old"
  cp -a "$PORTS/Digimon World 3.sh" "$B/" 
  echo "$B"
  ls -la "$B"
'
```

Depois **traga uma cópia dos saves para a máquina local** (o cartão no console não é a única cópia):

```sh
mkdir -p /home/alex/git/my/dw3/backups-console
scp <console>:<PORTS>/dw3/card1.mcd <console>:<PORTS>/dw3/card2.mcd /home/alex/git/my/dw3/backups-console/
```

Confira que os arquivos de save existem e não estão vazios (`card1.mcd` deve ter 131072 bytes) **antes de seguir**. Se não
existirem (jogador nunca salvou), diga ao dono e continue só com o resto.

## Passo 2 — copiar o build novo

```sh
scp /home/alex/git/my/dw3/portmaster/dw3/dw2003 <console>:<PORTS>/dw3/dw2003
scp -r /home/alex/git/my/dw3/portmaster/dw3/mods/. <console>:<PORTS>/dw3/mods/
ssh <console> 'chmod +x <PORTS>/dw3/dw2003'
```

Só se o dono pedir, copie também `dw3-menu.py`, `ledd.py`, `probe.sh` e o launcher (`chmod +x` neles).
Confira o hash: `sha1sum` local e remoto do `dw2003` devem ser iguais.

## Passo 3 — saves antigos e o MP (decisão do dono, não faça sozinho)

A versão definitiva muda o MP dos rookies (Kotemon, Kumamon, Monmon, Veemon, Guilmon). **O jogo não migra saves antigos.**
Um save antigo abre e joga, com o MP antigo. Para converter, o dono usa:

```sh
cd /home/alex/git/my/dw3/dw2003recomp
tools/venv/bin/python tools/convert_save_mp.py --dry-run /home/alex/git/my/dw3/backups-console/card1.mcd   # só mostra
tools/venv/bin/python tools/convert_save_mp.py /home/alex/git/my/dw3/backups-console/card1.mcd            # grava
```

Rode **sempre sobre a cópia local** (`backups-console/`), nunca direto no console, e só se o dono mandar. Se ele mandar
levar o cartão convertido de volta, faça `scp` para `PORTS/dw3/card1.mcd` **depois** de ter o backup do passo 1.

Novidade nesta versão: o cartão ganha um registro de bestiário na cauda do arquivo (últimos 0x800 bytes, nunca usados
pelo jogo). Save antigo abre com tudo "???" no bestiário, sem erro.

## Passo 4 — verificar

1. No console: `ls -la PORTS/dw3/` (o binário novo, `game/` e os `.mcd` intactos).
2. Pedir ao dono para abrir o jogo. Depois ler `PORTS/dw3/log.txt` e `logs/` (`ssh <console> 'tail -n 40 <PORTS>/dw3/log.txt'`)
   e avisar se aparecer erro de biblioteca ARM, `fatal` ou `crash`.
3. Se o jogo não abre ou o dono pedir para voltar: restaurar do backup do passo 1:
   `cp -a <B>/dw2003.old <PORTS>/dw3/dw2003`, `cp -a <B>/mods.old/. <PORTS>/dw3/mods/`, e os `.mcd` de `<B>/` se foram trocados.

## O que o dono vai ver de diferente (para saber o que é bug e o que é intencional)

- O HUD de batalha é sempre o novo (com o 7º botão Inspecionar, em L2 por padrão, e Start abre a memória do inimigo).
- O jogador sempre abre a luta (exceto emboscada). Fraqueza com técnica elemental abre o inimigo e dá ação extra.
- Os inimigos ainda usam os valores de resistência do disco: a tabela nova de fraquezas só entra depois que o dono aprovar
  `.dumps/tasks/persona/fraquezas.md`. Sem ela, "fraqueza" sai dos valores originais.
- Camada 10 (balanço) é do dono: ele joga e ajusta as constantes de `port/game/definitive.h`.
