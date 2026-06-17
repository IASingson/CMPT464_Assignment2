// assignment 2 

#include "sysio.h"
#include "ser.h"
#include "serf.h"
#include "tcv.h"

#include "phys_cc1350.h"
#include "plug_null.h"

#define MAX_PACKET_LENGTH 250
#define MAX_MES_LEN 14

//define message types
#define MSG_TYPE_DISC_REQ 0
#define MSG_TYPE_DISC_RESP 1
#define MSG_TYPE_CREATE 2
#define MSG_TYPE_DELETE 3
#define MSG_TYPE_RETRIEVE 4
#define MSG_TYPE_RESPONSE 5

//set up status messages
#define STATUS_SUCCESS 0x01 // when an operation is successful
#define STATUS_DB_FULL 0x02 // when trying to create a record but the database is full
#define STATUS_DEL_FAIL 0x03 // when trying to delete a record that doesn't exist
#define STATUS_RETRIEVE_FAIL 0x04 // when trying to retrieve a record that doesn't exist

int sfd = -1; //session descriptor 

int node_ID = 0; //initial node ID is 0
short group_ID = 0; //initial group ID is 0
int stored_records;
int max_records;
byte delete_request_num;
byte delete_destination_id;
byte delete_response_received;
byte delete_response_status;

byte pending_req;
byte pending_dest;
byte pending_type;
byte pending_active;

// following used for Find protocols
byte find_neighbors[20]; //array for neighbor id's
int find_neighbor_count;
int find_iteration;
int find_complete = 0;
int delete_finished = 0;
int create_finished = 0;
int retrieve_finished = 0;

int destination_id;

byte find_req_num = 0;
byte find_active = 0;

// pending
// usedd for keeping track of requests that we have sent and are waiting for a response for, used in both delete and retrieve protocols
byte pending_type = 0;
byte pending_request_num = 0; 
byte pending_destination_id = 0;
byte pending_active = 0; // used for both delete and retrieve, indicates whether there is a pending request that we are waiting for a response for

byte reply_received = 0;
byte reply_status = 0;
byte reply_record[20]; // max record length

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
	address packet;
	char *p;

	int dest_id;
	char record_data[20];

	int i;
	

	state ASK_DEST:
		ser_out(ASK_DEST, "\r\nDestination node ID: ");
		proceed READ_DEST;

	state READ_DEST:
		ser_inf(READ_DEST, "%d", &dest_id);

		if (dest_id < 1|| dest_id > 25){
			ser_out(READ_DEST, "\r\nInvalid destination node ID");
			finish;
		}

		for (i = 0; i < 20; i++){
			record_data[i] = '\0';
		}

		proceed ASK_RECORD;

	state ASK_RECORD:
		ser_out(ASK_RECORD, "\r\nRecord string: ");
		proceed READ_RECORD;

	state READ_RECORD:
		ser_in(READ_RECORD, record_data, 20 - 1); // max 19 char + \0

		for (i = 0; i < 20; i++){
			if (record_data[i] == '\r' || record_data[i] == '\n'){
				record_data[i] = '\0';
				break;
			}
		}
		record_data[19] = '\0';

		proceed SEND_CREATE;

	 state SEND_CREATE:
        pending_req = (byte)rnd();
        pending_destination_id = (byte)destination_id;
        pending_type = MSG_TYPE_CREATE;
        pending_active = 1;

        reply_received = 0;
        reply_status = 0;

        packet = tcv_wnp(SEND_CREATE, sfd, 7);

        packet[0] = 0;
        p = (char *)(packet);

        *p++ = (group_ID >> 8) & 0xFF;
        *p++ = group_ID & 0xFF;
        *p++ = MSG_TYPE_CREATE;
        *p++ = pending_req;
        *p++ = node_ID;
        *p++ = pending_destination_id;

		// no status or index here so skip 'arg' field
        for (i = 0; i < 20; i++) {
            *p++ = record_data[i];
        }

        tcv_endp(packet);
		proceed WAIT_RESPONSE;

    state WAIT_RESPONSE:
        if (reply_received) {
            proceed GOT_RESPONSE;
        }

        delay(3000, TIMEOUT);
        release;
		proceed TIMEOUT;

	// do we need this tho?
    state TIMEOUT:
        if (reply_received) {
            proceed GOT_RESPONSE;
        }

        pending_active = 0;
        ser_out(TIMEOUT, "\r\nFailed to reach the destination");
		create_finished = 1;
        finish;

    state GOT_RESPONSE:
        pending_active = 0;

        if (reply_status == STATUS_SUCCESS) {
            ser_out(GOT_RESPONSE, "\r\n Data Saved");
        } else {
            ser_outf(GOT_RESPONSE,
                     "\r\n The record can't be saved on node %d",
                     pending_destination_id);
        }
		create_finished = 1;
        finish;
}

fsm Retrieve {
    address packet;
    char *p;

    int dest_id;
    int record_index;

    state ASK_DEST:
        ser_out(ASK_DEST, "\r\nDestination node ID: ");
		proceed READ_DEST;

    state READ_DEST:
        ser_inf(READ_DEST, "%d", &dest_id);

		// invalid dest id
        if (dest_id < 1 || dest_id > 25) {
            ser_out(READ_DEST, "\r\nInvalid destination node ID");
            finish;
        }
		proceed ASK_INDEX;
		
    state ASK_INDEX:
        ser_out(ASK_INDEX, "\r\nEnter record index: ");
		proceed READ_INDEX;

    state READ_INDEX:
        ser_inf(READ_INDEX, "%d", &record_index);

        if (record_index < 0 || record_index >= 40) {
            ser_out(READ_INDEX, "\r\nInvalid record index");
            finish;
        }
		proceed SEND_RETRIEVE;

    state SEND_RETRIEVE:
        pending_req = (byte)rnd();
        pending_destination_id = (byte)dest_id;
        pending_type = MSG_TYPE_RETRIEVE;
        pending_active = 1;

        reply_received = 0;
        reply_status = 0;
        reply_record[0] = '\0';

        packet = tcv_wnp(SEND_RETRIEVE, sfd, 8);

        packet[0] = 0;
        p = (char *)(packet + 1);

        *p++ = (group_ID >> 8) & 0xFF;
        *p++ = group_ID & 0xFF;
        *p++ = MSG_TYPE_RETRIEVE;
        *p++ = pending_req;
        *p++ = node_ID;
        *p++ = pending_destination_id;
        *p++ = (byte)record_index;

        tcv_endp(packet);
		proceed WAIT_RESPONSE;

    state WAIT_RESPONSE:
        if (reply_received) {
            proceed GOT_RESPONSE;
        }

        delay(3000, TIMEOUT);
        release;
		proceed TIMEOUT;

    state TIMEOUT:
        if (reply_received) {
            proceed GOT_RESPONSE;
        }

        pending_active = 0;
        ser_out(TIMEOUT, "\r\nFailed to reach the destination");
		retrieve_finished = 1;
        finish;

    state GOT_RESPONSE:
        pending_active = 0;

        if (reply_status == STATUS_SUCCESS) {
            ser_outf(GOT_RESPONSE,
                     "\r\n Record Received from %d: %s",
                     pending_destination_id,
                     reply_record);
        } else {
            ser_outf(GOT_RESPONSE,
                     "\r\n The record does not exist on node %d",
                     pending_destination_id);
        }
		retrieve_finished = 1;
        finish;
}

fsm Receiver{
	address packet;
	address response;

	char *p;
	char *q;

	int packet_group;
	byte msg_type;
	byte request_num;
	byte sender_id;
	byte receiver_id;
	byte arg;
	byte status;

	int i;
	int temp_create;
	int found;

	state RECEIVE:
		packet = tcv_rnp(RECEIVE, sfd);
		proceed PROCESS;
	
	state PROCESS:
		p = (char *)(packet + 1);
		packet_group = (((int)((byte)p[0])) << 8) | ((int)((byte)p[1]));
		msg_type = p[2];
		request_num = p[3];
		sender_id = p[4];
		receiver_id = p[5];
		
		// now determine how to handle the message based on the type
		if (msg_type == MSG_TYPE_RESPONSE){
			status = p[6]; // for response messages, the 7th byte is the status of the operation

			if (packet_group == group_ID &&  // check if packet from same group
				receiver_id == node_ID && // check if packet intended for receiver
				pending_active && // check if we are expecting a packet to come
				sender_id == pending_destination_id && // check if sender is the same as dest we are expecting
				request_num == pending_request_num) {// check if the request number is the same
					// if all true then
					reply_status = status; 
					reply_received = 1;

					if (pending_type == MSG_TYPE_RETRIEVE && status == STATUS_SUCCESS){
						for (int i=0; i < 20; i++){
							reply_record[i] = p[7 + i]; // 7 for offset for response. 
						}
						reply_record[20 - 1] = '\0';
					}
				}
				tcv_endp(packet);
				proceed RECEIVE;
			}

		if (packet_group != group_ID || receiver_id != node_ID){
			tcv_endp(packet);
			proceed RECEIVE;
		}
		if (msg_type == MSG_TYPE_CREATE){
			proceed HANDLE_CREATE;
		}
		if (msg_type == MSG_TYPE_DELETE){
			proceed HANDLE_DELETE;
		}
		if (msg_type == MSG_TYPE_RETRIEVE){
			proceed HANDLE_RETRIEVE;
		}
		if (msg_type == MSG_TYPE_DISC_REQ){
			proceed HANDLE_DISC_REQ;
		}
		if (msg_type == MSG_TYPE_DISC_RESP){
			proceed HANDLE_DISC_RESP;
		}

		tcv_endp(packet);
		proceed RECEIVE;

	state HANDLE_DISC_REQ:
		// group, type = 0, req-num, sender, rec = 0
		// we respond only when same group, rec = 0
		if (packet_group != group_ID || receiver_id !=0){
			tcv_endp(packet);
			proceed RECEIVE;
		}
		tcv_endp(packet);
		proceed SEND_DISC_RESP;
	
	state SEND_DISC_RESP:
		// packet struct:
		// group, type = 1, req_num, sender, rec,
		// 10 bytes total?
		// 2 +1 +1 +1 +1 + 4; 4 from network id and crc
		// so 10 total

		response = tcv_wnp(SEND_DISC_RESP, sfd, 6);

		response[0] = 0;
		q = (char *)(response + 1);
		*q++ = (group_ID >> 8) & 0xFF;
		*q++ = group_ID & 0xFF;
		*q++ = request_num;
		*q++ = node_ID;
		*q++ = sender_id;
		tcv_endp(response);
		proceed RECEIVE;
	
	state HANDLE_DISC_RESP:
		if (packet_group == group_ID &&
			receiver_id == node_ID &&
			find_active &&
			request_num == find_req_num){
				found = 0;

				for (i = 0; i < find_neighbor_count; i++){
					if (find_neighbors[i] == sender_id){
						found = 1;
						break;
					}
				}
		}
		if (!found && find_neighbor_count < 20){
			find_neighbors[find_neighbor_count] == sender_id;
			find_neighbor_count++;
		}
		tcv_endp(packet);
		proceed RECEIVE;
	
	// handle create request
	state HANDLE_CREATE:
		temp_create = -1;

		// find an empty slot in the database
		for (i =0; i < 40; i++){
			if (database[i].used == 0){
				temp_create = i;
				break;
			}
		}

		if (temp_create < 0){ // if no empty slots in database
			status = STATUS_DB_FULL;
		}
		else{ // if available slot in database
			// set record params
			database[temp_create].used = 1;
			database[temp_create].owner_id = sender_id;
			database[temp_create].timestamp = seconds();

			// write record in database
			// p[0..6] = group, type, req, sender, receiver.
			for (i = 0; i < 20; i++){
				database[temp_create].record[i] = p[6 + i]; // 6 from offset for record in packet
			}

			// set null terminator in message
			database[temp_create].record[20 - 1] = '\0';

			stored_records++;
			status = STATUS_SUCCESS;
		}
		tcv_endp(packet);
		proceed SEND_CREATE_RESPONSE;
	
	state SEND_CREATE_RESPONSE:
		response = tcv_wnp(SEND_CREATE_RESPONSE, sfd, 11); // 11 from main + arg + 4 (6 + 1 + 4); 4 from netword id and CRC
		
		response[0] = 0;
		q = (char *)(response + 1);

		*q++ = (group_ID >> 8) & 0xFF;
		*q++ = group_ID & 0xFF;
		*q++ = MSG_TYPE_RESPONSE;
		*q++ = request_num;
		*q++ = node_ID;
		*q++ = sender_id;
		*q++ = status;
		
		tcv_endp(response);
		proceed RECEIVE;
	
	state HANDLE_DELETE:
		arg = p[6]; // arg offset

		// if selected record is out of range
		// or empty record is selected
		if (arg >= 40 || database[arg].used == 0){
			status = STATUS_DEL_FAIL; // set status
		}
		else{
			// set database record fields to 0
			// set first char of record field as null terminator
			database[arg].used = 0;
			database[arg].owner_id = 0;
			database[arg].timestamp = 0;
			database[arg].record[0] = '\0';

			// if more than 0 records stored then 
			if (stored_records > 0){
				stored_records--;
			}
			status = STATUS_SUCCESS; // successful delete operation
		}
		tcv_endp(packet);
		proceed SEND_DELETE_RESPONSE;
	
	state SEND_DELETE_RESPONSE:
		response = tcv_wnp(SEND_DELETE_RESPONSE, sfd, 11); // 11 from main + arg + 4

		response[0] = 0;
		q = (char *)(response + 1);

		*q++ = (group_ID >> 8) & 0xFF;
		*q++ = group_ID & 0xFF;
		*q++ = MSG_TYPE_RESPONSE;
		*q++ = request_num;
		*q++ = node_ID;
		*q++ = sender_id;
		*q++ = status;

		tcv_endp(response);
		proceed RECEIVE;
	
	state HANDLE_RETRIEVE:
		arg = p[6]; // get the index you try to retrieve

		// accessing out of bound or empty record
		if (arg >= 40 || database[arg].used == 0){
			status = STATUS_RETRIEVE_FAIL; // empty status
		}
		else{ // if valid record set status to OK
			status = STATUS_SUCCESS;
		}

		tcv_endp(packet);

		// if valid record is found, go to state where it sends the record
		if (status == STATUS_SUCCESS){
			proceed SEND_RETRIEVE_OK_RESP;
		}
		// if no record is found, go to state where it sends fail response
		else{
			proceed SEND_RETRIEVE_FAIL_RESP;
		}

	state SEND_RETRIEVE_OK_RESP:
		response = tcv_wnp(SEND_RETRIEVE_OK_RESP, sfd, 31);
		// 31 from main + status + record[20] + 4
		// main = group, type, req, sender, receiver
		response[0] = 0;
		q = (char *)(response + 1);

		*q++ = (group_ID >> 8) & 0xFF;
		*q++ = group_ID & 0xFF;
		*q++ = MSG_TYPE_RESPONSE;
		*q++ = request_num;
		*q++ = node_ID;
		*q++ = sender_id;
		*q++ = STATUS_SUCCESS; // ok status to response

		// put the record to packet
		for (i=0; i < 20; i++){
			*q++ = database[arg].record[i];
		}

		tcv_endp(response);
		proceed RECEIVE;

	state SEND_RETRIEVE_FAIL_RESP:
		response = tcv_wnp(SEND_RETRIEVE_FAIL_RESP, sfd, 11);
		// 11 from main + arg + 4

		response[0] = 0;
		q = (char *)(response + 1);

		*q++ = (group_ID >> 8) & 0xFF;
		*q++ = group_ID & 0xFF;
		*q++ = MSG_TYPE_RESPONSE;
		*q++ = request_num;
		*q++ = node_ID;
		*q++ = sender_id;
		*q++ = STATUS_RETRIEVE_FAIL; // retrieve fail to response

		tcv_endp(response);
		proceed RECEIVE;
}

fsm Find{
	// When E or e recieved, will loop through 2 iterations
	// to find neighbors, will store recieved message and
	// Then print the results once complete
	address packet;
	char *read_packet;
	int neighbor_print_index;

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
		packet[1] = 0; //Padding for network ID
		read_packet = (char *)(packet + 2);

		//format is Group ID 2 bytes / message type 1 byte / request number 1 byte / sender id 1 byte / receiver id 1 byte / padding
		*read_packet++ = (group_ID >> 8) & 0xFF;    // Group ID high
		*read_packet++ = group_ID & 0xFF;           // Group ID low
		*read_packet++ = 0;                         // Type = 0 (Discovery Request)
		*read_packet++ = (byte)rnd();               // Request Number (random)
		*read_packet++ = node_ID;                   // Sender ID
		*read_packet++ = 0;                         // Receiver ID = 0 (broadcast)

		tcv_endp(packet);
		proceed Wait_For_Responses;
	
	state Wait_For_Responses:
		// wait 3 sec for incoming response
		delay(3000, Check_Responses);

	state Check_Responses:
		//checks whether 2 iterations
		if (find_iteration >= 2) {
			proceed Print_Neighbors_Header;
		}

		proceed Send_Discovery_Request;
	
	state Print_Neighbors_Header:
		// print list following format
		// “\r\n Neighbors: [Listof Neighbors]” 
		// where [List of Neighbors] should be replaced by the neighbors’ IDs.
		neighbor_print_index = 0;
		ser_outf(Print_Neighbors_Header, "\r\n Neighbors: ");
		proceed Print_Neighbor_Item;

	state Print_Neighbor_Item:
		if (neighbor_print_index >= find_neighbor_count) {
			proceed Finish_Find;
		}
		ser_outf(Print_Neighbor_Item, "%d ", find_neighbors[neighbor_print_index]);
		neighbor_print_index++;
		proceed Print_Neighbor_Item;

	state Finish_Find:
		find_complete = 1;
		finish;
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
		proceed Ask_Destination_ID_In;
	state Ask_Destination_ID_In:
		ser_inf(Ask_Destination_ID_In, "%d", &destination_id);
		proceed Ask_Record_Index;
	//prompts the user of the record index to delete
	state Ask_Record_Index:
		ser_out(Ask_Record_Index, "Enter record index: ");
		proceed Ask_Record_Index_In;
	state Ask_Record_Index_In:
		ser_inf(Ask_Record_Index_In, "%d", &record_index);
		proceed Send_delete_request;
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
		proceed Wait_Response;
	
	
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
		delete_finished = 1;
		finish;
	//prints the results
	state Print_Result:
		if (delete_response_status == 0x01){
			ser_out(Print_Result, "\r\nRecord deleted");
		}
		else{
			ser_outf(Print_Result, "\r\nThe record does not exit on node %d", delete_destination_id);
		}
		delete_finished = 1;
		finish;
}

fsm root
{
	char choice;
	int db_print_index;

	int temp; int i;

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

		stored_records = 0;
		max_records = 40;

		create_finished = 0;
		retrieve_finished = 0;

		runfsm Receiver;

		proceed Menu_Print;
	
	
	state Menu_Print:
		ser_outf(Menu_Print, "\r\nGroup %d Device #%d (%d/%d records)", group_ID, node_ID, stored_records, 40);
		proceed Menu_Print2;
	state Menu_Print2:
		ser_out( Menu_Print2,
			"\r\n(G)roup ID"
			"\r\n(N)ew device ID"
			"\r\n(F)ind neighbour"
			"\r\n(C)reate record on neighbor"
			"\r\n(D)elete record on neighbor"
			"\r\n(R)etrieve record from neighbour"
			"\r\n(S)how local records"
			"\r\nR(e)set local storage"
			"\r\n\r\nSelection: ");
		proceed Read_Choice;
	
	state Read_Choice:
		choice = '0';
		// read the user input 
		ser_inf(Read_Choice, "%c", &choice);
		proceed Read_Choice_Print;

	state Read_Choice_Print:
		ser_outf(Read_Choice_Process, "%c\r\n", choice);
		proceed Read_Choice_Process;
	
	state Read_Choice_Process:
		
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
			proceed Handle_Find;
		}
		
		// if "C, run Create protocol
		else if ((choice == 'C') || (choice == 'c'))
		{
			proceed Handle_Create;
		}
		
		// if "D", run Delete protocol
		else if ((choice == 'D') || (choice == 'd'))
		{
			proceed Handle_Delete;
		}
		
		// if "R", run the Retrieve protocol
		else if ((choice == 'R') || (choice == 'r'))
		{
			proceed Handle_Retrieve;
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

		release;
		
		// Handle states run the corresponding fsm and then 
		// Run a state to wait for their process to finish
		// Follows by displaying menu again


	state Handle_Create:
		create_finished = 0;
		runfsm Create;
		proceed Wait_Create_Finish;

	state Wait_Create_Finish:
		if (create_finished == 1){
			proceed Menu_Print;
		}
		else{
			proceed Wait_Delete_Finish;
		}
		release;
	
	state Handle_Retrieve:
		retrieve_finished=  0;
		runfsm Retrieve;
		proceed Wait_Retrieve_Finish;
	
	state Wait_Retrieve_Finish:
		if(retrieve_finished == 1){
			proceed Menu_Print;
		}
		else{
			proceed Wait_Retrieve_Finish;
		}
		release;
		
	state Handle_Delete:
		// run the delete protocol
		delete_finished = 0;
		runfsm Delete;
		proceed Wait_Delete_Finish;
		
	state Wait_Delete_Finish:
		if (delete_finished == 1) {
			proceed Menu_Print;
		}
		else{
			proceed Wait_Delete_Finish;
		}
		release;

	state Handle_Find:
		// run the delete protocol
		find_complete = 0;
		runfsm Find;
		proceed Wait_Find_Finish;
		
	state Wait_Find_Finish:
		if (find_complete == 1) {
			proceed Menu_Print;
		}
		delay(100, Wait_Find_Loop);
	state Wait_Find_Loop:
		proceed Wait_Find_Finish;

	
		
		// NEW GROUP ID
	state Ask_Group_ID:
		// ask for group ID 
		ser_out(Ask_Group_ID, "Enter Group ID: ");
		proceed Ask_Group_ID_In;
	state Ask_Group_ID_In:
		ser_inf(Ask_Group_ID_In, "%d", (int*)&group_ID);
		proceed Group_Id_Finish;
	state Group_Id_Finish:
		ser_outf(Group_Id_Finish, "%d\r\n\r\n", group_ID);
		proceed Menu_Print;

		// NEW NODE ID
	state Ask_Node_ID:
		// ask for Node ID 
		ser_out(Ask_Node_ID, "Enter Node ID: ");
		proceed Ask_Node_ID_In;
	state Ask_Node_ID_In:
		ser_inf(Ask_Node_ID_In, "%d", (int*)&node_ID);
		proceed Node_Id_Finish;
	state Node_Id_Finish:
		ser_outf(Node_Id_Finish, "%d\r\n\r\n", node_ID);
		proceed Menu_Print;
		
	state Show_LocalDB:
		// user chose "S" or "s", 
		// format: index    Time Stamp     owner ID     Record Data
		db_print_index = 0;
		ser_out(Show_LocalDB, "\r\nIndex\tTime Stamp\tOwner ID\tRecord Data\r\n");
		proceed Show_LocalDB_Item;
	state Show_LocalDB_Item:
		// Prints one single line for each entry	
		if (db_print_index >= max_records) {
			ser_out(Show_LocalDB_Item, "\r\n");
			proceed Menu_Print;
		}
		ser_outf(Show_LocalDB_Item, "%d\t%d\t%d\t%s\r\n", db_print_index, database[db_print_index].timestamp, database[db_print_index].owner_id, database[db_print_index].record);
		db_print_index++;
		proceed Show_LocalDB_Item;
	
	
	state Reset_DB:
		// user chose "E" or "e", delete all the nodes 
		stored_records = 0;
		for (int i = 0; i < 40; i++) {
			database[i].used = 0;
			database[i].owner_id = 0;
			database[i].timestamp = 0;
			database[i].record[0] = '\0';
		}
		ser_out(Reset_DB, "\r\n");
		proceed Menu_Print;
}