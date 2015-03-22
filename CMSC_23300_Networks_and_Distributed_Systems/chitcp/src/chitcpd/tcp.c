/*
 *  chiTCP - A simple, testable TCP stack
 *
 *  Implementation of the TCP protocol.
 *
 *  chiTCP follows a state machine approach to implementing TCP.
 *  This means that there is a handler function for each of
 *  the TCP states (CLOSED, LISTEN, SYN_RCVD, etc.). If an
 *  event (e.g., a packet arrives) while the connection is
 *  in a specific state (e.g., ESTABLISHED), then the handler
 *  function for that state is called, along with information
 *  about the event that just happened.
 *
 *  Each handler function has the following prototype:
 *
 *  int f(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event);
 *
 *  si is a pointer to the chiTCP server info. The functions in
 *       this file will not have to access the data in the server info,
 *       but this pointer is needed to call other functions.
 *
 *  entry is a pointer to the socket entry for the connection that
 *          is being handled. The socket entry contains the actual TCP
 *          data (variables, buffers, etc.), which can be extracted
 *          like this:
 *
 *            tcp_data_t *tcp_data = &entry->socket_state.active.tcp_data;
 *
 *          Other than that, no other fields in "entry" should be read
 *          or modified.
 *
 *  event is the event that has caused the TCP thread to wake up. The
 *          list of possible events corresponds roughly to the ones
 *          specified in http://tools.ietf.org/html/rfc793#section-3.9.
 *          They are:
 *
 *            APPLICATION_CONNECT: Application has called socket_connect()
 *            and a three-way handshake must be initiated.
 *
 *            APPLICATION_SEND: Application has called socket_send() and
 *            there is unsent data in the send buffer.
 *
 *            APPLICATION_RECEIVE: Application has called socket_recv() and
 *            any received-and-acked data in the recv buffer will be
 *            collected by the application (up to the maximum specified
 *            when calling socket_recv).
 *
 *            APPLICATION_CLOSE: Application has called socket_close() and
 *            a connection tear-down should be initiated.
 *
 *            PACKET_ARRIVAL: A packet has arrived through the network and
 *            needs to be processed (RFC 793 calls this "SEGMENT ARRIVES")
 *
 *            TIMEOUT: A timeout (e.g., a retransmission timeout) has
 *            happened.
 *
 */

/*
 *  Copyright (c) 2013-2014, The University of Chicago
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or withsend
 *  modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of The University of Chicago nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software withsend specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY send OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "chitcp/log.h"
#include "chitcp/buffer.h"
#include "serverinfo.h"
#include "connection.h"
#include "tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <semaphore.h>

//sem_t s;
int closing_state = 0;
int fin_state = 0;

list_t* pending_packets;

tcp_packet_t *handle_packet_arrival(chisocketentry_t *entry){
  tcp_data_t *data = &entry->socket_state.active.tcp_data;
  tcp_packet_t *packet;
  pthread_mutex_lock(&data->lock_pending_packets);
  packet = list_fetch(&data->pending_packets);
  pthread_mutex_unlock(&data->lock_pending_packets);
  chilog_tcp(CRITICAL, packet, LOG_OUTBOUND);

  return packet;
}

void send_fin(serverinfo_t *si, chisocketentry_t *entry) {
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
      
      tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
      tcphdr_t *header;
      chitcpd_tcp_packet_create(entry, new, NULL, 0);
      header = TCP_PACKET_HEADER(new);
      printf("made header\n");
      header->seq = chitcp_htonl(data->SND_NXT);
      header->ack_seq = chitcp_htonl(data->RCV_NXT);
      header->ack = (1);
      header->fin= (1);
      header->win = chitcp_htons(data->RCV_WND);
      printf("init header\n");
      chitcpd_send_tcp_packet(si, entry, new);
      chilog_tcp(CRITICAL, new, LOG_OUTBOUND);
      chitcp_tcp_packet_free(new);
  return;
}

int chitcpd_tcp_state_handle_CLOSED(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == APPLICATION_CONNECT)
    {
      //sem_init(&s, 0, 1);

      tcp_data_t *data = &entry->socket_state.active.tcp_data;
      tcp_packet_t *packet = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
      tcphdr_t *header;
      srand(837);

      chitcpd_tcp_packet_create(entry, packet, NULL, 0);
      header = TCP_PACKET_HEADER(packet);
      data->ISS = rand() % 1000;
      header->seq = chitcp_htonl(data->ISS);
      header->syn = (1);
      header->win = chitcp_htons(TCP_BUFFER_SIZE);
      data->SND_UNA = (data->ISS);
      data->SND_NXT = (data->ISS + 1);
      data->RCV_WND = (TCP_BUFFER_SIZE);
      chilog_tcp(CRITICAL, packet, LOG_OUTBOUND);       
      chitcpd_send_tcp_packet(si, entry, packet);
      chitcp_tcp_packet_free(packet);
      chitcpd_update_tcp_state(si, entry, SYN_SENT);

      return CHITCP_OK ;
    }
  else if (event == CLEANUP)
    {
      chitcpd_free_socket_entry(si, entry);
    }
  else
    chilog(WARNING, "In CLOSED state, received unexpected event.");

  return CHITCP_OK;
}

int chitcpd_tcp_state_handle_LISTEN(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == PACKET_ARRIVAL)
    {
      tcp_data_t *tcp_data = &entry->socket_state.active.tcp_data;
      tcp_packet_t *arrived = handle_packet_arrival(entry);
      tcphdr_t *ar_header;
      srand(47);

      ar_header = TCP_PACKET_HEADER(arrived);
      tcp_data->RCV_WND = (TCP_BUFFER_SIZE);
      if (ar_header->ack) {
        chilog(ERROR, "Bad ACK");
      }
      else  if(ar_header->syn) {
        tcp_data->RCV_NXT = (SEG_SEQ(arrived) + 1);
        tcp_data->IRS = (SEG_SEQ(arrived));
        tcp_data->ISS = rand() % 1000;
        tcp_data->SND_WND = SEG_WND(arrived);

        tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
        tcphdr_t *header;
        chitcpd_tcp_packet_create(entry, new, NULL, 0);
        header = TCP_PACKET_HEADER(new);
        header->seq = chitcp_htonl(tcp_data->ISS);
        header->ack_seq = chitcp_htonl(tcp_data->RCV_NXT);
        header->win = chitcp_htons(tcp_data->RCV_WND);
        header->syn = (1);
        header->ack = (1);
        tcp_data->SND_NXT = tcp_data->ISS + 1;
        tcp_data->SND_UNA = tcp_data->ISS;
        //circular_buffer_write(&tcp_data->recv, TCP_PAYLOAD_START(arrived), TCP_PAYLOAD_LEN(arrived), FALSE);
        chitcpd_send_tcp_packet(si,entry,new);

        chilog_tcp(CRITICAL, new, LOG_OUTBOUND);            
        chitcpd_update_tcp_state(si,entry, SYN_RCVD);
        chitcp_tcp_packet_free(arrived);
        chitcp_tcp_packet_free(new);

        return CHITCP_OK ;
      }
    }
  else
    chilog(WARNING, "In LISTEN state, received unexpected event.");

  return CHITCP_OK;
}

int chitcpd_tcp_state_handle_SYN_RCVD(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == PACKET_ARRIVAL) {
    tcp_data_t *tcp_data = &entry->socket_state.active.tcp_data;
    tcp_packet_t *arrived = handle_packet_arrival(entry);
    tcphdr_t *ar_header;
    ar_header = TCP_PACKET_HEADER(arrived);
    tcp_data->SND_WND = SEG_WND(arrived);

    if((SEG_SEQ(arrived) == tcp_data->RCV_NXT) ||
      ((tcp_data->RCV_NXT <= SEG_SEQ(arrived)) && (SEG_SEQ(arrived) < (tcp_data->RCV_NXT + tcp_data->RCV_WND))) ||
      ((tcp_data->RCV_NXT <= (SEG_SEQ(arrived) + SEG_LEN(arrived) - 1)) && ((SEG_SEQ(arrived) + SEG_LEN(arrived) - 1) < (tcp_data->RCV_NXT + tcp_data->RCV_WND)))) {
      if(ar_header->ack) {
        if((tcp_data->SND_UNA <= SEG_ACK(arrived)) && (SEG_ACK(arrived) <= tcp_data->SND_NXT)) {
          tcp_data->SND_UNA = tcp_data->SND_NXT;   
          chitcpd_update_tcp_state(si, entry, ESTABLISHED);

          return CHITCP_OK ;
        }
        else chilog(ERROR, "segment acknowledgement not acceptable");
      }
    }     
    else {
      chilog(ERROR,"segment not acceptable");
    }
    chitcp_tcp_packet_free(arrived);

    return CHITCP_OK ;   
  }
  else
    chilog(WARNING, "In SYN_RCVD state, received unexpected event.");

  return CHITCP_OK;
}

int chitcpd_tcp_state_handle_SYN_SENT(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event) { 
  int ack_check = 0;
  if (event == PACKET_ARRIVAL) {
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
    tcp_packet_t *arrived = handle_packet_arrival(entry);
    tcphdr_t *ar_header;
    ar_header = TCP_PACKET_HEADER(arrived);
    data->SND_WND = SEG_WND(arrived);

    if(ar_header->ack){
      if ((SEG_ACK(arrived) <= data->ISS) || (SEG_ACK(arrived) > data->SND_NXT)) {
          chilog(ERROR, "incorrect seg.ack");
          chitcp_tcp_packet_free(arrived);
          return CHITCP_OK;
      }
      if ((data->SND_UNA <= SEG_ACK(arrived)) && (SEG_ACK(arrived) <= data->SND_NXT)){
        ack_check=1;
      }
    }
    if(((ar_header->syn)&&(ack_check))||((!ar_header->ack)&&(ar_header->syn))) {
      data->RCV_NXT = (SEG_SEQ(arrived) + 1);
      data->IRS = SEG_SEQ(arrived);
      if(ar_header->ack){
        data->SND_UNA = SEG_ACK(arrived);
      }
      if (data->SND_UNA > data->ISS){
        tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
        tcphdr_t *header;
        chitcpd_tcp_packet_create(entry, new, NULL, 0);
        header = TCP_PACKET_HEADER(new);
        header->seq = chitcp_htonl(data->SND_NXT);
        header->ack_seq = chitcp_htonl(data->RCV_NXT);
        header->ack = (1);                    
        header->win = chitcp_htons(data->RCV_WND);
        chilog_tcp(CRITICAL, new, LOG_OUTBOUND);                     
        chitcpd_send_tcp_packet(si, entry, new);
        chitcpd_update_tcp_state(si, entry, ESTABLISHED);
        chitcp_tcp_packet_free(arrived);
        chitcp_tcp_packet_free(new);

        return CHITCP_OK ;
      }
      else { 
        tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
        tcphdr_t *header;
        chitcpd_tcp_packet_create(entry, new, NULL, 0);
        header = TCP_PACKET_HEADER(new);
        header->seq = chitcp_htonl(data->ISS);
        header->ack_seq = chitcp_htonl(data->RCV_NXT);
        header->syn = (1);
        header->ack = (1);
        header->win = chitcp_htons(data->RCV_WND);
        chilog_tcp(CRITICAL, new, LOG_OUTBOUND);                
        chitcpd_send_tcp_packet(si, entry, new);
        chitcpd_update_tcp_state(si, entry, SYN_RCVD);
        chitcp_tcp_packet_free(arrived);
        chitcp_tcp_packet_free(new);

        return CHITCP_OK ;
      }
    }
    else{
      chilog(ERROR,"no syn set");
      chitcp_tcp_packet_free(arrived);
      return CHITCP_OK;
    }
  } 
  else
    chilog(WARNING, "In SYN_SENT state, received unexpected event.");

  return CHITCP_OK;
}

int chitcpd_tcp_state_handle_ESTABLISHED(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == APPLICATION_SEND) {
    //*
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
    //if (circular_buffer_count(&data->send) != 0) sem_trywait(&s);
    uint8_t buf[TCP_MSS];
    uint32_t wnd = data->SND_WND;
    uint32_t payload_sz = circular_buffer_count(&data->send);
    while (wnd != 0 && payload_sz != 0) {
      //printf("WND = %u, payload_sz = %u\n", wnd, payload_sz);
      if (payload_sz > TCP_MSS)
        payload_sz = TCP_MSS;
      if (payload_sz > data->SND_WND)
        payload_sz = data->SND_WND;

      circular_buffer_read(&data->send, buf, payload_sz, FALSE);
      data->SND_NXT += payload_sz;
      
      tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
      tcphdr_t *header;
      chitcpd_tcp_packet_create(entry, new, buf, payload_sz);
      header = TCP_PACKET_HEADER(new);
      header->seq = chitcp_htonl(data->SND_NXT);
      header->ack_seq = chitcp_htonl(data->RCV_NXT);
      header->ack = (1);                    
      header->win = chitcp_htons(data->RCV_WND);

      chitcpd_send_tcp_packet(si,entry,new);
      chitcp_tcp_packet_free(new);

      wnd -= payload_sz;
      payload_sz = circular_buffer_count(&data->send);
    }
    //if (circular_buffer_count(&data->send) == 0) sem_post(&s);
    if (circular_buffer_count(&data->send) == 0 && closing_state == 1) {
      send_fin(si, entry);
      chitcpd_update_tcp_state(si, entry, LAST_ACK);
    }
    else if (circular_buffer_count(&data->send) == 0 && fin_state == 1) {
      send_fin(si, entry);
      chitcpd_update_tcp_state(si, entry, FIN_WAIT_1);
    }
    else {}
    return CHITCP_OK;
      //*/
  }
  else if (event == PACKET_ARRIVAL)
    { 
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
    tcp_packet_t *arrived = handle_packet_arrival(entry);
    tcphdr_t *ar_header;
    ar_header = TCP_PACKET_HEADER(arrived);
    if(ar_header->fin){
      //data->RCV_NXT++; 
          printf("I don' wanna!\n");
          tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
          tcphdr_t *header;
          chitcpd_tcp_packet_create(entry, new, NULL, 0);
          header = TCP_PACKET_HEADER(new);
          header->seq = chitcp_htonl(data->SND_NXT);
          header->ack_seq = chitcp_htonl(data->RCV_NXT);
          header->ack = (1);
          header->win = chitcp_htons(data->RCV_WND);
          chitcpd_send_tcp_packet(si, entry, new);
          chitcp_tcp_packet_free(new);
          chitcp_tcp_packet_free(arrived);
          chitcpd_update_tcp_state(si, entry, CLOSE_WAIT);
          circular_buffer_close(&data->recv);
          return CHITCP_OK;
    }
    if ((data->RCV_NXT <= SEG_SEQ(arrived)) && (SEG_SEQ(arrived) < (data->RCV_NXT + data->RCV_WND))) {//(((SEG_SEQ(arrived) == data->RCV_NXT)) || ((data->RCV_NXT <= SEG_SEQ(arrived)) && (SEG_SEQ(arrived) < (data->RCV_NXT + data->RCV_WND)))){
      if (!ar_header->ack) {
        chilog(ERROR,"drop segment and return");
        return CHITCP_OK;
      }
      if (SEG_ACK(arrived) < data->SND_UNA) {
        printf("in duplicate conditional");
        chilog(ERROR,"Ignore duplicate packet");
        chitcp_tcp_packet_free(arrived);
        return CHITCP_OK ;
      }
      if ((data->SND_UNA < SEG_ACK(arrived)) && (SEG_ACK(arrived) <= data->SND_NXT)) {
        data->SND_UNA = SEG_ACK(arrived);
      }
     
      if(SEG_ACK(arrived) > data->SND_NXT){
        chilog(ERROR,"acked something not yet sent");
          return CHITCP_OK ;
        }
        else {
          if(TCP_PAYLOAD_LEN(arrived)==0){
            chitcp_tcp_packet_free(arrived);
            return CHITCP_OK;
          }
          printf("in %d\n", 2);
          data->SND_WND = SEG_WND(arrived);
          tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
          tcphdr_t *header;
          chitcpd_tcp_packet_create(entry, new, NULL, 0);
          header = TCP_PACKET_HEADER(new);
          header->seq = chitcp_htonl(data->SND_NXT);
          header->ack_seq = chitcp_htonl(data->RCV_NXT);
          header->ack = (1);
          header->win = chitcp_htons(data->RCV_WND);

          circular_buffer_write(&data->recv, TCP_PAYLOAD_START(arrived), TCP_PAYLOAD_LEN(arrived),TRUE);
          data->RCV_NXT += TCP_PAYLOAD_LEN(arrived);
          data->RCV_WND = circular_buffer_available(&data->recv);
          chilog_tcp(CRITICAL, new, LOG_OUTBOUND);                
          chitcpd_send_tcp_packet(si, entry, new);
          chitcp_tcp_packet_free(arrived);
          chitcp_tcp_packet_free(new);
        }
      }
      else {
        chilog(ERROR,"segment not acceptable");
      }
    }
  else if (event == APPLICATION_RECEIVE)
    {
      tcp_data_t *data = &entry->socket_state.active.tcp_data;
      data->RCV_WND = circular_buffer_available(&data->recv);
    }
  else if (event == APPLICATION_CLOSE)
    {
      tcp_data_t *data = &entry->socket_state.active.tcp_data;
      printf("Application close in established\n");
      //sem_wait(&s);
      if (circular_buffer_count(&data->send) == 0) {
        printf("circular buffer empty\n");
        send_fin(si, entry);
        chitcpd_update_tcp_state(si, entry, FIN_WAIT_1);

      }
      else {
        fin_state = 1;
      }
      return CHITCP_OK;
      /*
      circular_buffer_close(&data->recv);
      circular_buffer_close(&data->send);
      //*/
    }
  else
    chilog(WARNING, "In ESTABLISHED state, received unexpected event (%i).", event);

  return CHITCP_OK;
}


int chitcpd_tcp_state_handle_FIN_WAIT_1(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == PACKET_ARRIVAL)
    {
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
    tcp_packet_t *arrived = handle_packet_arrival(entry);
    tcphdr_t *ar_header;
    ar_header = TCP_PACKET_HEADER(arrived);
    data->SND_WND = SEG_WND(arrived); 
    if(ar_header->fin){
      printf("aaaaaaaaa fin\n");
          //data->RCV_NXT++; 
          tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
          tcphdr_t *header;
          chitcpd_tcp_packet_create(entry, new, NULL, 0);
          header = TCP_PACKET_HEADER(new);
          header->seq = chitcp_htonl(data->SND_NXT);
          header->ack_seq = chitcp_htonl(data->RCV_NXT);
          header->ack = (1);
          chilog_tcp(CRITICAL, new, LOG_OUTBOUND);                
          chitcpd_send_tcp_packet(si, entry, new);
          chitcp_tcp_packet_free(new);
          chitcp_tcp_packet_free(arrived);
            chitcpd_update_tcp_state(si,entry,CLOSING);
            return CHITCP_OK;
          }
    if (((SEG_SEQ(arrived)==data->RCV_NXT)) || ((data->RCV_NXT<=SEG_SEQ(arrived)) && (SEG_SEQ(arrived)<(data->RCV_NXT+data->RCV_WND)))){
      printf("aaaaaaaaa 2\n");
      if (!ar_header->ack){
        chilog(ERROR,"drop segment and return");
        return CHITCP_OK;
      }
      if ((data->SND_UNA <= SEG_ACK(arrived)) && (SEG_ACK(arrived)<=data->SND_NXT)){
        data->SND_UNA=SEG_ACK(arrived);
        if (SEG_ACK(arrived)<data->SND_UNA){
          chilog(ERROR,"Ignore duplicate packet");
          chitcp_tcp_packet_free(arrived);
          return CHITCP_OK;
        }
        if(SEG_ACK(arrived) > data->SND_NXT){
         chilog(ERROR,"acked something not yet sent");
          return CHITCP_OK ;
        }
        printf("aaaaaaaaa 1\n");
          tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
          tcphdr_t *header;
          chitcpd_tcp_packet_create(entry, new, NULL, 0);
          header = TCP_PACKET_HEADER(new);
          header->seq = chitcp_htonl(data->SND_NXT);
          header->ack_seq = chitcp_htonl(data->RCV_NXT);
          header->ack = (1);
          chilog_tcp(CRITICAL, new, LOG_OUTBOUND);                
          chitcpd_send_tcp_packet(si, entry, new);
           chitcp_tcp_packet_free(new);
          chitcp_tcp_packet_free(arrived);
          chitcpd_update_tcp_state(si, entry,FIN_WAIT_2);
          return CHITCP_OK;
        }
       
        }
        
        
    else {
     chilog(ERROR,"segment not acceptable");
      return CHITCP_OK ;
     }
   }
  else
    chilog(WARNING, "In FIN_WAIT_1 state, received unexpected event (%i).", event);

  return CHITCP_OK;
}


int chitcpd_tcp_state_handle_FIN_WAIT_2(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == PACKET_ARRIVAL)
    {
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
    tcp_packet_t *arrived = handle_packet_arrival(entry);
    tcphdr_t *ar_header;
    ar_header = TCP_PACKET_HEADER(arrived);
    data->SND_WND=SEG_WND(arrived);
    if (((SEG_SEQ(arrived)==data->RCV_NXT)) || ((data->RCV_NXT<=SEG_SEQ(arrived)) && (SEG_SEQ(arrived)<(data->RCV_NXT+data->RCV_WND)))){
      if (!ar_header->ack){
        chilog(ERROR,"drop segment and return");
        return CHITCP_OK;
      }
      if ((data->SND_UNA < SEG_ACK(arrived)) && (SEG_ACK(arrived)<=data->SND_NXT)){
        data->SND_UNA=SEG_ACK(arrived);
        if (SEG_ACK(arrived)<data->SND_UNA){
          chilog(ERROR,"Ignore duplicate packet");
          chitcp_tcp_packet_free(arrived);
          return CHITCP_OK ;
        }
        chitcp_tcp_packet_free(arrived);
        chitcpd_update_tcp_state(si, entry,CLOSED);
        //chitcpd_free_socket_entry(si, entry); 
        return CHITCP_OK;
      }
       if(SEG_ACK(arrived) > data->SND_NXT){
          tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
          tcphdr_t *header;
          header = TCP_PACKET_HEADER(new);
          header->seq = data->SND_NXT;
          header->ack_seq = data->RCV_NXT;
          header->ack = (1);
          chitcpd_send_tcp_packet(si, entry, new);
          chilog_tcp(CRITICAL, new, LOG_OUTBOUND);
          chitcp_tcp_packet_free(arrived);
          chitcp_tcp_packet_free(new);
          return CHITCP_OK ;
        }
        else {
          data->SND_WND = SEG_WND(arrived);
          tcp_packet_t *new = (tcp_packet_t *)malloc(sizeof(tcp_packet_t));
          tcphdr_t *header;
          chitcpd_tcp_packet_create(entry, new, NULL, 0);
          header = TCP_PACKET_HEADER(new);
          header->seq = chitcp_htonl(data->SND_NXT);
          header->ack_seq = chitcp_htonl(data->RCV_NXT);
          header->ack = (1);
          circular_buffer_write(&data->recv, TCP_PAYLOAD_START(arrived), TCP_PAYLOAD_LEN(arrived),FALSE);
          data->RCV_NXT += TCP_PAYLOAD_LEN(arrived);
          data->RCV_WND = circular_buffer_available(&data->recv);
          chilog_tcp(CRITICAL, new, LOG_OUTBOUND);                
          chitcpd_send_tcp_packet(si, entry, new);
          chitcp_tcp_packet_free(arrived);
          chitcp_tcp_packet_free(new);
        }
        
      }
    else{
      chilog(ERROR,"segment not acceptable");
    }
  }
    
  else
    chilog(WARNING, "In FIN_WAIT_2 state, received unexpected event (%i).", event);

  return CHITCP_OK;
}


int chitcpd_tcp_state_handle_CLOSE_WAIT(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == APPLICATION_CLOSE)
    {
      printf("in close_wait application close\n");
      //sem_wait(&s);

      tcp_data_t *data = &entry->socket_state.active.tcp_data;
      if (circular_buffer_count(&data->send) == 0) {
        send_fin(si, entry);
        chitcpd_update_tcp_state(si, entry, LAST_ACK);
      }
      else {
        closing_state = 1;
      }
      return CHITCP_OK ;
    }
  else if (event == PACKET_ARRIVAL)
    {
    printf("in close wait pcket arrival\n");
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
    tcp_packet_t *arrived = handle_packet_arrival(entry);
    tcphdr_t *ar_header;
    ar_header = TCP_PACKET_HEADER(arrived);
    data->SND_WND=SEG_WND(arrived); 
    /*
    if(ar_header->fin){
          //circular_buffer_close(&data->recv);
          printf("I don' wanna nananananna!\n");
          send_fin(si,entry);
          chitcpd_update_tcp_state(si, entry, LAST_ACK);
          return CHITCP_OK;
    } */
    if (((SEG_SEQ(arrived)==data->RCV_NXT)) || ((data->RCV_NXT<=SEG_SEQ(arrived)) && (SEG_SEQ(arrived)<(data->RCV_NXT+data->RCV_WND)))){
      if (!ar_header->ack) {
        chitcp_tcp_packet_free(arrived);
        chilog(ERROR,"drop segment and return");
         return CHITCP_OK;
      }
      if ((data->SND_UNA<=SEG_ACK(arrived))&&(SEG_ACK(arrived)<=data->SND_NXT)){
        data->SND_UNA=SEG_ACK(arrived);
        if (SEG_ACK(arrived)<data->SND_UNA){
          chilog(ERROR,"Ignore duplicate packet");
          chitcp_tcp_packet_free(arrived);
          return CHITCP_OK;
        }
        if(SEG_ACK(arrived) > data->SND_NXT){
          chilog(ERROR,"acked something not yet sent");
          return CHITCP_OK;
        }
        else {
          chitcp_tcp_packet_free(arrived);
          return CHITCP_OK;
        }
      }
    }
    else {
     chilog(ERROR,"segment not acceptable");
    }
  }
  else
    chilog(WARNING, "In CLOSE_WAIT state, received unexpected event (%i).", event);


  return CHITCP_OK;
}


int chitcpd_tcp_state_handle_CLOSING(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == PACKET_ARRIVAL)
    {
      tcp_data_t *data = &entry->socket_state.active.tcp_data;
      tcp_packet_t *arrived = handle_packet_arrival(entry);
      tcphdr_t *ar_header;
      ar_header = TCP_PACKET_HEADER(arrived);
      data->SND_WND=SEG_WND(arrived);
      if (((SEG_SEQ(arrived)==data->RCV_NXT)) || ((data->RCV_NXT<=SEG_SEQ(arrived)) && (SEG_SEQ(arrived)<(data->RCV_NXT+data->RCV_WND)))){
      if (!ar_header->ack){
        chitcp_tcp_packet_free(arrived);
        chilog(ERROR,"drop segment and return");
        return CHITCP_OK;
      }
      if ((data->SND_UNA<=SEG_ACK(arrived))&&(SEG_ACK(arrived)<=data->SND_NXT)){
        data->SND_UNA=SEG_ACK(arrived);
        if (SEG_ACK(arrived)<data->SND_UNA){
          chilog(ERROR,"Ignore duplicate packet");
          chitcp_tcp_packet_free(arrived);
          return CHITCP_OK ;
        }
        chitcpd_update_tcp_state(si, entry,TIME_WAIT);
        //Transition to CLOSE_WAIT because TIME_WAIT is not supported, need to double check this
        }
        chitcp_tcp_packet_free(arrived);
        return CHITCP_OK ;
  
      }
    else {
      chilog(ERROR,"segment not acceptabe");
     }
   }
  
  else
    chilog(WARNING, "In CLOSING state, received unexpected event (%i).", event);

  return CHITCP_OK;
}


int chitcpd_tcp_state_handle_TIME_WAIT(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  chitcpd_update_tcp_state(si, entry,CLOSED);
  chilog(WARNING, "Running handler for TIME_WAIT. This should not happen.");

  return CHITCP_OK;
}


int chitcpd_tcp_state_handle_LAST_ACK(serverinfo_t *si, chisocketentry_t *entry, tcp_event_type_t event)
{
  if (event == PACKET_ARRIVAL)
    {
    tcp_data_t *data = &entry->socket_state.active.tcp_data;
    tcp_packet_t *arrived = handle_packet_arrival(entry);
    tcphdr_t *ar_header;
    ar_header = TCP_PACKET_HEADER(arrived);
    data->SND_WND=SEG_WND(arrived);
    printf("soooo meal~\n");
    if (((SEG_SEQ(arrived)==data->RCV_NXT)) || ((data->RCV_NXT<=SEG_SEQ(arrived)) && (SEG_SEQ(arrived)<(data->RCV_NXT+data->RCV_WND)))){
      if(!ar_header->ack){
        chitcp_tcp_packet_free(arrived);
        chilog(ERROR,"drop segment and return");
        return CHITCP_OK;
      }
      printf("soooo meaningfull~\n");
      if((data->SND_UNA<=SEG_ACK(arrived))&&(SEG_ACK(arrived)<=data->SND_NXT)){
        printf("soooo ful of meanings~\n");
        chitcpd_update_tcp_state(si, entry,CLOSED);
        chitcp_tcp_packet_free(arrived);
        //chitcpd_free_socket_entry(si, entry); 
        return CHITCP_OK;
      }
    }
    else{
      chilog(ERROR,"segment not acceptable");
    }
  }
  else
    chilog(WARNING, "In LAST_ACK state, received unexpected event (%i).", event);

  return CHITCP_OK;
}

/*                                                           */
/*     Any additional functions you need should go here      */
/*                                                           */

