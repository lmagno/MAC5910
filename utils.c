#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// Transforma todas as letras da string 's' em maiúsculas
char* uppercase(char *s) {
    int i;
    char c;

    for(i = 0; i < (int)strlen(s); i++) {
        c = s[i];
        if(isalpha(c)){
            s[i] = toupper(c);
        }
    }

    return s;
}

// Remove whitespace no começo e no final da string 'src'
// gravando o resultado em 'dest'
void trim(char *dest, char *src) {
    int i, j, k;
    char ws[6] = " \r\n\t";

    strcpy(ws, " \r\n\t");
    // Encontra o primeiro whitespace à direita
    for(j = strlen(src)-1; j >=0; j--)
        if(strchr(ws, src[j]) == NULL) break;

    // Encontra o último à esquerda
    for(i = 0; i < (int)strlen(src); i++)
        if(strchr(ws, src[i]) == NULL) break;

    // Copia os elementos entre eles
    for(k = 0; k+i <= j; k++)
        dest[k] = src[k + i];

    dest[k] = 0;
}

// Copia todos os caracteres da string 'src' entre os delimitadores
// de 'l' e 'r', gravando o resultado em 'dest'.
// Se forem o mesmo delimitador, o resultado é a substring entre a primeira e a
// segunda ocorrência do mesmo.
// Se forem diferentes, o resultado é a substring entre o primeiro par (l, r)
// de delimitadores balanceados (mesma quantidade de 'l' e 'r' na substring).
void unquote(char *dest, char *src, char l, char r) {
    int i, par;
    char *s, *lp, *rp, c;
    bool diff = (l != r);

    lp = strchr(src, l);
    rp = strchr(src, r);
    if((lp == NULL) || (rp == NULL) || (strlen(lp) < strlen(rp))) {
        // Não há nada para ser alterado
        strcpy(dest, src);
        return;
    }

    s = (char*)malloc((strlen(src)+1)*sizeof(char));

    lp++;
    par = 1;
    i = 0;
    while (true) {
        c = lp[i];
        if(c == r) {
            par--;
            // Termina se encontrou o delimitador
            // e se estiver balanceado no caso de serem diferentes
            if(!diff || (diff && (par == 0))) break;
        } else if (c == l) {
            par++;
        }
        s[i++] = c;
    }
    s[i] = 0;

    strcpy(dest, s);
    free(s);
}
