
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_dev.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_Pipes_Sockets.h"

/* The port table */
SCB* PORT_MAP[MAX_PORT+1]; //MAX_PORT = 1023 (tinyos.h)

Mutex socket_lock = MUTEX_INIT;  /**Mutex for the Creation of sockets*/


int socket_read(void* this, char *buf, unsigned int size)
{
	SCB* s = (SCB*)this;
	if(s->type != PEER_SOCKET)
	{
		//fprintf(stderr, "Only Peers can Read\n");
		return -1;
	}

	return pipe_read(s->PSocket->pipeReceive,buf,size);
}

int socket_write(void* this, char *buf, unsigned int size)
{
	SCB* s = (SCB*)this;
	if(s->type != PEER_SOCKET)
	{
		//fprintf(stderr, "Only Peers can Write\n");
		return -1;
	}

	return pipe_write(s->PSocket->pipeSend,buf,size);
}
int socket_close(void* this)
{
	Mutex_Lock(& socket_lock);

	SCB* s = (SCB*) this;
	if(s->type == LISTENER_SOCKET)
	{
		PORT_MAP[s->port] = 0;
		kernel_broadcast(&s->LSocket->hasRequest);

		PORT_MAP[s->port] = 0;
		free(s->LSocket);
		free(s);
		//fprintf(stderr, "CLOSE LISTENER\n" );
		Mutex_Unlock(& socket_lock);
		return 0;
	}else if(s->type == PEER_SOCKET){
		if(s->port != -1){			/**Checking if the pipes of s are already free*/
			ShutdownFCB(s->fcb,3);
		}

		free(s->PSocket);
		free(s);
		//fprintf(stderr, "CLOSE PEER\n" );
		Mutex_Unlock(& socket_lock);
		return 0;
	}else if(s->type == UNBOUND_SOCKET){
		free(s->USocket);
		free(s);
		//fprintf(stderr, "CLOSE UNBOUND_SOCKET\n" );
		Mutex_Unlock(& socket_lock);
		return 0;
	}

	fprintf(stderr, "CLOSE PROBLEM ???  (JUST CHECKING) \n" );
	return -1;
}

static file_ops socket_fops =
{
  .Open = NULL,
  .Read = socket_read,
  .Write = (void*)socket_write,  //need casting returns integer
  .Close = socket_close
};


Fid_t sys_Socket(port_t port )
{
	/** Check if it is a valid port*/
	if (port > MAX_PORT || port < 0){
		//fprintf(stderr, "NOFILE\n" );
		return NOFILE;
	}

	FCB** fcb = xmalloc(sizeof(FCB*));
	Fid_t* fid = xmalloc(sizeof(Fid_t));


	if(FCB_reserve(1,fid,fcb) == 0){
		//fprintf(stderr, "NOFILE\n" );
		return NOFILE;
	}


	//fprintf(stderr, "Socket\n" );
	SCB* s = xmalloc(sizeof(SCB));
	s->fid = fid[0];
	s->fcb = fcb[0];


	s->port = port;
	s->type = UNBOUND_SOCKET;
	s->USocket = xmalloc(sizeof(USocket));
	//fprintf(stderr, "Create Socket s-port%d\n",s->port );
	rlnode_init(& s->USocket->node,s);

	fcb[0]->streamobj = s;
	fcb[0]->streamfunc = &socket_fops;

	return fid[0];

}

int return_Null()
{
	return -1;
}

int sys_Listen(Fid_t sock)
{
	Mutex_Lock(& socket_lock);
	//fprintf(stderr, "Listener\n");

	FCB* f = get_fcb(sock);

	if(sock<0 || sock >= MAX_FILEID){
		//fprintf(stderr, "Invalid FID\n" );
		goto after_Listen;
	}
	/**Checking if socket exists*/
	if(f == NULL){
		//fprintf(stderr, "Socket doesn't exist\n" );
		goto after_Listen;
	}
	SCB* s= f->streamobj;
	/**Checking if s is a valid object*/
	if(s== NULL){
		//fprintf(stderr, "FCB has invalid streamobj\n" );
		goto after_Listen;
	}
	/**Checking if s->port is a valid port*/
	if(s->port == NOPORT){
		//fprintf(stderr, "Invalid Port\n" );
		goto after_Listen;
	}

	/**Checking if the port is bound to another Listener*/
	if(PORT_MAP[s->port] != 0  )
	{
		if (PORT_MAP[s->port]->type == LISTENER_SOCKET){
			//fprintf(stderr, "POrt is bound to another Listener\n" );
			goto after_Listen;
		}
	}
	/**Checking if this socket is already initialized*/
	if(s->type == LISTENER_SOCKET || s->type == PEER_SOCKET)
	{
		//fprintf(stderr, "Listener already initialized\n" );
		goto after_Listen;
	}

	free(s->USocket); /** freeing the old unbound socket*/

	s->LSocket = xmalloc(sizeof(LSocket));
	s->LSocket->hasRequest = COND_INIT;
	rlnode_init(& s->LSocket->ReqQueue,NULL);
	s->type = LISTENER_SOCKET;
	PORT_MAP[s->port] = s;

	Mutex_Unlock(& socket_lock);

	return 0;
	after_Listen:
	Mutex_Unlock(& socket_lock);
	return -1;
}


Fid_t sys_Accept(Fid_t lsock)
{
	//fprintf(stderr, "Accept : ");

	FCB* f = get_fcb(lsock);

	if(lsock<0 || lsock >= MAX_FILEID){
		//fprintf(stderr, "Invalid FID\n" );
		return NOFILE;
	}

	/**Checking if socket exists*/
	if(f == NULL){
		//fprintf(stderr, "Socket doesn't exist\n" );
		return NOFILE;
	}
	SCB* ls= f->streamobj;
	/**Checking if ls is a valid object*/
	if(ls == NULL){
		//fprintf(stderr, "FCB has invalid streamobj\n" );
		return NOFILE;
	}
	/**Checking if lsock is initialized*/
	if(ls->type != LISTENER_SOCKET)
	{
		//fprintf(stderr, "Unitialized Listener\n" );
		return NOFILE;
	}

	FCB** fcb = xmalloc(sizeof(FCB*));
	Fid_t* fid = xmalloc(sizeof(Fid_t));


	if(FCB_reserve(1,fid,fcb) == 0){
		//fprintf(stderr, "Maximum Fid, reached\n" );
		return NOFILE;
	}

	//fprintf(stderr, "LIstener port: %d\n",ls->port );
	SCB* u = xmalloc(sizeof(SCB));
	u->fid = fid[0];
	u->fcb = fcb[0];


	u->port = ls->port;
	u->type = UNBOUND_SOCKET;
	u->USocket = xmalloc(sizeof(USocket));

	rlnode_init(& u->USocket->node,u);

	fcb[0]->streamobj = u;
	fcb[0]->streamfunc = &socket_fops;

	if(is_rlist_empty(& ls->LSocket->ReqQueue) == 1) /** Checking if there are any pending requests*/
	{
		//fprintf(stderr, "Accept Waiting\n");
		kernel_wait(& ls->LSocket->hasRequest,SCHED_IO);
		//fprintf(stderr, "Accept WAKING UP\n");
	}

	if (ls->port == 0){
			//fprintf(stderr, "Listener Closed\n" );
		return NOFILE;
	}

	ConReq* connect_req = (ConReq*)rlist_pop_front(& ls->LSocket->ReqQueue)->obj;


	if(connect_req == NULL){
		return NOFILE;

	}

	connect_req->accepted = 1;
	SCB* connect_socket = connect_req->socket;

	connect_socket->type = PEER_SOCKET;

	free(connect_socket->USocket);

	connect_socket->PSocket = xmalloc(sizeof(PSocket));

	u->type = PEER_SOCKET;
	free(u->USocket); /** freeing the old unbound socket*/
	u->PSocket = xmalloc(sizeof(PSocket));

	u->PSocket->PeerSocket = connect_socket;
	connect_socket->PSocket->PeerSocket  = u;



	pipe_t* pipe = xmalloc(2*sizeof(pipe_t));

	Fid_t fid1[2];
	fid1[0] = fid[0];
	fid1[1] = connect_socket->fid;
	//fid1[1] = findFID(connect_socket->fcb);

	FCB* fcb1[2];
	fcb1[0] = acquire_FCB();
	fcb1[1] = acquire_FCB();

	if (fcb1[0] == NULL || fcb1[1] == NULL){
		return NOFILE;
	}

	PPCB* p1 = initialize_Pipe(&pipe[0],fid1,fcb1);

	u->PSocket->pipeSend = p1;
	connect_socket->PSocket->pipeReceive = p1;

	//fid1[0] = findFID(connect_socket->fcb);
	fid1[0] = connect_socket->fid;
	fid1[1] = fid[0];

	fcb1[0] = acquire_FCB();
	fcb1[1] = acquire_FCB();

	if (fcb1[0] == NULL || fcb1[1] == NULL){
		return NOFILE;
	}


	PPCB* p2 = initialize_Pipe(&pipe[1],fid1,fcb1);

	connect_socket->PSocket->pipeSend = p2;
	u->PSocket->pipeReceive = p2;
	kernel_broadcast(& connect_req->condition);

	return fid[0];
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	//fprintf(stderr, "Connect\n");

	FCB* f = get_fcb(sock);
	if(port<0 ||port>MAX_PORT){
		//fprintf(stderr, "Invalid PORT\n" );
		return -1;
	}

	if(sock<0 || sock >= MAX_FILEID){
		//fprintf(stderr, "Invalid FID\n" );
		return -1;
	}

	/**Checking if socket exists*/
	if(f == NULL){
		//fprintf(stderr, "Socket doesn't exist\n" );
		return -1;
	}
	SCB* s= f->streamobj;
	/**Checking if s is a valid object*/
	if(s == NULL){
		//fprintf(stderr, "FCB has invalid streamobj\n" );
		return -1;
	}

	if(PORT_MAP[port] == 0){
		//fprintf(stderr, "Unitialized Listener\n" );
		return -1;
	}

	/**Checking if lsock is initialized*/
	if(PORT_MAP[port]->type != LISTENER_SOCKET)
	{
		//fprintf(stderr, "Unitialized Listener\n" );
		return -1;
	}
	SCB* ls = PORT_MAP[port];

	ConReq* req = xmalloc(sizeof(ConReq));
	req->condition = COND_INIT;
	req->socket = s;
	req->accepted = 0;
	rlnode_init(& req->node,req);
	rlist_push_back(& ls->LSocket->ReqQueue,& req->node);

	//fprintf(stderr, "COnnect wait, \n");

	kernel_signal(& ls->LSocket->hasRequest);
	kernel_timedwait(& req->condition,SCHED_IO,timeout);

	//fprintf(stderr, "Connect wakes up\n");

	if(req->accepted == 0){

		if(rlist_remove(& req->node) == NULL){
			//fprintf(stderr, "Cant remove this node, doesnt exist\n" );
		}else {
			//fprintf(stderr, "NOde deleted succesfully\n" );
		}

		//fprintf(stderr, "Timeout, Connect Failed\n" );
		return -1;
	}

	return 0;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	//fprintf(stderr, "Shutdown");

	if(sock<0 || sock >= MAX_FILEID){
		//fprintf(stderr, "Invalid FID\n" );
		return -1;
	}

	FCB* f = get_fcb(sock);

	return ShutdownFCB(f,how);
}

int ShutdownFCB(FCB* fcb,shutdown_mode how)
{
	FCB* f = fcb;

	/**Checking if socket exists*/
	if(f == NULL){
		//fprintf(stderr, "Socket doesn't exist\n" );
		return -1;
	}
	SCB* s= f->streamobj;
	/**Checking if s is a valid object*/
	if(s == NULL){
		//fprintf(stderr, "FCB has invalid streamobj\n" );
		return -1;
	}

	if (how == 1){
		//fprintf(stderr, " Read\n" );
		s->port = -1;
		return pipe_reader_close(s->PSocket->pipeReceive);
	}else if (how == 2 ){
		//fprintf(stderr, " Write\n" );
		//s->port = -1;
		return pipe_writer_close(s->PSocket->pipeSend);
	}else if (how == 3){
		//fprintf(stderr, " BOTH\n" );
		s->port = -1;
		return pipe_writer_close(s->PSocket->pipeSend) + pipe_reader_close(s->PSocket->pipeReceive);
	}

	return -1;
}

int findFID(FCB* f)
{
	PCB* p = CURPROC;
	FCB* fcb = NULL;

	for(int i=0;i<MAX_FILEID;i++) {
		fcb = p->FIDT[i];

    	if(fcb != NULL)
    	{
      		if(fcb == f)
      		{
      			return i;
      		}

    	}
  	}
  	fprintf(stderr, "ERROR !!!!!! ON FIND FID!!!!!!\n");
  	assert(0);
  	return -1;
}
