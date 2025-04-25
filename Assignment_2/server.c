#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Define the UDP port on which the server will listen.
#define PORT 8081

// Total number of subscribers maintained in the verification database.
#define NUM_OF_SUBS 10

// Access Permission Codes used for verifying subscriber status.
#define ACCESS_PERM 0XFFF8    // Code indicating an access permission request.
#define NOT_PAID 0XFFF9       // Code indicating the subscriber has not paid.
#define NOT_EXIST 0XFFFA      // Code indicating the subscriber does not exist.
#define ACCESS_OK 0XFFFB      // Code indicating that access is granted.

// Structure representing the permission packet exchanged between client and server.
typedef struct permissionPacket {
    uint16_t start_packet_identifier; // Identifier marking the start of the packet.
    uint8_t client_id;                // Identifier for the client sending the packet.
    uint16_t permission;              // Permission or response code.
    uint8_t seg_no;                   // Packet segment number.
    uint8_t plen;                     // Payload length (in bytes).
    uint8_t technology;               // Technology type (e.g., 2G, 3G, etc.).
    unsigned long src_sub_no;         // Subscriber number.
    uint16_t end_packet_identifier;   // Identifier marking the end of the packet.
} PermissionPacket;

// Structure for storing server-side subscriber data.
typedef struct ServerData {
    unsigned long sub_info;  // Subscriber number information.
    uint8_t technology;      // Technology type associated with the subscriber.
    int status;              // Subscription status: -1 (not found), 0 (not paid), or 1 (access granted).
} ServerData;

// Initialize a response packet based on the received packet.
// Copies all the common fields so that only the permission code is updated later.
PermissionPacket initializingPermissionPacket(PermissionPacket receivedPacket) {
    PermissionPacket sendPacket;
    sendPacket.start_packet_identifier = receivedPacket.start_packet_identifier;
    sendPacket.client_id = receivedPacket.client_id;
    sendPacket.seg_no = receivedPacket.seg_no;
    sendPacket.plen = receivedPacket.plen;
    sendPacket.technology = receivedPacket.technology;
    sendPacket.src_sub_no = receivedPacket.src_sub_no;
    sendPacket.end_packet_identifier = receivedPacket.end_packet_identifier;
    return sendPacket;
}

// Read the subscriber verification data from "Verification_Database.txt" and load it into the serverData array.
// Each line in the file should contain: subscriber number, technology type, and subscription status.
void getServerData(ServerData serverData[]) {
    char userInfo[50];
    int iterator = 0;
    FILE *dataFile;

    dataFile = fopen("Verification_Database.txt", "rt");
    if (dataFile == NULL) {
        printf("\nERROR - THE FILE DOESN'T EXIST. PLEASE CHECK THE FOLDER.\n");
    }

    while (fgets(userInfo, sizeof(userInfo), dataFile) != NULL) {
        // Tokenize the input line and extract subscriber info.
        char *cliInfoParts = strtok(userInfo, " ");
        serverData[iterator].sub_info = (unsigned long)strtol(cliInfoParts, (char **)NULL, 10);
        serverData[iterator].technology = atoi(strtok(NULL, " "));
        serverData[iterator].status = atoi(strtok(NULL, " "));
        iterator++;
    }    

    fclose(dataFile);
}

// Verify a subscriber's information against the server's database.
// Returns the subscriber's status if found, or -1 if the subscriber does not exist.
int verifyUser(ServerData serverData[], unsigned long src_sub_no, uint8_t technology) {
    int verify = -1;
    for (int i = 0; i < NUM_OF_SUBS; i++) {
        if ((serverData[i].sub_info == src_sub_no) && (serverData[i].technology == technology)) {
            return serverData[i].status;
        }
    }
    return verify;
}

// Display the contents of a permission packet to the console for debugging purposes.
void displayPermissionPacket(PermissionPacket permissionPacket) {
    printf("\n\n");
    printf("Start Packet ID: %x\n", permissionPacket.start_packet_identifier);
    printf("Client ID: %x\n", permissionPacket.client_id);
    printf("Packet Type: %x\n", permissionPacket.permission);
    printf("Segment #: %d\n", permissionPacket.seg_no);
    printf("Payload Length: %d\n", permissionPacket.plen);
    printf("Technology: %d\n", permissionPacket.technology);
    printf("Subscriber Number: %lu\n", permissionPacket.src_sub_no);
    printf("End Packet ID: %x\n", permissionPacket.end_packet_identifier);
}

int main() {
    PermissionPacket sendPacket;
    PermissionPacket receivedPacket;
    ServerData serverData[NUM_OF_SUBS];

    int sockfd;
    struct sockaddr_in serverAddress;
    socklen_t serverAddrLen;
    int time_temp = 0; 

    // Create a UDP socket for the server.
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
    if (sockfd < 0) {
        printf("\nERROR - A SOCKET COULDN'T BE CREATED.\n");
    }

    // Configure the server address structure with IP and port information.
    bzero(&serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT);
    serverAddrLen = sizeof(serverAddress);

    // Bind the socket to the server address and port.
    bind(sockfd, (struct sockaddr *) &serverAddress, serverAddrLen);

    // Load the subscriber data from the verification database file.
    getServerData(serverData);

    // Continuously listen for incoming packets from clients.
    while (1) {
        // Receive a packet from a client.
        time_temp = recvfrom(sockfd, &receivedPacket, sizeof(PermissionPacket), 0, (struct sockaddr *)&serverAddress, &serverAddrLen);
        displayPermissionPacket(receivedPacket);

        // If a valid packet is received and it is an access permission request, process it.
        if ((time_temp >= 0) && (receivedPacket.permission == ACCESS_PERM)) { 
            // Initialize the response packet based on the received packet.
            sendPacket = initializingPermissionPacket(receivedPacket);
            
            // Verify the subscriber's details against the server data.
            int verify = verifyUser(serverData, receivedPacket.src_sub_no, receivedPacket.technology);
            if (verify == -1) {
                sendPacket.permission = NOT_EXIST; // Subscriber not found.
            } else if (verify == 0) {
                sendPacket.permission = NOT_PAID;  // Subscriber exists but has not paid.
            } else if (verify == 1) {
                sendPacket.permission = ACCESS_OK; // Subscriber exists and has paid.
            }
            // Send the response packet back to the client.
            sendto(sockfd, &sendPacket, sizeof(PermissionPacket), 0, (struct sockaddr *) &serverAddress, serverAddrLen);
        }
        printf("\n\n");
    }

    return 0;
}
