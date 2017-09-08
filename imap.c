#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <fts.h>
#include <stdbool.h>
#include <signal.h>
#include "utils.c"

#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096

// Condições de uma resposta
typedef enum {OK, NO, BAD, PREAUTH, BYE} cond_t;

// Comandos válidos
typedef enum {CAPABILITY, IDLE, NOOP, LOGOUT,
              STARTTLS, AUTHENTICATE, LOGIN,
              SELECT, EXAMINE, CREATE, DELETE, RENAME, SUBSCRIBE, UNSUBSCRIBE, LIST, LSUB, STATUS, APPEND,
              CHECK, CLOSE, EXPUNGE, SEARCH, FETCH, STORE, COPY, UID} cmd_t;
char commands[][16] = {"CAPABILITY", "IDLE", "NOOP", "LOGOUT",
              "STARTTLS", "AUTHENTICATE", "LOGIN",
              "SELECT", "EXAMINE", "CREATE", "DELETE", "RENAME", "SUBSCRIBE", "UNSUBSCRIBE", "LIST", "LSUB", "STATUS", "APPEND",
              "CHECK", "CLOSE", "EXPUNGE", "SEARCH", "FETCH", "STORE", "COPY", "UID", ""};

// Estados da sessão
// Todos os comandos <= o estado são permitidos naquele estado
typedef enum {NOTAUTHENTICATED = LOGIN,
              AUTHENTICATED = APPEND,
              SELECTED = UID,
              LOGOUT_s} state_t;

// Linha de comando recebida
typedef struct {char tag[MAXLINE+1];    cmd_t cmd; char argv[10][MAXLINE+1]; int argc;} cmdline_t;

// BODYSTRUCTURE
typedef struct {char str[MAXLINE/2]; char *parts[10]; int nparts, psize[10];} bs_t;

// Mensagem armazenada
typedef struct {FTSENT *file; bool seen, deleted; int id; char *header, *text; int flines, fsize, hlines, hsize; char filepath[MAXLINE+1]; bs_t bs;} msg_t;

// Lista de logins válidos
char loginv[][2][MAXLINE+1] = {{"mriva@ime.usp.br", "password1"},
                              {"lmagno@ime.usp.br", "password2"}};
int loginc = 2;

// Sessão
typedef struct {int pid, connfd; char *user; state_t state; msg_t messages[10]; int exists, unseen; bool idle; char idletag[MAXLINE+1];} session_t;

//========================================= FUNÇÕES =========================================
cmd_t findcmd(char const name[MAXLINE+1]);
msg_t parse_title(FTSENT *file);
void parse_msg(msg_t *msg, session_t *session);
void parse_mime(char *line, char **structure);
void upd_flags(msg_t *msg);

void respond(char const *tag, char const *status, char const *message, session_t *session);
void cmd_login(cmdline_t cmdline, session_t *session);
void cmd_select(cmdline_t cmdline, session_t *session);
void cmd_list(cmdline_t cmdline, session_t *session);
void cmd_fetch(cmdline_t cmdline, session_t *session);
void cmd_uid(cmdline_t cmdline, session_t *session);
void cmd_store(cmdline_t cmdline, session_t *session);


void cmd_uid(cmdline_t cmdline, session_t *session) {
    char cmd[MAXLINE+1];

    // Atualiza argumentos
    strcpy(cmd, cmdline.argv[0]);
    for(int i = 0; i < cmdline.argc-1; i++)
        strcpy(cmdline.argv[i], cmdline.argv[i+1]);

    cmdline.argc--;

    // Só fetch e store foram implementados
    uppercase(cmd);
    if(!strcmp("FETCH", cmd)) {
        cmdline.cmd = FETCH;
        cmd_fetch(cmdline, session);
    } else if(!strcmp("STORE", cmd)) {
        cmdline.cmd = STORE;
        cmd_store(cmdline, session);
    } else {
        respond(cmdline.tag, "BAD", "UID Comando não implementado.", session);
    }
}

void cmd_fetch(cmdline_t cmdline, session_t *session) {
    char *token, *saveptr, *options, *prev, *next;
    int a, b; // Range
    int i;
    bool asterisk;
    bool flags, size, body, peek, header, bstruct;
    char tmp[MAXLINE+1], resp[MAXLINE+1];
    msg_t msg;

    // Checa número de argumentos
    if(cmdline.argc != 2) {
        respond(cmdline.tag, "BAD", "FETCH Argumentos inválidos", session);
        return;
    }

    // Determina quais mensagens foram pedidas
    char *range = cmdline.argv[0];
    if(strchr(range, ':') == NULL) {
        // Só foi pedido uma única mensagem
        a = b = atoi(range);
    } else {
        // Range
        asterisk = (strchr(range, '*') != NULL);

        token = strtok_r(range,":", &saveptr);
        a = atoi(token);

        token = strtok_r(NULL,"\r\n", &saveptr);
        b = atoi(token);

        if(b == 0) b = INT32_MAX;
    }

    // Opções
    options = uppercase(cmdline.argv[1]);
      flags = (strstr(options, "FLAGS") != NULL);
       size = (strstr(options, "RFC822.SIZE") != NULL);
       body = (strstr(options, "BODY") != NULL);
       peek = (strstr(options, "PEEK") != NULL);
     header = (strstr(options, "HEADER") != NULL);
    bstruct = (strstr(options, "BODYSTRUCTURE") != NULL);


    // Só mantém o que está entre colchetes
    if(body)
        options = strchr(options, '[');


    for(i = 0; i < session->exists; i++) {
        msg = session->messages[i];

        // Filtra as mensagens com ID dentro do intervalo
        // e a última se houver um asterisco
        if((msg.id >= a && msg.id <= b) || (i == session->exists-1 && asterisk)) {
            // BODYSTRUCTURE
            if(bstruct) {
                sprintf(tmp, "%d FETCH (UID %d BODYSTRUCTURE %s)", msg.id, msg.id, msg.bs.str);
                respond("*", tmp, NULL, session);
                continue;
            }

            // UID
            sprintf(tmp, "UID %d", msg.id);

            // Size
            if(size) sprintf(tmp+strlen(tmp), " RFC822.SIZE %d", msg.fsize);

            // Flags
            if(flags) {
                sprintf(tmp+strlen(tmp), " FLAGS (");
                if(msg.deleted) sprintf(tmp+strlen(tmp), " \\Deleted");
                if(msg.seen) sprintf(tmp+strlen(tmp), " \\Seen");
                sprintf(tmp+strlen(tmp), ")");
            }

            if(!body) {
                // Só responde de volta
                sprintf(resp, "%d FETCH (%s)", msg.id, tmp);
                respond("*", resp, NULL, session);
            } else {
                if(header) {
                    // Retorna só o header
                    sprintf(resp, "%d FETCH (%s BODY%s {%d}", msg.id, tmp, options, msg.hsize);
                    respond("*", resp, NULL, session);

                    // Envia o header uma linha por vez
                    prev = msg.header;
                    next = strchr(prev, '\n');
                    while(next != NULL) {
                        snprintf(resp, strlen(prev)-strlen(next), "%s", prev);
                        respond(NULL, NULL, resp, session);

                        prev = next+1;
                        next = strchr(next+1, '\n');
                    }
                    respond(NULL, prev, ")", session);
                    // respond(NULL, ")", NULL, session);

                } else {
                    // Retorna o arquivo todo
                    sprintf(resp, "%d FETCH (%s BODY%s {%d}", msg.id, tmp, options, msg.fsize);
                    respond("*", resp, NULL, session);

                    // Envia o arquivo uma linha por vez
                    prev = msg.text;
                    next = strchr(prev, '\n');
                    while(next != NULL) {
                        snprintf(resp, strlen(prev)-strlen(next), "%s", prev);
                        respond(NULL, NULL, resp, session);

                        prev = next+1;
                        next = strchr(next+1, '\n');
                    }
                    respond(NULL, NULL, prev, session);
                    respond(NULL, ")", NULL, session);
                }

                // Marca a mensagem como lida
                if(!peek) {
                    msg.seen = true;
                    upd_flags(&msg);
                }
            }
        }
    }

    respond(cmdline.tag, "OK", "FETCH Completado", session);
}

void cmd_store(cmdline_t cmdline, session_t *session) {
    int id, i;
    bool seen, deleted, mark;
    char *flags;
    msg_t *msg;

    // Checa número de argumentos
    if(cmdline.argc != 3) {
        respond(cmdline.tag, "BAD", "STORE Argumentos inválidos", session);
        return;
    }

    // Encontra a mensagem especificada
    id = atoi(cmdline.argv[0]);
    for(i = 0; i < session->exists; i++) {
        msg = &session->messages[i];
        if(msg->id == id)
            break;
    }

    // Verifica se o comando é para adicionar uma flag
    mark = (cmdline.argv[1][0] == '+');

    // Determina quais flags devem ser gravadas
      flags = cmdline.argv[2];
       seen = (strstr(flags, "\\Seen") != NULL);
    deleted = (strstr(flags, "\\Deleted") != NULL);

    // Marca ou desmarca as flags pedidas
    if(seen)    msg->seen    = mark;
    if(deleted) msg->deleted = mark;

    upd_flags(msg);

    respond(cmdline.tag, "OK", "STORE completed", session);
}

void cmd_list(cmdline_t cmdline, session_t *session) {
    char *dir = cmdline.argv[1];
    if(!strcmp(dir, "*") || !strcmp(dir, "INBOX")) {
        respond("*", "LIST", "() \"\" INBOX", session);
    }
    respond(cmdline.tag, "OK", "LIST completado.", session);
}

void cmd_select(cmdline_t cmdline, session_t *session) {
    char inbox[MAXLINE+1], resp[MAXLINE+1];
    char path[MAXLINE+1];
    FTS *dir;
    FTSENT *file, *children;
    int fts_options = FTS_LOGICAL | FTS_NOCHDIR;
    msg_t msg;

    // Checa argumentos
    if(cmdline.argc != 1) {
        respond(cmdline.tag, "BAD", "Argumentos inválidos.", session);
        return;
    }

    // Só existe a pasta INBOX
    unquote(inbox, cmdline.argv[0], '\"', '\"');
    if(strcmp(inbox, "INBOX") != 0) {
        respond(cmdline.tag, "NO", "Não existe esse diretório.", session);
        return;
    }

    // Flags
    respond("*", "FLAGS", "(\\Deleted \\Seen)", session);
    respond("*", "OK", "[PERMANENTFLAGS (\\Deleted \\Seen)]", session);

    // Vê as mensagens existentes
    sprintf(path, "%s/Maildir/cur", session->user);

    char* const argv[] = {path, NULL};
    if((dir = fts_open(argv, fts_options, NULL)) == NULL) {
        perror("Não foi possível abrir o diretório 'cur'.\n");
        exit(7);
    }

    session->exists = 0; // Inicia contador de mensagens
    children = fts_children(dir, 0);
    if(children != NULL) {
        while((file = fts_read(dir)) != NULL) {
            if(file->fts_info != FTS_F) continue;

            // Se é um arquivo, processa seu conteúdo e guarda
            // na sessão
            msg = parse_title(file);
            parse_msg(&msg, session);
            session->messages[session->exists++] = msg;
        }
    }


    // Número de mensagens existentes
    sprintf(resp, "%d", session->exists);
    respond("*", resp, "EXISTS", session);
    respond("*", "0", "RECENT", session);

    // Primeira não-lida e provável próxima
    session->unseen = 0;
    for(int i = 0; i < session->exists; i++) {
        if(!session->messages[i].seen) {
            session->unseen = session->messages[i].id;
            break;
        }
    }

    if(session->unseen > 0) {
        sprintf(resp, "[UNSEEN %d]", session->unseen);
        respond("*", "OK", resp, session);
    }

    if(session->unseen + 1 < session->exists) {
        sprintf(resp, "[UIDNEXT %d]", session->unseen+1);
        respond("*", resp, NULL, session);
    }

    // Finaliza
    respond(cmdline.tag, "OK", "[READ-WRITE] SELECT completado", session);

    session->state = SELECTED;
    fts_close(dir);
}

void cmd_login(cmdline_t cmdline, session_t *session) {
    int i;
    char *login, *password;

    // Checa se os argumentos estão corretos
    if(cmdline.argc != 2) {
        respond(cmdline.tag, "BAD", "Argumentos inválidos.", session);
        return;
    }

    // Verifica se o par (login, senha) se encontra na lista de logins
    login    = cmdline.argv[0];
    password = cmdline.argv[1];

    // Remove aspas
    unquote(login, login, '\"', '\"');
    unquote(password, password, '\"', '\"');

    for(i = 0; i < loginc; i++) {
        if(!strcmp(login, loginv[i][0]) && !strcmp(password, loginv[i][1])) {
            // O login é válido

            respond(cmdline.tag, "OK", "LOGIN", session);
            session->user = loginv[i][0];
            session->state = AUTHENTICATED;
            return;
        }
    }

    // O login é inválido se não está na lista
    respond(cmdline.tag, "NO", "LOGIN", session);
    return;
}

// Extrai as flags de uma mensagem a partir de seu título
msg_t parse_title(FTSENT *file) {
    char *token, *saveptr, filename[MAXLINE+1];
    msg_t msg;
    int i;

    msg.file = file;
    strcpy(filename, file->fts_name);

    // Primeira parte do nome é o id
    token = strtok_r(filename, ":", &saveptr);
    msg.id = atoi(token);

    // Versão do Maildir
    token = strtok_r(NULL, ",", &saveptr);

    // Flags
    msg.seen = false;
    msg.deleted = false;
    token = strtok_r(NULL, "\n\r", &saveptr);
    if(!token)
        return msg;

    for(i = 0; i < (int)strlen(token); i++) {
        switch(token[i]) {
            case 'S':
                msg.seen = true;
                break;
            case 'D':
                msg.deleted = true;
                break;
            default:
                break;
        }
    }

    return msg;
}

// Envia uma linha de resposta para o cliente
// e imprime o log localmente
void respond(char const *tag, char const *status, char const *message, session_t *session) {
    char resp[MAXLINE+1];

    // Escreve a linha de resposta pro cliente
    resp[0] = 0;
    if(tag) sprintf(resp+strlen(resp), "%s ", tag);
    if(status) sprintf(resp+strlen(resp), "%s ", status);
    if(message) sprintf(resp+strlen(resp), "%s ", message);

    sprintf(resp+strlen(resp)-1, "\r\n");
    write(session->connfd, resp, strlen(resp));

    // Imprime localmente a resposta
    printf("%d S: %s", session->pid, resp);
}

// Retorna o ID do comando a partir do nome
// (-1 se não for encontrado na lista)
cmd_t findcmd(char const name[MAXLINE+1]) {
    int i;

    // Encontra o comando na lista
    for(i = 0; strcmp(commands[i], ""); i++)
        if(!strcmp(name, commands[i])) {
            return (cmd_t)i;
        }

    return (cmd_t)-1;
}

// Extrai o conteúdo de uma mensagem e armazena de forma estruturada,
// para que não seja necessário abrir o arquivo referente novamente
void parse_msg(msg_t *msg, session_t *session) {
    FILE *file;
    char line[MAXLINE+1], parts[10][MAXLINE+1], boundary[MAXLINE+1],
    lang[MAXLINE+1], disposition[MAXLINE+1], type[MAXLINE+1], encoding[MAXLINE+1],
    filename[MAXLINE+1];
    char *s;
    bool header, multipart, content, text;
    int part, plines;

    // Pega o caminho até o arquivo
    sprintf(msg->filepath, "%s/Maildir/cur/%s", session->user, msg->file->fts_name);

    // Abre o arquivo
    file = fopen(msg->filepath, "r");
    if(!file) {
        perror(filename);
        exit(9);
    }

    // Aloca espaço para guardar o arquivo todo
    msg->fsize = msg->file->fts_statp->st_size;
    msg->text = (char*)malloc((msg->fsize+1)*sizeof(char));
    msg->text[0] = 0;

    // Calcula o tamanho do header, a quantidade de linhas
    // e armazena a BODYSTRUCTURE
    msg->flines = 0;
    msg->hlines = 0;
    msg->hsize = 0;
    header = true; multipart = false; content = false;
    part = 0;
    msg->bs.psize[part] = 0;
    while(fgets(line, MAXLINE, file) != NULL) {
        strcat(msg->text, line);
        msg->flines++;

        if(header) {
            msg->hsize += strlen(line);
            msg->hlines++;
        }

        // BODYSTRUCTURE
        if(strstr(line, "boundary")) {
            // Divisão entre as partes da mensagem
            unquote(boundary, line, '\"', '\"');
            header = false;

        } else if(strstr(line, "Content-Language")) {
            // O idioma do texto
            trim(lang, strchr(line, ' '));

        } else if(strstr(line, "Content-Type")) {
            // Tipo da parte
            if(!multipart) header = false;

            // A mensagem tem mais de uma parte
            if(strstr(line, "multipart/mixed")) {
                multipart = true;
                continue;
            }

            // Encontrou o começo de uma parte
            part++;
            msg->bs.psize[part] = 0;
            plines = 0;
            if(strstr(line, "text/plain")) {
                strcpy(type, "\"text\" \"plain\" (\"charset\" \"utf-8\" \"format\" \"flowed\")");
                text = true;
            }

            if (strstr(line, "application/pdf")) {
                // Nome do arquivo
                unquote(filename, line, '\"', '\"');

                sprintf(type, "\"application\" \"pdf\" (\"name\" \"%s\")", filename);
                text = false;
            }

        }

        if(strstr(line, "Content-Disposition")) {
            strcpy(disposition, "(\"attachment\" (\"filename\" \"enunciado.pdf\"))");
        }

        if (strstr(line, "Content-Transfer-Encoding")) {
            // Codificação da parte

            trim(encoding, strchr(line, ' '));

            // Começa o conteúdo de fato
            content = true;
            msg->bs.parts[part] = &(msg->text[strlen(msg->text)+1]);
        }

        // Registra o tamanho do conteúdo
        if(content) {
            if(multipart && strstr(line, boundary)) {
                // O conteúdo termina na divisão
                content = false;
                if(text)
                    sprintf(parts[part], "%s NIL NIL \"%s\" %d %d NIL NIL NIL NIL", type, encoding, msg->bs.psize[part], plines);
                else
                    sprintf(parts[part], "%s NIL NIL \"%s\" %d NIL %s NIL NIL", type, encoding, msg->bs.psize[part], disposition);

            } else {
                plines++;
                msg->bs.psize[part] += strlen(line);
            }
        }
    }

    // Grava a quantidade de partes
    msg->bs.nparts = part;
    int i;
    s = msg->bs.str;

    if(!multipart) {
        sprintf(s, "%s NIL NIL \"%s\" %d %d NIL NIL NIL NIL", type, encoding, msg->bs.psize[1], plines);
    } else {
        sprintf(s, "(");

        for(i = 1; i <= part; i++)
            sprintf(s+strlen(s), "(%s)", parts[i]);

        sprintf(s+strlen(s), " \"mixed\" (\"boundary\" \"%s\") NIL (\"%s\") NIL)", boundary, lang);
    }

    // Aloca espaço para o header
    msg->header = (char*)malloc((msg->hsize+1)*sizeof(char));
    strncpy(msg->header, msg->text, msg->hsize);
    msg->header[msg->hsize] = 0;

    // Volta para o início do arquivo
    fclose(file);
}

// Atualiza o nome do arquivo com as
// flags da mensagem
void upd_flags(msg_t *msg) {
    char *comma;
    char newfp[MAXLINE+1];

    strcpy(newfp, msg->filepath);
    comma = strchr(newfp, ',');
    comma[1] = 0;

    if(msg->seen)    strcat(newfp, "S");
    if(msg->deleted) strcat(newfp, "D");

    rename(msg->filepath, newfp);
    strcpy(msg->filepath, newfp);
}
