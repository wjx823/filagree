/*-----------------------------------------------------------------------------------
*
* node
*
* multithreaded TCP client / server
*
-----------------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cyassl/ssl.h>
#include <pthread.h>
#include <dirent.h>
#include "vm.h"
#include "serial.h"

#define MAXLINE 1024
#define STR_BUF_SIZE 1024
#define TRUE 1

struct thread_argument {
    struct context *context;
    struct variable *listener;
    CYASSL* ssl;
};

void *incoming_connection(void *arg)
{
	char readline[MAXLINE];
    struct thread_argument *ta = (struct thread_argument *)arg;

    for (;;)
    {
        bzero(readline, sizeof(readline));
        int n;
        if (((n = CyaSSL_read(ta->ssl, readline, MAXLINE)) <= 0))
        {
            fprintf(stderr, "client closed connection\n");
            goto free_ssl;
        }

        fprintf(stderr, "%d bytes received: %s\n", n, readline);
        struct byte_array *raw_message = byte_array_new_size(n);
        raw_message->data = (uint8_t*)readline;
        int32_t raw_message_length = serial_decode_int(raw_message);
        assert_message(raw_message_length < MAXLINE, "todo: handle long messages");
        struct variable *message = variable_deserialize(ta->context, raw_message);

        
    }

free_ssl:
	CyaSSL_free(ta->ssl); /* Free CYASSL object */
    free(ta);
	return NULL;
}

struct variable *sys_listen(struct context *context)
{
    struct variable *arguments = (struct variable*)stack_pop(context->operand_stack);
    const int32_t serverport = ((struct variable*)array_get(arguments->list, 1))->integer;
    struct variable *listener = ((struct variable*)array_get(arguments->list, 2));

	int listenfd;
	struct sockaddr_in servaddr;
	int optval;

	CyaSSL_Init(); // Initialize CyaSSL
	CYASSL_CTX* ctx;

	/* Create and initialize CYASSL_CTX structure */
	if ( (ctx = CyaSSL_CTX_new(CyaTLSv1_server_method())) == NULL)
	{
		fprintf(stderr, "CyaSSL_CTX_new error.\n");
		exit(0);
	}

	/* Load CA certificates into CYASSL_CTX */
	if (CyaSSL_CTX_load_verify_locations(ctx, "../conf/server/ca-cert.pem", 0) != SSL_SUCCESS)
	{
		fprintf(stderr, "Error loading ca-cert.pem, please check the file.\n");
		exit(0);
	}

	/* Load server certificate into CYASSL_CTX */
	if (CyaSSL_CTX_use_certificate_file(ctx, "conf/server-cert.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
	{
		fprintf(stderr, "Error loading server-cert.pem, please check the file.\n");
		exit(0);
	}

	/* Load server key into CYASSL_CTX */
	if (CyaSSL_CTX_use_PrivateKey_file(ctx, "conf/server-key.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
	{
		fprintf(stderr, "Error loading server-key.pem, please check the file.\n");
		exit(0);
	}

	/* open the server socket over specified port 8080 to accept client connections*/
	listenfd = socket(AF_INET, SOCK_STREAM, 0);

	/* setsockopt: Eliminates "ERROR on binding: Address already in use" error. */
	optval = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(serverport);

	bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	listen(listenfd, 5);

	/* create thread for processing each client request */
	struct sockaddr_in client_addr;
	socklen_t sin_size = sizeof (struct sockaddr_in);

    for(;;)
	{
		pthread_t child;
		CYASSL* ssl;
		int connfd = accept(listenfd, (struct sockaddr *) &client_addr, &sin_size );
		DEBUGPRINT("\n Got a connection from (%s , %d)\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		/* Create CYASSL Object */
		if ((ssl = CyaSSL_new(ctx)) == NULL) {
			fprintf(stderr, "CyaSSL_new error.\n");
			return 1;
		}

		CyaSSL_set_fd(ssl, connfd);
        struct thread_argument *ta = (struct thread_argument *)malloc(sizeof(struct thread_argument));
        ta->context = context;
        ta->listener = listener;
        ta->ssl = ssl;
		pthread_create(&child, NULL, incoming_connection, &ta);
        DEBUGPRINT("thread created\n");
	}

    return 0;
}

/*-----------------------------------------------------------------------------------
 *
 * singlethreaded TCP client socket implementation for file transfer
 *
 -----------------------------------------------------------------------------------*/
#include        <stdio.h>
#include        <stdlib.h>
#include        <string.h>
#include        <unistd.h>
#include        <errno.h>
#include        <arpa/inet.h>
#include        <signal.h>
#include        <sys/socket.h>
#include        <cyassl/ssl.h>

#define MAXLINE 1024
#define STR_BUF_SIZE 1024


struct variable *sys_send(struct context *context)
{
    CYASSL *ssl = NULL;

	char *sptr;
    char recvline[MAXLINE], sendline[MAXLINE];
	int chunks, lastchunk, byteCount;

    fprintf(stderr, "requesting list: %s (%lu bytes)\n", sendline, strlen(sendline));
    if (CyaSSL_write(ssl, sendline, strlen(sendline)) != strlen(sendline))
    {
        fprintf(stderr, "CyaSSL_write failed");
        return 1;
    }

    /* recv header, filename & byte count */
    int n;
    if ((n = CyaSSL_read(ssl, recvline, MAXLINE)) <= 0)
    {
        fprintf(stderr, "CyaSSL_read error\n");
        return 1;
    }
    fprintf(stderr, "received response %d bytes: %s\n", n, recvline);

    strtok(recvline, " " );               /* the word SEND */
    sptr = (char *)strtok(NULL, " ");     /* the size */
    if (sptr != NULL)
    {
        byteCount = strtol(sptr, (char **)NULL, 10);
    }

    bzero(sendline, sizeof(sendline));
    snprintf(sendline, MAXLINE, "RECV SIZE OK\n");

    fprintf(stderr, "sending ack\n");
    if (CyaSSL_write(ssl, sendline, strlen(sendline)) != strlen(sendline))
    {
        fprintf(stderr, "CyaSSL_write failed");
        return 1;
    }

    chunks = byteCount / MAXLINE;
    lastchunk = byteCount % MAXLINE;
    fprintf(stderr, "server has %d bytes / %dr%d chunks\n", byteCount, chunks, lastchunk);

    for (int i=0; i<chunks; i++)
    {
        if ((n = CyaSSL_read(ssl, recvline, MAXLINE)) <= 0)
        {
            fprintf(stderr, "CyaSSL_read error\n");
            return 1;
        }
        fprintf(stderr, "received chunk %d bytes\n", n);
        fprintf(stderr, "%s\n", recvline);
    }

    if (lastchunk > 0)
    {
        if ((n = CyaSSL_read(ssl, recvline, lastchunk)) <= 0)
        {
            fprintf(stderr, "CyaSSL_read error\n");
            return 1;
        }
        fprintf(stderr, "received last chunk %d bytes\n", n);
        fprintf(stderr, "%s\n", recvline);
    }
    fprintf(stderr, "done\n");
    return 0;
}

struct variable *sys_connect(struct context *context)
{
	int sockfd;
	struct sockaddr_in servaddr;
	char *spooldir, *request, *serveraddr;
	int serverport = 0;
	char sendline[MAXLINE], path[MAXLINE];
	CYASSL_CTX* ctx;
	CYASSL* ssl;
    int result = 0;

    serverport = serverport ? serverport : 8020;

	CyaSSL_Init();  /* Initialize CyaSSL */

	/* Create and initialize CYASSL_CTX structure */
	if ( (ctx = CyaSSL_CTX_new(CyaTLSv1_client_method())) == NULL)
	{
		fprintf(stderr, "SSL_CTX_new error.\n");
		result = 1;
        goto cya_l8r;
	}

	/* Load CA certificates into CYASSL_CTX. */
	if (CyaSSL_CTX_load_verify_locations(ctx, "../conf/client/ca-cert.pem", 0) != SSL_SUCCESS)
	{
		fprintf(stderr, "Error loading ca-cert.pem, please check the file.\n");
		result = 1;
        goto free_ctx;
	}

	/* Create Socket file descriptor */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(serverport);
	inet_pton(AF_INET, serveraddr, &servaddr.sin_addr);

	/* Blocking Connect to socket file descriptor */
	connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr));

	/* Create CYASSL object */
	if ((ssl = CyaSSL_new(ctx)) == NULL)
	{
		fprintf(stderr, "CyaSSL_new error.\n");
        result = 1;
		goto free_ctx;
	}

	CyaSSL_set_fd(ssl, sockfd);
	fprintf(stderr, "Connected over CyaSSL socket!\n");

	snprintf(path, STR_BUF_SIZE, "%s/%s", spooldir, request);

	CyaSSL_free(ssl);       /* Free SSL object */
free_ctx:
	CyaSSL_CTX_free(ctx);   /* Free SSL_CTX object */
cya_l8r:
	CyaSSL_Cleanup();       /* Free CyaSSL */

	return result;
}

struct variable *sys_disconnect(struct context *context)
{
    return NULL;
}