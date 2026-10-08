/*
	Änderung 2.1
*/
#include "VDCCommandAPI.h"
#include <iostream>
#include "VDCEventAPI.h"

#include <chrono>

#define IP		"192.168.0.84"
#define PORT	2243

#define evPort	2244
#define clIP	"192.168.0.135"


int main()
{
	VDCCommandAPI api(JSON);
	ERROR_STRUCT error; //zero initialiting all members

	api.open_command_channel(IP, PORT, error);

	HBASEOBJECT hbo = api.create_baseobject("boName", 1000, BOF_UNDER_CONSTRUCTION, error);

	HBASEOBJECT hbo2 = api.create_baseobject("boName", 1000, BOF_UNDER_CONSTRUCTION, error);


	std::cout << "Error: " << error.status << " | " << error.code << " | " << error.source << std::endl;

	api.close_command_channel();

	return 0;
}

