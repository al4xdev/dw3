# Rebalance "Persona-like" do DW3 — via técnicas existentes + Digimon

> **Brief de design + handoff** pra um **frontier AI model** que vai fazer o rebalance. Carrega as **fundações técnicas já validadas** (refs de código) pra o modelo não redescobrir o mecanismo. Meta: **papéis estilo Persona** (tank, healer, support, physical, magic, AoA), **sem criar técnicas novas** (reaproveitar/reapontar as existentes) e **transparente** (mini-guide + texto de técnica descritivo).

## 1 — Objetivo (o "porquê")
- Problema atual: só **1** Digimon viável como "tanque mágico" (Guarda + Espírito); acumula buffs mas é fraco vs mágico; **não há papéis de suporte**; mágica (element 3) é fraca.
- Meta: **múltiplos papéis claros** (Persona-like), suporte com propósito, e o "momento grande" (AoA) via **Digivolution**.

## 2 — Pilares (fechados)
1. **Sabedoria = escalonador universal de buff/debuff** (simplificação: 1 stat escala tudo). Sabedoria vira a **identidade do suporte** (cura + status + escala todo buff). → resolve "sabedoria inútil vs boss": buff/debuff/cura funcionam vs boss (debuff baixa stats; buff time; cura) — só o *inflict de status* que o boss resiste.
2. **Buff/debuff atuais também buffam Espírito** (magic) + **técnicas dedicadas** de buff/debuff de magia. Set completo: {Poder, Guarda, Boost, **Espírito, Sabedoria**}.
3. **Escudo compartilhado** (1/2/3 turnos), reusing o guard-at-ceiling do Evo Buff.
4. **"Dark buff" dispara a Digivolution** (form grande) = **All-Out Attack**. Sem multi-ataque por turno, o *payoff* grande é a transição pro form blast.
5. **Reaproveitar técnicas** (re-aim/re-point), não criar novas — exceto 1-2 dedicadas de magia (se não couber).
6. **Transparência**: mini-guide do rebalance + texto de técnica mais descritivo.

## 3 — Fundações técnicas (validadas — use direto)
### 3a. Stats + fórmulas (`src/fightstg/fightstg_8008D3B4.c`)
- Stats: Poder=`stats[0]`, Guarda=`stats[1]`, **Espírito=`stats[2]`**, **Sabedoria=`stats[3]`**, Boost=`stats[4]`.
- **Dano normal** (`get_damage` `:7968`, element 2): `poder × Poder / Guarda`.
- **Dano mágico** (`get_special_damage` `:7804`, element 3): `poder × (próprio_Esp × 50 / alvo_Esp + 50)/100`, clamp `[poder/2, 2×poder]` — **Espírito nos 2 lados** (ataque + defesa mágica).
- **Cura** (`get_heal` `:8123`): `poder*64 + SAB × poder/8` — **cura escala com Sabedoria**.
- Status-roll usa Sabedoria: `rec->effect_chance + stats[3]/8` (atacante) vs `target->stats[3]/2` (resistência) — cf. `:8337–8342`.

### 3b. Buff/debuff (a lacuna)
- Tabelas `fightstg_tech_stat_effects` (IDs 198–208, `:8846`) + `_party_stat_effects` (199/201/203, `:8851`) buffam só {Poder, Guarda, Boost}. **Nenhum toca Espírito(2) nem Sabedoria(3).** ← lacuna.
- `change_modifier` (`:8709`): `modifiers[kind] += base × (factor × effect_power)/128`, clamp `[-base/2, base]`; duração via `fightstg_events_start_modifier`.
- **Gap**: `fightstg_rules_modifier_stats[4]={0,1,4,0}` (`:9006`) → `kind 3→Poder` (duplo), **sem slot pra Espírito**; e o caminho *party* do `get_stats` (`:7690`) **não aplica `modifiers[3]`**.
- **Como tocar Espírito/Sabedoria**: (a) mapear `kind`→`stats[2]`/`stats[3]` + aplicar no caminho party do `get_stats`, OU **(b) hook direto em `get_stats`** somando/subtraindo o valor (estilo evo_buff). → **(b) é mais flexível/sseguro** — priorize.

### 3c. Escudo (já existe — 1 turno, reusing)
- `port/game/game_mods.c:388–400` = `dw3_evo_shield_member`; guarda no ceiling; **reseta no início de cada turno da party** (`wfightmn_party_turn`); aplicada em `get_stats` (`:7722`). → **generalizar pra N turnos** (contador + reset em `party_turn`).

### 3d. Digivolution/blast (o "dark buff" dispara isso)
- Blast forms: `RecordsDigimon.blast_forms[5]` (valores = `records_digimon index + 1`), por faixa de nível.
- Transição dispara quando **gauge = 1000** (assume o blast form até o evento `0x13`).
- **"Dark buff" = técnica que chama o mesmo código do `wfightmn_blast`** (ou força o gauge a 1000). Cuidado: cooldown/limit, turnos do blast, reset do gauge. **Validar contra golden `wfightmn`.**

### 3e. Rotação/combo
- Comando 5 = *switch* (`:2677`).
- `RecordsDigimon.pair_technique` (`include/records.h:12`) = **técnica do time oferecida quando o parceiro entra** → gatilho: trocar PRA um Digimon abre a team tech.

### 3f. Técnica base vs especial + **risco-chave**
- `techniques[7]`: `[0]` e `[6]` lidas pelo FIGHTSTG (base/signature, flag `0x8000` no menu); `[1..5]` aprendidas (STFGTREP).
- **Risco-chave**: `records_techniques[]` é **global**. Re-aimar uma técnica muda **todo** Digimon que a usa. → **regra: antes de re-aimar, listar quem usa aquele técnica ID** (não quebrar outros).

## 4 — Papéis → DW3 (ref: Persona, esp. P5)
| Papel | DW3 (via técnica existente) | Stat |
|---|---|---|
| Tank físico | alta Guarda | Guarda |
| Tank mágico | alta Espírito | Espírito |
| Healer | cura + clean status | Sabedoria |
| Buff/Debuff | técnicas de buff/debuff (+Espírito, ×Sabedoria) | Sabedoria |
| Resist / Resist-Down | Sabedoria / debuff Sabedoria no alvo | Sabedoria |
| A-Attack (physical) | element 2 | Poder |
| Skill (magic) | element 3 | Espírito |
| **All-Out Attack** | **Dark buff → Digivolution** | — |

## 5 — Kits concretos (exemplos; modelo decide)
- **Patamon→Angemon** (id 0x1F, `records_digimon[7]`, Dark; base = técnica 60, element 3 = mágica): **healer/support** — cura(+status) + buff de time (team tech) + escudo.
- **Veemon** (id 0x91, `records_digimon[4]`, Ice; base = técnica 57, element 2 = physical): **debuff + dark buff (digivolve=AoA)** + big hit.
- (Extender às outras linhas pra fechar os papéis.)

## 6 — Entregáveis (p/ o frontier model)
1. **Dados rebalanceados**: stats dos Digimon + `records_techniques[]` (re-aimed) + `pair_technique` + `techniques[6]`.
2. **Texto de técnica mais descritivo** (file `records_language + 0xA2`).
3. **Mini-guide** do rebalance (o que mudou e por quê).
4. **Validação**: golden tests (`tests/`) + batalha PC port (+ handheld).

## 7 — Decisões abertas (dono/modelo)
1. **Escala de Sabedoria**: linear? cap? afeta buff, debuff, cura e escudo?
2. **Níveis de escudo**: 2 **e** 3? ou 1 + 3 (mega)?
3. **Dark buff→digivolve**: cooldown/limit? 1 por battle? afeta a gauge (reseta/avança)?
4. **Escopo**: só os papéis-chave primeiro, ou todas as linhas?
5. **Re-aim vs 1-2 técnicas dedicadas de magia** — a regra "quem usa essa técnica" decide.