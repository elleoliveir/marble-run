#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <GLM/glm/glm.hpp>
#include <GLM/glm/gtc/matrix_transform.hpp>
#include <GLM/glm/gtc/type_ptr.hpp>
#include <GLM/glm/gtc/quaternion.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cctype>

// ============================================================
// MARBLE RUN 3D -- v2
// Pista continua com rampas/curvas, ceu estrelado estatico,
// cronometro por fase (HUD no canto superior direito) e telas
// de inicio / vitoria / derrota com fonte propria em pixels.
// ============================================================

// ------------------------------------------------------------
// SHADER PRINCIPAL (Phong + textura + sombra)
// ------------------------------------------------------------
const char* vertexShaderSource = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform vec2 uvScale;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 FragPosLightSpace;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords * uvScale;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)glsl";

const char* fragmentShaderSource = R"glsl(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;

uniform sampler2D albedoMap;
uniform sampler2D shadowMap;
uniform bool useTexture;
uniform vec3 fallbackColor;
uniform vec3 lightPos;
uniform vec3 viewPos;

float calcularSombra(vec4 fragPosLuz, vec3 normal, vec3 lightDir) {
    vec3 coordsProj = fragPosLuz.xyz / fragPosLuz.w;
    coordsProj = coordsProj * 0.5 + 0.5;
    if (coordsProj.z > 1.0) return 0.0;

    float bias = max(0.006 * (1.0 - dot(normal, lightDir)), 0.0015);
    float sombra = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float profundidadeMapa = texture(shadowMap, coordsProj.xy + vec2(x, y) * texelSize).r;
            sombra += (coordsProj.z - bias) > profundidadeMapa ? 1.0 : 0.0;
        }
    return sombra / 9.0;
}

void main() {
    vec3 cor = useTexture ? texture(albedoMap, TexCoords).rgb : fallbackColor;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    vec3 ambiente = 0.30 * cor;
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 difusa = diff * cor;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
    vec3 especular = 0.3 * spec * vec3(1.0);

    float sombra = calcularSombra(FragPosLightSpace, norm, lightDir);
    vec3 corFinal = ambiente + (1.0 - sombra) * (difusa + especular);

    FragColor = vec4(corFinal, 1.0);
}
)glsl";

// ------------------------------------------------------------
// SHADER DE PROFUNDIDADE (passe de sombra)
// ------------------------------------------------------------
const char* depthVertexShaderSource = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
void main() { gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0); }
)glsl";

const char* depthFragmentShaderSource = R"glsl(
#version 330 core
void main() { }
)glsl";

// ------------------------------------------------------------
// SHADER DAS ESTRELAS (pontos redondos e suaves, estaticos)
// ------------------------------------------------------------
const char* starVertexShaderSource = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aBrilho;
uniform mat4 view;
uniform mat4 projection;
out float brilho;
void main() {
    gl_Position = projection * view * vec4(aPos, 1.0);
    gl_PointSize = 1.2 + aBrilho * 2.8;
    brilho = aBrilho;
}
)glsl";

const char* starFragmentShaderSource = R"glsl(
#version 330 core
in float brilho;
out vec4 FragColor;
void main() {
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);
    if (dist > 0.5) discard;
    float alpha = smoothstep(0.5, 0.0, dist);
    vec3 cor = mix(vec3(0.65, 0.75, 1.0), vec3(1.0, 0.98, 0.85), brilho);
    FragColor = vec4(cor, alpha);
}
)glsl";

// ------------------------------------------------------------
// SHADER DO HUD (retangulos 2D, usados no relogio e na fonte de texto)
// ------------------------------------------------------------
const char* hudVertexShaderSource = R"glsl(
#version 330 core
layout (location = 0) in vec2 aPos;
uniform mat4 projecaoHUD;
uniform mat4 modelHUD;
void main() { gl_Position = projecaoHUD * modelHUD * vec4(aPos, 0.0, 1.0); }
)glsl";

const char* hudFragmentShaderSource = R"glsl(
#version 330 core
out vec4 FragColor;
uniform vec3 corHUD;
uniform float alphaHUD;
void main() { FragColor = vec4(corHUD, alphaHUD); }
)glsl";

// ------------------------------------------------------------
// UTILITARIOS DE SHADER
// ------------------------------------------------------------
unsigned int compilarShader(GLenum tipo, const char* fonte) {
    unsigned int shader = glCreateShader(tipo);
    glShaderSource(shader, 1, &fonte, NULL);
    glCompileShader(shader);
    int sucesso;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &sucesso);
    if (!sucesso) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, 1024, NULL, infoLog);
        std::cout << "[ERRO SHADER] " << infoLog << std::endl;
    }
    return shader;
}

unsigned int criarPrograma(const char* vsSrc, const char* fsSrc) {
    unsigned int vs = compilarShader(GL_VERTEX_SHADER, vsSrc);
    unsigned int fs = compilarShader(GL_FRAGMENT_SHADER, fsSrc);
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int sucesso;
    glGetProgramiv(prog, GL_LINK_STATUS, &sucesso);
    if (!sucesso) {
        char infoLog[1024];
        glGetProgramInfoLog(prog, 1024, NULL, infoLog);
        std::cout << "[ERRO LINK] " << infoLog << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ------------------------------------------------------------
// PSEUDO-ALEATORIO DETERMINISTICO (sem dependencias externas)
// ------------------------------------------------------------
float pseudoAleatorio(int semente) {
    float x = sinf((float)semente * 12.9898f) * 43758.5453f;
    return x - floorf(x);
}

// ------------------------------------------------------------
// GEOMETRIA: ESFERA
// ------------------------------------------------------------
void criarEsfera(unsigned int& VAO, unsigned int& indexCount, float raio, unsigned int setores, unsigned int pilhas) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    const float PI = 3.14159265359f;

    for (unsigned int i = 0; i <= pilhas; ++i) {
        float anguloPilha = PI / 2.0f - (float)i * (PI / pilhas);
        float xy = raio * cosf(anguloPilha);
        float y = raio * sinf(anguloPilha);
        for (unsigned int j = 0; j <= setores; ++j) {
            float anguloSetor = (float)j * (2.0f * PI / setores);
            float x = xy * cosf(anguloSetor);
            float z = xy * sinf(anguloSetor);
            vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
            glm::vec3 normal = glm::normalize(glm::vec3(x, y, z));
            vertices.push_back(normal.x); vertices.push_back(normal.y); vertices.push_back(normal.z);
            vertices.push_back((float)j / setores); vertices.push_back((float)i / pilhas);
        }
    }
    for (unsigned int i = 0; i < pilhas; ++i) {
        unsigned int k1 = i * (setores + 1);
        unsigned int k2 = k1 + setores + 1;
        for (unsigned int j = 0; j < setores; ++j, ++k1, ++k2) {
            if (i != 0) { indices.push_back(k1); indices.push_back(k2); indices.push_back(k1 + 1); }
            if (i != (pilhas - 1)) { indices.push_back(k1 + 1); indices.push_back(k2); indices.push_back(k2 + 1); }
        }
    }
    indexCount = (unsigned int)indices.size();

    unsigned int VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

// ------------------------------------------------------------
// GEOMETRIA: CUBO
// ------------------------------------------------------------
void criarCubo(unsigned int& VAO, unsigned int& indexCount) {
    float v[] = {
        -0.5f,-0.5f, 0.5f,   0,0,1,   0,0,
         0.5f,-0.5f, 0.5f,   0,0,1,   1,0,
         0.5f, 0.5f, 0.5f,   0,0,1,   1,1,
        -0.5f, 0.5f, 0.5f,   0,0,1,   0,1,

         0.5f,-0.5f,-0.5f,   0,0,-1,  0,0,
        -0.5f,-0.5f,-0.5f,   0,0,-1,  1,0,
        -0.5f, 0.5f,-0.5f,   0,0,-1,  1,1,
         0.5f, 0.5f,-0.5f,   0,0,-1,  0,1,

        -0.5f,-0.5f,-0.5f,  -1,0,0,   0,0,
        -0.5f,-0.5f, 0.5f,  -1,0,0,   1,0,
        -0.5f, 0.5f, 0.5f,  -1,0,0,   1,1,
        -0.5f, 0.5f,-0.5f,  -1,0,0,   0,1,

         0.5f,-0.5f, 0.5f,   1,0,0,   0,0,
         0.5f,-0.5f,-0.5f,   1,0,0,   1,0,
         0.5f, 0.5f,-0.5f,   1,0,0,   1,1,
         0.5f, 0.5f, 0.5f,   1,0,0,   0,1,

        -0.5f, 0.5f, 0.5f,   0,1,0,   0,0,
         0.5f, 0.5f, 0.5f,   0,1,0,   1,0,
         0.5f, 0.5f,-0.5f,   0,1,0,   1,1,
        -0.5f, 0.5f,-0.5f,   0,1,0,   0,1,

        -0.5f,-0.5f,-0.5f,   0,-1,0,  0,0,
         0.5f,-0.5f,-0.5f,   0,-1,0,  1,0,
         0.5f,-0.5f, 0.5f,   0,-1,0,  1,1,
        -0.5f,-0.5f, 0.5f,   0,-1,0,  0,1,
    };
    unsigned int idx[] = {
        0,1,2, 2,3,0,  4,5,6, 6,7,4,  8,9,10, 10,11,8,
        12,13,14, 14,15,12,  16,17,18, 18,19,16,  20,21,22, 22,23,20
    };
    indexCount = 36;

    unsigned int VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

// ------------------------------------------------------------
// ESTRELAS (esfera de pontos ao redor do cenario, estaticas)
// ------------------------------------------------------------
struct EstrelasInfo { unsigned int VAO; unsigned int quantidade; };

EstrelasInfo criarEstrelas(int quantidade, glm::vec3 centro, float raioMin, float raioMax) {
    std::vector<float> dados; // pos(3) + brilho(1)
    const float PI = 3.14159265359f;
    for (int i = 0; i < quantidade; ++i) {
        float u = pseudoAleatorio(i * 3 + 1);
        float v = pseudoAleatorio(i * 3 + 2);
        float raioRand = pseudoAleatorio(i * 3 + 3);

        float theta = 2.0f * PI * u;
        float phi = acosf(2.0f * v - 1.0f);
        float raio = raioMin + raioRand * (raioMax - raioMin);

        float x = centro.x + raio * sinf(phi) * cosf(theta);
        float y = centro.y + raio * sinf(phi) * sinf(theta);
        float z = centro.z + raio * cosf(phi);

        float brilho = pseudoAleatorio(i * 7 + 11);
        dados.push_back(x); dados.push_back(y); dados.push_back(z); dados.push_back(brilho);
    }

    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, dados.size() * sizeof(float), dados.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    return { VAO, (unsigned int)quantidade };
}

// ------------------------------------------------------------
// TEXTURAS PROCEDURAIS
// ------------------------------------------------------------
unsigned int criarTexturaQuadriculada(int tamanho, int quadrados, glm::vec3 corA, glm::vec3 corB) {
    std::vector<unsigned char> dados(tamanho * tamanho * 3);
    int passo = tamanho / quadrados;
    for (int y = 0; y < tamanho; ++y)
        for (int x = 0; x < tamanho; ++x) {
            bool ehA = (((x / passo) + (y / passo)) % 2) == 0;
            glm::vec3 cor = ehA ? corA : corB;
            int i = (y * tamanho + x) * 3;
            dados[i + 0] = (unsigned char)(cor.r * 255.0f);
            dados[i + 1] = (unsigned char)(cor.g * 255.0f);
            dados[i + 2] = (unsigned char)(cor.b * 255.0f);
        }
    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tamanho, tamanho, 0, GL_RGB, GL_UNSIGNED_BYTE, dados.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texID;
}

unsigned int criarTexturaListrada(int tamanho, int faixas, glm::vec3 corA, glm::vec3 corB) {
    std::vector<unsigned char> dados(tamanho * tamanho * 3);
    int passo = tamanho / faixas;
    for (int y = 0; y < tamanho; ++y)
        for (int x = 0; x < tamanho; ++x) {
            bool ehA = ((x / passo) % 2) == 0;
            glm::vec3 cor = ehA ? corA : corB;
            int i = (y * tamanho + x) * 3;
            dados[i + 0] = (unsigned char)(cor.r * 255.0f);
            dados[i + 1] = (unsigned char)(cor.g * 255.0f);
            dados[i + 2] = (unsigned char)(cor.b * 255.0f);
        }
    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tamanho, tamanho, 0, GL_RGB, GL_UNSIGNED_BYTE, dados.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texID;
}

// ------------------------------------------------------------
// PISTA: sequencia de segmentos orientados
// ------------------------------------------------------------
struct Segmento {
    glm::vec3 p0, p1;
    float largura;
    bool escorregadio;
    bool meta;
    int fase; // 1, 2 ou 3 -- define o tempo de relogio dessa etapa
    glm::vec3 dirF, dirLat, dirU;
    float comprimento;
};

void calcularBase(Segmento& s) {
    glm::vec3 delta = s.p1 - s.p0;
    s.comprimento = glm::length(delta);
    s.dirF = glm::normalize(delta);
    glm::vec3 upMundo(0.0f, 1.0f, 0.0f);
    s.dirLat = glm::normalize(glm::cross(s.dirF, upMundo));
    s.dirU = glm::normalize(glm::cross(s.dirLat, s.dirF));
}

struct PontoTrilha { glm::vec3 pos; float largura; bool escorregadio; bool meta; };

std::vector<Segmento> criarPista() {
    std::vector<PontoTrilha> pontos = {
        { glm::vec3(0.0f,  0.0f,  0.0f), 3.5f, false, false }, // inicio (fase 1)
        { glm::vec3(0.0f,  0.0f,  9.0f), 3.0f, false, false },
        { glm::vec3(2.5f,  0.0f, 15.0f), 2.6f, false, false }, // curva
        { glm::vec3(3.0f,  3.5f, 22.0f), 2.2f, false, false }, // rampa subindo
        { glm::vec3(1.5f,  3.5f, 28.0f), 1.1f, false, false }, // ponte estreita (comeca fase 2)
        { glm::vec3(-1.0f, 3.5f, 34.0f), 1.1f, false, false }, // ponte estreita continua
        { glm::vec3(-2.5f, 0.5f, 42.0f), 2.4f, false, false }, // rampa descendo
        { glm::vec3(-2.0f, 0.5f, 50.0f), 2.8f, true,  false }, // piso escorregadio
        { glm::vec3(0.0f,  0.5f, 58.0f), 2.8f, true,  false }, // fim do gelo (comeca fase 3)
        { glm::vec3(0.0f,  0.5f, 66.0f), 3.0f, false, false },
        { glm::vec3(0.0f,  2.2f, 73.0f), 3.0f, false, false }, // rampa final
        { glm::vec3(0.0f,  2.2f, 80.0f), 4.5f, false, true  }, // META
    };

    std::vector<Segmento> segs;
    for (size_t i = 0; i + 1 < pontos.size(); ++i) {
        Segmento s;
        s.p0 = pontos[i].pos;
        s.p1 = pontos[i + 1].pos;
        s.largura = (pontos[i].largura + pontos[i + 1].largura) * 0.5f;
        s.escorregadio = pontos[i + 1].escorregadio;
        s.meta = pontos[i + 1].meta;
        if (i <= 3) s.fase = 1;
        else if (i <= 7) s.fase = 2;
        else s.fase = 3;
        calcularBase(s);
        segs.push_back(s);
    }
    return segs;
}

// ------------------------------------------------------------
// INIMIGOS
// ------------------------------------------------------------
struct Inimigo {
    glm::vec3 centro, eixoMovimento;
    float amplitude, velocidade, fase, raio;
};

glm::vec3 posicaoInimigo(const Inimigo& e, float t) {
    return e.centro + e.eixoMovimento * sinf(t * e.velocidade + e.fase) * e.amplitude;
}

std::vector<Inimigo> criarInimigos() {
    return {
        { glm::vec3(1.5f, 4.05f, 25.0f), glm::vec3(1,0,0), 1.3f, 1.4f, 0.0f, 0.55f },
        { glm::vec3(-1.0f, 1.05f, 54.0f), glm::vec3(0,0,1), 2.0f, 1.0f, 1.5f, 0.55f },
    };
}

// ------------------------------------------------------------
// PILARES DE FUNDO
// ------------------------------------------------------------
struct Pilar { glm::vec3 pos; glm::vec3 escala; };

std::vector<Pilar> criarPilares() {
    std::vector<Pilar> pilares;
    for (int lado = -1; lado <= 1; lado += 2) {
        for (int i = 0; i < 26; ++i) {
            float z = i * 3.6f - 6.0f;
            float distX = 12.0f + pseudoAleatorio(i * 7 + lado * 31) * 6.0f;
            float altura = 9.0f + pseudoAleatorio(i * 13 + lado * 17) * 12.0f;
            pilares.push_back({ glm::vec3(lado * distX, altura * 0.5f - 6.0f, z), glm::vec3(1.6f, altura, 1.6f) });
        }
    }
    return pilares;
}

// ------------------------------------------------------------
// ESTADO GLOBAL DO JOGO
// ------------------------------------------------------------
enum class EstadoJogo { MENU, JOGANDO, VITORIA, DERROTA };
EstadoJogo estado = EstadoJogo::MENU;

const float RAIO_BOLA = 0.6f;
const float GRAVIDADE = 20.0f;
const float duracaoFases[3] = { 20.0f, 30.0f, 40.0f };

glm::vec3 posBola, velBola;
glm::quat rotacaoBola;
glm::vec3 direcaoAtual;
glm::vec3 ultimoCheckpointPos;
bool noChao = false;
float atritoAtual = 0.945f;

int faseAtual = 1;
float tempoRestante = duracaoFases[0];
bool fimDeJogo = false;
bool vitoria = false;

void reiniciarJogo() {
    posBola = glm::vec3(0.0f, 1.2f, 0.0f);
    velBola = glm::vec3(0.0f);
    rotacaoBola = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    direcaoAtual = glm::vec3(0.0f, 0.0f, 1.0f);
    ultimoCheckpointPos = posBola;
    noChao = false;
    atritoAtual = 0.945f;
    faseAtual = 1;
    tempoRestante = duracaoFases[0];
    fimDeJogo = false;
    vitoria = false;
}

// ------------------------------------------------------------
// CAMERA
// ------------------------------------------------------------
float cameraYawOffset = 0.0f;
float cameraPitch = 40.0f;
float cameraDistance = 15.0f;

bool mousePressionado = false;
double ultimoMouseX = 0.0, ultimoMouseY = 0.0;

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) { mousePressionado = true; glfwGetCursorPos(window, &ultimoMouseX, &ultimoMouseY); }
        else if (action == GLFW_RELEASE) mousePressionado = false;
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!mousePressionado) return;
    double dx = xpos - ultimoMouseX, dy = ypos - ultimoMouseY;
    ultimoMouseX = xpos; ultimoMouseY = ypos;
    cameraYawOffset += (float)dx * 0.3f;
    cameraPitch -= (float)dy * 0.3f;
    cameraPitch = glm::clamp(cameraPitch, 12.0f, 82.0f);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    cameraDistance -= (float)yoffset * 0.9f;
    cameraDistance = glm::clamp(cameraDistance, 6.0f, 32.0f);
}

// ------------------------------------------------------------
// FISICA E COLISAO
// ------------------------------------------------------------
void atualizarFisica(GLFWwindow* window, float dt, std::vector<Segmento>& pista, std::vector<Inimigo>& inimigos, float tempoJogo) {
    float inputAcel = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) inputAcel = 1.0f;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) inputAcel = -1.0f;

    const float ACELERACAO = 14.0f;
    velBola += direcaoAtual * (inputAcel * ACELERACAO) * dt;

    if (noChao) {
        float accelDescida = -GRAVIDADE * direcaoAtual.y;
        velBola += direcaoAtual * accelDescida * dt;
        velBola *= atritoAtual;
    }

    velBola.y -= GRAVIDADE * dt;

    glm::vec3 posAntiga = posBola;
    glm::vec3 posNova = posBola + velBola * dt;

    bool encontrouChao = false;
    for (auto& seg : pista) {
        glm::vec3 relAntiga = posAntiga - seg.p0;
        glm::vec3 relNova = posNova - seg.p0;

        float fNova = glm::dot(relNova, seg.dirF);
        float lNova = glm::dot(relNova, seg.dirLat);
        float uAntiga = glm::dot(relAntiga, seg.dirU);
        float uNova = glm::dot(relNova, seg.dirU);

        bool dentroComprimento = fNova >= -0.4f && fNova <= seg.comprimento + 0.4f;
        bool dentroLargura = fabsf(lNova) <= seg.largura * 0.5f + RAIO_BOLA * 0.4f;

        if (dentroComprimento && dentroLargura) {
            if (uAntiga >= RAIO_BOLA - 0.08f && uNova <= RAIO_BOLA) {
                glm::vec3 pontoSuperficie = seg.p0 + seg.dirF * fNova + seg.dirLat * lNova;
                posNova = pontoSuperficie + seg.dirU * RAIO_BOLA;
                velBola.y = 0.0f;
                encontrouChao = true;
                direcaoAtual = seg.dirF;
                atritoAtual = seg.escorregadio ? 0.999f : 0.945f;

                if (seg.meta) { vitoria = true; fimDeJogo = true; }

                if (seg.fase > faseAtual) {
                    faseAtual = seg.fase;
                    tempoRestante = duracaoFases[faseAtual - 1];
                    ultimoCheckpointPos = posNova;
                    std::cout << "\n>> Fase " << faseAtual << " iniciada! Tempo: " << (int)duracaoFases[faseAtual - 1] << "s\n";
                }
                break;
            }
        }
    }
    noChao = encontrouChao;
    posBola = posNova;

    for (auto& inimigo : inimigos) {
        glm::vec3 posIn = posicaoInimigo(inimigo, tempoJogo);
        float dist = glm::length(posBola - posIn);
        if (dist < RAIO_BOLA + inimigo.raio) {
            glm::vec3 empurrao = (dist > 0.001f) ? glm::normalize(posBola - posIn) : glm::vec3(0, 1, 0);
            velBola += empurrao * 8.0f;
            tempoRestante -= 6.0f * dt;
        }
    }

    if (posBola.y < -12.0f) {
        tempoRestante -= 6.0f;
        posBola = ultimoCheckpointPos;
        velBola = glm::vec3(0.0f);
    }

    tempoRestante -= dt;
    if (tempoRestante <= 0.0f) { tempoRestante = 0.0f; fimDeJogo = true; }
}

// ------------------------------------------------------------
// HUD -- retangulos e relogio digital de 7 segmentos
// ------------------------------------------------------------
void desenharRetanguloHUD(unsigned int shaderHUD, unsigned int vao, float x, float y, float w, float h, glm::vec3 cor, float alpha = 1.0f) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
    model = glm::scale(model, glm::vec3(w, h, 1.0f));
    glUniformMatrix4fv(glGetUniformLocation(shaderHUD, "modelHUD"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(glGetUniformLocation(shaderHUD, "corHUD"), 1, glm::value_ptr(cor));
    glUniform1f(glGetUniformLocation(shaderHUD, "alphaHUD"), alpha);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void desenharDigitoHUD(unsigned int shaderHUD, unsigned int vao, int digito, float x, float y, float W, float H, float esp, glm::vec3 cor) {
    static bool tabela[10][7] = {
        {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1},
        {0,1,1,0,0,1,1}, {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0},
        {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
    };
    if (digito < 0 || digito > 9) return;
    bool* s = tabela[digito];
    if (s[0]) desenharRetanguloHUD(shaderHUD, vao, x, y, W, esp, cor);
    if (s[1]) desenharRetanguloHUD(shaderHUD, vao, x + W - esp, y, esp, H / 2, cor);
    if (s[2]) desenharRetanguloHUD(shaderHUD, vao, x + W - esp, y + H / 2, esp, H / 2, cor);
    if (s[3]) desenharRetanguloHUD(shaderHUD, vao, x, y + H - esp, W, esp, cor);
    if (s[4]) desenharRetanguloHUD(shaderHUD, vao, x, y + H / 2, esp, H / 2, cor);
    if (s[5]) desenharRetanguloHUD(shaderHUD, vao, x, y, esp, H / 2, cor);
    if (s[6]) desenharRetanguloHUD(shaderHUD, vao, x, y + H / 2 - esp / 2, W, esp, cor);
}

void desenharNumeroHUD(unsigned int shaderHUD, unsigned int vao, int numero, float x, float y, float W, float H, float esp, glm::vec3 cor) {
    if (numero < 0) numero = 0;
    std::string texto = std::to_string(numero);
    float cursorX = x;
    for (char c : texto) {
        desenharDigitoHUD(shaderHUD, vao, c - '0', cursorX, y, W, H, esp, cor);
        cursorX += W + esp * 2.2f;
    }
}

// ------------------------------------------------------------
// FONTE DE PIXEL 5x7 (para as telas de inicio/vitoria/derrota)
// ------------------------------------------------------------
const char* const* obterGlifo(char c) {
    static const char* A[7] = { ".XXX.","X...X","X...X","XXXXX","X...X","X...X","X...X" };
    static const char* P[7] = { "XXXX.","X...X","X...X","XXXX.","X....","X....","X...." };
    static const char* E[7] = { "XXXXX","X....","X....","XXXX.","X....","X....","XXXXX" };
    static const char* R[7] = { "XXXX.","X...X","X...X","XXXX.","X.X..","X..X.","X...X" };
    static const char* T[7] = { "XXXXX","..X..","..X..","..X..","..X..","..X..","..X.." };
    static const char* N[7] = { "X...X","XX..X","X.X.X","X..XX","X...X","X...X","X...X" };
    static const char* F[7] = { "XXXXX","X....","X....","XXXX.","X....","X....","X...." };
    static const char* S[7] = { ".XXXX","X....","X....",".XXX.","....X","....X","XXXX." };
    static const char* V[7] = { "X...X","X...X","X...X","X...X","X...X",".X.X.","..X.." };
    static const char* O[7] = { ".XXX.","X...X","X...X","X...X","X...X","X...X",".XXX." };
    static const char* C[7] = { ".XXXX","X....","X....","X....","X....","X....",".XXXX" };
    static const char* U[7] = { "X...X","X...X","X...X","X...X","X...X","X...X",".XXX." };
    static const char* M[7] = { "X...X","XX.XX","X.X.X","X...X","X...X","X...X","X...X" };
    static const char* G[7] = { ".XXXX","X....","X....","X.XXX","X...X","X...X",".XXX." };
    static const char* D[7] = { "XXXX.","X...X","X...X","X...X","X...X","X...X","XXXX." };
    static const char* B[7] = { "XXXX.","X...X","X...X","XXXX.","X...X","X...X","XXXX." };
    static const char* L[7] = { "X....","X....","X....","X....","X....","X....","XXXXX" };
    static const char* UM[7] = { "..X..",".XX..","..X..","..X..","..X..","..X..",".XXX." };
    static const char* DOIS[7] = { ".XXX.","X...X","....X","...X.","..X..",".X...","XXXXX" };
    static const char* TRES[7] = { ".XXX.","X...X","....X","..XX.","....X","X...X",".XXX." };
    static const char* ESPACO[7] = { ".....",".....",".....",".....",".....",".....","....." };

    switch (toupper(c)) {
    case 'A': return A; case 'P': return P; case 'E': return E; case 'R': return R;
    case 'T': return T; case 'N': return N; case 'F': return F; case 'S': return S;
    case 'V': return V; case 'O': return O; case 'C': return C; case 'U': return U;
    case 'M': return M; case 'G': return G; case 'D': return D; case 'B': return B;
    case 'L': return L; case '1': return UM; case '2': return DOIS; case '3': return TRES;
    default: return ESPACO;
    }
}

void desenharTextoHUD(unsigned int shaderHUD, unsigned int vao, const std::string& texto, float x, float y, float escala, glm::vec3 cor, float alpha = 1.0f) {
    float cursorX = x;
    for (char c : texto) {
        const char* const* glifo = obterGlifo(c);
        for (int linha = 0; linha < 7; ++linha) {
            const char* rowStr = glifo[linha];
            for (int col = 0; col < 5; ++col) {
                if (rowStr[col] == 'X') {
                    desenharRetanguloHUD(shaderHUD, vao, cursorX + col * escala, y + linha * escala, escala * 0.9f, escala * 0.9f, cor, alpha);
                }
            }
        }
        cursorX += 6.0f * escala;
    }
}

// ------------------------------------------------------------
// MATRIZ DE MODELO PARA UM SEGMENTO DE PISTA
// ------------------------------------------------------------
glm::mat4 matrizSegmento(const Segmento& seg, float espessura) {
    glm::vec3 meio = (seg.p0 + seg.p1) * 0.5f - seg.dirU * (espessura * 0.5f);
    glm::mat3 rot(seg.dirLat, seg.dirU, seg.dirF);
    glm::mat4 model = glm::mat4(rot);
    model[3] = glm::vec4(meio, 1.0f);
    model = model * glm::scale(glm::mat4(1.0f), glm::vec3(seg.largura, espessura, seg.comprimento));
    return model;
}

// ------------------------------------------------------------
// PROGRAMA PRINCIPAL
// ------------------------------------------------------------
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const int LARGURA = 1280, ALTURA = 720;
    GLFWwindow* window = glfwCreateWindow(LARGURA, ALTURA, "Marble Run 3D", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    unsigned int shaderPrincipal = criarPrograma(vertexShaderSource, fragmentShaderSource);
    unsigned int shaderProfundidade = criarPrograma(depthVertexShaderSource, depthFragmentShaderSource);
    unsigned int shaderHUD = criarPrograma(hudVertexShaderSource, hudFragmentShaderSource);
    unsigned int shaderEstrelas = criarPrograma(starVertexShaderSource, starFragmentShaderSource);

    unsigned int esferaVAO, esferaIndices;
    criarEsfera(esferaVAO, esferaIndices, 1.0f, 32, 16);

    unsigned int cuboVAO, cuboIndices;
    criarCubo(cuboVAO, cuboIndices);

    unsigned int texPista = criarTexturaQuadriculada(256, 10, glm::vec3(0.85f, 0.9f, 0.95f), glm::vec3(0.15f, 0.2f, 0.3f));
    unsigned int texGelo = criarTexturaQuadriculada(256, 10, glm::vec3(0.75f, 0.9f, 1.0f), glm::vec3(0.55f, 0.75f, 0.9f));
    unsigned int texMeta = criarTexturaQuadriculada(256, 6, glm::vec3(1.0f, 0.85f, 0.2f), glm::vec3(1.0f, 1.0f, 1.0f));
    unsigned int texBola = criarTexturaQuadriculada(256, 8, glm::vec3(0.85f, 0.08f, 0.08f), glm::vec3(0.95f, 0.95f, 0.95f));
    unsigned int texPilar = criarTexturaListrada(256, 8, glm::vec3(0.10f, 0.35f, 0.65f), glm::vec3(0.05f, 0.15f, 0.30f));

    std::vector<Segmento> pista = criarPista();
    std::vector<Inimigo> inimigos = criarInimigos();
    std::vector<Pilar> pilares = criarPilares();
    EstrelasInfo estrelas = criarEstrelas(1000, glm::vec3(0.0f, 15.0f, 40.0f), 120.0f, 200.0f);

    reiniciarJogo();

    // Quad unitario para o HUD
    float quadVerts[] = { 0,0, 1,0, 1,1, 0,1 };
    unsigned int quadIdx[] = { 0,1,2, 2,3,0 };
    unsigned int hudVAO, hudVBO, hudEBO;
    glGenVertexArrays(1, &hudVAO);
    glGenBuffers(1, &hudVBO);
    glGenBuffers(1, &hudEBO);
    glBindVertexArray(hudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hudEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Mapa de sombras
    const unsigned int SHADOW_TAM = 1024;
    unsigned int depthMapFBO;
    glGenFramebuffers(1, &depthMapFBO);
    unsigned int depthMap;
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_TAM, SHADOW_TAM, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float corBorda[] = { 1,1,1,1 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, corBorda);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "Controles: seta CIMA = acelera | seta BAIXO = freia/reverte | ENTER = confirmar\n";
    std::cout << "Arraste o mouse = angulo da camera | Scroll = zoom\n\n";

    float lastFrame = 0.0f;
    bool enterEstavaPressionado = false;

    auto desenharCena = [&](unsigned int shaderAtivo, float tempoJogo) {
        glBindVertexArray(cuboVAO);
        for (const auto& seg : pista) {
            glm::mat4 model = matrizSegmento(seg, 0.5f);
            glUniformMatrix4fv(glGetUniformLocation(shaderAtivo, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform1i(glGetUniformLocation(shaderAtivo, "useTexture"), 1);
            glUniform2f(glGetUniformLocation(shaderAtivo, "uvScale"), seg.largura * 0.6f, seg.comprimento * 0.6f);
            glActiveTexture(GL_TEXTURE0);
            if (seg.meta) glBindTexture(GL_TEXTURE_2D, texMeta);
            else if (seg.escorregadio) glBindTexture(GL_TEXTURE_2D, texGelo);
            else glBindTexture(GL_TEXTURE_2D, texPista);
            glDrawElements(GL_TRIANGLES, cuboIndices, GL_UNSIGNED_INT, 0);
        }

        for (const auto& pilar : pilares) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pilar.pos);
            model = glm::scale(model, pilar.escala);
            glUniformMatrix4fv(glGetUniformLocation(shaderAtivo, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform1i(glGetUniformLocation(shaderAtivo, "useTexture"), 1);
            glUniform2f(glGetUniformLocation(shaderAtivo, "uvScale"), 1.0f, pilar.escala.y * 0.3f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texPilar);
            glDrawElements(GL_TRIANGLES, cuboIndices, GL_UNSIGNED_INT, 0);
        }

        glBindVertexArray(esferaVAO);
        for (const auto& inimigo : inimigos) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), posicaoInimigo(inimigo, tempoJogo));
            model = glm::scale(model, glm::vec3(inimigo.raio));
            glUniformMatrix4fv(glGetUniformLocation(shaderAtivo, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform1i(glGetUniformLocation(shaderAtivo, "useTexture"), 0);
            glUniform2f(glGetUniformLocation(shaderAtivo, "uvScale"), 1.0f, 1.0f);
            glUniform3fv(glGetUniformLocation(shaderAtivo, "fallbackColor"), 1, glm::value_ptr(glm::vec3(0.03f, 0.03f, 0.03f)));
            glDrawElements(GL_TRIANGLES, esferaIndices, GL_UNSIGNED_INT, 0);
        }

        glm::mat4 modelBola = glm::translate(glm::mat4(1.0f), posBola);
        modelBola = modelBola * glm::mat4_cast(rotacaoBola);
        modelBola = glm::scale(modelBola, glm::vec3(RAIO_BOLA));
        glUniformMatrix4fv(glGetUniformLocation(shaderAtivo, "model"), 1, GL_FALSE, glm::value_ptr(modelBola));
        glUniform1i(glGetUniformLocation(shaderAtivo, "useTexture"), 1);
        glUniform2f(glGetUniformLocation(shaderAtivo, "uvScale"), 1.0f, 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texBola);
        glDrawElements(GL_TRIANGLES, esferaIndices, GL_UNSIGNED_INT, 0);
        };

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        if (deltaTime > 0.05f) deltaTime = 0.05f;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);

        bool enterAgora = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
        bool enterClicado = enterAgora && !enterEstavaPressionado;
        enterEstavaPressionado = enterAgora;

        if (estado == EstadoJogo::MENU) {
            cameraYawOffset += deltaTime * 8.0f; // giro suave de vitrine no menu
            if (enterClicado) { reiniciarJogo(); estado = EstadoJogo::JOGANDO; }
        }
        else if (estado == EstadoJogo::JOGANDO) {
            glm::vec3 posAntesFrame = posBola;
            atualizarFisica(window, deltaTime, pista, inimigos, currentFrame);

            glm::vec3 deslocamento = posBola - posAntesFrame;
            float dist = glm::length(deslocamento);
            if (dist > 0.00001f) {
                glm::vec3 dir = glm::normalize(deslocamento);
                glm::vec3 eixo = glm::cross(glm::vec3(0, 1, 0), dir);
                if (glm::length(eixo) > 0.0001f) {
                    eixo = glm::normalize(eixo);
                    float angulo = dist / RAIO_BOLA;
                    rotacaoBola = glm::angleAxis(angulo, eixo) * rotacaoBola;
                }
            }

            if (vitoria) estado = EstadoJogo::VITORIA;
            else if (fimDeJogo) estado = EstadoJogo::DERROTA;
        }
        else { // VITORIA ou DERROTA
            if (enterClicado) estado = EstadoJogo::MENU;
        }

        // Luz segue a bola
        glm::vec3 lightPos = posBola + glm::vec3(10.0f, 16.0f, -8.0f);
        glm::mat4 lightProjection = glm::ortho(-18.0f, 18.0f, -18.0f, 18.0f, 1.0f, 50.0f);
        glm::mat4 lightView = glm::lookAt(lightPos, posBola, glm::vec3(0, 1, 0));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        // ===== PASSE 1: sombra =====
        glViewport(0, 0, SHADOW_TAM, SHADOW_TAM);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(shaderProfundidade);
        glUniformMatrix4fv(glGetUniformLocation(shaderProfundidade, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        desenharCena(shaderProfundidade, currentFrame);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // ===== PASSE 2: cena principal =====
        glViewport(0, 0, LARGURA, ALTURA);
        glClearColor(0.01f, 0.01f, 0.03f, 1.0f); // fundo bem escuro (espaco sideral)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float baseYaw = glm::degrees(atan2f(direcaoAtual.x, direcaoAtual.z));
        float yawFinal = baseYaw + cameraYawOffset;
        float yawRad = glm::radians(yawFinal);
        float pitchRad = glm::radians(cameraPitch);

        glm::vec3 offset;
        offset.x = cameraDistance * cosf(pitchRad) * sinf(yawRad);
        offset.y = cameraDistance * sinf(pitchRad);
        offset.z = cameraDistance * cosf(pitchRad) * cosf(yawRad);

        glm::vec3 alvo = posBola + glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 posCamera = alvo + offset;

        glm::mat4 projection = glm::perspective(glm::radians(58.0f), (float)LARGURA / (float)ALTURA, 0.1f, 300.0f);
        glm::mat4 view = glm::lookAt(posCamera, alvo, glm::vec3(0, 1, 0));

        // Estrelas (desenhadas primeiro, ficam atras de tudo pelo teste de profundidade)
        glUseProgram(shaderEstrelas);
        glUniformMatrix4fv(glGetUniformLocation(shaderEstrelas, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(shaderEstrelas, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(estrelas.VAO);
        glDrawArrays(GL_POINTS, 0, estrelas.quantidade);
        glDisable(GL_BLEND);

        glUseProgram(shaderPrincipal);
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        glUniform3fv(glGetUniformLocation(shaderPrincipal, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(shaderPrincipal, "viewPos"), 1, glm::value_ptr(posCamera));
        glUniform1i(glGetUniformLocation(shaderPrincipal, "albedoMap"), 0);
        glUniform1i(glGetUniformLocation(shaderPrincipal, "shadowMap"), 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthMap);

        desenharCena(shaderPrincipal, currentFrame);

        // ===== HUD =====
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(shaderHUD);
        glm::mat4 projecaoHUD = glm::ortho(0.0f, (float)LARGURA, (float)ALTURA, 0.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderHUD, "projecaoHUD"), 1, GL_FALSE, glm::value_ptr(projecaoHUD));

        if (estado == EstadoJogo::MENU) {
            desenharRetanguloHUD(shaderHUD, hudVAO, 0, 0, (float)LARGURA, (float)ALTURA, glm::vec3(0.0f), 0.35f);
            desenharTextoHUD(shaderHUD, hudVAO, "MARBLE RUN", (float)LARGURA / 2.0f - 360.0f, 220.0f, 12.0f, glm::vec3(1.0f, 1.0f, 1.0f));
            float pisca = (sinf(currentFrame * 4.0f) + 1.0f) * 0.5f;
            desenharTextoHUD(shaderHUD, hudVAO, "APERTE ENTER PARA COMECAR", (float)LARGURA / 2.0f - 468.0f, 400.0f, 6.0f, glm::vec3(1.0f, 0.9f, 0.3f), 0.4f + 0.6f * pisca);
        }
        else if (estado == EstadoJogo::JOGANDO) {
            desenharTextoHUD(shaderHUD, hudVAO, "FASE " + std::to_string(faseAtual), (float)LARGURA - 230.0f, 15.0f, 6.0f, glm::vec3(1.0f, 1.0f, 1.0f));
            glm::vec3 corRelogio = (tempoRestante < 6.0f) ? glm::vec3(1.0f, 0.15f + 0.5f * fabsf(sinf(currentFrame * 8.0f)), 0.15f) : glm::vec3(1.0f, 1.0f, 1.0f);
            desenharNumeroHUD(shaderHUD, hudVAO, (int)ceilf(tempoRestante), (float)LARGURA - 145.0f, 65.0f, 34.0f, 54.0f, 8.0f, corRelogio);
        }
        else if (estado == EstadoJogo::VITORIA) {
            desenharRetanguloHUD(shaderHUD, hudVAO, 0, 0, (float)LARGURA, (float)ALTURA, glm::vec3(0.1f, 0.6f, 0.2f), 0.35f);
            desenharTextoHUD(shaderHUD, hudVAO, "VOCE VENCEU", (float)LARGURA / 2.0f - 400.0f, 260.0f, 11.0f, glm::vec3(0.6f, 1.0f, 0.6f));
            float pisca = (sinf(currentFrame * 4.0f) + 1.0f) * 0.5f;
            desenharTextoHUD(shaderHUD, hudVAO, "APERTE ENTER", (float)LARGURA / 2.0f - 216.0f, 420.0f, 6.0f, glm::vec3(1.0f, 1.0f, 1.0f), 0.4f + 0.6f * pisca);
        }
        else if (estado == EstadoJogo::DERROTA) {
            desenharRetanguloHUD(shaderHUD, hudVAO, 0, 0, (float)LARGURA, (float)ALTURA, glm::vec3(0.6f, 0.05f, 0.05f), 0.35f);
            desenharTextoHUD(shaderHUD, hudVAO, "TEMPO ESGOTADO", (float)LARGURA / 2.0f - 504.0f, 260.0f, 9.0f, glm::vec3(1.0f, 0.6f, 0.6f));
            float pisca = (sinf(currentFrame * 4.0f) + 1.0f) * 0.5f;
            desenharTextoHUD(shaderHUD, hudVAO, "APERTE ENTER", (float)LARGURA / 2.0f - 216.0f, 420.0f, 6.0f, glm::vec3(1.0f, 1.0f, 1.0f), 0.4f + 0.6f * pisca);
        }

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}