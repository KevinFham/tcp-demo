// C
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_BUFFER 1024
#define PACKET_SIZE 3
#define TIMEOUT_MS 400

const char RX_ADDR[] = "127.0.0.1";
const unsigned int PORT = 65432;
unsigned int UNACK_COUNT = 0;
unsigned int REACK_COUNT = 0;

/*
 * Reciever Socket
 */
struct rxArgs {
    float   *pktLossChance;
};

void* rx(void* arg) {
    struct rxArgs *rxA = arg;
    printf("%f", arg->pktLossChance);

    struct sockaddr_in rxAddr, txAddr;
    socklen_t len = sizeof(txAddr);
    char buffer[MAX_BUFFER];
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "RX socket creation failed");
        exit(1);
    }
    memset(&rxAddr, 0, sizeof(rxAddr));
    rxAddr.sin_family = AF_INET;
    rxAddr.sin_addr.s_addr = INADDR_ANY;
    rxAddr.sin_port = htons(PORT);
    if (bind(sockfd, (const struct sockaddr*)&rxAddr, sizeof(rxAddr)) < 0) {
        fprintf(stderr, "RX socket bind failed");
        exit(1);
    }

    // Recieve Packets
    while (1) {
        printf("e");
        int recv = recvfrom(sockfd, (char*)buffer, MAX_BUFFER, MSG_WAITALL, (struct sockaddr*)&txAddr, &len);
        printf("f %i", recv);
        printf("%s", buffer);
    }

}

/*
 * Transmitter Socket
 */
void* tx(char* pktSeq[][PACKET_SIZE], float timeoutms) {
    struct sockaddr_in rxAddr;
    socklen_t len = sizeof(rxAddr);
    char buffer[MAX_BUFFER];
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "TX socket creation failed");
        exit(1);
    }
    memset(&rxAddr, 0, sizeof(rxAddr));
    rxAddr.sin_family = AF_INET;
    rxAddr.sin_port = htons(PORT);
    rxAddr.sin_addr.s_addr = inet_addr(RX_ADDR);

    // Send Packets
    char message[MAX_BUFFER] = "cheese";
    
    int sent = sendto(sockfd, message, strlen(message), 0, (struct sockaddr*)&rxAddr, len);
    printf("sent %i", sent);
}

/*
 * Main
 */
int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s txt_file\n", argv[0]);
        printf("    txt_file: Text file containing test case");
        exit(1);
    }

    if (!strstr(argv[1], ".txt")) {
        fprintf(stderr, "File path does not point to a text file!");
        exit(1);
    }

    FILE* fptr = fopen(argv[1], "r");
    if (fptr == NULL) {
        fprintf(stderr, "Failed to open %s", argv[1]);
        exit(1);
    }

    // Daemonize Receiver
    pthread_t rxThread;
    struct rxArgs *rxA = calloc(1, sizeof(*rxA));
    rxA->pktLossChance = 1.0f;
    pthread_create(&rxThread, NULL, rx, &rxA);
    //rx(1.0f);

    // Transmit Packets
    char* packets[][3] = {{"-1", "1", "ACK"}};
    tx(packets, 400);

    fclose(fptr);
    return 0;
}
