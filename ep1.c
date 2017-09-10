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
#include "imap.c"

#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096

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

            // Decide o que fazer dependendo do comando
            switch(cmdline.cmd) {
                case AUTHENTICATE:
                    respond(cmdline.tag, "NO", "AUTHENTICATE Comando não implementado", &session);
                    break;

                case LOGIN:
                    cmd_login(cmdline, &session);
                    break;

                case LIST:
                    cmd_list(cmdline, &session);
                    break;

                case LSUB:
                    respond("*", "LSUB", "() \"\" INBOX", &session);
                    respond(cmdline.tag, "OK", "LSUB completado.", &session);
                    break;

                case SELECT:
                    cmd_select(cmdline, &session);
                    break;

                case UID:
                    cmd_uid(cmdline, &session);
                    break;

                case LOGOUT:
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
