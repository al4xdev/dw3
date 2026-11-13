# Guia in-game: missão principal, missões e leilões de Asuka

Pedido do dono (2026-11-13, seção "Qualidade de Vida (QoL) e Menus" de
`definitive-rebalance-prd.md`): um guia dentro do jogo que diga o que fazer, com
gatilhos no próprio jogo (missões tipo evento, missão principal), sem precisar de
um detonado inteiro; e um aviso/menu dos leilões de Asuka com teleporte quase
direto. Começado numa sessão do Fable no lab (73700cee…1b20), que acabou a cota
antes de testar; terminado nesta.

### Feito (2026-11-13, aplicado direto por decisão do dono)

- **Gatilho:** o próprio contador de história do jogo,
  `gamestate_data.progress` (0..45, gravado pelos eventos das fases), mais uma
  flag quando a linha pede (um item-chave possuído, `0x8000 | item`). Nada é
  gravado no save: um save antigo abre no passo certo.
- **Missão principal** (`def_quests`, `port/game/definitive_quest.c`): uma
  linha por passo da história, com título, o que fazer e para onde ir (o nome do
  mapa é o título do próprio jogo). Variantes por item-chave antes da linha
  simples (o 8lue Card falso, o Blue Card, a Sepik Mask, o Agumon Suit, o TNT
  Chip). Textos próprios em `tools/text_override/en.txt` (`$Q<passo>`,
  `$Q<passo>T`), escritos a partir do detonado que o dono trouxe, resumidos,
  sem copiar o texto dele.
- **Missões** (`def_missions`): a partir do passo 4 (quando o menu de campo
  abre), enquanto falta o item: El Dorado ID (abre os leilões), Folder Bag
  (Card Battles), Tree Boots, Fishing Pole.
- **Leilões** (`def_auctions`): os 16 da Cargo Tower são os
  `fieldstg_flag_events` do jogo. O leilão N roda com o passo da história
  exatamente o dele, a flag `0x400 + N` limpa e o dinheiro; qualquer resposta
  marca a flag. Estado: ainda não, ABERTO AGORA, comparecido, perdido (passou o
  passo sem ir; a loja escondida de Amaterasu vende os itens perdidos). Preço
  pela própria tabela de dinheiro do jogo.
- **Tela GUIDE:** nova entrada no menu de campo (entre SAVE e o Bestiário), no
  layout da rede Guardromon. Aba Quest: a missão principal e as missões; aba
  Auctions (L1/R1): os 16 leilões coloridos pelo estado. Num leilão aberto,
  confirmar duas vezes viaja para El Dorado (`0x213`, a entrada da Cargo Tower),
  como o teleporte do Guardromon (zera a corrente da caçada). Sem viagem do mundo
  real, de Gunslinger, de El Dorado e da armadilha de Byakko.
- **Avisos:** quando a missão principal muda de passo ou um leilão abre, um
  painel desce no topo do campo por 5 s ("NEW OBJECTIVE", "AUCTION OPEN"). Não
  prende o jogador; espera enquanto um evento, o menu ou o começo de uma batalha
  seguram o campo.
- **Desligado em script de replay** (os scripts são compartilhados com o
  emulador); `DW3_GUIDE=0` desliga sempre, `DW3_GUIDE=1` liga também em script.
- **Teste:** `tests/port/definitive_quest.py` (o driver: linha de cada passo,
  variantes por item, missões, estado/preço/viagem dos leilões, avisos uma vez
  por mudança, replay; e as tabelas contra as do jogo: os 16 leilões contra
  `fieldstg_flag_events`, todo passo 0..45 com linha simples depois das
  variantes, todo texto no `en.txt` dentro das linhas da janela); no jogo,
  `guide_ingame.py` (o aviso ao chegar, a entrada GUIDE, as abas, o passo 5 com
  os avisos do objetivo e do leilão, e a viagem para El Dorado).
