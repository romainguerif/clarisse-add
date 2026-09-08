// Sonde d'edition de liens : les symboles GLEW reexportes par ix_glutils.lib
// se resolvent-ils depuis du code a nous ?
//
// Le .def de la bibliotheque en annonce 2 735, dont glDispatchCompute et les
// SSBO. Mais un nom dans une table d'exports ne prouve pas qu'on peut s'y lier :
// glew.h n'est pas livre avec le SDK reconstruit, donc il faut redeclarer les
// pointeurs a la main, et c'est la que l'ABI peut trahir.
//
// GLEW expose ses fonctions comme des POINTEURS DE DONNEE exportes
// (__glewXxx), pas comme des fonctions. Une donnee importee depuis une DLL se
// declare __declspec(dllimport) ; sans ca, MSVC cherche un symbole local et
// echoue. C'est le seul piege.
//
// On ne les APPELLE pas : hors contexte GL courant ils seraient nuls. On force
// seulement le linker a les resoudre, et on imprime leur adresse.

#include <cstdio>

typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef char GLchar;
typedef unsigned char GLboolean;

extern "C" {
    __declspec(dllimport) void (*__glewDispatchCompute)(GLuint, GLuint, GLuint);
    __declspec(dllimport) GLuint (*__glewCreateShader)(GLenum);
    __declspec(dllimport) void (*__glewShaderSource)(GLuint, int, const GLchar* const*, const GLint*);
    __declspec(dllimport) void (*__glewUseProgram)(GLuint);
    __declspec(dllimport) void (*__glewGenBuffers)(int, GLuint*);
    __declspec(dllimport) void (*__glewBindBufferBase)(GLenum, GLuint, GLuint);
    __declspec(dllimport) void (*__glewMemoryBarrier)(GLuint);
    __declspec(dllimport) void (*__glewGenFramebuffers)(int, GLuint*);
    __declspec(dllimport) void (*__glewBindImageTexture)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
    __declspec(dllimport) GLboolean __GLEW_ARB_compute_shader;
    __declspec(dllimport) GLboolean __GLEW_VERSION_4_3;
    __declspec(dllimport) GLenum glewInit(void);
    __declspec(dllimport) GLboolean glewIsSupported(const char *name);
}

int main()
{
    // Prendre l'adresse suffit a forcer la resolution a l'edition de liens.
    const void *syms[] = {
        (const void *)&__glewDispatchCompute,
        (const void *)&__glewCreateShader,
        (const void *)&__glewShaderSource,
        (const void *)&__glewUseProgram,
        (const void *)&__glewGenBuffers,
        (const void *)&__glewBindBufferBase,
        (const void *)&__glewMemoryBarrier,
        (const void *)&__glewGenFramebuffers,
        (const void *)&__glewBindImageTexture,
        (const void *)&__GLEW_ARB_compute_shader,
        (const void *)&__GLEW_VERSION_4_3,
        (const void *)&glewInit,
        (const void *)&glewIsSupported,
    };
    static const char *names[] = {
        "glDispatchCompute", "glCreateShader", "glShaderSource", "glUseProgram",
        "glGenBuffers", "glBindBufferBase", "glMemoryBarrier",
        "glGenFramebuffers", "glBindImageTexture",
        "GLEW_ARB_compute_shader", "GLEW_VERSION_4_3",
        "glewInit", "glewIsSupported" };

    printf("Symboles resolus depuis ix_glutils.dll :\n");
    for (int i = 0; i < 13; i++)
        printf("  %-26s -> %p\n", names[i], syms[i]);

    // Les pointeurs de fonction GLEW sont nuls tant que glewInit n'a pas tourne
    // dans un contexte GL courant. On l'affiche pour que ce soit explicite.
    printf("\nValeur du pointeur glDispatchCompute avant glewInit : %p\n",
           (void *)__glewDispatchCompute);
    printf("(nul est attendu hors contexte GL : ce test verifie la LIAISON,\n"
           " pas la disponibilite du pilote)\n");
    return 0;
}
