// -------------------------------------------------------------------------------------
/**
*  File name : tcp_echoserver.c
 *
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * This file is part of and a contribution to the lwIP TCP/IP stack. *
 * Credits go to Adam Dunkels (and the current maintainers) of this software. *
 * Christiaan Simons rewrote this file to get a more stable echo example.
 *
 * This file was modified by ST 
 
 * This file was modified by Jae Il Kim, Sang Min Kim, and Donghyuk Cha
			April 15. 2016
 */ 
// -------------------------------------------------------------------------------------

// -- <1> 필요한 헤더 파일을 인클루드
//#include "main.c"
#include "main.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"

#include <stdio.h>
#include <string.h>

#if LWIP_TCP

// -- <2> 변수, 함수의 선언
static struct tcp_pcb 		*tcp_echoserver_pcb;
static struct tcp_pcb 		*tpcb_this;
u8_t								data[100];

/* ECHO protocol states */
enum tcp_echoserver_states
{
	  ES_NONE = 0,
	  ES_ACCEPTED,
	  ES_RECEIVED,
	  ES_CLOSING
};

/* structure for maintaing connection infos to be passed as argument 
   to LwIP callbacks*/
struct 	tcp_echoserver_struct
{
	  u8_t 									state;            /* current connection state */
	  struct tcp_pcb 	*pcb;			/* pointer on the current tcp_pcb */
	  struct pbuf 			*p;         		/* pointer on the received/to be transmitted pbuf */
};

// -- <3> 함수의 선언
static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void tcp_echoserver_error(void *arg, err_t err);
static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb);
static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);
static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es);

void message_send(struct tcp_pcb *tpcb2, int number);

	
// -----------------------------------------------------------------------------
// -- <4> TCP 서버를 초기화함
//	Server의 IP를 binding하고 listening을 시작
// -----------------------------------------------------------------------------
void tcp_echoserver_init(void)
{
  // -- <4-1> create new tcp pcb 
  tcp_echoserver_pcb = tcp_new();

  if (tcp_echoserver_pcb != NULL)  {
		err_t  err;

		// -- <4-2> 위에서 만든 tcp_echoserver_pcb에  접속될 client의 IP (IP_ADDR_ANY)와
		//             Port (PORT)를 bind 함
		err = tcp_bind(tcp_echoserver_pcb, IP_ADDR_ANY, PORT);
		
		if (err == ERR_OK) {
			// -- <4-3> listening(client에서 접속 요청 수신을 대기)을 시작
			  tcp_echoserver_pcb = tcp_listen(tcp_echoserver_pcb);      
			  // -- <4-4> 새로운 tcp connection이 accept될때 호출될 콜백 함수를 지정
			  tcp_accept(tcp_echoserver_pcb, tcp_echoserver_accept);
		}	
		else {
			  /* deallocate the pcb */
			  memp_free(MEMP_TCP_PCB, tcp_echoserver_pcb);
		}
  }
}

// -----------------------------------------------------------------------------
//    -- <5> 새로운 tcp connection이 accept될때 호출될 콜백 함수
//	- 이 함수는 변경할 필요없이 그대로 사용하면 된다.
//
/* param  arg 		: not used
   param   newpcb  : pointer on tcp_pcb struct for the newly created tcp connection
   param   err			: not used 
   retval    err_t		: error status
  */
// -------------------------------------------------------------------------------

static err_t tcp_echoserver_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  err_t 	ret_err;

  // --<5-1> tcp_echoserver_struct형 구조체 변수 es를 선언함
  struct tcp_echoserver_struct 	*es;

  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(err);

  /* set priority for the newly accepted tcp connection newpcb */
  tcp_setprio(newpcb, TCP_PRIO_MIN);

  /* allocate structure es to maintain tcp connection informations */
  es = (struct tcp_echoserver_struct *)mem_malloc(sizeof(struct tcp_echoserver_struct));
	
  if (es != NULL)  {
		es->state = ES_ACCEPTED;
		es->pcb = newpcb;
		es->p = NULL;
		
		/* pass newly allocated es structure as argument to newpcb */
		tcp_arg(newpcb, es);		
		
		tcp_sent(newpcb, tcp_echoserver_sent);
		
		// -- <5-2> ������ �۽��� �Ϸ�Ǹ� ȣ��� �ݹ��Լ��� ���� 
		tcp_recv(newpcb, tcp_echoserver_recv);			
		
		/* initialize lwip tcp_err callback function for newpcb  */
		tcp_err(newpcb, tcp_echoserver_error);		
		
		// -- <5-3> polling시 호출될 콜백함수를 지정
		//       - 이 콜백함수는 주기적으로 호출되어야 한다.
		//      - 이 콜백함수에서는 송신할 데이터 중에서 아직 미송신된 것이 있는지와
		//	close되어야 할 connection이 있는지를 체크한다.
		tcp_poll(newpcb, tcp_echoserver_poll, 1);
		
		/* send data */
		tcp_echoserver_send(newpcb,es);
					
		ret_err = ERR_OK;
  }
  
  else  {
		/*  close tcp connection */
		tcp_echoserver_connection_close(newpcb, es);
		/* return memory error */
		ret_err = ERR_MEM;
  }
  return ret_err; 
  
}

// -------------------------------------------------------------------------------
//  -- <6> 데이터 수신이 완료되면 호출되는 콜백 함수
/**
  * param  arg		: pointer on a argument for the tcp_pcb connection
  * param  tpcb	: pointer on the tcp_pcb connection
  * param  pbuf	: pointer on the received pbuf
  * param  err		: error information regarding the reveived pbuf
  * retval err_		t: error code
  */
// -------------------------------------------------------------------------------

static err_t   tcp_echoserver_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  struct tcp_echoserver_struct *es;	
  err_t 	ret_err;
  char 	*cp;

  LWIP_ASSERT("arg != NULL",arg != NULL);
  
  es = (struct tcp_echoserver_struct *)arg;
  
  /* if we receive an empty tcp frame from client => close connection */
  if (p == NULL)  {
		/* remote host closed connection */
		es->state = ES_CLOSING;
		if(es->p == NULL) {
			   /* we're done sending, close connection */
			   tcp_echoserver_connection_close(tpcb, es);
		}
		else {
			  /* we're not done yet */
			  /* acknowledge received packet */
			  tcp_sent(tpcb, tcp_echoserver_sent);
			  
			  /* send remaining data*/
			  tcp_echoserver_send(tpcb, es);
		}
		ret_err = ERR_OK;
  }
  
  /* else : a non empty frame was received from client but for some reason err != ERR_OK */
  else if(err != ERR_OK)  {
		/* free received pbuf*/
		if (p != NULL)	{
			  es->p = NULL;
			  pbuf_free(p);
		}
		ret_err = err;
  }
 
	// -- <6-1> 통신이 accept되고 데이터가 최초로 수신(es->state == ES_ACCEPTED)되는 경우
  else if(es->state == ES_ACCEPTED)  {	
	  
		// -- my code 추가 : ST의 orignal  소스코드에는 없는 부분임 ----

	  	  	// -- <6-1-1> 수신된 데이터(p->payload)를 cp에 대입
			cp  = p->payload;		
	  
			//  -- <6-1-2> : <L11> 형식의 데이터가 수신될 경우
			//	              -> 대응하는 LED를 On/Off 하고, 메시지를 송신함
			if(strcmp(cp,"Ba") == 0) {
				Motion_1();
				message_send(tpcb, 10);
			}			
			else if(strcmp(cp,"Bb") == 0) {
				Motion_2();
				message_send(tpcb, 11);
			}			
			else if(strcmp(cp,"Bc") == 0)	{
				Motion_3();
				message_send(tpcb, 20);
			}
			else if(strcmp(cp,"L") == 0)	{
				sw_Left();
				message_send(tpcb, 21);
			}
			else if(strcmp(cp,"R") == 0)	{
				sw_Right();
				message_send(tpcb, 30);
			}
			else if(strcmp(cp,"F") == 0)	{
				sw_Foward();
				message_send(tpcb, 31);
			}
			else if(strcmp(cp,"B") == 0)	{
				sw_Back();
				message_send(tpcb, 40);
			}
			else if(strcmp(cp,"Cc") == 0)	{
				message_send(tpcb, 41);
			}
			
			// -- <6-1-3> : <L11> 형식이 아닌 데이터가 수신될 경우
			//	- 99번 메시지를 송신함
			else {
				message_send(tpcb_this, 99);
			}				
			//   -- 끝 :  my code 추가 --
			
			
			// -- <6-1-4> ES_ACCEPTED 상태에서 필요한 처리를 함
			/* first data chunk in p->payload */
			es->state = ES_RECEIVED;		
			/* store reference to incoming pbuf (chain) */
			es->p = p;
				
			/* initialize LwIP tcp_sent callback function */
			tcp_sent(tpcb, tcp_echoserver_sent);		
			/* send back the received data (echo) */
			//tcp_echoserver_send(tpcb, es);		
			ret_err = ERR_OK;

  }   //  End of <6-1>

  
  // -- <6-2> 데이터가 수신(es->state == ES_RECEIVED)되는 경우
  else if (es->state == ES_RECEIVED)  {
		/* more data received from client and previous data has been already sent*/
		if(es->p == NULL)	{	
			
			// -- my code 추가 : ST의 orignal  소스코드에는 없는 부분임 -----

			// -- <6-2-1> 수신된 데이터(p->payload)를 cp에 대입
			cp  = p->payload; 	
			
			//  -- <6-2-2> : <L11> 형식의 데이터가 수신될 경우
			//      		- 대응하는 LED를 On/Off 하고, 메시지를 송신함
			if(strcmp(cp,"Ba") == 0) {
				Motion_1();
				message_send(tpcb, 10);
			}			
			else if(strcmp(cp,"Bb") == 0) {
				Motion_2();
				message_send(tpcb, 11);
			}			
			else if(strcmp(cp,"Bc") == 0)	{
				Motion_3();
				message_send(tpcb, 20);
			}
			else if(strcmp(cp,"L") == 0)	{
				sw_Left();
				message_send(tpcb, 21);
			}
			else if(strcmp(cp,"R") == 0)	{
				sw_Right();
				message_send(tpcb, 30);
			}
			else if(strcmp(cp,"F") == 0)	{
				sw_Foward();
				message_send(tpcb, 31);
			}
			else if(strcmp(cp,"B") == 0)	{
				sw_Back();
				message_send(tpcb, 40);
			}
			else if(strcmp(cp,"Cc") == 0)	{
				message_send(tpcb, 41);
			}			

			// -- <6-1-3> : <L11> 형식이 아닌 데이터가 수신될 경우
			// 	- 수신된 데이터를 그대로 echo back 함
			else {
				/* send back received data */
				 es->p = p;
				tcp_echoserver_send(tpcb, es);	
			}				
			//  -- 끝 :  my code 추가 --
			
			// ST예제에는 있으나 여기서는 사용하지 않음
			// 	 - 받은 데이터를 echo back 함
			// es->p = p;
			// tcp_echoserver_send(tpcb, es);
		}
		
		else	{
			  struct pbuf *ptr;
			  /* chain pbufs to the end of what we recv'ed previously  */
			  ptr = es->p;
			  pbuf_chain(ptr,p);
		}
		ret_err = ERR_OK;
  }
  
  /* data received when connection already closed */
  else  {
		/* Acknowledge data reception */
		tcp_recved(tpcb, p->tot_len);
		
		/* free pbuf and do nothing */
		es->p = NULL;
		pbuf_free(p);
		ret_err = ERR_OK;
  }
  return ret_err;
}


// -------------------------------------------------------------------------------
//  -- <7> 데이터를 송신하는 함수
//	- 이 함수는 변경할 필요없이 그대로 사용하면 된다.
/**
* brief  : This function is used to send data for tcp connection
  * param  tpcb	: pointer on the tcp_pcb connection
  * param  es		: pointer on echo_state structure
  * retval None
  */
// -------------------------------------------------------------------------------

static void tcp_echoserver_send(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  struct pbuf *ptr;
  err_t wr_err = ERR_OK;
 
  while ((wr_err == ERR_OK) &&
         (es->p != NULL) && 
         (es->p->len <= tcp_sndbuf(tpcb)))
  {
    
    /* get pointer on pbuf from es structure */
    ptr = es->p;

    /* enqueue data for transmission */
    wr_err = tcp_write(tpcb, ptr->payload, ptr->len, 1);
    
    if (wr_err == ERR_OK)
    {
      u16_t plen;

      plen = ptr->len;
     
      /* continue with next pbuf in chain (if any) */
      es->p = ptr->next;
      
      if(es->p != NULL)
      {
        /* increment reference count for es->p */
        pbuf_ref(es->p);
      }
      
      /* free pbuf: will free pbufs up to es->p (because es->p has a reference count > 0) */
      pbuf_free(ptr);

      /* Update tcp window size to be advertized : should be called when received
      data (with the amount plen) has been processed by the application layer */
      tcp_recved(tpcb, plen);
   }
   else if(wr_err == ERR_MEM)
   {
      /* we are low on memory, try later / harder, defer to poll */
     es->p = ptr;
   }
   else
   {
     /* other problem ?? */
   }
  }
}


// -------------------------------------------------------------------------
//  -- <8> 데이터 송신이 완료되면 호출되는 콜백 함수
//	- 이 함수는 변경할 필요없이 그대로 사용하면 된다.

/**
  *  This function implements the tcp_sent LwIP callback (called when ACK
  *         is received from remote host for sent data) 
  * param  	arg		: pointer on argument passed to callback
  * param  	tcp_pcb: tcp connection control block
  * param  	len		: length of data sent 
  * retval 	err_t		: returned error code
  */
// -------------------------------------------------------------------------

static err_t tcp_echoserver_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(len);

  es = (struct tcp_echoserver_struct *)arg;
  
  if(es->p != NULL)
  {
    /* still got pbufs to send */
    tcp_echoserver_send(tpcb, es);
  }
  else
  {
    /* if no more data to send and client closed connection*/
    if(es->state == ES_CLOSING)
      tcp_echoserver_connection_close(tpcb, es);
  }
  return ERR_OK;
}

// ------------------------------------------------------------
//  -- <9> 폴링시 호출되는 콜백 함수
//	- 이 함수는 변경할 필요없이 그대로 사용하면 된다.

/**
  *  This function implements the tcp_poll LwIP callback function
  * param  		arg	: pointer on argument passed to callback
  * param  		tpcb	: pointer on the tcp_pcb for the current tcp connection
  * retval 		err_t	: error code
  */
// ------------------------------------------------------------

static err_t tcp_echoserver_poll(void *arg, struct tcp_pcb *tpcb)
{
  err_t ret_err;
  struct tcp_echoserver_struct *es;

  es = (struct tcp_echoserver_struct *)arg;
  
	if (es != NULL)  {
		if (es->p != NULL)    {
			  /* there is a remaining pbuf (chain) , try to send data */
			  tcp_echoserver_send(tpcb, es);
		}
		else  {
			  /* no remaining pbuf (chain)  */
			  if(es->state == ES_CLOSING)  {
					/*  close tcp connection */
					tcp_echoserver_connection_close(tpcb, es);
			  }
		}
		ret_err = ERR_OK;
  }
  
  else  {
		/* nothing to be done */
		tcp_abort(tpcb);
		ret_err = ERR_ABRT;
  }
  return ret_err;
  
}

// -------------------------------------------------------------------------------
//  -- <10> tcp connection을 close할 때 사용하는 함수
//	- 이 함수는 변경할 필요없이 그대로 사용하면 된다.

/**
  *   This functions closes the tcp connection
  * param  	tcp_pcb	: pointer on the tcp connection
  * param  	es			: pointer on echo_state structure
  * retval 	None
  */
// -------------------------------------------------------------------------------

static void tcp_echoserver_connection_close(struct tcp_pcb *tpcb, struct tcp_echoserver_struct *es)
{
  
  /* remove all callbacks */
  tcp_arg(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);
  
  /* delete es structure */
  if (es != NULL)
  {
    mem_free(es);
  }  
  
  /* close tcp connection */
  tcp_close(tpcb);
}

// -------------------------------------------------------------------------------
//  -- <11> tcp connection error 발생시 호출되는 콜백함수
//	- 이 함수는 변경할 필요없이 그대로 사용하면 된다.

/**
  *   This function implements the tcp_err callback function (called
  *         when a fatal tcp_connection error occurs. 
  * param  	arg	: pointer on argument parameter 
  * param  	err	: not used
  * retval 	None
  */
// -------------------------------------------------------------------------------

static void tcp_echoserver_error(void *arg, err_t err)
{
  struct tcp_echoserver_struct *es;

  LWIP_UNUSED_ARG(err);

  es = (struct tcp_echoserver_struct *)arg;
  if (es != NULL)
  {
    /*  free es structure */
    mem_free(es);
  }
}

#endif /* LWIP_TCP */



// -------------------------------------------------------------------------
//  -- <12>   Nucleo-F429 보드(Server)에서  PC(Client)로 메시지를 전송하는 함수
//
// -------------------------------------------------------------------------

void message_send(struct tcp_pcb *tpcb2, int number)
{
		struct tcp_echoserver_struct *es2;	
	
		es2 = (struct tcp_echoserver_struct *)mem_malloc(sizeof(struct tcp_echoserver_struct));
		tpcb_this = tpcb2;
		
		switch(number) {
				// --  <12-1> number의 값에 따라 대응되는 메시지를 data에 저장.
				case 1 :	sprintf((char*)data, " Go Go Left!!! ");
								break;
				case 2 :	sprintf((char*)data, " Go Go Right!!! ");
								break;
				case 3 :	sprintf((char*)data, " Go Go Foward!!! ");
								break;
				case 4 :	sprintf((char*)data, " Go Go Back!!! ");
								break;
				case 10 :	sprintf((char*)data, " Box1 is Open!! ");
								break;
				case 11 :	sprintf((char*)data, " Box2 is Open!! ");
								break;
				case 20 :	sprintf((char*)data, " Box3 is Open!! ");
								break;
				case 21 :	sprintf((char*)data, " Go Go Left!!! ");
								break;
				case 30 :	sprintf((char*)data, " Go Go Right!!! ");
								break;
				case 31 :	sprintf((char*)data, " Go Go Foward!!! ");
								break;
				case 40 :	sprintf((char*)data, " Go Go Back!!! ");
								break;
				case 41 :	sprintf((char*)data, " Camera is ON!!! ");
								break;
				case 99 :	sprintf((char*)data, " Connected ! ");
								break;								
		}

		//--<12-2> 데이터를 보냄 : - server의 message_send() 와 비슷한 방법을 사용
		// 송신할 데이터를 es->p_tx (pbuf)에 넣는다	 (allocate pbuf )

		es2->p = pbuf_alloc(PBUF_TRANSPORT, strlen((char*)data) , PBUF_POOL);
		
		if (es2->p) {
					// copy data to pbuf 
					pbuf_take(es2->p, (char*)data, strlen((char*)data));
					// send data 
					tcp_echoserver_send(tpcb2, es2);
		}

}

// ------------------------------------------------------------------------
// -- <13> GPIO의 EXTI 가 발생하면 (즉, SW가 눌러지면) 호출되는 callback 함수
//
// ------------------------------------------------------------------------

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{

  // -- <13-1> 눌러지는 SW 값에 따라 message_send( ) 함수를 호출함
  if (GPIO_Pin == SW1)  {
	  sw_Left();
	  message_send(tpcb_this, 1);
  }
  if (GPIO_Pin == SW2)  {
	  	sw_Right();
		message_send(tpcb_this, 2);
  }
  if (GPIO_Pin == SW3)  {
	    sw_Foward();
		message_send(tpcb_this, 3);
  }
  if (GPIO_Pin == SW4)  {
	    sw_Back();
		message_send(tpcb_this, 4);
  }
  // -- <13-2> SW의 chattering 현상을 방지하기 위해 시간지연을 준다
  for (int i=0; i<=100000; i++) ;
 
}

// ------------------------------------------------------------------------
