# PRD 2 — Versão definitiva: progressão, buffs e conforto

Continuação de `definitive-combat-prd.md` (mesmas regras de trabalho: camadas,
teste entre camadas, tudo em constantes em `port/game/definitive.h`, código novo
sob `PC_PORT`, save antigo abre e joga, Evo Buff intocado). Cada camada termina
com um relatório curto ao dono e espera aprovação.

Fatos abaixo foram lidos no código em 2026-11-12. Confirme a linha antes de
editar: os números de linha andam.

## Constantes novas

```c
#define DEF_TP_GAIN_LV_2_20     7
#define DEF_TP_GAIN_LV_21_40    9
#define DEF_TP_GAIN_LV_41_50   30
#define DEF_TP_GAIN_LV_51_UP    0    /* depois do 50 só Train Chip dá TP */
#define DEF_TP_CAP             99    /* igual ao original, de propósito: não deixa acumular para o último ginásio */
#define DEF_BOSS_DEBUFF_PCT   100    /* força do debuff de stat em luta que o original bloqueava */
#define DEF_TRAIN_PITY_AFTER    3    /* treinos sem perfeito antes do treino garantido */
#define DEF_TRAIN_PITY_BONUS    1    /* o treino garantido também ganha a rodada bônus */
#define DEF_EVO_ANIM_SKIP       1
#define DEF_TRAIN_NO_STAT_LOSS  1
#define DEF_EVO_HINT_DEPTH      1    /* formas mostradas: 1 passo à frente; 0 desliga, 99 mostra tudo */
```

## Camada A — Chefe aceita debuff de stat

O dono corrigiu a regra do PRD 1: chefe é imune a **status** (veneno, sono,
etc.), não a debuff.

- `records_state.blocked[12]` por luta. Índices 0–4 e 7 são status: **continuam
  valendo**. Índices 8, 9, 10 são redução de Força, Defesa e Velocidade: na
  versão definitiva são **ignorados** (o efeito entra, multiplicado por
  `DEF_BOSS_DEBUFF_PCT / 100` quando o original bloquearia). Não mexa em 5, 6 e
  11 sem entender o que guardam.
- Pontos que leem 8..10: `fightstg_8008D3B4.c` ~495, ~508, ~998, ~1061, ~1739
  (`blocked[e->stat + 8]`), ~7446, e `port/game/definitive.c` ~553 (debuffs
  elementais 207/208, hoje `return 2` com `blocked[10]`).
- Corrigir o texto da regra 7 e da camada 6 no PRD 1.
- Conferir de passagem: 4 inimigos usam as técnicas convertidas 202/203/207/208
  (ex.: Suijinmon tem 203). Diga no relatório o que elas fazem hoje na mão do
  inimigo; se for o efeito novo de jogador, proponha a correção.

**Aceite:** Armor Break e os debuffs elementais funcionam num chefe que
bloqueava (ex.: batalha 24); veneno e sono continuam bloqueados no mesmo chefe.

## Camada B — Buff de ataque vale para magia, defesa vale para as duas

O motor tem 3 modificadores por membro: `modifiers[0]` Força, `[1]` Defesa,
`[2]` Velocidade. Espírito não tem. Cada técnica tem um só efeito
(`fightstg_tech_stat_effects`, `fightstg_tech_party_stat_effects`).

- Em `get_special_damage`, não em `get_stats` (no inimigo o Espírito é ataque e
  defesa mágica ao mesmo tempo):
  - atacante: Espírito usado como ataque é escalado pela mesma proporção do
    modificador de Força (`(força_base + modifiers[0]) / força_base`);
  - alvo: o stat usado como defesa mágica (Defesa no jogador, Espírito no
    inimigo) é escalado pela proporção do modificador de Defesa. No jogador a
    Defesa já vem modificada: não aplique duas vezes.
- O chefe final (`state.type == 6`) já tem ajustes próprios em Defesa e
  Espírito: não some em cima sem checar.
- Atualizar as descrições (198, 199, 200, 201, 205, 206; ≤34 caracteres).

**Aceite:** no log, Double Power aumenta dano mágico na mesma proporção do
físico; Armor Break aumenta o dano mágico recebido pelo inimigo; magia do
inimigo não muda com Armor Break.

## Camada C — Ícones de buff e debuff no HUD

Usar a base visual que já existe no HUD da versão definitiva (não criar estilo
novo).

- Por membro, dos dois lados: seta para cima/baixo para Força, Defesa,
  Velocidade, conforme o sinal do modificador; seta para baixo com a cor do
  elemento para o debuff elemental (207/208).
- Some quando o modificador volta a zero. Vários ao mesmo tempo ficam lado a
  lado.

**Aceite:** captura de tela com buff e debuff simultâneos no jogador e no
inimigo.

## Camada D — TP

- Hoje: `stfgtrep_add_exp` (`src/stfgtrep/stfgtrep_80082E70.c` ~1312) dá `+5`
  por nível, teto 99.
- Novo: ganho pela faixa do nível alcançado, com as constantes acima. Teto
  continua `DEF_TP_CAP`. Totais esperados: 313 no nível 40, 613 no 50, nada
  depois (original: 195 / 245 / 490 no 99).
- Na tela de nível, se o ganho estourar o teto, mostrar aviso (TP perdido), para
  o jogador ir treinar.
- Save antigo: `tools/convert_save_mp.py` passa a dar o TP retroativo = (total
  novo até o nível atual, limitado ao 50) − (5 × níveis ganhos até o 50), nunca
  negativo, respeitando o teto. Como o teto é 99, o que não couber se perde:
  avise no `--dry-run`. Marcador: trocar `MPV1` por uma versão (`MPV1` = só MP,
  `DFV2` = MP e TP), no mesmo lugar (slot + 0x26EC), para não gastar mais bytes.

**Aceite:** teste de unidade da tabela (totais acima); conversor idempotente em
cópia do cartão; diff binário só nos campos esperados.

## Camada E — Treino garantido

Fatos: rodada acerta com chance 75 (+3 ou +6 com item). Treinos abaixo de 13 têm
3 rodadas e bônus automática se as 3 acertam; de 13 em diante são 5 rodadas, com
bônus opcional de chance 50 (ou 0 se ganhou o bônus anterior).

- Contador em RAM (não vai para o cartão; zera ao fechar o jogo). Um contador
  só, para todos os Digimon.
- "Perfeito" = todas as rodadas normais do treino acertaram. Perfeito zera o
  contador; qualquer outro resultado soma 1.
- Com o contador em `DEF_TRAIN_PITY_AFTER`, o próximo treino acerta todas as
  rodadas (e a bônus, se `DEF_TRAIN_PITY_BONUS`), e zera.
- Indicador na tela de treino: marcas preenchidas (ex.: 2/3) e um aviso claro
  quando o próximo é garantido.
- Só vale para treino em que o TP foi gasto.

**Aceite:** com RNG forçado a falhar, o 4º treino acerta tudo; indicador visível
em captura.

## Camada F — Blast Gauge no save

Bug do original: `records_state.gauges[8]` (s16, 0..1000, por Digimon) vive só
na RAM e é zerado no título (`records_clear_gauges`, chamado em
`stdwtitl_80085CD4.c` ~340/347).

- Gravar e ler junto com o slot. Lugar proposto: cauda do slot, 16 bytes em
  slot + 0x26F0 (depois do marcador). **Confirme antes** que 0x26F0–0x2700 não é
  usado; se for, use o bloco do bestiário com um registro novo por slot.
- Ao ler: valor fora de 0..1000 vira 0. Save antigo (zeros) carrega com gauge 0.
  Novo jogo continua zerando.
- Savestate do port (`game_savestate`) também.

**Aceite:** encher parte da barra, salvar, fechar, abrir: barra igual. Save
antigo abre com 0.

## Camada G — Animação de digivolução curta

- Primeira digivolução de cada forma na sessão: animação inteira. Depois: versão
  curta (corte direto para a forma pronta). Em RAM, um bit por forma; sem save.
- Além disso, segurar Círculo ou X durante a animação pula, mesmo na primeira
  vez.
- Vale para digivolver e blast (`wfightmn_digivolve` ~585, `wfightmn_blast`). O
  Evo Buff se aplica igual. *(Obsoleto: o Evo Buff foi removido no PRD 3, camada B.)*

**Aceite:** tempo da segunda digivolução medido e menor; Evo Buff ainda entra na
versão curta.

## Camada H — Segunda passada nas técnicas (para na proposta)

O jogador tem só 13 técnicas de buff/debuff em 55 formas e nenhuma redução de
ataque (o efeito existe: técnica 204, só de inimigo).

1. Esperar o resultado da análise de UI/UX das skills (em andamento) para não
   mexer nas mesmas descrições.
2. Verificar se um golpe de dano aceita um efeito de stat secundário (hoje a
   tabela só é lida para a classe 5).
3. Entregar em `.dumps/tasks/persona/tecnicas-v2.md` uma tabela: forma, técnica,
   efeito atual, efeito proposto, custo de MP. Meta: uma identidade por forma,
   pelo menos uma redução de ataque para o jogador, nada que dê turno extra (o
   sistema de corrente já dá).
4. **Parar e esperar o dono aprovar a tabela.** Só então implementar.

## Camada I — Treino de resistência sem perda de stat

Fato: o treino de resistência (tipos 8..14) tem um segundo stat
(`entry->stat_2`); quando é 1..5, cada rodada tem 50% de chance de tirar pontos
dele (`stgtrain_session_lower_stat`, `stgtrain_stat_losses` = {1, 2+r3, 4+r3};
`src/stgtrain/stgtrain_800861BC.c` ~78 e ~187). Quando `stat_2` é 15/16 ele SOBE
HP/MP máximo: isso fica.

- `DEF_TRAIN_NO_STAT_LOSS 1`: a perda vira 0 e a linha de perda não aparece no
  resultado.
- Atenção ao RNG: o original consome um sorteio para a perda. Tanto faz manter
  ou não, mas diga no relatório qual escolheu (afeta replays de teste).
- Corrigir os textos do treino que avisam da perda (`ESTRAINI`).

**Aceite:** 20 treinos de resistência com RNG variado, nenhum stat cai; ganho de
HP/MP máximo continua.

## Camada J — Requisitos de digivolução visíveis

Fato: cada parceiro tem uma lista de 44 formas (`stfgtrep_learn_lists[digimon]`,
`StfgtrepLearn` em `src/stfgtrep/stfgtrep_80082E70.c` ~127–138): até duas formas
exigidas com nível mínimo (`requires[i] =
{forma, nível}`) e um stat mínimo
(`stat` 1–6 os seis stats, 7 o nível do Digimon, 8–14 resistências; `min`). A
checagem está em `stfgtrep_learn_technique` (~1360). Ver também
`docs/MECHANICS.md` seção 7.

- Nova tela ou painel no Digi-Lab (`stgdglab_8008C278.c`), por parceiro: as
  próximas formas ainda não obtidas cujos pré-requisitos de forma o jogador já
  tem ou está perto de ter, com cada requisito e o valor atual, por exemplo
  "Greymon Nv 5 (3/5)", "Força 180 (142/180)". Requisito cumprido aparece
  marcado.
- Mostrar só um passo à frente (formas cujas formas exigidas o jogador já
  possui), para não revelar a árvore toda. Constante `DEF_EVO_HINT_DEPTH 1`; 0
  desliga, 99 mostra tudo.
- Só leitura: não muda regra de digivolução nem save. Usar a base visual do HUD
  definitivo.
- Entregar primeiro um esboço da tela (captura ou texto) e esperar o dono
  aprovar o layout.

**Aceite:** os números da tela batem com `stfgtrep_learn_lists` para Kotemon e
Kumamon; ao cumprir o último requisito, a forma é aprendida na próxima subida de
nível, como no original.

## Fora deste PRD

PRD 3 (o dono já tem o desenho): EXP para a reserva (já existe como mod) e modo
difícil com 2 Digimon; Rebalancemanto dos campos elementais; desenho de animação
de WEAK é resist;
