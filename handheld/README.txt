Digimon World 3 / 2003 - Port Nativo para R36S (ArkOS / PortMaster)
===================================================================

COMO INSTALAR NO R36S:
1. Copie o arquivo "Digimon World 3.sh" para a pasta "/roms/ports/" do cartão SD.
2. Copie a pasta "dw3" inteira para "/roms/ports/dw3/".
3. Coloque a imagem do jogo (Digimon World 2003 Europa, .bin e .cue) na pasta:
   /roms/ports/dw3/game/

CONTROLES NO R36S:
- D-Pad ou Analógico Esquerdo: Movimento e menus
- Botão A (Sul): Confirmar / Conversar (Cross)
- Botão B (Leste): Cancelar / Menu (Circle)
- Botão X (Oeste): Ação secundária (Square)
- Botão Y (Norte): Status (Triangle)
- L1 / R1: Trocar abas de menu
- L2 / R2: Funções adicionais
- Select: Mapa / Seleção
- Start: Pausa / Iniciar

HOTKEYS HANDHELD:
- Select + Start: Sair do jogo de forma limpa
- L3 + R3 (pressionar os dois analógicos): Alternar Fast-Forward (Acelerar jogo)

OPÇÕES DE VÍDEO:
Por padrão o jogo roda com DW3_VIDEO="fbdev" usando escalonamento 2x inteiro
acelerado por ARM NEON na tela de 640x480 do R36S.
Caso queira testar a GPU Mali via SDL2 KMSDRM, edite o launcher "Digimon World 3.sh"
e altere para:
export DW3_VIDEO="sdl2"
