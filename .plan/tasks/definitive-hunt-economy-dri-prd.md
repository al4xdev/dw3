# PRD 4 — Versão definitiva: Caçada, Forja, Black Market e DRI Gauntlet

Continuação de `definitive-combat-prd.md`, `definitive-progression-prd.md` e
`definitive-rebalance-prd.md`. Mesmas regras de trabalho: camadas pequenas,
teste entre camadas, toda constante em `port/game/definitive.h` com uma linha
dizendo o que controla, código do jogo só chama ganchos `def_*`/`dw3_*` sob
`#ifdef PC_PORT`, save antigo abre e joga, nada roda dentro de um replay script
(`def_on()` / `def_bestiary_active()`), relatório curto ao dono no fim de cada
camada.

Este PRD é para um modelo menor executar. Por isso cada camada diz **onde
mexer**, **o que já existe** e **como testar**. Onde há uma decisão do dono em
aberto, ela está marcada `[dono]`: pare e pergunte, não invente.

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
| Estado extra do save | `port/game/definitive_bestiary.c` (`REC_EXTRA`, `def_save_flag`, `def_network_mask`) | O registro do bestiário (0x100 bytes por slot) tem bytes livres depois da tabela: é **o** lugar para estado novo por save. A cauda do slot (0x26C4..0x2700) está cheia |
| Bestiário | `src/main/fieldmenu.c` (`fieldmenu_bestiary_*`), `port/game/definitive_bestiary.c` | Tela que vai ganhar a linha de materiais |
| Rede Guardromon | `port/game/game_mods.c` (`dw3_net_nodes`, `dw3_network_leaving`) | Tabela das 25 pousadas com mapa e posição; teleporte zera a cadeia de caçada |
| Modo Difícil | `def_save_flag(DEF_SAVE_HARD)` | Party de 2: o balanceamento dos equipamentos mira 2–3 Digimon |
| Tabela de inimigos por espécie | `port/game/definitive_enemies.c`, gerada por `tools/gen_enemy_weaknesses.py` | Modelo de "tabela gerada por ferramenta e aprovada pelo dono" |
| Textos novos | `tools/text_override/en.txt` + `tools/gen_text_override.py` (gera `port/game/definitive_text.c`) | Todo texto na fonte do jogo entra por aqui |

Fatos do motor que você vai precisar:

- **Batalha:** `fieldstg_start_battle(battle)` (`src/fieldstg/fieldstg_80087DB0.c`)
  copia `fieldstg_battles[battle]` para `records_state` (inimigos com `digimon`,
  `level`, `hp`, `mp`, `stat_scale`) e manda o campo para o mapa `0x600`.
  Encontro aleatório: `fieldstg_encounter_start`. Batalha de script:
  `fieldstg_start_listed_battle`.
- **Recompensa:** `stfgtrep_rewards[records_battle_results.battle]` tem
  `tech_exp`, `exp`, `money` (`src/stfgtrep/stfgtrep_80082E70.c`). O item vem de
  `records_battle_results.item`.
- **Registro de espécie:** `FightstgEnemyRecord` (arquivo `0x1CF`, 193 espécies)
  tem `id`, `item`, `drop_rate`, `resists`, `name`. No campo ele é lido com
  `cdload_module.files.get_file(0x1CF)`.
- **Fuga:** `fightstg_battle.state.escapes` e `state.type` (0 normal, 1 o inimigo
  pode fugir, 5 script, 6 chefe) em `include/fightstg.h`.
- **Área do mapa:** `ststatus_map_areas[map & 0xFF] & 0x7F` dá a área do
  mapa-múndi (0..46); o bit 7 é o servidor (Amaterasu).
- **Objetos:** um `object_new` solto no campo **não roda** (ninguém o atualiza).
  Estado estático + um gancho por frame (como `fieldstg_rush_frame`, chamado de
  `fieldstg_camera_update`) funciona.
- **Ordem de desenho:** numa mesma entrada da ordering table, o que é adicionado
  depois é desenhado **por baixo**. Texto primeiro, painel escuro por último.

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
`tests/port/text_override.py` e `tests/port/run.py`.

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
- **Feedback:** sinal discreto na transição (som diferente, ou flash; um callout
  novo `RARE!` em `definitive_callout.c` é barato: acrescente a palavra em
  `styles` e a constante no enum de `definitive.h`).
- **Fuga garantida:** contra Alvo Raro, fugir sempre funciona.

### Implementação

1. Estado: `region_chain` (u8) e `region_area` (u8) no registro extra do save
   (`REC_EXTRA` em `definitive_bestiary.c`; hoje usa os bytes +0..+7, continue em
   +8). "Região" = área do mapa-múndi + servidor (`ststatus_map_areas`).
2. Incremento: no fim de batalha vencida. O ponto é `def_log_end(result)` em
   `port/game/definitive.c` (chamado de `wfightmn_800A6440.c` com
   `fightstg_events.result != 0` = vitória).
3. Reset: em `fieldstg` quando o mapa carregado tem área diferente (o mesmo
   lugar onde `def_hard_party_fix()` é chamado, logo depois de
   `gamestate_flags.set_flag(get_map() + 0x1E00, 1)`), e em
   `dw3_fast_travel_take`.
4. Troca do encontro: em `fieldstg_encounter_start`, antes de
   `fieldstg_start_battle(event->battle)`. O Alvo Raro é **uma batalha que já
   existe** em `fieldstg_battles[]` (um chefe/sub-chefe), escolhida por região
   numa tabela nova `def_hunt_targets[]` (área, servidor, índice da batalha,
   escala de nível). Não crie inimigos novos.
5. Escala: ajuste `records_state.enemies[i].level` / `stat_scale` depois de
   `fieldstg_start_battle` copiar os inimigos, para a faixa de nível da região.
   A referência de faixa é o tier dos Cardmons da região `[dono: confirmar a
   tabela antes de aplicar]`.
6. Fuga: achar a rolagem de fuga em `src/fightstg/` (procure `escapes`) e forçar
   sucesso quando `def_hunt_active()`.

### Tabela de alvos

Gere com uma ferramenta (`tools/gen_hunt_targets.py`), como
`gen_enemy_weaknesses.py`: liste os chefes/sub-chefes de `fieldstg_battles[]`
com espécie e nível, proponha um por região e **mostre ao dono antes de
aplicar**.

### Teste

- Unidade: contador (incrementa, zera ao trocar de área, não conta Rush),
  curva de chance, com a sequência aleatória simulada.
- Jogo: estado no mapa `0x21D`, escreva `region_chain` = 9 e
  `encounter_timer` = 1, ande, confira o Alvo Raro, o aviso e a fuga.

## Camada B — Recompensas do Alvo Raro e Materiais de Caça

### Regras

- **10x XP** em relação ao encontro padrão da área (`DEF_HUNT_XP_MULT`).
- **Carta rara** da tabela de drops dos Cardmons daquela faixa de nível.
- **Peças Raras de Monstro (Materiais de Caça):** itens exclusivos do alvo,
  usados na forja das lojas, no Black Market e nos DRI.

### Implementação

1. **Materiais não são itens novos na tabela do disco.** Não há IDs de item
   livres garantidos e a tela de itens lê a tabela do disco. Guarde os materiais
   como contadores próprios no registro extra do save: um byte por material
   (`DEF_HUNT_MATERIALS`, comece com um por alvo, no máximo 32). `[dono:
   confirmar a lista e os nomes]`
2. XP: em `src/stfgtrep/stfgtrep_80082E70.c`, onde `stfgtrep_rewards[...]` é
   lido, multiplique quando `def_hunt_take_reward()` disser que a batalha foi um
   Alvo Raro. Faça como `def_overfinish_take_bits`: uma função que devolve o
   valor e limpa o estado.
3. Carta: `gamestate_add_card(card, n)` (`src/main/gamestate.c`). A tabela de
   drops dos Cardmons precisa ser levantada (procure `prize_item` em
   `fieldstg_start_battle`: os Cardmons são as espécies `0x1C9..0x1D0`).
4. Material: +1 no contador do alvo, com uma linha na tela de recompensas
   (`data->messages->set_text`, texto novo em `en.txt`).

### Teste

- Unidade: gravação/leitura dos contadores no registro (estenda
  `tests/port/definitive_bestiary_driver.c`; um registro sem os campos novos
  continua lendo como zero e um save antigo continua byte a byte igual quando
  nada foi ganho).
- Jogo: vencer um Alvo Raro forçado e ver XP, carta e material.

## Camada C — Bestiário mostra os materiais

- Cada entrada do Bestiário mostra os materiais que o Digimon derruba, para o
  jogador saber onde caçar cada peça.
- A tela hoje tem 5 linhas de família de elemento e sobra espaço embaixo
  (`BS_ROW_Y + 5 * 14` até `BS_HINT_Y`). Use esse espaço: "Drops:" + nome do
  material + quantidade em mãos. Só aparece para espécie com algo conhecido
  (`def_bestiary_known(index) != 0`); desconhecida mostra `???`.
- Onde: `fieldmenu_bestiary_draw` em `src/main/fieldmenu.c`. As janelas
  `bs_info[6]`, `bs_info[7]` e `bs_states[5]`, `bs_states[6]` estão livres
  (recebem texto vazio hoje).
- Teste: screenshot da tela para um alvo com material e para uma espécie sem.

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
   (`src/stitshop/`; os registros de item são `records_item_*` e
   `records_funcs.get_item`). Entregue a lista ao dono com a proposta.
   `[dono: aprovar a tabela]`
2. Aplique como patch em memória no início (o modelo é `def_mp_hook` /
   `patch_techniques` em `definitive.c`): nunca edite o arquivo do disco.
3. Constantes: `DEF_SELL_CAP_PCT` (teto de revenda em % do preço de compra) e a
   tabela de exceções gerada.

### Teste

- Unidade: a tabela gerada bate com o disco (como `definitive_mp.py` faz para o
  MP).
- Jogo: loja de Asuka (`shop_open` é um checkpoint do script de replay).

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
2. Em `src/stitshop/stitshop_8008321C.c`: na compra, além de `money`, cheque
   `def_recipe_can_buy(item)`; ao comprar, `def_recipe_pay(item)`. Recusa usa o
   mesmo som e a mesma mensagem de "sem dinheiro" com um texto novo ("Missing
   materials").
3. Ao selecionar um item com receita, mostre a linha de requisitos na caixa de
   descrição (texto montado como `def_train_pity_label` monta o seu).

### Teste

- Unidade: `def_recipe_can_buy` / `def_recipe_pay` com contadores simulados.
- Jogo: comprar com e sem material.

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
   com a ferramenta da camada D quais IDs existem, quais nunca são vendidos nem
   dropados (candidatos a reaproveitar) e como o nome/descrição é lido
   (`ITMNAM` e afins, via `def_text_override`). **Pare e relate ao dono antes de
   criar qualquer item.** `[dono]`
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
   bit por item possuído. Se não houver onde guardar por unidade, a saída
   simples é: enquanto o jogador tiver **qualquer** réplica daquele ID, a venda
   daquele ID paga 0. Guarde um bitmask de IDs replicados no registro extra.
   `[dono: aceitar essa simplificação?]`
4. Lista de Réplicas Lendárias: levantar os itens de leilão/evento perdíveis.
   `[dono: lista]`

### Teste

- Unidade: troca de materiais (não cria nem destrói valor fora da tabela),
  exigência de chefes distintos, revenda 0.
- Jogo: as duas abas, e tentar vender uma réplica.

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
   (`fightstg_events_add_knockout(0x10)`), trocar a espécie do membro inimigo,
   restaurar o HP e tocar a animação de digivolução. Peças que já existem:
   - o inimigo caído que continua em cena é exatamente o que o OVERFINISH
     explora (o evento de nocaute fica atrás do turno da party);
   - a digivolução do jogador está em `fightstg_digivolve_update`
     (`src/fightstg/fightstg_80086A00.c`) e a animação curta/pulável em
     `definitive_progression.c` (`def_evo_form_seen`);
   - os modelos entram por `slots->add(slots, slot, model, ...)` e
     `fightstg_models.get`.
   Pergunta a responder: um modelo de Digimon jogável carrega e anima no slot
   `0x10`? Se não, a camada inteira muda para "chefes equivalentes da ISO".
2. Estado: bitmask de desafios destravados e melhor estágio por desafio, no
   registro extra do save.
3. DRI Agents: localize os atores como os Guardromon foram localizados (procure
   o nome de quem fala nos arquivos de diálogo com `tools/dump_text.py` e cruze
   com `FieldstgPlacedActor` nos `wstag*.c`).
4. Menu: lista em duas colunas no estilo do Bestiário ou painel em polígonos;
   decida depois da prova de conceito.
5. Sem game over: a derrota em batalha manda para o mapa `0xE00`
   (`wfightmn_800A6440.c`, perto de `set_next_map(0xE00, 0)`); numa simulação,
   volte para `gamestate_data.field_map`.
6. Acessórios de Sinergia: saem da camada F (mesma tabela de variações).
   `[dono: lista de linhas, falas e bônus por tier]`

### Teste

- Unidade: máquina de estágios (zera HP → próxima forma, último estágio →
  vitória), desbloqueio único, tier por estágio.
- Jogo: uma linha completa, uma derrota no meio (voltar ao mapa sem perder
  nada), party mantendo HP/MP entre estágios.

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

## Decisões em aberto `[dono]`

1. Tabela de Alvos Raros por região e a escala de nível (camada A).
2. Lista e nomes dos Materiais de Caça (camada B).
3. Tabela de preços nova (camada D).
4. Receitas de forja e quais itens continuam só com Bits (camada E).
5. Itens novos: reaproveitar quais IDs, com quais ícones (camada F).
6. NPC do Black Market, lista de Réplicas Lendárias e a simplificação da
   revenda 0 (camada G).
7. Linhas do Gauntlet, falas dos Tamers e bônus por tier (camada H).
