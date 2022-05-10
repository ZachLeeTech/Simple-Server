#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "wrapsock.h"
#include "ws_helpers.h"

void initClients(struct clientstate *client, int size) {
    // Initialize client array
    for (int i = 0; i < size; i++){
        client[i].sock = -1;	/* -1 indicates available entry */
        client[i].fd[0] = -1;
        client[i].request = NULL;	
        client[i].path = NULL;
        client[i].query_string = NULL;
        client[i].output = NULL;
        client[i].optr = client[i].output;
    }
}


/* Reset the client state cs.
 * Free the dynamically allocated fields
 */
void resetClient(struct clientstate *cs){
    cs->sock = -1;
    cs->fd[0] = -1;

    if(cs->path != NULL) {
        free(cs->path);
        cs->path = NULL;
    }
    if(cs->request != NULL) {
        free(cs->request);
        cs->request = NULL;
    }
    if(cs->output != NULL) {
        free(cs->output);
        cs->output = NULL;
    }
    if(cs->query_string != NULL) {
        free(cs->query_string);
        cs->query_string = NULL;
    }
}

/* Write the 404 Not Found error message on the file descriptor fd 
 */
void printNotFound(int fd) {

    char *error_str = "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
        "<html><head>\n"
        "<title>404 Not Found  </title>\n"
        "</head><body>\n"
        "<h1>Not Found (CSC209)</h1>\n"
        "<hr>\n</body>The server could not satisfy the request.</html>\n";

    int result = write(fd, error_str, strlen(error_str));
    if(result != strlen(error_str)) {
        perror("write");
    }
}

/* Write the 500 error message on the file descriptor fd 
 */
void printServerError(int fd) {

    char *error_str = "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
        "<html><head>\n"
        "<title>500 Internal Server Error</title>\n"
        "</head><body>\n"
        "<h1>Internal Server Error (CSC209) </h1>\n"
        "The server encountered an internal error or\n"
        "misconfiguration and was unable to complete your request.<p>\n"
        "</body></html>\n";

    int result = write(fd, error_str, strlen(error_str));
    if(result != strlen(error_str)) {
        perror("write");
    }
}

/* Write the 200 OK response on the file descriptor fd, and write the 
 * content of the response from the string output. The string in output
 * is expected to be correctly formatted.
 */
void printOK(int fd, char *output, int length) {
    int nbytes = strlen("HTTP/1.1 200 OK\r\n");
    if(write(fd, "HTTP/1.1 200 OK\r\n", nbytes) != nbytes) {
        perror("write");
    }
    int n;
    while(length > MAXLINE) {
        n = write(fd, output, MAXLINE);
        length -= n;
        output += n;
    }
    n = write(fd, output, length);
    if(n != length) {
        perror("write");
    }
}
