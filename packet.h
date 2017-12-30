/*Shannon Tom, Michelle Tran
  CS 118 Winter 2017
  Project 2 
  Packet Header
*/

#ifndef PACKET_INCLUDED
#define PACKET_INCLUDED


#define PACKET_SIZE 1024
#define DATA_SIZE 1010


#define TYPE_DATA 1
#define TYPE_SYN 2
#define TYPE_ACK 3
#define TYPE_FIN 4
#define TYPE_SYNACK 5


//Packet is of size 1024 bytes

struct packet {
	int seq;
	int nextSeq;
	int length; // length of data 
	short type; 
		/*   PACKET TYPES:
			1 - DATA
			2 - SYN
			3 - ACK  - seq num will be ack number
			4 - FIN
			5 - SYNACK
		*/
	char data[DATA_SIZE];
};

#endif //PACKET_INCLUDED