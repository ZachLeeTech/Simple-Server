#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>    /* Internet domain header */

#include "wrapsock.h"
#include "ws_helpers.h"

#define MAXCLIENTS 10

int handleClient(struct clientstate *cs, char *line);
int ready_for_processing(const char *request);

// You may want to use this function for initial testing
// void write_page(int fd);

int
main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: wserver <port>\n");
        exit(1);
    }
    unsigned short port = (unsigned short)atoi(argv[1]);
    int listenfd;
    struct clientstate client[MAXCLIENTS];


    // Set up the socket to which the clients will connect
    listenfd = setupServerSocket(port);

    initClients(client, MAXCLIENTS);

    // Set up the list of file descriptors for select
    int max_fd = listenfd; // Set the maxfd to the current listenfd
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds); // Add the listen fd to the list

    // Set up the timer for the timeout argument of select
    struct timeval timer;
    timer.tv_sec = 300; // 300 sec = 5 min
    timer.tv_usec = 0; // 0 milliseconds

    // Keeps track of the index of the client to be added
    int client_index = 0;

    // Keeps track of the number of completed requests (successful or not)
    int completed_clients = 0;

    // Loop while we have less than 10 completed requests
    while (completed_clients < 10){

        // Make a copy of the fd_set for select
        fd_set listen_fds = all_fds;

        // Call select and check what the return value is
        if (Select(max_fd + 1, &listen_fds, NULL, NULL, &timer) == 0){
            exit(0); // If it times out then exit with a code of 0
        }

        // Check if the listening socket descriptor is ready
        // and we have less than the max number of clients connnected
        if (FD_ISSET(listenfd, &listen_fds) && client_index < MAXCLIENTS){
            // Call accept on the listenfd
            int client_fd = Accept(listenfd, NULL, NULL);
            
            // Set the clientstate struct to have this new fd
            client[client_index].sock = client_fd;

            // Update the max_fd if needed
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }

            // Add the new fd to the fd_set
            FD_SET(client_fd, &all_fds);

            client_index ++; // Increment client_index
        }

        // Then check the clients
        for (int index = 0; index < MAXCLIENTS; index++) {
            // First check their sockets
            if (client[index].sock > -1 && FD_ISSET(client[index].sock, &listen_fds)) {
                // Then the client is ready to read from

                // Initialize a buffer for reading
                char buf[MAXLINE + 1];

                // Read from the socket
                int num_read = read(client[index].sock, &buf, MAXLINE);
                buf[num_read] = '\0';

                // Now we have a line so call handleClient
                int result = handleClient(&client[index], buf);

                if (result == 1){
                    // Then it is ready for processing

                    // Get the pipe descriptor to read from
                    int read_fd = processRequest(&client[index]);

                    // Add the descriptor to the fd_set and check the max_fd
                    if (read_fd != -1){
                        if(read_fd > max_fd){
                            max_fd = read_fd;
                        }
                        FD_SET(read_fd, &all_fds);
                    } else {
                        fprintf(stderr, "Failed process Request \n");
                    }
        

                } else if (result == -1){
                    // Then there was an error
                    Close(client[index].sock); // Close the socket
                    FD_CLR(client[index].sock, &all_fds); // remove it from the fd_set
                    resetClient(&client[index]); // Reset the client
                    completed_clients ++; // Increment the number of completed clients
                }
            }

            // Then check their pipe file descriptors
            if (client[index].fd[0] > -1 && FD_ISSET(client[index].fd[0], &listen_fds)) {
                int n;
                if ((n = read(client[index].fd[0], client[index].optr, MAXPAGE)) <= 0){
                    // Then the read is complete so close the pipe
                    Close(client[index].fd[0]);
                    // Remove it from the fd_set as well
                    FD_CLR(client[index].fd[0], &all_fds);

                    // Also write to the client socket with the appropriate response
                    int status;
                    if (wait(&status) == -1){
                        perror("wait");
                    } else {
                        if (WIFEXITED(status)){
                            if (WEXITSTATUS(status) == 0){
                                // If the exit status is 0 then we have a 200 OK response
                                printOK(client[index].sock, client[index].output, client[index].optr - client[index].output);
                            } else if (WEXITSTATUS(status) == 100){
                                // Then the exec call failed so we have a 404 error
                                printNotFound(client[index].sock);
                            } else {
                                // Otherwise there is a 500 error
                                printServerError(client[index].sock);
                            }
                        } else if (WIFSIGNALED(status)){
                            // Otherwise there is a signal so error 500
                            printServerError(client[index].sock);
                        }
                    }

                    Close(client[index].sock); // Close the socket
                    FD_CLR(client[index].sock, &all_fds); // remove it from the fd_set
                    resetClient(&client[index]); // Reset the client
                    completed_clients ++; // Increment the number of completed clients
                } else {
                    client[index].optr += n;
                }
            }
        }
    }

    exit(0);
}

/* Update the client state cs with the request input in line.
 * Intializes cs->request if this is the first read call from the socket.
 * Note that line must be null-terminated string.
 *
 * Return 0 if the get request message is not complete and we need to wait for
 *     more data
 * Return -1 if there is an error and the socket should be closed
 *     - Request is not a GET request
 *     - The first line of the GET request is poorly formatted (getPath, getQuery)
 * 
 * Return 1 if the get request message is complete and ready for processing
 *     cs->request will hold the complete request
 *     cs->path will hold the executable path for the CGI program
 *     cs->query will hold the query string
 *     cs->output will be allocated to hold the output of the CGI program
 *     cs->optr will point to the beginning of cs->output
 */
int handleClient(struct clientstate *cs, char *line) {

    // Check if it is the first read call
    if (cs->request == NULL){
        // Allocate space for the request if it is
        cs->request = malloc(MAXLINE * sizeof(char));
        cs->request[0] = '\0'; // null terminate it
    }

    // Concatenate the line to the end of the request
    strncat(cs->request, line, MAXLINE);

    // Check if there is a "\r\n\r\n" at the end
    if (ready_for_processing(cs->request) == 0){
        // If not return 0
        return 0;
    }

    // Extract the resource string first
    char *path = getPath(cs->request);
    // If it is NULL then there's an error
    if (path == NULL){
        return -1;
    }
    cs->path = path; // Otherwise we store the path

    // Extract the query string
    char *query = getQuery(cs->request);
    // If it is NULL then there's an error
    if (query == NULL){
        return -1;
    }
    cs->query_string = query; // Otherwise we store the query
    
    // If the resource is favicon.ico we will ignore the request
    if(strcmp("favicon.ico", cs->path) == 0){
        // A suggestion for debugging output
        fprintf(stderr, "Client: sock = %d\n", cs->sock);
        fprintf(stderr, "        path = %s (ignoring)\n", cs->path);
		printNotFound(cs->sock);
        return -1;
    }

    // Initialize the output and optr values
    cs->output = malloc(sizeof(char) * MAXPAGE + 1); // allocate one more space for a \0
    cs->optr = cs->output;

    // A suggestion for printing some information about each client. 
    // You are welcome to modify or remove these print statements
    fprintf(stderr, "Client: sock = %d\n", cs->sock);
    fprintf(stderr, "        path = %s\n", cs->path);
    fprintf(stderr, "        query_string = %s\n", cs->query_string);

    return 1;
}

/*
 * Return 1 if the request string has a \r\n\r\n at the end,
 * Otherwise return 0
 */
int ready_for_processing(const char *request){
    int num_char = strlen(request);
    if (num_char >= 4){
        if (request[num_char - 1] == '\n' && request[num_char - 2] == '\r' &&
            request[num_char - 3] == '\n' && request[num_char - 4] == '\r'){
            return 1;
        }
    }
    return 0;
}
