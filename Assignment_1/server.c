#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Define the port number used for the UDP server.
#define PORT 8081

// -----------------------------------------------------------------------------
// Protocol Constants
// -----------------------------------------------------------------------------

// Unique identifiers used to mark the start and end of each packet.
// These help in verifying the integrity of the packet structure.
#define START_PACKET_IDENTIFIER 0XFFFF   // Indicates the beginning of a packet.
#define END_PACKET_IDENTIFIER  0XFFFF    // Indicates the end of a packet.

// Define client-specific and packet-specific constants.
#define CLIENT_ID 0XFF                   // Predefined client ID for packet verification.
#define MAX_LEN_DATA 255                 // Maximum allowed length of the payload.
#define MAX_CLIENT_ID 255                // Maximum allowed value for a client ID.

// -----------------------------------------------------------------------------
// Packet Type Definitions
// -----------------------------------------------------------------------------

// Define constants for the types of packets that can be exchanged.
#define DATA 0XFF1                      // Represents a data packet containing payload.
#define ACK 0XFFF2                     // Represents an acknowledgment packet.
#define REJECT 0XFFF3                  // Represents a packet sent to reject erroneous data.

// -----------------------------------------------------------------------------
// Reject Sub-Code Definitions
// -----------------------------------------------------------------------------

// When a packet is rejected, a sub-code indicates the specific error condition.
#define REJECT_OUT_OF_SEQUENCE 0XFFF4       // Error: Packet received out of the expected sequence.
#define REJECT_LENGTH_MISMATCH 0XFFF5         // Error: Declared payload length doesn't match the actual payload.
#define REJECT_END_OF_PACKET_MISSING 0XFFF6   // Error: End packet identifier is missing or incorrect.
#define REJECT_DUPLICATE_PACKET  0XFFF7       // Error: Duplicate packet received.

// -----------------------------------------------------------------------------
// Timing and Retransmission Constants
// -----------------------------------------------------------------------------

// Define how long to wait for an ACK and the maximum retransmission attempts.
#define ACK_TIMER_SET 3               // ACK timer setting (e.g., wait 3 seconds before retransmission).
#define MAX_TRIES 3                   // Maximum number of tries before giving up.

// -----------------------------------------------------------------------------
// Error Code Definitions
// -----------------------------------------------------------------------------

// Numeric codes corresponding to each error condition.
#define OUT_OF_SEQUENCE_SEQ_NO 7      // Sequence number for out-of-sequence error.
#define LENGTH_MISMATCH_SEQ_NO  8     // Sequence number for length mismatch error.
#define NO_END_PACKETID_SEQ_NO 9      // Sequence number for missing end packet identifier error.
#define DUPLICATE_PACKET_SEQ_NO 10    // Sequence number for duplicate packet error.

// -----------------------------------------------------------------------------
// Packet Structures
// -----------------------------------------------------------------------------

// Structure for a Data Packet that carries actual payload data from the client.
typedef struct DataPacket{
    uint16_t start_packet_identifier;   // Marker for the start of the packet (should equal START_PACKET_IDENTIFIER).
    uint8_t client_id;                  // Identifier for the client sending this packet.
    uint16_t packet_type;               // Indicates the type of packet (should be DATA for data packets).
    uint8_t seg_no;                     // Sequence number of the packet in the transmission order.
    uint8_t plen;                       // Declared length of the payload.
    char pload[255];                    // The data payload itself.
    uint16_t end_packet_identifier;     // Marker for the end of the packet (should equal END_PACKET_IDENTIFIER).
} DataPacket;

// Structure for an Acknowledgment (ACK) Packet.
// This packet is sent by the server when a data packet is received correctly.
typedef struct AckPacket{
    uint16_t start_packet_identifier;   // Copy of the start packet identifier from the data packet.
    uint8_t client_id;                  // Client ID copied from the data packet.
    uint16_t packet_type;               // Packet type set to ACK.
    uint8_t received_segment_no;        // The segment number of the correctly received data packet.
    uint16_t end_packet_identifier;     // Copy of the end packet identifier from the data packet.
} AckPacket;

// Structure for a Reject Packet.
// This packet is sent by the server when an error is detected in the received data packet.
typedef struct RejectPacket{
    uint16_t start_packet_identifier;   // Copy of the start packet identifier from the data packet.
    uint8_t client_id;                  // Client ID copied from the data packet.
    uint16_t packet_type;               // Packet type set to REJECT.
    uint16_t rej_sub_code;              // Specific sub-code indicating the reason for rejection.
    uint8_t received_segment_no;        // The segment number of the packet that caused the error.
    uint16_t end_packet_identifier;     // Copy of the end packet identifier from the data packet.
} RejectPacket;

// -----------------------------------------------------------------------------
// Helper Functions for Packet Initialization
// -----------------------------------------------------------------------------

// Function to initialize an ACK packet based on a received data packet.
// It copies common fields and sets the packet type to ACK.
AckPacket initializeAck(DataPacket dataPacket){
    AckPacket ackPacket;
    ackPacket.start_packet_identifier = dataPacket.start_packet_identifier; // Set the start identifier.
    ackPacket.client_id = dataPacket.client_id;                           // Set the client ID.
    ackPacket.packet_type = ACK;                                           // Set packet type to ACK.
    ackPacket.end_packet_identifier = dataPacket.end_packet_identifier;     // Set the end identifier.
    return ackPacket;
}

// Function to initialize a Reject packet based on a received data packet.
// It copies common fields and sets the packet type to REJECT.
RejectPacket initializeReject(DataPacket dataPacket){
    RejectPacket rejectPacket;
    rejectPacket.start_packet_identifier = dataPacket.start_packet_identifier; // Set the start identifier.
    rejectPacket.client_id = dataPacket.client_id;                           // Set the client ID.
    rejectPacket.packet_type = REJECT;                                       // Set packet type to REJECT.
    rejectPacket.end_packet_identifier = dataPacket.end_packet_identifier;     // Set the end identifier.
    return rejectPacket;
}

// -----------------------------------------------------------------------------
// Utility Function to Display Packet Contents
// -----------------------------------------------------------------------------

// Function to print out the details of a Data Packet.
// This is useful for debugging and verifying the packet's integrity.
void displayDataPacket (DataPacket dataPack){
    printf("\n\n\n\n");
    printf("Start Packet ID -  %x\n", dataPack.start_packet_identifier);
    printf("Client ID -  %x\n", dataPack.client_id);
    printf("Packet Type -  %x\n", dataPack.packet_type);
    printf("Segment # -  %d\n", dataPack.seg_no);
    printf("Payload Length -  %d\n", dataPack.plen);
    printf("Payload -  %s\n", dataPack.pload);
    printf("End Packet ID -  %x\n", dataPack.end_packet_identifier);
    printf("\n\n\n\n");
}

// -----------------------------------------------------------------------------
// Main Function: Server Setup and Packet Handling Loop
// -----------------------------------------------------------------------------

int main(){
    
    // Declare instances for storing incoming data packets, and packets to be sent as ACK or Reject.
    DataPacket dataPacket;
    AckPacket ackPacket;
    RejectPacket rejectPacket;

    // Set up the server address structure.
    struct sockaddr_in serverAddress;
    socklen_t serverAddrLen;
    int sockfd;
    
    // This variable tracks the expected segment number to ensure packets are in order.
    int expectedPackNum = 1;
    
    // Buffer array to keep track of how many times a packet with a given segment number is received.
    int seq_buffer[50] = {0};
    int time_temp = 0;

    // -----------------------------
    // Socket Creation and Binding
    // -----------------------------

    // Create a UDP socket using IPv4 (AF_INET) and the datagram type (SOCK_DGRAM).
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
    if(sockfd < 0){
        // Error handling: if socket creation fails, output an error message.
        printf("\n ERROR - THE SOCKET COULD NOT BE CREATED.\n");
    }

    // Zero out the serverAddress structure to avoid garbage values.
    bzero(&serverAddress, sizeof(serverAddress));
    // Specify the address family (IPv4).
    serverAddress.sin_family = AF_INET;
    // Allow the server to receive packets from any IP address.
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    // Set the port number, converting it to network byte order.
    serverAddress.sin_port = htons(PORT);
    serverAddrLen = sizeof(serverAddress);

    // Bind the socket to the server address so that it can listen for incoming packets.
    bind(sockfd, (struct sockaddr *) &serverAddress, serverAddrLen);
    // At this point, the server is set up and ready to receive data packets.

    // -----------------------------
    // Main Loop: Receiving and Processing Packets
    // -----------------------------

    // Loop indefinitely to continuously receive packets from the client.
    while(10){
        // Receive a data packet from the client.
        // The recvfrom function fills the dataPacket structure with the incoming packet data.
        time_temp = recvfrom(sockfd, &dataPacket, sizeof(DataPacket), 0, (struct sockaddr *)&serverAddress, &serverAddrLen);
        
        // Display the packet contents for debugging and verification.
        displayDataPacket(dataPacket);

        // Determine the actual payload length using strlen, which calculates the length of the payload string.
        int payloadLength = strlen(dataPacket.pload);

        // Update the sequence buffer to count how many times this segment number is received.
        seq_buffer[dataPacket.seg_no] += 1;
        
        // -----------------------------
        // Error Checking and Handling
        // -----------------------------
        
        // Check 1: Duplicate Packet
        // If the packet with this segment number has already been received, it's a duplicate.
        if(seq_buffer[dataPacket.seg_no] != 1){
            // Initialize a Reject packet based on the received data packet.
            rejectPacket = initializeReject(dataPacket);
            // Set the reject sub-code to indicate a duplicate packet error.
            rejectPacket.rej_sub_code = REJECT_DUPLICATE_PACKET;
            // Send the Reject packet back to the client.
            sendto(sockfd, &rejectPacket, sizeof(rejectPacket), 0, (struct sockaddr *)&serverAddress, serverAddrLen);
            // Increment expectedPackNum to move on to the next expected packet.
            expectedPackNum++;
        }
        // Check 2: Out-of-Sequence Packet
        // If the segment number does not match the expected sequence number, the packet is out-of-sequence.
        else if(dataPacket.seg_no != expectedPackNum){
            rejectPacket = initializeReject(dataPacket);
            // Set the reject sub-code for an out-of-sequence error.
            rejectPacket.rej_sub_code = REJECT_OUT_OF_SEQUENCE;
            sendto(sockfd, &rejectPacket, sizeof(rejectPacket), 0, (struct sockaddr *)&serverAddress, serverAddrLen);
            expectedPackNum++;
        }
        // Check 3: Payload Length Mismatch
        // Compare the declared payload length with the actual length calculated.
        else if(payloadLength != dataPacket.plen){
            rejectPacket = initializeReject(dataPacket);
            // Set the reject sub-code for a payload length mismatch error.
            rejectPacket.rej_sub_code = REJECT_LENGTH_MISMATCH;
            sendto(sockfd, &rejectPacket, sizeof(rejectPacket), 0, (struct sockaddr *)&serverAddress, serverAddrLen);
            expectedPackNum++;
        }
        // Check 4: End Packet Identifier Verification
        // Ensure that the end packet identifier in the received packet matches the expected value.
        else if(dataPacket.end_packet_identifier != END_PACKET_IDENTIFIER){
            rejectPacket = initializeReject(dataPacket);
            // Set the reject sub-code for a missing or incorrect end packet identifier.
            rejectPacket.rej_sub_code = REJECT_END_OF_PACKET_MISSING;
            sendto(sockfd, &rejectPacket, sizeof(rejectPacket), 0, (struct sockaddr *)&serverAddress, serverAddrLen);
            expectedPackNum++;
        }
        // If all checks pass and the packet is received in the correct order.
        else if(dataPacket.seg_no == expectedPackNum){ 
            // Initialize an ACK packet to acknowledge the correct reception.
            ackPacket = initializeAck(dataPacket);
            // Send the ACK packet back to the client.
            sendto(sockfd, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&serverAddress, serverAddrLen);
            // Increment expectedPackNum for the next packet.
            expectedPackNum++;
        }
        
    }

    return 0;
}
