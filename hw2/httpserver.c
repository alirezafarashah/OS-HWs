#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
#define MAX_BUFFER_SIZE 131072
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

/*
 * Serves the contents the file stored at `path` to the client socket `fd`.
 * It is the caller's reponsibility to ensure that the file stored at `path` exists.
 * You can change these functions to anything you want.
 * 
 * ATTENTION: Be careful to optimize your code. Judge is
 *            sesnsitive to time-out errors.
 */
void serve_file(int fd, char *path, long file_size) {
    char *cont_len = malloc(sizeof(char) * 10);
    snprintf(cont_len, 10, "%ld", file_size);
    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", http_get_mime_type(path));
    http_send_header(fd, "Content-Length", cont_len);
    http_end_headers(fd);
    int file_d = open(path, O_RDONLY);
    char buffer[MAX_BUFFER_SIZE];
    int read_bytes = 0;
    while ((read_bytes = read(file_d, buffer, sizeof(buffer) - 1)) != 0) {
        http_send_data(fd, buffer, read_bytes);
    }
    free(cont_len);
    close(file_d);
}


void serve_directory(int fd, char *path) {
    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", http_get_mime_type(".html"));
    http_end_headers(fd);

    DIR *dir = opendir(path);
    struct dirent *dir_entry;
//    struct stat file_stat;
    int max_len = strlen("<a href='%s'>%s</a><br>\n") + strlen(path) + 1;
    char *dir_link = (char *) malloc(sizeof(char) * (max_len));
    if (dir != NULL) {
        while ((dir_entry = readdir(dir)) != NULL) {
            char *temp = malloc(sizeof(char) * (strlen(dir_entry->d_name) + strlen(path) + 1));
            snprintf(dir_link, max_len, "<a href='%s'>%s</a><br>\n", dir_entry->d_name, dir_entry->d_name);
            http_send_string(fd, dir_link);
        }
        closedir(dir);
    }
    free(dir_link);


}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 * 
 *   Closes the client socket (fd) when finished.
 */
void handle_files_request(int fd) {
    struct http_request *request = http_request_parse(fd);
    printf("req: %s\n", request->path);
    if (request == NULL || request->path[0] != '/') {
        http_start_response(fd, 400);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);
        close(fd);
        return;
    }
    if (strstr(request->path, "..") != NULL) {
        http_start_response(fd, 403);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);
        close(fd);
        return;
    }
    /* Remove beginning `./` */
    char *path = malloc(2 + strlen(request->path) + 1);
    path[0] = '.';
    path[1] = '/';
    memcpy(path + 2, request->path, strlen(request->path) + 1);

    char *fpath = malloc(sizeof(char) * (strlen(request->path) + strlen(server_files_directory) + 1));
    strcpy(fpath, server_files_directory);
    strcat(fpath, request->path);
    /*
     * call serve_file() on it. Else, serve a 404 Not Found error below.
     *
     * determine when to call serve_file() or serve_directory() depending
     * on `path`.
     *
     * Feel FREE to delete/modify anything on this function.
     */
    struct stat file_status;

    stat(fpath, &file_status);
    printf("%s\n", fpath);
    if (S_ISREG(file_status.st_mode)) {
        serve_file(fd, fpath, file_status.st_size);
    } else if (S_ISDIR(file_status.st_mode)) {
        char *copy_path = malloc(sizeof(char) * strlen(fpath) + 2);
        strcpy(copy_path, fpath);
        strcat(copy_path, "/");
        strcat(fpath, "/index.html");
        stat(fpath, &file_status);
        if (S_ISREG(file_status.st_mode)) {
            serve_file(fd, fpath, file_status.st_size);
        } else {
            serve_directory(fd, copy_path);
        }
    } else {
        http_start_response(fd, 404);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);
    }

//    http_start_response(fd, 200);
//    http_send_header(fd, "Content-Type", "text/html");
//    http_end_headers(fd);
//    http_send_string(fd,
//                     "<center>"
//                     "<h1>Welcome to httpserver!</h1>"
//                     "<hr>"
//                     "<p>Nothing's here yet.</p>"
//                     "</center>");
    free(fpath);
    free(path);
    free(request);
    close(fd);
    return;
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */

typedef struct server_client {
    int *read_port;
    int *write_port;
    pthread_cond_t *cond;
    int *finished;
} server_client;

void *msg_socket(void *args) {
    struct server_client *sc = (server_client *) args;
    char buffer[MAX_BUFFER_SIZE];
    int read_result;
    while (!(*sc->finished) && (read_result = read(*sc->read_port, buffer, sizeof(buffer) - 1)) > 0) {
        http_send_data(*sc->write_port, buffer, read_result);
        printf("read res : %d\n", read_result);
        printf("finished: %d\n", *sc->finished);
    }
    printf("downloaded\n");
    if (!(*sc->finished)) {
        *sc->finished = 1;
        pthread_cond_signal(sc->cond);
    }
    return NULL;
}

void handle_proxy_request(int fd) {

    /*
    * The code below does a DNS lookup of server_proxy_hostname and
    * opens a connection to it. Please do not modify.
    */

    struct sockaddr_in target_address;
    memset(&target_address, 0, sizeof(target_address));
    target_address.sin_family = AF_INET;
    target_address.sin_port = htons(server_proxy_port);

    struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

    int target_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (target_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        close(fd);
        exit(errno);
    }

    if (target_dns_entry == NULL) {
        fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
        close(target_fd);
        close(fd);
        exit(ENXIO);
    }

    char *dns_address = target_dns_entry->h_addr_list[0];

    memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
    int connection_status = connect(target_fd, (struct sockaddr *) &target_address,
                                    sizeof(target_address));

    if (connection_status < 0) {
        /* Dummy request parsing, just to be compliant. */
        http_request_parse(fd);

        http_start_response(fd, 502);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);
        http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
        close(target_fd);
        close(fd);
        return;
    }


    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    server_client *server = malloc(sizeof(server_client));
    server_client *client = malloc(sizeof(server_client));
    int finished = 0;
    server->read_port = &target_fd;
    server->write_port = &fd;
    server->finished = &finished;
    server->cond = &cond;

    client->read_port = &fd;
    client->write_port = &target_fd;
    client->finished = &finished;
    client->cond = &cond;

    pthread_t threads[2];
    pthread_create(threads, NULL, msg_socket, client);
    pthread_create(threads + 1, NULL, msg_socket, server);

    while (!finished) pthread_cond_wait(&cond, &mutex);

    close(target_fd);
    close(fd);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    free(server);
    free(client);
    pthread_cancel(threads[0]);
    pthread_cancel(threads[1]);
    printf("closed socket");

}

void dispatcher(void (*request_handler)(int)) {
    while (1) {
        int client_socket = wq_pop(&work_queue);
        request_handler(client_socket);
        close(client_socket);
        printf("task complete\n");
    }
}

void init_thread_pool(int num_threads, void (*request_handler)(int)) {
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, dispatcher, request_handler);
    }
    printf("all threads initialized\n");
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

    struct sockaddr_in server_address, client_address;
    size_t client_address_length = sizeof(client_address);
    int client_socket_number;

    *socket_number = socket(PF_INET, SOCK_STREAM, 0);
    if (*socket_number == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    int socket_option = 1;
    if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(server_port);

    if (bind(*socket_number, (struct sockaddr *) &server_address,
             sizeof(server_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    if (listen(*socket_number, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", server_port);
    wq_init(&work_queue);
    init_thread_pool(num_threads, request_handler);

    while (1) {
        client_socket_number = accept(*socket_number,
                                      (struct sockaddr *) &client_address,
                                      (socklen_t * ) & client_address_length);
        if (client_socket_number < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);

        if (num_threads == 0) {
            request_handler(client_socket_number);
            close(client_socket_number);
        } else {
            wq_push(&work_queue, client_socket_number);
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);
    }
    close(*socket_number);
    shutdown(*socket_number, SHUT_RDWR);

}

int server_fd;

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    printf("Closing socket %d\n", server_fd);
    if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    exit(0);
}

char *USAGE =
        "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
        "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Default settings */
    server_port = 8000;
    void (*request_handler)(int) = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("--files", argv[i]) == 0) {
            request_handler = handle_files_request;
            free(server_files_directory);
            server_files_directory = argv[++i];
            if (!server_files_directory) {
                fprintf(stderr, "Expected argument after --files\n");
                exit_with_usage();
            }
        } else if (strcmp("--proxy", argv[i]) == 0) {
            request_handler = handle_proxy_request;

            char *proxy_target = argv[++i];
            if (!proxy_target) {
                fprintf(stderr, "Expected argument after --proxy\n");
                exit_with_usage();
            }

            char *colon_pointer = strchr(proxy_target, ':');
            if (colon_pointer != NULL) {
                *colon_pointer = '\0';
                server_proxy_hostname = proxy_target;
                server_proxy_port = atoi(colon_pointer + 1);
            } else {
                server_proxy_hostname = proxy_target;
                server_proxy_port = 80;
            }
        } else if (strcmp("--port", argv[i]) == 0) {
            char *server_port_string = argv[++i];
            if (!server_port_string) {
                fprintf(stderr, "Expected argument after --port\n");
                exit_with_usage();
            }
            server_port = atoi(server_port_string);
        } else if (strcmp("--num-threads", argv[i]) == 0) {
            char *num_threads_str = argv[++i];
            if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
                fprintf(stderr, "Expected positive integer after --num-threads\n");
                exit_with_usage();
            }
            printf("multi threads\n");
        } else if (strcmp("--help", argv[i]) == 0) {
            exit_with_usage();
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }

    if (server_files_directory == NULL && server_proxy_hostname == NULL) {
        fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                        "                      \"--proxy [HOSTNAME:PORT]\"\n");
        exit_with_usage();
    }

    serve_forever(&server_fd, request_handler);

    return EXIT_SUCCESS;
}
