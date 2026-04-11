// C
/*
 * Note for future me: C arrays are unnasignable
 * (https://stackoverflow.com/questions/37225244/error-assignment-to-expression-with-array-type-error-when-i-assign-a-struct-f)
 */
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#define MAX_BUFFER 512
#define MAX_PKT_SEQ 128
#define PACKET_SIZE 3
#define TIMEOUT_MS 400

const char RX_ADDR[] = "127.0.0.1";
const unsigned int PORT = 65432;
unsigned int UNACK_COUNT = 0;
unsigned int REACK_COUNT = 0;

void delay(int msec) {
    clock_t startTime = clock();
    while (clock() < startTime + msec);
}

struct pktSequence {
    char** pkts;
    // char* pkts[MAX_PKT_SEQ][MAX_BUFFER];
    int seqLen;
};

/*
 * Reciever Socket
 */
void* rx(void* arg) {
    float* pktLossChance = (float*)arg;

    struct sockaddr_in rxAddr, txAddr;
    socklen_t len = sizeof(txAddr);
    char buffer[MAX_BUFFER];
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "RX socket creation failed\n");
        exit(1);
    }
    memset(&rxAddr, 0, sizeof(rxAddr));
    rxAddr.sin_family = AF_INET;
    rxAddr.sin_addr.s_addr = INADDR_ANY;
    rxAddr.sin_port = htons(PORT);
    if (bind(sockfd, (const struct sockaddr*)&rxAddr, sizeof(rxAddr)) < 0) {
        fprintf(stderr, "RX socket bind failed\n");
        exit(1);
    }

    // Recieve Packets
    struct pktSequence* recvPktSeq = malloc(sizeof(struct pktSequence));
    recvPktSeq->pkts = (char**)malloc(MAX_PKT_SEQ * sizeof(char*));
    for (int i = 0; i < MAX_PKT_SEQ; i++) {
        recvPktSeq->pkts[i] = (char*)malloc(MAX_BUFFER * sizeof(char));
    }
    recvPktSeq->seqLen = 0;

    while (1) {
        memset(buffer, '\0', MAX_BUFFER);
        int recv = recvfrom(sockfd, (char*)buffer, sizeof(buffer), MSG_WAITALL,
                            (struct sockaddr*)&txAddr, &len);
        char recvPkt[MAX_BUFFER];
        strncpy(recvPkt, buffer, sizeof(buffer));
        char pktSeqNum[8];
        char pktFlag[8];
        char* recvPktToken = strtok(buffer, " ");
        strncpy(pktSeqNum, recvPktToken, strlen(recvPktToken));
        recvPktToken = strtok(NULL, " ");
        strncpy(pktFlag, recvPktToken, strlen(recvPktToken));

        // Return ACK
        float pktLoss = (float)rand() / RAND_MAX;
        if (pktLoss < *pktLossChance) {
            // If EOF Packet, push out buffer. Else send to buffer
            if (strcmp(pktFlag, "-1") == 0) {
                printf("pkt seq recvd:\n");
                for (int i = 0; i < recvPktSeq->seqLen; i++) {
                    printf("%s\n", recvPktSeq->pkts[i]);
                }
                // flush buffer
            } else {
                strncpy(recvPktSeq->pkts[recvPktSeq->seqLen++], recvPkt,
                        MAX_BUFFER);
            }

            char ackPkt[MAX_BUFFER];
            snprintf(ackPkt, MAX_BUFFER, "%s 1 ACK", pktSeqNum);
            int sent = sendto(sockfd, ackPkt, MAX_BUFFER, MSG_WAITALL,
                              (struct sockaddr*)&txAddr, len);
            printf("RX: send ACK\n");
        } else {
            printf("RX: fail send ACK\n");
        }
    }
}

/*
 * Transmitter Socket
 */
void* tx(struct pktSequence* pktSeq, float timeoutms) {
    struct sockaddr_in rxAddr;
    socklen_t len = sizeof(rxAddr);
    char buffer[MAX_BUFFER];
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "TX socket creation failed\n");
        exit(1);
    }
    memset(&rxAddr, 0, sizeof(rxAddr));
    rxAddr.sin_family = AF_INET;
    rxAddr.sin_port = htons(PORT);
    rxAddr.sin_addr.s_addr = inet_addr(RX_ADDR);

    // Set Timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeoutms * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Send Packets
    for (int i = 0; i < pktSeq->seqLen; i++) {
        while (1) {
            char sendPkt[MAX_BUFFER];
            strncpy(sendPkt, pktSeq->pkts[i], MAX_BUFFER);
            char sendPktSeqNum[8];
            char* sendPktToken = strtok(sendPkt, " ");
            strncpy(sendPktSeqNum, sendPktToken, strlen(sendPktToken));

            // Send
            int sent = sendto(sockfd, pktSeq->pkts[i], MAX_BUFFER, MSG_WAITALL,
                              (struct sockaddr*)&rxAddr, len);

            // Await ACK, retry if UNACK
            memset(buffer, '\0', MAX_BUFFER);
            if (recvfrom(sockfd, (char*)buffer, sizeof(buffer), MSG_WAITALL,
                         (struct sockaddr*)&rxAddr, &len) > -1) {
                char pktSeqNum[8];
                char pktFlag[8];
                char* token = strtok(buffer, " ");
                strncpy(pktSeqNum, token, strlen(token));
                token = strtok(NULL, " ");
                strncpy(pktFlag, token, strlen(token));

                if (strcmp(pktFlag, "1") == 0 && strcmp(pktSeqNum, sendPktSeqNum) == 0) {  // Check if ACK for this sequence number
                    break;
                }
                printf("TX: Invalid ACK");
            } else {
                printf("TX: Timeout (%i ms)\n", TIMEOUT_MS);
            }
        }
    }
}

/*
 * Main
 */
int main(int argc, char** argv) {
    srand(time(NULL));

    if (argc != 2) {
        printf("Usage: %s txt_file\n", argv[0]);
        printf("    txt_file: Text file containing test case\n");
        exit(1);
    }

    if (!strstr(argv[1], ".txt")) {
        fprintf(stderr, "File path does not point to a text file!\n");
        exit(1);
    }

    FILE* fptr = fopen(argv[1], "r");
    if (fptr == NULL) {
        fprintf(stderr, "Failed to open %s\n", argv[1]);
        exit(1);
    }

    char pktLossChanceStr[MAX_BUFFER];
    fgets(pktLossChanceStr, MAX_BUFFER, fptr);
    float pktLossChance = atof(pktLossChanceStr);

    struct pktSequence* pktSeq = malloc(sizeof(struct pktSequence));
    pktSeq->pkts = (char**)malloc(MAX_PKT_SEQ * sizeof(char*));
    for (int i = 0; i < MAX_PKT_SEQ; i++) {
        pktSeq->pkts[i] = (char*)malloc(MAX_BUFFER * sizeof(char));
    }
    pktSeq->seqLen = 0;

    char fBuffer[MAX_BUFFER];
    while (fgets(pktSeq->pkts[pktSeq->seqLen], MAX_BUFFER, fptr) != NULL) {
        pktSeq->seqLen++;
    }

    // Daemonize Receiver
    pthread_t rxThread;
    pthread_create(&rxThread, NULL, rx, (void*)&pktLossChance);

    // Transmit Packets
    tx(pktSeq, TIMEOUT_MS);

    fclose(fptr);
    return 0;
}
