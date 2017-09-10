#### MAC5910 - Programação para Redes de Computadores
# EP1
*Autores*: Lucas Magno, Matheus Riva

Neste trabalho foi implementado um servidor básico IMAP na linguagem C, com estrutura interna no padrão Maildir e desenhado para comunicação com o cliente Thunderbird, utilizando como base o código de um servidor *echo* disponibilizado pelo docente.<sup>1</sup>

Os seguintes comandos IMAP foram implementados:
* `LOGIN`
* `LIST`
* `LSUB`
* `SELECT`
* `UID`
* `FETCH`
* `STORE`
* `IDLE`
* `NOOP`
* `LOGOUT`

Com suporte às flags
* `\Seen`
* `\Deleted`

De forma que o cliente consiga logar, listar e baixar os emails, além de os marcar/desmarcar como lidos e deletados. No entanto, a única caixa disponível é a de entrada (`INBOX`), a lixeira (`Trash`) não foi explicitamente implementada.

Seguindo o padrão Maildir, as mensagens ficam guardadas na hierarquia
* *usuário*/
    * `Maildir/`
        * `cur/`
            * *mensagens*
        * `new/`
        * `tmp/`

Ou seja, elas não contêm a flag `\Recent` (mensagens armazenadas em `new/`), mas isso não altera o funcionamento do servidor, pois todas as outras flags são registradas na pasta `cur/`. A pasta `tmp/` não é utilizada, já que ela é reservada para o recebimento de mensagens, o que não foi considerado neste trabalho.

## Exemplos
A fim de demonstrar o funcionamento do programa foram definidos dois usuários de email com suas respectivas caixas de entrada contendo mensagens fictícias, que vão desde texto simples a anexos codificados em base64. Como exemplo segue uma das mensagens.

```
From: email@domain
Subject: This is an email
To: mriva@ime.usp.br
Message-ID: <efb89e20-5855-c9eb-6db3-bb9db7bcc880@localhost>
Date: Sun, 3 Sep 2017 19:47:11 -0300
User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:52.0) Gecko/20100101
 Thunderbird/52.3.0
MIME-Version: 1.0
Content-Type: text/plain; charset=utf-8; format=flowed
Content-Language: en-US
Content-Transfer-Encoding: 7bit

This is the body of the email.
```

## Utilização
Para compilar o programa basta executar o comando `make` no diretório raiz, o que criará um executável chamado `ep1` que pode ser invocado da forma
```
./ep1 8000
```
para iniciar o servidor na porta 8000.

## Conexão
Para conectar com o servidor basta utilizar uma das contas definidas, cujos login e senha são, respectivamente
* `mriva@ime.usp.br`, `password1`
* `lmagno@ime.usp.br`, `password2`

A conexão foi testada com o cliente Thunderbird configurado da seguinte forma:

```
Server name: 127.0.0.1
Port: 8000
Connection security: None
Authentication method: Password, transmitted insecurely
```
Feito isso o cliente deve puxar automaticamente as mensagens do servidor.

[1]: *Observação*: boa parte da compreensão do protocolo foi obtida observando a comunicação entre o Dovecot e o Thunderbird através do Wireshark.

## Referências
1. [RFC3501 -  INTERNET MESSAGE ACCESS PROTOCOL - VERSION 4rev1](https://tools.ietf.org/html/rfc3501)
2. [IMAP Sucks](http://hea-www.cfa.harvard.edu/~fine/opinions/IMAPsucks.html)
3. [Using maildir format](http://cr.yp.to/proto/maildir.html)
4. [Dovecot Maildir](https://wiki2.dovecot.org/MailboxFormat/Maildir)
5. [Courier Mail Server](http://www.courier-mta.org/maildir.html)
