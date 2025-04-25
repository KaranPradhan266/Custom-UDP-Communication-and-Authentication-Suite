#include <stdio.h>      // Standard I/O for printing and file operations
#include <stdlib.h>     // Standard library for exit() and other utilities
#include <string.h>     // For string manipulation functions such as strcpy() and strlen()
#include <netdb.h>      // Definitions for network database operations (gethostbyname, etc.)
#include <sys/socket.h> // Core socket definitions and functions for socket programming
#include <sys/types.h>  // Data types used in system calls
#include <netinet/in.h> // Internet address family structures and functions
#include <arpa/inet.h>  // Functions for converting between host and network byte order
#include <time.h>       // Time functions (if needed for timeouts, logging, etc.)

// Define the server's port number on which it listens for incoming UDP packets.
#define PORT 8081

// Define protocol primitives for the custom packet structure
#define START_PACKET_IDENTIFIER 0XFFFF  // Unique identifier marking the start of a packet
#define END_PACKET_IDENTIFIER 0XFFFF    // Unique identifier marking the end of a packet
#define CLIENT_ID 0XFF                  // Unique identifier for the client
#define MAX_LEN_DATA 255                // Maximum length for data payload in a packet
#define MAX_CLIENT_ID 255               // Maximum value for a client identifier

// Packet types for the protocol
#define DATA 0XFF1                     // Packet type indicating a data packet
#define ACK 0XFFF2                     // Packet type indicating an acknowledgment packet
#define REJECT 0XFFF3                  // Packet type indicating a reject (error) packet

// Sub-codes for reject packet errors, which help to identify the type of error encountered
#define REJECT_OUT_OF_SEQUENCE 0XFFF4          // Reject error if packets are received out-of-sequence
#define REJECT_LENGTH_MISMATCH 0XFFF5          // Reject error if payload length does not match the specified length
#define REJECT_END_OF_PACKET_MISSING 0XFFF6    // Reject error if the end packet identifier is missing
#define REJECT_DUPLICATE_PACKET 0XFFF7         // Reject error if a duplicate packet is detected

// Timer and retransmission constants used in the client logic
#define ACK_TIMER_SET 3               // Time in seconds to wait for an ACK from the server
#define MAX_TRIES 3                   // Maximum number of retransmission attempts before giving up

// Specific sequence numbers that trigger packet errors (for testing or simulation purposes)
#define OUT_OF_SEQUENCE_SEQ_NO 7      // Sequence number to simulate an out-of-sequence error
#define LENGTH_MISMATCH_SEQ_NO 8      // Sequence number to simulate a length mismatch error
#define NO_END_PACKETID_SEQ_NO 9      // Sequence number to simulate missing end packet identifier error
#define DUPLICATE_PACKET_SEQ_NO 10    // Sequence number to simulate a duplicate packet error

// Structure defining the layout of a Data Packet in this protocol
typedef struct DataPacket {
    uint16_t start_packet_identifier; // Start identifier for the packet (fixed value)
    uint8_t client_id;                // Client identifier
    uint16_t packet_type;             // Indicates the type of packet (DATA, ACK, or REJECT)
    uint8_t seg_no;                   // Segment or sequence number of the packet
    uint8_t plen;                     // Length of the payload data
    char pload[255];                  // Actual payload data (character array)
    uint16_t end_packet_identifier;   // End identifier for the packet (fixed value)
} DataPacket;

// Structure defining the layout of an Acknowledgment (ACK) Packet
typedef struct AckPacket {
    uint16_t start_packet_identifier; // Start identifier for the ACK packet
    uint8_t client_id;                // Client identifier
    uint16_t packet_type;             // Packet type (should be ACK for this structure)
    uint8_t received_segment_no;      // The segment number that has been acknowledged
    uint16_t end_packet_identifier;   // End identifier for the packet
} AckPacket;

// Structure defining the layout of a Reject Packet used for error reporting
typedef struct RejectPacket {
    uint16_t start_packet_identifier; // Start identifier for the reject packet
    uint8_t client_id;                // Client identifier
    uint16_t packet_type;             // Packet type (should be REJECT for this structure)
    uint16_t rej_sub_code;            // Sub-code specifying the error type (e.g., out-of-sequence)
    uint8_t received_segment_no;      // The segment number that caused the error
    uint16_t end_packet_identifier;   // End identifier for the packet
} RejectPacket;

// Function: initializeDataPacket
// Purpose: Sets up a DataPacket with the basic fixed fields (start and end identifiers, client ID, and packet type).
DataPacket initializeDataPacket() {
    DataPacket dataPacket;
    dataPacket.start_packet_identifier = START_PACKET_IDENTIFIER; // Set start of packet ID
    dataPacket.client_id = CLIENT_ID;                             // Set client identifier
    dataPacket.packet_type = DATA;                                // Specify this is a data packet
    dataPacket.end_packet_identifier = END_PACKET_IDENTIFIER;     // Set end of packet ID
    return dataPacket;
}

// Function: initializeAck
// Purpose: Initializes an AckPacket with standard header values.
AckPacket initializeAck() {
    AckPacket ackPacket;
    ackPacket.start_packet_identifier = START_PACKET_IDENTIFIER; // Set start of packet ID
    ackPacket.client_id = CLIENT_ID;                             // Set client identifier
    ackPacket.packet_type = ACK;                                 // Specify this is an ACK packet
    ackPacket.end_packet_identifier = END_PACKET_IDENTIFIER;     // Set end of packet ID
    return ackPacket;
}

// Function: initializeReject
// Purpose: Initializes a RejectPacket with standard header values. Error-specific fields will be set later.
RejectPacket initializeReject() {
    RejectPacket rejectPacket;
    rejectPacket.start_packet_identifier = START_PACKET_IDENTIFIER; // Set start of packet ID
    rejectPacket.client_id = CLIENT_ID;                             // Set client identifier
    rejectPacket.packet_type = REJECT;                              // Specify this is a reject packet
    rejectPacket.end_packet_identifier = END_PACKET_IDENTIFIER;      // Set end of packet ID
    return rejectPacket;
}

// Function: displayDataPacket
// Purpose: Prints the content of a DataPacket to the console for debugging/monitoring purposes.
void displayDataPacket(DataPacket dataPack) {
    printf("Start Packet ID -  %x\n", dataPack.start_packet_identifier);
    printf("Client ID - %x\n", dataPack.client_id);
    printf("Packet Type -  %x\n", dataPack.packet_type);
    printf("Segment # -  %d\n", dataPack.seg_no);
    printf("Payload Length -  %d\n", dataPack.plen);
    printf("Payload -  %s\n", dataPack.pload);
    printf("End Packet ID -  %x\n", dataPack.end_packet_identifier);
}

int main() {
    // Declare variables for packet structures and network operations.
    DataPacket dataPacket;      // Data packet to be sent
    RejectPacket packetReceived; // Packet to store response from server (either ACK or REJECT)

    struct sockaddr_in clAddress; // Structure to store server address information
    int sockfd;                  // Socket file descriptor for network communication
    socklen_t clAddrLen;         // Length of the address structure
    FILE *payloadFile;           // File pointer to read payload data from a text file
    char fpload[255];            // Buffer to temporarily store payload data read from file
    int time_temp = 0;           // Temporary variable used to store the result of recvfrom() (used for timeout detection)
    int seqNo = 0;               // Sequence number counter for packets
    int resendCt = 0;            // Counter for the number of retransmission attempts

    // SOCKET CREATION
    // Create a UDP socket using IPv4 addressing. If socket creation fails, print an error message.
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("\nERROR - A SOCKET COULDN'T BE CREATED.\n");
    }

    // Initialize the server address structure and zero out memory to avoid garbage values.
    bzero(&clAddress, sizeof(clAddress));
    clAddress.sin_family = AF_INET;                  // Set address family to IPv4
    clAddress.sin_addr.s_addr = htonl(INADDR_ANY);     // Accept any incoming interface
    clAddress.sin_port = htons(PORT);                // Set the server port (convert to network byte order)
    clAddrLen = sizeof(clAddress);                   // Set the length of the address structure
    
    // SETTING A TIMEOUT FOR RECEIVE OPERATIONS
    // Configure the socket to timeout after 3 seconds if no ACK is received from the server.
    struct timeval setTimer;
    setTimer.tv_sec = 3;                             // Timeout in seconds
    setTimer.tv_usec = 0;                            // Additional microseconds (set to zero)
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &setTimer, sizeof(struct timeval));

    // Initialize the DataPacket with default header values.
    dataPacket = initializeDataPacket();

    // OPEN THE PAYLOAD FILE
    // Open the file "payload.txt" in read-text mode. This file contains the data to be sent in each packet.
    payloadFile = fopen("payload.txt", "rt");
    if (payloadFile == NULL) {
        printf("\nERROR - FILE NOT FOUND\n");
    }

    // LOOP TO SEND 10 PACKETS
    // Iterate 10 times to send 10 different packets with incrementing sequence numbers.
    for (int i = 0; i < 10; i++) {
        seqNo++;           // Increment the sequence number for the current packet
        resendCt = 0;      // Reset retransmission counter for this packet
        time_temp = 0;     // Reset the timeout indicator

        // Read a line from the payload file into fpload. If successful, copy it into the packet's payload.
        if (fgets(fpload, sizeof(fpload), payloadFile) != NULL) {
            strcpy(dataPacket.pload, fpload);
        }
        // Set the payload length based on the string length
        dataPacket.plen = strlen(dataPacket.pload);
        // Assign the current sequence number to the data packet
        dataPacket.seg_no = seqNo;

        // SIMULATING ERRORS BASED ON PREDEFINED SEQUENCE NUMBERS
        // When a specific sequence number is reached, intentionally modify packet parameters to simulate errors.
        if (seqNo == OUT_OF_SEQUENCE_SEQ_NO) {
            // Simulate an out-of-sequence packet by artificially increasing the segment number.
            dataPacket.seg_no += 8;
        } else if (seqNo == LENGTH_MISMATCH_SEQ_NO) {
            // Simulate a length mismatch error by increasing the payload length without modifying the actual payload.
            dataPacket.plen += 6;
        } else if (seqNo == NO_END_PACKETID_SEQ_NO) {
            // Simulate a missing end packet identifier error by setting it to zero.
            dataPacket.end_packet_identifier = 0;
        } else if (seqNo == DUPLICATE_PACKET_SEQ_NO) {
            // Simulate a duplicate packet error by reusing a previous sequence number (packet 1).
            dataPacket.seg_no = 1;
        }

        // Ensure that if the error for missing end packet identifier is not simulated, 
        // then the end_packet_identifier is reset to its defined constant.
        if (seqNo != NO_END_PACKETID_SEQ_NO) {
            dataPacket.end_packet_identifier = END_PACKET_IDENTIFIER;
        }

        // SEND THE PACKET AND WAIT FOR ACK/REJECT RESPONSE
        // Attempt to send the packet and wait for a response, with retransmissions if necessary.
        while (time_temp <= 0 && resendCt < MAX_TRIES) {
            // Display packet details for debugging purposes.
            printf("\n\n");
            printf("Packet #%d sent.\n", seqNo);
            displayDataPacket(dataPacket);

            // Send the data packet to the server using UDP sendto()
            sendto(sockfd, &dataPacket, sizeof(DataPacket), 0, (struct sockaddr *)&clAddress, clAddrLen);

            // Attempt to receive a response from the server.
            // The response could be either an ACK or a REJECT packet.
            time_temp = recvfrom(sockfd, &packetReceived, sizeof(RejectPacket), 0, NULL, NULL);

            printf("\n\n");
            printf("Server Response.\n");

            // Check if no response was received within the timeout period.
            if (time_temp <= 0) {
                printf("\nERROR - NO ACK RECEIVED FROM SERVER.\n");
                printf("RE-TRANSMITTING THE PACKET.\n");
                resendCt++;  // Increment the retransmission counter
            } 
            // If an ACK is received, confirm successful transmission.
            else if (packetReceived.packet_type == ACK) {
                printf("\nACK FOR PACKET# %d HAS BEEN SENT FROM SERVER\n", seqNo);
            } 
            // Handle various types of rejection based on the reject sub-code.
            else if ((packetReceived.packet_type == REJECT) && (packetReceived.rej_sub_code == REJECT_OUT_OF_SEQUENCE)) {
                printf("\nERROR - REJECT PACKET RECEIVED.\n");
                printf("\nREJECT PACKET SUB-CODE - %x.\n", packetReceived.rej_sub_code);
                printf("\nOUT OF SEQUENCE PACKET SENT.\n");
            } else if ((packetReceived.packet_type == REJECT) && (packetReceived.rej_sub_code == REJECT_LENGTH_MISMATCH)) {
                printf("\nERROR - REJECT PACKET RECEIVED.\n");
                printf("\nREJECT PACKET SUB-CODE - %x.\n", packetReceived.rej_sub_code);
                printf("\nLENGTH MIS-MATCH PACKET SENT.\n");
            } else if ((packetReceived.packet_type == REJECT) && (packetReceived.rej_sub_code == REJECT_END_OF_PACKET_MISSING)) {
                printf("\nERROR - REJECT PACKET RECEIVED.\n");
                printf("\nREJECT PACKET SUB-CODE - %x.\n", packetReceived.rej_sub_code);
                printf("\nEND OF PACKET ID MISSING.\n");
            } else if ((packetReceived.packet_type == REJECT) && (packetReceived.rej_sub_code == REJECT_DUPLICATE_PACKET)) {
                printf("\nERROR - REJECT PACKET RECEIVED.\n");
                printf("\nREJECT PACKET SUB-CODE - %x.\n", packetReceived.rej_sub_code);
                printf("\nDUPLICATE PACKET SENT.\n");
            }

            // If the maximum number of retransmission attempts has been reached, exit the program with an error.
            if (resendCt >= MAX_TRIES) {
                printf("\nERROR - SERVER NOT RESPONDING.\n");
                exit(0);
            }
            printf("\n\n");
        }

        // Print a separator for the completion of the current packet's transmission process.
        printf("\n\n");
    }

    // Close the file pointer once all packets have been processed.
    fclose(payloadFile);

    return 0;
}
