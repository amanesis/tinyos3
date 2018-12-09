#ifndef __KERNEL_PIPEANDSOCKETS_H
#define __KERNEL_PIPEANDSOCKETS_H


#define SIZE_OF_BUFFER 8192 //buffer size 8kb

/*
PIPES
DECLARATIONS
*/
int pipe_read(void* this, char *buf, unsigned int size); //return pipe's size(i), -1: smth going wrong
int pipe_reader_close(void* this); //return 0
int pipe_write(void* this, const char* buf, unsigned int size);// -1: pipe is closed, return size(i)
int pipe_writer_close(void* this); //return 0
int pipe_Null(); //return -1

typedef struct Pipe_control_block{
	char* buffer ;
	CondVar noSpace;   //need it for checks
	CondVar hasNoData; //need it for checks
	FCB * reader;
	FCB * writer;
	pipe_t* pipe;
	int wP;			//the position of writer pointer
	int rP; 		// the position of reader pointer


}PPCB;

PPCB* initialize_Pipe(pipe_t* pipe,Fid_t* fid,FCB** fcb); //initialization

// -------- SOCKETS -----------

typedef struct Socket_Control_Block SCB;

int return_Null(); //return -1
int socket_close(void* this); //return 0 (or -1: close problem)
//only PEERS can read or write
int socket_read(void* this, char *buf, unsigned int size); // return -1 when !=PEER_SOCKET
int socket_write(void* this, char *buf, unsigned int size); // return -1 when !=PEER_SOCKET
int findFID(FCB* f); // return -1: ERROR FIND FID, else return: FID
int ShutdownFCB(FCB* fcb,shutdown_mode how); //return -1: socket doesnt exit or FCB->invalid streamobj
																							// else return pipe_reader_close or
																							// pipe_writer_close
																							// or both.
/*
SOCKETS
DECLARATIONS
*/

typedef struct Unbound_Socket
{
	rlnode node;
}USocket;

typedef struct Listener_Socket
{
	CondVar hasRequest;
	rlnode ReqQueue;
}LSocket;

typedef struct PEER_SOCKET{
	SCB* PeerSocket;
	PPCB* pipeSend;
	PPCB* pipeReceive;
}PSocket;


typedef enum{
	UNBOUND_SOCKET,
	LISTENER_SOCKET,
	PEER_SOCKET
}Socket_type;

typedef struct Socket_Control_Block
{
	int fid;
	FCB* fcb;
	port_t port;
	Socket_type type;
	union{
		USocket* USocket;
		LSocket* LSocket;
		PSocket* PSocket;
	};
}SCB;

typedef struct Connection_Request
{
	CondVar condition;
	SCB* socket;
	int accepted;
	rlnode node;
}ConReq;



#endif
