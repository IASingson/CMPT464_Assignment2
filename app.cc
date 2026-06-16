// assignment 2 

#include "sysio.h"
#include "ser.h"
#include "serf.h"
#include "tcv.h"

#include "phys_cc1350.h"
#include "plug_null.h"

#define MAX_PACKET_LENGTH 250
#define MAX_MES_LEN 14

int sfd = -1; //session descriptor 

int node_ID;
int group_ID;
int stored_records;
int max_records;
byte delete_request_num;
byte delete_destination_id;
byte delete_response_received;
byte delete_response_status;

// following used for Find protocols
byte find_neighbors[20]; //array for neighbor id's
int find_neighbor_count;
int find_iteration;

//record structure
struct Record{
	byte used; //0 for empty, 1 for used
	byte owner_id; //owner id of the record
	int timestamp; //stores the timestamp of the record when it was made
	char record[20]; //stores the record data of up to 20 characters
};
//database storage for up to 40 records
struct Record database[40];

//temp for testing
fsm Create{
	state Skip:
		finish;
}
fsm Retrieve{
	state Skip:
		finish;
}



fsm Find{
	// When E or e recieved, will loop through 2 iterations
	// to find neighbors, will store recieved message and
	// Then print the results once complete
	address packet;
	char *read_packet;
	int find_neighbor_count;
	int find_iteration;

	state Reset_Neighbors:
		// For resetting neighbor list
		find_neighbor_count = 0;
		find_iteration = 0;
		proceed Send_Discovery_Request;
	
	state Send_Discovery_Request:
		find_iteration++;
		// Create a discovery request
		packet = tcv_wnp(Send_Discovery_Request, sfd, 8);
		packet[0] = 0; //Network ID
		read_packet = (char *)(packet + 1);

		//packet format: Group ID 2 bytes / message type 1 byte / padding
		*read_packet++ = (group_ID >> 8) & 0xFF;
		*read_packet++ = group_ID & 0xFF;
		*read_packet++ = 1; //message type for discovery request
		*read_packet++ = 0; //padding
		*read_packet++ = 0; //padding
		*read_packet++ = 0; //padding
		*read_packet++ = 0; //padding

		tcv_endp(packet);
		proceed Wait_For_Responses;
	
	state Wait_For_Responses:
		// wait 3 sec for incoming response
		delay(3000, Check_Responses);

	state Check_Responses:
		//checks whether 2 iterations
		if (find_iteration >= 2) {
			proceed Print_Neighbors;
		}

		proceed Send_Discovery_Request;
	
	state Print_Neighbors:
		// print list following format
		// “\r\n Neighbors: [Listof Neighbors]” 
		// where [List of Neighbors] should be replaced by the neighbors’ IDs.
		ser_outf(Print_Neighbors, "\r\n Neighbors: ");
		for (int i = 0; i < find_neighbor_count; i++) {
			ser_outf(Print_Neighbors, "%d ", find_neighbors[i]);
		}
		finish; 
}

fsm Find_Reciever{
	address packet;
	address response;
	char *read_packet;
	char *write_packet;
	int packet_group;
	byte msg_type;
	byte sender_id;
	int i;

	state Wait_Message:
		//recieve packets
		packet = tcv_rnp(Wait_Message, sfd);
		read_packet = (char *)(packet + 1);

		// first byte is network id, next 2 bytes are group id, next byte is message type, next byte is sender id
		packet_group = ((byte)read_packet[0] << 8) | (byte)read_packet[1];
		msg_type = read_packet[2];
		sender_id = read_packet[4];

		// Two different messages can be recieved, one is by other nodes
		// Attempting to find neighbors, the other is from neighbors
		// Responding to our discovery request, the message type will determine
		// how we handle the message

		// discovery request
		if (msg_type == 1) {
			proceed Discovery_Request;
		}

		// discovery response (only in find fsm)
		if (msg_type == 2) {
			proceed Discovery_Response;
		}

		// Ignores other messages
		tcv_endp(packet);
		proceed Wait_Message;
	
	state Discovery_Request:
		// if group id matches and sender id is not this node, send response
		if (packet_group == group_ID && sender_id != node_ID) {
			tcv_endp(packet);
			proceed Send_Discovery_Response;
		}
		// Otherwise ignore the message
		else{
			tcv_endp(packet);
			proceed Wait_Message;
		}
	
	state Discovery_Response:
		// if group id matches and sender id is not this node, add sender id to neighbor list
		if (packet_group == group_ID && sender_id != node_ID) {
			// check if sender id is already in neighbor list
			int found = 0;
			for (i = 0; i < find_neighbor_count; i++) {
				if (find_neighbors[i] == sender_id) {
					found = 1;
					break;
				}
			}
			// if not found, add to neighbor list
			if (!found && find_neighbor_count < 20) {
				find_neighbors[find_neighbor_count++] = sender_id;
			}
		}
		tcv_endp(packet);
		proceed Wait_Message;

	state Send_Discovery_Response:
		// Creates the 8 byte response packet
		// Format Group ID 2 bytes / message type 1 byte / sender id 1 byte / padding
		response = tcv_wnp(Send_Discovery_Response, sfd, 8);
		response[0] = 0; //Network ID
		write_packet = (char *)(response + 1);

		*write_packet++ = (group_ID >> 8) & 0xFF;
		*write_packet++ = group_ID & 0xFF;
		*write_packet++ = 2; //message type for discovery response
		*write_packet++ = node_ID; //sender id
		*write_packet++ = 0; //padding
		*write_packet++ = 0; //padding
		*write_packet++ = 0; //padding
		*write_packet++ = 0; //padding

		tcv_endp(response);
		proceed Wait_Message;
}

fsm Delete{
	address packet;
	char *read_packet;
	int destination_id;
	int record_index;
	byte request_num;
	//prompts the user for the destination node ID to delete
	state Ask_Destination_ID:
		ser_out(Ask_Destination_ID, "Destination node ID: ");
		ser_inf(Ask_Destination_ID, "%d", &destination_id);
	//prompts the user of the record index to delete
	state Ask_Record_Index:
		ser_out(Ask_Record_Index, "Enter record index: ");
		ser_inf(Ask_Record_Index, "%d", &record_index);
	//prepares the delete request packet
	state Send_delete_request:
		//prepares the delete operation
		delete_request_num = (byte)rnd();
		delete_destination_id = (byte)destination_id;
		delete_response_received = 0;
		//creates a packet to send the delete request to the destination node with 8 bytes of payload
		packet = tcv_wnp(Send_delete_request, sfd, 8);
		//sets network id to 0
		packet[0] = 0;
		read_packet = (char *)(packet +1);

		*read_packet++ = (group_ID >> 8) & 0xFF;
		*read_packet++ = group_ID & 0xFF;
		//sets message type to 3 to indicate a delete request
		*read_packet++ = 3;
		//stores the request number in the packet
		*read_packet++ = delete_request_num;
		//stores the current node ID as the sender ID in the packet
		*read_packet++ =  node_ID;
		//stores the receiver id of the delete request in the packet
		*read_packet++ = delete_destination_id;
		//stores the record index to be deleted in the packet
		*read_packet++ = record_index;
		//sets the rest of the packet to 0 for padding
		*read_packet++ = 0;
		
		tcv_endp(packet);
	
	
	//waits for a response from the destination node
	state Wait_Response:

		if (delete_response_received){
			proceed Print_Result;
		}
		delay(3000, Timeout);
	//if no response was received print failed to reach....
	state Timeout:
		if (delete_response_received){
			proceed Print_Result;
		}

		ser_out(Timeout, "\r\nFailed to reach the destination");
		finish;
	//prints the results
	state Print_Result:
		if (delete_response_status == 0x01){
			ser_out(Timeout, "\r\nRecord deleted");
		}
		else{
			ser_outf(Timeout, "\r\nThe record does not exit on node %d", delete_destination_id);
		}
		finish;
}
fsm Delete_Receiver{
	address packet;
	address response;
	
	char *read_packet;
	char *write_packet;
	int packet_group;
	int i;

	byte request_num;
	byte type;
	byte request_number;
	byte sender_id;
	byte receiver_id;
	byte record_index;
	byte status;
	state wait_packet:
		//receives a packets
		packet = tcv_rnp(wait_packet, sfd);
		read_packet = (char *)(packet + 1);
		//reads the packet group type request number sender id and reciever id from the packet
		packet_group = ((byte)read_packet[0] <<8) | (byte)read_packet[1];
		type = read_packet[2];
		request_number = read_packet[3];
		sender_id = read_packet[4];
		receiver_id = read_packet[5];
		//if the packet is the reponse message to delete record message (5)
		if(type == 5){
			status = read_packet[6];
			// check if it's for this node and if it's the node that sent the delete request and if it matches the request number
			if(receiver_id == node_ID && sender_id == delete_destination_id && request_number == delete_request_num){
				delete_response_received = 1;
				delete_response_status = status;
			}
			tcv_endp(packet);
			proceed wait_packet;
		}
		//if the packet is a delete record message (3)
		if (type != 3){
			tcv_endp(packet);
			proceed wait_packet;
		}
		record_index = read_packet[6];
		proceed handle_delete;

	state handle_delete:
	//if node id doesn't match the message group id the message is ignored
		if (packet_group != group_ID || receiver_id != node_ID){
			tcv_endp(packet);
			proceed wait_packet;
		}
		//if the record index is out of bounds or the record is not used, set status to 0x03 to indicate the delete operation failed
		if (record_index >= max_records || database[record_index].used == 0){
			status = 0x03;
		}
		//if the record exists then clear the reecord and set the status to 0x01 to indicate the delete operation was successful
		else{
			database[record_index].used = 0;
			database[record_index].owner_id = 0;
			database[record_index].timestamp = 0;
			database[record_index].record[0] = '\0';
			stored_records--;
			status = 0x01;
		}
		tcv_endp(packet);
		proceed send_response;

	state send_response:
	//allocates space for a response packet with 28 bytes of payload
		response = tcv_wnp(send_response, sfd, 28);
		// sets the first byte of the packet to 0 for the network ID (always needs to be set to 0)
		response[0] = 0;
		//initilises a pointer to write the packet payload starting from the second byte of the packet (i.e starting at group ID)
		write_packet = (char *)(response + 1);
		//writes the information to the packet
		*write_packet++ = (group_ID >> 8) & 0xFF;
		*write_packet++ = group_ID & 0xFF;
		*write_packet++ = 5;
		*write_packet++ = request_number;
		*write_packet++ = node_ID;
		*write_packet++ = sender_id;
		*write_packet++ = status;
		//padding byte
		*write_packet++ = 0;
		//fills the remaining bytes of the packet with 0
		for (i = 0; i < 20; i++) {
			write_packet[i] = 0;
		}
		//transmits the packet
		tcv_endp(response);
		proceed wait_packet;
}
fsm root
{
	char choice;
	int temp;
	
	state Init:
		phys_cc1350 (0, MAX_PACKET_LENGTH);
		
		tcv_plug (0, &plug_null);
		sfd = tcv_open(WNONE, 0, 0);
		tcv_control (sfd, PHYSOPT_ON, NULL);
		
		if (sfd < 0)
		{
			diag("Cannot open tcv interface");
			halt();
		}
		runfsm Delete_Receiver;
	
	
	state Menu_Print:
		ser_outf(Menu_Print, "\r\nGroup%d Device #%d (%d/%d records)", group_ID, node_ID, stored_records, 40);
		ser_out(Menu_Print, "(G)roup ID\r\n(N)ew device ID\r\n(F)ind neighbour\r\n(R)etrieve record from neighbour\r\n(S)how local records\r\nR(e)set local storage\r\n\r\nSelection: ");
	
	state Read_Choice:
		// read the user input 
		ser_inf(Read_Choice, "%d", &temp);
		
		// if "G" or "g", state Ask_Group_ID
		if ((choice == 'G') || (choice == 'g'))
		{
			proceed Ask_Group_ID;	
		}
		
		// if "N" or "n", state Ask_Node
		else if ((choice == 'N') || (choice == 'n'))
		{
			proceed Ask_Node_ID;	
		}
		
		// if "F", run Find protocol
		else if ((choice == 'F') || (choice == 'f'))
		{
			runfsm Find; // change to match fsm name if different
		}
		
		// if "C, run Create protocol
		else if ((choice == 'C') || (choice == 'c'))
		{
			runfsm Create;
		}
		
		// if "D", run Delete protocol
		else if ((choice == 'D') || (choice == 'd'))
		{
			runfsm Delete;
		}
		
		// if "R", run the Retrieve protocol
		else if ((choice == 'R') || (choice == 'r'))
		{
			runfsm Retrieve;
		}
		
		// if "S", go to Show_LocalDB state
		else if ((choice == 'S') || (choice == 's'))
		{
			proceed Show_LocalDB;
		}
		// if "E", go to Reset_DB state
		else if ((choice == 'E') || (choice == 'e'))
		{
			proceed Reset_DB;
		}
		
		// any other input, show menu again
		else
		{
			proceed Menu_Print;
		}
		
		
		
		
	state Ask_Group_ID:
		// ask for group ID 
		 
		ser_out(Ask_Group_ID, "Enter Group ID: ");
		
		// update node group ID 
		
		
		// show the menu again 
		proceed Menu_Print;
		
	
	state Ask_Node_ID:
		// ask for Node ID 
		ser_out(Ask_Node_ID, "Enter Node ID: ");
		
		//update node ID
		
		
		// show the menu 
		proceed Menu_Print;
		
		
	state Read_Node:
		// reads from the specified node given by user
		
	state Show_LocalDB:
		// user chose "S" or "s", 
		// format: index    Time Stamp     owner ID     Record Data
	
	
	state Reset_DB:
		// user chose "E" or "e", delete all the nodes 
}

