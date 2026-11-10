# 🎱 Marble Time Rush: jogo 3D em C++ e OpenGL

![C++](https://img.shields.io/badge/C++-OpenGL-00599C?style=for-the-badge&logo=cplusplus) ![OpenGL](https://img.shields.io/badge/OpenGL-3.3_Core-5586A4?style=for-the-badge&logo=opengl) ![Status](https://img.shields.io/badge/Status-Concluído-success?style=for-the-badge)

**Marble Time Rush** é um jogo de plataforma 3D no estilo *marble run*: o jogador conduz uma bola por pistas suspensas, desvia de obstáculos e corre contra o relógio até o troféu de cada fase. O projeto foi desenvolvido para a segunda avaliação da disciplina de Computação Gráfica da UFPA.

Nenhuma engine pronta foi usada. Renderização, física, colisão, câmeras, HUD e o editor de fases foram implementados manualmente sobre o OpenGL 3.3 Core, com shaders próprios em GLSL.

---

## 📸 Galeria do Projeto

**Menu e HUD Customizado**  
Menu de pausa/início com instruções de controle. Toda a interface (HUD) e a tipografia foram desenhadas manualmente usando primitivas geométricas e um shader 2D dedicado, sem carregamento de fontes externas.  
> <img width="912" height="600" alt="image" src="https://github.com/user-attachments/assets/9d8bf3f5-abec-497b-9b1a-e0ae8dfbc056" />

**Gameplay e Sombras Dinâmicas**  
A bola percorrendo a pista com câmera em terceira pessoa. É possível notar a textura procedural esférica da bola, a grade infinita do chão e o *Shadow Mapping* projetando sombras suaves e realistas a partir do ponto de vista do Sol.  
> <img width="912" height="650" alt="image" src="https://github.com/user-attachments/assets/c15651ff-2ced-4bf6-b4b7-3118711a6a0f" />

**Física e Colisão Orientada**  
Progressão em trechos de escada. A física calcula rigorosamente a colisão com caixas orientadas no espaço (OBB) para permitir subidas e interações corretas com rampas e degraus.  
> <img width="912" height="686" alt="image" src="https://github.com/user-attachments/assets/391d3238-639f-41ae-af4e-c8b755b9d9e2" />

**Coleta de Itens e Visibilidade**  
Trecho avançado mostrando o HUD dinâmico no topo (tempo restante à esquerda, pontuação à direita), com obstáculos e folhagens otimizadas pelo algoritmo geométrico de *Back-Face Culling*.  
> <img width="912" height="520" alt="image" src="https://github.com/user-attachments/assets/020e2524-bba8-4d06-8b29-95a78f27d1af" />

**Máquina de Estados: Tela de Vitória**  
Tela de conclusão de fase exibindo o evento de vitória da Máquina de Estados, com destaque para a pontuação final (70 pontos) e a sobreposição da UI sobre a cena 3D.  
> <img width="912" height="600" alt="Captura de tela 2026-11-10 161531" src="https://github.com/user-attachments/assets/0e584ac1-4457-44ec-b191-260d221d149e" />

**Editor de Fases Integrado (Raycasting)**  
O robusto modo editor executado em tempo real. À esquerda, a paleta de blocos e ferramentas; à direita, sliders interativos de ambiente (gravidade, sol, tempo); ao centro, a edição geométrica via interseção de raios matemáticos (*Raycasting*) na grade tridimensional.  
> <img width="912" height="520" alt="image" src="https://github.com/user-attachments/assets/ecc3a67a-723b-4a52-ae61-f21664caccf5" />

---

## 🎮 O que tem no jogo

- **3 fases jogáveis**, cada uma com tempo limite, ponto de partida e checkpoints. Tocar o troféu leva à fase seguinte, mantendo a pontuação.
- **Física de plataforma:** gravidade, aceleração na direção da câmera, pulo, atrito diferente em pisos escorregadios e queda com retorno ao último checkpoint.
- **Obstáculos variados:** sólidos, que repelem a bola, móveis (com movimento senoidal) e "falsos", além de moedas giratórias.
- **Editor de fases integrado:** coloca, seleciona, move, gira e apaga objetos com o mouse, por *raycasting* a partir da câmera, com encaixe numa grade.
- **Duas câmeras orbitais independentes**, uma para o editor e outra que segue a bola, controladas por coordenadas esféricas (yaw, pitch e distância).
- **HUD próprio:** relógio de 7 segmentos, pontuação, menus e botões desenhados só com retângulos, com uma fonte bitmap 5×7 feita à mão.

---

## ⚙️ Conceitos de Computação Gráfica aplicados

- **Pipeline completo por quadro:** VAO/VBO/EBO → vertex shader (Model · View · Projection) → rasterização → fragment shader → framebuffer.
- **Shadow mapping:** uma passada extra desenha a cena do ponto de vista do sol num framebuffer de profundidade de 1024×1024. O fragment shader principal usa esse mapa, com filtragem PCF, para decidir quais pixels estão na sombra.
- **Iluminação Blinn-Phong** com componente emissiva para objetos de destaque.
- **Transformações geométricas:** translação, rotação e escala por objeto. A rolagem da bola usa **quatérnions** acumulados a cada quadro, o que evita *gimbal lock* e faz o giro acompanhar a direção do movimento.
- **Geometria gerada por código:** esfera, cubo, rampa (prisma triangular) e cilindro, montados vértice a vértice.
- **Texturas procedurais** (xadrez, grade do editor) geradas em tempo de execução, com mipmaps e repetição por escala de UV.
- **Ray casting:** converte o clique do mouse de coordenadas de tela para NDC, depois para o espaço da câmera e para o mundo, gerando um raio usado para posicionar e selecionar objetos.
- **Colisão com caixa orientada** para obstáculos, rampas e pilares, e **back-face culling** com Z-buffer.

---

## 🕹️ Controles

| Modo | Tecla | Ação |
|---|---|---|
| Jogo | `W A S D` / setas | Movimenta a bola |
| Jogo | `Espaço` | Pula |
| Jogo | Botão direito (arrastar) | Gira a câmera em volta da bola |
| Geral | `Tab` / `Esc` | Alterna entre jogo e editor / abre o menu |
| Editor | Botão direito (arrastar) | Gira a câmera do editor |
| Editor | `W A S D` / `Q E` | Move o foco da câmera |
| Editor | `1` a `9` | Escolhe o tipo de objeto a colocar |
| Editor | Clique esquerdo | Coloca ou seleciona um objeto |
| Editor | `R` / `F` / `G` / `M` | Gira 90°, troca o tamanho, troca a forma, troca o eixo de movimento |
| Editor | `Delete` / `Ctrl+Z` | Apaga o objeto selecionado / desfaz |
| Editor | `Page Up` / `Page Down` / `N` | Navega entre fases / cria fase nova |
| Editor | `F5` / `F9` | Salva / carrega a fase atual (`cenario_faseN.txt`) |

---

## 🛠️ Tecnologias

- **Linguagem:** C++
- **Gráficos:** OpenGL 3.3 Core Profile, GLSL
- **Janela e entrada:** GLFW
- **Carregamento de funções OpenGL:** GLAD
- **Matemática:** GLM (vetores, matrizes e quatérnions)

---

## 📁 Estrutura

| Arquivo | Conteúdo |
|---|---|
| `marble_editor_v12_fix.cpp` | Versão completa: 3 fases, editor de fases, HUD e todos os efeitos |
| `marble_run.cpp` | Versão anterior, com pista contínua, inimigos móveis e relógio por fase |
| `jogo3D.exe` | Executável para Windows |
| `Apresentação de CGPI - 2.pdf` | Slides da apresentação do projeto |

---

## 🚀 Como executar

**Windows (executável pronto):** baixe o repositório e abra `jogo3D.exe`.

**Compilando:**

1. Tenha um compilador C++ e as bibliotecas GLFW, GLAD e GLM configuradas (por exemplo, num projeto do Visual Studio).
2. Adicione `marble_editor_v12_fix.cpp` e o `glad.c` gerado para OpenGL 3.3 Core ao projeto.
3. Compile e execute.

---

## 👩‍💻 Autores

| Nome | GitHub |
|---|---|
| **Adryele Oliveira** | [@elleoliveir](https://github.com/elleoliveir) |
| **Filipe Corrêa** | [@Filipecorr34](https://github.com/Filipecorr34) |

Desenvolvido para a disciplina de Computação Gráfica, ministrada pelo Prof. Dr. Fabrício Rossy (UFPA).
