# Fix de glifos do menu de batalha + simplificação dos mods de pad

Escopo: só as modificações do dono (mods, menu de batalha, launcher). Não mexer no upstream.

## Estado
O dono testou no console (192.168.0.170) e confirmou que o fix funciona: os glifos do menu de batalha seguem o custom. O binário testado tem MD5 `9ff7e35c62b01aa55689f76799b1de61`. Os inputs não foram alterados.

## Causa do bug (para o corpo do commit)
Os lugares do battle menu usam os nomes do launcher (botão físico), e o `pad.map` usa os nomes do jogo. O launcher traduz com `PLACE_TO_GAME` ao gravar: south vira west, west vira east, east vira south, north continua north. O código buscava letra e símbolo sem traduzir. Por isso só o `north` batia.

## Passo 1 - commitar o fix
Arquivos alterados e instalados no console:
- `port/game/game_mods.c`:
  - `dw3_pad_bit_by_place`, guardando o botão PS1 de cada linha do `pad.map` por lugar
  - `port_battle_menu_symbol_bit`
  - `bm_place_to_game` (a tradução), usada em `port_battle_menu_letter` e `port_battle_menu_symbol_bit`
- `src/fightstg/fightstg_8008D3B4.c`: `fightstg_battle_menu_icon_text` usa o botão do `pad.map` no lugar da ação
- `include/port.h`: declaração de `port_battle_menu_symbol_bit`

Antes do commit:
- Rodar `tests/port/mods.py`. A tradução pode mudar o que os testes de remap do battle menu esperam; se algum falhar, decidir se o teste ou o código está errado.
- Commit só desses três arquivos. Não incluir `.plan/`, `.dumps/` nem `build/`.

## Passo 2 - simplificação (tarefa separada, depois do commit)
Linha de base: rodar `tests/port/mods.py` antes de começar e de novo depois de cada item.

1. Unificar as tabelas de lugar em uma só estrutura por posição: `bm_places`/`bm_place_bits`, `dw3_pad_places`, `dw3_place_index` e a nova `bm_place_to_game`. O bug veio de três vocabulários de lugar misturados.
2. Fonte única da tradução launcher-jogo: hoje `PLACE_TO_GAME` está no `dw3-menu.py` e `bm_place_to_game` no C, duplicadas. Deixar uma só (por exemplo, o launcher grava o battle menu já nos nomes do jogo) ou documentar as duas lado a lado.
3. Guardar o `pad.map` uma vez só: letra e botão PS1 por posição. Hoje existem `letter[bit]`/`perm[bit]` e os arrays por lugar (`dw3_pad_letters_by_place`, `dw3_pad_bit_by_place`) com o mesmo dado.
4. `fightstg_battle_menu_icon_text` passa a usar `dw3_button_glyph`, sem repetir `0x1B/0x2F/0x32/0x34`.
5. `dw3_feat_icons`: hoje é checada em `dw3_button_glyph` e em `src/main/message.c:711`, e o menu de batalha a ignora (desenha a letra mesmo com `DW3_FEAT_ICONS=0`). Decidir um comportamento único e aplicar nos dois.
6. Pad lido por dois caminhos no menu de batalha (`port_battle_menu_pressed` com `dw3_raw_pressed`, e `pad_state.get_pressed`): ficar com um só, sem mudar o comportamento dos inputs.
7. `dw3_raw_buttons` só é escrita (`src/main/pad.c:300`): confirmar com grep e remover se for mesmo morta.
8. Revisar comentários longos dos blocos `pad.map` e battle_menu, atualizando o que não bate mais com o código.
9. `handheld/dw3-menu.py`: ler o arquivo inteiro e simplificar a dupla tradução de lugar (`PLACE_TO_GAME`, `PLACE_FROM_GAME`, `PLACE_DISPLAY`) e o que mais estiver duplicado.
10. Varrer os demais mods de `game_mods.c` (`skip_dialogues`, `global_save`, `party_xp`, `xp_boost`, `evo_buff`, `led_bar`, viagem rápida, objetivo) e os testes, e listar o que for redundante antes de mexer.
11. Compilar ARM (`tools/build_arm.sh`), instalar no console e pedir novo teste ao dono antes de commitar.

## Fora de escopo
Os inputs em si, que o dono considera perfeitos. O código upstream.
