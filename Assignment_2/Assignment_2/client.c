#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Define the port number for UDP communication
#define PORT 8081

// Protocol Primitives
#define PK_START_ID 0XFFFF    // Start identifier for packets
#define PK_END_ID 0XFFFF      // End identifier for packets
#define CL_ID 0XFF            // Default client identifier
#define MAX_LEN_DATA 255      // Maximum length for data in packet
#define MAX_CL_ID 255         // Maximum allowed client ID

// Access Permission Codes
#define ACCESS_PERM 0XFFF8    // Request for access permission
#define NOT_PAID 0XFFF9       // Subscriber has not paid
#define NOT_EXIST 0XFFFA      // Subscriber does not exist
#define ACCESS_OK 0XFFFB      // Access granted

// Supported Technology Types
#define TECH_2G 2             // 2G technology
#define TECH_3G 3             // 3G technology
#define TECH_4G 4             // 4G technology
#define TECH_5G 5             // 5G technology

// Acknowledgment Timer and Retransmission Parameters
#define ACK_TIMER_SET 3       // Timeout duration in seconds for ACK
#define MAX_TRIES 3           // Maximum retransmission attempts

// Structure representing an access permission request packet
typedef struct permissionPacket {
    uint16_t pk_start_id;  // Packet start identifier
    uint8_t cid;           // Client identifier
    uint16_t permission;   // Permission or response code
    uint8_t seg_no;        // Segment number of the packet
    uint8_t plen;          // Payload length
    uint8_t tech;          // Technology type (2G, 3G, 4G, 5G)
    unsigned long src_sub_no;  // Subscriber number
    uint16_t pk_end_id;    // Packet end identifier
} PermissionPacket;

// Initialize a permission request packet with default header values.
// Note: Fields such as seg_no, plen, tech, and src_sub_no are expected
// to be updated later as needed.
PermissionPacket initializingPermissionPacket() {
    PermissionPacket permissionPacket;
    permissionPacket.pk_start_id = PK_START_ID;
    permissionPacket.cid = CL_ID;
    permissionPacket.permission = ACCESS_PERM;
    permissionPacket.pk_end_id = PK_END_ID;
    return permissionPacket;
}

// Display the contents of a permission packet to the console.
void displayPermissionPacket(PermissionPacket permissionPacket) {
    printf("\n\n");
    printf("Start Packet ID: %x\n", permissionPacket.pk_start_id);
    printf("Client ID: %x\n", permissionPacket.cid);
    printf("Packet Type: %x\n", permissionPacket.permission);
    printf("Segment #: %d\n", permissionPacket.seg_no);
    printf("Payload Length: %d\n", permissionPacket.plen);
    printf("Technology: %d\n", permissionPacket.tech);
    printf("Subscriber Number: %lu\n", permissionPacket.src_sub_no);
    printf("End Packet ID: %x\n", permissionPacket.pk_end_id);
}

int main() {
    PermissionPacket permissionRequestPacket;
    PermissionPacket returnedPacket;

    struct sockaddr_in clAddress;
    int sockfd;
    socklen_t clAddrLen;
    FILE *clientInfoFile;
    char cliInfo[255];
    int time_temp = 0;
    int seqNo = 0;
    int resendCt = 0;

    // Create a UDP socket and check for errors.
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("\nERROR - A SOCKET COULDN'T BE CREATED.\n");
    }

    // Initialize the server address structure with IP and port information.
    bzero(&clAddress, sizeof(clAddress));
    clAddress.sin_family = AF_INET;
    clAddress.sin_addr.s_addr = INADDR_ANY;
    clAddress.sin_port = htons(PORT);
    clAddrLen = sizeof(clAddress);

    // Set a receive timeout of 3 seconds on the socket to handle delayed responses.
    struct timeval setTimer;
    setTimer.tv_sec = 3;
    setTimer.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &setTimer, sizeof(setTimer));

    // Initialize the permission request packet with default header values.
    permissionRequestPacket = initializingPermissionPacket();

    // Open the payload file that contains client information.
    clientInfoFile = fopen("payload.txt", "rt");
    if (clientInfoFile == NULL) {
        printf("\nERROR - FILE NOT FOUND\n");
    }

    // Process 5 packets based on client data from the payload file.
    for (int i = 0; i < 5; i++) {
        seqNo++;         // Increment packet sequence number.
        resendCt = 0;    // Reset retransmission counter.
        time_temp = 0;   // Reset timer variable.
        char *cliInfoParts;

        // Read a line of client information and tokenize it.
        if (fgets(cliInfo, sizeof(cliInfo), clientInfoFile) != NULL) {
            // Extract subscriber number.
            cliInfoParts = strtok(cliInfo, " ");
            permissionRequestPacket.plen = strlen(cliInfoParts);
            permissionRequestPacket.src_sub_no = (unsigned long)strtol(cliInfoParts, (char **)NULL, 10);

            // Extract technology type.
            cliInfoParts = strtok(NULL, " ");
            permissionRequestPacket.tech = atoi(cliInfoParts);
            permissionRequestPacket.plen += strlen(cliInfoParts);

            // Set the segment number for the packet.
            cliInfoParts = strtok(NULL, " ");
            permissionRequestPacket.seg_no = seqNo;
        }

        // Attempt to send the packet and wait for an acknowledgment.
        while (time_temp <= 0 && resendCt < MAX_TRIES) {
            printf("\nPacket #%d is sent", seqNo);
            displayPermissionPacket(permissionRequestPacket);

            // Send the permission packet to the server.
            sendto(sockfd, &permissionRequestPacket, sizeof(PermissionPacket), 0,
                   (struct sockaddr *)&clAddress, clAddrLen);
            
            // Wait for a response from the server.
            time_temp = recvfrom(sockfd, &returnedPacket, sizeof(PermissionPacket), 0, NULL, NULL);
            printf("\n\n");

            if (time_temp <= 0) {
                // No acknowledgment received; increment retransmission counter.
                printf("\nERROR - NO ACK RECEIVED FROM SERVER.\n");
                printf("RE-TRANSMITTING THE PACKET.\n");
                resendCt++;
            } else if (returnedPacket.permission == NOT_PAID) {
                printf("\nINFO - SUBSCRIBER %lu HAS NOT PAID FOR THE SERVICE.\n", permissionRequestPacket.src_sub_no);
            } else if (returnedPacket.permission == NOT_EXIST) {
                printf("\nINFO - SUBSCRIBER %lu DOESN'T EXIST ON THE SERVER.\n", permissionRequestPacket.src_sub_no);
            } else if (returnedPacket.permission == ACCESS_OK) {
                printf("\nINFO - SUBSCRIBER %lu IS GRANTED PERMISSION FOR THE SERVICE\n", permissionRequestPacket.src_sub_no);
            }

            // If maximum retransmission attempts are reached, exit with an error.
            if (resendCt >= MAX_TRIES) {
                printf("\nERROR - SERVER NOT RESPONDING.\n");
                exit(0);
            }
        }

        // Print a newline for clear separation between packets.
        printf("\n\n");
    }

    // Close the payload file after processing.
    fclose(clientInfoFile);

    return 0;
}
