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

struct pktSequence {
    char** pkts;
    // char* pkts[MAX_PKT_SEQ][MAX_BUFFER];
    int seqLen;
};

int pktSortAsc(const void* a, const void* b) {
    char aPkt[MAX_BUFFER];
    char bPkt[MAX_BUFFER];
    strncpy(aPkt, *(char**)a, sizeof(*(char**)a));
    strncpy(bPkt, *(char**)b, sizeof(*(char**)b));
    char* aTok = strtok(aPkt, " ");
    char* bTok = strtok(bPkt, " ");
    return (atoi(aTok) - atoi(bTok));
}

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
                            (struct sockaddr*)&txAddr, &len);  // Blocking until incoming packet is received
        char recvPkt[MAX_BUFFER];
        strncpy(recvPkt, buffer, sizeof(buffer));
        char recvPktSeqNum[8] = "0";
        char recvPktFlag[8] = "0";
        char* recvPktToken = strtok(buffer, " ");
        strncpy(recvPktSeqNum, recvPktToken, strlen(recvPktToken));
        recvPktToken = strtok(NULL, " ");
        strncpy(recvPktFlag, recvPktToken, strlen(recvPktToken));

        // Return ACK based on % chance
        float pktLoss = (float)rand() / RAND_MAX;
        if (pktLoss < *pktLossChance) {
            // If EOF Packet, push out buffer. Else send to buffer
            if (strcmp(recvPktFlag, "-1") == 0) {
                /*
                 * Simulation Conclusion + Flush Buffer
                 */
                printf("Buffer Seq (unsorted): ");
                for (int i = 0; i < recvPktSeq->seqLen; i++) {
                    char pkt[MAX_BUFFER];
                    strncpy(pkt, recvPktSeq->pkts[i], MAX_BUFFER);
                    char pktSeqNum[8] = "0";
                    char* pktToken = strtok(pkt, " ");
                    strncpy(pktSeqNum, pktToken, strlen(pktToken));

                    printf("%s ", pktSeqNum);
                }

                printf("\nBuffer Seq (sorted): ");
                qsort(recvPktSeq->pkts, recvPktSeq->seqLen, sizeof(recvPktSeq->pkts[0]), pktSortAsc);
                for (int i = 0; i < recvPktSeq->seqLen; i++) {
                    char pkt[MAX_BUFFER];
                    strncpy(pkt, recvPktSeq->pkts[i], MAX_BUFFER);
                    char pktSeqNum[8] = "0";
                    char* pktToken = strtok(pkt, " ");
                    strncpy(pktSeqNum, pktToken, strlen(pktToken));

                    printf("%s ", pktSeqNum);
                }

                printf("\nUnACK\'d pkts: %u", UNACK_COUNT);
                printf("\nResent pkts: %u", REACK_COUNT);
                printf("\nDelivered msg: ");
                for (int i = 0; i < recvPktSeq->seqLen; i++) {
                    char pkt[MAX_BUFFER];
                    strncpy(pkt, recvPktSeq->pkts[i], MAX_BUFFER);
                    char pktData[MAX_BUFFER] = "";
                    char* pktToken = strtok(pkt, " ");
                    pktToken = strtok(NULL, " ");
                    pktToken = strtok(NULL, " ");

                    // Skip the line carry '\n'
                    for (int j = 0; j < MAX_BUFFER; j++) {
                        if (pktToken[j] == '\n') {
                            break;
                        }
                        printf("%c", pktToken[j]);
                    }
                    printf(" ");
                }
                printf("\n");

                // Flush buffer
                for (int i = 0; i < recvPktSeq->seqLen; i++) {
                    memset(recvPktSeq->pkts[i], '\0', MAX_BUFFER);
                }
                recvPktSeq->seqLen = 0;
            } else {
                strncpy(recvPktSeq->pkts[recvPktSeq->seqLen++], recvPkt, MAX_BUFFER);
            }

            // Send ACK packet
            char ackPkt[MAX_BUFFER];
            snprintf(ackPkt, MAX_BUFFER, "%s 1 ACK", recvPktSeqNum);
            int sent = sendto(sockfd, ackPkt, MAX_BUFFER, MSG_WAITALL,
                              (struct sockaddr*)&txAddr, len);
        } else {
            // Fail ACK send
            UNACK_COUNT += 1;
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
            char sendPktSeqNum[8] = "0";
            char* sendPktToken = strtok(sendPkt, " ");
            strncpy(sendPktSeqNum, sendPktToken, strlen(sendPktToken));

            // Send current packet in sequence
            int sent = sendto(sockfd, pktSeq->pkts[i], MAX_BUFFER, MSG_WAITALL,
                              (struct sockaddr*)&rxAddr, len);

            // Await ACK, retry if UNACK
            memset(buffer, '\0', MAX_BUFFER);
            if (recvfrom(sockfd, (char*)buffer, sizeof(buffer), MSG_WAITALL, (struct sockaddr*)&rxAddr, &len) > -1) {
                char pktSeqNum[8] = "0";
                char pktFlag[8] = "0";
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
                REACK_COUNT += 1;
            }
        }
    }
}

/*
 * Main
 */
int main(int argc, char** argv) {
    srand(time(NULL));

    // Args + Grab TXT file for test case
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

    // Populate packet loss chance and packet sequence to test
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
