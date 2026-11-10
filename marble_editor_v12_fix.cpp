#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <GLM/glm/glm.hpp>
#include <GLM/glm/gtc/matrix_transform.hpp>
#include <GLM/glm/gtc/type_ptr.hpp>
#include <GLM/glm/gtc/quaternion.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>

// ============================================================
// MARBLE TIME RUSH -- EDITOR COM MOUSE
// Base: exatamente o v4 que o usuario confirmou que funciona bem.
// Duas correcoes cirurgicas nesta versao:
//   1) A janela de CONSOLE (terminal preto) e escondida ao iniciar.
//      Ela sempre existiu por baixo/por cima da janela do jogo (o
//      Windows cria as duas porque o projeto e "aplicacao de console"),
//      e por isso parecia que tempo/pontuacao/instrucoes so apareciam
//      "no terminal" -- na verdade sempre estiveram desenhados dentro
//      da janela OpenGL, so que escondidos atras do terminal.
//   2) Sistema de 3 fases de verdade, com progressao ao tocar o trofeu.
// Nada mais foi alterado.
// ============================================================

// ------------------------------------------------------------
// SHADER PRINCIPAL (Blinn-Phong + textura + sombra + emissivo)
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
uniform bool emissivo;

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

    if (emissivo) {
        FragColor = vec4(cor, 1.0);
        return;
    }

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    vec3 ambiente = 0.48 * cor;
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 difusa = diff * cor;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
    vec3 especular = 0.35 * spec * vec3(1.0);

    float sombra = calcularSombra(FragPosLightSpace, norm, lightDir);
    vec3 corFinal = ambiente + (1.0 - sombra) * (difusa + especular);

    FragColor = vec4(corFinal, 1.0);
}
)glsl";

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
uniform vec4 corHUD;
void main() { FragColor = corHUD; }
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
// GEOMETRIA: RAMPA (cunha/prisma triangular) -- alta em z=-0.5,
// baixa (altura zero) em z=+0.5. Gire com R (eixo Y) pra apontar
// a subida pra onde precisar.
// ------------------------------------------------------------
void criarRampa(unsigned int& VAO, unsigned int& indexCount) {
    const float S = 0.70710678f; // 1/sqrt(2), componente da normal inclinada
    float v[] = {
        // fundo (normal 0,-1,0)
        -0.5f,-0.5f,-0.5f,   0,-1,0,  0,0,
         0.5f,-0.5f,-0.5f,   0,-1,0,  1,0,
         0.5f,-0.5f, 0.5f,   0,-1,0,  1,1,
        -0.5f,-0.5f, 0.5f,   0,-1,0,  0,1,
        // face traseira vertical, altura total (normal 0,0,-1)
         0.5f,-0.5f,-0.5f,   0,0,-1,  0,0,
        -0.5f,-0.5f,-0.5f,   0,0,-1,  1,0,
        -0.5f, 0.5f,-0.5f,   0,0,-1,  1,1,
         0.5f, 0.5f,-0.5f,   0,0,-1,  0,1,
        // rampa inclinada (superficie caminhavel), normal (0,S,S)
        -0.5f, 0.5f,-0.5f,   0,S,S,   0,0,
        -0.5f,-0.5f, 0.5f,   0,S,S,   0,1,
         0.5f,-0.5f, 0.5f,   0,S,S,   1,1,
         0.5f, 0.5f,-0.5f,   0,S,S,   1,0,
        // face triangular esquerda (normal -1,0,0)
        -0.5f,-0.5f,-0.5f,  -1,0,0,   0,0,
        -0.5f,-0.5f, 0.5f,  -1,0,0,   1,0,
        -0.5f, 0.5f,-0.5f,  -1,0,0,   0,1,
        // face triangular direita (normal 1,0,0)
         0.5f,-0.5f,-0.5f,   1,0,0,   0,0,
         0.5f, 0.5f,-0.5f,   1,0,0,   0,1,
         0.5f,-0.5f, 0.5f,   1,0,0,   1,0,
    };
    unsigned int idx[] = {
        0,1,2, 2,3,0,        // fundo
        4,5,6, 6,7,4,        // traseira
        8,9,10, 10,11,8,     // rampa
        12,13,14,            // esquerda
        15,16,17             // direita
    };
    indexCount = 24;

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
// GEOMETRIA: CILINDRO (pilar/coluna -- usado tambem pra sugerir
// tuneis quando enfileirado; a colisao usa a caixa envolvente,
// entao nao e um tunel "oco" de verdade, e um pilar solido).
// ------------------------------------------------------------
void criarCilindro(unsigned int& VAO, unsigned int& indexCount, unsigned int segmentos = 20) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    const float PI = 3.14159265359f;
    const float raio = 0.5f, meiaAltura = 0.5f;

    // Parede lateral: um anel de baixo + um anel de cima por segmento,
    // cada vertice com normal radial (aponta pra fora do eixo).
    for (unsigned int i = 0; i <= segmentos; ++i) {
        float ang = (float)i / segmentos * 2.0f * PI;
        float cx = cosf(ang) * raio, cz = sinf(ang) * raio;
        glm::vec3 normal = glm::normalize(glm::vec3(cx, 0.0f, cz));
        vertices.insert(vertices.end(), { cx, -meiaAltura, cz, normal.x, normal.y, normal.z, (float)i / segmentos, 0.0f });
        vertices.insert(vertices.end(), { cx,  meiaAltura, cz, normal.x, normal.y, normal.z, (float)i / segmentos, 1.0f });
    }
    for (unsigned int i = 0; i < segmentos; ++i) {
        unsigned int b0 = i * 2, t0 = b0 + 1, b1 = (i + 1) * 2, t1 = b1 + 1;
        indices.push_back(b0); indices.push_back(t1); indices.push_back(b1);
        indices.push_back(b0); indices.push_back(t0); indices.push_back(t1);
    }

    // Tampa de cima (leque), normal (0,1,0)
    unsigned int baseTopo = (unsigned int)(vertices.size() / 8);
    vertices.insert(vertices.end(), { 0.0f, meiaAltura, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f });
    for (unsigned int i = 0; i <= segmentos; ++i) {
        float ang = (float)i / segmentos * 2.0f * PI;
        float cx = cosf(ang) * raio, cz = sinf(ang) * raio;
        vertices.insert(vertices.end(), { cx, meiaAltura, cz, 0.0f, 1.0f, 0.0f, 0.5f + cosf(ang) * 0.5f, 0.5f + sinf(ang) * 0.5f });
    }
    for (unsigned int i = 0; i < segmentos; ++i) {
        indices.push_back(baseTopo);
        indices.push_back(baseTopo + 1 + i + 1);
        indices.push_back(baseTopo + 1 + i);
    }

    // Tampa de baixo (leque), normal (0,-1,0)
    unsigned int baseFundo = (unsigned int)(vertices.size() / 8);
    vertices.insert(vertices.end(), { 0.0f, -meiaAltura, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f });
    for (unsigned int i = 0; i <= segmentos; ++i) {
        float ang = (float)i / segmentos * 2.0f * PI;
        float cx = cosf(ang) * raio, cz = sinf(ang) * raio;
        vertices.insert(vertices.end(), { cx, -meiaAltura, cz, 0.0f, -1.0f, 0.0f, 0.5f + cosf(ang) * 0.5f, 0.5f + sinf(ang) * 0.5f });
    }
    for (unsigned int i = 0; i < segmentos; ++i) {
        indices.push_back(baseFundo);
        indices.push_back(baseFundo + 1 + i);
        indices.push_back(baseFundo + 1 + i + 1);
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

// Grade estilo "editor 3D": fundo verde-azulado escuro com linhas mais claras
unsigned int criarTexturaGrade(int tamanho, int celulas, glm::vec3 corFundo, glm::vec3 corLinha) {
    std::vector<unsigned char> dados(tamanho * tamanho * 3);
    int passo = tamanho / celulas;
    int espessura = glm::max(1, passo / 16);
    for (int y = 0; y < tamanho; ++y)
        for (int x = 0; x < tamanho; ++x) {
            bool linha = (x % passo) < espessura || (y % passo) < espessura;
            glm::vec3 cor = linha ? corLinha : corFundo;
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

unsigned int criarTexturaCannonbolt(int tamanho) {
    std::vector<unsigned char> dados(tamanho * tamanho * 3);
    glm::vec3 amarelo(1.0f, 0.82f, 0.0f);
    glm::vec3 preto(0.03f, 0.03f, 0.03f);

    const float PI = 3.14159265359f;
    const int NUM_LISTRAS = 4;
    const float LARGURA_LISTRA = 0.075f;
    const float AMPLITUDE_ONDA = 0.05f;
    const float FREQ_ONDA = 2.0f;
    const float RAIO_CALOTA = 0.20f;
    const float RAIO_CENTRO_CALOTA = 0.09f;

    for (int y = 0; y < tamanho; ++y) {
        float v = (float)y / (float)(tamanho - 1);
        for (int x = 0; x < tamanho; ++x) {
            float u = (float)x / (float)(tamanho - 1);
            glm::vec3 cor = amarelo;

            float distPoloNorte = v;
            float distPoloSul = 1.0f - v;
            float distPolo = (distPoloNorte < distPoloSul) ? distPoloNorte : distPoloSul;

            if (distPolo < RAIO_CALOTA) {
                cor = (distPolo < RAIO_CENTRO_CALOTA) ? amarelo : preto;
            }
            else {
                for (int i = 0; i < NUM_LISTRAS; ++i) {
                    float u0 = (float)i / (float)NUM_LISTRAS;
                    float onda = AMPLITUDE_ONDA * sinf(v * FREQ_ONDA * 2.0f * PI);
                    float centroListra = u0 + onda;
                    float d = fabsf(u - centroListra);
                    d = (d < 1.0f - d) ? d : 1.0f - d;
                    if (d < LARGURA_LISTRA) { cor = preto; break; }
                }
            }

            int idx = (y * tamanho + x) * 3;
            dados[idx + 0] = (unsigned char)(cor.r * 255.0f);
            dados[idx + 1] = (unsigned char)(cor.g * 255.0f);
            dados[idx + 2] = (unsigned char)(cor.b * 255.0f);
        }
    }

    unsigned int texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tamanho, tamanho, 0, GL_RGB, GL_UNSIGNED_BYTE, dados.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texID;
}

// ------------------------------------------------------------
// GERADOR PSEUDO-ALEATORIO DETERMINISTICO
// ------------------------------------------------------------
float pseudoAleatorio(int semente) {
    float x = sinf((float)semente * 12.9898f) * 43758.5453f;
    return x - floorf(x);
}

// ------------------------------------------------------------
// CENARIO DE FUNDO (decorativo): arvores e nuvens (so no modo Jogo)
// ------------------------------------------------------------
struct Arvore { glm::vec3 pos; float alturaTronco; float raioFolhagem; int numFolhas; glm::vec3 corFolhagem; };

std::vector<Arvore> criarArvores() {
    std::vector<Arvore> arvores;
    glm::vec3 paletaFolhagem[4] = {
        glm::vec3(0.20f, 0.45f, 0.20f),
        glm::vec3(0.30f, 0.58f, 0.25f),
        glm::vec3(0.48f, 0.60f, 0.20f),
        glm::vec3(0.25f, 0.50f, 0.38f),
    };
    for (int lado = -1; lado <= 1; lado += 2) {
        for (int i = 0; i < 26; ++i) {
            float z = i * 4.2f - 15.0f;
            float distX = 9.0f + pseudoAleatorio(i * 7 + lado * 31) * 8.0f;
            float alturaTronco = 3.0f + pseudoAleatorio(i * 13 + lado * 17) * 2.8f;
            float raioFolhagem = 1.3f + pseudoAleatorio(i * 5 + lado * 11) * 1.3f;
            int numFolhas = 1 + (int)(pseudoAleatorio(i * 9 + lado * 3) * 2.99f);
            glm::vec3 cor = paletaFolhagem[(i + (lado > 0 ? 0 : 2)) % 4];
            arvores.push_back({ glm::vec3(lado * distX, 0.0f, z), alturaTronco, raioFolhagem, numFolhas, cor });
        }
    }
    return arvores;
}

struct Nuvem { glm::vec3 pos; glm::vec3 escala; };

std::vector<Nuvem> criarNuvens() {
    std::vector<Nuvem> nuvens;
    for (int i = 0; i < 14; ++i) {
        float z = pseudoAleatorio(i * 9 + 3) * 120.0f - 30.0f;
        float x = (pseudoAleatorio(i * 17 + 7) - 0.5f) * 80.0f;
        float y = 22.0f + pseudoAleatorio(i * 23 + 11) * 14.0f;
        float tam = 3.0f + pseudoAleatorio(i * 29 + 13) * 3.0f;
        nuvens.push_back({ glm::vec3(x, y, z), glm::vec3(tam * 1.8f, tam * 0.6f, tam) });
    }
    return nuvens;
}

// Pedras: aglomerados de cubos levemente girados e de tamanhos diferentes,
// espalhados perto da trilha, dando mais textura ao chao.
struct Rocha { glm::vec3 pos; glm::vec3 escala; float rotY; glm::vec3 cor; };

std::vector<Rocha> criarRochas() {
    std::vector<Rocha> rochas;
    glm::vec3 paletaPedra[3] = {
        glm::vec3(0.45f, 0.44f, 0.42f),
        glm::vec3(0.55f, 0.53f, 0.50f),
        glm::vec3(0.38f, 0.37f, 0.36f),
    };
    for (int lado = -1; lado <= 1; lado += 2) {
        for (int i = 0; i < 18; ++i) {
            float z = i * 6.0f - 12.0f;
            float distX = 6.5f + pseudoAleatorio(i * 11 + lado * 41) * 6.0f;
            float tam = 0.5f + pseudoAleatorio(i * 13 + lado * 19) * 0.9f;
            float rotY = pseudoAleatorio(i * 17 + lado * 23) * 360.0f;
            glm::vec3 cor = paletaPedra[i % 3];
            rochas.push_back({ glm::vec3(lado * distX, tam * 0.4f, z), glm::vec3(tam, tam * 0.7f, tam * 0.8f), rotY, cor });
        }
    }
    return rochas;
}

// Arbustos: aglomerados baixos de pequenas esferas verdes, distintos da
// folhagem das arvores (ficam rente ao chao).
struct Arbusto { glm::vec3 pos; float raio; glm::vec3 cor; };

std::vector<Arbusto> criarArbustos() {
    std::vector<Arbusto> arbustos;
    glm::vec3 paletaArbusto[3] = {
        glm::vec3(0.22f, 0.40f, 0.18f),
        glm::vec3(0.30f, 0.48f, 0.22f),
        glm::vec3(0.18f, 0.36f, 0.24f),
    };
    for (int lado = -1; lado <= 1; lado += 2) {
        for (int i = 0; i < 20; ++i) {
            float z = i * 5.5f - 14.0f;
            float distX = 5.0f + pseudoAleatorio(i * 31 + lado * 53) * 3.0f;
            float raio = 0.5f + pseudoAleatorio(i * 37 + lado * 59) * 0.5f;
            glm::vec3 cor = paletaArbusto[i % 3];
            arbustos.push_back({ glm::vec3(lado * distX, raio * 0.6f, z), raio, cor });
        }
    }
    return arbustos;
}

// ------------------------------------------------------------
// OBJETOS DO CENARIO
// ------------------------------------------------------------
enum class TipoObjeto {
    OBSTACULO_REPELIR = 0,
    OBSTACULO_SOLIDO = 1,
    OBSTACULO_MOVEL = 2,
    OBSTACULO_FALSO = 3,
    OBSTACULO_MORTE = 4,
    CHECKPOINT = 5,
    VITORIA = 6,
    MOEDA = 7,
    SPAWN = 8,
};

// Forma visual dos obstaculos tipo "caixa" (nao se aplica aos esfericos
// como moeda/checkpoint/vitoria/spawn, que continuam sempre esfera).
// A colisao de RAMPA e CILINDRO usa a mesma caixa orientada dos outros
// obstaculos (aproximacao) -- a RAMPA e caminhavel de verdade porque,
// se voce girar ela com R, a caixa de colisao gira junto e vira um
// plano inclinado de verdade (o motor de colisao ja suporta isso).
enum class FormaGeometrica { CUBO = 0, RAMPA = 1, CILINDRO = 2 };

struct ObjetoCenario {
    TipoObjeto tipo = TipoObjeto::OBSTACULO_SOLIDO;
    FormaGeometrica forma = FormaGeometrica::CUBO;
    glm::vec3 posicao = glm::vec3(0.0f);
    glm::vec3 escala = glm::vec3(2.0f, 2.0f, 2.0f);
    glm::vec3 rotacao = glm::vec3(0.0f); // angulos de Euler (graus) em X, Y, Z
    glm::vec3 eixoMovimento = glm::vec3(0.0f, 1.0f, 0.0f);
    float amplitudeMovimento = 1.5f;
    float velocidadeMovimento = 1.2f;
    bool coletada = false;
};

glm::vec3 corDoTipo(TipoObjeto t) {
    switch (t) {
    case TipoObjeto::OBSTACULO_REPELIR: return glm::vec3(0.95f, 0.35f, 0.10f);
    case TipoObjeto::OBSTACULO_SOLIDO:  return glm::vec3(0.75f, 0.35f, 0.15f); // tijolo (como na referencia)
    case TipoObjeto::OBSTACULO_MOVEL:   return glm::vec3(0.45f, 0.55f, 0.85f); // caixa azulada
    case TipoObjeto::OBSTACULO_FALSO:   return glm::vec3(0.70f, 0.55f, 0.85f); // roxo claro
    case TipoObjeto::OBSTACULO_MORTE:   return glm::vec3(0.90f, 0.35f, 0.10f); // lava
    case TipoObjeto::CHECKPOINT:        return glm::vec3(0.20f, 0.90f, 0.30f);
    case TipoObjeto::VITORIA:           return glm::vec3(1.00f, 0.85f, 0.20f);
    case TipoObjeto::MOEDA:             return glm::vec3(1.00f, 0.90f, 0.10f);
    case TipoObjeto::SPAWN:             return glm::vec3(0.20f, 1.00f, 0.60f);
    }
    return glm::vec3(1.0f);
}

bool ehEsfera(TipoObjeto t) {
    return t == TipoObjeto::CHECKPOINT || t == TipoObjeto::VITORIA || t == TipoObjeto::MOEDA || t == TipoObjeto::SPAWN;
}

float raioEsferaDoTipo(TipoObjeto t) {
    switch (t) {
    case TipoObjeto::VITORIA:    return 1.2f;
    case TipoObjeto::CHECKPOINT: return 0.7f;
    case TipoObjeto::MOEDA:      return 0.4f;
    case TipoObjeto::SPAWN:      return 0.5f;
    default: return 0.5f;
    }
}

const char* nomeDoTipo(TipoObjeto t) {
    switch (t) {
    case TipoObjeto::OBSTACULO_REPELIR: return "Obstaculo (Repelir)";
    case TipoObjeto::OBSTACULO_SOLIDO:  return "Parede (Solida)";
    case TipoObjeto::OBSTACULO_MOVEL:   return "Caixa Movel";
    case TipoObjeto::OBSTACULO_FALSO:   return "Parede Falsa";
    case TipoObjeto::OBSTACULO_MORTE:   return "Lava (Mata)";
    case TipoObjeto::CHECKPOINT:        return "Checkpoint";
    case TipoObjeto::VITORIA:           return "Trofeu (Vitoria)";
    case TipoObjeto::MOEDA:             return "Moeda";
    case TipoObjeto::SPAWN:             return "Spawn do avatar";
    }
    return "?";
}

// Rotulo curto (cabe no botao de ~108px de largura com a fonte 5x7).
const char* rotuloBotaoPaleta(int acao) {
    switch (acao) {
    case 0:  return "REPEL";
    case 1:  return "SOLIDA";
    case 2:  return "MOVEL";
    case 3:  return "FALSA";
    case 4:  return "LAVA";
    case 5:  return "CHECK";
    case 6:  return "META";
    case 7:  return "MOEDA";
    case 8:  return "SPAWN";
    case 9:  return "APAGAR";
    case 10: return "SALVAR";
    case 11: return "ABRIR";
    case 12: return "LIMPAR";
    case 14: return "EDITAR";
    default: return "";
    }
}

bool colideComObjeto(const ObjetoCenario& obj, const glm::vec3& posBolaMundo, float raioBola, glm::vec3& normalSaida, glm::vec3& pontoSaida) {
    glm::mat4 rot = glm::mat4(1.0f);
    rot = glm::rotate(rot, glm::radians(obj.rotacao.y), glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(obj.rotacao.x), glm::vec3(1, 0, 0));
    rot = glm::rotate(rot, glm::radians(obj.rotacao.z), glm::vec3(0, 0, 1));
    glm::mat4 invRot = glm::inverse(rot);

    glm::vec3 relMundo = posBolaMundo - obj.posicao;
    glm::vec3 relLocal = glm::vec3(invRot * glm::vec4(relMundo, 1.0f));

    glm::vec3 metade = obj.escala * 0.5f;
    glm::vec3 pontoLocal = glm::clamp(relLocal, -metade, metade);

    glm::vec3 diffLocal = relLocal - pontoLocal;
    float distSq = glm::dot(diffLocal, diffLocal);
    if (distSq > raioBola * raioBola) return false;

    glm::vec3 pontoMundo = obj.posicao + glm::vec3(rot * glm::vec4(pontoLocal, 1.0f));
    float dist = sqrtf(distSq);
    glm::vec3 normalMundo;
    if (dist > 0.0001f) {
        glm::vec3 normalLocal = diffLocal / dist;
        normalMundo = glm::normalize(glm::vec3(rot * glm::vec4(normalLocal, 0.0f)));
    }
    else {
        normalMundo = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    normalSaida = normalMundo;
    pontoSaida = pontoMundo;
    return true;
}

// ------------------------------------------------------------
// CONFIGURACAO GLOBAL (sol e tipo de vitoria -- compartilhados
// entre as 3 fases; tempo e spawn agora sao POR FASE, ver struct Fase)
// ------------------------------------------------------------
enum class TipoVitoria { POR_OBJETO_OU_TEMPO, POR_PONTOS };

struct ConfiguracaoCenario {
    float velocidadeSol = 0.05f;
    glm::vec3 posicaoInicialSol = glm::vec3(-30.0f, 45.0f, -35.0f);
    TipoVitoria tipoVitoria = TipoVitoria::POR_OBJETO_OU_TEMPO;
    float pontuacaoParaVencer = 50.0f;
};

const float RAIO_ORBITA_SOL = 55.0f;
const float ALTURA_SOL = 45.0f;

float anguloSolInicial = 210.0f; // graus -- controla cfg.posicaoInicialSol via slider

void atualizarPosicaoInicialSolPorAngulo(ConfiguracaoCenario& cfg) {
    float rad = glm::radians(anguloSolInicial);
    cfg.posicaoInicialSol = glm::vec3(RAIO_ORBITA_SOL * cosf(rad), ALTURA_SOL, RAIO_ORBITA_SOL * sinf(rad));
}

glm::vec3 calcularPosicaoSol(const ConfiguracaoCenario& cfg, float tempo) {
    glm::vec3 pivot(0.0f, 0.0f, 0.0f);
    glm::vec3 rel = cfg.posicaoInicialSol - pivot;
    float raio = glm::length(glm::vec2(rel.x, rel.z));
    float anguloInicial = atan2f(rel.z, rel.x);
    float anguloAtual = anguloInicial + tempo * cfg.velocidadeSol;
    glm::vec3 pos;
    pos.x = raio * cosf(anguloAtual);
    pos.z = raio * sinf(anguloAtual);
    pos.y = cfg.posicaoInicialSol.y;
    return pos;
}

// ------------------------------------------------------------
// FASES -- cada fase e um cenario independente e completo
// ------------------------------------------------------------
struct Fase {
    std::vector<ObjetoCenario> objetos;
    float tempoLimite = 45.0f;
    glm::vec3 spawn = glm::vec3(0.0f, 1.2f, 0.0f);
};

const int NUM_FASES_INICIAL = 3;

std::vector<ObjetoCenario> criarFase1() {
    std::vector<ObjetoCenario> objs;
    auto addPiso = [&](glm::vec3 pos, glm::vec3 escala) {
        ObjetoCenario o; o.tipo = TipoObjeto::OBSTACULO_SOLIDO; o.posicao = pos; o.escala = escala;
        objs.push_back(o);
        };
    auto addMoeda = [&](glm::vec3 pos) {
        ObjetoCenario o; o.tipo = TipoObjeto::MOEDA; o.posicao = pos;
        objs.push_back(o);
        };
    auto addCheckpoint = [&](glm::vec3 pos) {
        ObjetoCenario o; o.tipo = TipoObjeto::CHECKPOINT; o.posicao = pos;
        objs.push_back(o);
        };

    addPiso({ 0.0f, 0.0f, 0.0f }, { 5.0f, 0.6f, 5.0f });
    addPiso({ 0.0f, 0.0f, 7.0f }, { 5.0f, 0.6f, 4.0f });
    // buraco proposital entre z=9 e z=14 -- exige pulo
    addPiso({ 0.0f, 0.0f, 16.0f }, { 5.0f, 0.6f, 4.0f });
    addMoeda({ 1.5f, 1.2f, 7.0f });
    addMoeda({ -1.5f, 1.2f, 7.0f });
    addCheckpoint({ 0.0f, 1.0f, 18.0f });
    addPiso({ 0.0f, 0.0f, 24.0f }, { 4.0f, 0.6f, 4.0f });
    addMoeda({ 0.0f, 1.2f, 24.0f });
    // segundo buraco, um pouco maior, entre z=26 e z=30
    addPiso({ 0.0f, 0.0f, 32.0f }, { 4.0f, 0.6f, 4.0f });

    ObjetoCenario movel;
    movel.tipo = TipoObjeto::OBSTACULO_MOVEL;
    movel.posicao = glm::vec3(0.0f, 0.0f, 38.0f);
    movel.escala = glm::vec3(3.0f, 0.6f, 3.0f);
    movel.eixoMovimento = glm::vec3(1.0f, 0.0f, 0.0f);
    movel.amplitudeMovimento = 2.0f;
    movel.velocidadeMovimento = 1.0f;
    objs.push_back(movel);

    addPiso({ 0.0f, 0.0f, 44.0f }, { 5.0f, 0.6f, 5.0f });
    addCheckpoint({ 0.0f, 1.0f, 44.0f });
    addMoeda({ 1.5f, 1.2f, 44.0f });
    addMoeda({ -1.5f, 1.2f, 44.0f });

    ObjetoCenario vit;
    vit.tipo = TipoObjeto::VITORIA;
    vit.posicao = glm::vec3(0.0f, 1.4f, 44.0f);
    objs.push_back(vit);

    return objs;
}

std::vector<ObjetoCenario> criarFase2() {
    std::vector<ObjetoCenario> objs;
    auto addPiso = [&](glm::vec3 pos, glm::vec3 escala) {
        ObjetoCenario o; o.tipo = TipoObjeto::OBSTACULO_SOLIDO; o.posicao = pos; o.escala = escala;
        objs.push_back(o);
        };
    auto addMoeda = [&](glm::vec3 pos) {
        ObjetoCenario o; o.tipo = TipoObjeto::MOEDA; o.posicao = pos;
        objs.push_back(o);
        };
    auto addCheckpoint = [&](glm::vec3 pos) {
        ObjetoCenario o; o.tipo = TipoObjeto::CHECKPOINT; o.posicao = pos;
        objs.push_back(o);
        };

    addPiso({ 0.0f, 0.0f, 0.0f }, { 5.0f, 0.6f, 5.0f });

    for (int i = 0; i < 6; ++i) {
        addPiso({ 0.0f, 0.2f + i * 0.4f, 5.0f + i * 1.8f }, { 3.0f, 0.4f, 2.0f });
    }
    addPiso({ 0.0f, 2.3f, 17.0f }, { 5.0f, 0.6f, 5.0f });
    addCheckpoint({ 0.0f, 3.4f, 17.0f });
    addMoeda({ 1.5f, 3.4f, 17.0f });
    addMoeda({ -1.5f, 3.4f, 17.0f });

    ObjetoCenario movel;
    movel.tipo = TipoObjeto::OBSTACULO_MOVEL;
    movel.posicao = glm::vec3(0.0f, 2.3f, 24.0f);
    movel.escala = glm::vec3(3.0f, 0.6f, 3.0f);
    movel.eixoMovimento = glm::vec3(1.0f, 0.0f, 0.0f);
    movel.amplitudeMovimento = 2.5f;
    movel.velocidadeMovimento = 1.2f;
    objs.push_back(movel);

    addPiso({ 0.0f, 2.3f, 30.0f }, { 3.0f, 0.6f, 3.0f });

    ObjetoCenario repelir;
    repelir.tipo = TipoObjeto::OBSTACULO_REPELIR;
    repelir.posicao = glm::vec3(0.0f, 2.6f, 34.5f);
    repelir.escala = glm::vec3(2.0f, 0.6f, 0.6f);
    objs.push_back(repelir);

    addPiso({ 0.5f, 2.3f, 39.0f }, { 2.0f, 0.6f, 2.0f });
    addPiso({ -0.4f, 2.3f, 44.0f }, { 2.0f, 0.6f, 2.0f });
    addMoeda({ -0.4f, 3.4f, 44.0f });

    addPiso({ 0.0f, 2.3f, 50.0f }, { 5.0f, 0.6f, 5.0f });
    addMoeda({ 1.5f, 3.4f, 50.0f });
    addMoeda({ -1.5f, 3.4f, 50.0f });

    ObjetoCenario vit;
    vit.tipo = TipoObjeto::VITORIA;
    vit.posicao = glm::vec3(0.0f, 3.8f, 50.0f);
    objs.push_back(vit);

    return objs;
}

// Fase 3 -- exatamente o cenario original do v4 (criarCenarioPadrao),
// reaproveitado como a fase final, mais longa e completa.
std::vector<ObjetoCenario> criarFase3() {
    std::vector<ObjetoCenario> objs;

    auto addPiso = [&](glm::vec3 pos, glm::vec3 escala) {
        ObjetoCenario o; o.tipo = TipoObjeto::OBSTACULO_SOLIDO; o.posicao = pos; o.escala = escala;
        objs.push_back(o);
        };
    auto addMoeda = [&](glm::vec3 pos) {
        ObjetoCenario o; o.tipo = TipoObjeto::MOEDA; o.posicao = pos;
        objs.push_back(o);
        };
    auto addCheckpoint = [&](glm::vec3 pos) {
        ObjetoCenario o; o.tipo = TipoObjeto::CHECKPOINT; o.posicao = pos;
        objs.push_back(o);
        };

    // ---- Trecho 1: reta inicial com um buraco ----
    addPiso({ 0.0f, 0.0f, 0.0f }, { 5.0f, 0.6f, 5.0f });
    addPiso({ 0.0f, 0.0f, 7.0f }, { 5.0f, 0.6f, 4.0f });
    // buraco proposital entre z=9 e z=14
    addPiso({ 0.0f, 0.0f, 16.0f }, { 5.0f, 0.6f, 4.0f });
    addMoeda({ 1.5f, 1.2f, 7.0f });
    addMoeda({ -1.5f, 1.2f, 7.0f });
    addCheckpoint({ 0.0f, 1.0f, 18.0f });

    // ---- Escada subindo ----
    for (int i = 0; i < 6; ++i) {
        addPiso({ 0.0f, 0.2f + i * 0.4f, 21.0f + i * 1.8f }, { 3.0f, 0.4f, 2.0f });
    }
    addPiso({ 0.0f, 2.3f, 33.0f }, { 5.0f, 0.6f, 5.0f }); // patamar no topo

    // ---- Plataforma movel sobre outro buraco ----
    ObjetoCenario movel;
    movel.tipo = TipoObjeto::OBSTACULO_MOVEL;
    movel.posicao = glm::vec3(0.0f, 2.3f, 40.0f);
    movel.escala = glm::vec3(3.0f, 0.6f, 3.0f);
    movel.eixoMovimento = glm::vec3(1.0f, 0.0f, 0.0f);
    movel.amplitudeMovimento = 2.5f;
    movel.velocidadeMovimento = 1.2f;
    objs.push_back(movel);

    addPiso({ 0.0f, 2.3f, 47.0f }, { 5.0f, 0.6f, 4.0f });
    addCheckpoint({ 0.0f, 3.4f, 47.0f });
    addMoeda({ 0.0f, 3.4f, 44.0f });

    // ---- Trecho final: ilhas pequenas exigindo saltos de precisao ----
    addPiso({ 0.5f, 2.3f, 54.0f }, { 2.2f, 0.6f, 2.2f });
    addPiso({ -0.5f, 2.3f, 59.0f }, { 2.0f, 0.6f, 2.0f });
    addPiso({ 0.3f, 2.3f, 64.0f }, { 2.0f, 0.6f, 2.0f });

    addPiso({ 0.0f, 2.3f, 70.0f }, { 6.0f, 0.6f, 6.0f }); // plataforma final

    ObjetoCenario vit;
    vit.tipo = TipoObjeto::VITORIA;
    vit.posicao = glm::vec3(0.0f, 3.8f, 70.0f);
    objs.push_back(vit);

    return objs;
}

std::vector<Fase> criarFasesPadrao() {
    std::vector<Fase> fases(NUM_FASES_INICIAL);
    fases[0].objetos = criarFase1(); fases[0].tempoLimite = 40.0f; fases[0].spawn = glm::vec3(0.0f, 1.2f, 0.0f);
    fases[1].objetos = criarFase2(); fases[1].tempoLimite = 55.0f; fases[1].spawn = glm::vec3(0.0f, 1.2f, 0.0f);
    fases[2].objetos = criarFase3(); fases[2].tempoLimite = 90.0f; fases[2].spawn = glm::vec3(0.0f, 1.2f, 0.0f);
    return fases;
}

// ------------------------------------------------------------
// SALVAR / CARREGAR (um arquivo por fase)
// ------------------------------------------------------------
bool salvarFase(const std::string& caminho, const std::vector<ObjetoCenario>& objetos, float tempoLimite, const glm::vec3& spawn) {
    std::ofstream arq(caminho);
    if (!arq.is_open()) return false;

    arq << "CONFIG " << tempoLimite << " " << spawn.x << " " << spawn.y << " " << spawn.z << "\n";

    for (const auto& o : objetos) {
        arq << (int)o.tipo << " " << (int)o.forma << " "
            << o.posicao.x << " " << o.posicao.y << " " << o.posicao.z << " "
            << o.escala.x << " " << o.escala.y << " " << o.escala.z << " "
            << o.rotacao.x << " " << o.rotacao.y << " " << o.rotacao.z << " "
            << o.eixoMovimento.x << " " << o.eixoMovimento.y << " " << o.eixoMovimento.z << " "
            << o.amplitudeMovimento << " " << o.velocidadeMovimento << "\n";
    }
    arq.close();
    return true;
}

bool carregarFase(const std::string& caminho, std::vector<ObjetoCenario>& objetos, float& tempoLimite, glm::vec3& spawn) {
    std::ifstream arq(caminho);
    if (!arq.is_open()) return false;

    objetos.clear();
    std::string linha;
    while (std::getline(arq, linha)) {
        if (linha.empty()) continue;
        std::istringstream iss(linha);
        std::string chave;
        iss >> chave;

        if (chave == "CONFIG") {
            iss >> tempoLimite >> spawn.x >> spawn.y >> spawn.z;
        }
        else {
            ObjetoCenario o;
            o.tipo = (TipoObjeto)std::stoi(chave);
            int formaInt = 0;
            iss >> formaInt
                >> o.posicao.x >> o.posicao.y >> o.posicao.z
                >> o.escala.x >> o.escala.y >> o.escala.z
                >> o.rotacao.x >> o.rotacao.y >> o.rotacao.z
                >> o.eixoMovimento.x >> o.eixoMovimento.y >> o.eixoMovimento.z
                >> o.amplitudeMovimento >> o.velocidadeMovimento;
            o.forma = (FormaGeometrica)formaInt;
            objetos.push_back(o);
        }
    }
    arq.close();
    return true;
}

// ------------------------------------------------------------
// ESTADO GLOBAL DO JOGO
// ------------------------------------------------------------
enum class EstadoApp { MENU, EDITOR, JOGANDO, PAUSADO, VITORIA, DERROTA };
EstadoApp estado = EstadoApp::MENU;

const float RAIO_BOLA = 0.6f;
// Nao sao mais 'const': viram ajustaveis pelo HUD do editor
float gravidadeJogo = 22.0f;      // "aceleracao do pulo"
float velocidadeAvatar = 22.0f;   // velocidade/aceleracao lateral do avatar
float impulsoPulo = 11.0f;        // "altura do pulo"
const float GRID_TAM = 2.0f;

glm::vec3 posBola = glm::vec3(0.0f, 1.2f, 0.0f);
glm::vec3 velBola = glm::vec3(0.0f);
glm::quat rotacaoBola = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
glm::vec3 ultimoCheckpointPos = posBola;
bool noChao = false;

float tempoRestante = 60.0f;
bool fimDeJogo = false;
bool vitoria = false;
int pontuacao = 0;
int faseJogando = 0;

// ------------------------------------------------------------
// REINICIAR PARTIDA (comeca do zero, na fase 0)
// ------------------------------------------------------------
void resetJogo(std::vector<ObjetoCenario>& objetos, const glm::vec3& spawn, float tempoLimite) {
    for (auto& o : objetos) o.coletada = false;
    posBola = spawn;
    velBola = glm::vec3(0.0f);
    rotacaoBola = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    ultimoCheckpointPos = spawn;
    noChao = false;
    tempoRestante = tempoLimite;
    fimDeJogo = false;
    vitoria = false;
    pontuacao = 0;
}

// Troca o cenario ativo pra proxima fase, mantendo a pontuacao acumulada.
void avancarParaFase(std::vector<ObjetoCenario>& objetos, const Fase& fase) {
    objetos = fase.objetos;
    for (auto& o : objetos) o.coletada = false;
    posBola = fase.spawn;
    velBola = glm::vec3(0.0f);
    rotacaoBola = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    ultimoCheckpointPos = fase.spawn;
    noChao = false;
    tempoRestante = fase.tempoLimite;
}

// ------------------------------------------------------------
// FISICA E COLISAO -- recebe frente/lateral calculados a partir do
// angulo ATUAL da camera (corrige a inversao e mantem coerencia
// mesmo girando a camera livremente)
// ------------------------------------------------------------
void atualizarFisica(GLFWwindow* window, float dt, std::vector<ObjetoCenario>& objetos, float tempoJogo,
    const glm::vec3& frenteCam, const glm::vec3& lateralCam, const ConfiguracaoCenario& cfg,
    std::vector<Fase>& fases) {
    if (fimDeJogo) return;

    float inZ = 0.0f, inX = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) inZ += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) inZ -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) inX += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) inX -= 1.0f;

    glm::vec2 direcao(inX, inZ);
    glm::vec3 velHorizontalDesejada(0.0f);
    if (glm::length(direcao) > 0.0001f) {
        direcao = glm::normalize(direcao);
        velHorizontalDesejada = (lateralCam * direcao.x + frenteCam * direcao.y) * velocidadeAvatar;
    }
    // X/Z definidos diretamente pela tecla (para instantaneamente ao soltar).
    // Y intocado -- gravidade e pulo continuam 100% naturais, sem nenhum limite de velocidade artificial.
    velBola.x = velHorizontalDesejada.x;
    velBola.z = velHorizontalDesejada.z;

    velBola.y -= gravidadeJogo * dt;
    posBola += velBola * dt;

    noChao = false;
    bool avancarFaseAgora = false;

    for (auto& obj : objetos) {
        if (obj.tipo == TipoObjeto::SPAWN) continue;

        if (obj.tipo == TipoObjeto::CHECKPOINT || obj.tipo == TipoObjeto::VITORIA || obj.tipo == TipoObjeto::MOEDA) {
            if (obj.tipo == TipoObjeto::MOEDA && obj.coletada) continue;
            float raioObj = raioEsferaDoTipo(obj.tipo);
            float dist = glm::length(posBola - obj.posicao);
            if (dist < RAIO_BOLA + raioObj) {
                if (obj.tipo == TipoObjeto::CHECKPOINT) ultimoCheckpointPos = obj.posicao + glm::vec3(0.0f, 0.9f, 0.0f);
                else if (obj.tipo == TipoObjeto::MOEDA) {
                    obj.coletada = true;
                    pontuacao += 10;
                    if (cfg.tipoVitoria == TipoVitoria::POR_PONTOS && pontuacao >= (int)cfg.pontuacaoParaVencer) {
                        vitoria = true; fimDeJogo = true;
                    }
                }
                else if (obj.tipo == TipoObjeto::VITORIA) {
                    if (faseJogando + 1 < (int)fases.size()) avancarFaseAgora = true;
                    else { vitoria = true; fimDeJogo = true; }
                }
            }
            continue;
        }

        ObjetoCenario tmp = obj;
        if (obj.tipo == TipoObjeto::OBSTACULO_MOVEL) {
            float offset = sinf(tempoJogo * obj.velocidadeMovimento) * obj.amplitudeMovimento;
            tmp.posicao = obj.posicao + obj.eixoMovimento * offset;
        }

        glm::vec3 normal, pontoPerto;
        if (!colideComObjeto(tmp, posBola, RAIO_BOLA, normal, pontoPerto)) continue;

        if (obj.tipo == TipoObjeto::OBSTACULO_FALSO) continue;

        if (obj.tipo == TipoObjeto::OBSTACULO_MORTE) {
            posBola = ultimoCheckpointPos;
            velBola = glm::vec3(0.0f);
            continue;
        }

        posBola = pontoPerto + normal * RAIO_BOLA;
        if (obj.tipo == TipoObjeto::OBSTACULO_REPELIR) {
            velBola = glm::reflect(velBola, normal) * 1.15f;
        }
        else {
            float velNormal = glm::dot(velBola, normal);
            if (velNormal < 0.0f) velBola -= normal * velNormal;
            if (normal.y > 0.6f) { noChao = true; }
        }
    }

    static bool espacoAntes = false;
    bool espacoAgora = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (espacoAgora && !espacoAntes && noChao) {
        velBola.y = impulsoPulo;
        noChao = false;
    }
    espacoAntes = espacoAgora;

    // So agora, com o laco de colisao ja terminado, e seguro trocar o
    // vetor de objetos (trocar durante o laco corromperia a iteracao).
    if (avancarFaseAgora) {
        faseJogando++;
        avancarParaFase(objetos, fases[faseJogando]);
        return;
    }

    if (posBola.y < -20.0f) {
        posBola = ultimoCheckpointPos;
        velBola = glm::vec3(0.0f);
    }

    tempoRestante -= dt;
    if (tempoRestante <= 0.0f) { tempoRestante = 0.0f; fimDeJogo = true; }
}

// ------------------------------------------------------------
// HUD
// ------------------------------------------------------------
void desenharRetanguloHUD(unsigned int shaderHUD, unsigned int vao, float x, float y, float w, float h, glm::vec4 cor) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
    model = glm::scale(model, glm::vec3(w, h, 1.0f));
    glUniformMatrix4fv(glGetUniformLocation(shaderHUD, "modelHUD"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform4fv(glGetUniformLocation(shaderHUD, "corHUD"), 1, glm::value_ptr(cor));
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void desenharDigitoHUD(unsigned int shaderHUD, unsigned int vao, int digito, float x, float y, float W, float H, float esp, glm::vec4 cor) {
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

void desenharNumeroHUD(unsigned int shaderHUD, unsigned int vao, int numero, float x, float y, float W, float H, float esp, glm::vec4 cor) {
    if (numero < 0) numero = 0;
    std::string texto = std::to_string(numero);
    float cursorX = x;
    for (char c : texto) {
        desenharDigitoHUD(shaderHUD, vao, c - '0', cursorX, y, W, H, esp, cor);
        cursorX += W + esp * 2.2f;
    }
}

// ------------------------------------------------------------
// FONTE DE TEXTO 5x7 -- bitmap simples desenhado com os mesmos
// retangulos do HUD (sem carregar nenhum arquivo de fonte).
// Cada glifo e uma grade de 5 colunas x 7 linhas: '1' = pixel aceso.
// Suporta A-Z, 0-9, espaco e alguns simbolos. Passe sempre texto
// em MAIUSCULAS.
// ------------------------------------------------------------
const char* const* glifo5x7(char c) {
    static const char* ESP[7] = { "00000","00000","00000","00000","00000","00000","00000" };
    static const char* TRACO[7] = { "00000","00000","00000","11111","00000","00000","00000" };
    static const char* BARRA[7] = { "00001","00010","00010","00100","01000","01000","10000" };
    static const char* DOISP[7] = { "00000","00100","00100","00000","00100","00100","00000" };
    static const char* PONTO[7] = { "00000","00000","00000","00000","00000","01100","01100" };
    static const char* PORC[7] = { "10001","00010","00100","00100","01000","10001","00000" };
    static const char* A[7] = { "01110","10001","10001","11111","10001","10001","10001" };
    static const char* B[7] = { "11110","10001","10001","11110","10001","10001","11110" };
    static const char* C[7] = { "01111","10000","10000","10000","10000","10000","01111" };
    static const char* D[7] = { "11110","10001","10001","10001","10001","10001","11110" };
    static const char* E[7] = { "11111","10000","10000","11110","10000","10000","11111" };
    static const char* F[7] = { "11111","10000","10000","11110","10000","10000","10000" };
    static const char* G[7] = { "01111","10000","10000","10111","10001","10001","01111" };
    static const char* H[7] = { "10001","10001","10001","11111","10001","10001","10001" };
    static const char* I[7] = { "11111","00100","00100","00100","00100","00100","11111" };
    static const char* J[7] = { "00111","00010","00010","00010","00010","10010","01100" };
    static const char* K[7] = { "10001","10010","10100","11000","10100","10010","10001" };
    static const char* L[7] = { "10000","10000","10000","10000","10000","10000","11111" };
    static const char* M[7] = { "10001","11011","10101","10001","10001","10001","10001" };
    static const char* N[7] = { "10001","11001","10101","10011","10001","10001","10001" };
    static const char* O[7] = { "01110","10001","10001","10001","10001","10001","01110" };
    static const char* P[7] = { "11110","10001","10001","11110","10000","10000","10000" };
    static const char* Q[7] = { "01110","10001","10001","10001","10101","10010","01101" };
    static const char* R[7] = { "11110","10001","10001","11110","10100","10010","10001" };
    static const char* S[7] = { "01111","10000","10000","01110","00001","00001","11110" };
    static const char* T[7] = { "11111","00100","00100","00100","00100","00100","00100" };
    static const char* U[7] = { "10001","10001","10001","10001","10001","10001","01110" };
    static const char* V[7] = { "10001","10001","10001","10001","10001","01010","00100" };
    static const char* W[7] = { "10001","10001","10001","10101","10101","11011","10001" };
    static const char* X[7] = { "10001","01010","00100","00100","00100","01010","10001" };
    static const char* Y[7] = { "10001","01010","00100","00100","00100","00100","00100" };
    static const char* Z[7] = { "11111","00001","00010","00100","01000","10000","11111" };
    static const char* N0[7] = { "01110","10001","10011","10101","11001","10001","01110" };
    static const char* N1[7] = { "00100","01100","00100","00100","00100","00100","11111" };
    static const char* N2[7] = { "01110","10001","00001","00010","00100","01000","11111" };
    static const char* N3[7] = { "01110","10001","00001","00110","00001","10001","01110" };
    static const char* N4[7] = { "00010","00110","01010","10010","11111","00010","00010" };
    static const char* N5[7] = { "11111","10000","11110","00001","00001","10001","01110" };
    static const char* N6[7] = { "00110","01000","10000","11110","10001","10001","01110" };
    static const char* N7[7] = { "11111","00001","00010","00100","01000","01000","01000" };
    static const char* N8[7] = { "01110","10001","10001","01110","10001","10001","01110" };
    static const char* N9[7] = { "01110","10001","10001","01111","00001","00010","01100" };

    switch (c) {
        case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
        case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
        case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
        case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
        case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
        case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
        case 'Y': return Y; case 'Z': return Z;
        case '0': return N0; case '1': return N1; case '2': return N2; case '3': return N3;
        case '4': return N4; case '5': return N5; case '6': return N6; case '7': return N7;
        case '8': return N8; case '9': return N9;
        case '-': return TRACO; case '/': return BARRA; case ':': return DOISP; case '.': return PONTO;
        case '%': return PORC;
        default: return ESP;
    }
}

// Desenha texto (sempre em MAIUSCULAS) usando a fonte 5x7. 'tam' e o
// tamanho de cada "pixel" do glifo em pixels de tela.
void desenharTextoHUD(unsigned int shaderHUD, unsigned int vao, const std::string& texto, float x, float y, float tam, glm::vec4 cor) {
    float cursorX = x;
    for (char c : texto) {
        const char* const* g = glifo5x7(c);
        for (int linha = 0; linha < 7; ++linha) {
            for (int col = 0; col < 5; ++col) {
                if (g[linha][col] == '1') {
                    desenharRetanguloHUD(shaderHUD, vao, cursorX + col * tam, y + linha * tam, tam, tam, cor);
                }
            }
        }
        cursorX += tam * 6.0f; // 5 colunas de glifo + 1 coluna de espacamento
    }
}

// Largura total (em pixels) que um texto ocuparia, pra poder centralizar.
float larguraTextoHUD(const std::string& texto, float tam) {
    if (texto.empty()) return 0.0f;
    return (float)texto.size() * tam * 6.0f - tam; // sem o espacamento extra do ultimo char
}

bool teclaApertada(GLFWwindow* window, int tecla, bool& estadoAnterior) {
    bool agora = glfwGetKey(window, tecla) == GLFW_PRESS;
    bool apertouAgora = agora && !estadoAnterior;
    estadoAnterior = agora;
    return apertouAgora;
}

bool dentroDoRetangulo(float px, float py, float x, float y, float w, float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

// ------------------------------------------------------------
// ELEMENTOS DE UI (botoes e sliders do painel do editor)
// ------------------------------------------------------------
struct BotaoUI {
    float x, y, w, h;
    glm::vec3 cor;
    int acao; // 0-8 = seleciona TipoObjeto; 9 = remover; 10 = salvar; 11 = carregar; 12 = limpar; 13 = jogar; 14 = editar (mover/girar)
};

struct SliderUI {
    float x, y, w, h;
    float valorMin, valorMax;
    float* alvo;
};

// Botao de ajuste continuo (+/-) para mover/girar o objeto selecionado
struct BotaoEixo {
    float x, y, w, h;
    int eixo;       // 0=X, 1=Y, 2=Z
    bool positivo;  // true = incrementa, false = decrementa
    bool rotacao;   // true = ajusta rotacao, false = ajusta posicao
};

enum class ModoInteracao { COLOCAR, REMOVER, EDITAR };

// Testa a proximidade de um raio (origem+direcao) com cada objeto e retorna
// o indice do mais proximo ao longo do raio (ou -1 se nenhum for atingido).
int selecionarObjetoPorRaio(const std::vector<ObjetoCenario>& objetos, const glm::vec3& origem, const glm::vec3& direcao) {
    int melhor = -1;
    float menorT = 1e9f;
    for (size_t i = 0; i < objetos.size(); ++i) {
        if (objetos[i].tipo == TipoObjeto::SPAWN) continue;
        glm::vec3 oc = objetos[i].posicao - origem;
        float t = glm::dot(oc, direcao);
        if (t < 0.0f) continue;
        glm::vec3 pontoNaLinha = origem + direcao * t;
        float raioAprox = ehEsfera(objetos[i].tipo) ? raioEsferaDoTipo(objetos[i].tipo) : (glm::length(objetos[i].escala) * 0.5f);
        float dist = glm::length(pontoNaLinha - objetos[i].posicao);
        if (dist < raioAprox + 0.4f && t < menorT) { menorT = t; melhor = (int)i; }
    }
    return melhor;
}

// ------------------------------------------------------------
// PROGRAMA PRINCIPAL
// ------------------------------------------------------------
int main() {
    // ---- Esconde a janela de console (terminal). Ela sempre existiu
    // por causa do subsistema do projeto; agora nao fica mais visivel
    // por cima/atras da janela do jogo. ----
    HWND janelaConsole = GetConsoleWindow();
    if (janelaConsole) ShowWindow(janelaConsole, SW_HIDE);

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const int LARGURA = 1280, ALTURA = 720;
    GLFWwindow* window = glfwCreateWindow(LARGURA, ALTURA, "Marble Time Rush - Editor", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwFocusWindow(window);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    unsigned int shaderPrincipal = criarPrograma(vertexShaderSource, fragmentShaderSource);
    unsigned int shaderProfundidade = criarPrograma(depthVertexShaderSource, depthFragmentShaderSource);
    unsigned int shaderHUD = criarPrograma(hudVertexShaderSource, hudFragmentShaderSource);

    unsigned int esferaVAO, esferaIndices;
    criarEsfera(esferaVAO, esferaIndices, 1.0f, 32, 16);
    unsigned int cuboVAO, cuboIndices;
    criarCubo(cuboVAO, cuboIndices);
    unsigned int rampaVAO, rampaIndices;
    criarRampa(rampaVAO, rampaIndices);
    unsigned int cilindroVAO, cilindroIndices;
    criarCilindro(cilindroVAO, cilindroIndices);

    unsigned int texGrade = criarTexturaGrade(512, 32, glm::vec3(0.09f, 0.32f, 0.28f), glm::vec3(0.16f, 0.46f, 0.40f));
    unsigned int texChaoJogo = criarTexturaQuadriculada(512, 24, glm::vec3(0.72f, 0.88f, 0.62f), glm::vec3(0.62f, 0.82f, 0.55f));
    unsigned int texBola = criarTexturaCannonbolt(512);

    std::vector<Fase> fases = criarFasesPadrao();
    int faseEditando = 0;
    std::vector<ObjetoCenario> objetos = fases[0].objetos;
    float tempoLimiteAtual = fases[0].tempoLimite;
    glm::vec3 spawnAtual = fases[0].spawn;

    ConfiguracaoCenario cfg;
    atualizarPosicaoInicialSolPorAngulo(cfg);
    std::vector<Arvore> arvores = criarArvores();
    std::vector<Nuvem> nuvens = criarNuvens();
    std::vector<Rocha> rochas = criarRochas();
    std::vector<Arbusto> arbustos = criarArbustos();

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

    // ---------------- Camera do EDITOR (orbita em torno de um foco pan-avel) ----------------
    glm::vec3 editorFoco = glm::vec3(0.0f, 0.5f, 0.0f);
    float editorYaw = -50.0f, editorPitch = 35.0f, editorDistancia = 20.0f;
    bool editorArrastando = false;
    double editorMouseX = 0.0, editorMouseY = 0.0;

    // ---------------- Camera do JOGO (orbita em torno da bola) ----------------
    float jogoYaw = 0.0f, jogoPitch = 28.0f, jogoDistancia = 9.0f;
    bool jogoArrastando = false;
    double jogoMouseX = 0.0, jogoMouseY = 0.0;

    TipoObjeto tipoSelecionado = TipoObjeto::OBSTACULO_SOLIDO;
    ModoInteracao modoInteracao = ModoInteracao::COLOCAR;
    int objetoSelecionado = -1;
    std::vector<std::vector<ObjetoCenario>> pilhaDesfazer; // Ctrl+Z desfaz colocar/remover/limpar/mover
    glm::vec3 escalaSelecionada = glm::vec3(2.0f, 2.0f, 2.0f);
    FormaGeometrica formaSelecionada = FormaGeometrica::CUBO;
    glm::vec3 rotacaoSelecionada = glm::vec3(0.0f);
    glm::vec3 eixoMovimentoSelecionado = glm::vec3(0.0f, 1.0f, 0.0f);
    float alturaGrade = 0.0f;
    int presetAtual = 2;

    // ---------------- Layout do painel esquerdo (botoes) ----------------
    std::vector<BotaoUI> botoes;
    {
        float px = 20, colW = 108, colGap = 8, rowH = 46, rowGap = 8;
        float col0 = px + 10, col1 = col0 + colW + colGap;
        float linha0 = 90;
        struct { TipoObjeto tipo; int coluna; int linha; } layout[] = {
            { TipoObjeto::OBSTACULO_SOLIDO, 0, 0 }, { TipoObjeto::OBSTACULO_FALSO, 1, 0 },
            { TipoObjeto::OBSTACULO_MORTE,  0, 1 }, { TipoObjeto::CHECKPOINT,      1, 1 },
            { TipoObjeto::VITORIA,          0, 2 }, { TipoObjeto::MOEDA,           1, 2 },
            { TipoObjeto::OBSTACULO_MOVEL,  0, 3 },
        };
        for (auto& L : layout) {
            float bx = (L.coluna == 0) ? col0 : col1;
            float by = linha0 + L.linha * (rowH + rowGap);
            botoes.push_back({ bx, by, colW, rowH, corDoTipo(L.tipo), (int)L.tipo });
        }
        // Botao "Remover" no lugar (1,3)
        botoes.push_back({ col1, linha0 + 3 * (rowH + rowGap), colW, rowH, glm::vec3(0.55f, 0.12f, 0.12f), 9 });

        // Botao "Editar" (mover/girar) -- linha inteira abaixo
        float yEditar = linha0 + 4 * (rowH + rowGap);
        botoes.push_back({ col0, yEditar, colW * 2 + colGap, rowH, glm::vec3(0.55f, 0.40f, 0.85f), 14 });

        float yBotoesFim = yEditar + rowH + rowGap;
        botoes.push_back({ col0, yBotoesFim, colW, 40, glm::vec3(0.15f, 0.65f, 0.35f), 10 }); // salvar
        botoes.push_back({ col1, yBotoesFim, colW, 40, glm::vec3(0.35f, 0.35f, 0.85f), 11 }); // carregar
        botoes.push_back({ col0, yBotoesFim + 48, colW * 2 + colGap, 36, glm::vec3(0.75f, 0.15f, 0.15f), 12 }); // limpar
    }
    BotaoUI botaoJogar = { (float)LARGURA - 280, 20, 260, 50, glm::vec3(0.20f, 0.45f, 0.85f), 13 };

    // ---------------- Sliders e botao de tipo de vitoria no painel direito ----------------
    float painelDirX = LARGURA - 280, painelDirY = 90, painelDirW = 260;
    float passoSlider = 46.0f;
    std::vector<SliderUI> sliders = {
        { painelDirX + 20, painelDirY + 30 + passoSlider * 0,  painelDirW - 40, 10,   5.0f,  50.0f, &velocidadeAvatar },
        { painelDirX + 20, painelDirY + 30 + passoSlider * 1,  painelDirW - 40, 10,   8.0f,  40.0f, &gravidadeJogo },
        { painelDirX + 20, painelDirY + 30 + passoSlider * 2,  painelDirW - 40, 10,   4.0f,  20.0f, &impulsoPulo },
        { painelDirX + 20, painelDirY + 30 + passoSlider * 3,  painelDirW - 40, 10,  -0.30f,  0.30f, &cfg.velocidadeSol },
        { painelDirX + 20, painelDirY + 30 + passoSlider * 4,  painelDirW - 40, 10,   0.0f, 360.0f, &anguloSolInicial },
        { painelDirX + 20, painelDirY + 30 + passoSlider * 5,  painelDirW - 40, 10,  10.0f, 180.0f, &tempoLimiteAtual },
        { painelDirX + 20, painelDirY + 30 + passoSlider * 6,  painelDirW - 40, 10,  10.0f, 500.0f, &cfg.pontuacaoParaVencer },
    };
    int sliderArrastando = -1;
    float yTipoVitoria = painelDirY + 30 + passoSlider * 7 + 10.0f;
    BotaoUI botaoTipoVitoria = { painelDirX + 20, yTipoVitoria, painelDirW - 40, 38, glm::vec3(1.0f, 0.85f, 0.2f), 15 };
    float painelDirAltura = yTipoVitoria + 38.0f - painelDirY + 15.0f;

    // ---------------- Painel inferior: mover/girar objeto selecionado ----------------
    std::vector<BotaoEixo> botoesEixo;
    {
        float largura = 6 * 92.0f;
        float baseX = (float)LARGURA / 2.0f - largura / 2.0f;
        float baseY = (float)ALTURA - 168.0f;
        for (int col = 0; col < 6; ++col) {
            int eixo = col % 3;
            bool rot = col >= 3;
            float bx = baseX + col * 92.0f + 10.0f;
            botoesEixo.push_back({ bx, baseY, 72.0f, 40.0f, eixo, true, rot });          // "+"
            botoesEixo.push_back({ bx, baseY + 84.0f, 72.0f, 40.0f, eixo, false, rot }); // "-"
        }
    }
    int botaoEixoSegurando = -1;

    std::cout << "Marble Time Rush iniciado (console escondido -- tudo aparece na janela do jogo).\n";

    float lastFrame = 0.0f;
    float tempoAcumuladoJogo = 0.0f;
    float tempoAcumuladoTotal = 0.0f;

    // ---- Auxiliares de fase: salvam/restauram o buffer de edicao e
    // controlam a transicao editor<->jogo sem perder o que foi editado ----
    auto salvarBufferNaFase = [&]() {
        fases[faseEditando].objetos = objetos;
        fases[faseEditando].tempoLimite = tempoLimiteAtual;
        fases[faseEditando].spawn = spawnAtual;
        };
    auto carregarFaseNoEditor = [&](int indice) {
        faseEditando = indice;
        objetos = fases[faseEditando].objetos;
        tempoLimiteAtual = fases[faseEditando].tempoLimite;
        spawnAtual = fases[faseEditando].spawn;
        objetoSelecionado = -1;
        pilhaDesfazer.clear(); // desfazer nao atravessa fases diferentes
        };
    auto iniciarJogo = [&]() {
        salvarBufferNaFase();
        faseJogando = 0;
        objetos = fases[0].objetos;
        resetJogo(objetos, fases[0].spawn, fases[0].tempoLimite);
        tempoAcumuladoJogo = 0.0f;
        estado = EstadoApp::JOGANDO;
        };
    auto voltarParaEditor = [&]() {
        objetos = fases[faseEditando].objetos;
        estado = EstadoApp::EDITOR;
        };
    // Guarda uma copia do cenario atual antes de uma acao destrutiva
    // (colocar/remover/limpar/mover), pra poder voltar com Ctrl+Z.
    const size_t LIMITE_DESFAZER = 30;
    auto salvarParaDesfazer = [&]() {
        pilhaDesfazer.push_back(objetos);
        if (pilhaDesfazer.size() > LIMITE_DESFAZER) pilhaDesfazer.erase(pilhaDesfazer.begin());
        };
    auto desfazer = [&]() {
        if (pilhaDesfazer.empty()) return;
        objetos = pilhaDesfazer.back();
        pilhaDesfazer.pop_back();
        objetoSelecionado = -1;
        std::cout << "Desfeito. (" << pilhaDesfazer.size() << " passos restantes)\n";
        };

    auto desenharObjeto = [&](unsigned int shaderAtivo, const ObjetoCenario& obj, const glm::vec3& posDesenho) {
        if (ehEsfera(obj.tipo)) {
            glBindVertexArray(esferaVAO);
            float raio = raioEsferaDoTipo(obj.tipo);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), posDesenho);
            if (obj.tipo == TipoObjeto::MOEDA) model = glm::rotate(model, tempoAcumuladoTotal * 3.0f, glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(raio));
            glUniformMatrix4fv(glGetUniformLocation(shaderAtivo, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform1i(glGetUniformLocation(shaderAtivo, "useTexture"), 0);
            glUniform2f(glGetUniformLocation(shaderAtivo, "uvScale"), 1.0f, 1.0f);
            glUniform3fv(glGetUniformLocation(shaderAtivo, "fallbackColor"), 1, glm::value_ptr(corDoTipo(obj.tipo)));
            glDrawElements(GL_TRIANGLES, esferaIndices, GL_UNSIGNED_INT, 0);
        }
        else {
            unsigned int vaoForma = cuboVAO;
            unsigned int indicesForma = cuboIndices;
            if (obj.forma == FormaGeometrica::RAMPA) { vaoForma = rampaVAO; indicesForma = rampaIndices; }
            else if (obj.forma == FormaGeometrica::CILINDRO) { vaoForma = cilindroVAO; indicesForma = cilindroIndices; }

            glBindVertexArray(vaoForma);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), posDesenho);
            model = glm::rotate(model, glm::radians(obj.rotacao.y), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(obj.rotacao.x), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(obj.rotacao.z), glm::vec3(0, 0, 1));
            model = glm::scale(model, obj.escala);
            glUniformMatrix4fv(glGetUniformLocation(shaderAtivo, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform1i(glGetUniformLocation(shaderAtivo, "useTexture"), 0);
            glUniform2f(glGetUniformLocation(shaderAtivo, "uvScale"), 1.0f, 1.0f);
            glUniform3fv(glGetUniformLocation(shaderAtivo, "fallbackColor"), 1, glm::value_ptr(corDoTipo(obj.tipo)));
            glDrawElements(GL_TRIANGLES, indicesForma, GL_UNSIGNED_INT, 0);
        }
        };

    auto desenharObjetos = [&](unsigned int shaderAtivo, float tempoJogo, bool passeDeSombra) {
        glUniform1i(glGetUniformLocation(shaderAtivo, "emissivo"), 0);
        for (const auto& obj : objetos) {
            if (obj.tipo == TipoObjeto::SPAWN && estado != EstadoApp::EDITOR) continue;
            if (obj.tipo == TipoObjeto::MOEDA && obj.coletada && estado != EstadoApp::EDITOR) continue;
            if (passeDeSombra) {
                bool castSombra = obj.tipo == TipoObjeto::OBSTACULO_REPELIR || obj.tipo == TipoObjeto::OBSTACULO_SOLIDO || obj.tipo == TipoObjeto::OBSTACULO_MOVEL;
                if (!castSombra) continue;
            }
            glm::vec3 posDesenho = obj.posicao;
            if (obj.tipo == TipoObjeto::OBSTACULO_MOVEL) {
                float offset = sinf(tempoJogo * obj.velocidadeMovimento) * obj.amplitudeMovimento;
                posDesenho = obj.posicao + obj.eixoMovimento * offset;
            }
            desenharObjeto(shaderAtivo, obj, posDesenho);
        }

        glBindVertexArray(esferaVAO);
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

    auto desenharCenario = [&](const glm::vec3& posSol, bool modoEditor) {
        glUniform1i(glGetUniformLocation(shaderPrincipal, "emissivo"), 0);
        glBindVertexArray(cuboVAO);

        glm::mat4 modelChao = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        modelChao = glm::scale(modelChao, glm::vec3(240.0f, 1.0f, 240.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "model"), 1, GL_FALSE, glm::value_ptr(modelChao));
        glUniform1i(glGetUniformLocation(shaderPrincipal, "useTexture"), 1);
        glUniform2f(glGetUniformLocation(shaderPrincipal, "uvScale"), 60.0f, 60.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, modoEditor ? texGrade : texChaoJogo);
        glDrawElements(GL_TRIANGLES, cuboIndices, GL_UNSIGNED_INT, 0);

        if (!modoEditor) {
            glUniform1i(glGetUniformLocation(shaderPrincipal, "useTexture"), 0);
            glBindVertexArray(cuboVAO);
            for (const auto& a : arvores) {
                glm::mat4 modelTronco = glm::translate(glm::mat4(1.0f), glm::vec3(a.pos.x, a.alturaTronco * 0.5f, a.pos.z));
                modelTronco = glm::scale(modelTronco, glm::vec3(0.4f, a.alturaTronco, 0.4f));
                glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "model"), 1, GL_FALSE, glm::value_ptr(modelTronco));
                glUniform3fv(glGetUniformLocation(shaderPrincipal, "fallbackColor"), 1, glm::value_ptr(glm::vec3(0.35f, 0.22f, 0.12f)));
                glDrawElements(GL_TRIANGLES, cuboIndices, GL_UNSIGNED_INT, 0);
            }

            glBindVertexArray(esferaVAO);
            for (const auto& a : arvores) {
                for (int f = 0; f < a.numFolhas; ++f) {
                    float offX = (pseudoAleatorio((int)(a.pos.z * 3.0f) + f * 5) - 0.5f) * a.raioFolhagem * 0.7f;
                    float offZ = (pseudoAleatorio((int)(a.pos.x * 3.0f) + f * 7) - 0.5f) * a.raioFolhagem * 0.7f;
                    float y = a.alturaTronco + a.raioFolhagem * 0.5f + f * a.raioFolhagem * 0.6f;
                    float escalaFolha = a.raioFolhagem * (1.0f - f * 0.16f);
                    glm::mat4 modelFolha = glm::translate(glm::mat4(1.0f), glm::vec3(a.pos.x + offX, y, a.pos.z + offZ));
                    modelFolha = glm::scale(modelFolha, glm::vec3(escalaFolha));
                    glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "model"), 1, GL_FALSE, glm::value_ptr(modelFolha));
                    glUniform3fv(glGetUniformLocation(shaderPrincipal, "fallbackColor"), 1, glm::value_ptr(a.corFolhagem));
                    glDrawElements(GL_TRIANGLES, esferaIndices, GL_UNSIGNED_INT, 0);
                }
            }

            for (const auto& n : nuvens) {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), n.pos);
                model = glm::scale(model, n.escala);
                glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "model"), 1, GL_FALSE, glm::value_ptr(model));
                glUniform3fv(glGetUniformLocation(shaderPrincipal, "fallbackColor"), 1, glm::value_ptr(glm::vec3(1.0f)));
                glDrawElements(GL_TRIANGLES, esferaIndices, GL_UNSIGNED_INT, 0);
            }

            glBindVertexArray(cuboVAO);
            for (const auto& r : rochas) {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), r.pos);
                model = glm::rotate(model, glm::radians(r.rotY), glm::vec3(0, 1, 0));
                model = glm::scale(model, r.escala);
                glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "model"), 1, GL_FALSE, glm::value_ptr(model));
                glUniform3fv(glGetUniformLocation(shaderPrincipal, "fallbackColor"), 1, glm::value_ptr(r.cor));
                glDrawElements(GL_TRIANGLES, cuboIndices, GL_UNSIGNED_INT, 0);
            }

            glBindVertexArray(esferaVAO);
            for (const auto& b : arbustos) {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), b.pos);
                model = glm::scale(model, glm::vec3(b.raio));
                glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "model"), 1, GL_FALSE, glm::value_ptr(model));
                glUniform3fv(glGetUniformLocation(shaderPrincipal, "fallbackColor"), 1, glm::value_ptr(b.cor));
                glDrawElements(GL_TRIANGLES, esferaIndices, GL_UNSIGNED_INT, 0);
            }
        }

        glBindVertexArray(esferaVAO);
        glm::mat4 modelSol = glm::translate(glm::mat4(1.0f), posSol);
        modelSol = glm::scale(modelSol, glm::vec3(modoEditor ? 4.0f : 9.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "model"), 1, GL_FALSE, glm::value_ptr(modelSol));
        glUniform3fv(glGetUniformLocation(shaderPrincipal, "fallbackColor"), 1, glm::value_ptr(glm::vec3(1.0f, 0.96f, 0.75f)));
        glUniform1i(glGetUniformLocation(shaderPrincipal, "emissivo"), 1);
        glDrawElements(GL_TRIANGLES, esferaIndices, GL_UNSIGNED_INT, 0);
        glUniform1i(glGetUniformLocation(shaderPrincipal, "emissivo"), 0);
        };

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        tempoAcumuladoTotal += deltaTime;

        static bool escAntes = false, tabAntes = false, fAntes = false, rAntes = false, mAntes = false, gAntes = false, nAntes = false, zAntes = false, deleteAntes = false;
        static bool f5Antes = false, f9Antes = false, esqAntes = false, pgUpAntes = false, pgDownAntes = false;

        bool escApertado = teclaApertada(window, GLFW_KEY_ESCAPE, escAntes);
        bool tabApertado = teclaApertada(window, GLFW_KEY_TAB, tabAntes);

        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        bool esqPressionado = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool esqClicado = esqPressionado && !esqAntes;
        bool dirPressionado = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        glm::vec3 posCamera, alvo;
        glm::vec3 frenteCam(0, 0, 1), lateralCam(1, 0, 0);

        if (estado == EstadoApp::EDITOR) {
            // Orbita da camera do editor (botao direito)
            if (dirPressionado) {
                if (editorArrastando) {
                    double dx = mx - editorMouseX, dy = my - editorMouseY;
                    editorYaw += (float)dx * 0.3f;
                    editorPitch -= (float)dy * 0.3f;
                    editorPitch = glm::clamp(editorPitch, 10.0f, 85.0f);
                }
                editorArrastando = true;
                editorMouseX = mx; editorMouseY = my;
            }
            else editorArrastando = false;

            float yawRad = glm::radians(editorYaw), pitchRad = glm::radians(editorPitch);
            glm::vec3 frenteFlat = glm::normalize(glm::vec3(-sinf(yawRad), 0.0f, -cosf(yawRad)));
            glm::vec3 lateralFlat = glm::normalize(glm::cross(frenteFlat, glm::vec3(0, 1, 0)));

            float velPan = 10.0f * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) editorFoco += frenteFlat * velPan;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) editorFoco -= frenteFlat * velPan;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) editorFoco += lateralFlat * velPan;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) editorFoco -= lateralFlat * velPan;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) editorFoco.y -= velPan;
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) editorFoco.y += velPan;

            if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) alturaGrade -= 2.0f * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) alturaGrade += 2.0f * deltaTime;

            glm::vec3 offset;
            offset.x = editorDistancia * cosf(pitchRad) * sinf(yawRad);
            offset.y = editorDistancia * sinf(pitchRad);
            offset.z = editorDistancia * cosf(pitchRad) * cosf(yawRad);
            posCamera = editorFoco + offset;
            alvo = editorFoco;

            glm::mat4 projTmp = glm::perspective(glm::radians(58.0f), (float)LARGURA / (float)ALTURA, 0.1f, 260.0f);
            glm::mat4 viewTmp = glm::lookAt(posCamera, alvo, glm::vec3(0, 1, 0));

            // Atalhos de teclado
            for (int i = 0; i < 9; ++i) {
                if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS) { tipoSelecionado = (TipoObjeto)i; modoInteracao = ModoInteracao::COLOCAR; }
            }
            if (teclaApertada(window, GLFW_KEY_F, fAntes)) {
                presetAtual = (presetAtual + 1) % 3;
                if (presetAtual == 0) escalaSelecionada = glm::vec3(3.0f, 0.6f, 3.0f);
                else if (presetAtual == 1) escalaSelecionada = glm::vec3(0.6f, 3.0f, 3.0f);
                else escalaSelecionada = glm::vec3(2.0f, 2.0f, 2.0f);
            }
            if (teclaApertada(window, GLFW_KEY_R, rAntes)) rotacaoSelecionada.y += 90.0f;
            if (teclaApertada(window, GLFW_KEY_M, mAntes)) {
                if (eixoMovimentoSelecionado.y > 0.5f) eixoMovimentoSelecionado = glm::vec3(1, 0, 0);
                else if (eixoMovimentoSelecionado.x > 0.5f) eixoMovimentoSelecionado = glm::vec3(0, 0, 1);
                else eixoMovimentoSelecionado = glm::vec3(0, 1, 0);
            }
            if (teclaApertada(window, GLFW_KEY_G, gAntes)) {
                formaSelecionada = (FormaGeometrica)(((int)formaSelecionada + 1) % 3);
            }

            // PAGE UP / PAGE DOWN trocam qual fase esta sendo editada
            if (teclaApertada(window, GLFW_KEY_PAGE_UP, pgUpAntes) && faseEditando + 1 < (int)fases.size()) {
                salvarBufferNaFase();
                carregarFaseNoEditor(faseEditando + 1);
            }
            if (teclaApertada(window, GLFW_KEY_PAGE_DOWN, pgDownAntes) && faseEditando > 0) {
                salvarBufferNaFase();
                carregarFaseNoEditor(faseEditando - 1);
            }
            // N cria uma fase nova (vazia) logo apos a atual e passa a edita-la
            if (teclaApertada(window, GLFW_KEY_N, nAntes)) {
                salvarBufferNaFase();
                Fase novaFase;
                novaFase.tempoLimite = 45.0f;
                novaFase.spawn = glm::vec3(0.0f, 1.2f, 0.0f);
                fases.insert(fases.begin() + faseEditando + 1, novaFase);
                pilhaDesfazer.clear();
                carregarFaseNoEditor(faseEditando + 1);
                std::cout << "Nova fase criada. Total: " << fases.size() << "\n";
            }

            if (teclaApertada(window, GLFW_KEY_F5, f5Antes)) {
                if (salvarFase("cenario_fase" + std::to_string(faseEditando + 1) + ".txt", objetos, tempoLimiteAtual, spawnAtual))
                    std::cout << "Fase " << (faseEditando + 1) << " salva.\n";
            }
            if (teclaApertada(window, GLFW_KEY_F9, f9Antes)) {
                if (carregarFase("cenario_fase" + std::to_string(faseEditando + 1) + ".txt", objetos, tempoLimiteAtual, spawnAtual)) {
                    pilhaDesfazer.clear();
                    std::cout << "Fase " << (faseEditando + 1) << " carregada.\n";
                }
            }
            if (teclaApertada(window, GLFW_KEY_DELETE, deleteAntes) && modoInteracao == ModoInteracao::EDITAR && objetoSelecionado >= 0 && objetoSelecionado < (int)objetos.size()) {
                salvarParaDesfazer();
                std::cout << "Removido: " << nomeDoTipo(objetos[objetoSelecionado].tipo) << "\n";
                objetos.erase(objetos.begin() + objetoSelecionado);
                objetoSelecionado = -1;
            }

            // Raio da camera ate o cursor do mouse (usado tanto pra colocar quanto pra selecionar)
            float xNdc = (2.0f * (float)mx) / LARGURA - 1.0f;
            float yNdc = 1.0f - (2.0f * (float)my) / ALTURA;
            glm::vec4 rayClip(xNdc, yNdc, -1.0f, 1.0f);
            glm::vec4 rayEye = glm::inverse(projTmp) * rayClip;
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
            glm::vec3 rayMundo = glm::normalize(glm::vec3(glm::inverse(viewTmp) * rayEye));

            // ---------------- Interacao do mouse ----------------

            // Painel mover/girar (ajuste continuo enquanto segura o botao)
            bool temSelecao = objetoSelecionado >= 0 && objetoSelecionado < (int)objetos.size();
            if (botaoEixoSegurando >= 0 && esqPressionado && temSelecao) {
                BotaoEixo& be = botoesEixo[botaoEixoSegurando];
                ObjetoCenario& o = objetos[objetoSelecionado];
                float sinalDir = be.positivo ? 1.0f : -1.0f;
                if (!be.rotacao) {
                    float passo = 3.0f * deltaTime * sinalDir;
                    if (be.eixo == 0) o.posicao.x += passo;
                    else if (be.eixo == 1) o.posicao.y += passo;
                    else o.posicao.z += passo;
                    if (o.tipo == TipoObjeto::SPAWN) spawnAtual = o.posicao;
                }
                else {
                    float passo = 60.0f * deltaTime * sinalDir;
                    if (be.eixo == 0) o.rotacao.x += passo;
                    else if (be.eixo == 1) o.rotacao.y += passo;
                    else o.rotacao.z += passo;
                }
            }
            else {
                botaoEixoSegurando = -1;
            }

            if (sliderArrastando >= 0 && esqPressionado) {
                SliderUI& s = sliders[sliderArrastando];
                float fracao = glm::clamp(((float)mx - s.x) / s.w, 0.0f, 1.0f);
                *s.alvo = s.valorMin + fracao * (s.valorMax - s.valorMin);
                if (sliderArrastando == 4) atualizarPosicaoInicialSolPorAngulo(cfg); // slider de angulo do sol
            }
            else if (botaoEixoSegurando < 0) {
                sliderArrastando = -1;
            }

            if (esqClicado && sliderArrastando < 0 && botaoEixoSegurando < 0) {
                bool atingiuUI = false;

                // Botoes de mover/girar (so aparecem com um objeto selecionado no modo Editar)
                if (!atingiuUI && modoInteracao == ModoInteracao::EDITAR && temSelecao) {
                    for (size_t i = 0; i < botoesEixo.size() && !atingiuUI; ++i) {
                        BotaoEixo& be = botoesEixo[i];
                        if (dentroDoRetangulo((float)mx, (float)my, be.x, be.y, be.w, be.h)) {
                            salvarParaDesfazer();
                            botaoEixoSegurando = (int)i;
                            atingiuUI = true;
                        }
                    }
                }

                // Sliders (area de clique um pouco maior que a barra, pra facilitar)
                for (size_t i = 0; i < sliders.size() && !atingiuUI; ++i) {
                    SliderUI& s = sliders[i];
                    if (dentroDoRetangulo((float)mx, (float)my, s.x - 4, s.y - 10, s.w + 8, s.h + 20)) {
                        sliderArrastando = (int)i;
                        float fracao = glm::clamp(((float)mx - s.x) / s.w, 0.0f, 1.0f);
                        *s.alvo = s.valorMin + fracao * (s.valorMax - s.valorMin);
                        if (i == 4) atualizarPosicaoInicialSolPorAngulo(cfg);
                        atingiuUI = true;
                    }
                }

                // Botao de tipo de vitoria (alterna Por Objeto/Tempo <-> Por Pontos)
                if (!atingiuUI && dentroDoRetangulo((float)mx, (float)my, botaoTipoVitoria.x, botaoTipoVitoria.y, botaoTipoVitoria.w, botaoTipoVitoria.h)) {
                    cfg.tipoVitoria = (cfg.tipoVitoria == TipoVitoria::POR_OBJETO_OU_TEMPO) ? TipoVitoria::POR_PONTOS : TipoVitoria::POR_OBJETO_OU_TEMPO;
                    atingiuUI = true;
                }

                // Botao "Jogar Cenario"
                if (!atingiuUI && dentroDoRetangulo((float)mx, (float)my, botaoJogar.x, botaoJogar.y, botaoJogar.w, botaoJogar.h)) {
                    iniciarJogo();
                    atingiuUI = true;
                }

                // Botoes do painel esquerdo
                for (size_t i = 0; i < botoes.size() && !atingiuUI; ++i) {
                    BotaoUI& b = botoes[i];
                    if (dentroDoRetangulo((float)mx, (float)my, b.x, b.y, b.w, b.h)) {
                        atingiuUI = true;
                        if (b.acao == 9) { modoInteracao = ModoInteracao::REMOVER; objetoSelecionado = -1; }
                        else if (b.acao == 14) { modoInteracao = ModoInteracao::EDITAR; }
                        else if (b.acao == 10) { if (salvarFase("cenario_fase" + std::to_string(faseEditando + 1) + ".txt", objetos, tempoLimiteAtual, spawnAtual)) std::cout << "Fase salva.\n"; }
                        else if (b.acao == 11) { if (carregarFase("cenario_fase" + std::to_string(faseEditando + 1) + ".txt", objetos, tempoLimiteAtual, spawnAtual)) { pilhaDesfazer.clear(); std::cout << "Fase carregada.\n"; } }
                        else if (b.acao == 12) { salvarParaDesfazer(); objetos.clear(); objetoSelecionado = -1; std::cout << "Fase limpa.\n"; }
                        else { tipoSelecionado = (TipoObjeto)b.acao; modoInteracao = ModoInteracao::COLOCAR; }
                    }
                }

                // Clique no mundo
                if (!atingiuUI) {
                    if (modoInteracao == ModoInteracao::REMOVER) {
                        int alvoRemocao = selecionarObjetoPorRaio(objetos, posCamera, rayMundo);
                        if (alvoRemocao >= 0) {
                            salvarParaDesfazer();
                            std::cout << "Removido: " << nomeDoTipo(objetos[alvoRemocao].tipo) << "\n";
                            objetos.erase(objetos.begin() + alvoRemocao);
                        }
                    }
                    else if (modoInteracao == ModoInteracao::EDITAR) {
                        objetoSelecionado = selecionarObjetoPorRaio(objetos, posCamera, rayMundo);
                    }
                    else {
                        // COLOCAR: raycast do mouse ate o plano de colocacao
                        if (fabsf(rayMundo.y) > 0.0001f) {
                            float t = (alturaGrade - posCamera.y) / rayMundo.y;
                            if (t > 0.0f) {
                                glm::vec3 pontoClicado = posCamera + rayMundo * t;
                                pontoClicado.x = roundf(pontoClicado.x / GRID_TAM) * GRID_TAM;
                                pontoClicado.z = roundf(pontoClicado.z / GRID_TAM) * GRID_TAM;

                                // Empilhar: se ja existe um objeto solido embaixo do ponto
                                // clicado (na mesma coluna X/Z), coloca em cima do topo dele
                                // em vez de usar sempre a altura fixa da grade.
                                float alturaBase = alturaGrade;
                                for (const auto& o : objetos) {
                                    if (o.tipo == TipoObjeto::SPAWN || ehEsfera(o.tipo)) continue;
                                    float meiaX = o.escala.x * 0.5f + 0.1f, meiaZ = o.escala.z * 0.5f + 0.1f;
                                    if (fabsf(pontoClicado.x - o.posicao.x) <= meiaX && fabsf(pontoClicado.z - o.posicao.z) <= meiaZ) {
                                        float topoO = o.posicao.y + o.escala.y * 0.5f;
                                        if (topoO > alturaBase) alturaBase = topoO;
                                    }
                                }
                                pontoClicado.y = alturaBase;

                                ObjetoCenario novo;
                                novo.tipo = tipoSelecionado;
                                novo.forma = formaSelecionada;
                                novo.posicao = ehEsfera(tipoSelecionado) ? (pontoClicado + glm::vec3(0, raioEsferaDoTipo(tipoSelecionado), 0)) : (pontoClicado + glm::vec3(0, escalaSelecionada.y * 0.5f, 0));
                                novo.escala = escalaSelecionada;
                                novo.rotacao = rotacaoSelecionada;
                                novo.eixoMovimento = eixoMovimentoSelecionado;
                                salvarParaDesfazer();
                                if (tipoSelecionado == TipoObjeto::SPAWN) {
                                    for (size_t i = 0; i < objetos.size();) {
                                        if (objetos[i].tipo == TipoObjeto::SPAWN) objetos.erase(objetos.begin() + i);
                                        else ++i;
                                    }
                                    spawnAtual = novo.posicao;
                                }
                                objetos.push_back(novo);
                                std::cout << "Colocado: " << nomeDoTipo(tipoSelecionado) << "\n";
                            }
                        }
                    }
                }
            }

            if (teclaApertada(window, GLFW_KEY_Z, zAntes) && glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
                desfazer();
            }

            if (escApertado) estado = EstadoApp::MENU;
            if (tabApertado) iniciarJogo();

            const char* nomeForma = (formaSelecionada == FormaGeometrica::CUBO) ? "cubo" : (formaSelecionada == FormaGeometrica::RAMPA) ? "rampa" : "cilindro";
            // Recalculado aqui (nao reaproveita "temSelecao" de mais cedo no frame):
            // Limpar/Ctrl+Z podem ter mudado objetos/objetoSelecionado nesse meio tempo,
            // e usar o valor antigo causava acesso fora dos limites do vetor.
            bool temSelecaoAgora = objetoSelecionado >= 0 && objetoSelecionado < (int)objetos.size();
            std::ostringstream titulo;
            titulo << "EDITOR - Fase " << (faseEditando + 1) << "/" << fases.size() << " - ";
            if (modoInteracao == ModoInteracao::REMOVER) titulo << "REMOVER (clique no objeto)";
            else if (modoInteracao == ModoInteracao::EDITAR) titulo << "EDITAR" << (temSelecaoAgora ? (" - " + std::string(nomeDoTipo(objetos[objetoSelecionado].tipo))) : " (clique num objeto)");
            else titulo << nomeDoTipo(tipoSelecionado);
            titulo << " | Forma: " << nomeForma << " (G alterna)"
                << " | Objetos: " << objetos.size()
                << " | Tempo: " << (int)tempoLimiteAtual << "s"
                << " | Vitoria: " << (cfg.tipoVitoria == TipoVitoria::POR_PONTOS ? "por pontos" : "objeto/tempo")
                << " | PAGE UP/DOWN=trocar fase | N=nova fase | CTRL+Z=desfazer | TAB=jogar";
            glfwSetWindowTitle(window, titulo.str().c_str());

        }
        else {
            // ---------------- Camera do JOGO: orbita centrada na bola ----------------
            if (dirPressionado) {
                if (jogoArrastando) {
                    double dx = mx - jogoMouseX, dy = my - jogoMouseY;
                    jogoYaw += (float)dx * 0.3f;
                    jogoPitch -= (float)dy * 0.3f;
                    jogoPitch = glm::clamp(jogoPitch, 10.0f, 85.0f);
                }
                jogoArrastando = true;
                jogoMouseX = mx; jogoMouseY = my;
            }
            else jogoArrastando = false;

            float yawRad = glm::radians(jogoYaw), pitchRad = glm::radians(jogoPitch);
            glm::vec3 offset;
            offset.x = jogoDistancia * cosf(pitchRad) * sinf(yawRad);
            offset.y = jogoDistancia * sinf(pitchRad);
            offset.z = jogoDistancia * cosf(pitchRad) * cosf(yawRad);

            alvo = posBola + glm::vec3(0.0f, 1.0f, 0.0f);
            posCamera = alvo + offset;

            // Frente/lateral recalculados a partir do angulo ATUAL da camera --
            // e isso que corrige a inversao e evita a confusao ao girar.
            frenteCam = glm::normalize(glm::vec3(-offset.x, 0.0f, -offset.z));
            lateralCam = glm::normalize(glm::cross(frenteCam, glm::vec3(0, 1, 0)));

            if (estado == EstadoApp::JOGANDO) {
                std::string tituloJogo = "Marble Time Rush - Fase " + std::to_string(faseJogando + 1) + "/" + std::to_string(fases.size()) + " - Testando (TAB volta ao editor)";
                glfwSetWindowTitle(window, tituloJogo.c_str());
                if (escApertado) {
                    estado = EstadoApp::PAUSADO;
                }
                else if (tabApertado) {
                    voltarParaEditor();
                }
                else {
                    glm::vec3 posAntesFrame = posBola;
                    tempoAcumuladoJogo += deltaTime;
                    atualizarFisica(window, deltaTime, objetos, tempoAcumuladoJogo, frenteCam, lateralCam, cfg, fases);

                    glm::vec3 deslocamento = posBola - posAntesFrame;
                    float dist = glm::length(deslocamento);
                    if (dist > 0.00001f) {
                        glm::vec3 dir = glm::normalize(deslocamento);
                        glm::vec3 eixo = glm::cross(dir, glm::vec3(0, 1, 0));
                        if (glm::length(eixo) > 0.0001f) {
                            eixo = glm::normalize(eixo);
                            float angulo = dist / RAIO_BOLA;
                            rotacaoBola = glm::angleAxis(angulo, eixo) * rotacaoBola;
                        }
                    }
                    if (fimDeJogo) estado = vitoria ? EstadoApp::VITORIA : EstadoApp::DERROTA;
                }
            }
            else if (estado == EstadoApp::PAUSADO) {
                glfwSetWindowTitle(window, "Marble Time Rush - PAUSADO (ESC retoma, TAB volta ao editor)");
                if (escApertado) estado = EstadoApp::JOGANDO;
                if (tabApertado) voltarParaEditor();
            }
            else if (estado == EstadoApp::VITORIA) {
                glfwSetWindowTitle(window, "Marble Time Rush - VITORIA! (TAB volta ao editor)");
                if (tabApertado) voltarParaEditor();
            }
            else if (estado == EstadoApp::DERROTA) {
                glfwSetWindowTitle(window, "Marble Time Rush - TEMPO ESGOTADO (TAB volta ao editor)");
                if (tabApertado) voltarParaEditor();
            }
            else {
                // ---------------- MENU ----------------
                glfwSetWindowTitle(window, "Marble Time Rush - ENTER: jogar | TAB: editar cenario | ESC: sair");
                if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) {
                    iniciarJogo();
                }
                if (tabApertado) voltarParaEditor();
                if (escApertado) glfwSetWindowShouldClose(window, true);
            }
        }

        esqAntes = esqPressionado;

        glm::mat4 projection = glm::perspective(glm::radians(58.0f), (float)LARGURA / (float)ALTURA, 0.1f, 260.0f);
        glm::mat4 view = glm::lookAt(posCamera, alvo, glm::vec3(0, 1, 0));

        glm::vec3 posSol = calcularPosicaoSol(cfg, tempoAcumuladoTotal);
        glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 100.0f);
        glm::mat4 lightView = glm::lookAt(alvo + glm::normalize(posSol) * 40.0f, alvo, glm::vec3(0, 1, 0));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        glViewport(0, 0, SHADOW_TAM, SHADOW_TAM);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(shaderProfundidade);
        glUniformMatrix4fv(glGetUniformLocation(shaderProfundidade, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        desenharObjetos(shaderProfundidade, tempoAcumuladoJogo, true);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        int fbLargura, fbAltura;
        glfwGetFramebufferSize(window, &fbLargura, &fbAltura);
        glViewport(0, 0, fbLargura, fbAltura);
        glClearColor(0.55f, 0.80f, 0.96f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderPrincipal);
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderPrincipal, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        glUniform3fv(glGetUniformLocation(shaderPrincipal, "lightPos"), 1, glm::value_ptr(posSol));
        glUniform3fv(glGetUniformLocation(shaderPrincipal, "viewPos"), 1, glm::value_ptr(posCamera));
        glUniform1i(glGetUniformLocation(shaderPrincipal, "albedoMap"), 0);
        glUniform1i(glGetUniformLocation(shaderPrincipal, "shadowMap"), 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthMap);

        bool modoEditorAtivo = (estado == EstadoApp::EDITOR);
        desenharCenario(posSol, modoEditorAtivo);
        desenharObjetos(shaderPrincipal, tempoAcumuladoJogo, false);

        if (estado == EstadoApp::EDITOR) {
            glDisable(GL_CULL_FACE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            if (modoInteracao == ModoInteracao::COLOCAR) {
                ObjetoCenario preview;
                preview.tipo = tipoSelecionado;
                preview.forma = formaSelecionada;
                preview.posicao = alvo;
                preview.escala = escalaSelecionada;
                preview.rotacao = rotacaoSelecionada;
                desenharObjeto(shaderPrincipal, preview, alvo);
            }
            else if (modoInteracao == ModoInteracao::EDITAR && objetoSelecionado >= 0 && objetoSelecionado < (int)objetos.size()) {
                desenharObjeto(shaderPrincipal, objetos[objetoSelecionado], objetos[objetoSelecionado].posicao);
            }
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glEnable(GL_CULL_FACE);
        }

        // ===== HUD =====
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); // a projecao do HUD inverte o eixo Y, o que inverte a
                                   // ordem dos vertices -- sem isso o culling descartava
                                   // TODO o HUD silenciosamente (essa era a causa raiz).
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(shaderHUD);
        glm::mat4 projecaoHUD = glm::ortho(0.0f, (float)LARGURA, (float)ALTURA, 0.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderHUD, "projecaoHUD"), 1, GL_FALSE, glm::value_ptr(projecaoHUD));

        if (estado == EstadoApp::EDITOR) {
            // Painel esquerdo (fundo)
            desenharRetanguloHUD(shaderHUD, hudVAO, 20, 20, 250, 460, glm::vec4(0.08f, 0.09f, 0.14f, 0.88f));
            for (auto& b : botoes) {
                bool selecionado = (b.acao == 9 && modoInteracao == ModoInteracao::REMOVER)
                    || (b.acao == 14 && modoInteracao == ModoInteracao::EDITAR)
                    || (b.acao == (int)tipoSelecionado && modoInteracao == ModoInteracao::COLOCAR && b.acao <= 8);
                if (selecionado) desenharRetanguloHUD(shaderHUD, hudVAO, b.x - 4, b.y - 4, b.w + 8, b.h + 8, glm::vec4(1, 1, 1, 0.9f));
                desenharRetanguloHUD(shaderHUD, hudVAO, b.x, b.y, b.w, b.h, glm::vec4(b.cor, 1.0f));
                std::string rotulo = rotuloBotaoPaleta(b.acao);
                float tamFonte = 2.2f;
                float largRotulo = larguraTextoHUD(rotulo, tamFonte);
                desenharTextoHUD(shaderHUD, hudVAO, rotulo, b.x + b.w / 2.0f - largRotulo / 2.0f, b.y + b.h / 2.0f - (7.0f * tamFonte) / 2.0f, tamFonte, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));
            }

            // Painel direito (parametros do jogo)
            desenharRetanguloHUD(shaderHUD, hudVAO, botaoJogar.x, botaoJogar.y, botaoJogar.w, botaoJogar.h, glm::vec4(botaoJogar.cor, 1.0f));
            {
                std::string rJogar = "JOGAR";
                float tf = 3.5f;
                float lr = larguraTextoHUD(rJogar, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rJogar, botaoJogar.x + botaoJogar.w / 2.0f - lr / 2.0f, botaoJogar.y + botaoJogar.h / 2.0f - (7.0f * tf) / 2.0f, tf, glm::vec4(1, 1, 1, 1));
            }
            desenharRetanguloHUD(shaderHUD, hudVAO, painelDirX, painelDirY, painelDirW, painelDirAltura, glm::vec4(0.08f, 0.09f, 0.14f, 0.88f));
            {
                static const char* nomesSliders[7] = { "VELOCIDADE", "GRAVIDADE", "PULO", "SOL VEL", "SOL ANG", "TEMPO FASE", "META PONTOS" };
                for (size_t i = 0; i < sliders.size(); ++i) {
                    SliderUI& s = sliders[i];
                    desenharTextoHUD(shaderHUD, hudVAO, nomesSliders[i], s.x, s.y - 16.0f, 1.8f, glm::vec4(0.75f, 0.80f, 0.90f, 1.0f));

                    // Valor numerico atual, alinhado a direita (2 casas decimais
                    // pra velocidade do sol, que e um numero pequeno; inteiro
                    // arredondado pros demais).
                    std::ostringstream ossVal;
                    if (i == 3) { ossVal << std::fixed; ossVal.precision(2); ossVal << *s.alvo; }
                    else ossVal << (int)roundf(*s.alvo);
                    std::string valTexto = ossVal.str();
                    float tfVal = 1.8f;
                    float lrVal = larguraTextoHUD(valTexto, tfVal);
                    desenharTextoHUD(shaderHUD, hudVAO, valTexto, s.x + s.w - lrVal, s.y - 16.0f, tfVal, glm::vec4(1.0f, 0.9f, 0.4f, 1.0f));

                    desenharRetanguloHUD(shaderHUD, hudVAO, s.x, s.y, s.w, s.h, glm::vec4(0.25f, 0.27f, 0.35f, 1.0f));
                    float fracao = glm::clamp((*s.alvo - s.valorMin) / (s.valorMax - s.valorMin), 0.0f, 1.0f);
                    desenharRetanguloHUD(shaderHUD, hudVAO, s.x, s.y, s.w * fracao, s.h, glm::vec4(0.30f, 0.55f, 0.95f, 1.0f));
                    desenharRetanguloHUD(shaderHUD, hudVAO, s.x + s.w * fracao - 6, s.y - 4, 12, s.h + 8, glm::vec4(0.85f, 0.90f, 1.0f, 1.0f));
                }
            }
            // Botao de tipo de vitoria: dourado = por objeto/tempo, roxo = por pontos
            glm::vec3 corVitoria = (cfg.tipoVitoria == TipoVitoria::POR_PONTOS) ? glm::vec3(0.60f, 0.35f, 0.85f) : glm::vec3(1.0f, 0.85f, 0.2f);
            desenharRetanguloHUD(shaderHUD, hudVAO, botaoTipoVitoria.x, botaoTipoVitoria.y, botaoTipoVitoria.w, botaoTipoVitoria.h, glm::vec4(corVitoria, 1.0f));
            {
                std::string rVit = (cfg.tipoVitoria == TipoVitoria::POR_PONTOS) ? "VITORIA: PONTOS" : "VITORIA: META/TEMPO";
                float tf = 1.8f;
                float lr = larguraTextoHUD(rVit, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rVit, botaoTipoVitoria.x + botaoTipoVitoria.w / 2.0f - lr / 2.0f, botaoTipoVitoria.y + botaoTipoVitoria.h / 2.0f - (7.0f * tf) / 2.0f, tf, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));
            }

            // Painel inferior: mover/girar objeto selecionado
            if (modoInteracao == ModoInteracao::EDITAR && objetoSelecionado >= 0 && objetoSelecionado < (int)objetos.size()) {
                float painelEixoX = botoesEixo[0].x - 14.0f, painelEixoY = botoesEixo[0].y - 34.0f;
                float painelEixoW = (botoesEixo[10].x + botoesEixo[10].w) - painelEixoX + 14.0f;
                float painelEixoH = (botoesEixo[1].y + botoesEixo[1].h) - painelEixoY + 14.0f;
                desenharRetanguloHUD(shaderHUD, hudVAO, painelEixoX, painelEixoY, painelEixoW, painelEixoH, glm::vec4(0.08f, 0.09f, 0.14f, 0.90f));
                glm::vec3 corEixo[3] = { glm::vec3(0.85f, 0.25f, 0.25f), glm::vec3(0.25f, 0.80f, 0.35f), glm::vec3(0.30f, 0.55f, 0.95f) };
                {
                    static const char* letraEixo[3] = { "X", "Y", "Z" };
                    for (int col = 0; col < 6; ++col) {
                        float bx = botoesEixo[col * 2].x, bw = botoesEixo[col * 2].w;
                        std::string rot = (col < 3) ? std::string("POS ") + letraEixo[col] : std::string("ROT ") + letraEixo[col - 3];
                        float tf = 1.6f;
                        float lr = larguraTextoHUD(rot, tf);
                        desenharTextoHUD(shaderHUD, hudVAO, rot, bx + bw / 2.0f - lr / 2.0f, painelEixoY + 6.0f, tf, glm::vec4(0.85f, 0.88f, 0.95f, 1.0f));
                    }
                }
                for (size_t i = 0; i < botoesEixo.size(); ++i) {
                    BotaoEixo& be = botoesEixo[i];
                    bool segurando = (botaoEixoSegurando == (int)i);
                    glm::vec3 cor = corEixo[be.eixo] * (be.rotacao ? 0.65f : 1.0f);
                    if (segurando) desenharRetanguloHUD(shaderHUD, hudVAO, be.x - 3, be.y - 3, be.w + 6, be.h + 6, glm::vec4(1, 1, 1, 0.9f));
                    desenharRetanguloHUD(shaderHUD, hudVAO, be.x, be.y, be.w, be.h, glm::vec4(cor, 1.0f));
                    // simbolo "+" ou "-" desenhado com retangulos finos
                    float cx = be.x + be.w / 2.0f, cy = be.y + be.h / 2.0f, braco = 14.0f, esp = 4.0f;
                    desenharRetanguloHUD(shaderHUD, hudVAO, cx - braco, cy - esp / 2.0f, braco * 2.0f, esp, glm::vec4(1, 1, 1, 1));
                    if (be.positivo) desenharRetanguloHUD(shaderHUD, hudVAO, cx - esp / 2.0f, cy - braco, esp, braco * 2.0f, glm::vec4(1, 1, 1, 1));
                }
            }
        }

        if (estado == EstadoApp::JOGANDO || estado == EstadoApp::PAUSADO) {
            float W = 34.0f, H = 54.0f, esp = 8.0f;
            int tempoExibido = (int)ceilf(tempoRestante);
            int digitos = (tempoExibido >= 10) ? 2 : 1;
            float largTotal = digitos * (W + esp * 2.2f);
            glm::vec4 corRelogio = (tempoRestante < 8.0f)
                ? glm::vec4(1.0f, 0.15f + 0.5f * fabsf(sinf(currentFrame * 8.0f)), 0.15f, 1.0f)
                : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

            float tempoX = (float)LARGURA / 2.0f - largTotal / 2.0f;
            desenharRetanguloHUD(shaderHUD, hudVAO, tempoX - 16.0f, 10.0f, largTotal + 32.0f, H + 20.0f, glm::vec4(0.05f, 0.05f, 0.08f, 0.55f));
            desenharNumeroHUD(shaderHUD, hudVAO, tempoExibido, tempoX, 20.0f, W, H, esp, corRelogio);

            desenharRetanguloHUD(shaderHUD, hudVAO, 8.0f, 10.0f, 100.0f, H + 20.0f, glm::vec4(0.05f, 0.05f, 0.08f, 0.55f));
            desenharNumeroHUD(shaderHUD, hudVAO, pontuacao, 24.0f, 20.0f, 26.0f, 40.0f, 6.0f, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
        }

        auto desenharIconesInstrucoes = [&](float cx, float painelY) {
            float painelX = cx - 280.0f, painelW = 560.0f, painelH = 430.0f;
            desenharRetanguloHUD(shaderHUD, hudVAO, painelX, painelY, painelW, painelH, glm::vec4(0.07f, 0.08f, 0.13f, 0.92f));

            {
                std::string titulo = "COMANDOS";
                float tf = 3.0f;
                float lr = larguraTextoHUD(titulo, tf);
                desenharTextoHUD(shaderHUD, hudVAO, titulo, cx - lr / 2.0f, painelY + 14.0f, tf, glm::vec4(1, 1, 1, 1));
            }

            float keySize = 46.0f, gap = 6.0f;
            float py = painelY + 50.0f;

            glm::vec4 corTecla(0.78f, 0.86f, 0.98f, 1.0f);
            glm::vec4 corLetra(0.05f, 0.05f, 0.08f, 1.0f);
            auto letraCentralizada = [&](const char* letra, float bx, float by, float bw, float bh) {
                float tf = 3.2f;
                float lr = larguraTextoHUD(letra, tf);
                desenharTextoHUD(shaderHUD, hudVAO, letra, bx + bw / 2.0f - lr / 2.0f, by + bh / 2.0f - (7.0f * tf) / 2.0f, tf, corLetra);
                };
            desenharRetanguloHUD(shaderHUD, hudVAO, cx - keySize / 2.0f, py, keySize, keySize, corTecla); // W
            letraCentralizada("W", cx - keySize / 2.0f, py, keySize, keySize);
            desenharRetanguloHUD(shaderHUD, hudVAO, cx - keySize / 2.0f - (keySize + gap), py + keySize + gap, keySize, keySize, corTecla); // A
            letraCentralizada("A", cx - keySize / 2.0f - (keySize + gap), py + keySize + gap, keySize, keySize);
            desenharRetanguloHUD(shaderHUD, hudVAO, cx - keySize / 2.0f, py + keySize + gap, keySize, keySize, corTecla); // S
            letraCentralizada("S", cx - keySize / 2.0f, py + keySize + gap, keySize, keySize);
            desenharRetanguloHUD(shaderHUD, hudVAO, cx - keySize / 2.0f + (keySize + gap), py + keySize + gap, keySize, keySize, corTecla); // D
            letraCentralizada("D", cx - keySize / 2.0f + (keySize + gap), py + keySize + gap, keySize, keySize);
            {
                std::string rMover = "MOVER";
                float tf = 1.6f;
                float lr = larguraTextoHUD(rMover, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rMover, cx - keySize / 2.0f - (keySize + gap) - lr - 10.0f, py + keySize + gap + keySize / 2.0f - 5.0f, tf, glm::vec4(0.7f, 0.75f, 0.85f, 1.0f));
            }

            float spaceW = 3.0f * keySize + 2.0f * gap, spaceH = 34.0f;
            float spaceY = py + 2.0f * (keySize + gap) + 22.0f;
            desenharRetanguloHUD(shaderHUD, hudVAO, cx - spaceW / 2.0f, spaceY, spaceW, spaceH, glm::vec4(0.35f, 0.85f, 0.50f, 1.0f));
            {
                std::string rPular = "ESPACO = PULAR";
                float tf = 2.0f;
                float lr = larguraTextoHUD(rPular, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rPular, cx - lr / 2.0f, spaceY + spaceH / 2.0f - (7.0f * tf) / 2.0f, tf, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));
            }

            desenharRetanguloHUD(shaderHUD, hudVAO, cx + 3.0f * (keySize + gap), py, keySize, keySize, glm::vec4(0.90f, 0.30f, 0.30f, 1.0f));
            {
                std::string rEsc = "ESC";
                float tf = 1.7f;
                float lr = larguraTextoHUD(rEsc, tf);
                float bx = cx + 3.0f * (keySize + gap);
                desenharTextoHUD(shaderHUD, hudVAO, rEsc, bx + keySize / 2.0f - lr / 2.0f, py + keySize / 2.0f - (7.0f * tf) / 2.0f, tf, corLetra);
            }
            {
                std::string rPausa = "PAUSA";
                float tf = 1.6f;
                desenharTextoHUD(shaderHUD, hudVAO, rPausa, cx + 3.0f * (keySize + gap), py + keySize + 8.0f, tf, glm::vec4(0.7f, 0.75f, 0.85f, 1.0f));
            }

            float mouseY = spaceY + spaceH + 34.0f, mouseBtnW = 40.0f, mouseBtnH = 58.0f, mouseGap = 6.0f;
            desenharRetanguloHUD(shaderHUD, hudVAO, cx - mouseBtnW - mouseGap / 2.0f, mouseY, mouseBtnW, mouseBtnH, glm::vec4(0.65f, 0.65f, 0.70f, 1.0f));
            desenharRetanguloHUD(shaderHUD, hudVAO, cx + mouseGap / 2.0f, mouseY, mouseBtnW, mouseBtnH, glm::vec4(0.90f, 0.60f, 0.30f, 1.0f));
            {
                std::string rMouse = "MOUSE: ESQ CONSTROI / DIR GIRA CAMERA";
                float tf = 1.6f;
                float lr = larguraTextoHUD(rMouse, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rMouse, cx - lr / 2.0f, mouseY + mouseBtnH + 14.0f, tf, glm::vec4(0.7f, 0.75f, 0.85f, 1.0f));
            }
            };

        if (estado == EstadoApp::MENU) {
            desenharRetanguloHUD(shaderHUD, hudVAO, 0.0f, 0.0f, (float)LARGURA, (float)ALTURA, glm::vec4(0.0f, 0.0f, 0.05f, 0.55f));
            desenharIconesInstrucoes((float)LARGURA / 2.0f, 100.0f);

            float pulso = 0.5f + 0.5f * sinf(currentFrame * 4.0f);
            float w = 320.0f, h = 46.0f;
            desenharRetanguloHUD(shaderHUD, hudVAO, (float)LARGURA / 2.0f - w / 2.0f, (float)ALTURA - 90.0f, w, h, glm::vec4(1.0f, 1.0f, 1.0f, pulso));
            {
                std::string rEnter = "ENTER = JOGAR";
                float tf = 2.6f;
                float lr = larguraTextoHUD(rEnter, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rEnter, (float)LARGURA / 2.0f - lr / 2.0f, (float)ALTURA - 90.0f + h / 2.0f - (7.0f * tf) / 2.0f, tf, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));
            }
        }

        if (estado == EstadoApp::PAUSADO) {
            // Info/instrucoes tambem disponivel na propria tela do jogo (ao pausar)
            desenharRetanguloHUD(shaderHUD, hudVAO, 0.0f, 0.0f, (float)LARGURA, (float)ALTURA, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
            // Icone "||" de pausa, acima do painel de instrucoes
            float pausaCx = (float)LARGURA / 2.0f, pausaCy = 40.0f;
            desenharRetanguloHUD(shaderHUD, hudVAO, pausaCx - 25.0f, pausaCy, 16.0f, 46.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            desenharRetanguloHUD(shaderHUD, hudVAO, pausaCx + 9.0f, pausaCy, 16.0f, 46.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            desenharIconesInstrucoes((float)LARGURA / 2.0f, 90.0f);
            float pulso = 0.5f + 0.5f * sinf(currentFrame * 4.0f);
            desenharRetanguloHUD(shaderHUD, hudVAO, (float)LARGURA / 2.0f - 150.0f, (float)ALTURA - 60.0f, 300.0f, 40.0f, glm::vec4(1.0f, 1.0f, 1.0f, pulso));
            {
                std::string rContinuar = "ESC = CONTINUAR";
                float tf = 2.2f;
                float lr = larguraTextoHUD(rContinuar, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rContinuar, (float)LARGURA / 2.0f - lr / 2.0f, (float)ALTURA - 60.0f + 20.0f - (7.0f * tf) / 2.0f, tf, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));
            }
        }
        else if (estado == EstadoApp::VITORIA || estado == EstadoApp::DERROTA) {
            float pulso = 0.5f + 0.5f * sinf(currentFrame * 4.0f);
            glm::vec3 corBase = (estado == EstadoApp::VITORIA) ? glm::vec3(1.0f, 0.85f, 0.2f) : glm::vec3(1.0f, 0.2f, 0.2f);
            desenharRetanguloHUD(shaderHUD, hudVAO, (float)LARGURA / 2.0f - 160.0f, (float)ALTURA - 100.0f, 320.0f, 46.0f, glm::vec4(corBase * pulso, 1.0f));
            {
                std::string rBarra = "TAB = VOLTAR AO EDITOR";
                float tf = 2.0f;
                float lr = larguraTextoHUD(rBarra, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rBarra, (float)LARGURA / 2.0f - lr / 2.0f, (float)ALTURA - 100.0f + 23.0f - (7.0f * tf) / 2.0f, tf, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));
            }
            {
                std::string rTitulo = (estado == EstadoApp::VITORIA) ? "VITORIA" : "TEMPO ESGOTADO";
                float tf = 3.5f;
                float lr = larguraTextoHUD(rTitulo, tf);
                desenharTextoHUD(shaderHUD, hudVAO, rTitulo, (float)LARGURA / 2.0f - lr / 2.0f, (float)ALTURA / 2.0f - 110.0f, tf, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                std::string rPontos = "PONTUACAO FINAL";
                float tf2 = 1.8f;
                float lr2 = larguraTextoHUD(rPontos, tf2);
                desenharTextoHUD(shaderHUD, hudVAO, rPontos, (float)LARGURA / 2.0f - lr2 / 2.0f, (float)ALTURA / 2.0f - 55.0f, tf2, glm::vec4(0.8f, 0.85f, 0.9f, 1.0f));
            }

            float W = 34.0f, H = 54.0f, esp = 8.0f;
            float largPontos = (pontuacao >= 10 ? (pontuacao >= 100 ? 3 : 2) : 1) * (W + esp * 2.2f);
            desenharRetanguloHUD(shaderHUD, hudVAO, (float)LARGURA / 2.0f - largPontos / 2.0f - 16.0f, (float)ALTURA / 2.0f - 40.0f, largPontos + 32.0f, H + 20.0f, glm::vec4(0.05f, 0.05f, 0.08f, 0.65f));
            desenharNumeroHUD(shaderHUD, hudVAO, pontuacao, (float)LARGURA / 2.0f - largPontos / 2.0f, (float)ALTURA / 2.0f - 30.0f, W, H, esp, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
        }

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
