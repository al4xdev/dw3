# PRD — Versão Definitiva: combate estilo Persona

Dono: Alex. Data: 2026-11-12. Estado: aprovado em desenho, implementação por camadas.
Relatório do código que embasa este PRD: `.dumps/tasks/persona/relatorio-e-plano.md` (a Parte B de lá está
substituída por este arquivo). Dados extraídos: `.dumps/tasks/persona/data.json` (técnicas, Digimon, inimigos) e
`techs.txt` (toda técnica com quem a usa); regenere com `tools/venv/bin/python .dumps/tasks/persona/dump.py`.

## Como trabalhar neste PRD

- **Uma camada por sessão.** Leia a seção "Regras fixas", a camada da vez e os trechos de código citados. Não leia o
  arquivo de 9000 linhas inteiro: use os números de linha e `grep`.
- **Pare no fim de cada camada** e mostre ao dono: o que mudou, a saída do teste, o que ficou em aberto. Só siga com o ok.
- Números de linha são de 2026-11-12 e andam conforme o código muda: confirme com `grep` pelo nome da função.
- Onde este PRD diz "decisão do implementador", escolha, anote no relatório da camada e siga.
- Onde diz **[CONFIRMAR COM O DONO]**, implemente o padrão indicado e levante a pergunta no relatório da camada.

## Regras fixas

1. **Não é mod.** Sempre ligado no port. Sem opção em `mod.json`, sem flag `port_mod_*`.
2. Todo código novo no jogo fica sob `#ifdef PC_PORT`. O build PS1 (matching) e os goldens de `tests/golden` não
   podem mudar. Rode `scripts/test.sh` antes e depois de cada camada; uma falha nova é bloqueio.
3. **Evo Buff não se toca** (`dw3_evo_buff_member`, `dw3_evo_shield_member`, `wfightmn_evo_buff_apply`). *(Obsoleto: o Evo Buff foi removido no PRD 3, camada B.)*
4. O HUD `battle_menu` passa a ser obrigatório (camada 2): os caminhos `port_mod_battle_menu == 0` deixam de existir.
5. **Dado do disco nunca é editado nem versionado.** Inimigos (`SDIGIEDT.PRO`) e textos (`ES*.BIN`) mudam por tabela
   de override no port. `src/main/records.c` (técnicas, Digimon) está no repo, mas é fonte matching: mudanças de dado
   entram por patch aplicado no boot do port, não editando a tabela.
6. **Todo número é constante** em `port/game/definitive.h`, com comentário de uma linha dizendo o que controla.
7. Compatibilidade de save: save antigo abre e joga. MP do jogador continua limitado. Chefe é imune a **status**
   (veneno, sono...), não a debuff de stat (corrigido pelo dono no PRD 2, camada A), nem a dano elemental.
8. Código novo fica em `port/game/definitive.c` sempre que possível; no código do jogo entram só ganchos curtos.
   Estado de batalha novo entra em `game_savestate` (`port/game/game_mods.c:679`).
9. Remoção de arquivo: mover para `/tmp/`, nunca `rm`. Python com `uv`/`tools/venv`.

## Vocabulário do código (confirmado)

- Elementos, ordem fixa: Fogo, Água, Gelo, Vento, Trovão, Máquina, Trevas = `resists[0..6]`. Na técnica,
  `RecordsTechnique.defense_stat` 2..8 é o elemento (`resists[defense_stat - 2]`); 1 = sem elemento.
- `RecordsTechnique.element` é a classe: 2 física, 3 mágica, 4 cura, 5 buff/debuff, 6 campo.
- Stats: `stats[0]` Força, `[1]` Defesa, `[2]` Espírito, `[3]` Sabedoria, `[4]` Velocidade.
- `side == 0` é o jogador atacando. `fightstg_rules.stats[0]` atacante, `[1]` alvo.
- Chefe: só o final tem `state.type == 6`. Os demais se reconhecem por `records_state.blocked[]` (imunidades por luta).

## Constantes iniciais (`definitive.h`)

| Nome | Valor | Controla |
|---|---|---|
| `DEF_WEAK_MAX` | 80 | uma família é fraqueza enquanto um valor dela estiver até aqui (emenda de 2026-11-13; substitui `DEF_WEAK_COUNT` e `DEF_WEAK_GAP`) |
| `DEF_RESIST_MIN` | 250 | valor a partir do qual o elemento é "resistido" |
| `DEF_MULT_WEAK` | 0,5 | bônus por acertar fraqueza |
| `DEF_MULT_STAGGER` | 0,2 | bônus por alvo em stagger |
| `DEF_MULT_RESISTED` | 0,5 | multiplicador total contra elemento resistido (confirmado pelo dono em 2026-11-13) |
| `DEF_AFF_SINGLE_MAX` | 0,3 | teto de afinidade de Fogo e de Vento (status 999) |
| `DEF_AFF_PAIR_MAX` | 0,6 | teto de Água+Gelo e de Trovão+Máquina (soma 1998) |
| `DEF_AFF_DARK_MAX` | 0,6 | teto de Trevas (status 999) |
| `DEF_PLAYER_REF_RESIST` | 100 | resistência de referência do jogador como alvo de elemento |
| `DEF_BATON_PASS_MP` | 0 | custo do Baton Pass (o dono, 2026-11-13: grátis, como o código já fazia) |
| `DEF_RELAY_MP` | 50 | custo do Mega Boost refeito |

Represente frações em ponto fixo (por exemplo /1000): o jogo não usa float.

---

## Camada 0 — Log de batalha e linha de base

**Objetivo:** medir antes de mudar. Sem alteração de regra.

- Variável de ambiente `DW3_BATTLE_LOG=<arquivo>`: uma linha por evento, texto simples. Eventos: início de luta (id da
  batalha, inimigos, nível, HP), turno (lado, número), ação (lado, técnica, classe, elemento), dano (valor, HP restante
  do alvo, e a partir da camada 5 os termos do multiplicador), fim (resultado, turnos do jogador, turnos do inimigo).
- Ganchos: `fightstg_rules_get_damage` e `_get_special_damage` (retorno), `wfightmn_party_turn`, o tratador do turno
  inimigo e `wfightmn_battle_end` em `src/wfightmn/wfightmn_800A6440.c`.
- Roteiro de medição: o port já roda roteiros por estado do jogo (`--script`, `tests/replay/scripts/`) e tem save
  states. Monte um roteiro que entra numa luta de chefe e joga com política fixa (sempre a técnica X). Rode cada chefe
  escolhido com um time físico e um mágico. Escolha de 4 a 6 chefes espalhados pelo jogo usando
  `fieldstg_battles[]` (`blocked[]` quase todo em 1) — decisão do implementador, liste-os no relatório.
- Se montar o roteiro automático custar mais que a camada inteira, entregue o log e um procedimento manual curto, e
  diga isso claramente.

**Aceite:** tabela "chefe × time → turnos até vencer, dano médio por turno, HP restante" gravada em
`.dumps/tasks/persona/baseline.md`. Goldens intactos.

**Decisão do dono (2026-11-12): camada 0 fechada sem a tabela.** O log (`DW3_BATTLE_LOG`) ficou pronto e basta; não
haverá autoplay nem `DW3_FORCE_BATTLE`. O dono balanceia jogando. Qualquer verificação de regra nas camadas seguintes
é por **teste host com cálculo** (a camada 3 e a 5 já pedem), não por jogar o jogo. A camada 10 deixa de ser tarefa
do implementador: vira ajuste feito pelo dono nas constantes de `definitive.h` (regra 6) depois de jogar.

## Camada 1 — Override de texto e descrições

- Gancho em `font_get_entry` (`src/main/font.c:120`): tabela `(arquivo de texto, índice) → bytes já codificados`.
  Codificação: `tools/dump_text.py` tem as tabelas de glifos; gere a tabela C com um script em `tools/`.
- Reescreva as descrições em inglês das técnicas (`SKLINF`) a partir do comportamento no código, **máximo 34
  caracteres** (o maior texto atual). Formato sugerido: efeito primeiro, sem enfeite ("Fire magic. Hits weakness x1.5").
  Só descreva o que já é verdade nesta camada; textos que dependem de camadas futuras entram com a camada.
- Entregue a lista antes/depois em `.dumps/tasks/persona/descricoes.md` para o dono revisar.

**Aceite:** texto novo aparece no menu de técnicas e no menu de status; nenhuma descrição estoura a janela (verificar
as mais longas na tela); idiomas que não o inglês seguem como estão.

## Camada 2 — HUD obrigatório, inspecionar, som, indicador de turno

- Remover a condição `port_mod_battle_menu` (o HUD vale sempre). O menu de lista original deixa de ser alcançável.
- **Inspecionar:** ação nova no HUD (um dos lugares livres; decisão do implementador) que mostra, para o inimigo
  atual, os 7 elementos como fraco / neutro / resistido. Até a camada 9 mostra tudo; depois, só o descoberto ("???").
- **Som:** três sons distintos para efetivo, neutro e resistido, tocados no golpe (`sound_module.play(id)`); escolha
  entre os efeitos que o jogo já carrega em batalha. Nesta camada "efetivo/resistido" usa a regra provisória
  `resist ≤ 75` / `≥ DEF_RESIST_MIN`; a camada 3 troca pela função definitiva.
- **Indicador "inimigo age 2×":** com E = atraso restante do turno inimigo na fila, Pe = `get_delay(0x10, 0)` e P =
  atraso restante do turno do jogador depois da ação atual (`get_delay(0, 0)`): mostrar quando `E + Pe < P`.
  `get_delay` do tipo 0 não sorteia nada, mas sobrescreve `fightstg_rules.stats[]`: chame só com o menu de comando
  aberto, nunca no meio de um cálculo de dano.

**Aceite:** captura de tela das três coisas; luta contra inimigo mais rápido mostra o indicador e ele de fato age duas
vezes; contra inimigo mais lento não mostra.

## Camada 3 — Fraqueza dinâmica e dados dos inimigos

> **Emenda do dono (2026-11-13): afinidade por família.** Água+Gelo e Trovão+Máquina são uma afinidade só (por isso a
> UI já os junta). Tudo abaixo que fala de "elemento" passa a valer por família (Fogo, Água+Gelo, Vento, Trovão+Máquina,
> Trevas):
> - a tabela dos inimigos é gerada por família: os dois elementos de um par têm sempre o mesmo valor; arquétipos
>   contados por família: plain 2+1, wall 1+2, late 2+2, **boss 2+2**, **final 1+3** (mais a marca de Trevas), porque
>   fora Trevas só há 4 famílias;
> - `def_weakness_mask` marca a família inteira quando um valor dela está em `DEF_WEAK_MAX` (80) ou abaixo; **sem
>   mediana e sem limite** de quantidade: uma Break que cria uma terceira fraqueza não esconde nenhuma natural;
> - a ação extra (camada 4) é uma por família na janela: Gelo depois de Água não dá outra;
> - Resist Down e Mega Break (camada 6) baixam a família inteira; a escolhida é a de maior afinidade do usuário
>   (`def_affinity`, a soma do par);
> - um acerto ensina ao Bestiário a família inteira.
> Os passos 1–3 abaixo (mediana, `DEF_WEAK_GAP`, `DEF_WEAK_COUNT`) ficam como histórico.

**Função única** `def_weakness_mask(const s16 resists[7], int dark_weak)`, chamada na hora do golpe com os resists
já modificados por buffs/debuffs:
1. Considere os 6 não-Trevas. Mediana = média do 3º e 4º valores ordenados.
2. Candidatos: valores `≤ mediana − DEF_WEAK_GAP`. Ordene por valor, desempate pela ordem fixa dos elementos.
3. Fraquezas = os `DEF_WEAK_COUNT` primeiros. Tudo igual → nenhuma.
4. Trevas só entra se `dark_weak`; nesse caso é fraqueza adicional, fora da contagem.
5. Resistido = valor `≥ DEF_RESIST_MIN`.

**Marca de Trevas:** campo novo por espécie numa tabela do port (não existe no registro do disco).

**Override dos inimigos:** tabela por ID de espécie com os 7 resists, aplicada em `fightstg_enemy_find_record` /
`fightstg_enemy_records.get` (`fightstg_8008D3B4.c:6381`). Gere a tabela com um script versionado em `tools/`,
determinístico (semente fixa), com estas regras:
- **Arquétipos (pedido do dono, estilo Persona; supera "2 fracos + 1 forte" para todos):** `plain` 2 fracos + 1 forte;
  `wall` (mob fraco, ~20% dos de nível ≤ 30) 1 fraco + 2 fortes, a fraqueza é tudo; `late` (nível ≥ 45) 2 + 2; `boss`
  (lutas de chefe) 2 + 3; `final` (Galacticmon) 2 + 4. Fraco = 60, forte ≥ 250 (300), resto neutro (~100–115, mantendo
  o valor atual quando já estiver nessa faixa). **Cuidado com a mediana:** com 3 ou mais fortes entre os seis, os neutros
  passam a valer "fraqueza"; só combinações com 2 fracos (o teto os esconde) ou até 2 fortes são seguras. O gerador
  confere cada espécie pela regra do jogo;
- preservar o que já existe quando já obedece; quando há fraquezas ou fortes demais, manter as mais coerentes com o tema;
- tema: o `type` da espécie e o elemento das técnicas dela dão o elemento "da casa", que nunca vira fraqueza e é o
  candidato natural a resistência forte;
- variedade: equilibrar a contagem global por elemento (hoje Fogo é o menor em 87 espécies) e evitar que inimigos da
  mesma batalha (`fieldstg_battles[]`) tenham o mesmo par de fraquezas;
- chefes também com 2, incluindo Galacticmon (hoje 505 em tudo);
- **Resistir a Trevas é tão raro quanto ser fraco a ele** (regra do dono, 2026-11-12): poucos inimigos resistem a Trevas,
  para Trevas continuar um elemento útil. No gerador: só 8 espécies (demônios e mortos-vivos) têm Trevas como resistência
  forte; as outras recebem outro elemento. Fraquezas a Trevas: 8 (4 só-Trevas, 4 extra).
- Trevas: marcar poucas espécies raras e alguns chefes; em 3 a 5 delas, Trevas é a ÚNICA fraqueza (as outras duas
  viram neutras).
- Saída: a tabela C e um `.dumps/tasks/persona/fraquezas.md` legível (espécie, antes, depois) **para o dono aprovar
  antes de entrar no jogo**.

**Aceite:** teste host da função (empates, tudo igual, buff que troca a fraqueza, Trevas com e sem marca); a tabela
aprovada pelo dono; inspecionar e sons passam a usar a função.

## Camada 4 — Turnos, stagger, corrente, troca

**Abertura da luta** (`wfightmn_800A6440.c` ~:386–410): a emboscada (`wfightmn_roll_first_strike`, que dá o primeiro
turno ao INIMIGO) continua igual. Fora dela, some o sorteio por velocidade: o jogador sempre abre. Velocidade segue
valendo só pelo atraso de turno (até 2 ações por turno do outro lado).

**Decisões do dono depois do primeiro teste (2026-11-12):** o primeiro turno do inimigo vem depois do atraso inteiro
dele (`get_delay(0x10, 0)`), não da metade do atraso do jogador: quem é mais rápido ganha as ações a mais desde o
início. O Baton Pass só aparece enquanto o inimigo está em stagger (uma vez por janela, custo `DEF_BATON_PASS_MP` = 0);
a troca normal gasta o turno (a não ser depois do Relay). Isso substitui a "troca grátis" automática abaixo.

**Stagger:** flag própria por inimigo (`def_staggered[3]`), sem relação com HP.
- Abre quando o jogador acerta uma fraqueza com técnica elemental (`defense_stat ≥ 2`). Ataque físico sem elemento e o
  elemento vindo de crest ("coringa", `attack_element`) não abrem.
- Fecha quando o inimigo aberto recebe o próximo golpe do jogador (esse golpe ainda leva o bônus) ou quando ele age.
- Técnica multi-golpe conta como um golpe só.

**Corrente:** cada fraqueza concede uma ação extra, uma vez por janela. A janela zera a cada ação NORMAL do jogador
(não-extra). Sem teto: com 2 ações por velocidade e 2 fraquezas, são até 6 ações. É desenho, não bug.
- O turno seguinte é reagendado no momento do comando (`wfightmn_party_turn`, fim do passo 2), antes de o golpe
  resolver. Então: ao acertar, marque `extra_pending`; quando a ação terminar, ache o turno do jogador na fila
  (`fightstg_events.find(2)`) e zere o atraso dele. Confira a regra de empate de `fightstg_events_take_next` (:6437).
- A corrente é só do jogador. Inimigo não ganha ação extra **[CONFIRMAR COM O DONO]**.

**Pose:** `wfightmn_update_idle_anim` (:1854) passa a ligar a pose de "cansado" pela flag de stagger no lado inimigo,
e nunca por HP (lado do jogador: sempre 0 — o dono considera a pose de HP baixo removida). No chefe final
(`type == 6`): pose = `final_phase || staggered`.

**Troca grátis:** com um inimigo aberto, trocar (comando 5) não gasta a vez, uma vez por janela; o botão Tag do HUD
mostra ícone diferente enquanto a troca grátis está disponível.

**Baton Pass:** troca que custa `DEF_BATON_PASS_MP` em vez da vez, fora do stagger. O dono pediu como "técnica";
como o rookie só tem uma técnica na lista, a forma mais simples é uma ação no menu de troca
(`fightstg_switch_menu_update`, :5072), ao lado da troca normal e da técnica em dupla **[CONFIRMAR COM O DONO]**.

**Aceite:** pelo log da camada 0: fraqueza → ação extra; mesma fraqueza de novo na janela → sem extra; segunda
fraqueza → outra extra; golpe em alvo aberto fecha; inimigo que age fecha; troca grátis uma vez; chefe final mantém a
fase própria. Save state no meio de uma corrente restaura igual.

## Camada 5 — Multiplicador e afinidade

Só soma. Para o jogador atacando com técnica elemental:

`mult = 1,0 + (fraqueza ? 0,5 : 0) + (alvo aberto ? 0,2 : 0) + (fraqueza ? afinidade : 0) + (alvo aberto ? afinidade : 0)`

- Afinidade pelo elemento da técnica, lida dos resists do ATACANTE (os antigos status de resistência do jogador):
  Fogo, Vento: `0,3 × s/999`. Água ou Gelo: `0,6 × (água + gelo)/1998`. Trovão ou Máquina: `0,6 × (trovão +
  máquina)/1998`. Trevas: `0,6 × s/999`.
- Neutro = 1,0. Resistido (`≥ DEF_RESIST_MIN`) = `DEF_MULT_RESISTED`, sem os bônus.
- Golpe sem elemento em alvo aberto: `1,0 + 0,2`, sem afinidade.
- **Substitui** os termos antigos quando o jogador ataca: no físico, o passo `value += value × element_power × 2 /
  resist` de `scale_damage` (:7921); no mágico, o multiplicador por faixas de `get_special_damage` (:8016–8024). O
  bônus de crest (`attack_element`) em ataque básico fica como está, mas lendo a regra nova de fraqueza para o valor.
- Inimigo atacando o jogador com elemento: os resists do jogador não defendem mais. Use `DEF_PLAYER_REF_RESIST` no
  lugar deles nas fórmulas antigas, para o dano inimigo ficar na escala de hoje. As rolagens de STATUS continuam
  usando os resists do jogador como antes.
- O teto `[poder/2, 2×poder]` da magia NÃO muda aqui (ver camada 10).

**Aceite:** teste host com casos fechados (neutro 1,0; fraqueza 1,5; fraqueza+aberto com afinidade cheia de Fogo =
1,0+0,5+0,2+0,3+0,3 = 2,3; par Água+Gelo 999+0 = 0,3 de afinidade); o log mostra os termos de cada golpe.

## Camada 6 — Técnicas

Antes de mudar qualquer técnica, consulte `techs.txt`: `records_techniques[]` é global e toda forma/inimigo que usa
o ID muda junto. Herança fica **como é hoje** (marca no Digi-Lab, +20% de MP, sem limite novo).

Refeitas (decididas pelo dono):
| ID | Era | Vira |
|---|---|---|
| 202 Speed Up | +velocidade | **Libra**: revela as fraquezas do alvo e registra na memória de batalha |
| 203 Mega Boost | +velocidade do time | **Relay**: a próxima troca é grátis; custo `DEF_RELAY_MP` |
| 207 Slow Down | −velocidade | debuff de resistência elemental do alvo (muda a fraqueza na hora) |
| 208 Mega Break | −velocidade forte | o mesmo, mais forte ou em mais elementos |

Os dois debuffs funcionam em chefe (PRD 2, camada A: `DEF_BOSS_DEBUFF_PCT` onde o original bloquearia). Qual elemento cada um baixa: decisão do implementador,
proponha e mostre ao dono.

Candidatas a virar técnica elemental (lista para proposta, o dono aprova antes): 129, 130 (vazias); 194, 195
(nenhuma forma aprende); 146, 148, 204, 211, 212 (só inimigo: NÃO mudar o registro, o inimigo usa); 131–136
(Busters por tipo); 213–219 (Campos); 190, 192; 127, 128. Meta: toda linha evolutiva com acesso a pelo menos 2
elementos, nenhuma com os 6.

**Aceite:** proposta aprovada; descrições novas (≤ 34) na tabela da camada 1; nenhum inimigo teve técnica alterada sem
querer (comparar `techs.txt` antes/depois).

## Camada 7 — Defesa unificada

- Só com o JOGADOR como alvo: em `get_special_damage`, quando `side != 0`, a razão usa a Defesa do alvo
  (`target->stats[1]`) no lugar do Espírito. Inimigo como alvo mantém Espírito.
- Espírito do jogador fica como ataque mágico (e as durações que já usa).
- *(Obsoleto: Evo Buff removido no PRD 3, camada B.)* Atenção ao escudo do Evo Buff: ele põe Defesa em 9999 quando o membro é alvo; com a defesa unificada, passa a
  cobrir magia também. É consequência aceitável, mas avise no relatório.
- Textos do menu de status: "Tolerance" dos 7 elementos vira afinidade; corrigir o que descrever Espírito como
  defesa (`ESSTATUS` 92–98, `ESTRAINI` 29+).

**Aceite:** log mostra magia inimiga escalando com a Defesa do jogador; magia do jogador inalterada; textos na tela.

## Camada 8 — MP

Formas não têm MP próprio: o MP é do rookie (`RecordsDigimon.start_mp`, `hp_mp_gains[1]`).

| Rookie | MP inicial | Ganho por nível |
|---|---|---|
| Kotemon | 100 → 140 | 55 → 57 |
| Kumamon | 10 → 140 | 30 → 57 |
| Monmon | 80 → 140 | 45 → 57 |
| Veemon | 150 | 50 → 57 |
| Guilmon | 130 → 140 | 60 |

Aplicar por patch no boot do port. Save antigo: sem migração dentro do jogo; o dono converte com
`tools/convert_save_mp.py` (mesma tabela — mantenha as duas em sincronia).

**Aceite:** novo jogo começa com os valores novos; subir de nível usa o ganho novo.

## Camada 9 — Memória de batalha e bestiário

- Tabela: um byte por espécie (índice do registro em `SDIGIEDT`, 193), bits 0..6 = elemento descoberto.
- Descobre ao acertar uma fraqueza ou uma resistência, e pela técnica Libra (tudo de uma vez).
- **Start** (bit `0x8`, livre em batalha) abre a memória: elementos não descobertos como "???", descobertos com o
  estado ATUAL (fraco/neutro/resistido, já com debuffs). Inspecionar (camada 2) passa a respeitar a mesma tabela.
- **Save:** o arquivo do cartão tem 4 blocos e o jogo usa até 0x7800; os 0x800 finais nunca são escritos. Gravar ali
  um registro por slot (3 × 0x100): magic, versão, checksum, 193 bytes. Ausente, versão desconhecida ou checksum ruim
  = tudo zero, sem erro. Caminho de escrita/leitura: `memcard_read`/`memcard_write` (`src/main/memcard.c`, o `switch
  (part)`) ganham uma parte nova sob `PC_PORT`; chamadas ao lado de `port_global_save_record`/`_restore`
  (`src/stgmcard/stgmcard_80082CD0.c` :1247, :1330). Não use a cauda do slot: 0x26C4..0x26EB é do `global_save` e
  0x26EC..0x26EF é o marcador do conversor de MP.
- **Bestiário:** tela fora de batalha que lê a mesma tabela (entrada no menu de campo, como o SAVE do `global_save`
  em `src/main/fieldmenu.c`).

**Aceite:** save antigo abre com tudo "???"; descobrir, salvar, fechar, carregar: continua descoberto; os 3 slots não
se misturam; `tests/saves` e os goldens de memcard intactos.

## Camada 10 — Balanço (do dono, não do implementador: ver a decisão na camada 0)

Rode de novo a medição da camada 0 com os mesmos chefes e times.
- Chefe caindo em 2–3 turnos: **subir o HP dos chefes primeiro** (multiplicador constante sobre
  `RecordsEnemy.hp` das lutas de chefe). Só depois mexer em magia.
- Se, com HP ajustado, o time mágico ainda perder feio para o físico: aí sim rever o teto `[poder/2, 2×poder]` de
  `get_special_damage`.

**Aceite:** tabela antes/depois em `.dumps/tasks/persona/balanco.md`; time físico e time mágico vencem os mesmos
chefes em número de turnos comparável.

## Fase posterior (fora deste PRD)

Modo difícil com 2 Digimon em vez de 3. Não implementar agora; só não feche portas (não assuma 3 membros em código
novo quando `gamestate_get_party_member` já resolve).
