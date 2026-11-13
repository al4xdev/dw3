13;2u# PRD 3 — Versão definitiva: rebalanceamento nativo (mods viram padrão)

Continuação de `definitive-combat-prd.md` e `definitive-progression-prd.md`
(mesmas regras de trabalho: camadas, teste entre camadas, constantes em
`port/game/definitive.h`, código sob `PC_PORT`, save antigo abre e joga). Cada
camada termina com um relatório curto ao dono.

Ideia: a configuração de mods com que o dono jogou metade do jogo vira o
comportamento fixo do jogo. O código dos mods já faz o que ele quer: **não mude
a lógica**, só tire o interruptor e fixe os valores.

## Configuração alvo (o que o dono usa hoje no launcher)

| Mod        | Opção                               | Valor fixo                        |
| ---------- | ----------------------------------- | --------------------------------- |
| `party_xp` | ligado, `share`                     | 100                               |
| `party_xp` | `knocked_out`, `catch_up`           | **removidos** (o dono nunca usou) |
| `xp_boost` | `exp` (EXP do rookie)               | 1x                                |
| `xp_boost` | `bits`                              | 1x                                |
| `xp_boost` | `form_exp`                          | 14x                               |
| `xp_boost` | `all_forms`                         | ligado                            |
| `xp_boost` | `card_boost` (15 boosters por drop) | ligado                            |
| `evo_buff` | —                                   | **removido**                      |

```c
#define DEF_XP_PARTY_SHARE   100  /* % da parte de um lutador que o membro que não lutou recebe */
#define DEF_XP_FORM_MULT      14  /* multiplicador da EXP de forma */
#define DEF_XP_ALL_FORMS       1
#define DEF_CARD_BOOST_PACKS  15
```

## Camada A — XP e cartas nativos

- Ler antes: `port/game/game_mods.c` (`port_party_xp_share`,
  `port_party_xp_knocked_out`, `port_xp_boost`, ~334–360 as opções),
  `port/mods/party_xp/`, `port/mods/xp_boost/`, `docs/LAUNCHER.md` ("Party
  experience", "XP boost"), os ganchos em `src/stfgtrep/`.
- As funções passam a rodar sempre, com as constantes acima, sem ler
  `port_mod_*` nem `settings.json`. EXP do rookie e bits ficam sem multiplicador
  (pode tirar esse caminho do código).
- `knocked_out` e `catch_up` saem do código: as opções, os ramos em
  `port_party_xp_share`, e `port_party_xp_knocked_out` com a chamada dele em
  WFIGHTMN, se não servir a mais nada. O comportamento fica o de hoje com os
  dois desligados: nocauteado não recebe a EXP compartilhada, sem bônus por
  nível abaixo. Sem código morto.
- `form_exp` em 14x desliga o teto de EXP de forma do original, como o mod já
  faz: manter.
- Tirar do launcher e do menu do handheld (`handheld/dw3-menu.py`, tela de XP:
  `XP / BITS BOOST`, `FORM XP`, `CARD DROP BOOST`, party XP) e apagar as pastas
  dos dois mods. Um `settings.json` antigo com essas chaves não pode dar erro:
  chaves desconhecidas são ignoradas.
- Atualizar `tests/port/mods.py` (os testes desses mods viram testes do
  comportamento padrão).

**Aceite:** A constante passa para dentro do joog, sem precisar do launcher pra
ativar

## Camada B — Remover o Evo Buff

Com fraqueza, stagger e corrente ele não tem mais função. Remover **sem deixar
resto**:

- `port_mod_evo_buff`, `dw3_evo_buff_member`, `dw3_evo_shield_member`,
  `wfightmn_evo_buff_apply` e todos os ganchos `PC_PORT` em
  `src/wfightmn/wfightmn_800A6440.c` (~589 e os demais) e em `fightstg`; a pasta
  do mod; a entrada no launcher e no menu; os testes dele.
- `grep -ri "evo_buff\|evo buff\|evo_shield"` no repo (fora de `.plan/` e
  `.dumps/`) tem de voltar vazio.
- Notas dos PRDs 1 e 2 que citam o Evo Buff (escudo com defesa unificada,
  animação curta) deixam de valer: marque-as como obsoletas, sem reescrever os
  PRDs.

**Aceite:** não ha resqucios do mod pelo codigo

## Camada C — Documentação

- `docs/LAUNCHER.md`: tirar as seções "Party experience", "XP boost" e Evo Buff;
  a regra de EXP vai para `docs/MECHANICS.md` (seção 6) como comportamento da
  versão definitiva.
- Conferir no código o que `all_forms` credita de fato: o texto do mod diz "toda
  digivolução que o membro possui", o `LAUNCHER.md` diz "as três formas
  escolhidas". Deixar um só texto, o verdadeiro, e ajustar a frase do
  `README.md` ("Experience") se precisar.
- `README.md` e `docs/README.md` já descrevem o resultado final: só conferir que
  batem.

## Fora deste PRD ( possivlel prd 4 final a definir ainda em sessao isolado pelo Dono)

# Roadmap de Desenvolvimento e Melhorias

## ⚔️ Gameplay e Balanceamento

- [x] **Modo Difícil (Hard Mode):** Limitar a equipe do jogador a apenas **2
      Digimon**.
- [ ] **Rebalanceamento Elemental:** Revisar e ajustar os multiplicadores e
      efeitos dos **campos elementais**.
- [ ] **Ajuste no Numemon:** Tornar o Numemon resistente a todos os elementos,
      exceto ataques do tipo **Físico**.

---

## 🎨 Interface (HUD) e Animações

- [x] **Indicadores de Dano (`WEAK` / `RESIST`):** Desenhar e implementar as
      animações de **WEAK** e **RESIST** em batalha.
- [x] **Unificação de Ícones Elementais:** Juntar os ícones de **Gelo/Água** e
      **Trovão/Máquina** (aplicar exclusivamente na **HUD de batalha** e no
      **Bestiário**).

---

## 🐛 Correção de Bugs e Novas Mecânicas

- [x] **Mecânica `OVERFINISH` (Bug transformado em Feature):**
  - **Contexto:** Ao derrotar um Digimon e ativar o _One More_, o inimigo
    permanece caído no chão e pode ser atacado novamente (aproveitando a
    animação de dano no chão já existente no jogo).
  - **Implementação:** Em vez de remover o comportamento, adicionar o aviso
    **`OVERFINISH`** em **vermelho** (aproveitando o fluxo de criação das artes
    de `WEAK`/`RESIST`) e conceder **Bits extras** ( talvez 0.1, se um dia achar
    alto, o dono vai balvcencar pra menos) proporcionais ao dano causado após a
    derrota.
- [x] **Correção do Teleporte Fora do Mapa:**
  - **Problema:** Ao usar o teleporte, o jogador nasce fora dos limites do mapa
    com os controles invertidos até conseguir andar de volta para dentro.
  - **Solução:** Ajustar o destino do teleporte para utilizar as **coordenadas
    iniciais padrão** de caso cada mapa já registradas no projeto.

- [ ] **não renderiza elemtno da tecnica em outros menus** - Em como troca de
      digimon, ou evolução ( rastrear mais menus talvez como status,
      digiblab,etc)**

---

## 🧭 Qualidade de Vida (QoL) e Menus

- [ ] **Guia do Jogador In-Game:**
  - Criar um guia/detonado salvo em **Markdown** indicando objetivos e próximos
    destinos.( o dono pode pegar um guia inteiro de um site famoso e trazer em
    markdown)
  - Adicionar um botão no menu principal/pause para abrir o guia diretamente no
    jogo.
- [ ] **Sistema de Notificações e Leilões de Asuka:**
  - Implementar um aviso na tela / menu de notificações (integrado à proposta do
    guia).
  - Adicionar um menu específico rastreando os **leilões em Asuka** (escopo
    enxuto, pois há poucos leilões no jogo) com opção de **teleporte quase
    direto** para o local do evento.

## 🧭 Traduções

Criar um pipeline automatizado em Python dentro de tools/ utilizando
tools/dump_text.py e o gancho de font_get_entry (src/main/font.c) para adicionar
o PT-BR como um 6º idioma selecionável no launcher e no menu do handheld
(carregando a base em inglês da ISO e aplicando a tabela de override pt.txt com
fallback automático para o inglês), além de sincronizar os outros 5 idiomas
europeus (EN, FR, DE, ES, IT); para isso, extrair via git diff apenas as strings
novas ou alteradas pela Definitive Edition (SKLINF, ESSTATUS, ESTRAINI, HUD,
Bestiário e Digi-Lab) e traduzi-las para FR/DE/ES/IT preservando os textos
originais do disco, enquanto o dump completo do inglês atualizado é traduzido
para PT-BR usando um LLM local/leve com glossário fixo (mantendo termos como
Digimon, Digi-Lab, Load Tech, Blast Gauge, Bits, TP, MP e os 7 elementos
padronizados), preservação estrita de tokens de controle (\n, variáveis e
códigos de cor), validação automática do limite de caracteres por janela (ex.:
<= 34 chars em descrições de técnicas) com re-geração para linhas excedentes, e
codificação dos caracteres acentuados do português (á, é, í, ó, ú, ã, õ, ç, â,
ê, ô) mapeados diretamente para os glifos já presentes na fonte europeia.

## 👧 Girls Power (Protagonista Feminina)

- [ ] **Substituição de Sprite Base (Junior ➔ Kaili):**
  - **Contexto:** Utilizar a personagem **Kaili** (amiga do protagonista
    Junior), que já possui praticamente todos os conjuntos de sprites
    equivalentes no jogo, para realizar a troca do modelo jogável.
- [ ] **Tratamento de Animações Ausentes:**
  - **Problema:** Possível falta de sprites específicos para ações exclusivas do
    protagonista (como **chutar árvores** e **pescar**).
  - **Plano A (Edição Técnica):** Analisar e adaptar a lógica em **C /
    Assembly** (com auxílio de ferramentas como Claude/Fable, se viável) para
    criar ou ajustar os frames necessários.
  - **Plano B (Fallback):** Reutilizar animações já existentes da Kaili para
    simular visualmente as ações de chute na árvore e pesca sem quebrar o jogo.
