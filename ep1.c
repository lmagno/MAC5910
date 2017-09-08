/* Por Prof. Daniel Batista <batista@ime.usp.br>
 * Em 9/8/2017
 *
 * Um código simples (não é o código ideal, mas é o suficiente para o
 * EP) de um servidor de eco a ser usado como base para o EP1. Ele
 * recebe uma linha de um cliente e devolve a mesma linha. Teste ele
 * assim depois de compilar:
 *
 * ./servidor 8000
 *
 * Com este comando o servidor ficará escutando por conexões na porta
 * 8000 TCP (Se você quiser fazer o servidor escutar em uma porta
 * menor que 1024 você precisa ser root).
 *
 * Depois conecte no servidor via telnet. Rode em outro terminal:
 *
 * telnet 127.0.0.1 8000
 *
 * Escreva sequências de caracteres seguidas de ENTER. Você verá que
 * o telnet exibe a mesma linha em seguida. Esta repetição da linha é
 * enviada pelo servidor. O servidor também exibe no terminal onde ele
 * estiver rodando as linhas enviadas pelos clientes.
 *
 * Obs.: Você pode conectar no servidor remotamente também. Basta saber o
 * endereço IP remoto da máquina onde o servidor está rodando e não
 * pode haver nenhum firewall no meio do caminho bloqueando conexões na
 * porta escolhida.
 */

#define _GNU_SOURCE
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
              "CHECK", "CLOSE", "EXPUNGE", "SEARCH", "FETCH", "STORE", "COPY", "UID"};

// Estados da sessão
// Todos os comandos <= o estado são permitidos naquele estado
typedef enum {NOTAUTHENTICATED = LOGIN,
              AUTHENTICATED = APPEND,
              SELECTED = UID,
              LOGOUT_s} state_t;

// Linha de comando recebida
typedef struct {char tag[MAXLINE+1];    cmd_t cmd; char argv[10][MAXLINE+1]; int argc;} cmdline_t;

// Resposta enviada
typedef struct {char tag[MAXLINE+1];  cond_t cond; char text[MAXLINE+1];} resp_t;

// Mensagem armazenada
typedef struct {FTSENT *file; bool seen, deleted; int id; char *header, *text; int flines, fsize, hlines, hsize; char bodystructure[MAXLINE+1], filepath[MAXLINE+1];} msg_t;

// Lista de logins válidos
char loginv[][2][MAXLINE+1] = {{"user1@localhost", "password1"},
                              {"user2@localhost", "password2"}};
int loginc = 2;

// Sessão
typedef struct {int pid, connfd; char *user; state_t state; msg_t messages[10]; int exists, unseen; bool idle; char idletag[MAXLINE+1];} session_t;

char* uppercase(char *s);
char* unquote(char *s);
cmd_t findcmd(char const name[MAXLINE+1]);
msg_t parse_title(FTSENT *file);
void parse_msg(msg_t *msg, session_t *session);
void parse_mime(char *line, char **structure);
void mark_seen(msg_t *msg);
void mark_deleted(msg_t *msg);

void respond(char const *tag, char const *status, char const *message, session_t *session);
void cmd_login(cmdline_t cmdline, session_t *session);
void cmd_select(cmdline_t cmdline, session_t *session);
void cmd_list(cmdline_t cmdline, session_t *session);
void cmd_fetch(cmdline_t cmdline, session_t *session);
void cmd_uid(cmdline_t cmdline, session_t *session);
void cmd_store(cmdline_t cmdline, session_t *session);

int main (int argc, char **argv) {
   /* Os sockets. Um que será o socket que vai escutar pelas conexões
    * e o outro que vai ser o socket específico de cada conexão */
	int listenfd, connfd;
   /* Informações sobre o socket (endereço e porta) ficam nesta struct */
	struct sockaddr_in servaddr;
   /* Retorno da função fork para saber quem é o processo filho e quem
    * é o processo pai */
   pid_t childpid;
   /* Armazena linhas recebidas do cliente */
	char	recvline[MAXLINE + 1];
   /* Armazena o tamanho da string lida do cliente */
   ssize_t  n;

   // Ignora SIGPIPE
   signal(SIGPIPE, SIG_IGN);

	if (argc != 2) {
      fprintf(stderr,"Uso: %s <Porta>\n",argv[0]);
      fprintf(stderr,"Vai rodar um servidor de echo na porta <Porta> TCP\n");
		exit(1);
	}

   /* Criação de um socket. Eh como se fosse um descritor de arquivo. Eh
    * possivel fazer operacoes como read, write e close. Neste
    * caso o socket criado eh um socket IPv4 (por causa do AF_INET),
    * que vai usar TCP (por causa do SOCK_STREAM), já que o IMAP
    * funciona sobre TCP, e será usado para uma aplicação convencional sobre
    * a Internet (por causa do número 0) */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket :(\n");
		exit(2);
	}

   /* Agora é necessário informar os endereços associados a este
    * socket. É necessário informar o endereço / interface e a porta,
    * pois mais adiante o socket ficará esperando conexões nesta porta
    * e neste(s) endereços. Para isso é necessário preencher a struct
    * servaddr. É necessário colocar lá o tipo de socket (No nosso
    * caso AF_INET porque é IPv4), em qual endereço / interface serão
    * esperadas conexões (Neste caso em qualquer uma -- INADDR_ANY) e
    * qual a porta. Neste caso será a porta que foi passada como
    * argumento no shell (atoi(argv[1]))
    */
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(atoi(argv[1]));
	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		perror("bind :(\n");
		exit(3);
	}


   /* Como este código é o código de um servidor, o socket será um
    * socket passivo. Para isto é necessário chamar a função listen
    * que define que este é um socket de servidor que ficará esperando
    * por conexões nos endereços definidos na função bind. */
	if (listen(listenfd, LISTENQ) == -1) {
		perror("listen :(\n");
		exit(4);
	}

   printf("[Servidor no ar. Aguardando conexoes na porta %s]\n",argv[1]);
   printf("[Para finalizar, pressione CTRL+c ou rode um kill ou killall]\n");

   /* O servidor no final das contas é um loop infinito de espera por
    * conexões e processamento de cada uma individualmente */
	for (;;) {
      /* O socket inicial que foi criado é o socket que vai aguardar
       * pela conexão na porta especificada. Mas pode ser que existam
       * diversos clientes conectando no servidor. Por isso deve-se
       * utilizar a função accept. Esta função vai retirar uma conexão
       * da fila de conexões que foram aceitas no socket listenfd e
       * vai criar um socket específico para esta conexão. O descritor
       * deste novo socket é o retorno da função accept. */
		if ((connfd = accept(listenfd, (struct sockaddr *) NULL, NULL)) == -1 ) {
			perror("accept :(\n");
			exit(5);
		}


      /* Agora o servidor precisa tratar este cliente de forma
       * separada. Para isto é criado um processo filho usando a
       * função fork. O processo vai ser uma cópia deste. Depois da
       * função fork, os dois processos (pai e filho) estarão no mesmo
       * ponto do código, mas cada um terá um PID diferente. Assim é
       * possível diferenciar o que cada processo terá que fazer. O
       * filho tem que processar a requisição do cliente. O pai tem
       * que voltar no loop para continuar aceitando novas conexões */
      /* Se o retorno da função fork for zero, é porque está no
       * processo filho. */
      if ( (childpid = fork()) == 0) {
         /**** PROCESSO FILHO ****/
         printf("[Uma conexao aberta]\n");
         /* Já que está no processo filho, não precisa mais do socket
          * listenfd. Só o processo pai precisa deste socket. */
         close(listenfd);

         /* Agora pode ler do socket e escrever no socket. Isto tem
          * que ser feito em sincronia com o cliente. Não faz sentido
          * ler sem ter o que ler. Ou seja, neste caso está sendo
          * considerado que o cliente vai enviar algo para o servidor.
          * O servidor vai processar o que tiver sido enviado e vai
          * enviar uma resposta para o cliente (Que precisará estar
          * esperando por esta resposta)
          */

         /* ========================================================= */
         /* ========================================================= */
         /*                         EP1 INÍCIO                        */
         /* ========================================================= */
         /* ========================================================= */
         /* TODO: É esta parte do código que terá que ser modificada
          * para que este servidor consiga interpretar comandos IMAP  */

		// Comandos a ser implementados:
		//    * LOGIN: login
        //    * LIST: listar mensagens
        //    * marcar mensagem como não lida
        //    * FETCH: download de anexos
        //    * apagar mensagens
        //    * LOGOUT: logout


        char *token;
        char *saveptr;
        char *c; // ponteiro para a posição equivalente de 'input' em 'recvline'
        char input[MAXLINE+1];
        char resp[MAXLINE+1];
        cmd_t cmd;
        session_t session = {getpid(), connfd, NULL, NOTAUTHENTICATED};

        respond("*", "OK", "[CAPABILITY IMAP4rev1]", &session);
        while ((n=read(session.connfd, recvline, MAXLINE)) > 0) {
            recvline[n]=0;

            printf("%d C: %s", session.pid, recvline);

            // Termina o IDLE
            if(!strncmp(recvline, "DONE", 4)) {
                session.idle = false;

                respond(session.idletag, "OK", "IDLE Completed", &session);
                continue;
            }

            // Copia a linha para manter uma cópia intacta
            strcpy(input, recvline);
            c = recvline;

            // struct que vai guardar a linha recebida
            cmdline_t cmdline;



            // Registra a tag da linha
            token = strtok_r(input, " \t\n\r", &saveptr);
            strcpy(cmdline.tag, token);
            c += strlen(token);

            // Identifica o comando
            token = strtok_r(NULL, " \t\n\r", &saveptr);
            c += strlen(token)+1;

            cmd = findcmd(uppercase(token));
            if(cmd == -1) {
                sprintf(resp, "%s Comando inválido", token);
                respond(cmdline.tag, "BAD", resp, &session);
                continue;
            } else if ((int)cmd > (int)session.state) {
                sprintf(resp, "%s Comando não permitido", token);
                respond(cmdline.tag, "BAD", resp, &session);
                continue;
            } else {
                cmdline.cmd = cmd;
            }


            // Lê o restante dos argumentos
            int i = 0, j;
            int par;
            while((token = strtok_r(NULL, " \t\n\r", &saveptr)) != NULL) {
                strcpy(cmdline.argv[i++], token);
                c += strlen(token)+1;

                // Verifica se existe um abre parênteses,
                // nesse caso tudo até o próximo fecha parênteses
                // é um único argumento
                if((saveptr != NULL) && saveptr[0] == '(') {
                    // Encontra o próximo ')'
                    // "par" indica a diferença entre '(' e ')' encontrados
                    par = 1;
                    j = 0;
                    while(par != 0) {
                        j++;
                        if(saveptr[j] == '(') par++;
                        else if(saveptr[j] == ')') par--;
                    }
                    // Salva o bloco input[saveptr, ..., saveptr+(j-1)) como argumento
                    saveptr[j] = 0;
                    strcpy(cmdline.argv[i++], saveptr+1);
                    c += j;
                    // Reinicia o token
                    saveptr += j+1;
                }

            }

            cmdline.argc = i;

            // fprintf(stdout, "tag: '%s'\n", cmdline.tag);
            // fprintf(stdout, "cmd: '%s'\n", commands[cmdline.cmd]);
            // for(i = 0; i < cmdline.argc; i++)
            //     fprintf(stdout, "arg %d: '%s'\n", i, cmdline.argv[i]);

            // Decide o que fazer dependendo do comando
            switch(cmdline.cmd) {
                case AUTHENTICATE:
                    respond(cmdline.tag, "NO", "SAI DAQUE", &session);
                    break;

                case LOGIN:
                    cmd_login(cmdline, &session);
                    break;

                case LIST:
                    // Lista as pastas
                    cmd_list(cmdline, &session);
                    break;

                case LSUB:
                    respond("*", "LSUB", "() \"\" INBOX", &session);
                    respond(cmdline.tag, "OK", "LSUB completado.", &session);
                    break;

                case SELECT:
                    // Seleciona diretório
                    cmd_select(cmdline, &session);
                    break;

                case UID:
                    cmd_uid(cmdline, &session);
                    break;

                case LOGOUT:
                    // Faz o logout
                    respond("*", "BYE", "LOGOUT", &session);
                    respond(cmdline.tag, "OK", "LOGOUT", &session);

                    break;
                case FETCH:
                    cmd_fetch(cmdline, &session);
                    break;

                case STORE:
                    cmd_store(cmdline, &session);
                    break;

                case IDLE:
                    session.idle = true;
                    strcpy(session.idletag, cmdline.tag);
                    respond("+", "idling", NULL, &session);
                    break;

                case NOOP:
                    respond(cmdline.tag, "OK", "NOOP Completed", &session);
                    break;

                default:
                    // Comando não implementado
                    respond("BAD", commands[cmdline.cmd], "Comando não implementado.", &session);
                    break;
            }

            // write(session.connfd, recvline, strlen(recvline));
        }
         /* ========================================================= */
         /* ========================================================= */
         /*                         EP1 FIM                           */
         /* ========================================================= */
         /* ========================================================= */

         /* Após ter feito toda a troca de informação com o cliente,
          * pode finalizar o processo filho */
         printf("[Uma conexao fechada]\n");
         int i;
         for(i = 0; i < session.exists; i++) {
             free(session.messages[i].text);
             free(session.messages[i].header);
         }
         close(session.connfd);
         exit(0);
      }
      /**** PROCESSO PAI ****/
      /* Se for o pai, a única coisa a ser feita é fechar o socket
       * connfd (ele é o socket do cliente específico que será tratado
       * pelo processo filho) */
	   close(connfd);
	}
	exit(0);
}

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

    // Range
    char *range = cmdline.argv[0];
    if(strchr(range, ':') == NULL) {
        // Só foi pedido uma única mensagem
        a = b = atoi(range);
    } else {
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

    if(bstruct) {
        respond(cmdline.tag, "BAD", "FETCH BODYSTRUCTURE não implementado.", session);
    }
    // Só mantém o que está entre colchetes
    if(body)
        options = strchr(options, '[');


    for(i = 0; i < session->exists; i++) {
        msg = session->messages[i];

        // Filtra as mensagens com ID dentro do intervalo
        // e a última se houver um asterisco
        if((msg.id >= a && msg.id <= b) || (i == session->exists-1 && asterisk)) {
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
                // Retorna o email de fato (ou só o cabeçalho)
                if(header) {
                    sprintf(resp, "%d FETCH (%s BODY%s {%d}", msg.id, tmp, options, msg.hsize);
                    respond("*", resp, NULL, session);
                    printf("%d S: UID %d (Enviando header...)\r\n", session->pid, msg.id);

                    // Envia o header uma linha por vez
                    prev = msg.header;
                    next = strchr(prev, '\n');
                    while(next != NULL) {
                        write(session->connfd, prev, strlen(prev)-strlen(next));

                        prev = next;
                        next = strchr(next+1, '\n');
                    }
                    write(session->connfd, prev, strlen(prev));
                    respond(NULL, ")", NULL, session);
                } else {
                    // O tamanho é do arquivo todo
                    sprintf(resp, "%d FETCH (%s BODY%s {%d}", msg.id, tmp, options, msg.fsize);
                    respond("*", resp, NULL, session);
                    printf("%d S: UID %d (Enviando body...)\r\n", session->pid, msg.id);

                    // Envia o arquivo uma linha por vez
                    prev = msg.text;
                    next = strchr(prev, '\n');
                    while(next != NULL) {
                        write(session->connfd, prev, strlen(prev)-strlen(next));

                        prev = next;
                        next = strchr(next+1, '\n');
                    }
                    write(session->connfd, prev, strlen(prev));
                    respond(NULL, ")", NULL, session);
                }

                // Marca a mensagem como lida
                if(!peek)
                    mark_seen(&msg);
            }
        }
    }

    respond(cmdline.tag, "OK", "FETCH Completado", session);
}

void cmd_store(cmdline_t cmdline, session_t *session) {
    int id, i;
    bool seen, deleted;
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

      flags = cmdline.argv[2];
       seen = (strstr(flags, "\\Seen") != NULL);
    deleted = (strstr(flags, "\\Deleted") != NULL);

    if(seen)    mark_seen(msg);
    if(deleted) mark_deleted(msg);
}

void cmd_list(cmdline_t cmdline, session_t *session) {
    char *dir = cmdline.argv[1];
    if(!strcmp(dir, "*") || !strcmp(dir, "INBOX")) {
        respond("*", "LIST", "() \"\" INBOX", session);
    }
    respond(cmdline.tag, "OK", "LIST completado.", session);
}

void cmd_select(cmdline_t cmdline, session_t *session) {
    char *inbox, resp[MAXLINE+1];
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
    inbox = unquote(cmdline.argv[0]);
    if(strcmp(inbox, "INBOX") != 0) {
        respond(cmdline.tag, "NO", "Não existe esse diretório.", session);
        return;
    }

    // Flags
    respond("*", "FLAGS", "(\\Deleted \\Seen)", session);
    respond("*", "OK", "[PERMANENTFLAGS (\\Deleted \\Seen)]", session);

    // Vê as mensagens existentes
    sprintf(path, "%s/Maildir/cur", session->user);
    // fprintf(stdout, "path = '%s'\n", path);

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

            msg = parse_title(file);
            parse_msg(&msg, session);
            session->messages[session->exists++] = msg;
            // fprintf(stdout, "%s\n", file->fts_path);
        }
    }


    // Número de mensagens existentes
    sprintf(resp, "%d", session->exists);
    // printf("resp = %s\n", resp); //BREAK
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
    login = unquote(login);
    password = unquote(password);

    for(i = 0; i < loginc; i++) {
        if(!strcmp(login, loginv[i][0]) && !strcmp(password, loginv[i][1])) {
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
char* unquote(char *s) {
    if(s[0] == '"') s++;
    if(s[strlen(s)-1] == '"') s[strlen(s)-1] = 0;

    return s;
}

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
    for(i = 0; i < 25; i++)
        if(!strcmp(name, commands[i])) {
            return (cmd_t)i;
        }

    return (cmd_t)-1;
}

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

void parse_msg(msg_t *msg, session_t *session) {
    FILE *file;
    char line[MAXLINE+1];
    bool header;

    // Pega o caminho até o arquivo
    sprintf(msg->filepath, "%s/Maildir/cur/%s", session->user, msg->file->fts_name);

    // Abre o arquivo
    file = fopen(msg->filepath, "r");
    // if(!file) {
    //     perror(filename);
    //     exit(9);
    // }

    // Aloca espaço para guardar o arquivo todo
    msg->fsize = msg->file->fts_statp->st_size;
    msg->text = (char*)malloc((msg->fsize+1)*sizeof(char));
    msg->text[0] = 0;

    // Calcula o tamanho do header e a quantidade de linhas
    msg->flines = 0;
    msg->hlines = 0;
    msg->hsize = 0;
    header = true;
    while(fgets(line, MAXLINE, file) != NULL) {
        strcat(msg->text, line);
        msg->flines++;

        if(header) {
            msg->hsize += strlen(line);
            msg->hlines++;
        }

        if(strstr(line, "text/plain") || strstr(line, "boundary"))
            header = false;

    }

    // Aloca espaço para o header
    msg->header = (char*)malloc((msg->hsize+1)*sizeof(char));
    strncpy(msg->header, msg->text, msg->hsize);
    msg->header[msg->hsize] = 0;

    // Volta para o início do arquivo
    fclose(file);
}

void mark_seen(msg_t *msg) {
    char newfp[MAXLINE+1];

    if(msg->seen) return;

    strcpy(newfp, msg->filepath);
    strcat(newfp, "S");

    rename(msg->filepath, newfp);
    strcpy(msg->filepath, newfp);
    msg->seen = true;
}

void mark_deleted(msg_t *msg) {
    char newfp[MAXLINE+1];

    if(msg->deleted) return;

    strcpy(newfp, msg->filepath);
    strcat(newfp, "D");

    rename(msg->filepath, newfp);
    strcpy(msg->filepath, newfp);
    msg->deleted = true;
}

void parse_mime(char *line, char **structure) {

}
