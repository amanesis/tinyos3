#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_dev.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_Pipes_Sockets.h"

//read and write operations and the return values
//implementation of functions at the bottom

static file_ops reader_fops =
{
  .Open = NULL,
  .Read = pipe_read,
  .Close = pipe_reader_close
};


static file_ops writer_fops =
{
  .Open = NULL,
  .Read = pipe_Null,
  .Write = pipe_write,
  .Close = pipe_writer_close
};

//initialize the control-block
PPCB* initialize_Pipe(pipe_t* pipe,Fid_t* fid,FCB** fcb)
{
	PPCB* p = xmalloc(sizeof(PPCB)); //succeed or die
	p->buffer = xmalloc(SIZE_OF_BUFFER);
	p->noSpace = COND_INIT;
	p->hasNoData = COND_INIT;


	p->reader = fcb[0];
	p->writer = fcb[1];

	p->reader->streamfunc = &reader_fops;
	p->writer->streamfunc = &writer_fops;

	p->reader->streamobj = p;
	p->writer->streamobj = p;

	pipe->read = fid[0];
	pipe->write = fid[1];

	p->pipe = pipe;
	p->wP =0;
	p->rP = 0;
	return p;

}
int sys_Pipe(pipe_t* pipe)
{
	PPCB* p;

	FCB** fcb = xmalloc(2*sizeof(FCB*));
	Fid_t *fid = xmalloc(2*sizeof(Fid_t));

	if(FCB_reserve(2,fid,fcb)==0){

		return -1;
	}

	p = initialize_Pipe(pipe,fid,fcb);

	if(p ==NULL){
		return -1;
	}
	return 0;

}

int pipe_read(void* this, char *buf, unsigned int size)
{

	PPCB* p = (PPCB*) this;
	int i =0;

	while (i<size){

		if(p->reader == NULL){
			return -1;
		}


		if (p->rP == p->wP)
		{
			if(p->writer == NULL){
				return  i;
			}
			//fprintf(stderr, "Blocked : Reader\n" );
			kernel_broadcast(& p->noSpace);
			kernel_wait(& p->hasNoData,SCHED_IO);
			//fprintf(stderr, "Waking : Reader\n" );
			continue;
		}

		if (p->rP != p->wP){
			buf[i] = p->buffer[p->rP];
			//fprintf(stderr, "Reading from:%d word : %c\n",p->rP,p->buffer[p->rP] );
			p->rP ++;
			i++;
		}


		if(p->rP == SIZE_OF_BUFFER){
			p->rP = 0;
		}


	}
	kernel_broadcast(& p->noSpace);
	return i;
}

int pipe_write(void* this, const char* buf, unsigned int size)
{
	//fprintf(stderr, "TWRITE\n" );
	PPCB* p = (PPCB*) this;
	if(p == NULL)
		return -1;
	/*the Reading end of the pipe, is closed*/
	if(p->reader == NULL){
		//fprintf(stderr, "PIPE IS CLOSED, NOT WRITTABLE\n" );
		return -1;
	}

	int i =0;
	while(i<size){
		if(p->writer == NULL){
			return -1;
		}

		/*No space to write data, Block Thread*/
		if((p->wP + 1)== p->rP || ((p->wP == SIZE_OF_BUFFER -1) && p->rP == 0)){  
			//fprintf(stderr, "Blocked : WRITER\n" );
			kernel_broadcast(& p->hasNoData);
			kernel_wait(& p->noSpace,SCHED_IO);
			//fprintf(stderr, "Waking : WRITER\n" );
			continue;
		}


		p->buffer[p->wP] = buf[i];
		//fprintf(stderr, "Writing to:%d word : %c\n",p->wP,p->buffer[p->wP] );
		p->wP ++;
		i++;
		/*End of array, reset the pointer to zero*/
		if(p->wP == SIZE_OF_BUFFER){
			//fprintf(stderr, "Reset wPointer\n");
			p->wP =0;
		}


	}
	kernel_broadcast(& p->hasNoData);
	return i;
}

int pipe_reader_close(void* this)
{
	PPCB* p = (PPCB*) this;

	p->reader = NULL;
	free(p->buffer);

	return 0;
}


int pipe_writer_close(void* this)
{
	PPCB* p = (PPCB*) this;
	p->writer = NULL;

	kernel_broadcast(& p->hasNoData);


	return 0;
}


int pipe_Null()
{
	return -1;
}
