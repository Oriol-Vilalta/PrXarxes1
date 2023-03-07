#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <bits/types/struct_timeval.h>//TODO Preguntar profe

//Package types for the registration phase
#define REGISTER_REQ 0x00
#define REGISTER_ACK 0x02
#define REGISTER_NACK 0x04
#define REGISTER_REJ 0x06
#define ERROR 0x0F

//Possible client states during the registration process
#define DISCONNECTED 0xA0
#define WAIT_REG_RESPONSE 0xA2
#define WAIT_DB_CHECK 0xA4
#define REGISTERED 0xA6
#define SEND_ALIVE 0xA8

//Package types for periodic communication
#define ALIVE_INF 0x10
#define ALIVE_ACK 0x12
#define ALIVE_NACK 0x14
#define ALIVE_REJ 0x16

//Time values used in registration
#define T 1
#define P 2
#define Q 3
#define U 2
#define N 6
#define O 2

//Values for the periodic communication
#define R 2
#define S 3

//Client data
struct Client {
    char id[7];
    char mac[13];
};

//Server data
struct Server {
    char id[7];
    char mac[13];
    char rand[7];
};

//Server addr
struct Server_Conn {
    char addr[16];
    int  udp_port;
    int  tcp_port;
};

struct Udp_PDU {
    unsigned char type;
    char id[7];
    char mac[13];
    char rand[7];
    char data[50];
};

//GLOBAL VARIABLES
//Program variables
bool debug_mode = false;
char *equipment_file;

//Data storing structs
struct Server_Conn *server_conn;
struct Server *server;
struct Client *client;

//Sockets
int udp_sock, tcp_sock;

//Addr used for registration
struct sockaddr_in client_addr, server_addr;

//Client state
unsigned char client_state = DISCONNECTED;

//Threads tids and stopping semaphore
pthread_t send_alives_tid, process_alive_tid;
sem_t stop_client_semaphore;

//Pending alives for the periodic communication
pthread_mutex_t pending_alives_mtx, stop_send_alives_mtx;
int pending_alives;

//SAFE STOP
/**
 * Frees the memory of the structures and thread control objects
 */
void free_memory(void) {
    free(server_conn);
    free(server);
    free(client);
    pthread_mutex_destroy(&pending_alives_mtx);
    pthread_mutex_destroy(&stop_send_alives_mtx);
    sem_destroy(&stop_client_semaphore);
}

/**
 * Frees the memory and stops the threads for a safe stop of the application
 */
void end_client(void) {
    //Free the memory
    free_memory();

    //Stop sockets
    if(udp_sock != 0) close(udp_sock);
    if(tcp_sock != 0) close(tcp_sock);

    //Stop threads
    if(send_alives_tid != 0) pthread_cancel(send_alives_tid);
    if(process_alive_tid != 0) pthread_cancel(process_alive_tid);

    //Exit the system correctly
    exit(0);
}

/**
 * Stop routine when ctrl + c is detected
 * @param sig_num id of the signal
 */
void handle_signal(int sig_num) {
    //Ctrl + c id
    if(sig_num == 2) {
        printf("[CLIENT] Stopped with ctrl + c\n");
    }

    //Stop the client
    end_client();
}


//SET UP CLIENT DATA
/**
 * Print the syntax of the command line
 * @param msg error message
 * @param exe_name name of the executable
 */
void print_correct_usage(char *msg, char *exe_name) {
    printf("[SET UP] ERROR: %s\n", msg);
    printf("[SET UP] USAGE: %s [-c client_config.cfg] [-d] [-f equipment_file.cfg]\n", exe_name);
    exit(1);
}

/**
 * Parses the command line if exists. Changes the configuration file (default: client.cfg)
 * @param argc number of arguments
 * @param arguments arguments of the program
 * @return the configuration file
 */
char *check_command_line(int argc, char **arguments) {
    //Default file names
    char *config_file = "client.cfg";
    equipment_file = "boot.cfg";
    
    int option;
    //Parse command line
    while((option = getopt(argc, arguments, ":c:df:")) != -1) {
        switch (option) {
            //Change the configuration file
            case 'c':
                config_file = optarg;
                break;
                
            //Activate debug mode
            case 'd':
                debug_mode = true;
                break;
                
            //Change the equipment file
            case 'f':
                equipment_file = optarg;
                break;
                
            //Unknown option such as -u
            case '?':
                print_correct_usage("Unknown option", arguments[0]);
                break;
                
            //An option requires an argument
            case ':':
                print_correct_usage("Option needs an argument", arguments[0]);
                break;
            default:
                break;
        }
    }

    //Check if the equipment file exists
    if(access(equipment_file, F_OK) != 0) {
        printf("[SET UP] ERROR: The file %s might not exist\n", equipment_file);
        exit(1);
    }

    return config_file;
}

/**
 * For parsing purposes, deletes the first word and returns the second
 * @param line raw line
 * @return the second word of the line
 */
char *eliminate_prompt(char *line) {
    char *delim = " \n";
    char *no_prompt_string = malloc(1);

    //Delete the first line
    strtok(line, delim);

    //Get the useful data
    strcpy(no_prompt_string, strtok(NULL, delim));
    return no_prompt_string;
}

/**
 * Saves all the data from the configuration file to the global Server_Conn and Client structures
 * @param config_file_name Name of the configuration file
 */
void set_up_config_data(char *config_file_name) {
    FILE *config_file;
    char *actual_line = malloc(sizeof(char) * 256);
    int line_num = 0;

    //Allocate the memory for the server_conn and the client data structs
    server_conn = malloc(sizeof(struct Server_Conn));
    client = malloc(sizeof(struct Client));

    //Opens the file
    if((config_file = fopen(config_file_name, "r")) == NULL) {
        printf("[SET UP] ERROR: The file %s might not exist\n", config_file_name);
        exit(1);
    }

    //Read and save config data
    while(!feof(config_file)) {
        fgets(actual_line, 256, config_file);

        switch(line_num) {
            //Save client id (Ex: Sw-001)
            case 0:
                strcpy(client->id, eliminate_prompt(actual_line));
                break;
                //Save client mac address (Ex: 23F474D2AC67)
            case 1:
                strcpy(client->mac, eliminate_prompt(actual_line));
                break;
                //Save server_conn ip address (Ex: localhost)
            case 2:
                strcpy(server_conn->addr, eliminate_prompt(actual_line));
                break;
                //Save server_conn port udp (Ex: 2023)
            case 3:
                server_conn->udp_port = atoi(eliminate_prompt(actual_line));
                break;

            default:
                break;
        }
        line_num++;
    }

}

//REGISTRATION PHASE
/**
 * Sets up the udp socket
 */
void open_udp_socket(void) {
    //Opens the socket
    if((udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("[REGISTER] ERROR: Cannot open the socket\n");
        exit(1);
    }

    memset(&client_addr, 0, sizeof(struct sockaddr_in));

    //Check if the port might belong to the root
    if(server_conn->udp_port <= 1024) {
        printf("[REGISTER] WARNING: The port %i might belong to the root\n", server_conn->udp_port);
    }

    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(0);

    //Bind the socket to the addr
    if(bind(udp_sock, (const struct sockaddr *) &client_addr, sizeof(client_addr)) < 0) {
        printf("[REGISTER] ERROR: Cannot bind\n");
        exit(1);
    }

}

/**
 * Set up the sockaddr_in instance of the server_conn
 * @return the sockaddr_in instance of the server_conn
 */
struct sockaddr_in set_up_server_addr(void) {
    struct hostent *hostent;

    //Define the hostent
    hostent = gethostbyname(server_conn->addr);
    if(!hostent) {
        printf("[REGISTER] ERROR: Cannot find %s", server_conn->addr);
        exit(1);
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_in));

    //Complete with the server_conn addr data
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = (((struct in_addr *) hostent->h_addr_list[0])->s_addr);
    server_addr.sin_port = htons(server_conn->udp_port);
    return server_addr;
}

/**
 * Builds a Udp_PDU instance with the information required for a REGISTER_REQ package.
 * @return a Udp_PDU package
 */
struct Udp_PDU build_reg_req(void) {
    //Create the package
    struct Udp_PDU reg_req;
    memset(&reg_req, 0, sizeof(struct Udp_PDU));

    //Fill the package with the required information
    reg_req.type = REGISTER_REQ;
    strcpy(reg_req.id, client->id);
    strcpy(reg_req.mac, client->mac);
    strcpy(reg_req.rand, "000000");
    strcpy(reg_req.data, "");

    return reg_req;
}

/**
 * Calculates a new timeout
 * @param num_package the number of packages already sent
 * @param previous_timeout the timeout of the previous iteration
 * @return a brand new timeout for the next iteration
 */
long calculate_timeout(int num_package, long previous_timeout) {
    //For the first P packages the timeout will be T
    if(num_package < P) {
        return T;

    //If the timeout reaches Q * T keeps it constant
    } else if(previous_timeout == Q * T) {
        return Q * T;

    //Increments the timeout
    } else {
        return previous_timeout += 1;
    }
}

/**
 * Send a Udp_PDU to the server_conn
 * @param udp_package Udp_PDU to send
 */
void send_udp_pdu_to_server(struct Udp_PDU udp_package) {
    //Send the package
    long res = sendto(udp_sock, (struct Udp_PDU *) &udp_package, sizeof(udp_package), 0, (struct sockaddr *) &server_addr, sizeof(server_addr));

    //Check if there has been an error
    if(res < 0) {
        printf("[REGISTER] ERROR: Sendto error\n");
        exit(1);
    }
}

/**
 * Attempts to send the first package to the server_conn
 * @param reg_req_package The package to send
 * @return the result received from the server_conn
 */
struct Udp_PDU do_registration_process(struct Udp_PDU reg_req_package) {
    struct Udp_PDU response;
    socklen_t client_addr_len;

    //Timeval instance to set the recvfrom function timeout
    struct timeval timeout;

    //A registration process attempts N times
    for(int num_package = 0; num_package < N; num_package++){

        //Attempt to send the REGISTER_REQ package to the server_conn and sets the client_state to WAIT_REG_RESPONSE
        send_udp_pdu_to_server(reg_req_package);
        if(client_state != WAIT_REG_RESPONSE) {
            client_state = WAIT_REG_RESPONSE;
            printf("[CLIENT] The client changes to WAIT_REG_RESPONSE\n");
        }

        //Sets a new timeout to the recvfrom
        timeout.tv_sec = calculate_timeout(num_package, timeout.tv_sec);
        setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        //Receive server_conn data
        recvfrom(udp_sock, (struct Udp_PDU *) &response, sizeof(struct Udp_PDU), 0, (struct sockaddr *) &client_addr, &client_addr_len);
        if(response.type != 0) {
            return response;
        }
    }
    return response;
}

/**
 * Processes the received package according to its type
 * @param answer package to be treated
 */
void process_register_req_answer(struct Udp_PDU answer) {
    switch (answer.type) {
        case REGISTER_ACK:
            //Change the client state
            client_state = REGISTERED;
            printf("[CLIENT] The client changes to REGISTERED\n");

            //Creates a Server instance
            server = malloc(sizeof(struct Server));

            //Save server data and tcp port
            strcpy(server->id, answer.id);
            strcpy(server->mac, answer.mac);
            strcpy(server->rand, answer.rand);
            server_conn->tcp_port = atoi(answer.data);
            break;

        case REGISTER_REJ:
            //Stop the client and show the reason
            printf("[SERVER] REGISTER_REJ: Rejected because %s\n", answer.data);
            exit(1);
    }
}
/**
 * Does the number of registration processes
 * @param max_processes number of processes
 */
void start_registration(int max_processes) {
    client_state = DISCONNECTED;

    //Build the REGISTER_REQ package
    struct Udp_PDU reg_req_package = build_reg_req();
    struct Udp_PDU received_package;

    //Do O registration processes, if a process receives a REGISTER_ACK package doesn't do anymore registration processes
    for(int num_process = 0; num_process < max_processes && received_package.type != REGISTER_ACK; num_process++) {
        //Registration process function, may or may not receive an answer
        received_package = do_registration_process(reg_req_package);

        //received_package.type equals 0 if the server_conn doesn't respond
        if(received_package.type != 0) {
            process_register_req_answer(received_package);
        } else {
            //Not server_conn response
            sleep(U);
        }
    }

    //If the client doesn't receive an answer or gets not accepted too much, it ends
    if(received_package.type == REGISTER_NACK) {
        printf("[REGISTER] REGISTER_NACK: Client not accepted because %s\n", received_package.data);
        exit(0);
    }
}

/**
 * Try to register to the server
 */
void register_to_server(void) {
    //Open the socket, bind and prepare the instance of sockaddr_in of the client
    open_udp_socket();

    //Prepare the instance of sockaddr_in of the server_conn
    server_addr = set_up_server_addr();

    //
    start_registration(O);
}

//PERIODIC COMMUNICATION
/**
 * Build the alive_inf package
 * @return the ALIVE_INF package
 */
struct Udp_PDU build_send_inf(void) {
    //Create the package
    struct Udp_PDU send_inf;
    memset(&send_inf, 0, sizeof(struct Udp_PDU));

    //Fill the package with the data
    send_inf.type = ALIVE_INF;
    strcpy(send_inf.id, client->id);
    strcpy(send_inf.mac, client->mac);
    strcpy(send_inf.rand, server->rand);
    strcpy(send_inf.data, "");
    return send_inf;
}

_Noreturn void *send_alives(void) {
    //Build the ALIVE_INF package
    struct Udp_PDU send_inf = build_send_inf();

    while(true) {
        //Send the ALIVE_INF package
        pthread_mutex_lock(&stop_send_alives_mtx);
        send_udp_pdu_to_server(send_inf);
        pthread_mutex_unlock(&stop_send_alives_mtx);

        //Increase the pendent alives
        pthread_mutex_lock(&pending_alives_mtx);
        pending_alives++;
        if(pending_alives >= S) {
            start_registration(1);
        }
        pthread_mutex_unlock(&pending_alives_mtx);

        //Wait R seconds before sending another alive package
        sleep(R);
    }
}

bool check_package_information(struct Udp_PDU response) {
    if(strcmp(response.id, server->id) != 0 ) {
        printf("[ALIVE] Wrong id received %s, expected: %s\n", response.id, server->id);
        return false;
    } else if(strcmp(response.mac, server->mac) != 0) {
        printf("[ALIVE] Wrong mac received %s, expected: %s\n", response.mac, server->mac);
        return false;
    } else if(strcmp(response.rand, server->rand) != 0) {
        printf("[ALIVE] Wrong random number received %s, expected: %s\n", response.rand, server->rand);
        return false;
    }

    return true;
}

_Noreturn void *receive_alive_answer(void) {
    socklen_t client_addr_len;
    struct Udp_PDU response;

    struct timeval timeout;
    timeout.tv_sec = 10000;
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while(true) {
        recvfrom(udp_sock, (struct Udp_PDU *) &response, sizeof(struct Udp_PDU), 0, (struct sockaddr *) &client_addr, &client_addr_len);

        check_package_information(response);

        if(response.type == ALIVE_ACK) {
            pthread_mutex_lock(&pending_alives_mtx);
            pending_alives--;
            pthread_mutex_unlock(&pending_alives_mtx);
        } else if(response.type == ALIVE_REJ) {
            pthread_mutex_lock(&stop_send_alives_mtx);
            start_registration(1);
            pthread_mutex_unlock(&stop_send_alives_mtx);
        }

    }
}

void start_periodic_communication(void) {
    sem_init(&stop_client_semaphore, 0, 0);
    pthread_mutex_init(&pending_alives_mtx, NULL);
    pthread_create(&send_alives_tid, NULL, (void *(*)(void *)) send_alives, NULL);
    pthread_create(&process_alive_tid, NULL, (void *(*)(void *)) receive_alive_answer, NULL);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    set_up_config_data(check_command_line(argc, argv));
    register_to_server();
    start_periodic_communication();

    sem_wait(&stop_client_semaphore);
    end_client();
}
