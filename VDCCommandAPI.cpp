/*
	Initial
*/
#include <iostream>

/* For Memory Management  */
#include <new>

/* API Header */
#include "VDCCommandAPI.h"

/* Support Headers */
#include "JSON_Encoding_Macros.h"
#include "fIDs.h"
#include "VDC_ErrorCodes.h"

#include <intrin.h>
#include <sstream>
#include <ctime>

#define SER_DEBUG   false

/***************************************************************************************************/
/* Private Implementation */
/***************************************************************************************************/
class VDCCommandAPI::Impl
{
public:

	/* private memebr to hold serialization type */
	uint8_t serialization;

	/* Socket Variables */
	VDC_Socket ConnectSocket;

	/* Channel Flag */
	bool CommChannelOpen;

	/* Converts the Json::Value object argument to a corresponding string */
	std::string Json_Serialize(Json::Value Data)
	{
		std::string json_String;
		Json::JSON_STRING_WRITER st_writer; //styled or fast writer

		json_String = st_writer.write(Data); //any possible exceptions thrown ??

											 /* Removing the new line 0a added by the write function */
		json_String.pop_back();

		return json_String; //has new line in the stringS
	}
	
	/* Serialitation Functions for Binary_1 Serialitation */
	unsigned char* init_dArray(size_t size)
	{
		unsigned char* dArray = new (std::nothrow) unsigned char[size];  // This memory is deleted in the send function
		if (dArray == NULL) {
			std::cout << "Memory Could not be allocated" << std::endl;
			std::exit(EXIT_FAILURE);
		}
		return dArray;
	}
	void pushData(VDC_API::STRING data, unsigned char * dArray, int index)
	{
		pushData((VDC_API::LONG)data.length(), dArray, index); //string has to be prceeded by 4 length bytes.
		strncpy((char*)dArray + index + size_LONG, data.c_str(), data.length());
	}
	void pushData(VDC_API::BOOL data, unsigned char * dArray, int index)
	{
		dArray[index] = (char)data;
	}
	void pushData(VDC_API::SHORT data, unsigned char * dArray, int index)
	{
		data = htons(data);
		memcpy(dArray + index, &data, size_SHORT);
	}
	void pushData(VDC_API::LONG data, unsigned char * dArray, int index)
	{
		data = htonl(data);
		memcpy(dArray + index, &data, size_LONG);
	}
	void pushData(VDC_API::LLONG data, unsigned char * dArray, int index)
	{
		data = htonll(data);
		memcpy(dArray + index, &data, size_LLONG);
	}
	void pushData(VDC_API::DOUBLE data, unsigned char * dArray, int index)
	{
		int8_t arr[8];
		memcpy(arr, &data, size_DOUBLE);
		for (int i = 0; i < 8; i++)
			dArray[index + i] = arr[7 - i];

	}
	void pushData(VDC_API::VECTOR data, unsigned char * dArray, int index)
	{
		pushData(data.x, dArray, index);
		pushData(data.y, dArray, index + size_LLONG);
		pushData(data.z, dArray, index + size_LLONG + size_LLONG);
	}
	void pushData(VDC_API::MATRIX data, unsigned char * dArray, int index)
	{
		pushData(data.v0, dArray, index);
		pushData(data.v1, dArray, index + size_VECTOR);
		pushData(data.v2, dArray, index + size_VECTOR + size_VECTOR);
		pushData(data.v3, dArray, index + size_VECTOR + size_VECTOR + size_VECTOR);
	}
	void DateTimeStringToDateTimeLongs(const VDC_API::STRING & datetime, VDC_API::LONG & no_of_days, VDC_API::LONG & no_of_ms)
	{
		bool data_format = true;

		/* Checking the data_format-- y-m-d or d-m-y */
		std::string first_S = datetime.substr(0, datetime.find("-"));
		std::string last_S;

		size_t pos = 0;
		for (int i = 0; i < 2; i++)
		{
			pos = datetime.find("-");
			last_S = datetime.substr(0, pos + 1);
		}

		std::cout << "First = " << first_S << std::endl;
		std::cout << "Last = " << last_S << std::endl;

		if (first_S.size() == 4 && last_S.size() == 2)
			data_format = false; //y-m-d


		VDC_API::SHORT year = 0, mnth = 0, days = 0, hr = 0, mins = 0, sec = 0;
		VDC_API::LONG  ms = 0;

		VDC_API::SSHORT sep1, sep2, sep3, sepT, sepZ;

		/* parsing according to the inout data_format possibilities */
		if (data_format)
			std::stringstream(datetime) >> days >> sep1 >> mnth >> sep1 >> year >> sepT >> hr >> sep2 >> mins >> sep2 >> sec >> sep3 >> ms >> sepZ;
		else
			std::stringstream(datetime) >> year >> sep1 >> mnth >> sep1 >> days >> sepT >> hr >> sep2 >> mins >> sep2 >> sec >> sep3 >> ms >> sepZ;

		std::cout << "Year = " << year;
		std::cout << " Month = " << mnth;
		std::cout << " Day = " << days;
		std::cout << " Hour = " << hr;
		std::cout << " Minutes = " << mins;
		std::cout << " Seconds = " << sec;
		std::cout << " Milliseconds = " << ms << std::endl;;


		if (mnth < 3)
			year--, mnth += 12;
		no_of_days = 365 * year + year / 4 - year / 100 + year / 400 + (153 * mnth - 457) / 5 + days - 306 - (365 * 1991 + 1991 / 4 - 1991 / 100 + 1991 / 400 + (153 * 13 - 457) / 5 + 1 - 306);

		no_of_ms = hr * 60 * 60 * 1000 + mins * 60 * 1000 + sec * 1000 + ms;

		std::cout << "Number of Days = " << no_of_days << std::endl;
		std::cout << "Number of MS = " << no_of_ms << std::endl;

	}

	/***************************************************************************************************/
	/***************************************************************************************************/
	/*Encodes the byte array in a bigEndian format to the Command Message struct */
	Command_Message Encode_Data(uint16_t F_ID, unsigned char* data, size_t data_len)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);			//converts form host (short-end) to TCP/IP (big-end) byte order  
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;
		VDC_Command.MsgData.Data = data;
		VDC_Command.length = htonl(Msg_Descr_Size + data_len);  //converts form host (short-end) to TCP/IP (big-end) byte order

		return VDC_Command;
	}

	/*
	Encodes the string  to the Command Message struct
	Note: Data here is in the Big Endian Format
	This function requires the call to the send function to clear the memory allocated in this function
	*/
	Command_Message Encode_Data(uint16_t F_ID, std::string data)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);   //converts form host (short-end) to TCP/IP (big-end) byte order  
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;

		VDC_Command.MsgData.Data = new (std::nothrow) unsigned char[data.length()];
		if (VDC_Command.MsgData.Data == NULL) {
			std::cout << "Memory Could not be allocated" << std::endl;
			std::exit(EXIT_FAILURE);
		}//Data Freed at the end of send vdc function

		if (serialization == JSON) {
			/* The c_str() function adds a 0x00 at the end of string, using strncpy to restrict the number of bytes copied to the char array */
			strncpy((char*)VDC_Command.MsgData.Data, data.c_str(), data.length());
			VDC_Command.length = htonl(Msg_Descr_Size + data.length());  //converts form host (short-end) to TCP/IP (big-end) byte order... length does not have null termination and new line
		}
		else if (serialization == BINARY_1) {
			/* In case of Binary_1 Serialization the string is followed with 4 bytes length info */
			pushData((VDC_API::LONG)data.length(), VDC_Command.MsgData.Data, 0);
			strncpy((char*)VDC_Command.MsgData.Data + 4, data.c_str(), data.length());
			VDC_Command.length = htonl(Msg_Descr_Size + data.length() + 4);
		}
		return VDC_Command;
	}

	/* Encode the Mesg_Descr when the data to send is NULL */
	Command_Message Encode_Data(uint16_t F_ID)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;
		VDC_Command.MsgData.Data = NULL;
		VDC_Command.length = htonl(Msg_Descr_Size);

		return VDC_Command;
	}

	/* Encode when the data to send is BOOL */
	Command_Message Encode_Data(uint16_t F_ID, VDC_API::BOOL data)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;

		VDC_Command.MsgData.Data = new (std::nothrow) unsigned char[size_BOOL]; //freed at the end of send function
		if (VDC_Command.MsgData.Data == NULL) {
			std::cout << "Memory Could not be allocated" << std::endl;
			std::exit(EXIT_FAILURE);
		}//Data Freed at the end of send vdc function

		VDC_Command.MsgData.Data[0] = (char)data;
		VDC_Command.length = htonl(Msg_Descr_Size + 1); //1 byte for bool

		return VDC_Command;
	}
	/* Encode when the data to send is SHORT */
	Command_Message Encode_Data(uint16_t F_ID, VDC_API::SHORT data)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;

		VDC_Command.MsgData.Data = new (std::nothrow) unsigned char[size_SHORT]; //freed at the end of send function
		if (VDC_Command.MsgData.Data == NULL) {
			std::cout << "Memory Could not be allocated" << std::endl;
			std::exit(EXIT_FAILURE);
		}//Data Freed at the end of send vdc function

		 /* Copying the Data to the Command Message Struct */
		data = htons(data);// Network byte order
		memcpy(VDC_Command.MsgData.Data, &data, size_SHORT);

		VDC_Command.length = htonl(Msg_Descr_Size + size_SHORT);

		return VDC_Command;
	}
	/* Encode when the data to send is LONG */
	Command_Message Encode_Data(uint16_t F_ID, VDC_API::LONG data)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;

		VDC_Command.MsgData.Data = new (std::nothrow) unsigned char[size_LONG]; //freed at the end of send function
		if (VDC_Command.MsgData.Data == NULL) {
			std::cout << "Memory Could not be allocated" << std::endl;
			std::exit(EXIT_FAILURE);
		}//Data Freed at the end of send vdc function

		 /* Copying the Data to the Command Message Struct */
		data = htonl(data); //BIG ENDIAN
		memcpy(VDC_Command.MsgData.Data, &data, size_LONG);

		VDC_Command.length = htonl(Msg_Descr_Size + size_LONG);

		return VDC_Command;
	}
	/* Encode when the data to send is LLONG */
	Command_Message Encode_Data(uint16_t F_ID, VDC_API::LLONG data)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;

		VDC_Command.MsgData.Data = new (std::nothrow) unsigned char[size_LLONG]; //freed at the end of send function
		if (VDC_Command.MsgData.Data == NULL) {
			std::cout << "Memory Could not be allocated" << std::endl;
			std::exit(EXIT_FAILURE);
		}//Data Freed at the end of send vdc function

		 /* Copying the Data to the Command Message Struct */
		data = htonll(data); //Network Order BIG Endian
		memcpy(VDC_Command.MsgData.Data, &data, size_LLONG);

		VDC_Command.length = htonl(Msg_Descr_Size + size_LLONG);

		return VDC_Command;
	}
	/* Encode when the data to send is DOUBLE */
	Command_Message Encode_Data(uint16_t F_ID, VDC_API::DOUBLE data)
	{
		Command_Message VDC_Command;

		VDC_Command.Mdesc.Function_ID = htons(F_ID);
		VDC_Command.Mdesc.Selector = Type_Command_Message;
		VDC_Command.Mdesc.Serialization = serialization;

		VDC_Command.MsgData.Data = new (std::nothrow) unsigned char[size_DOUBLE]; //freed at the end of send function
		if (VDC_Command.MsgData.Data == NULL) {
			std::cout << "Memory Could not be allocated" << std::endl;
			std::exit(EXIT_FAILURE);
		}//Data Freed at the end of send vdc function

		 /* Copying the Data to the Command Message Struct */
		data = htond(data);
		memcpy(VDC_Command.MsgData.Data, &data, size_DOUBLE);

		VDC_Command.length = htonl(Msg_Descr_Size + size_DOUBLE);

		return VDC_Command;
	}

	int send_VDC_Command(Command_Message VDC_Command)
	{
		/* Timeout settings */
		VDC_Sock_TimeVal tv;
		tv.tv_sec = 10; //this is in ms as SO_RCVTIMEO converts it to ms in Windows.. not sure about linux.
		tv.tv_usec = 0;

		/* The number of bytes in Data to send */
		long data_len = ntohl(VDC_Command.length) - Msg_Descr_Size; // from big to short for WINDOWS.... data_len = size of data without newline and/or null

																	/* The char array to send -- its unsigned char cuz char adds extra junk when using memcpy-- */
		unsigned char *sendbuf = new (std::nothrow) unsigned char[Msg_Length_Size + Msg_Descr_Size + data_len];
		if (sendbuf == NULL) {
			std::cout << "Memory Could not be allocated " << std::endl;
			std::exit(EXIT_FAILURE);
		}

		/* copying headers*/
		memcpy(sendbuf, &VDC_Command, Msg_Length_Size + Msg_Descr_Size);

		/*copying data*/
		if (data_len > 0)
			memcpy(sendbuf + Msg_Length_Size + Msg_Descr_Size, VDC_Command.MsgData.Data, data_len);

		int iResult = 0;
		/* Number of bytes to send includes data bytes and header bytes */
		size_t size_to_send = Msg_Length_Size + Msg_Descr_Size;
		if (data_len > 0)
			size_to_send = size_to_send + data_len;

		/*setting the socket option for timeout*/
		iResult = VDC_SockSetTO(ConnectSocket, tv);
		if (iResult == -1) {
			std::cout << "SetSockOpt for send failed with error" << std::endl;
			return -1;
		}

		/* Sending the Char Array */
		iResult = VDC_SockSend(ConnectSocket, (char*)sendbuf, size_to_send, 0);
		if (iResult == VDC_SockError) {
			std::cout << "Send failed with Error " << std::endl;
			VDC_SockClose(ConnectSocket);
			VDC_SockCleanup();
			return -1;
		}

		/* Printing the sent data */
		if (SER_DEBUG) {
			for (int i = 0; i < 8; i++)
				printf("%02x", sendbuf[i]);   // Header
			for (int i = 8; i < Msg_Length_Size + Msg_Descr_Size + data_len; i++)
			{
				if (serialization == JSON)
					printf("%c", sendbuf[i]);
				else if (serialization == BINARY_1)
					printf("%02x", sendbuf[i]);
			}
			std::cout << std::endl << "Bytes sent: " << iResult << std::endl;
		}

		/* Freeing the memory */
		delete[] sendbuf;

		if (VDC_Command.MsgData.Data != NULL) //making sure that it deletes only if the memory exists.
			delete[] VDC_Command.MsgData.Data; // the memory was allocated in Encode_Data

		return iResult;
	}
	/***************************************************************************************************/
	/***************************************************************************************************/
	int Encode_and_Send(uint16_t F_ID, unsigned char * data, size_t length)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID, data, length);
		bytes_sent = send_VDC_Command(VDC_Command);

		return bytes_sent;
	}
	/*
	Combines the Encode and Send Functions
	This helps in managing the memory leak problem and also reduces the typing required in cpp file
	*/
	int Encode_and_Send(uint16_t F_ID, std::string data)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID, data); // has dynamic mem allocated to a char
		bytes_sent = send_VDC_Command(VDC_Command); //clears the mem allocated to the char in Encode_Data

		return bytes_sent;
	}
	/* Encode and Send Variant for when data is NULL*/
	int Encode_and_Send(uint16_t F_ID)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID);
		bytes_sent = send_VDC_Command(VDC_Command);

		return bytes_sent;
	}

	/* Encode and Send for when the data is BOOL */
	int Encode_and_Send(uint16_t F_ID, VDC_API::BOOL data)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID, data);
		bytes_sent = send_VDC_Command(VDC_Command);

		return bytes_sent;
	}
	/* Encode and Send for when the data is SHORT */
	int Encode_and_Send(uint16_t F_ID, VDC_API::SHORT data)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID, data);
		bytes_sent = send_VDC_Command(VDC_Command);

		return bytes_sent;
	}
	/* Encode and Send for when the data is LONG */
	int Encode_and_Send(uint16_t F_ID, VDC_API::LONG data)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID, data);
		bytes_sent = send_VDC_Command(VDC_Command);

		return bytes_sent;
	}
	/* Encode and Send for when the data is LLONG */
	int Encode_and_Send(uint16_t F_ID, VDC_API::LLONG data)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID, data);
		bytes_sent = send_VDC_Command(VDC_Command);

		return bytes_sent;
	}
	/* Encode and Send for when the data is DOUBLE */
	int Encode_and_Send(uint16_t F_ID, VDC_API::DOUBLE data)
	{
		Command_Message VDC_Command;
		int bytes_sent = 0;

		VDC_Command = Encode_Data(F_ID, data);
		bytes_sent = send_VDC_Command(VDC_Command);

		return bytes_sent;
	}
	/***************************************************************************************************/
	/***************************************************************************************************/
	/* Helpers functions to convert the a byte array to the corresponding data type */
	uint16_t convFrom8bit(uint8_t a, uint8_t b)
	{
		uint16_t fn_return = (b << 0) | (a << 8);
		return fn_return;
	}
	uint32_t convFrom8bit(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
	{
		uint32_t fn_return = (d << 0) | (c << 8) | (b << 16) | (a << 24);
		return fn_return;
	}
	uint64_t convFrom8bit(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f, uint8_t g, uint8_t h)
	{
		uint64_t fn_return = (h << 0) | (g << 8) | (f << 16) | (e << 24) | (d << 32) | (c << 40) | (b << 48) | (a << 56);
		return fn_return;
	}
	VDC_API::SHORT convToShort(unsigned char* dArray)
	{
		VDC_API::SHORT fn_return = ((VDC_API::SHORT)dArray[1] << 0) |
			((VDC_API::SHORT)dArray[0] << 8);
		return fn_return;
	}
	VDC_API::LONG convToLong(unsigned char* dArray)
	{
		VDC_API::LONG fn_return = ((VDC_API::LONG)dArray[3] << 0) |
			((VDC_API::LONG)dArray[2] << 8) |
			((VDC_API::LONG)dArray[1] << 16) |
			((VDC_API::LONG)dArray[0] << 24);
		return fn_return;
	}
	VDC_API::LLONG convToLLong(unsigned char* dArray)
	{
		VDC_API::LLONG fn_return = (VDC_API::LLONG)(dArray[7] << 0) |
			((VDC_API::LLONG)dArray[6] << 8) |
			((VDC_API::LLONG)dArray[5] << 16) |
			((VDC_API::LLONG)dArray[4] << 24) |
			((VDC_API::LLONG)dArray[3] << 32) |
			((VDC_API::LLONG)dArray[2] << 40) |
			((VDC_API::LLONG)dArray[1] << 48) |
			((VDC_API::LLONG)dArray[0] << 56);
		return fn_return;
	}
	VDC_API::DOUBLE convToDouble(unsigned char* dArray)
	{
		/* No bitwise operators possible on Double/float */
		VDC_API::DOUBLE fn_return;

		int8_t arr[8];
		for (int i = 0; i < 8; i++)
			arr[i] = dArray[7 - i];

		memcpy(&fn_return, arr, size_DOUBLE);

		return fn_return;
	}
	VDC_API::BOOL convToBool(unsigned char * DataArray)
	{
		return (VDC_API::BOOL)DataArray[0];
	}
	VDC_API::STRING convToString(unsigned char* dArray, size_t size)
	{
		VDC_API::STRING fn_return((char*)dArray, size);
		return fn_return;
	}
	VDC_API::VECTOR convToVector(unsigned char * DataArray)
	{
		VDC_API::VECTOR fn_return;

		fn_return.x = convToLLong(DataArray);
		fn_return.y = convToLLong(DataArray + size_LLONG);
		fn_return.z = convToLLong(DataArray + size_LLONG + size_LLONG);

		return  fn_return;
	}
	VDC_API::MATRIX convToMatrix(unsigned char * DataArray)
	{
		VDC_API::MATRIX fn_return;

		fn_return.v0 = convToVector(DataArray);
		fn_return.v1 = convToVector(DataArray + size_VECTOR);
		fn_return.v2 = convToVector(DataArray + size_VECTOR + size_VECTOR);
		fn_return.v3 = convToVector(DataArray + size_VECTOR + size_VECTOR + size_VECTOR);

		return fn_return;
	}
	void DateTimeLongsToDateTimeString(VDC_API::STRING & datetime, VDC_API::LONG no_of_days, VDC_API::LONG no_of_ms)
	{
		//std::tm time;
		//int seconds = no_of_ms % 1000;
		//int min = no_of_ms%

	}
	/***************************************************************************************************/
	/***************************************************************************************************/
	/*
		receives twice the command message from the server over created tcp socket
		and enodes the information in bigEndian format #
	*/
	int recv_VDC_Command(Command_Message *VDC_Command)
	{
		int iResult = 0;

		/* Timeout Settings */
		VDC_Sock_TimeVal tv;
		tv.tv_sec = 10; //this is in ms as SO_RCVTIMEO converts it to ms in Windows.. not sure about linux.
		tv.tv_usec = 0;

		/*setting the socket option for timeout*/
		iResult = VDC_SockSetTO(ConnectSocket, tv);
		if (iResult == VDC_SockError) {
			std::cout << "SetSockOpt for rcv failed with error" << std::endl;
			return -1;
		}

		/* receive header to compute length and msg_descr*/
		char recvhead[Msg_Length_Size + Msg_Descr_Size];
		iResult = VDC_SockRecv(ConnectSocket, recvhead, Msg_Length_Size + Msg_Descr_Size, 0);
		if (iResult > 0) {
			if (SER_DEBUG)
				std::cout << "Header Bytes received: " << iResult << std::endl;
		}
		else if (iResult == 0)
			std::cout << "Connection Closed" << std::endl;
		else {
			std::cout << "Header Receive failed with Error " << std::endl;
			return -1;
		}

		if (SER_DEBUG)
		{
			for (int i = 0; i < 8; i++)
				printf("%02x", recvhead[i]);
			std::cout << std::endl;
		}

		/* Copying length*/
		uint8_t len_recv[Msg_Length_Size];
		memcpy(len_recv, recvhead, Msg_Length_Size);

		VDC_Command->length = htonl((len_recv[3] << 0) | (len_recv[2] << 8) | (len_recv[1] << 16) | (len_recv[0] << 24));

		if (SER_DEBUG) {
			/* Printing the headers */
			std::cout << "The Length Recieved is (HEX-BE) ";
			for (int i = 0; i < Msg_Length_Size; i++)
				printf("%02x", len_recv[i]);
			std::cout << std::endl;

			std::cout << "The length received is (DEC-SE) " << ntohl(VDC_Command->length) << std::endl; //Little Endian
		}

		/* Copying Selector */
		VDC_Command->Mdesc.Selector = recvhead[Msg_Length_Size];
		if (SER_DEBUG)
			printf("The Selector is %02x \n", VDC_Command->Mdesc.Selector);

		/* Copying Serialization */
		VDC_Command->Mdesc.Serialization = recvhead[Msg_Length_Size + 1];
		if (SER_DEBUG)
			printf("The Serialization is %02x \n", VDC_Command->Mdesc.Serialization);

		/* Copying Function ID */
		uint8_t fid_recv[2];
		memcpy(fid_recv, recvhead + Msg_Length_Size + 2, 2);
		VDC_Command->Mdesc.Function_ID = convFrom8bit(fid_recv[0], fid_recv[1]);

		if (SER_DEBUG) {
			std::cout << "The FId is (HEX-BE) ";
			for (int i = 0; i < 2; i++)
				printf("%02x", fid_recv[i]);
			std::cout << std::endl;

			std::cout << "The FId is (DEC-SE) " << VDC_Command->Mdesc.Function_ID << std::endl;
		}

		/* Number of bytes in Data */
		long data_len = 0;
		data_len = ntohl(VDC_Command->length) - Msg_Descr_Size; //converts to small endian

																/* recieve data based on length before */
		if (data_len > 0)
		{
			/* Memory for the Data */
			VDC_Command->MsgData.Data = new (std::nothrow) unsigned char[data_len];
			if (VDC_Command->MsgData.Data == NULL) {
				std::cout << "Memory could not be allocated for the receive" << std::endl;
				std::exit(EXIT_FAILURE);
			}

			/* Memory for the receive buffer */
			char *recvdata = new (std::nothrow) char[data_len];
			if (recvdata == NULL) {
				std::cout << "Memory could not be allocated for the receive" << std::endl;
				std::exit(EXIT_FAILURE);
			}

			/*
			Recieving the Data
			flag set to wait till the recvdata buffer is full
			Note:	Without this flag when the data to receive is really big, the recv funvtion returns and the code context moves forward although the buffer
			is still not filled. This causes problems as the code tries to extract the data in the partially filled buffer which produces junk
			*/
			iResult = VDC_SockRecv(ConnectSocket, recvdata, data_len, MSG_WAITALL);
			if (iResult > 0) {
				if (SER_DEBUG)
					std::cout << "Data Bytes received:" << iResult << std::endl;
			}
			else if (iResult == 0)
				std::cout << "Connection Closed" << std::endl;
			else {
				std::cout << "Data Receive failed with Error " << WSAGetLastError() << std::endl;
				return -1;
			}

			/*Copying the received data to the VDC_Command struct*/
			memcpy(VDC_Command->MsgData.Data, recvdata, data_len);

			if (SER_DEBUG) {
				if (serialization == JSON) {
					/* printing the received data */
					for (int c = 0; c < data_len; c++)
						printf("%c", VDC_Command->MsgData.Data[c]);
				}
				else if (serialization == BINARY_1) {
					/* printing the received data */
					for (int c = 0; c < data_len; c++)
						printf("%02x", VDC_Command->MsgData.Data[c]);
				}
				std::cout << std::endl;
			}

			/* Freeing the Memory */
			delete[] recvdata;//
							  //delete[] VDC_Command->MsgData.Data; This is done at the very end of every API function
							  //because of the string usage in JSON deserialize function
		}
		else
		{
			if (SER_DEBUG)
				std::cout << "No Data Received" << std::endl;
		}

		return iResult;
	}
	/***************************************************************************************************/
	/***************************************************************************************************/
	int Recv_and_Decode(uint8_t &serialization, unsigned char* Data)
	{
		Command_Message VDC_Command;
		int bytes_recv = 0;

		bytes_recv = recv_VDC_Command(&VDC_Command);

		serialization = VDC_Command.Mdesc.Serialization;
		Data = VDC_Command.MsgData.Data;

		return bytes_recv;

	}
	/***************************************************************************************************/
	/***************************************************************************************************/
	/* Return the Json Value class from the byte array */
	Json::Value Json_DeSerialize(unsigned char *data, long data_len)
	{
		Json::Value Data;
		Json::Reader reader;

		std::string json_string((char*)data, data_len + 1); //delete messes things up with this part. +1 for null.

		reader.parse(json_string, Data);

		return Data;
	}

	/***************************************************************************************************/
	/***************************************************************************************************/
	//Sets the passed error struct manually
	void setErrorStruct(VDC_API::BOOL status, VDC_API::LONG code, VDC_API::STRING source, ERROR_STRUCT& error)
	{
		if (error.status == false) //so that when error was already true, the error info is simply propogated through to the next function
		{
			error.status = status;
			error.code = code;
			error.source = error.source;
		}
	}
	//Sets the passed error struct from Command Message (received)
	void setErrorStruct(ERROR_STRUCT& error, Command_Message Msg , int index = 0)
	{
		if (error.status == false) //so that when error was already true, the error info is simply propogated through to the next function
		{
			error.status = convToBool(Msg.MsgData.Data + index);
			error.code = convToLong(Msg.MsgData.Data + index + size_BOOL);
			/* 4 bytes of additiona length information comes here */
			error.source = convToString(Msg.MsgData.Data + index + size_BOOL + size_LONG + size_LONG, ntohl(Msg.length) - Msg_Descr_Size - index - size_BOOL - size_LONG - size_LONG);
		}
	}
	//Sets the passed error struct from Command Message (received) and also returns the not of error status
	void setErrorStruct(ERROR_STRUCT& error, Command_Message Msg, bool& nStatus, int index = 0)
	{
		nStatus = !convToBool(Msg.MsgData.Data + index);
		if (error.status == false) //so that when error was already true, the error info is simply propogated through to the next function
		{
			error.status = !nStatus;
			error.code = convToLong(Msg.MsgData.Data + index + size_BOOL);
			/* 4 bytes of additiona length information comes here */
			error.source = convToString(Msg.MsgData.Data + index + size_BOOL + size_LONG + size_LONG, ntohl(Msg.length) - Msg_Descr_Size - index - size_BOOL - size_LONG - size_LONG);
		}
	}
};

/***************************************************************************************************/
/*Constructors*/
/***************************************************************************************************/
VDCCommandAPI::VDCCommandAPI(SERIALIZATION_TYPE ser)
{
	/* Constructing the Implementation Object and its members */
	pImpl = new VDCCommandAPI::Impl();
	pImpl->ConnectSocket = 0;
	pImpl->serialization = (uint8_t)ser;

}
/***************************************************************************************************/
/*Destructors*/
/***************************************************************************************************/
VDCCommandAPI::~VDCCommandAPI()
{
	/* Deleting the implemntation object */
	delete pImpl;
}
/***************************************************************************************************/
/*Open Command Channel
/***************************************************************************************************/
void VDCCommandAPI::open_command_channel(const STRING& netaddr, const int& port, ERROR_STRUCT& error)
{

	int iResult = 0;

	VDC_Sock_AddrInfo	 hints;
	VDC_Sock_AddrInfo	*result = NULL;
	VDC_Sock_AddrInfo	*ptr = NULL;

	/* Type Conversions for WinSock */
	STRING st_port = std::to_string(port);
	char const *ch_port = st_port.c_str();

	char const *ch_netaddr = netaddr.c_str();

	/* init WinSock */
	iResult = VDC_SockInit();
	if (iResult != 0) {
		error.status = true;
		error.source = "WSA_STARTUP";
		error.code = E_WinSock_Init;
	}

	/* Initializing addrinfo  */
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family =	AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	/*Resolve the Server Address and Port*/
	iResult = VDC_GetAddrInfo(ch_netaddr, ch_port, &hints, &result);
	if (iResult != 0)
	{
		VDC_SockCleanup();
		error.status = true;
		error.source = "GetAddressInfo";
		error.code = iResult;
		return;
	}

	ptr = result;

	/* Creating the Socket (Previous versions had the for loop here)  */
	pImpl->ConnectSocket = VDC_Create_Socket(ptr);
	if (pImpl->ConnectSocket == VDC_SockInvalid)
	{
		VDC_SockCleanup();
		pImpl->setErrorStruct(true, E_SOCKET_CREATE, "Socket Create", error);
		return;
	}

	/* connecting the socket */
	iResult = VDC_Connect_Socket(pImpl->ConnectSocket, ptr);
	if (iResult == VDC_SockError)
	{
		printf("Failed to connect to the server\n");
		VDC_SockClose(pImpl->ConnectSocket);
		pImpl->ConnectSocket = 0;
		
		pImpl->setErrorStruct(true, E_SOCKET_CONNECT, "Socket Connect", error);

	}

	/* Setting the Command Channel Flag to be true */
	pImpl->CommChannelOpen = true;

	/* Free the result variable  */
	freeaddrinfo(result);

}

/***************************************************************************************************/
/*Close Command Channel*/
/***************************************************************************************************/
VDC_API::BOOL VDCCommandAPI::close_command_channel()
{
	if (pImpl->CommChannelOpen)
	{
		int iResult = 0;

		/*Shut Down the connection */
		iResult = VDC_SockShutdown(pImpl->ConnectSocket);
		if (iResult == VDC_SockError) {
			VDC_SockClose(pImpl->ConnectSocket);
			VDC_SockCleanup();
			std::cout << "Error in Closing the Socket " << iResult << std::endl;
			return false;
		}

		/*Clean UPs*/
		VDC_SockClose(pImpl->ConnectSocket);
		VDC_SockCleanup();

		/* Setting the Command Channel Flag to be false */
		pImpl->CommChannelOpen = false;

		return true;
	}
	else
		return false;
}

VDC_API::BOOL VDCCommandAPI::is_command_channel_open(ERROR_STRUCT & error)
{
	bool fn_return = false;
	VDC_API::HANDLE test = NULL;

	if (pImpl->CommChannelOpen == true)//meaning the open command channel has been called and the close command channel not
	{
		test = this->get_systemheap_space_handle(error);
		if (test != NULL)
			fn_return = true;
	}

	return fn_return;
}

/***************************************************************************************************/
/*  API FUNCTIONS - Support Commands */
/***************************************************************************************************/
HSYSTEMHEAP VDCCommandAPI::get_systemheap_space_handle(ERROR_STRUCT& error)
{
	/* Initializing the retrun value of the function */
	HSYSTEMHEAP fn_return = HANDLE_DEF;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON || pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_SH_SP_HNDL_T);  //Function ID
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}

	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_SH_SP_HNDL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_HANDLE_INVALID_SH, E_HANDLE_INVALID_SH_Src, error);
		}
			break;
		case BINARY_1:
		{
			fn_return = (HSYSTEMHEAP)pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_HANDLE_INVALID_SH, E_HANDLE_INVALID_SH_Src, error);
		}
			break;
		default:
			break;
		}

	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);

	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}

	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::BOOL VDCCommandAPI::write_to_datalog(const VDC_API::STRING& filename, ERROR_STRUCT& error)
{
	VDC_API::BOOL fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonpth] = filename;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(WRT_TO_DLOG_T, json_string);
	}
	else if (pImpl->serialization == BINARY_1)
	{
		unsigned char* dArray = pImpl->init_dArray(size_STRING(filename));
		pImpl->pushData(filename, dArray, 0);

		bytes = pImpl->Encode_and_Send(WRT_TO_DLOG_T, dArray, size_STRING(filename));
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Checking the Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == WRT_TO_DLOG_R)
	{
		/*Check the Serialization*/
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_WRT_DLG, E_WRT_DLG_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_WRT_DLG, E_WRT_DLG_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
			break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}

VDC_API::BOOL VDCCommandAPI::start_logging(ERROR_STRUCT & error)
{
	/* Initializing the retrun value of the function */
	VDC_API::BOOL fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON || pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(STRT_DLOG_T);  //Function ID
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}

	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == STRT_DLOG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_START_LOG, E_START_LOG_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_START_LOG, E_START_LOG_Src, error);
		}
		break;
		default:
			break;
		}

	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);

	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}

	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}

VDC_API::BOOL VDCCommandAPI::stop_logging(ERROR_STRUCT & error)
{
	/* Initializing the retrun value of the function */
	VDC_API::BOOL fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON || pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(STOP_DLOG_T);  //Function ID
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}

	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == STOP_DLOG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_STOP_LOG, E_STOP_LOG_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_STOP_LOG, E_STOP_LOG_Src, error);
		}
		break;
		default:
			break;
		}

	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);

	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}

	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}

VDC_API::BOOL VDCCommandAPI::is_logging(ERROR_STRUCT & error)
{
	/* Initializing the retrun value of the function */
	VDC_API::BOOL fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON || pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(IS_DLOG_T);  //Function ID
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}

	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == IS_DLOG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asBool();
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
		}
		break;
		default:
			break;
		}

	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);

	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}

	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}

/***************************************************************************************************/
bool VDCCommandAPI::set_server_read_timeout(VDC_API::LONG timeout, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonTO] = timeout;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_SRVR_RD_TO_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(SET_SRVR_RD_TO_T, timeout);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_SRVR_RD_TO_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_SET_SRVR_TO, E_SET_SRVR_TO_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_SET_SRVR_TO, E_SET_SRVR_TO_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}

/***************************************************************************************************/
/*  API FUNCTIONS - BaseObject Commands */
/***************************************************************************************************/
HBASEOBJECT VDCCommandAPI::create_baseobject(const STRING& obj_name, VDC_API::LONG object_id, BASEOBJECT_FLAGS flags, ERROR_STRUCT& error)
{
	HBASEOBJECT fn_return = HANDLE_DEF;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonName]	= obj_name;    
		Data[jsonID]	= object_id;
		Data[jsonFLGS]	= flags;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_BO_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1)
	{
		unsigned char* dArray = pImpl->init_dArray(size_STRING(obj_name) + size_LONG + size_LONG);
		pImpl->pushData(obj_name, dArray, 0);
		pImpl->pushData(object_id, dArray, size_STRING(obj_name));
		pImpl->pushData((VDC_API::LONG)flags, dArray, size_STRING(obj_name) + size_LONG);

		bytes = pImpl->Encode_and_Send(CRT_BO_T, dArray, size_STRING(obj_name) + size_LONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Checking the Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_BO_R)
	{
		/*Check the Serialization*/
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);

		}
		break;
		case BINARY_1:
		{
			fn_return = (HBASEOBJECT)pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
			break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}

	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::delete_baseobject(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_BO_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1){
		bytes = pImpl->Encode_and_Send(DEL_BO_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}

	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_BO_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_DEL_BO, E_DEL_BO_Src, error);
		}
			break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_DEL_BO, E_DEL_BO_Src, error);
		}
			break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
			break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}

	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
STRING VDCCommandAPI::get_baseobject_name(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	STRING fn_return = STRING_WRN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_BO_NAME_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_BO_NAME_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}

	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_BO_NAME_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonName].asString();
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(true, E_GET_BO_NAME, E_GET_BO_NAME_Src, error);
		}
		break;
		case BINARY_1:
		{
			/* Size of the string comes here with the string */
			fn_return = pImpl->convToString(VDC_Command_recv.MsgData.Data + size_LONG , pImpl->convToLong(VDC_Command_recv.MsgData.Data));
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(true, E_GET_BO_NAME, E_GET_BO_NAME_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::LONG VDCCommandAPI::get_baseobject_id(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	VDC_API::LONG fn_return = BO_ID_WRN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_BO_ID_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_BO_ID_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}

	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_BO_ID_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonID].asInt();
			if (fn_return == BO_ID_WRN)
				pImpl->setErrorStruct(false, E_GET_BO_ID, E_GET_BO_ID_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == BO_ID_WRN)
				pImpl->setErrorStruct(false, E_GET_BO_ID, E_GET_BO_ID_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_baseobject_flags(HBASEOBJECT obj, BASEOBJECT_FLAGS mask, BASEOBJECT_FLAGS flags, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]	= (VDC_API::HBASEOBJECT)obj;    
		Data[jsonMSK]	= mask;
		Data[jsonFLGS]	= flags;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_BO_FLGS_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_LONG + size_LONG);
		pImpl->pushData(obj, dArray, 0);
		pImpl->pushData(mask, dArray, size_LLONG);
		pImpl->pushData(flags, dArray, size_LLONG + size_LONG);

		bytes = pImpl->Encode_and_Send(SET_BO_FLGS_T, dArray, size_LLONG + size_LONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_BO_FLGS_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_SET_BO_FLGS, E_SET_BO_FLGS_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_SET_BO_FLGS, E_SET_BO_FLGS_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
BASEOBJECT_FLAGS VDCCommandAPI::get_baseobject_flags(HBASEOBJECT obj, ERROR_STRUCT & error)
{
	BASEOBJECT_FLAGS fn_return = BOF_NONE;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_BO_FLGS_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG);
		pImpl->pushData(obj, dArray, 0);

		bytes = pImpl->Encode_and_Send(SET_BO_FLGS_T, dArray, size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_BO_FLGS_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (BASEOBJECT_FLAGS)Data[jsonFLGS].asInt();
		}
		break;
		case BINARY_1:
		{
			fn_return = (BASEOBJECT_FLAGS)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::LONG VDCCommandAPI::get_baseobject_count(ERROR_STRUCT& error)
{
	VDC_API::LONG fn_return = COUNT_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON || pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_BO_CNT_T); //Function ID
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_BO_CNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonCNT].asInt();
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_BO_CNT, E_GET_BO_CNT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_BO_CNT, E_GET_BO_CNT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::LONG VDCCommandAPI::get_baseobject_list(VDC_API::LONG index, HBASEOBJECT* obj_list, VDC_API::LONG max_no_of_elements, ERROR_STRUCT& error)
{
	VDC_API::LONG fn_return = COUNT_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonIDX]	= index;    
		Data[jsonSIZE]	= max_no_of_elements;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_BO_LST_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1){
		unsigned char *dArray = pImpl->init_dArray(size_LONG + size_LONG);
		pImpl->pushData(index, dArray, 0);
		pImpl->pushData(max_no_of_elements, dArray, size_LONG);

		bytes = pImpl->Encode_and_Send(GET_BO_LST_T, dArray, size_LONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_BO_LST_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			fn_return = Data[jsonNMBR].asInt(); //as now the packet received gives the count

			Json::Value& mover_Data = Data[jsonHList];

			if (fn_return > 0) 
			{
				for (int i = 0; i < fn_return; i++)
					obj_list[i] = (HBASEOBJECT)(mover_Data[i].asInt64());
			}

			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_BO_LST, E_GET_BO_LST_Src, error);

		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return > 0) 
			{
				for (int i = 0; i < fn_return; i++)
					obj_list[i] = pImpl->convToLLong(VDC_Command_recv.MsgData.Data + size_LONG + (size_LLONG * i ));
			}
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_BO_LST, E_GET_BO_LST_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
VDC_API::LONG VDCCommandAPI::get_visible_baseobject_count(ERROR_STRUCT & error)
{
	return VDC_API::LONG();
}
VDC_API::LONG VDCCommandAPI::get_visible_baseobject_list(VDC_API::LONG index, HBASEOBJECT * obj_list, VDC_API::LONG max_no_of_elements, ERROR_STRUCT & error)
{
	return VDC_API::LONG();
}
/***************************************************************************************************/
/*  API FUNCTIONS - DataElement Commands */
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::create_long(HBASECONTAINER h, const STRING& name, VDC_API::LONG default_value, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_DEF;

	int bytes = 0;

	Command_Message VDC_Command_recv;
	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]		= (VDC_API::HBASECONTAINER)h;		
		Data[jsonName]		= name;
		Data[jsonDefVal]	= default_value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_LNG_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_STRING(name) + size_LONG);
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);
		pImpl->pushData(default_value, dArray, size_LLONG + size_STRING(name));

		bytes = pImpl->Encode_and_Send(CRT_LNG_T, dArray, size_LLONG + size_STRING(name) + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_LNG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);

		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::create_double(HBASECONTAINER h, const STRING& name, VDC_API::DOUBLE default_value, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_DEF;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]		= (VDC_API::HBASECONTAINER)h;    
		Data[jsonName]		= name;
		Data[jsonDefVal]	= default_value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_DBL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_STRING(name) + size_DOUBLE);
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);
		pImpl->pushData(default_value, dArray, size_LLONG + size_STRING(name));

		bytes = pImpl->Encode_and_Send(CRT_DBL_T, dArray, size_LLONG + size_STRING(name) + size_DOUBLE);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_DBL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::create_string(HBASECONTAINER h, const STRING& name, const STRING& default_value, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]		= (VDC_API::HBASECONTAINER)h;   
		Data[jsonName]		= name;
		Data[jsonDefVal]	= default_value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_STRNG_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_STRING(name) + size_STRING(default_value));
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);
		pImpl->pushData(default_value, dArray, size_LLONG + size_STRING(name));

		bytes = pImpl->Encode_and_Send(CRT_STRNG_T, dArray , size_LLONG + size_STRING(name) + size_STRING(default_value));
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_STRNG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::create_datetime(HBASECONTAINER h, const STRING& name, const DATETIME& default_value, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASECONTAINER)h;    
		Data[jsonName] = name;
		Data[jsonDefVal] = default_value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_DT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_STRING(name) + size_LLONG);
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);
		VDC_API::LONG nms, nd;
		pImpl->DateTimeStringToDateTimeLongs(default_value, nd, nms);
		pImpl->pushData(nd, dArray, size_LLONG + size_STRING(name));
		pImpl->pushData(nms, dArray, size_LLONG + size_STRING(name) + size_LONG);

		bytes = pImpl->Encode_and_Send(CRT_DT_T, dArray, size_LLONG + size_STRING(name) + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_DT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);

		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::create_vector(HBASECONTAINER h, const STRING& name, const VECTOR& default_value, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASECONTAINER)h;    
		Data[jsonName] = name;

		Json::Value JsonVCT;
		JsonVCT[jsonX] = default_value.x;
		JsonVCT[jsonY] = default_value.y;
		JsonVCT[jsonZ] = default_value.z;

		Data[jsonDefVal] = JsonVCT;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_VCT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_STRING(name) + size_VECTOR);
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);
		pImpl->pushData(default_value, dArray, size_LLONG + size_STRING(name));

		bytes = pImpl->Encode_and_Send(CRT_VCT_T, dArray, size_LLONG + size_STRING(name) + size_VECTOR);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_VCT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::create_matrix(HBASECONTAINER h, const STRING& name, const MATRIX& default_value, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASECONTAINER)h;    
		Data[jsonName] = name;

		Json::Value JsonVCT0, JsonVCT1, JsonVCT2, JsonVCT3;

		JsonVCT0[jsonX] = default_value.v0.x;
		JsonVCT0[jsonY] = default_value.v0.y;
		JsonVCT0[jsonZ] = default_value.v0.z;

		JsonVCT1[jsonX] = default_value.v1.x;
		JsonVCT1[jsonY] = default_value.v1.y;
		JsonVCT1[jsonZ] = default_value.v1.z;

		JsonVCT2[jsonX] = default_value.v2.x;
		JsonVCT2[jsonY] = default_value.v2.y;
		JsonVCT2[jsonZ] = default_value.v2.z;

		JsonVCT3[jsonX] = default_value.v3.x;
		JsonVCT3[jsonY] = default_value.v3.y;
		JsonVCT3[jsonZ] = default_value.v3.z;

		Json::Value JsonMTRX;
		JsonMTRX[jsonV0] = JsonVCT0;
		JsonMTRX[jsonV1] = JsonVCT1;
		JsonMTRX[jsonV2] = JsonVCT2;
		JsonMTRX[jsonV3] = JsonVCT3;

		Data[jsonDefVal] = JsonMTRX;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_MTRX_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG +  size_STRING(name) + size_MATRIX);
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);
		pImpl->pushData(default_value, dArray, size_LLONG + size_STRING(name));

		bytes = pImpl->Encode_and_Send(CRT_MTRX_T, dArray, size_LLONG + size_STRING(name) + size_MATRIX);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_MTRX_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::create_handle(HBASECONTAINER h, const STRING& name, VDC_API::HANDLE default_value, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;
	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]		= (VDC_API::HBASECONTAINER)h;    
		Data[jsonName]		= name;
		Data[jsonDefVal]	= default_value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_HNDL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_STRING(name) + size_LLONG);
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);
		pImpl->pushData(default_value, dArray, size_LLONG + size_STRING(name));

		bytes = pImpl->Encode_and_Send(CRT_HNDL_T, dArray, size_LLONG + size_STRING(name) + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_HNDL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();

			Json::Value& error_Data = Data[jsonERR];
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::delete_data_element(HDELEMENT hElement, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_DEL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(DEL_DEL_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_DEL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_DEL_DATEL, E_DEL_DATEL_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (fn_return == false)
				pImpl->setErrorStruct(true, E_DEL_DATEL, E_DEL_DATEL_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
STRING VDCCommandAPI::get_data_element_name(HDELEMENT hElement, ERROR_STRUCT& error)
{
	STRING fn_return = STRING_WRN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_DEL_NAME_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1){
		bytes = pImpl->Encode_and_Send(GET_DEL_NAME_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_DEL_NAME_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonName].asString();
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(false, E_GET_DEL_NAME, E_GET_DEL_NAME_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToString(VDC_Command_recv.MsgData.Data + size_LONG, pImpl->convToLong(VDC_Command_recv.MsgData.Data));
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(false, E_GET_DEL_NAME, E_GET_DEL_NAME_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
DATATYPE VDCCommandAPI::get_data_element_type(HDELEMENT hElement, ERROR_STRUCT& error)
{
	DATATYPE fn_return = T_UNKNOWN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_DEL_TYP_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_DEL_TYP_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_DEL_TYP_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (VDC_API::DATATYPE)Data[jsonTYP].asInt();
			if (fn_return == T_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_DEL_TYP, E_GET_DEL_TYP_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = (VDC_API::DATATYPE)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == T_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_DEL_TYP, E_GET_DEL_TYP_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::LONG VDCCommandAPI::get_data_element_count(HBASECONTAINER h, ERROR_STRUCT& error)
{
	VDC_API::LONG fn_return = COUNT_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASECONTAINER)h;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_DEL_CNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_DEL_CNT_T, h);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_DEL_CNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonCNT].asInt();
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_DEL_CNT, E_GET_DEL_CNT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_DEL_CNT, E_GET_DEL_CNT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::LONG VDCCommandAPI::get_data_element_list(HBASECONTAINER h, VDC_API::LONG index, HDELEMENT* data_element_list, VDC_API::LONG max_no_of_elements, ERROR_STRUCT& error)
{
	VDC_API::LONG fn_return = COUNT_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonIDX]	= index;   
		Data[jsonHndl]	= (VDC_API::HBASECONTAINER)h;
		Data[jsonSIZE]	= max_no_of_elements;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_DEL_LST_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LONG + size_LONG);
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(index, dArray, size_LLONG);
		pImpl->pushData(max_no_of_elements, dArray, size_LLONG + size_LONG);

		bytes = pImpl->Encode_and_Send(GET_DEL_LST_T, dArray, size_LLONG + size_LONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_DEL_LST_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			fn_return = Data[jsonNMBR].asInt(); //As the Received packet contains the count

			Json::Value& mover_Data = Data[jsonHList];

			if (fn_return > 0) 
			{
				for(int i = 0; i < fn_return; i++)
					data_element_list[i] = (HBASEOBJECT)(mover_Data[i].asInt64());
			}
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_DEL_LIST, E_GET_DEL_LIST_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return > 0) 
			{
				for (int i = 0; i < fn_return; i++)
					data_element_list[i] = pImpl->convToLLong(VDC_Command_recv.MsgData.Data + size_LONG + (size_LLONG * i));
			}
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_DEL_LIST, E_GET_DEL_LIST_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_long(HDELEMENT hElement, VDC_API::LONG value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]	= (VDC_API::HDELEMENT)hElement;   
		Data[jsonVal]	= value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_LNG_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LONG);
		pImpl->pushData(hElement, dArray, 0);
		pImpl->pushData(value, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_LNG_T, dArray, size_LLONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_LNG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
bool VDCCommandAPI::get_long(HDELEMENT hElement, VDC_API::LONG& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_LNG_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_LNG_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_LNG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			value = Data[jsonVal].asInt();

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			value = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return ,size_LONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
bool VDCCommandAPI::set_double(HDELEMENT hElement, VDC_API::DOUBLE value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]	= (VDC_API::HDELEMENT)hElement;    
		Data[jsonVal]	= value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_DBL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_LLONG);
		pImpl->pushData(hElement, dArray, 0);
		pImpl->pushData(value, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_DBL_T, dArray, size_LLONG + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_DBL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::get_double(HDELEMENT hElement, VDC_API::DOUBLE& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_DBL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_DBL_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_DBL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			value = Data[jsonVal].asDouble();

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			value = pImpl->convToDouble(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return, size_DOUBLE);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_string(HDELEMENT hElement, const STRING& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]	= (VDC_API::HDELEMENT)hElement;    
		Data[jsonVal]	= value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_STRNG_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_STRING(value));
		pImpl->pushData(hElement, dArray, 0);
		pImpl->pushData(value, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_STRNG_T, dArray, size_LLONG + size_STRING(value));
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_STRNG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::get_string(HDELEMENT hElement, STRING& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_STRNG_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_STRNG_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_STRNG_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			value = Data[jsonVal].asString();

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			value = pImpl->convToString(VDC_Command_recv.MsgData.Data + size_LONG, pImpl->convToLong(VDC_Command_recv.MsgData.Data));
			pImpl->setErrorStruct(error, VDC_Command_recv,fn_return, size_STRING(value));
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_datetime(HDELEMENT hElement, const DATETIME& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    
		Data[jsonVal] = value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_DT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		VDC_API::LONG no_of_days, no_of_ms;
		pImpl->DateTimeStringToDateTimeLongs(value, no_of_days, no_of_ms);

		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LONG + size_LONG);
		pImpl->pushData(hElement, dArray, 0);
		pImpl->pushData(no_of_days, dArray, size_LLONG);
		pImpl->pushData(no_of_ms, dArray, size_LLONG + size_LONG);

		bytes = pImpl->Encode_and_Send(SET_DT_T, dArray, size_LLONG + size_LONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_DT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			pImpl->setErrorStruct(error, VDC_Command_recv,fn_return);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
bool VDCCommandAPI::get_datetime(HDELEMENT hElement, DATETIME& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_DT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_DT_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_DT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			value = Data[jsonVal].asString();

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			VDC_API::LONG no_of_days, no_of_ms;
			no_of_days = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			no_of_ms = pImpl->convToLong(VDC_Command_recv.MsgData.Data + size_LONG);

			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return, size_STRING(value));
			
			pImpl->DateTimeLongsToDateTimeString(value, no_of_days, no_of_ms);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_vector(HDELEMENT hElement, const VECTOR& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;   

		Json::Value JsonVCT;
		JsonVCT[jsonX] = value.x;
		JsonVCT[jsonY] = value.y;
		JsonVCT[jsonZ] = value.z;

		Data[jsonVal] = JsonVCT;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_VCT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_VECTOR);
		pImpl->pushData(hElement, dArray, 0);
		pImpl->pushData(value, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_VCT_T, dArray, size_LLONG + size_VECTOR);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_VCT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::get_vector(HDELEMENT hElement, VECTOR& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_VCT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_VCT_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_VCT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& JsonVCT = Data[jsonVal];
			value.x = JsonVCT[jsonX].asDouble();
			value.y = JsonVCT[jsonY].asDouble();
			value.z = JsonVCT[jsonZ].asDouble();

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			value = pImpl->convToVector(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return, size_VECTOR);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_matrix(HDELEMENT hElement, const MATRIX& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		Json::Value JsonVCT0, JsonVCT1, JsonVCT2, JsonVCT3;
		JsonVCT0[jsonX] = value.v0.x;
		JsonVCT0[jsonY] = value.v0.y;
		JsonVCT0[jsonZ] = value.v0.z;

		JsonVCT1[jsonX] = value.v1.x;
		JsonVCT1[jsonY] = value.v1.y;
		JsonVCT1[jsonZ] = value.v1.z;

		JsonVCT2[jsonX] = value.v2.x;
		JsonVCT2[jsonY] = value.v2.y;
		JsonVCT2[jsonZ] = value.v2.z;

		JsonVCT3[jsonX] = value.v3.x;
		JsonVCT3[jsonY] = value.v3.y;
		JsonVCT3[jsonZ] = value.v3.z;

		Json::Value JsonMTRX;
		JsonMTRX[jsonV0] = JsonVCT0;
		JsonMTRX[jsonV1] = JsonVCT1;
		JsonMTRX[jsonV2] = JsonVCT2;
		JsonMTRX[jsonV3] = JsonVCT3;

		Data[jsonVal] = JsonMTRX;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_MTRX_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_MATRIX);
		pImpl->pushData(hElement, dArray, 0);
		pImpl->pushData(value, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_MTRX_T, dArray, size_LLONG + size_MATRIX);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_MTRX_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::get_matrix(HDELEMENT hElement, MATRIX& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_MTRX_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_MTRX_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_MTRX_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& JsonMTRX = Data[jsonVal];
			Json::Value& JsonVCT0 = JsonMTRX[jsonV0];
			Json::Value& JsonVCT1 = JsonMTRX[jsonV1];
			Json::Value& JsonVCT2 = JsonMTRX[jsonV2];
			Json::Value& JsonVCT3 = JsonMTRX[jsonV3];

			value.v0.x = JsonVCT0[jsonX].asDouble();
			value.v0.y = JsonVCT0[jsonY].asDouble();
			value.v0.z = JsonVCT0[jsonZ].asDouble();

			value.v1.x = JsonVCT1[jsonX].asDouble();
			value.v1.y = JsonVCT1[jsonY].asDouble();
			value.v1.z = JsonVCT1[jsonZ].asDouble();

			value.v2.x = JsonVCT2[jsonX].asDouble();
			value.v2.y = JsonVCT2[jsonY].asDouble();
			value.v2.z = JsonVCT2[jsonZ].asDouble();

			value.v3.x = JsonVCT3[jsonX].asDouble();
			value.v3.y = JsonVCT3[jsonY].asDouble();
			value.v3.z = JsonVCT3[jsonZ].asDouble();

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			value = pImpl->convToMatrix(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return, size_MATRIX);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_handle(HDELEMENT hElement, VDC_API::HANDLE value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]	= (VDC_API::HDELEMENT)hElement;    
		Data[jsonVal]	= value;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_HNDL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LLONG);
		pImpl->pushData(hElement, dArray, 0);
		pImpl->pushData(value, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_HNDL_T, dArray, size_LLONG + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_HNDL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			pImpl->setErrorStruct(error, VDC_Command_recv,fn_return);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			return fn_return;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::get_handle(HDELEMENT hElement, VDC_API::HANDLE& value, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_HNDL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_HNDL_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_HNDL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			value = Data[jsonVal].asInt();

			Json::Value& error_Data = Data[jsonERR];
			fn_return = !error_Data[jsonerSTATUS].asBool();
			pImpl->setErrorStruct(error_Data[jsonerSTATUS].asBool(), error_Data[jsonerCODE].asInt(), error_Data[jsonerSRC].asString(), error);
		}
		break;
		case BINARY_1:
		{
			value = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			pImpl->setErrorStruct(error, VDC_Command_recv, fn_return, size_LLONG);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
bool VDCCommandAPI::get_string_md5(HDELEMENT hElement, STRING& md5, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_MD5_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_MD5_T, hElement);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_MD5_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			md5 = Data[jsonMD5].asString();

			if (md5.length() > 1)
				fn_return = true;
			else
				pImpl->setErrorStruct(!fn_return, E_GET_STR_MD5, E_GET_STR_MD5_Src, error);
		}
		break;
		case BINARY_1:
		{
			md5 = pImpl->convToString(VDC_Command_recv.MsgData.Data, 0);
			if (md5.length() > 1)
				fn_return = true;
			else
				pImpl->setErrorStruct(!fn_return, E_GET_STR_MD5, E_GET_STR_MD5_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	if(VDC_Command_recv.MsgData.Data != NULL)
		delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
/*  BaseObject Link Operations */
/***************************************************************************************************/
bool VDCCommandAPI::set_parent_link(HBASEOBJECT obj, HBASEOBJECT hParentObject, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    
		Data[jsonHARG] = (VDC_API::HBASEOBJECT)hParentObject;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_PRNT_LNK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LLONG);
		pImpl->pushData(obj, dArray, 0);
		pImpl->pushData(hParentObject, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_PRNT_LNK_T, dArray, size_LLONG + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_PRNT_LNK_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if(!fn_return)
				pImpl->setErrorStruct(!fn_return, E_SET_PARENT_LINK, E_SET_PARENT_LINK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(!fn_return, E_SET_PARENT_LINK, E_SET_PARENT_LINK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HBASEOBJECT VDCCommandAPI::get_parent_link(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	HBASEOBJECT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_PRNT_LNK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_PRNT_LNK_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_PRNT_LNK_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_GET_PARENT_LNK, E_GET_PARENT_LNK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_GET_PARENT_LNK, E_GET_PARENT_LNK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}/***************************************************************************************************/
 /***************************************************************************************************/
bool VDCCommandAPI::delete_parent_link(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_PRNT_LNK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(DEL_PRNT_LNK_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_PRNT_LNK_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_PARENT_LNK, E_DEL_PARENT_LNK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_PARENT_LNK, E_DEL_PARENT_LNK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::set_base_link(HBASEOBJECT obj, HBASEOBJECT hBaseObject, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    
		Data[jsonHARG] = (VDC_API::HBASEOBJECT)hBaseObject;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_BASE_LNK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray( size_LLONG + size_LLONG );
		pImpl->pushData(obj, dArray, 0);
		pImpl->pushData(hBaseObject, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_BASE_LNK_T, dArray, size_LLONG + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_BASE_LNK_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_SET_BASE_LNK, E_SET_BASE_LNK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_SET_BASE_LNK, E_SET_BASE_LNK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HBASEOBJECT VDCCommandAPI::get_base_link(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	HBASEOBJECT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_BASE_LNK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_BASE_LNK_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_BASE_LNK_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_GET_BASE_LNK, E_GET_BASE_LNK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_GET_BASE_LNK, E_GET_BASE_LNK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::delete_base_link(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_BASE_LNK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(DEL_BASE_LNK_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_BASE_LNK_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_BASE_LNK, E_DEL_BASE_LNK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_BASE_LNK, E_DEL_BASE_LNK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::LONG VDCCommandAPI::get_child_count(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	VDC_API::LONG fn_return = COUNT_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_CHLD_CNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_CHLD_CNT_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_CHLD_CNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonCNT].asInt();
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_CHLD_CNT, E_GET_CHLD_CNT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_CHLD_CNT, E_GET_CHLD_CNT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
VDC_API::LONG VDCCommandAPI::get_child_list(HBASEOBJECT obj, VDC_API::LONG index, HBASEOBJECT* hChildObject_list, VDC_API::LONG max_no_of_elements, ERROR_STRUCT& error)
{
	VDC_API::LONG fn_return = COUNT_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonIDX]	= index;    
		Data[jsonHndl]	= (VDC_API::HBASEOBJECT)obj;
		Data[jsonSIZE]	= max_no_of_elements;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_CHLD_LST_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LLONG);
		pImpl->pushData(obj, dArray, 0);
		pImpl->pushData(index, dArray, size_LLONG);
		pImpl->pushData(max_no_of_elements, dArray, size_LLONG + size_LONG);

		bytes = pImpl->Encode_and_Send(GET_CHLD_LST_T, dArray, size_LLONG + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_CHLD_LST_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			fn_return = Data[jsonNMBR].asInt();

			Json::Value& mover_Data = Data[jsonHList];

			if (fn_return > 0) 
			{
				for(int i = 0; i < fn_return;  i++)
						hChildObject_list[i] = (HBASEOBJECT)(mover_Data[i].asInt64());
			}
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_CHLD_LST, E_GET_CHLD_LST_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return > 0)
			{
				for (int i = 0; i < fn_return; i++)
					hChildObject_list[i] = pImpl->convToLLong(VDC_Command_recv.MsgData.Data + size_LONG + (size_LLONG * i));
			}
			if (fn_return == COUNT_ERR)
				pImpl->setErrorStruct(true, E_GET_CHLD_LST, E_GET_CHLD_LST_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
bool VDCCommandAPI::set_baseobject_ml(HBASEOBJECT obj, const MATRIX& m, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		Json::Value JsonVCT0, JsonVCT1, JsonVCT2, JsonVCT3;
		JsonVCT0[jsonX] = m.v0.x;
		JsonVCT0[jsonY] = m.v0.y;
		JsonVCT0[jsonZ] = m.v0.z;

		JsonVCT1[jsonX] = m.v1.x;
		JsonVCT1[jsonY] = m.v1.y;
		JsonVCT1[jsonZ] = m.v1.z;

		JsonVCT2[jsonX] = m.v2.x;
		JsonVCT2[jsonY] = m.v2.y;
		JsonVCT2[jsonZ] = m.v2.z;

		JsonVCT3[jsonX] = m.v3.x;
		JsonVCT3[jsonY] = m.v3.y;
		JsonVCT3[jsonZ] = m.v3.z;

		Json::Value JsonMTRX;
		JsonMTRX[jsonV0] = JsonVCT0;
		JsonMTRX[jsonV1] = JsonVCT1;
		JsonMTRX[jsonV2] = JsonVCT2;
		JsonMTRX[jsonV3] = JsonVCT3;

		Data[jsonMTRX] = JsonMTRX;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_BO_ML_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_MATRIX);
		pImpl->pushData(obj, dArray, 0);
		pImpl->pushData(m, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_BO_ML_T, dArray, size_LLONG + size_MATRIX);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_BO_ML_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_SET_BO_ML, E_SET_BO_ML_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_SET_BO_ML, E_SET_BO_ML_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
MATRIX VDCCommandAPI::get_baseobject_ml(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	/* Default value of the Matrix */
	MATRIX fn_return = { {0,0,0},{0,0,0},{0,0,0},{0,0,0} };

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_BO_ML_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_BO_ML_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_BO_ML_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);

			Json::Value& JsonMTRX = Data[jsonMTRX];
			Json::Value& JsonVCT0 = JsonMTRX[jsonV0];
			Json::Value& JsonVCT1 = JsonMTRX[jsonV1];
			Json::Value& JsonVCT2 = JsonMTRX[jsonV2];
			Json::Value& JsonVCT3 = JsonMTRX[jsonV3];

			fn_return.v0.x = JsonVCT0[jsonX].asDouble();
			fn_return.v0.y = JsonVCT0[jsonY].asDouble();
			fn_return.v0.z = JsonVCT0[jsonZ].asDouble();

			fn_return.v1.x = JsonVCT1[jsonX].asDouble();
			fn_return.v1.y = JsonVCT1[jsonY].asDouble();
			fn_return.v1.z = JsonVCT1[jsonZ].asDouble();

			fn_return.v2.x = JsonVCT2[jsonX].asDouble();
			fn_return.v2.y = JsonVCT2[jsonY].asDouble();
			fn_return.v2.z = JsonVCT2[jsonZ].asDouble();

			fn_return.v3.x = JsonVCT3[jsonX].asDouble();
			fn_return.v3.y = JsonVCT3[jsonY].asDouble();
			fn_return.v3.z = JsonVCT3[jsonZ].asDouble();
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToMatrix(VDC_Command_recv.MsgData.Data);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
bool VDCCommandAPI::delete_baseobject_ml(HBASEOBJECT obj, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HBASEOBJECT)obj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_BO_ML_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(DEL_BO_ML_T, obj);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_BO_ML_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_BO_ML, E_DEL_BO_ML_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_BO_ML, E_DEL_BO_ML_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;


	return fn_return;
}
/***************************************************************************************************/
/*  Find Functions  */
/***************************************************************************************************/
HBASEOBJECT VDCCommandAPI::find_baseobject(const STRING& name, ERROR_STRUCT& error)
{
	HBASEOBJECT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonName] = name;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(FND_BO_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(FND_BO_T, name);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == FND_BO_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_FND_BO, E_FND_BO_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_FND_BO, E_FND_BO_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HDELEMENT VDCCommandAPI::find_data_element(HBASECONTAINER h, const STRING& name, ERROR_STRUCT& error)
{
	HDELEMENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonName] = name;    
		Data[jsonHndl] = (VDC_API::HBASECONTAINER)h;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(FND_DEL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_STRING(name));
		pImpl->pushData(h, dArray, 0);
		pImpl->pushData(name, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(FND_DEL_T, dArray, size_LLONG + size_STRING(name));
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == FND_DEL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_FND_DEL, E_FND_DEL_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_FND_DEL, E_FND_DEL_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HSCRIPT VDCCommandAPI::find_script(const STRING& name, ERROR_STRUCT& error)
{
	HSCRIPT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonName] = name;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(FND_SPT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(FND_SPT_T, name);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == FND_SPT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_FND_SCT, E_FND_SCT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_FND_SCT, E_FND_SCT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************
SCRIPT OPERATION
/***************************************************************************************************/
bool VDCCommandAPI::run_script(HSCRIPT hScript, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hScript;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(RUN_SPT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(RUN_SPT_T, hScript);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == RUN_SPT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_RUN_SCT, E_RUN_SCT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_RUN_SCT, E_RUN_SCT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::stop_script(HSCRIPT hScript, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hScript;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(STOP_SPT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(STOP_SPT_T, hScript);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == STOP_SPT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_STP_SCT, E_STP_SCT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_STP_SCT, E_STP_SCT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::abort_script(HSCRIPT hScript, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hScript;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(ABORT_SPT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(ABORT_SPT_T, hScript);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == ABORT_SPT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_ABT_SCT, E_ABT_SCT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_ABT_SCT, E_ABT_SCT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
STRING VDCCommandAPI::get_script_name(HSCRIPT hScript, ERROR_STRUCT& error)
{
	STRING fn_return = STRING_WRN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hScript;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_SPT_NAME_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_SPT_NAME_T, hScript);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_SPT_NAME_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonName].asString();
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(true, E_GET_SPT_NAME, E_GET_SPT_NAME_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToString(VDC_Command_recv.MsgData.Data + size_LONG, pImpl->convToLong(VDC_Command_recv.MsgData.Data));
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(true, E_GET_SPT_NAME, E_GET_SPT_NAME_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
SCRIPT_STATE VDCCommandAPI::get_script_state(HSCRIPT hScript, ERROR_STRUCT& error)
{
	SCRIPT_STATE fn_return = SCRIPT_UNKNOWN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hScript;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_SPT_STATE_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_SPT_STATE_T, hScript);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_SPT_STATE_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (SCRIPT_STATE)Data[jsonState].asInt();
			if (fn_return = SCRIPT_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_SPT_STATE, E_GET_SPT_STATE_Src, error);
			if (fn_return = SCRIPT_ERROR)
				pImpl->setErrorStruct(true, E_GET_SPT_STATE_E, E_GET_SPT_STATE_E_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = (SCRIPT_STATE)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return = SCRIPT_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_SPT_STATE, E_GET_SPT_STATE_Src, error);
			if (fn_return = SCRIPT_ERROR)
				pImpl->setErrorStruct(true, E_GET_SPT_STATE_E, E_GET_SPT_STATE_E_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
bool VDCCommandAPI::set_script_state(HSCRIPT hScript, SCRIPT_STATE state, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]	= hScript;  
		Data[jsonState] = state;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(SET_SPT_STATE_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_LONG);
		pImpl->pushData(hScript, dArray, 0);
		pImpl->pushData(state, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(SET_SPT_STATE_T, dArray, size_LLONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == SET_SPT_STATE_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_SET_SPT_STATE, E_SET_SPT_STATE_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_SET_SPT_STATE, E_SET_SPT_STATE_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);

	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
SCRIPT_CMD VDCCommandAPI::get_script_command(HSCRIPT hScript, ERROR_STRUCT& error)
{
	SCRIPT_CMD fn_return = SCRIPTCMD_UNKNOWN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hScript;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_SPT_CMD_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_SPT_CMD_T, hScript);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_SPT_CMD_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (SCRIPT_CMD)Data[jsonCMD].asInt();
			if (fn_return == SCRIPTCMD_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_SPT_CMD, E_GET_SPT_CMD_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = (SCRIPT_CMD)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == SCRIPTCMD_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_SPT_CMD, E_GET_SPT_CMD_Src, error);
		}
		break;
		default:
		{
			std::cout << "Serialization not implemented yet" << std::endl;
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) {
		/* Parse the Binary_1 Error Struct */
	}
	else {
		std::cout << "Undefined Message Frame " << std::endl;
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
/*****************************************************************************************************
Event Channel definition
	The event channel defines the communication line the event messages have to run over.
	This may be the current connection or a different on.
*****************************************************************************************************/
HCHANNEL VDCCommandAPI::create_event_channel(CHANNEL_TYPE sel, const STRING& rmtaddr, int port, SERIALIZATION_TYPE ser, ERROR_STRUCT& error)
{
	HCHANNEL fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonChTyp]		= sel;  
		Data[jsonRMT]		= rmtaddr;
		Data[jsonPORT]		= port;
		Data[jsonSerTyp]	= ser;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_EVNT_CHNL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LONG + size_STRING(rmtaddr) + size_LONG + size_LONG);
		pImpl->pushData(sel, dArray, 0);
		pImpl->pushData(rmtaddr, dArray, size_LONG);
		pImpl->pushData(port, dArray, size_LONG + rmtaddr.length());
		pImpl->pushData(ser, dArray, size_LONG + rmtaddr.length() + size_LONG);

		bytes = pImpl->Encode_and_Send(CRT_EVNT_CHNL_T, dArray, size_LONG + size_STRING(rmtaddr) + size_LONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_EVNT_CHNL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_EVNT_CH, E_CRT_EVNT_CH_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_EVNT_CH, E_CRT_EVNT_CH_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::delete_event_channel(HCHANNEL hChannel, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_EVNT_CHNL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(DEL_EVNT_CHNL_T, hChannel);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_EVNT_CHNL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_EVNT_CH, E_DEL_EVNT_CH_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_DEL_EVNT_CH, E_DEL_EVNT_CH_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;

}
/***************************************************************************************************/
void VDCCommandAPI::start_event_communication(HCHANNEL hChannel, ERROR_STRUCT& error)
{
	int bytes = 0;
	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(START_EVNT_COM_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(START_EVNT_COM_T, hChannel);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == START_EVNT_COM_R)
	{
		/* Do Nothing */
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* No Need to delete the VDC_Command_recv.msgdata.data as it was not set when data length is 0 */


}
/***************************************************************************************************/
void VDCCommandAPI::stop_event_communication(HCHANNEL hChannel, ERROR_STRUCT& error)
{
	int bytes = 0;
	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(STOP_EVNT_COM_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(STOP_EVNT_COM_T, hChannel);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == STOP_EVNT_COM_R)
	{
		/* Do Nothing */
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* No Need to delete the VDC_Command_recv.msgdata.data as it was not set when data length is 0 */

}
/***************************************************************************************************/
bool VDCCommandAPI::is_running(HCHANNEL hChannel, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(IS_RUN_EVNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(IS_RUN_EVNT_T, hChannel);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == IS_RUN_EVNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;


	return fn_return;
}
/***************************************************************************************************/
EVENTCHANNEL_STATE VDCCommandAPI::get_event_channel_state(HCHANNEL hChannel, ERROR_STRUCT& error)
{
	EVENTCHANNEL_STATE fn_return = EVENTCHANNEL_UNKNOWN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_EVNT_CHNL_STATE_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_EVNT_CHNL_STATE_T, hChannel);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_EVNT_CHNL_STATE_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (EVENTCHANNEL_STATE)Data[jsonState].asInt();
		}
		break;
		case BINARY_1:
		{
			fn_return = (EVENTCHANNEL_STATE)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == EVENTCHANNEL_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_EVNT_CH_STATE, E_GET_EVNT_CH_STATE_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
			if (fn_return == EVENTCHANNEL_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_EVNT_CH_STATE, E_GET_EVNT_CH_STATE_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
/* EVENT operations */
/***************************************************************************************************/
HEVENT VDCCommandAPI::create_systemheap_event(HCHANNEL hChannel, const SYSTEMHEAP_EVENT_STRUCT& eVent, ERROR_STRUCT& error)
{
	HEVENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel; 

		Json::Value JsonEvSt;
		JsonEvSt[jsonCON] = eVent.condition;

		Json::Value JsonARG;

		if (eVent.condition == eVent.ON_CREATE_BASEOBJECT)
		{
			JsonARG = Json::objectValue;
		}
		if (eVent.condition == eVent.ON_CREATE_DELEMENT)
		{
			JsonARG = Json::objectValue;
		}

		/* Using switch case to send the union part of the event */
		JsonEvSt[jsonARG]	= JsonARG;
		Data[jsonEVNT]		= JsonEvSt;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_SH_EVNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_LONG);
		pImpl->pushData(hChannel, dArray, 0);
		pImpl->pushData(eVent.condition, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(CRT_SH_EVNT_T, dArray, size_LLONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_SH_EVNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_SH_EVNT, E_CRT_SH_EVNT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_SH_EVNT, E_CRT_SH_EVNT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HEVENT VDCCommandAPI::create_data_element_event(HCHANNEL hChannel, HDELEMENT hElement, const DELEMENT_EVENT_STRUCT& eVent, ERROR_STRUCT& error)
{
	HEVENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  
		Data[jsonHARG] = hElement;

		Json::Value JsonEvSt;
		JsonEvSt[jsonCON] = eVent.condition;

		Json::Value JsonARG;
		if (eVent.condition == eVent.ON_DELETE)
		{
			JsonARG[jsonIT] = 0;
			JsonARG[jsonEvCyc] = 0;
		}
		else if (eVent.condition == eVent.ON_CYCLIC)
		{
			JsonARG[jsonIT] = 0;
			JsonARG[jsonEvCyc] = eVent.arg.on_cyclic.event_cycle;

		}
		if (eVent.condition == eVent.ON_VALUE_CHANGE)
		{
			JsonARG[jsonIT] = eVent.arg.on_value_change.inhibit_time;
			JsonARG[jsonEvCyc] = 0;
		}

		/* Using switch case to send the union part of the event */
		JsonEvSt[jsonARG]	= JsonARG;
		Data[jsonEVNT]		= JsonEvSt;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_DEL_EVNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LLONG + size_LONG + size_LONG); //alocating in case of max
		pImpl->pushData(hChannel, dArray, 0);
		pImpl->pushData(hElement, dArray, size_LLONG);
		pImpl->pushData(eVent.condition, dArray, size_LLONG + size_LLONG);

		if (eVent.condition == eVent.ON_CYCLIC) {
			pImpl->pushData(eVent.arg.on_cyclic.event_cycle, dArray, size_LLONG + size_LLONG + size_LONG);
			pImpl->Encode_and_Send(CRT_DEL_EVNT_T, dArray, size_LLONG + size_LLONG + size_LONG + size_LONG);
		}

		if (eVent.condition == eVent.ON_VALUE_CHANGE) {
			pImpl->pushData(eVent.arg.on_value_change.inhibit_time, dArray, size_LLONG + size_LLONG + size_LONG);
			pImpl->Encode_and_Send(CRT_DEL_EVNT_T, dArray, size_LLONG + size_LLONG + size_LONG + size_LONG);
		}

		pImpl->Encode_and_Send(CRT_DEL_EVNT_T, dArray, size_LLONG + size_LLONG + size_LONG );

	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_DEL_EVNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_DEL_EVNT, E_CRT_BO_EVNT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_DEL_EVNT, E_CRT_BO_EVNT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;


	return fn_return;
}
/***************************************************************************************************/
HEVENT VDCCommandAPI::create_baseobject_event(HCHANNEL hChannel, HBASEOBJECT h, const BASEOBJECT_EVENT_STRUCT& eVent, ERROR_STRUCT& error)
{
	HEVENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  
		Data[jsonHARG] = h;  

		Json::Value JsonEvSt;
		JsonEvSt[jsonCON] = eVent.condition;

		Json::Value JsonARG;
		if (eVent.condition == eVent.ON_DELETE)
		{
			JsonARG[jsonIT] = 0;
		}
		else if (eVent.condition == eVent.ON_CHILD_LINK_REMOVE)
		{
			JsonARG[jsonIT] = 0;
		}
		else if (eVent.condition == eVent.ON_LINK_CHANGE)
		{
			JsonARG[jsonIT] = 0;
		}
		if (eVent.condition == eVent.ON_VALUE_CHANGE)
		{
			JsonARG[jsonIT] = eVent.arg.on_value_change.inhibit_time;
		}

		/* Using switch case to send the union part of the event */
		JsonEvSt[jsonARG]	= JsonARG;
		Data[jsonEVNT]		= JsonEvSt;


		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_BO_EVNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LLONG + size_LONG + size_LONG);
		pImpl->pushData(hChannel, dArray, 0);
		pImpl->pushData(h, dArray, size_LLONG);
		pImpl->pushData(eVent.condition, dArray, size_LLONG + size_LLONG);

		if (eVent.condition == eVent.ON_VALUE_CHANGE)
			pImpl->pushData(eVent.arg.on_value_change.inhibit_time, dArray, size_LLONG + size_LLONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_BO_EVNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_BO_EVNT, E_CRT_BO_EVNT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_BO_EVNT, E_CRT_BO_EVNT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID)
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HEVENT VDCCommandAPI::create_transaction_event(HCHANNEL hChannel, HTG htg, const TRANSACTION_EVENT_STRUCT& eVent, ERROR_STRUCT& error)
{
	HEVENT fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hChannel;  
		Data[jsonHARG] = htg;  

		Json::Value JsonEvSt;
		JsonEvSt[jsonCON] = eVent.condition;

		Json::Value JsonARG;
		if (eVent.condition == eVent.ON_DELETE)
		{
			JsonARG[jsonIT] = 0;
			JsonARG[jsonEvCyc] = 0;
		}
		else if (eVent.condition == eVent.ON_CYCLIC)
		{
			JsonARG[jsonIT] = 0;
			JsonARG[jsonEvCyc] = eVent.arg.on_cyclic.event_cycle;
		}
		else if (eVent.condition == eVent.ON_MEMBER_CHANGE)
		{
			JsonARG[jsonIT] = 0;
			JsonARG[jsonEvCyc] = 0;
		}
		if (eVent.condition == eVent.ON_VALUE_CHANGE)
		{
			JsonARG[jsonIT] = eVent.arg.on_value_change.inhibit_time;
			JsonARG[jsonEvCyc] = 0;
		}

		/* Using switch case to send the union part of the event */
		JsonEvSt[jsonARG]	= JsonARG;
		Data[jsonEVNT]		= JsonEvSt;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(CRT_TRNS_EVNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LLONG + size_LONG + size_LONG);
		pImpl->pushData(hChannel, dArray, 0);
		pImpl->pushData(htg, dArray, size_LLONG);
		pImpl->pushData(eVent.condition, dArray, size_LLONG + size_LLONG);

		if (eVent.condition == eVent.ON_CYCLIC)
			pImpl->pushData(eVent.arg.on_cyclic.event_cycle, dArray, size_LLONG + size_LLONG + size_LONG);

		if (eVent.condition == eVent.ON_VALUE_CHANGE)
			pImpl->pushData(eVent.arg.on_value_change.inhibit_time, dArray, size_LLONG + size_LLONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_TRNS_EVNT_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_TRN_EVNT, E_CRT_BO_EVNT_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_TRN_EVNT, E_CRT_BO_EVNT_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
void VDCCommandAPI::delete_event(HEVENT hEvent, ERROR_STRUCT& error)
{
	int bytes = 0;
	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = hEvent;

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_EVNT_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(DEL_EVNT_T, hEvent);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_EVNT_R)
	{
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* No Need to delete the VDC_Command_recv.msgdata.data as it was not set when data length is 0 */



}/***************************************************************************************************/
/***************************************************************************************************/
/*  TRANSACTION FUNCTIONS*/
/***************************************************************************************************/
HTG VDCCommandAPI::create_transaction_group(ERROR_STRUCT& error)
{
	HTG fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON || pImpl->serialization == BINARY_1) {

		bytes = pImpl->Encode_and_Send(CRT_TRNS_GR_T); //Function ID
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return; 
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == CRT_TRNS_GR_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_TRN_G, E_CRT_TRN_G_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_CRT_TRN_G, E_CRT_TRN_G_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;


	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::add_data_element_ref(HTG htg, HDELEMENT hElement, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HTG)htg;    
		Data[jsonHARG] = (VDC_API::HDELEMENT)hElement;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(ADD_EL_REF_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char *dArray = pImpl->init_dArray(size_LLONG + size_LLONG);
		pImpl->pushData(htg, dArray, 0);
		pImpl->pushData(hElement, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(ADD_EL_REF_T, dArray, size_LLONG + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == ADD_EL_REF_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_ADD_DEL_REF, E_ADD_DEL_REF_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_ADD_DEL_REF, E_ADD_DEL_REF_Src, error); 
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
bool VDCCommandAPI::add_baseobject_ref(HTG htg, HBASEOBJECT hObj, ERROR_STRUCT& error)
{
	bool fn_return = false;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HTG)htg;   
		Data[jsonHARG] = (VDC_API::HBASEOBJECT)hObj;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(ADD_BO_REF_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LLONG);
		pImpl->pushData(htg, dArray, 0);
		pImpl->pushData(hObj, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(ADD_BO_REF_T, dArray, size_LLONG + size_LLONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == ADD_BO_REF_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data, ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonRslt].asBool();
			if (!fn_return)
				pImpl->setErrorStruct(true, E_ADD_BO_REF, E_ADD_BO_REF_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToBool(VDC_Command_recv.MsgData.Data);
			if (!fn_return)
				pImpl->setErrorStruct(true, E_ADD_BO_REF, E_ADD_BO_REF_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
void VDCCommandAPI::delete_transaction_group(HTG htg, ERROR_STRUCT& error)
{
	int bytes = 0;
	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HTG)htg;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(DEL_TRNS_GR_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(DEL_TRNS_GR_T, htg);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == DEL_TRNS_GR_R)
	{
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* No Need to delete the VDC_Command_recv.msgdata.data as it was not set when data length is 0 */

}
/***************************************************************************************************/
TRANSACTIONSTATE VDCCommandAPI::transaction_lock(HTG htg, VDC_API::LONG timeout, ERROR_STRUCT& error)
{
	TRANSACTIONSTATE fn_return = TRNS_STATE_UNKNOWN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl]	= (VDC_API::HTG)htg;    
		Data[jsonTO]	= timeout;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(TRNS_LOCK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		unsigned char* dArray = pImpl->init_dArray(size_LLONG + size_LONG);
		pImpl->pushData(htg, dArray, 0);
		pImpl->pushData(timeout, dArray, size_LLONG);

		bytes = pImpl->Encode_and_Send(TRNS_LOCK_T, dArray, size_LLONG + size_LONG);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == TRNS_LOCK_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (TRANSACTIONSTATE)Data[jsonState].asInt();
			if (fn_return == TRNS_STATE_UNKNOWN)
				pImpl->setErrorStruct(true, E_TRN_LOCK, E_TRN_LOCK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = (TRANSACTIONSTATE)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == TRNS_STATE_UNKNOWN)
				pImpl->setErrorStruct(true, E_TRN_LOCK, E_TRN_LOCK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
TRANSACTIONSTATE VDCCommandAPI::get_transaction_state(HTG htg, ERROR_STRUCT& error)
{
	TRANSACTIONSTATE fn_return = TRNS_STATE_UNKNOWN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HTG)htg;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_TRNS_STATE_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_TRNS_STATE_T, htg);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_TRNS_STATE_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (TRANSACTIONSTATE)Data[jsonState].asInt();
			if (fn_return == TRNS_STATE_UNKNOWN)
				pImpl->setErrorStruct(true, E_TRN_STATE, E_TRN_LOCK_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = (TRANSACTIONSTATE)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == TRNS_STATE_UNKNOWN)
				pImpl->setErrorStruct(true, E_TRN_STATE, E_TRN_LOCK_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
void VDCCommandAPI::transaction_unlock(HTG htg, ERROR_STRUCT& error)
{
	int bytes = 0;
	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HTG)htg;  

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(TRNS_UNLOCK_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(TRNS_UNLOCK_T, htg);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == TRNS_UNLOCK_R)
	{
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* No Need to delete the VDC_Command_recv.msgdata.data as it was not set when data length is 0 */


}
/***************************************************************************************************/
/*  ATOM ACCESS FUNCTIONS  */
/***************************************************************************************************/
HATOM VDCCommandAPI::get_atom_handle(VDC_API::HANDLE h, ERROR_STRUCT & error)
{
	HATOM fn_return = HANDLE_ERR;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HANDLE)h;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_ATOM_HNDL_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_ATOM_HNDL_T, h);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_ATOM_HNDL_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonHndl].asInt64();
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_GET_ATOM_HNDL, E_GET_ATOM_HNDL_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToLLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == HANDLE_ERR)
				pImpl->setErrorStruct(true, E_GET_ATOM_HNDL, E_GET_ATOM_HNDL_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
HATOMTYPE VDCCommandAPI::get_atom_handle_type(HATOM h, ERROR_STRUCT& error)
{
	HATOMTYPE fn_return = TH_UNKNOWN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HATOM)h;   

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_ATOM_HNDL_TYP_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_ATOM_HNDL_T, h);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_ATOM_HNDL_TYP_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = (HATOMTYPE)Data[jsonTYP].asInt();
			if (fn_return == TH_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_ATOM_HNDL_Typ, E_GET_ATOM_HNDL_Typ_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = (HATOMTYPE)pImpl->convToLong(VDC_Command_recv.MsgData.Data);
			if (fn_return == TH_UNKNOWN)
				pImpl->setErrorStruct(true, E_GET_ATOM_HNDL_Typ, E_GET_ATOM_HNDL_Typ_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);

	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
/***************************************************************************************************/
STRING VDCCommandAPI::get_atom_handle_name(HATOM h, ERROR_STRUCT& error)
{
	STRING fn_return = STRING_WRN;

	int bytes = 0;

	Command_Message VDC_Command_recv;

	/*___sending sequence____*/
	if (pImpl->serialization == JSON) {
		Json::Value Data;
		Data[jsonHndl] = (VDC_API::HATOM)h;    

		std::string json_string = pImpl->Json_Serialize(Data);
		bytes = pImpl->Encode_and_Send(GET_ATOM_HNDL_NAME_T, json_string); //Function ID
	}
	else if (pImpl->serialization == BINARY_1) {
		bytes = pImpl->Encode_and_Send(GET_ATOM_HNDL_NAME_T, h);
	}
	else {
		pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		return fn_return;  
	}

	/*___receiving sequence____*/
	bytes = pImpl->recv_VDC_Command(&VDC_Command_recv);
	if (bytes <= 0) {
		pImpl->setErrorStruct(true, E_RECEIVE_ERROR, E_RECEIVE_ERROR_Src, error);
		return fn_return;
	}
	/* Check if the correct Function ID */
	if (VDC_Command_recv.Mdesc.Function_ID == GET_ATOM_HNDL_NAME_R)
	{
		/* Check the Serialization */
		switch (VDC_Command_recv.Mdesc.Serialization)
		{
		case JSON:
		{
			Json::Value Data;
			Data = pImpl->Json_DeSerialize(VDC_Command_recv.MsgData.Data,
				ntohl(VDC_Command_recv.length) - Msg_Descr_Size);
			fn_return = Data[jsonName].asString();
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(true, E_GET_AHNDL_NAME, E_GET_AHNDL_NAME_Src, error);
		}
		break;
		case BINARY_1:
		{
			fn_return = pImpl->convToString(VDC_Command_recv.MsgData.Data + size_LONG, pImpl->convToLong(VDC_Command_recv.MsgData.Data));
			if (fn_return == STRING_WRN)
				pImpl->setErrorStruct(true, E_GET_AHNDL_NAME, E_GET_AHNDL_NAME_Src, error);
		}
		break;
		default:
		{
			pImpl->setErrorStruct(true, E_FASLE_ENCODING, E_FASLE_ENCODING_Src, error);
		}
		break;
		}
	}
	else if (VDC_Command_recv.Mdesc.Function_ID == ABORT_TRANSFER_FID) 
	{
		/*Check if Binary 1 Serialization*/
		if (VDC_Command_recv.Mdesc.Serialization == BINARY_1)
			pImpl->setErrorStruct(error, VDC_Command_recv);
		else
			pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	else 
	{
		pImpl->setErrorStruct(true, E_MESSAGE_INVALID, E_MESSAGE_INVALID_Src, error);
	}
	/* The memeory was allocated in recv_VDC_Command Function */
	delete[] VDC_Command_recv.MsgData.Data;

	return fn_return;
}
