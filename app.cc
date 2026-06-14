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
	
	
	state Menu_Print:
		ser_outf(Menu_Print, "\r\nGroup%i Device #%i (%i/%i records)");
		ser_out(Menu_Print, "(G)roup ID\r\n(N)ew device ID\r\n(F)ind neighbour\r\n(R)etrieve record from neighbour\r\n(S)how local records\r\nR(e)set local storage\r\n\r\n");
		ser_out(Menu_Print, "Selection: ");
	
	
	state Read_Choice:
		// read the user input 
		ser_inf(Read_Choice, "%d", &temp);
		
		// if "G" or "g", state Ask_Group_ID
		if ((choice == 'G') || (choice == 'g'))
		{
			proceed Ask_Group_ID;	
		}
		
		
		// if "N" or "n", state Ask_Node
		if ((choice == 'N') || (choice == 'n'))
		{
			proceed Ask_Node_ID;	
		}
		
		
		
	state Ask_Group_ID:
		// user chose "G" or "g", ask for group ID
		int group_id;
		
		// ask user for the node group ID 
		ser_out(Ask_Group_ID, "Enter Group ID: ");
		
		// update node group ID store in node
		
		
		// show the menu again 
		proceed Menu_Print;
		
	
	state Ask_Node_ID:
		// user chose "N" or N, ask for node ID
		int node_id;
		
		// ask user for node ID
		ser_out(Ask_Node_ID, "Enter Node ID: ");
		
		
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
