/*
	Initial
*/
#include "VDCEventAPI.h"
#include <iostream>
#include "VDC_Types.h"

#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>

/* WinSock Pragmas */
#pragma comment(lib,"ws2_32.lib") //Winsock Library
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
#pragma warning(disable:4996)


VDCConsumerEventAPI::VDCConsumerEventAPI()
{
	/* Setting the init state of the stop thread flag */
	evChannelStopFlag = false;

	/* wait Flag for the Consumer */
	wait_flag = 1;
}

VDCConsumerEventAPI::~VDCConsumerEventAPI()
{
	/* Handling the safe joining of the Child Thread with the Main thread */

	if(this->evChannelThread.joinable()) /* To make sure that if the Thread was already joined by closeConsumer this doesn't execute */
	{ 
		/* Joining the thread as it would have stopped through the stop Flag  */
		this->evChannelThread.join();
		std::cout << "Destructor joining the Consumer" << std::endl;
	}

}

void VDCConsumerEventAPI::consumer_wait()
{
	wait_flag += 1;
	if (wait_flag <= 1)
		fu_global.get();
	else
	{
		// Error Handling
		std::cout << "Wait function already called for this consumer" << std::endl;
	}
}

bool VDCConsumerEventAPI::consumer_open(const STRING& net_address, int port)
{

	/*
		The return value is also a shared resource but it is handled via the packaged task
		which goes through the mutex and concition varibale hence no Race Condition
	*/
	bool fn_return = false;

	/* Starting the Thread. The TaskDispatcher function will execute in a seperate thread  */
	this->evChannelThread = std::thread(&VDCConsumerEventAPI::TaskDispatcher, this);

	/* 
		Creating the Packaged Task for the Given Command 
		This line of code converts the lambda into a 
	*/
	std::packaged_task<void()> t([&net_address, port , this]() {

		int iResult = 0;

		/* Init WinSock */ //would have already been initialited by the Command Channel
		//iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		//if (iResult != 0)
		//{
		//	std::cout << "Error in WSAStartup Consumer" << std::endl;
		//}

		/* Initializing the ServerInfo Struct */
		local.sin_family = AF_INET;
		local.sin_port = htons(port);
		local.sin_addr.S_un.S_addr = INADDR_ANY; //have to see if to use INADDR_ANY

		/* Creating the Socket */
		ConnectSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (ConnectSocket <= 0)
		{
			std::cout << "Error in creating the Consumer Socket" << std::endl;
		}

		/* Binding the Local Socket */
		iResult = bind(ConnectSocket, (sockaddr*)&local, sizeof(local));
		if (iResult != 0)
		{
			std::cout << "Consumer Sokcet Binding failed" << std::endl;
		}

		std::cout << "Consumer Socket Started" << std::endl;
	});

	/* Creating a placeholder for the return value from the thread. This will hold the value when get is called */
	std::future<void> fu = t.get_future(); 

	/* Pushing the packaged task created above to the task queue through the mutex as queue is a shared resource */
	{
		std::lock_guard<std::mutex> locker(evChannelMutex);
		evChannelTaskQueue.push_back(std::move(t));  //Pushing the task to the back of the queue

		/* Notifying the Task Dispatcher about the task being pushed */
		evChannelCV.notify_one(); /* Now the Task Dispatcher which was waiting will start executing the task in parallel  */
	}

	/* Getting the future from the thread... This command makes the main thread to wait till the task has been executed by the dispatcher */
	fu.get();

	/*
		fn_return is a shared resource between the main thread and the child thread
		fu.get() above at the backend makes the main thread to wait till the value
		from the thread is ready. Once the value is received the program moves further
	*/

	return fn_return;
}

bool VDCConsumerEventAPI::consumer_close()
{
	/*
		The return value is also a shared resource but it is handled via the packaged task
		which goes through the mutex and concition varibale hence no Race Condition
	*/
	bool fn_return = false;


	/*
		Creating the packaged task for the given command
		This line of code converts the lambda into a packaged task
	*/
	std::packaged_task<void()> t([ this]() {
		
		closesocket(ConnectSocket);
		//std::cout << "The UDP Socket closed" << std::endl;
		evChannelStopFlag = true;

	});

	/* Pushing the packaged task into the task queue through the mutex */
	{
		std::lock_guard<std::mutex> locker(evChannelMutex);
		evChannelTaskQueue.push_back(std::move(t));

		/* Notifying the Child Thread(TD) about the push back */
		evChannelCV.notify_one();
	}

	/*
		fn_return is a shared resource between the main thread and the child thread
		fu.get() above at the backend makes the main thread to wait till the value
		from the thread is ready. Once the value is received the program moves further
	*/

	return fn_return;

}

void VDCConsumerEventAPI::convFrom8bit(uint8_t *a, VDC_API::SHORT& fn_return)
{
	fn_return = 0x0000;
	fn_return = (*(a + 1) << 0) | ((*a) << 8);
}
void VDCConsumerEventAPI::convFrom8bit(uint8_t *a, VDC_API::LONG& fn_return)
{
	fn_return = 0x00000000;
	fn_return = (*(a + 3) << 0) | (*(a + 2) << 8) | (*(a + 1) << 16) | ((*a) << 24);

}
void VDCConsumerEventAPI::convFrom8bit(uint8_t *a, VDC_API::DOUBLE& fn_return)
{
	fn_return = 0x0000000000000000;
	fn_return = (*(a + 7) << 0) | (*(a + 6) << 8) | (*(a + 5) << 16) | (*(a + 4) << 24)
		        |(*(a + 3) << 32) | (*(a + 2) << 40) | (*(a + 1) << 48) | ((*a) << 56);
}
void VDCConsumerEventAPI::convFrom8bit(uint8_t *a, VDC_API::LLONG& fn_return)
{
	fn_return = 0x0000000000000000;
	fn_return = (*(a + 7) << 0) | (*(a + 6) << 8) | (*(a + 5) << 16) | (*(a + 4) << 24)
				| (*(a + 3) << 32) | (*(a + 2) << 40) | (*(a + 1) << 48) | ((*a) << 56);
}
void VDCConsumerEventAPI::convFrom8bit(uint8_t *a, VDC_API::VECTOR& fn_return)
{
	convFrom8bit(a, fn_return.x);
	convFrom8bit(a + 8, fn_return.y);
	convFrom8bit(a + 16, fn_return.z);

}
void VDCConsumerEventAPI::convFrom8bit(uint8_t *a, VDC_API::MATRIX& fn_return)
{
	convFrom8bit(a, fn_return.v0);
	convFrom8bit(a + 24, fn_return.v1);
	convFrom8bit(a + 48, fn_return.v2);
	convFrom8bit(a + 72, fn_return.v3);
}

void VDCConsumerEventAPI::convFrom8bit(uint8_t *a, VDC_API::EvString& fn_return)
{
	fn_return.data = new char[fn_return.size];

	for (int i = 0; i < fn_return.size; i++)
	{
		fn_return.data[i] = *(a + i);
	}
}
VDC_API::SHORT VDCConsumerEventAPI::convToSShort(unsigned char* dArray)
{
	VDC_API::SSHORT fn_return = (VDC_API::SSHORT)dArray[0];
	return fn_return;
}
VDC_API::SHORT VDCConsumerEventAPI::convToShort(unsigned char* dArray)
{
	VDC_API::SHORT fn_return = ((VDC_API::SHORT)dArray[1] << 0) |
		((VDC_API::SHORT)dArray[0] << 8);
	return fn_return;
}
VDC_API::LONG VDCConsumerEventAPI::convToLong(unsigned char* dArray)
{
	VDC_API::LONG fn_return = ((VDC_API::LONG)dArray[3] << 0  ) |
		((VDC_API::LONG)dArray[2] << 8) |
		((VDC_API::LONG)dArray[1] << 16) |
		((VDC_API::LONG)dArray[0] << 24);
	return fn_return;
}
VDC_API::LLONG VDCConsumerEventAPI::convToLLong(unsigned char* dArray)
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
VDC_API::DOUBLE VDCConsumerEventAPI::convToDouble(unsigned char* dArray)
{
	VDC_API::DOUBLE fn_return = (VDC_API::LLONG)(dArray[7] << 0) |
		((VDC_API::LLONG)dArray[6] << 8) |
		((VDC_API::LLONG)dArray[5] << 16) |
		((VDC_API::LLONG)dArray[4] << 24) |
		((VDC_API::LLONG)dArray[3] << 32) |
		((VDC_API::LLONG)dArray[2] << 40) |
		((VDC_API::LLONG)dArray[1] << 48) |
		((VDC_API::LLONG)dArray[0] << 56);
	return fn_return;
}
VDC_API::BOOL VDCConsumerEventAPI::convToBool(unsigned char * DataArray)
{
	return (VDC_API::BOOL)DataArray[0];
}
VDC_API::STRING VDCConsumerEventAPI::convToString(unsigned char* dArray, size_t size)
{
	VDC_API::STRING fn_return((char*)dArray, size);
	return fn_return;
}
VDC_API::VECTOR VDCConsumerEventAPI::convToVector(unsigned char * DataArray)
{
	VDC_API::VECTOR fn_return;

	fn_return.x = convToLLong(DataArray);
	fn_return.y = convToLLong(DataArray + size_LLONG);
	fn_return.z = convToLLong(DataArray + size_LLONG + size_LLONG);

	return  fn_return;
}
VDC_API::MATRIX VDCConsumerEventAPI::convToMatrix(unsigned char * DataArray)
{
	VDC_API::MATRIX fn_return;

	fn_return.v0 = convToVector(DataArray);
	fn_return.v1 = convToVector(DataArray + size_VECTOR);
	fn_return.v2 = convToVector(DataArray + size_VECTOR + size_VECTOR);
	fn_return.v3 = convToVector(DataArray + size_VECTOR + size_VECTOR + size_VECTOR);

	return fn_return;
}


//bool VDCConsumerEventAPI::consumer_read_single(TIMESTAMP& t_stamp, HEVENT hevent, Data_Set& data_set, VDC_API::LONG timeout, VDC_API::LONG &length, ERROR_STRUCT &error)
//{
//	bool fn_return = false;
//
//	/* 
//		The PAckaged Task for the given command
//	*/
//	std::packaged_task<void()>  t([this, &t_stamp, hevent, &data_set, timeout, &length, &error]() {
//
//		// Set Socket options for Timeout
//		struct timeval tv;
//		tv.tv_sec = timeout; // This is ms as SO_RCVTIMEO converts s to ms
//		tv.tv_usec = 0;
//
//		if (setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv)) < 0) {
//			error.code = 123;
//			error.status = true;
//			error.source = "UDP SOCKET SET OPTIONS";
//			std::cout << "Something Wrong" << std::endl;
//		}
//	
//		/*
//			Char array to receive the UDP Message. Hardcoding the size to be maximum
//		*/
//		uint8_t recv_buffer[4096];
//		ZeroMemory(recv_buffer, sizeof(recv_buffer));
//
//		/* second sockaddr_in for the recvfrom */
//		struct sockaddr_in from;   
//		int fromlen = sizeof(from);
//
//		/* number of bytes recv */
//		int bytes_recv = 0;
//
//		/* 
//			flag to indicate if all the event message came in one UDP paacket
//			false means only one packet.
//			calculated based on the length and bytes_recv. If length + 4 <= bytes_recv then event info came in one udp packet
//		*/
//		bool packet_flag = false;
//
//		/* 
//			Receivng the UDP Packet 
//		*/
//		bytes_recv = recvfrom(ConnectSocket, (char*)recv_buffer, 4096, 0, (sockaddr*)&from, &fromlen);
//
//		if (bytes_recv == SOCKET_ERROR) {
//			error.code = 234;
//			error.status = true;
//			error.source = "UDP recvfrom";
//			std::cout << "Error in recv UDP " << WSAGetLastError() << std::endl;
//		}
//
//		for (int i = 0; i < 40; i++)
//			printf("%02x", recv_buffer[i]);
//		printf("\n");
//
//		/* Copying the Headers */
//
//		/* Short Endian */
//		convFrom8bit(recv_buffer,length);
//
//		std::cout <<"THE LENGTH " <<length << std::endl;
//
//		if (length + 4 <= bytes_recv){
//			packet_flag = true;
//			std::cout << "The packet Flag is "<< packet_flag << std::endl;
//		}
//
//		uint8_t Selector		= recv_buffer[4];
//		std::cout << "Selector = " << Selector << std::endl;
//
//		uint8_t Serialization	= recv_buffer[5];
//		std::cout << "Serialization = " << Serialization << std::endl;
//
//		VDC_API::SHORT FunctionID;
//		convFrom8bit(recv_buffer+6, FunctionID);
//		std::cout << "Function ID = " << FunctionID <<std::endl;
//
//		VDC_API::LONG Seq_No;  
//		convFrom8bit(recv_buffer + 8, Seq_No);
//		std::cout << "Seq No = " << Seq_No << std::endl;
//
//		convFrom8bit(recv_buffer+12, t_stamp);
//		std::cout << "TimeStamp = " << t_stamp <<std::endl;
//
//		/* 
//			First Evnet Element Starts form now
//			(HEVENT, SIZE, DATASET). (HEVENT, SIZE, DATASET)......
//		*/
//
//		HEVENT hevent_recv;
//		VDC_API::LONG size = 0;
//
//		int index = 20;  //index points at the start of EventElement
//
//		while ( length >= 17 )
//		{
//			/* Copying the received HEVENT */
//			for (int i = 0; i < 8; i++)
//				printf("%02x", *(recv_buffer + index + i));
//			printf("\n");
//			convFrom8bit(recv_buffer + index , hevent_recv);
//
//			/* Copying the received Size */
//			convFrom8bit(recv_buffer + index + 8, size);
//			
//			/*
//				Now we have the hevent that we received
//				Comparing the passed with the local Hevent
//			*/
//			if (hevent_recv == hevent)
//			{
//				std::cout << "Match found" << std::endl;
//
//				/* Finding out the Type of the Event Received */
//				switch (data_set.Event_Type)
//				{
//				case Data_Set::ev_SH_ON_CREATE_ANY:
//					convFrom8bit(recv_buffer + index + 12, (VDC_API::LONG&)data_set.Event_value.ev_V_SH_ON_CREATE_ANY.Handle_Type);
//					convFrom8bit(recv_buffer + index + 16, data_set.Event_value.ev_V_SH_ON_CREATE_ANY.Handle);
//					break;
//				case  Data_Set::ev_SH_ON_CREATE_DELEMENT:
//					convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_SH_ON_CREATE_DELEMENT.Handle);
//					break;
//				case Data_Set::ev_SH_ON_CREATE_BASEOBJECT:
//					convFrom8bit(recv_buffer+index+12, data_set.Event_value.ev_V_SH_ON_CREATE_BASEOBJECT.Handle);
//					convFrom8bit(recv_buffer+index+20, data_set.Event_value.ev_V_SH_ON_CREATE_BASEOBJECT.Object_ID);
//					break;
//				case Data_Set::ev_DEL_ON_DELETE:
//					/* Nothing to send */
//					break;
//				case Data_Set::ev_DEL_ON_VALUE_CHANGE:
//					
//					switch ( data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Type)
//					{
//					case EVENT_VALUE::ev_long:
//						 convFrom8bit(recv_buffer+index+12, data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vlong);
//						 break;
//					case EVENT_VALUE::ev_double:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdouble);
//						break;
//					case EVENT_VALUE::ev_string:
//						data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_datetime:
//						data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_vector:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vvector);
//						break;
//					case EVENT_VALUE::ev_matrix:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vmatrix);
//						break;
//					case EVENT_VALUE::ev_handle:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vhandle);
//						break;
//					default:
//						break;
//					}
//
//					break;
//				case Data_Set::ev_DEL_ON_CYCLIC:
//					switch (data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Type)
//					{
//					case EVENT_VALUE::ev_long:
//						 convFrom8bit(recv_buffer+index+12, data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vlong);
//						 break;
//					case EVENT_VALUE::ev_double:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vdouble);
//						break;
//					case EVENT_VALUE::ev_string:
//						data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_datetime:
//						data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_vector:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vvector);
//						break;
//					case EVENT_VALUE::ev_matrix:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vmatrix);
//						break;
//					case EVENT_VALUE::ev_handle:
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vhandle);
//						break;
//					default:
//						break;
//					}
//
//					break;
//				case Data_Set::ev_BO_ON_DELETE:
//					/* Nothing  */
//					break;
//				case Data_Set::ev_BO_ON_VALUE_CHANGE:
//					 convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Handle);
//
//					switch (data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Type)
//					{
//					case EVENT_VALUE::ev_long:
//						 convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vlong);
//						 break;
//					case EVENT_VALUE::ev_double:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdouble);
//						break;
//					case EVENT_VALUE::ev_string:
//						data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_datetime:
//						data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_vector:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vvector);
//						break;
//					case EVENT_VALUE::ev_matrix:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vmatrix);
//						break;
//					case EVENT_VALUE::ev_handle:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vhandle);
//						break;
//					default:
//						break;
//					}
//
//					break;
//				case Data_Set::ev_BO_ON_LINK_CHANGE:
//					convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_BO_ON_LINK_CHANGE.Handle_Parent);
//					convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_BO_ON_LINK_CHANGE.Handle_Child);
//					convFrom8bit(recv_buffer + index + 28, data_set.Event_value.ev_V_BO_ON_LINK_CHANGE.MI);
//					break;
//				case Data_Set::ev_BO_ON_CHILD_LINK_REMOVE:
//					convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_BO_ON_CHILD_LINK_REMOVE.Handle);
//					break;
//				case Data_Set::ev_TRN_ON_DELETE:
//					/* NO DataSet */
//					break;
//				case Data_Set::ev_TRN_ON_MEMBER_CHANGE:
//					 convFrom8bit(recv_buffer + index + 12, (VDC_API::LONG&)data_set.Event_value.ev_V_TRN_ON_MEMBER_CHANGE.Handle_Type);
//					 convFrom8bit(recv_buffer + index + 16, data_set.Event_value.ev_V_TRN_ON_MEMBER_CHANGE.Handle);
//					break;
//				case Data_Set::ev_TRN_ON_VALUE_CHANGE:
//					convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Handle);
//					
//					switch (data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Type)
//					{
//					case EVENT_VALUE::ev_long:
//						 convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vlong);
//						 break;
//					case EVENT_VALUE::ev_double:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdouble);
//						break;
//					case EVENT_VALUE::ev_string:
//						data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_datetime:
//						data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_vector:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vvector);
//						break;
//					case EVENT_VALUE::ev_matrix:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vmatrix);
//						break;
//					case EVENT_VALUE::ev_handle:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vhandle);
//						break;
//					default:
//						break;
//					}
//					
//					break;
//				case Data_Set::ev_TRN_ON_CYCLIC:
//					 convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Handle);
//
//					switch (data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Type)
//					{
//					case EVENT_VALUE::ev_long:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vlong);
//						break;
//					case EVENT_VALUE::ev_double:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vdouble);
//						break;
//					case EVENT_VALUE::ev_string:
//						data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_datetime:
//						data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vstring.size = size;
//						convFrom8bit(recv_buffer + index + 12, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vstring);
//						break;
//					case EVENT_VALUE::ev_vector:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vvector);
//						break;
//					case EVENT_VALUE::ev_matrix:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vmatrix);
//						break;
//					case EVENT_VALUE::ev_handle:
//						convFrom8bit(recv_buffer + index + 20, data_set.Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vhandle);
//						break;
//					default:
//						break;
//					}
//					break;
//				default:
//					break;
//				}
//			}
//
//			if (length <= size + index + 12) { //12 = sizeof(HEVENT) + sizeof(size)
//				/* No more Event elements */
//				break;
//			}
//
//			index = index + 12 + size; //Index points to the next element's hevent. 
//		}
//
//	});
//
//
//	/* Pushing the packaged task into the task queue through the mutex */
//	{
//		std::lock_guard<std::mutex> locker(evChannelMutex);
//		evChannelTaskQueue.push_back(std::move(t));
//
//		/* Notifying the Child Thread(TD) about the push back */
//		evChannelCV.notify_one();
//	}
//
//	/*
//		fn_return is a shared resource between the main thread and the child thread
//		fu.get() above at the backend makes the main thread to wait till the value
//		from the thread is ready. Once the value is received the program moves further
//	*/
//
//	return true;
//}

bool VDCConsumerEventAPI::consumer_read(TIMESTAMP& t_stamp, HEVENT* hevent_list, Data_Set* data_set_list, int NoEvEl, VDC_API::LONG timeout, VDC_API::LONG  &length, ERROR_STRUCT &error)
{
	bool fn_return = false;
	wait_flag -= 1;
	/*
		Using the Global Read Taks here as later the user needs to call the wait function if he/she wants to wait
	*/

	std::packaged_task<void()>  t([this, &t_stamp, hevent_list, data_set_list,NoEvEl, timeout, &length, &error]() {

		// Set Socket options for Timeout
		struct timeval tv;
		tv.tv_sec = timeout; // This is ms as SO_RCVTIMEO converts s to ms
		tv.tv_usec = 0;

		if (setsockopt(ConnectSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv)) < 0) {
			error.code = 123;
			error.status = true;
			error.source = "UDP SOCKET SET OPTIONS";
		}

		/*
			Char array to receive the UDP Message. Hardcoding the size to be maximum
		*/
		uint8_t recv_buffer[4096];
		ZeroMemory(recv_buffer, sizeof(recv_buffer));

		/* second sockaddr_in for the recvfrom */
		struct sockaddr_in from;
		int fromlen = sizeof(from);

		/* number of bytes recv */
		int bytes_recv = 0;

		/*
			flag to indicate if all the event message came in one UDP paacket
			false means only one packet.
			calculated based on the length and bytes_recv. If length + 4 <= bytes_recv then event info came in one udp packet
		*/
		bool packet_flag = false;

		/*
		Receivng the UDP Packet
		*/
		bytes_recv = recvfrom(ConnectSocket, (char*)recv_buffer, 4096, 0, (sockaddr*)&from, &fromlen);

		if (bytes_recv == SOCKET_ERROR) {
			error.code = WSAGetLastError();
			error.status = true;
			error.source = "UDP recvfrom";
		}
		/* Copying the Headers */

		/* Short Endian */
		length = convToLong(recv_buffer);
		std::cout << "THE LENGTH " << length << std::endl;

		if (length + 4 > bytes_recv) {
			packet_flag = true;
			std::cout << "The packet Flag is " << packet_flag << std::endl;
		}

		for (int i = 0; i < length + 4; i++)
			printf("%02x", recv_buffer[i]);
		printf("\n");

		uint8_t Selector = recv_buffer[4];
		std::cout << "Selector = " << Selector << std::endl;

		uint8_t Serialization = recv_buffer[5];
		std::cout << "Serialization = " << Serialization << std::endl;

		VDC_API::SHORT FunctionID;
		FunctionID = convToShort(recv_buffer + 6);
		std::cout << "Function ID = " << FunctionID << std::endl;

		VDC_API::LONG Seq_No;
		Seq_No = convToShort(recv_buffer + 8);
		std::cout << "Seq No = " << Seq_No << std::endl;

		t_stamp = convToLLong(recv_buffer + 12);
		std::cout << "TimeStamp = " << t_stamp << std::endl;

		/*
			First Evnet Element Starts form now
			(HEVENT, SIZE, DATASET). (HEVENT, SIZE, DATASET)......
		*/

		HEVENT hevent_recv;
		VDC_API::SSHORT size = 0;

		int value_index = 0; //index pointing to the current HEVENT in the hevent_list
		int check = 0; //index for the "find loop". 
		int index = 20;  //index points at the start of EventElement

		std::cout << "size of hevent list " << sizeof(hevent_list) / 4 << std::endl;
		
		while (length > 16)
		{
			/* Copying the received HEVENT */
			hevent_recv = convToLLong(recv_buffer + index);

			/* Copying the received Size */
			size = convToSShort(recv_buffer + index + 8);
			
			/* Find Loop */
			for (int i = check ; i < NoEvEl; i++)
			{
				if (hevent_recv == *(hevent_list + i))
				{
					check = i + 1;   //so that the next time the loop is called the if is not performed again on the previous members.
					break;
				}
			}

			/********************************************************************************************************************************
			IMPORTANT NOTE: The Data is converted directly from big Endian to Visual Studio Variables. That is the new observation. ??
			****************************/
			VDC_API::LONG ll;

			/*
				Now we have the hevent that we received
				Comparing the passed with the local Hevent
			*/
			value_index = check - 1;
			if (hevent_recv == hevent_list[value_index])
			{
				std::cout << "Match found" << std::endl;

				/* Finding out the Type of the Event Received */
				switch (data_set_list[value_index].Event_Type)
				{
				case Data_Set::ev_SH_ON_CREATE_ANY:
					data_set_list[value_index].Event_value.ev_V_SH_ON_CREATE_ANY.Handle_Type = (VDC_API::HATOMTYPE)convToLong(recv_buffer + index + 9);
					data_set_list[value_index].Event_value.ev_V_SH_ON_CREATE_ANY.Handle	= convToLLong(recv_buffer + index + 13);
					break;
				case  Data_Set::ev_SH_ON_CREATE_DELEMENT:
					data_set_list[value_index].Event_value.ev_V_SH_ON_CREATE_DELEMENT.Handle = convToLLong(recv_buffer + index + 9);
					break;
				case Data_Set::ev_SH_ON_CREATE_BASEOBJECT:
					data_set_list[value_index].Event_value.ev_V_SH_ON_CREATE_BASEOBJECT.Handle = convToLLong(recv_buffer + index + 9);
					data_set_list[value_index].Event_value.ev_V_SH_ON_CREATE_BASEOBJECT.Object_ID = convToLong(recv_buffer + index + 17);
					break;
				case Data_Set::ev_DEL_ON_DELETE:
					/* Nothing to receive */
					break;
				case Data_Set::ev_DEL_ON_VALUE_CHANGE:

					switch (data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Type)
					{
					case EVENT_VALUE::ev_long:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vlong = convToLong(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_double:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdouble = convToDouble(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_string:
						/* Add String Support */
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring = convToString(recv_buffer + index + 10, recv_buffer[index + 9]);
						break;
					case EVENT_VALUE::ev_datetime:
						/* Add String Support */
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdatetime = convToString(recv_buffer + index + 9, 24); // Date Time might have to be changed
						break;
					case EVENT_VALUE::ev_vector:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vvector = convToVector(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_matrix:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vmatrix = convToMatrix(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_handle:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vhandle = convToLLong(recv_buffer + index + 9);
						break;
					default:
						break;
					}

					break;
				case Data_Set::ev_DEL_ON_CYCLIC:
					switch (data_set_list[value_index].Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Type)
					{
					case EVENT_VALUE::ev_long:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vlong = convToLong(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_double:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vdouble = convToDouble(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_string:
						/* Add String Support */
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring = convToString(recv_buffer + index + 10, recv_buffer[index + 9]);
						break;
					case EVENT_VALUE::ev_datetime:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdatetime = convToString(recv_buffer + index + 9, 24);
						break;
					case EVENT_VALUE::ev_vector:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vvector = convToVector(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_matrix:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vmatrix = convToMatrix(recv_buffer + index + 9);
						break;
					case EVENT_VALUE::ev_handle:
						data_set_list[value_index].Event_value.ev_V_DEL_ON_CYCLIC.Data.Data_Value.ev_Vhandle = convToLLong(recv_buffer + index + 9);
						break;
					default:
						break;
					}

					break;
				case Data_Set::ev_BO_ON_DELETE:
					/* Nothing  */
					break;
				case Data_Set::ev_BO_ON_VALUE_CHANGE:
					data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Handle = convToLLong(recv_buffer + index + 9);

					switch (data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Type)
					{
					case EVENT_VALUE::ev_long:
						data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vlong = convToLong(recv_buffer + index + 17);
						ll = convToLong(recv_buffer + index + 20); std::cout << "The long iside is " << ll << std::endl;
						break;
					case EVENT_VALUE::ev_double:
						data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdouble = convToDouble(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_string:
						data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring = convToString(recv_buffer + index + 18, recv_buffer[index + 17]);
						break;
					case EVENT_VALUE::ev_datetime:
						data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdatetime = convToString(recv_buffer + index + 17, 24);//might have to be changed
						break;
					case EVENT_VALUE::ev_vector:
						data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vvector = convToVector(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_matrix:
						data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vmatrix = convToMatrix(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_handle:
						data_set_list[value_index].Event_value.ev_V_BO_ON_VALUE_CHANGE.Data.Data_Value.ev_Vhandle = convToLLong(recv_buffer + index + 17);
						break;
					default:
						break;
					}

					break;
				case Data_Set::ev_BO_ON_LINK_CHANGE:
					data_set_list[value_index].Event_value.ev_V_BO_ON_LINK_CHANGE.Handle_Child = convToLLong(recv_buffer + index + 9);
					data_set_list[value_index].Event_value.ev_V_BO_ON_LINK_CHANGE.Handle_Parent = convToLLong(recv_buffer + index + 17);
					data_set_list[value_index].Event_value.ev_V_BO_ON_LINK_CHANGE.MI = convToMatrix(recv_buffer + index + 25);
					break;
				case Data_Set::ev_BO_ON_CHILD_LINK_REMOVE:
					data_set_list[value_index].Event_value.ev_V_BO_ON_CHILD_LINK_REMOVE.Handle = convToLLong(recv_buffer + index + 9);
					break;
				case Data_Set::ev_TRN_ON_DELETE:
					/* NO DataSet */
					break;
				case Data_Set::ev_TRN_ON_MEMBER_CHANGE:
					data_set_list[value_index].Event_value.ev_V_TRN_ON_MEMBER_CHANGE.Handle_Type = (HATOMTYPE)convToLong(recv_buffer + index + 9);
					data_set_list[value_index].Event_value.ev_V_TRN_ON_MEMBER_CHANGE.Handle = convToLLong(recv_buffer + index + 13);
					break;
				case Data_Set::ev_TRN_ON_VALUE_CHANGE:
					data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Handle = convToLLong(recv_buffer + index + 9);

					switch (data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Type)
					{
					case EVENT_VALUE::ev_long:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vlong = convToLong(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_double:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdouble = convToDouble(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_string:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vstring = convToString(recv_buffer + index + 18, recv_buffer[index + 17]);
						break;
					case EVENT_VALUE::ev_datetime:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vdatetime = convToString(recv_buffer + index + 17, 24);
						break;
					case EVENT_VALUE::ev_vector:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vvector = convToVector(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_matrix:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vmatrix = convToMatrix(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_handle:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_VALUE_CHANGE.Data.Data_Value.ev_Vhandle = convToLLong(recv_buffer + index + 17);
						break;
					default:
						break;
					}

					break;
				case Data_Set::ev_TRN_ON_CYCLIC:
					data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Handle = convToLLong(recv_buffer + index + 9);

					switch (data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Type)
					{
					case EVENT_VALUE::ev_long:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vlong = convToLong(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_double:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vdouble = convToDouble(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_string:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vstring = convToString(recv_buffer + index + 18, recv_buffer[index + 17]);
						break;
					case EVENT_VALUE::ev_datetime:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vdatetime = convToString(recv_buffer + index + 17, 24);
						break;
					case EVENT_VALUE::ev_vector:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vvector = convToVector(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_matrix:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vmatrix = convToMatrix(recv_buffer + index + 17);
						break;
					case EVENT_VALUE::ev_handle:
						data_set_list[value_index].Event_value.ev_V_TRN_ON_CYCLIC.Data.Data_Value.ev_Vhandle = convToLLong(recv_buffer + index + 17);
						break;
					default:
						break;
					}
					break;
				default:
					break;
				}
			}

			if (length <= size + index + 12) { //12 = sizeof(HEVENT) + sizeof(size)
											   /* No more Event elements */
				break;
			}
			index = index + 12 + size; //Index points to the next element's hevent. 
		}

	});


	fu_global = t.get_future();

	/* Pushing the packaged task into the task queue through the mutex */
	{
		std::lock_guard<std::mutex> locker(evChannelMutex);
		evChannelTaskQueue.push_back(std::move(t));

		/* Notifying the Child Thread(TD) about the push back */
		evChannelCV.notify_one();
	}
	
	//fu_global.get();

	/* Note: Not waiting for the return from the task dispatcher as that would make this function wait for the UDP receive function. 
			 The User in the main thread must make sure to wait for the received UDP packet. 
			 The idea could be to have another passed parameter to this function for ewxample a done variable or through the already present length functions
	*/

	return true;
}


/***************************************************************************************************/
/*  TASK DISPATCHER Event Consumer  */
/***************************************************************************************************/
void VDCConsumerEventAPI::TaskDispatcher()
{
	std::packaged_task<void()> t;

	while (true)
	{
		
		{
			std::unique_lock<std::mutex> locker(evChannelMutex); // creating a unique lock with the mutex variable. 
			evChannelCV.wait(locker, [this]() { return evChannelTaskQueue.size();});  //uniquelock and a lambda function as ARGs
			{/* The code in this block will only execute when the evChannelCV is notified  */
				t = std::move(evChannelTaskQueue.front()); // t now has the task from queue
				evChannelTaskQueue.pop_front(); // removing the task from the queue
			}
		} // These brackets are important for the scope declarations 
			// The unique lock unlocks automatically when it goes out of scope (destroyed) so any code after is out of lock. 

			t(); //performing the task--> for UDP timeout settings this execution waits --> have to release the lock befor this so that queue is available. 

			/* Check if the evChannelStopFlag was set by any function */
			if (evChannelStopFlag == true)
				break; //stopping the task dispatcher.
	}
}






/*************************************************************************************************************************/
/***************************************************************************************************/
/*  VDCProducerEventAPI  */
/***************************************************************************************************/
/*************************************************************************************************************************/
VDCProducerEventAPI::VDCProducerEventAPI()
{

	/* Serialization is always Binary 2 */
	serialization = VDC_API::BINARY_2;

	/* Setting the init state of the stop thread flag */
	evChannelStopFlag = false;

	/* initializing the sequence number */
	seq_Number = 1;

}


VDCProducerEventAPI::~VDCProducerEventAPI()
{
	/* Handling the safe joining of the Child Thread with the Main thread */

	if (this->evChannelThread.joinable()) /* To make sure that if the Thread was already joined by closeConsumer this doesn't execute */
	{
		/* Setting the stop thread flag to be true */
		evChannelStopFlag = true;

		/* Creating a pseudo packaged task  */
		std::packaged_task<void()> t([]() {

			/* Other functionalities of Destructor can go here if needed to be executed in the child thread */

			return 0;
		});

		/* Creating the place holder for the return value */
		std::future<void> fu = t.get_future();

		/*
			Pushing the packaged task into the task queue through a mutex
			necessary cuz the thread waits for an element in the queue along
			with the notify _one() call
		*/
		{
			std::lock_guard<std::mutex> locker(evChannelMutex);
			evChannelTaskQueue.push_back(std::move(t));

			/* Notifying the thread about the push back */
			evChannelCV.notify_one();
		}

		/* Getting the future from the Chile Thread */
		fu.get();

		/* Joining the thread as it would have stopped through the stop Flag  */
		this->evChannelThread.join();
		std::cout << "Destructor joining the Producer" << std::endl;
	}

}


bool VDCProducerEventAPI::producer_open(const STRING& address, int port, VDC_API::LONG timeout)
{
	/*
		The return value is also a shared resource but it is handled via the packaged task
		which goes through the mutex and concition varibale hence no Race Condition
	*/
	bool fn_return = false;

	/* Starting the Thread. The TaskDispatcher function will execute in a seperate thread  */
	this->evChannelThread = std::thread(&VDCProducerEventAPI::TaskDispatcher, this);

	/*
	Creating the Packaged Task for the Given Command
	This line of code converts the lambda into a
	*/
	std::packaged_task<void()> t([&address, port, &fn_return, this]() {

		int iResult = 0;

		/* Type Conversion from String to Char */
		char const *ch_netaddr = address.c_str();

		/* Init WinSock */
		//iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		//if (iResult != 0)
		//{
		//	std::cout << "Error in WSAStartup Producer" << std::endl;
		//	fn_return = false;
		//}

		/* Initializing the ServerInfo Struct */
		server_info.sin_family = AF_INET;
		server_info.sin_port = port;
		server_info.sin_addr.S_un.S_addr = INADDR_ANY; //have to see if to use INADDR_ANY

															/* Creating the Socket */
		ConnectSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (ConnectSocket <= 0)
		{
			std::cout << "Error in creating the Producer Socket" << std::endl;
			fn_return = false;
		}

		fn_return = true;

	});

	/* Creating a placeholder for the return value from the thread. This will hold the value when get is called */
	std::future<void> fu = t.get_future();

	/* Pushing the packaged task created above to the task queue through the mutex as queue is a shared resource */
	{
		std::lock_guard<std::mutex> locker(evChannelMutex);
		evChannelTaskQueue.push_back(std::move(t));  //Pushing the task to the back of the queue

													 /* Notifying the Task Dispatcher about the task being pushed */
		evChannelCV.notify_one(); /* Now the Task Dispatcher which was waiting will start executing the task in parallel  */
	}

	/* Getting the future from the thread... This command makes the main thread to wait till the task has been executed by the dispatcher */
	fu.get();

	/*
		fn_return is a shared resource between the main thread and the child thread
		fu.get() above at the backend makes the main thread to wait till the value
		from the thread is ready. Once the value is received the program moves further
	*/

	return fn_return;
}

bool VDCProducerEventAPI::producer_close()
{
	/*
		The return value is also a shared resource but it is handled via the packaged task
		which goes through the mutex and concition varibale hence no Race Condition
	*/
	bool fn_return = false;

	/*
		Setting the stop thread flag to be true
			1) This is a Shared Resource between the main thread and the child thread (TD)
			2) In the child thread we are checking the value of this resource AFTER
				it is notified throught the CV.
	*/
	evChannelStopFlag = true;

	/*
		Creating the packaged task for the given command
		This line of code converts the lambda into a packaged task
	*/
	std::packaged_task<void()> t([&fn_return, this]() {

		closesocket(ConnectSocket);
		fn_return = true;

	});

	/* Creating a placeholder for the return value from the thread */
	std::future<void> fu = t.get_future();

	/* Pushing the packaged task into the task queue through the mutex */
	{
		std::lock_guard<std::mutex> locker(evChannelMutex);
		evChannelTaskQueue.push_back(std::move(t));

		/* Notifying the Child Thread(TD) about the push back */
		evChannelCV.notify_one();
	}

	/* Getting the future from the thread */
	fu.get();

	/*
		fn_return is a shared resource between the main thread and the child thread
		fu.get() above at the backend makes the main thread to wait till the value
		from the thread is ready. Once the value is received the program moves further
	*/

	/* Joining the thread to main as this is the last operation being called */
	if (this->evChannelThread.joinable())
	{
		this->evChannelThread.join();
		std::cout << "Producer Joined" << std::endl;
	}

	return fn_return;
}
///***************************************************************************************************/
//bool VDCProducerEventAPI::producer_write(TIMESTAMP tStamp, Data_Pair* datapair_array, VDC_API::LONG no_of_data_pairs )
//{
//	bool fn_return = false;
//
//	std::packaged_task<void()> t([tStamp, datapair_array, no_of_data_pairs, &fn_return, this]() {
//
//		VDC_API::SSHORT selector = 0x01;
//		VDC_API::SSHORT ser = VDC_API::BINARY_2;
//		VDC_API::SHORT F_ID = 0x0000;
//
//		VDC_API::LLONG seq_no = 0x0000000000000000;
//
//		VDC_API::LONG length = 24;
//
//		VDC_API::LLONG hevent = 0x0000000000000000;
//		VDC_API::SSHORT *size_array = new SSHORT[no_of_data_pairs];
//
//		for (int i = 0; i < no_of_data_pairs; i++)
//		{
//			switch (datapair_array[i].hdel_type)
//			{
//			case Data_Pair::sshort_typ:
//			{
//				size_array[i] = size_LLONG + size_SSHORT;
//			}
//			case Data_Pair::short_typ:
//			{
//				size_array[i] = size_LLONG + size_SHORT;
//			}
//			case Data_Pair::long_typ:
//			{
//				size_array[i] = size_LLONG + size_LONG;
//			}
//			case Data_Pair::llong_typ:
//			{
//				size_array[i] = size_LLONG + size_LLONG;
//			}
//			case Data_Pair::double_typ:
//			{
//				size_array[i] = size_LLONG + size_LLONG;
//			}
//			default:
//				break;
//			}
//
//			length += size_LLONG + size_array[i];
//		}
//
//
//
//		/*Sending the UDP Message*/
//		//sendto(ConnectSocket, buffer_to_send, length + 4, 0, (sockaddr*)&server_info, sizeof(server_info));
//
//	});
//
//	return fn_return;
//}
//
/***************************************************************************************************/
/*  TASK DISPATCHER Event Producer  */
/***************************************************************************************************/
void VDCProducerEventAPI::TaskDispatcher()
{
	std::packaged_task<void()> t;

	while (true)
	{
		std::unique_lock<std::mutex> locker(evChannelMutex); // creating a unique lock with the mutex variable. 
		evChannelCV.wait(locker, [this]() { return evChannelTaskQueue.size();});  //uniquelock and a lambda function as ARGs
		{/* The code in this block will only execute when the evChannelCV is notified  */
			t = std::move(evChannelTaskQueue.front()); // t now has the task from queue
			evChannelTaskQueue.pop_front(); // removing the task from the queue
			t(); //performing the task

				 /* Check if the evChannelStopFlag was set by any function */
			if (evChannelStopFlag == true)
				break; //stopping the task dispatcher.
		}
	}
}