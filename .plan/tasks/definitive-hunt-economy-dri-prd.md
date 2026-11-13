# PRD 4 — Versão definitiva: Caçada, Forja, Black Market, DRI Gauntlet e Rush flutuante

Dono: Alex. Data: 2026-11-13. Estado: desenho completo; as decisões do dono estão
marcadas `[dono]`, cada uma com uma proposta padrão (fim do arquivo);
implementação por camadas, cada camada só depois do ok da anterior. **Camadas A,
B e C implementadas em 2026-11-13**; a tabela de alvos e os nomes dos materiais
aprovados pelo dono no mesmo dia.

Continuação de `definitive-combat-prd.md`, `definitive-progression-prd.md` e
`definitive-rebalance-prd.md`. Mesmas regras de trabalho: camadas pequenas,
teste entre camadas, toda constante em `port/game/definitive.h` com uma linha
dizendo o que controla, código do jogo só chama ganchos `def_*`/`dw3_*` sob
`#ifdef PC_PORT`, save antigo abre e joga, nada roda dentro de um replay script
(`def_on()` / `def_bestiary_active()`), relatório curto ao dono no fim de cada
camada.

Este PRD é para um modelo menor executar. Por isso cada camada diz **onde
mexer**, **o que já existe** e **como testar**. Onde há uma decisão do dono em
aberto, ela está marcada `[dono]`: leve a proposta padrão ao dono e só aplique
depois do ok. Não invente a resposta.

- **Uma camada por sessão.** Leia este cabeçalho inteiro, a camada da vez e os
  trechos citados. Não leia arquivos de milhares de linhas inteiros: use as
  linhas abaixo e `grep` pelo nome da função.
- Números de linha são de 2026-11-13 e andam conforme o código muda: confirme
  com `grep` antes de editar.

## Visão geral do ciclo (core loop)

Os sistemas formam uma engrenagem única, opcional e progressiva, na campanha e
no pós-game:

1. **Exploração e Caçada:** encadear batalhas na mesma região invoca Encontros
   Raros, que derrubam Materiais de Caça de alto tier (documentados no
   Bestiário).
2. **Economia e Forja:** as lojas tradicionais exigem Materiais + Bits para
   equipamentos avançados. O Black Market drena materiais excedentes e Bits em
   troca de peças faltantes ou Réplicas Lendárias (itens únicos perdíveis de
   leilão/eventos, com revenda 0 Bits).
3. **Desafios DRI (Gauntlet):** os mesmos materiais são chaves permanentes que
   destravam simulações de evolução em cadeia nos DRI Agents, com Acessórios de
   Sinergia em tiers conforme o estágio alcançado.

## O que já existe (não refaça)

Leia antes de começar; tudo abaixo está no código e foi testado no jogo.

| Peça | Onde | Para que serve aqui |
| --- | --- | --- |
| Avisos `WEAK!` / `RESIST!` / `OVERFINISH!` | `port/game/definitive_callout.c` (`def_callout_show`) | Feedback de Alvo Raro; não precisa de textura nem VRAM |
| Texto em polígonos (`def_poly_text`) | `port/game/definitive_callout.c` | Texto em telas sem a fonte do jogo (título) ou sem sistema de diálogo à mão (campo). Só maiúsculas, `2`, `3`, `!`, `?`; uma letra nova é uma linha na tabela `glyphs` |
| OVERFINISH (Bits por dano em inimigo caído) | `port/game/definitive.c` (`overfinish_damage`, `def_overfinish_take_bits`), `src/stfgtrep/stfgtrep_80082E70.c` caso 20 | Fonte de Bits que a economia nova precisa drenar |
| Overlevel Rush | `port/game/definitive.c` (`def_rush_offer`, `def_rush_results`), `src/fieldstg/fieldstg_80087DB0.c` (`fieldstg_rush_*`, `fieldstg_encounter_start`) | Mostra como ir do campo direto para a tela de recompensas (mapa `0x1400`) preenchendo `records_battle_results`; e como desenhar uma pergunta no campo |
| Estado extra do save | `port/game/definitive_bestiary.c` (`REC_EXTRA`, `def_save_flag`, `def_network_mask`) | O registro do bestiário (0x100 bytes por slot) tem bytes livres depois da tabela: é **o** lugar para estado novo por save. A cauda do slot (0x26C4..0x2700) está cheia. Orçamento em "Estado no save" abaixo |
| Bestiário | `src/main/fieldmenu.c` (`fieldmenu_bestiary_*`), `port/game/definitive_bestiary.c` | Tela que vai ganhar a linha de materiais |
| Rede Guardromon | `port/game/definitive_travel.c` (`nodes`, `dw3_travel_leave`, `dw3_travel_frame`) | Tabela das 25 pousadas com mapa e posição, a tela SAVE/TELEPORT e o gancho em FIELDSTG; teleporte zera a cadeia de caçada |
| Modo Difícil | `def_save_flag(DEF_SAVE_HARD)` | Party de 2: o balanceamento dos equipamentos mira 2–3 Digimon |
| Tabela de inimigos por espécie | `port/game/definitive_enemies.c`, gerada por `tools/gen_enemy_weaknesses.py` | Modelo de "tabela gerada por ferramenta e aprovada pelo dono" |
| Textos novos | `tools/text_override/en.txt` + `tools/gen_text_override.py` (gera `port/game/definitive_text.c`) | Todo texto na fonte do jogo entra por aqui |

Fatos do motor que você vai precisar:

- **Batalha:** `fieldstg_start_battle(battle)` (`src/fieldstg/fieldstg_80087DB0.c`)
  copia `fieldstg_battles[battle]` para `records_state` (inimigos com `digimon`,
  `level`, `hp`, `mp`, `stat_scale`) e manda o campo para o mapa `0x600`.
  Encontro aleatório: `fieldstg_encounter_start`. Batalha de script:
  `fieldstg_start_listed_battle`. A tabela tem 335 batalhas
  (`fieldstg_battles[335]`, `src/fieldstg/fieldstg_80083784.c`).
- **Encontros por estágio:** `fieldstg_stage.battle_lists->encounters[area]`
  (`FieldstgBattleLists`, `include/fieldstg.h`) são as listas de encontro
  aleatório; `area` ali é a camada de atributo 4 **dentro do estágio** (0..2),
  não a área do mapa-múndi. A batalha sorteada é `events[pad_random.next() & 7]`.
- **Recompensa:** `stfgtrep_rewards[records_battle_results.battle]` tem
  `tech_exp`, `exp`, `money` (`src/stfgtrep/stfgtrep_80082E70.c`). O item vem de
  `records_battle_results.item`.
- **Registro de espécie:** `FightstgEnemyRecord` (arquivo `0x1CF`, 193 espécies)
  tem `id`, `item`, `drop_rate`, `resists`, `name`. No campo ele é lido com
  `cdload_module.files.get_file(0x1CF)`.
- **Fuga:** `fightstg_rules_roll_escape` (`src/fightstg/fightstg_8008D3B4.c`)
  devolve 0 antes de rolar se `records_state.blocked[11]` estiver ligado (luta
  em que fugir é proibido) ou se o status impede; senão a chance é
  `(battle->escapes + 1) * 8`. `fightstg_battle.state.type` (0 normal, 1 o
  inimigo pode fugir, 5 script, 6 chefe) é definido no começo da luta em
  `src/wfightmn/wfightmn_800A6440.c`.
- **Área do mapa:** `ststatus_map_areas[map & 0xFF] & 0x7F` dá a área do
  mapa-múndi (0..46); o bit 7 é o servidor (Amaterasu).
- **Objetos:** um `object_new` solto no campo **não roda** (ninguém o atualiza).
  Estado estático + um gancho por frame (como `fieldstg_rush_frame`, chamado de
  `fieldstg_camera_update`) funciona.
- **Ordem de desenho:** numa mesma entrada da ordering table, o que é adicionado
  depois é desenhado **por baixo**. Texto primeiro, painel escuro por último.

## Pontos de gancho (confirmados em 2026-11-13)

| O quê | Onde | Usado por |
| --- | --- | --- |
| Fim de batalha, vitória = `fightstg_events.result != 0` | `src/wfightmn/wfightmn_800A6440.c:721` (`def_log_end(fightstg_events.result)`) | A (cadeia) |
| Mapa carregado | `src/fieldstg/fieldstg_80087DB0.c:1699` (`gamestate_flags.set_flag(get_map() + 0x1E00, 1)`), logo antes de `def_hard_party_fix()` em `:1701` | A (reset por região) |
| Encontro aleatório | `src/fieldstg/fieldstg_80087DB0.c:5078` (`fieldstg_encounter_start`); o bloco `PC_PORT` do Rush vai de `:5087` a `:5104`; a chamada final `fieldstg_start_battle(event->battle)` está em `:5105`. A chamada em `:2274` é `fieldstg_start_battle_5`, batalha de script: não mexa nela | A (troca pelo Alvo Raro) |
| Rolagem de fuga | `src/fightstg/fightstg_8008D3B4.c`, `fightstg_rules_roll_escape` (a checagem de `blocked[11]` antecede `chance = (battle->escapes + 1) * 8;` em `:9066`) | A (fuga garantida) |
| Bits da luta | `src/stfgtrep/stfgtrep_80082E70.c:899`–`904` (o OVERFINISH soma em `:904`) | B (modelo de "uma função que devolve e limpa") |
| XP da luta | `src/stfgtrep/stfgtrep_80082E70.c:1074` (`exp = stfgtrep_rewards[...].exp`) | B (10x XP) |
| Cardmon | `src/fieldstg/fieldstg_80087DB0.c:1903` (`(records_state.enemies[0].digimon - 0x1C9) >= 8`) | B (carta rara) |
| Bestiário: linhas e dica | `src/main/fieldmenu.c:34` `BS_ROW_Y 0x40`, `:39` `BS_HINT_Y 0xA5`, janelas criadas em `:139`–`144` | C |
| Loja: seleção, quantidade, débito | `src/stitshop/stitshop_8008321C.c:291` (preço x dinheiro ao selecionar), `:322` e `:363` (máximo comprável), `:446` (débito) | D, E |
| Nocaute do inimigo | `src/fightstg/fightstg_8008D3B4.c:947` (`fightstg_events_add_knockout(0x10)`, um dos 15 pontos que enfileiram nocaute) | H (prova de conceito) |
| Derrota → mapa `0xE00` | `src/wfightmn/wfightmn_800A6440.c:817` (`set_next_map(0xE00, 0)`) | H (sem game over) |
| Tabela de itens | `src/main/records.c:556` (`records_items[]`, cerca de 402 entradas); listas por tipo em `:508`–`546` (cerca de 124 armas, 78 armaduras, 69 acessórios) | D, F, G |

## Constantes novas (`definitive.h`)

Valores iniciais; o balanço final é do dono.

```c
#define DEF_HUNT_CHAIN_MIN        5   /* vitórias seguidas na região antes de o Alvo Raro poder aparecer */
#define DEF_HUNT_CHANCE_BASE      5   /* % de chance no primeiro encontro depois do mínimo */
#define DEF_HUNT_CHANCE_STEP      5   /* % a mais por vitória além do mínimo */
#define DEF_HUNT_CHANCE_MAX      40   /* teto da chance, % */
#define DEF_HUNT_XP_MULT         10   /* XP do Alvo Raro em relação ao encontro padrão da área */
#define DEF_HUNT_MATERIALS       16   /* materiais distintos; um contador u8 cada no registro (orçamento abaixo) */
#define DEF_HUNT_MATERIAL_CAP    99   /* máximo em mãos de cada material */
#define DEF_REPLICAS             16   /* Réplicas Lendárias: posições no bitmask do registro (índice em def_replicas[]) */
#define DEF_DRI_CHALLENGES       16   /* desafios do Gauntlet: 1 bit de destravado e 2 bits de melhor estágio cada */
#define DEF_RUSH_FLOAT            1   /* camada I: o Rush mostra os resultados num painel no campo, sem a tela de resultados */
#define DEF_RUSH_FLOAT_FRAMES   120   /* quanto tempo o painel fica, em frames (60 por segundo) */
/* DEF_SELL_CAP_PCT e a tabela de exceções de preço saem da camada D, aprovados pelo dono */
```

## Estado no save: orçamento do registro extra

O registro de cada slot tem 0x100 bytes: `"BEST"`, versão, checksum, a tabela
de 193 espécies a partir de `+8`, e o registro extra em `REC_EXTRA` = `0xD0`.
Do `REC_EXTRA` até o fim são **48 bytes**, e hoje 8 estão em uso. Todo estado
novo deste PRD cabe nos 40 restantes **só com este orçamento**:

| `REC_EXTRA +` | Campo | Camada |
| --- | --- | --- |
| 0–1 | `"X1"` | existente |
| 2 | flags (`DEF_SAVE_HARD`) | existente |
| 3 | livre | — |
| 4–7 | máscara da rede Guardromon | existente |
| 8 | `region_area` (área do mapa-múndi + bit 7 do servidor) | A |
| 9 | `region_chain` | A |
| 10–25 | materiais, 16 × u8 | B |
| 26–27 | réplicas possuídas, 16 bits (índice em `def_replicas[]`) | G |
| 28–29 | desafios DRI destravados, 16 bits | H |
| 30–33 | melhor estágio por desafio, 16 × 2 bits | H |
| 34–47 | reserva | — |

Regras para quem acrescentar um campo:

1. **Gravação:** `def_bestiary_record_write` só grava o registro (e só grava a
   parte `"X1"`) quando há algo não-zero. Todo campo novo entra nas duas
   condições, senão um save que só tem materiais perde tudo.
2. **Leitura:** só com `"X1"`. O registro é zerado antes de gravar, então um
   save antigo lê zero em tudo que é novo, e um save sem nada ganho continua
   byte a byte igual.
3. **Sem checksum:** o checksum (`out[5]`) cobre só a tabela. Valide o que ler
   (material acima do teto vira o teto; `region_area` desconhecida zera a
   cadeia).
4. `def_bestiary_reset` zera o campo, e `def_bestiary_savestate` o registra
   com `PORT_STATE_VAR`; sem isso os estados do servidor MCP não o carregam.
5. **Se faltar espaço**, não cresça o `REC_EXTRA`. Os últimos 0x800 bytes do
   arquivo têm 8 registros de 0x100, e o slot tem `slot_size` 0x2700
   (`src/main/memcard.c:33`); três slots cabem em 0x7800, então sobram
   registros. Um segundo registro por slot é o caminho, depois de confirmar a
   contagem de slots e mudar a parte 5 de `memcard.c`. Relate antes.

O bitmask de réplicas por **ID de item** do texto original não cabe: com cerca
de 402 itens seriam 51 bytes. Por isso a camada G usa o índice na lista de
Réplicas Lendárias.

## Como testar no jogo

Use o servidor MCP `dw2003` (um estado só carrega no mesmo binário que o
gravou, então regrave depois de cada build):

```fish
# estado na primeira batalha
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/port/dw2003 --disc iso/dw2003.cue \
    --script tests/replay/scripts/first_battle_save.json \
    --save-state battle_start:/tmp/battle.state --save-state-exit
# estado no campo com encontros aleatórios (mapa 0x21D), dois frames antes do fim do script
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/port/dw2003 --disc iso/dw2003.cue \
    --script tests/replay/scripts/first_battle_save.json \
    --save-state 41824:/tmp/field.state --save-state-exit
```

Depois `game_start` com `extra_args ["--load-state", ...]`, `mem_write` nos
símbolos estáticos (por exemplo `gamestate_data+0x30` = `encounter_timer`, 1
força um encontro ao andar) e `screenshot`. Confirmar é `CIRCLE`, cancelar é
`CROSS`, o menu do campo abre com `START`.

Testes de unidade: siga `tests/port/definitive_bestiary.py` (compila o `.c`
sozinho contra um driver). Rode sempre `tests/port/definitive*.py`,
`tests/port/text_override.py` e `tests/port/run.py`, e `scripts/test.sh` antes
e depois de cada camada: o build PS1 (matching) e os goldens de `tests/golden`
não podem mudar, e uma falha nova é bloqueio.

---

## Camada A — Cadeia regional e Encontro Raro

### Regras

- **Contador regional (`region_chain`):** +1 a cada batalha **vencida** na mesma
  região.
- **Reset:** trocar de região, usar o teleporte (rede Guardromon) ou sair da área
  zera na hora.
- **Overlevel Rush não conta:** batalha pulada não incrementa (evita Alvo Raro
  durante backtracking). O gancho é `def_rush_results`.
- **Gatilho:** acima de `DEF_HUNT_CHAIN_MIN` (5) vitórias seguidas, o próximo
  encontro aleatório tem chance crescente de ser o Alvo Raro da região
  (`DEF_HUNT_CHANCE_BASE` + `DEF_HUNT_CHANCE_STEP` por vitória além do mínimo,
  teto `DEF_HUNT_CHANCE_MAX`). Use `pad_random` só **depois** de decidir que o
  recurso está ligado, para não mexer na sequência aleatória dos replays.
- **O Rush nunca oferece pular o Alvo Raro.** A decisão do Alvo Raro vem antes
  do bloco do Rush em `fieldstg_encounter_start`; sendo Alvo Raro, o bloco do
  Rush é pulado.
- **Feedback:** sinal discreto na transição (som diferente, ou flash; um callout
  novo `RARE!` em `definitive_callout.c` é barato: acrescente a palavra em
  `styles` e a constante no enum de `definitive.h`).
- **Fuga garantida:** contra Alvo Raro, fugir sempre funciona.

### Implementação

1. Estado: `region_area` (`REC_EXTRA + 8`) e `region_chain` (`+ 9`), como no
   orçamento acima. "Região" = área do mapa-múndi + servidor
   (`ststatus_map_areas[map & 0xFF]`, os 8 bits inteiros), **não** o índice
   `area` de `encounters[area]`.
2. Incremento: no fim de batalha vencida, em `def_log_end(result)`
   (`port/game/definitive.c`, chamado de `wfightmn_800A6440.c:721`). Batalha de
   script e chefe de história não contam: só encontro aleatório (guarde, no
   início do encontro, que a luta veio de `fieldstg_encounter_start`).
3. Reset: em `fieldstg` quando o mapa carregado tem área diferente
   (`fieldstg_80087DB0.c:1699`, onde `def_hard_party_fix()` é chamado), e em
   `dw3_travel_frame` (`port/game/definitive_travel.c`).
4. Troca do encontro: em `fieldstg_encounter_start`, **antes** do bloco do
   Rush (`:5087`). O Alvo Raro é **uma batalha que já existe** em
   `fieldstg_battles[]` (um chefe/sub-chefe), escolhida por região numa tabela
   nova `def_hunt_targets[]` (área, servidor, índice da batalha, escala de
   nível). Não crie inimigos novos. A troca é de `event->battle`; `stage` e
   `music` continuam os do encontro sorteado, para a luta acontecer no cenário
   da região.
5. Escala: ajuste `records_state.enemies[i].level` / `stat_scale` depois de
   `fieldstg_start_battle` copiar os inimigos, para a faixa de nível da região.
   A referência de faixa é o tier dos Cardmons da região `[dono: confirmar a
   tabela antes de aplicar]`.
6. Fuga: no começo de `fightstg_rules_roll_escape`, lado do jogador
   (`side == 0`), devolva 1 quando `def_hunt_active()`, **antes** da checagem
   de `records_state.blocked[11]`: a batalha de chefe usada como Alvo Raro
   pode trazer a fuga bloqueada.
7. `def_hunt_active()` vale da troca até o fim da luta e é limpo em
   `def_log_end`, ganhando ou perdendo.

### Tabela de alvos

`tools/gen_hunt_targets.py` (proposta em `.dumps/tasks/hunt/alvos.md`; `--apply`
escreve `port/game/definitive_hunt_targets.c`). O mapa de cada estágio vem do
comentário do próprio `src/wstag/wstagN.c` ("WSTAG321: stage 0x28A"): o segundo
campo de `fieldstg_stages` **não** é o número do arquivo. Chefe é a regra de
`gen_enemy_weaknesses.py` (`kind` 2 com 9 ou mais `blocked[]`).

A regra do desenho original ("o chefe que a própria região inicia") não serve:
só 13 das 67 regiões com encontro têm chefe próprio, e muitos são superchefes
opcionais muito acima dos encontros (um nível 70 numa área de nível 6). A regra
aplicada: para uma região cujo maior encontro aleatório é T, um chefe do jogo
com nível próprio entre T e T + 15, de preferência um que a própria região
inicia, sem repetir chefe entre regiões enquanto houver outro; luta em T + 3,
nunca abaixo do próprio nível. As batalhas do chefe final (Galacticmon, que em
qualquer mapa rodam as fases finais: `state.type` 4) nunca entram. Resultado:
66 das 67 regiões com alvo. **Aprovada pelo dono em 2026-11-13**; regenerar com outra regra é rodar a
ferramenta de novo.

Até a camada B, o Alvo Raro dá a recompensa da própria batalha de chefe
(`stfgtrep_rewards[battle]`).

### Teste

- Unidade: contador (incrementa, zera ao trocar de área, não conta Rush nem
  batalha de script), curva de chance, com a sequência aleatória simulada; um
  replay script não consome `pad_random` a mais.
- Jogo: estado no mapa `0x21D`, escreva `region_chain` = 9 e
  `encounter_timer` = 1, ande, confira o Alvo Raro, o aviso, que o Rush não
  apareceu e a fuga.

## Camada B — Recompensas do Alvo Raro e Materiais de Caça

### Regras

- **10x XP** em relação ao encontro padrão da área (`DEF_HUNT_XP_MULT`).
- **Carta rara** da tabela de drops dos Cardmons daquela faixa de nível.
- **Peças Raras de Monstro (Materiais de Caça):** itens exclusivos do alvo,
  usados na forja das lojas, no Black Market e nos DRI.

### Implementação

1. **Materiais não são itens novos na tabela do disco.** Não há IDs de item
   livres garantidos e a tela de itens lê a tabela do disco. Guarde os materiais
   como contadores próprios no registro extra do save: um byte por material,
   `DEF_HUNT_MATERIALS` (16) no máximo pelo orçamento, teto
   `DEF_HUNT_MATERIAL_CAP`. Se houver mais regiões com Alvo Raro do que
   materiais, o material é por **tier** de região, não por alvo.
   `[dono: confirmar a lista e os nomes]`
2. XP: em `src/stfgtrep/stfgtrep_80082E70.c:1074`, onde
   `stfgtrep_rewards[...].exp` é lido, multiplique quando
   `def_hunt_take_reward()` disser que a batalha foi um Alvo Raro. Faça como
   `def_overfinish_take_bits` (`:904`): uma função que devolve o valor e limpa o
   estado.
3. Carta: `gamestate_add_card(card, n)` (`src/main/gamestate.c`). A tabela de
   drops dos Cardmons precisa ser levantada (procure `prize_item` em
   `fieldstg_start_battle`: os Cardmons são as espécies `0x1C9..0x1D0`,
   `fieldstg_80087DB0.c:1903`).
4. Material: +1 no contador do alvo, com uma linha na tela de recompensas
   (`data->messages->set_text`, texto novo em `en.txt`).

### Teste

- Unidade: gravação/leitura dos contadores no registro (estenda
  `tests/port/definitive_bestiary_driver.c`; um registro sem os campos novos
  continua lendo como zero, um save antigo continua byte a byte igual quando
  nada foi ganho, e um save que só tem materiais grava e relê os materiais).
- Jogo: vencer um Alvo Raro forçado e ver XP, carta e material.

### Feito (2026-11-13)

- **XP:** `stfgtrep_main_run` recalcula depois do `switch` dos membros:
  `stfgtrep_rewards[b].exp * DEF_HUNT_XP_MULT`, onde `b` é o encontro aleatório
  que o Alvo Raro substituiu (o "encontro padrão da área", não a batalha do
  chefe), dividido entre os membros como o jogo faz (6/10 com dois, 1/3 com três).
- **Carta rara → R-Booster.** A tela de resultados mostra um item só, e as
  cartas chegam em pacotes. Quando a batalha não deixou item, o Alvo Raro deixa
  um R-Booster (itens `0x186..0x18A`): 01 abaixo do nível 25, um a mais a cada
  `DEF_HUNT_PACK_LEVELS` (15) níveis, 05 a partir do 70. É **um** pacote
  (`DEF_HUNT_PACKS`, decisão do dono em 2026-11-13): o x15 do PRD 3
  (`DEF_CARD_BOOST_PACKS`) fica para os Boosters comuns e deixaria o raro farto
  demais. A tela marca o Booster que deu (`def_hunt_take_pack`) e só ele conta
  como um (`def_hunt_pack_count`). Não usei a tabela dos Cardmons.
- **Materiais:** um por batalha de alvo, no campo `material` da tabela
  (gerado por `tools/gen_hunt_targets.py`), na ordem do menor nível em que a
  batalha luta: o alvo mais fraco dá o primeiro. A primeira versão usava faixas
  de 6 níveis do alvo, mas os alvos da tabela lutam em poucos níveis
  (15, 34, 38 a 65) e 9 dos 16 materiais ficavam sem fonte. Com as 15 batalhas
  distintas da tabela, 15 materiais têm fonte; o 16º (Obsidian Digizoid) fica
  livre para um alvo novo. Se houver mais batalhas que materiais, elas dividem
  os materiais na mesma ordem. Nomes, do mais fraco ao mais forte: Bit Shard,
  Byte Shard, Data Chip, Code Chip, Core Fragment, Core Crystal, Mega Crystal,
  Giga Crystal, Tera Prism, Peta Prism, Chrome Digizoid, Blue Digizoid, Red
  Digizoid, Gold Digizoid, Black Digizoid, Obsidian Digizoid (`$HuntMat0..15` em
  `en.txt`; aprovados pelo dono em 2026-11-13). Contador em `REC_EXTRA + 10..25`, teto 99;
  a leitura corta um valor acima do teto, porque nenhum checksum cobre o
  registro. O material entra na vitória (`def_hunt_battle_end`), não na tela, e
  por isso não se perde se o jogo fechar nela.
- **Linha na tela:** "Hunt material:" + nome, no layout do "Item gained:"
  (ESFGTRPT 11), depois da linha do item. É o passo 22 de `stfgtrep_main_run`,
  que estava livre (o 22 de `stfgtrep_member_run` é outro). A recompensa
  pendente é limpa no fim da tela (passo 50) e no começo da próxima batalha
  (`def_log_begin`), para uma tela que nunca rodou não passar adiante.
- **Teste de unidade:** estendi `tests/port/definitive_hunt_driver.c`, que já
  linka `definitive_bestiary.c`, em vez do driver do Bestiário: material na
  vitória, XP e Booster pendentes, linha mostrada uma vez, limpeza, faixas,
  registro que só tem materiais grava e relê, valor acima do teto lê como o
  teto. Um jogo novo continua gravando o registro todo zero.
- **Teste no jogo:** Alvo Raro forçado (mapa 0x21D, batalha 323, nível 15),
  três membros: "EXP gained: 60 EXP" para cada um, "BIT gained: 275 BIT",
  "Item gained: R-Booster 01", "Hunt material: Bit Shard" (a batalha 323 é a
  mais fraca da tabela), e o contador do material 0 foi de 0 a 1. Frames em `.dumps/tasks/hunt/item_*.ppm`.

### Emenda do dono (2026-11-13): materiais comuns e raros por região

Substitui os bullets "Materiais" e "Linha na tela" acima (o resto do Feito vale):

- **Comuns:** os 16 nomes aprovados são materiais comuns. Qualquer inimigo de
  uma batalha aleatória vencida (ou que o Rush levou) deixa o material da sua
  espécie com `DEF_HUNT_COMMON_PCT` (25%). A espécie → material vem da
  ferramenta (`def_hunt_commons`): as espécies que aparecem em batalha
  aleatória, na ordem do maior nível em que aparecem, em 16 grupos iguais, os
  mais fracos primeiro (`.dumps/tasks/hunt/materiais.md`). Contador no registro
  1, como antes (`REC_EXTRA + 10..25`, até 99). As receitas da forja podem
  pedir quantidades bem altas: os raros trocados no Black Market repõem
  comuns (pedido do dono).
- **Raros:** um por região com Alvo Raro (66), variante numerada do item do
  Digimon à frente da batalha: "Datamon Core I" … "XIII". As regiões cujo alvo
  tem o mesmo Digimon à frente são numeradas pelo nível (campo `variant` da
  tabela). Na batalha o Alvo Raro aparece com o número ("Datamon IV"), no HUD e
  no Inspect, para não parecer o mesmo Digimon com nível maior. Os raros são
  gastos: forja (camada E), Black Market (G, troca por comuns) e DRI (H, uma
  seleção de raros destrava uma simulação). Contador até `DEF_HUNT_RARE_CAP`
  (99) e um bit de "já tive" por raro, num **segundo registro** do cartão
  (parte 6 de `memcard.c`, em `0x7B00 + slot * 0x100`, a área livre depois dos
  três primeiros; "HUNT", versão, checksum), lido e gravado depois do primeiro
  (passos `0x49` e `0x37` do STGMCARD). Sobra espaço nele para as camadas E–H.
- **Tela de resultados:** uma linha por material, depois do item: "Hunt
  material: Bit Shard x2" para cada comum, "Rare material: Datamon Core IV"
  para o raro, uma por toque (passo 22 em laço).
- **Nomes compostos em tempo de execução:** o nome da espécie vem do disco
  (texto 0x4E) e o resto é nosso (`$HuntRareWord`, `$HuntRoman*`, `$HuntSpace`
  entre aspas, que o carregador agora preserva), sem embutir texto do jogo.
- **Teste (2026-11-13):** o driver da caçada cobre comuns (sorteio por
  inimigo, erro, derrota, batalha de roteiro, Rush), raros (contagem, teto,
  "já tive", o nome numerado só no Alvo Raro) e o segundo registro (grava,
  relê, todo zero sem nada, checksum, ausente). A ferramenta confere a tabela
  (66 regiões, numerais I..XIII) e os comuns (193 espécies, 131 com material,
  os 16 com espécie). No jogo: o Alvo Raro da região 0x1D aparece como
  "Bulbmon III" no HUD e a tela mostra "Item gained: R-Booster 01" e "Rare
  material: Bulbmon Core III" (raro 20 de 0 a 1); um encontro comum vencido deu
  "Hunt material: Bit Shard x1"; o Bestiário mostra "Hunt Bit Shard x7"
  (Kunemon) e "Hunt -" / "Rare Cores 2/11" (Bulbmon). **Falta** gravar e
  carregar um save no jogo para ver o segundo registro no cartão.

## Camada C — Bestiário mostra os materiais

- Cada entrada do Bestiário mostra os materiais que o Digimon derruba, para o
  jogador saber onde caçar cada peça.
- A tela hoje tem 5 linhas de família de elemento e sobra espaço embaixo: a
  quinta linha termina em `BS_ROW_Y + 5 * 14` = `0x86` e a dica está em
  `BS_HINT_Y` = `0xA5`, ou seja, 31 pixels, duas linhas de 14. Use esse espaço:
  "Drops:" + nome do material + quantidade em mãos. Só aparece para espécie com
  algo conhecido (`def_bestiary_known(index) != 0`); desconhecida mostra `???`.
- Onde: `fieldmenu_bestiary_draw` em `src/main/fieldmenu.c`. As janelas
  `bs_info[6]`, `bs_info[7]` e `bs_states[5]`, `bs_states[6]` estão livres
  (recebem texto vazio hoje).
- Teste: screenshot da tela para um alvo com material e para uma espécie sem.
- **Emenda (2026-11-13), com o modelo do dono:** a primeira linha mostra o
  comum da espécie e quantos o jogador tem; a segunda, só para espécie que é o
  Digimon à frente de algum Alvo Raro, os raros dela como coleção ("Rare Cores
  4/13": quantos das regiões dela já teve, de quantos; laranja quando completa).

### Feito (2026-11-13)

- Duas linhas abaixo das famílias, com uma régua acima: o rótulo "Hunt"
  (`$HuntDrops`, o mesmo nome da linha "Hunt material:" da tela de resultados;
  "Drops" invadia a coluna dos nomes), o nome de cada material na coluna dos
  nomes das famílias (x 42) e a quantidade em mãos ("x12", cinza em zero) em
  `BS_HELD_X` (136). Espécie desconhecida mostra `???`; conhecida que não luta
  em nenhuma batalha de alvo mostra `-`.
- Espécie → materiais: `def_hunt_species_materials(espécie)` percorre as
  batalhas da tabela (`fieldstg_battles[...].enemies`, já compiladas no port) e
  marca o material de cada batalha em que a espécie luta. Nenhum dado novo é
  gerado. Com a tabela de alvos, 22 espécies do Bestiário derrubam algo, no
  máximo 2 materiais (Bulbmon: batalhas 323 e 315). Uma espécie com mais de 2
  mostraria só os 2 primeiros.
- Janelas: `bs_info[6..7]` (nomes) e `bs_states[5..6]` (quantidades), que
  estavam livres, e uma nova, `bs_drops`, para o rótulo.
- Largura: o nome mais longo, "Obsidian Digizoid", mede 89 px na fonte do jogo
  (medido na tela de resultados). De x 42 vai até 131, antes da quantidade.
- Teste de unidade: o driver da caçada confere os materiais por espécie (duas
  batalhas, uma, nenhuma, e o espaço vazio, espécie 0).
- Teste no jogo: Kunemon conhecido e sem alvo (`-`), espécie desconhecida
  (`???`), Bulbmon com Bit Shard x3 e Core Fragment x12, BK Imperialdramon com
  Core Crystal x0 em cinza (`.dumps/tasks/hunt/bestiary_c2.ppm`).
- PS1: `src/main/fieldmenu.c` pré-processa igual sem `PC_PORT`.

## Camada D — Rebalanceamento de preços

### Regras

- **Revenda:** reduzir o valor de venda de equipamentos raros/de leilão que
  quebram a economia. Exemplo do dono: item comprado por 2.000 Bits e vendido
  por 30.000 deve cair para 7.000–10.000.
- **Dreno:** aumentar o custo de compra de equipamentos avançados, para dar
  destino aos Bits do OVERFINISH.

### Implementação

1. Levante a tabela antes de mudar: escreva `tools/gen_item_prices.py` que lista
   todo item com preço de compra, preço de venda e onde é vendido
   (`src/stitshop/`; os registros de item são `records_items[]` e
   `records_funcs.get_item`). Entregue a lista ao dono com a proposta.
   `[dono: aprovar a tabela]`
2. Aplique como patch em memória no início (o modelo é `def_mp_hook` /
   `patch_techniques` em `definitive.c`): nunca edite o arquivo do disco.
3. Constantes: `DEF_SELL_CAP_PCT` (teto de revenda em % do preço de compra) e a
   tabela de exceções gerada. No exemplo do dono, a venda nova (7.000–10.000)
   fica entre 350% e 500% do preço de compra (2.000): um teto só em % do preço
   de compra não reproduz isso. A tabela de exceções guarda o **valor absoluto**
   de venda dos itens que fogem da regra.

### Teste

- Unidade: a tabela gerada bate com o disco (como `definitive_mp.py` faz para o
  MP).
- Jogo: loja de Asuka (`shop_open` é um checkpoint do script de replay).

### Feito (2026-11-13, aplicado direto por decisão do dono)

- **O que levantei:** os preços são do próprio jogo compilado (`records_items`
  em `src/main/records.c`), não do disco. Toda revenda é metade da compra; nada
  vende por mais do que custa. Os equipamentos de 60000 (Ronin Blade, Brave
  Sword, Dramon Horn...) são da loja 18 e revendem por 30000; achados ou ganhos
  barato (o exemplo do dono), viram 30000 Bits. Não há leilão no código; o
  "2.000" do exemplo é a via barata, qualquer que seja.
- **Regra** (`tools/gen_item_prices.py`, constantes em `definitive.h`): arma,
  armadura ou acessório vendido em loja com preço a partir de
  `DEF_PRICE_KNEE` (10000) custa `DEF_PRICE_DRAIN_PCT` (150%) e revende por
  `DEF_SELL_BASE` (5000) + `DEF_SELL_ABOVE_PCT` (8%) do que o preço antigo
  passava de 10000, nunca mais que metade do novo, arredondado a 100. Um 60000
  passa a custar 90000 e revender 9000 (o exemplo do dono: 7000–10000); um
  10000 custa 15000 e revende 5000. O resto fica como no jogo, inclusive os
  equipamentos que nenhuma loja vende (revenda até 2000). Lista completa em
  `.dumps/tasks/prices/precos.md`.
- **Como entra:** a tabela gerada (`port/game/definitive_prices.c`) e
  `def_item_buy` / `def_item_sell` (`port/game/definitive_economy.c`); as duas
  unidades da loja leem todo preço por elas sob `PC_PORT` (macros
  `STITSHOP_BUY`/`STITSHOP_SELL`, e o `u16 price` local vira `s32`, porque um
  preço novo passa de 65535). `records_items` não é alterado; num script de
  replay os preços são os do jogo.
- **Teste:** `tests/port/definitive_economy.py` (o driver: tabela, preço
  acima do u16, item fora da tabela, replay; e `gen_item_prices.py --check`: a
  tabela é a da regra e um item mais caro nunca revende por menos).

## Camada E — Forja nas lojas tradicionais

### Regras

- **Requisito duplo:** lojas de armas, armaduras e equipamentos avançados mantêm
  a interface nativa de compra, mas exigem Materiais de Caça + Bits para
  equipamentos intermediários e de topo. Os básicos iniciais continuam só com
  Bits, para não travar o começo.
- **Sem menu extra:** nada de tela de crafting. A validação acontece na janela
  da loja, que mostra os requisitos da receita ao selecionar o equipamento.
- **Rastreável:** todo material exigido aparece no Bestiário (camada C).

### Implementação

1. Tabela `def_recipes[]`: item, material A + quantidade, material B +
   quantidade (0 = nenhum). Gerada e aprovada pelo dono. `[dono]`
2. Em `src/stitshop/stitshop_8008321C.c` a loja olha o dinheiro em quatro
   pontos, e cada um precisa considerar a receita:
   - na seleção (`:291`), recuse com `def_recipe_can_buy(item)`;
   - no máximo comprável (`:322` e `:363`), limite também pelos materiais em
     mãos;
   - no débito (`:446`), chame `def_recipe_pay(item, count)`.
   A recusa usa o mesmo som e a mesma mensagem de "sem dinheiro", com um texto
   novo ("Missing materials").
3. Ao selecionar um item com receita, mostre a linha de requisitos na caixa de
   descrição (texto montado como `def_train_pity_label` monta o seu).

### Teste

- Unidade: `def_recipe_can_buy` / `def_recipe_pay` com contadores simulados,
  incluindo compra de várias unidades.
- Jogo: comprar com e sem material, e a quantidade máxima limitada pelos
  materiais.

### Feito (2026-11-13, aplicado direto por decisão do dono)

- **Receitas** (`tools/gen_recipes.py`, constantes `DEF_RECIPE_*`): pelo preço
  antigo de arma, armadura ou acessório vendido em loja. Abaixo de 5000, só
  Bits. De 5000 a 20000 (intermediário), um comum (do 3º ao 9º, pela ordem de
  preço), 10 a 25 dele. De 20000 em diante (topo), um dos sete comuns de cima,
  30 a 50 dele, e um raro de região: os raros pela ordem do nível do alvo,
  espalhados pelos itens de topo pela ordem de preço, um (dois a partir de
  50000). É o modelo do dono: comuns em grande quantidade, raros para o
  melhor equipamento. Lista em `.dumps/tasks/forge/receitas.md`.
- **Na loja** (`stitshop_8008321C.c`, sob `PC_PORT`): na seleção, sem os
  materiais, a recusa é a mesma da falta de dinheiro com o texto "You don't
  have enough materials."; o máximo comprável fica limitado pelo que os
  materiais fazem; a compra tira os materiais com os Bits. No painel do item,
  a linha do slot mostra os requisitos ("Data Chip 12/30, Datamon IV 0/1": o
  raro pelo nome do Alvo Raro que o deixa, que cabe na linha), laranja
  enquanto falta.
- `definitive_glyph.c`: os textos montados (string, contagem, algarismo
  romano) saíram de `definitive_hunt.c` para servir a loja também.
- **Teste:** o driver da economia (limite pelos materiais, o mais escasso de
  dois, pagamento de várias unidades, item sem receita, rótulo e cor, replay)
  e `gen_recipes.py --check`.

## Camada F — Novos equipamentos (build crafting)

### Regras

- **Foco em 2–3 Digimon:** o jogador concentra a progressão numa equipe
  reduzida (e no Modo Difícil são 2), então os equipamentos precisam oferecer
  escolha tática, não "espada B sempre substitui espada A".
- **Reaproveitar assets:** varrer a tabela de itens e criar variações de armas,
  armaduras e acessórios reutilizando ícones do disco (muda nome, descrição,
  custo e modificadores).
- **Arquétipos**, distribuídos pelas lojas ao longo das regiões:
  - **Speed / Ritmo:** agilidade para habilitar ataque duplo já no primeiro
    turno.
  - **Elemental / Técnica:** amplificam afinidades elementais, potencializando
    `WEAK!` e o One More.
  - **Defensivo / Tanque:** sobrevivência contra chefes de alto dano (Encontros
    Raros, estágios avançados dos DRI).

### Implementação

1. **Risco principal:** não se sabe se há IDs de item livres. Primeiro levante
   com a ferramenta da camada D quais IDs existem (cerca de 402 em
   `records_items[]`), quais nunca são vendidos nem dropados (candidatos a
   reaproveitar) e como o nome/descrição é lido (`ITMNAM` e afins, via
   `def_text_override`). **Pare e relate ao dono antes de criar qualquer
   item.** `[dono]`
2. Variações entram como patch em memória da tabela de itens + override de
   texto, nunca no disco.
3. A afinidade elemental já é somatório por par (Água+Gelo, Trovão+Máquina):
   `def_affinity` em `port/game/definitive_mask.c`. Equipamento elemental mexe
   nos resists do portador, que é de onde a afinidade sai.
4. Ataque duplo: a regra está em `def_enemy_acts_twice` /
   `fightstg_events.get_delay`. Calibre os bônus de Speed contra ela.

### Teste

- Unidade: a tabela de variações é consistente (ID existe, ícone existe, texto
  cabe).
- Jogo: equipar uma de cada arquétipo e conferir o efeito na batalha.

### Feito (2026-11-13, aplicado direto por decisão do dono)

- **O que levantei:** não há ID livre (`records_items` vai de 1 a 402, o inventário do save é `items[0x193]`, nenhum
  item vazio ou duplicado), então cada variação é um item de loja **convertido no lugar**: mesmo ID, ícone (`type`),
  slot, grupo, quem usa, carisma, preço e receita da forja; mudam o poder ou a defesa, a precisão ou a evasão, os dois
  bônus e o nome e a descrição. Item "estritamente dominado" no próprio tier (outro do mesmo slot, que todo usuário dele
  pode usar, não mais caro, pelo menos igual em todo stat) só há dois: Long Sword e Kulon Sword. A regra de escolha
  abaixo começa por eles e segue pela redundância. Os stats saem dos dados do item toda vez que são usados
  (`gamestate_get_stats`, a prévia da loja, `fightstg_rules_get_stats`): nada fica gravado no save, então um item
  equipado num save antigo passa a ter os stats da variação.
- **Arquétipos e regra** (`tools/gen_equipment.py`, constantes `DEF_EQUIP_*`): um de cada em cada tier de loja (as 9
  listas de armas e armaduras; a loja 18, só peças de conjunto, fica de fora), portanto em toda loja de equipamento da
  campanha.
  - **Speed**, arma de uma mão (não cajado nem braçadeira): poder da melhor arma do tier menos 50%, Boost completa o
    orçamento (+5 no tier 1 a +85 no 9, 15% a 25% do Boost esperado do rookie no nível da loja). Calibrado pela regra
    definitiva de turnos: o jogador abre e o primeiro turno do inimigo vem depois do atraso inteiro dele
    (`1000 * raiz(Boost nosso / Boost dele)`, 707 a 1414), então quem tem mais Boost que o inimigo age duas vezes antes
    dele, e no longo prazo faz `Boost / Boost do inimigo` ações por ação dele (até 2).
  - **Elemental**, peça de cabeça: defesa da melhor do tier menos 50%, as resistências de uma família completam o
    orçamento (Água+Gelo e Trovão+Máquina divididos no par, que `def_affinity` soma; Trevas pesa o dobro), no máximo
    400 por elemento: de +85 Fire no tier 1 a +355 Wind no 9 (afinidade +0,025 a +0,107 no WEAK! e de novo no alvo
    aberto). A família é a do elemento que o item já tinha, senão a menos usada até ali; as cinco aparecem.
  - **Tank**, corpo ou escudo: defesa da melhor do tier mais 50%, Espírito 25% dela, Boost negativo que volta ao
    orçamento (−6 a −30).
  - **Orçamento:** cada stat vale um peso em pontos de Power (`DEF_EQUIP_W_*`: Guard, Spirit e Boost 1; Wisdom 0,5;
    precisão e evasão 0,25; uma resistência 0,05, porque no combate definitivo ela só dá afinidade). Uma variação nunca
    passa a maior pontuação dos itens comuns do tier no mesmo slot que dividem um usuário com ela.
  - **Escolha:** dominado primeiro; depois o traço (um item que já puxa para o arquétipo antes, Wing Sword e Fenrir
    Sword para Speed, Wool Cap e Night Vision para Elemental; um com bônus de outro arquétipo por último, para o Kung
    Fu Cap de velocidade e os itens de Espírito e Sabedoria ficarem); depois a redundância (quem pode usar continua com
    uma opção comum do mesmo slot pelo menos tão forte); depois o mais barato. Nunca: item com efeito por ID no
    FIGHTSTG (veneno, crítico...), com tipo forte, ou de conjunto; os acessórios ficam intactos (inclusive os Fire
    Power da camada H). Lista completa em `.dumps/tasks/equipment/variantes.md`.
- **Como entra:** a tabela gerada (`port/game/definitive_equipment.c`) e `def_variants_hook`
  (`port/game/definitive_variants.c`), que grava os valores nos `RecordsWeapon`/`RecordsArmor` dos itens toda vez,
  como o MP dos rookies: chamado dentro de `def_mp_hook` (jogo novo, início de batalha) e a cada mapa de campo
  (`fieldstg_80087DB0.c`, no bloco `PC_PORT` do layer A), para um save carregado do título já ter as variações antes da
  primeira loja ou tela de status. Valores absolutos: rodar de novo grava os mesmos números, nunca acumula; o reset
  restaura a tabela do disco e o próximo gancho grava de novo. Num script de replay nada é gravado, e os nomes e
  descrições (`ITMNAM`/`ITMINF` em `tools/text_override/en.txt`, entrada = ID do item) também não aparecem:
  `def_text_override` devolve o texto do disco quando `def_variant_text_hidden` diz que o texto é de uma variação
  (`DEF_EQUIP_ON` 0 desliga tudo).
- **Preços e receitas:** herdados do item (as tabelas das camadas D e E são por ID). Cada variação está no orçamento do
  tier, então o preço do tier serve, e mudar preço mexeria nas duas regras (as duas leem o preço antigo).
- **Teste:** `tests/port/definitive_variants.py` (o driver com os dados de `records.c`: num replay tudo fica igual e os
  textos das variações ficam escondidos; com `def_on` as 27 variações têm os stats da tabela e o resto delas e dos outros
  itens fica; o segundo gancho não muda nada; linha de categoria errada não grava; os IDs dos arquivos de texto são os
  que o `gen_text_override.py` resolveu) e `gen_equipment.py --check` (tabela = regra, orçamento, nenhuma variação é
  upgrade direto de outro item do tier, nomes até 15 letras e descrições até 34). No jogo, o script do lab
  `equip_ingame.py` (fora do repositório, como os das camadas anteriores; quatro batalhas com os mesmos sorteios: equipamento próprio, depois uma variação de
  cada arquétipo no líder; stats antes e depois, o bloco de stats da batalha, a linha `twice` e quantos turnos do
  jogador vêm antes do primeiro do inimigo).

## Camada G — Black Market

### Regras

- **Acesso:** um NPC figurante reaproveitado numa área secundária/beco. Sem
  assets novos e sem loja comum nova.
- **Regra de ouro (anti-exploit):** tudo que sai do Black Market é "Réplica /
  Dados Piratas": 100% dos atributos e efeitos, **revenda 0 Bits**.
- **Duas abas:**
  - **Câmbio de Materiais:** trocar Peças Raras excedentes por materiais
    específicos que faltam para uma receita.
  - **Réplicas Lendárias (itens perdíveis):** equipamentos únicos de leilão ou
    de eventos perdidos, por 2 ou mais Peças Raras de chefes **distintos** + uma
    quantia alta de Bits.

### Implementação

1. NPC: os atores são `FieldstgPlacedActor` nos `src/wstag/wstag*.c` (id, x, y,
   falas). Escolha um figurante com fala genérica num mapa secundário e proponha
   ao dono. `[dono]` A interação é interceptada como a rede Guardromon faz com
   o `GOTO_MAP`: um gancho no ponto onde o ator é falado
   (`fieldstg_actor_talk_to`).
2. Tela: a mais barata é reaproveitar a loja (`STITSHOP`) com uma lista própria
   e os preços em material, ou um painel de texto em polígonos como o do Rush.
   Decida depois de ler `stitshop_8008321C.c`; relate a escolha.
3. Réplica com revenda 0: o item é o mesmo ID, então "réplica" precisa de um
   bit por item possuído. A lista `def_replicas[]` (no máximo `DEF_REPLICAS`)
   dá a cada Réplica Lendária um índice, e o registro guarda um bit por índice
   (`REC_EXTRA + 26`). Enquanto o jogador tiver a réplica de um ID, a venda
   daquele ID paga 0, mesmo que ele também tenha um original.
   `[dono: aceitar essa simplificação?]`
4. Lista de Réplicas Lendárias: levantar os itens de leilão/evento perdíveis.
   `[dono: lista]`

### Teste

- Unidade: troca de materiais (não cria nem destrói valor fora da tabela),
  exigência de chefes distintos, revenda 0, bit de réplica gravado e relido.
- Jogo: as duas abas, e tentar vender uma réplica.

### Feito (2026-11-13, aplicado direto por decisão do dono)

- **Acesso:** um figurante da cidade fala pelo Black Market. FIELDSTG pergunta a
  `def_bm_talk(mapa, key1)` no `fieldstg_actor_talk_to` (depois do laço dos
  redirecionamentos); quando é o comerciante, a fala do jogo não acontece e a
  tela abre no quadro ocioso seguinte (`def_bm_frame`, logo depois de
  `dw3_travel_frame` no passo 0 do `fieldstg_manager_update`). O comerciante é
  uma linha da tabela `traders` (`port/game/definitive_market_screen.c`):
  mapa 0x20C, ator 42 (WSTAG240, na cidade da pousada 0x20A: uma fala só,
  sempre presente, sem flag e sem evento) e a mesma casa na segunda metade do
  jogo (0x27B, WSTAG930). `[dono: confirmar o figurante]`;
  `tools/bm_npc_candidates.py` lista os candidatos de cada cidade com a fala
  decodificada (precisa do disco).
- **Tela** (modelo da rede Guardromon: painéis em polígonos, a fonte do jogo,
  `event_running` enquanto está aberta, o jogador parado como o menu do campo
  faz). Duas abas, L1/R1 (ou esquerda/direita):
  - **Câmbio:** os raros em mãos, um por linha ("Datamon IV x2"). Confirmar
    num deles escolhe o comum (cima/baixo ou esquerda/direita pelos 16, com
    "Get: Data Chip x12   Held: 30/99"); cada confirmação troca **um** raro
    por `DEF_BM_COMMONS` (12) daquele comum. A troca só acontece se os 12
    couberem no teto (`DEF_HUNT_MATERIAL_CAP`): nenhuma troca cria nem perde
    nada fora da taxa. `[dono: 12 por raro, qualquer raro por qualquer comum]`
  - **Réplicas:** as `DEF_REPLICAS` (16) de `def_replicas`
    (`tools/gen_replicas.py`): equipamento que nenhuma loja vende, com preço de
    venda, que nenhum evento cita (flag de item 0x80..0x8E numa lista de flags
    ou em `set_flag`/`get_flag`, e `gamestate_cond_items`: as armas da
    missão do ferreiro ficam fora), os 16 de maior venda, empate pelo ID: os
    sete Power S, Machinegun, Magician Rod, Ghost Fang, Pulse Guard, Drill
    Horn, Chain Saw, Naginata, Recover Crest e MP Mega Proxy. Preço:
    `DEF_BM_REPLICA_BITS` (20) vezes a venda (40000, 30000, 25000, 20000) e
    `DEF_BM_REPLICA_RARES` (2) raros de chefes **distintos** (a espécie à
    frente do Alvo Raro: "Datamon IV" e "Datamon VII" são o mesmo chefe). A
    tela escolhe os dois mais fartos (empate: o alvo mais fraco) e mostra
    quais ("Cost: Datamon IV, Bulbmon III"); compra com duas confirmações, a
    segunda avisando que a réplica revende por 0. Pode comprar de novo (uma por
    Digimon). `[dono: lista e preços]`
- **Revenda 0** (a simplificação do PRD): um bit por réplica no registro 1,
  `REC_EXTRA + 26..27` (`def_record_replica`, gravado, relido, zerado num jogo
  novo, no estado do MCP; um registro mais antigo lê zero). Com o bit ligado,
  `def_item_sell` paga 0 por aquele ID, mesmo com um original junto; como a
  lista de venda da loja pula o que vale 0, a réplica nem aparece para venda.
  O bit desliga quando o jogador não tem mais nenhum daquele ID, na bolsa nem
  equipado (`def_bm_sweep`, a cada quadro do campo): os pontos em que a
  contagem muda (venda, evento, descarte) não têm gancho próprio, e até lá a
  loja só paga 0 por um ID que nenhuma loja vende.
- **Textos:** `$BM*` em `en.txt`; constantes `DEF_BM_*` e `DEF_REPLICAS` em
  `definitive.h`. Num script de replay (`def_on`) nada muda.
- **Teste de unidade:** `tests/port/definitive_market.py` (o câmbio com a
  taxa e o teto, 400 trocas sem valor criado nem perdido, chefes distintos,
  compra e recusas, revenda 0 e o fim do bit, equipado conta, o registro
  grava e relê, registro antigo, jogo novo todo zero, replay) e
  `gen_replicas.py --check`; o driver da economia confere a revenda 0 em
  `def_item_sell`.
- **Teste no jogo:** `market_ingame.py` (estado do campo do mapa 0x21D, a
  tela aberta pelo pedido, uma troca, uma compra, uma recusa, o fechamento, a
  varredura; e a lista de venda da loja com a réplica, se o script tiver o
  checkpoint `shop_open`).

## Camada H — DRI Agents: simulações de evolução em cadeia (Gauntlet)

### Estrutura e acesso

- **Menu centralizado:** falar com um DRI Agent abre a lista de simulações
  (várias linhas evolutivas e desafios especiais). Não é só pós-game: é uma
  arena de desafios acessível durante toda a jornada.
- **Desbloqueio permanente:** cada desafio pede a entrega de Materiais de Caça
  específicos **uma vez** e fica destravado para sempre. Os de topo (linhas Mega
  completas, alvos como Beelzemon) pedem materiais de Encontros Raros de regiões
  avançadas.
- **Sem game over:** derrota na simulação só encerra o desafio e devolve o
  jogador ao mapa, sem perda de progresso.

### Combate (evolução sequencial)

- **Inimigos espelhados:** os modelos 3D dos Digimon jogáveis no slot do
  oponente, com falas temáticas inspiradas nos Tamers clássicos (linhas
  Agumon/WarGreymon, Gabumon/MetalGarurumon, Guilmon/Gallantmon).
- **Linhas incompletas / especiais:** para Digimon sem linha completa no disco
  (Guardromon sem Terriermon, Beelzemon solo), monte a escada com chefes
  temáticos equivalentes da ISO ou trate como desafio único de alto nível.
- **Transição em tempo real:** ao zerar o HP de um estágio (Agumon → Greymon →
  MetalGreymon → WarGreymon) a batalha **não termina**:
  1. dispara a animação de Digivolução no oponente;
  2. restaura 100% do HP máximo da nova forma;
  3. mantém o estado da party do jogador (HP/MP/itens gastos), exigindo gestão
     de recursos na sequência inteira.

### Recompensas (Acessórios de Sinergia)

O jogador não precisa vencer a linha inteira de primeira. Cada estágio derrotado
libera um tier superior do acessório temático da linha/elemento:

- até **Champion:** Tier 1 (bônus base de atributo/afinidade);
- até **Ultimate:** Tier 2 (bônus intermediário + sinergia elemental/Speed);
- até **Mega** (linha completa): Tier 3 (efeito máximo de build crafting).

### Implementação

Esta é a camada mais arriscada. Faça **primeiro uma prova de conceito** e relate
antes de construir o resto.

1. **Prova de conceito (pare e relate):** numa batalha comum, quando o HP do
   inimigo chega a 0, em vez de enfileirar o nocaute
   (`fightstg_events_add_knockout(0x10)`, `fightstg_8008D3B4.c:947`; há 15
   chamadas, comece por essa e confirme com o log de batalha qual delas a
   morte por dano usa), trocar a espécie do membro inimigo, restaurar o HP e
   tocar a animação de digivolução. Peças que já existem:
   - o inimigo caído que continua em cena é exatamente o que o OVERFINISH
     explora (o evento de nocaute fica atrás do turno da party);
   - a digivolução do jogador está em `fightstg_digivolve_update`
     (`src/fightstg/fightstg_80086A00.c`) e a animação curta/pulável em
     `definitive_progression.c` (`def_evo_form_seen`);
   - os modelos entram por `slots->add(slots, slot, model, ...)` e
     `fightstg_models.get`.
   Pergunta a responder: um modelo de Digimon jogável carrega e anima no slot
   `0x10`? Se não, a camada inteira muda para "chefes equivalentes da ISO".
2. Estado: bitmask de desafios destravados (`REC_EXTRA + 28`) e melhor estágio
   por desafio, 2 bits cada (`+ 30`), no registro extra do save.
3. DRI Agents: localize os atores como os Guardromon foram localizados (procure
   o nome de quem fala nos arquivos de diálogo com `tools/dump_text.py` e cruze
   com `FieldstgPlacedActor` nos `wstag*.c`).
4. Menu: lista em duas colunas no estilo do Bestiário ou painel em polígonos;
   decida depois da prova de conceito.
5. Sem game over: a derrota em batalha manda para o mapa `0xE00`
   (`wfightmn_800A6440.c:817`, `set_next_map(0xE00, 0)`); numa simulação,
   volte para `gamestate_data.field_map`.
6. Acessórios de Sinergia: saem da camada F (mesma tabela de variações).
   `[dono: lista de linhas, falas e bônus por tier]`

### Teste

- Unidade: máquina de estágios (zera HP → próxima forma, último estágio →
  vitória), desbloqueio único, tier por estágio, melhor estágio só sobe.
- Jogo: uma linha completa, uma derrota no meio (voltar ao mapa sem perder
  nada), party mantendo HP/MP entre estágios.

## Camada I — Resultados do Rush flutuantes, sem sair do campo

### Regras

- **Só para o Overlevel Rush.** Batalha lutada continua indo para a tela de
  resultados de sempre (STFGTREP, mapa `0x1400`).
- **Sem transição e sem trava:** aceito o Rush, a recompensa é aplicada na hora
  e um painel pequeno aparece perto do personagem por
  `DEF_RUSH_FLOAT_FRAMES`, com o jogador andando livre. Um segundo Rush com o
  painel ainda na tela substitui o painel.
- **Conteúdo:** uma linha por membro (`+EXP`, e `LV UP!` quando subiu), os
  Bits e o item. Técnica aprendida ou forma nova aparecem como uma linha a
  mais (`NEW TECH!`). `[dono: o painel basta para essas, ou nesses casos o
  Rush cai na tela de resultados completa?]`
- **Mesmo resultado que a tela:** o estado do jogo depois do Rush flutuante é
  idêntico ao do Rush de hoje (XP, níveis, stats, técnicas, Bits, item,
  cartas). Muda só a apresentação.

### Por que não reaproveitar a tela de resultados no campo

A tela de resultados é um estágio próprio: `STFGTREP` carrega seus arquivos
(`stfgtrep_load_files`), cria seus objetos e layers e roda como mapa `0x1400`.
Desenhá-la em cima do campo exigiria carregar esse estágio dentro do FIELDSTG,
com heap e ordering table divididos, que é a parte cara e arriscada. Ela também
mistura lógica e animação numa máquina de passos (`stfgtrep_member_run`). O que
se reaproveita é a **lógica** dela, sem tela, e o **visual** do painel escuro
com texto da fonte do jogo, que o campo já sabe desenhar.

### Implementação

1. **Prova de conceito (pare e relate):** a recompensa sem tela. As peças são
   funções soltas em `src/stfgtrep/stfgtrep_80082E70.c`:
   `stfgtrep_add_exp(digimon, exp)` (`:1313`, que chama
   `stfgtrep_raise_stats` ao subir de nível), `stfgtrep_learn_technique`
   (`:1373`) e o crédito de Bits (`:911`). O XP de cada membro sai de
   `stfgtrep_member_count_exp` / `stfgtrep_member_get_exp` (`:424`, `:713`),
   que levam em conta o `party_xp` e o `xp_boost` fixados no PRD 3. Escreva
   `def_rush_apply_quiet()` que, a partir de `records_battle_results`
   (preenchido por `def_rush_results`), aplica tudo isso na mesma ordem da
   tela e devolve o resumo do que mudou. Código de jogo só sob `PC_PORT`.
2. **Teste de equivalência (o critério da camada):** a mesma batalha pulada
   pelo Rush de hoje e pelo flutuante deixa `gamestate_data` idêntico. Grave
   um estado antes, rode os dois caminhos e compare o bloco dos Digimon da
   party, `money`, itens e cartas. Diferença é bloqueio.
3. **Troca do destino:** em `fieldstg_80087DB0.c:5018`–`5019`, depois de
   `def_rush_results()`, quando `DEF_RUSH_FLOAT` está ligado, chame
   `def_rush_apply_quiet()`, devolva o controle ao jogador
   (`event_running` e `battle_starting` de volta a 0, como a tela da rede
   Guardromon faz na saída) e não mude `next_map`.
4. **Painel:** estado estático + gancho por frame, como
   `fieldstg_rush_frame`. Desenhe com janelas da fonte do jogo, como
   `nt_window` em `port/game/definitive_travel.c` (fonte de 11 pixels para
   nomes e números; a de 14 pixels se precisar dos símbolos de botão), e o
   painel escuro por último (ordem de desenho acima). Posição: acima do
   personagem, a partir de `pixel_pos` do ator 5 menos a câmera; se sair da
   tela, preso à borda. **Não** ligue `event_running`: é isso que manteria o
   jogador parado.
5. **Fora de replay:** nada disso roda sob `def_on()` falso; o Rush já não
   roda.

### Teste

- Unidade: `def_rush_apply_quiet` contra um driver com party simulada (XP
  dividido, level up, técnica aprendida, Bits, item), e o teste de
  equivalência do passo 2.
- Jogo: `DW3_RUSH_TEST=2` (Rush automático), andar no mapa `0x21D` com a party
  acima do nível; screenshot do painel andando, e um level up forçado
  (`mem_write` do XP perto do próximo nível).

### Feito (2026-11-13, aplicado direto por decisão do dono)

- Troca do destino em `fieldstg_rush_leave` (`fieldstg_rush_float`): com `def_rush_float()` (`DEF_RUSH_FLOAT`;
  `DW3_RUSH_FLOAT=0/1` ou `def_rush_float_mode` para o teste) o gerente guarda estado, passos, `next_map` e
  `map_entry`, `fieldstg_start_battle` monta a luta como antes (inclusive o prêmio do Cardmon), tudo volta,
  `event_running` e `battle_starting` vão a 0, e vêm `def_rush_results`, `def_rush_apply_quiet` e
  `def_rush_panel_open`.
- `def_rush_apply_quiet` (em STFGTREP, sob `PC_PORT`): as mesmas chamadas, na mesma ordem da tela (XP dividido, item
  0x141, parte da party, `DEF_XP_ALL_FORMS`, técnicas, `add_exp`/`raise_stats`/TP por faixa/nota de TP perdido, formas
  novas, Bits com 0x142 e OVERFINISH, item com Boosters, linhas de material, `def_hunt_reward_done`).
- Painel (`definitive_rush.c` puro, `definitive_rush_panel.c`): fonte de 11 px na camada 0x1004, acima do jogador e
  preso à tela: `+N EXP` e `LV UP!` por membro, `NEW TECH!`, `TP FULL!`, Bits, item (`???` até o arquivo de nomes
  chegar), materiais; 120 ticks; fecha quando o campo segura o jogador; um segundo Rush fecha o painel ao começar e
  abre o seu.
- Decisões: o painel basta para técnica e forma nova (sem tela cheia); a dica de "mais de 3 formas" da tela não
  aparece; o ganho de status no level up sai do mesmo sorteio da tela, mas num momento diferente, então os números
  podem diferir dos que a tela daria naquele instante (a regra é a mesma).
- Testes: `tests/port/definitive_rush.py` (31 casos) e `rush_float_ingame.py` no lab (equivalência com e sem level up,
  painel andando, segundo Rush).

---

## Ordem e dependências

```text
A (cadeia + alvo raro) ─► B (recompensas + materiais) ─► C (bestiário)
                                    │
D (preços) ─────────────────────────┼─► E (forja) ─► F (equipamentos) ─► G (black market)
                                    └─────────────────────────────────► H (DRI)
```

- A, B, C e D são independentes de decisões grandes: comece por elas.
- E, F e G dependem das tabelas aprovadas pelo dono.
- H começa pela prova de conceito e pode correr em paralelo com E–G.
- I só depende do Overlevel Rush, que já existe: pode ser feita a qualquer momento, também começando pela prova de conceito.

## Riscos

| Risco | Onde aparece | Mitigação |
| --- | --- | --- |
| Espaço do save | B, G, H | Orçamento acima; segundo registro por slot se faltar, nunca crescer o `REC_EXTRA` |
| Replay muda | A | `pad_random` só depois de `def_on()`; `scripts/test.sh` e goldens a cada camada |
| Alvo Raro preso em luta de chefe | A | Fuga forçada antes de `blocked[11]`; a luta usa `stage` e `music` do encontro |
| Sem IDs de item livres | F, G | Levantamento da camada D antes de criar qualquer item; materiais nunca são itens |
| Modelo jogável não anima no slot inimigo | H | Prova de conceito primeiro; plano B são chefes equivalentes da ISO |
| Recompensa sem tela diverge da tela | I | Teste de equivalência do estado do jogo; diferença é bloqueio |
| Economia nova inflaciona Bits | D, E, G | Revenda com teto e exceções absolutas; réplicas com revenda 0 |

## Decisões em aberto `[dono]`, com proposta padrão

Cada item traz a proposta que o implementador leva ao dono. Nada é aplicado
sem o ok.

1. **Alvos Raros por região e escala de nível (camada A).** **Aprovada pelo
   dono em 2026-11-13.** Foi aplicada como provisória: a regra por nível da camada A (66 regiões), em
   `.dumps/tasks/hunt/alvos.md` para o dono riscar e trocar. Em aberto: repetir
   o mesmo chefe em várias regiões (há poucas batalhas de chefe: o Datamon
   cobre 13), e se a escala deve seguir o Cardmon da região em vez de T + 3.
2. **Materiais de Caça (camada B).** **Revisto pelo dono em 2026-11-13:** os
   16 nomes aprovados são materiais comuns, de qualquer Digimon; os raros são um
   por região com Alvo Raro (emenda da camada B, em implementação); a
   recompensa do Alvo inclui um R-Booster só. Proposta original: no máximo 16, por tier de
   região (começo, meio, fim, pós-game) e tipo; a quantidade exata e os nomes
   são do dono. Com 16 ou menos regiões com Alvo Raro, pode ser um por alvo.
3. **Tabela de preços (camada D).** **Dono, 2026-11-13: aplicar a proposta
   direto e revisar depois.** Proposta: um teto de revenda em % do preço
   de compra para os itens comuns e valores absolutos de venda para as
   exceções de leilão, como no exemplo dos 2.000 → 7.000–10.000.
4. **Receitas de forja (camada E).** Proposta: o que as primeiras lojas vendem
   fica só com Bits; a partir do segundo tier de loja, 1 material; o topo, 2
   materiais de regiões diferentes.
5. **Itens novos (camada F).** Proposta: só depois do levantamento de IDs;
   começar com 3 variações (uma por arquétipo) para validar o caminho antes
   de uma linha inteira.
6. **Black Market (camada G).** Proposta: o NPC é um figurante de cidade (não
   de dungeon) já visitado no meio da história; Réplicas Lendárias são os
   itens de leilão e de eventos perdíveis que o levantamento encontrar, até
   16; aceitar a simplificação de revenda 0 por ID enquanto houver a réplica.
7. **Gauntlet (camada H).** Proposta: depois da prova de conceito, as três
   linhas citadas (Agumon, Gabumon, Guilmon) como primeiros desafios; falas e
   bônus por tier ficam com o dono.
8. **Resultados flutuantes do Rush (camada I).** Proposta: o painel cobre XP,
   level up, Bits e item; técnica aprendida ou forma nova também ficam no
   painel (`NEW TECH!`), porque o jogador vê o detalhe no menu de status. Se o
   dono preferir, esses dois casos caem na tela de resultados completa.

## Definição de pronto (vale para toda camada)

- Os testes da camada passam, com `tests/port/definitive*.py`,
  `tests/port/text_override.py`, `tests/port/run.py` e `scripts/test.sh`; o
  build PS1 e os goldens não mudaram.
- Um save de antes da camada abre e joga; sem nada ganho, o registro do slot
  continua byte a byte igual.
- Dentro de um replay script nada da camada roda (`def_on()`).
- Toda constante nova está em `definitive.h` com uma linha dizendo o que
  controla; todo texto novo entrou por `tools/text_override/en.txt`.
- Relatório ao dono: o que mudou, a saída dos testes, os screenshots do teste
  no jogo e o que ficou em aberto.
