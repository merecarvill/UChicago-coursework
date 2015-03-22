/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"
 
/*---------------------------------------------------------------------
 * Method: csum_verify(int)
 * Scope:  Global
 *
 * Verify that checksum in IP or ICMP header is correct
 *
 *---------------------------------------------------------------------*/
struct sr_if *iface_lookup_ip(struct sr_if *interfaces, uint32_t gw_ip) {
  while(interfaces!=NULL){
    if (gw_ip == interfaces->ip){
      return interfaces;
    }
    interfaces=interfaces->next;
  }
  return interfaces;
}

/*---------------------------------------------------------------------
 * Method: csum_verify(int)
 * Scope:  Global
 *
 * Verify that checksum in IP or ICMP header is correct
 *
 *---------------------------------------------------------------------*/

int csum_verify(void *hdr, char* hdr_type) {
  uint16_t csum;
  int out = 0;
  if (!strcmp(hdr_type, "IP")) {
    sr_ip_hdr_t *hdrcpy = (sr_ip_hdr_t*)malloc(sizeof(sr_ip_hdr_t));
    memcpy(hdrcpy, hdr, sizeof(sr_ip_hdr_t));

    csum = hdrcpy->ip_sum;
    hdrcpy->ip_sum = 0;
    if (csum == cksum(hdrcpy, sizeof(sr_ip_hdr_t))) {
      out = 1; 
    }
    free(hdrcpy);
  }
  if (!strcmp(hdr_type, "ICMP")) {
    sr_icmp_hdr_t *hdrcpy = (sr_icmp_hdr_t*)malloc(sizeof(sr_icmp_hdr_t));
    memcpy(hdrcpy, hdr, sizeof(sr_icmp_hdr_t));

    csum = hdrcpy->icmp_sum;
    hdrcpy->icmp_sum = 0;
    if (csum == cksum(hdrcpy, sizeof(sr_icmp_hdr_t))) {
      out = 1; 
    }
    free(hdrcpy);
  }
  return out;
}

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* Add initialization code here! */

} /* -- sr_init -- */

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int length,
        char* interface/* lent */)
{
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);
  printf("*** -> Received packet of length %d \n", length);

  int minlength = sizeof(sr_ethernet_hdr_t);
  if (length < minlength) {
    fprintf(stderr, "Failed to parse ETHERNET header, insufficient length\n");
    return;
  }

  print_hdr_eth(packet);

  struct sr_if *iface = sr_get_interface(sr, interface);
  struct sr_rt *rtable = sr->routing_table;
  uint16_t ethtype = ethertype(packet);
  sr_ethernet_hdr_t *eth_hdr = (sr_ethernet_hdr_t*) packet;

  if (ethtype == ethertype_ip) { /* IP */ 
    minlength += sizeof(sr_ip_hdr_t);
    if (length < minlength) {
      fprintf(stderr, "Failed to parse IP header, insufficient length\n");
      return;
    }

    uint8_t ip_proto = ip_protocol(packet + sizeof(sr_ethernet_hdr_t));
    sr_ip_hdr_t *ip_hdr =  (sr_ip_hdr_t*) (packet + sizeof(sr_ethernet_hdr_t));

    if (csum_verify(ip_hdr, "IP") == 0) {
      fprintf(stderr, "IP header checksum verification failed\n");
      return;
    }

    print_hdr_ip(packet + sizeof(sr_ethernet_hdr_t));

    struct sr_if *if_target = iface_lookup_ip(sr->if_list, ip_hdr->ip_dst);

    if (if_target == NULL) { /* non-router destination */
      ip_hdr->ip_ttl--;
      if (ip_hdr->ip_ttl==0){ /* send ICMP 11,0 if TTL == 0 */
        int size=(sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+sizeof(sr_icmp_t3_hdr_t));
        uint8_t *buf =(uint8_t *)malloc(size);
        memset(buf,0,size);
        sr_ethernet_hdr_t* ehdtogo = (sr_ethernet_hdr_t *) (buf);
        memcpy(ehdtogo->ether_shost,eth_hdr->ether_dhost,ETHER_ADDR_LEN);
        memcpy(ehdtogo->ether_dhost,eth_hdr->ether_shost,ETHER_ADDR_LEN);
        ehdtogo->ether_type=htons(ethertype_ip);

        sr_ip_hdr_t* iphdtogo = (sr_ip_hdr_t *)(buf + sizeof(sr_ethernet_hdr_t));
        iphdtogo->ip_ttl=(INIT_TTL);
        iphdtogo->ip_p=(ip_protocol_icmp);
        iphdtogo->ip_v=(4);
        iphdtogo->ip_hl=(5);
        iphdtogo->ip_len=htons(size - sizeof(sr_ethernet_hdr_t));
        iphdtogo->ip_sum=0;
        iphdtogo->ip_tos=0;
        iphdtogo->ip_id=0;
        iphdtogo->ip_dst=ip_hdr->ip_src;
        iphdtogo->ip_src=iface->ip;
        uint16_t csum=cksum(iphdtogo,sizeof(sr_ip_hdr_t));
        iphdtogo->ip_sum=csum;

        sr_icmp_t3_hdr_t *icmphdtogo=(sr_icmp_t3_hdr_t *) (buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
        icmphdtogo->icmp_type=(11);
        icmphdtogo->icmp_code=(0);
        icmphdtogo->icmp_sum=0;
        memcpy(icmphdtogo->data,packet + sizeof(sr_ethernet_hdr_t),ICMP_DATA_SIZE);
        icmphdtogo->icmp_sum=cksum(icmphdtogo,sizeof(sr_icmp_t3_hdr_t));
        printf("SENT ICMP <t11,c0>\n");
        print_hdr_eth(buf);
        print_hdr_ip(buf + sizeof(sr_ethernet_hdr_t));
        print_hdr_icmp(buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
        sr_send_packet(sr,buf,size,iface->name);
        free(buf);
        return;
      }

      ip_hdr->ip_sum = 0;
      ip_hdr->ip_sum = cksum(ip_hdr, sizeof(sr_ip_hdr_t));

      uint32_t masksz = 0;
      uint32_t mask;
      uint32_t gw_ip, table_ip = 0;
      while (rtable != NULL) {
        mask = (uint32_t) rtable->mask.s_addr;
        if (mask > masksz && (ip_hdr->ip_dst & mask) == (rtable->dest.s_addr & mask)) {
          masksz = mask;
          table_ip = rtable->dest.s_addr;
          gw_ip = rtable->gw.s_addr;
        }
        rtable = rtable->next;
      }

      if (table_ip == 0) { /* Send ICMP 3, 0 */
        int size=(sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+sizeof(sr_icmp_t3_hdr_t));
        uint8_t *buf =(uint8_t *)malloc(size);
        memset(buf,0,size);
        sr_ethernet_hdr_t* ehdtogo = (sr_ethernet_hdr_t *) (buf);
        memcpy(ehdtogo->ether_shost,eth_hdr->ether_dhost,ETHER_ADDR_LEN);
        memcpy(ehdtogo->ether_dhost,eth_hdr->ether_shost,ETHER_ADDR_LEN);
        ehdtogo->ether_type=htons(ethertype_ip);

        sr_ip_hdr_t* iphdtogo = (sr_ip_hdr_t *)(buf + sizeof(sr_ethernet_hdr_t));
        iphdtogo->ip_ttl=(INIT_TTL);
        iphdtogo->ip_p=(ip_protocol_icmp);
        iphdtogo->ip_v=(4);
        iphdtogo->ip_hl=(5);
        iphdtogo->ip_len=htons(size - sizeof(sr_ethernet_hdr_t));
        iphdtogo->ip_sum=0;
        iphdtogo->ip_tos=0;
        iphdtogo->ip_id=0;
        iphdtogo->ip_dst=ip_hdr->ip_src;
        iphdtogo->ip_src=iface->ip;
        uint16_t csum=cksum(iphdtogo,sizeof(sr_ip_hdr_t));
        iphdtogo->ip_sum=csum;

        sr_icmp_t3_hdr_t *icmphdtogo=(sr_icmp_t3_hdr_t *) (buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
        icmphdtogo->icmp_type=(3);
        icmphdtogo->icmp_code=(0);
        icmphdtogo->icmp_sum=0;
        memcpy(icmphdtogo->data,packet + sizeof(sr_ethernet_hdr_t),ICMP_DATA_SIZE);
        icmphdtogo->icmp_sum=cksum(icmphdtogo,sizeof(sr_icmp_t3_hdr_t));
        printf("SENT ICMP <t3,c0>\n");
        sr_send_packet(sr,buf,size,iface->name);
        free(buf);
        return;
      }
      else {
        struct sr_arpentry *entry = sr_arpcache_lookup(&sr->cache, htonl(table_ip));
        if (entry != NULL) { /* Send packet, dont forget to free entry */
          struct sr_if *if_out = iface_lookup_ip(sr->if_list, gw_ip);
          memcpy(eth_hdr->ether_dhost,entry->mac,ETHER_ADDR_LEN);
          memcpy(eth_hdr->ether_shost,if_out->addr,ETHER_ADDR_LEN);

          printf("Forwarded packet\n");
          sr_send_packet(sr,packet,length,if_out->name);
          free(entry);
        }
        else { /* Queue packet, send arprequest */
          struct sr_if *interfaces=sr->if_list;
          sr_arpcache_queuereq(&sr->cache,ip_hdr->ip_dst,packet,length,iface->name);
          while(interfaces){
            int size=(sizeof(sr_ethernet_hdr_t)+sizeof(sr_arp_hdr_t));
            uint8_t *buf=(uint8_t *)malloc(size);
            memset(buf,0,size);
            sr_ethernet_hdr_t* eheadtogo = (sr_ethernet_hdr_t *) (buf);
            unsigned long long dest = 0xFFFFFFFFFFFFFFFF; /* THIS IS JENKED UPPP */
            memcpy(eheadtogo->ether_shost,interfaces->addr,ETHER_ADDR_LEN);
            memcpy(eheadtogo->ether_dhost,&dest,ETHER_ADDR_LEN);
            eheadtogo->ether_type=htons(ethertype_arp);

            sr_arp_hdr_t *arphdtogo = (sr_arp_hdr_t *) (buf + sizeof(sr_ethernet_hdr_t));
            arphdtogo->ar_hrd=htons(arp_hrd_ethernet);
            arphdtogo->ar_pro=htons(ethertype_ip);
            arphdtogo->ar_hln=(ETHER_ADDR_LEN);
            arphdtogo->ar_pln= sizeof(uint32_t);
            arphdtogo->ar_op= htons(arp_op_request);
            memcpy(arphdtogo->ar_sha,eheadtogo->ether_shost,ETHER_ADDR_LEN);
            arphdtogo->ar_tip=ip_hdr->ip_dst;
            bzero(arphdtogo->ar_tha,ETHER_ADDR_LEN);
            arphdtogo->ar_sip=interfaces->ip;
            printf("Sent ARP request\n");
            print_hdr_eth(buf);
            print_hdr_arp(buf + sizeof(sr_ethernet_hdr_t));
            sr_send_packet(sr,buf,size,interfaces->name);
            free(buf);
            interfaces=interfaces->next;
          }
        }
      }
    } /* End non-router destination */
    else { /* Destination is router */
      if (ip_proto == ip_protocol_icmp) { /* ICMP */
        minlength += sizeof(sr_icmp_hdr_t);
        if (length < minlength) {
          fprintf(stderr, "Failed to parse ICMP header, insufficient length\n");
          return;
        }
        sr_icmp_hdr_t *icmp_hdr = (sr_icmp_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));

        /*if (csum_verify(icmp_hdr, "ICMP") == 0) {
          fprintf(stderr, "ICMP header checksum verification failed\n");
          return;
        } */

        print_hdr_icmp(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));

        if (icmp_hdr->icmp_type == 8) { /* Respond with ICMP 0, echo reply */
          uint8_t tmp[ETHER_ADDR_LEN];
          memcpy(tmp, eth_hdr->ether_dhost, ETHER_ADDR_LEN);
          memcpy(eth_hdr->ether_dhost,eth_hdr->ether_shost, ETHER_ADDR_LEN);
          memcpy(eth_hdr->ether_shost, tmp, ETHER_ADDR_LEN);

          uint32_t iptmp = ip_hdr->ip_dst;
          ip_hdr->ip_dst = ip_hdr->ip_src;
          ip_hdr->ip_src = iptmp;
          ip_hdr->ip_sum = 0;
          ip_hdr->ip_sum = cksum(ip_hdr, sizeof(sr_ip_hdr_t));

          sr_icmp_hdr_t *icmp_hdr = (sr_icmp_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
          icmp_hdr->icmp_code = 0;
          icmp_hdr->icmp_type = 0;
          icmp_hdr->icmp_sum = 0;
          icmp_hdr->icmp_sum = cksum(icmp_hdr, length - (sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)));

          printf("SENT ICMP <t0,c0>\n");
          print_hdr_eth(packet);
          print_hdr_ip(packet + sizeof(sr_ethernet_hdr_t));
          print_hdr_icmp(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
          sr_send_packet(sr,packet,length,iface->name);
          return;
        }
      } /* End ICMP */
      if (ip_proto == 6 || ip_proto == 17) { /* TCP/UDP - Send ICMP type 3, code 3 - port unreachable */
        int size=(sizeof(sr_ethernet_hdr_t)+sizeof(sr_ip_hdr_t)+sizeof(sr_icmp_t3_hdr_t));
        uint8_t *buf =(uint8_t *)malloc(size);
        memset(buf,0,size);
        sr_ethernet_hdr_t* ehdtogo = (sr_ethernet_hdr_t *) (buf);
        memcpy(ehdtogo->ether_shost,eth_hdr->ether_dhost,ETHER_ADDR_LEN);
        memcpy(ehdtogo->ether_dhost,eth_hdr->ether_shost,ETHER_ADDR_LEN);
        ehdtogo->ether_type=htons(ethertype_ip);

        sr_ip_hdr_t* iphdtogo = (sr_ip_hdr_t *)(buf + sizeof(sr_ethernet_hdr_t));
        iphdtogo->ip_ttl=(INIT_TTL);
        iphdtogo->ip_p=(ip_protocol_icmp);
        iphdtogo->ip_v=(4);
        iphdtogo->ip_hl=(5);
        iphdtogo->ip_len=htons(size - sizeof(sr_ethernet_hdr_t));
        iphdtogo->ip_sum=0;
        iphdtogo->ip_tos=0;
        iphdtogo->ip_id=0;
        iphdtogo->ip_dst=ip_hdr->ip_src;
        iphdtogo->ip_src=iface->ip;
        uint16_t csum=cksum(iphdtogo,sizeof(sr_ip_hdr_t));
        iphdtogo->ip_sum=csum;

        sr_icmp_t3_hdr_t *icmphdtogo=(sr_icmp_t3_hdr_t *) (buf + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
        icmphdtogo->icmp_type=(3);
        icmphdtogo->icmp_code=(3);
        icmphdtogo->icmp_sum=0;
        memcpy(icmphdtogo->data,packet + sizeof(sr_ethernet_hdr_t),ICMP_DATA_SIZE);
        icmphdtogo->icmp_sum=cksum(icmphdtogo,sizeof(sr_icmp_t3_hdr_t));
        printf("SENT ICMP <t3,c3>\n");
        sr_send_packet(sr,buf,size,iface->name);
        free(buf);
        return;
      } /* End TCP/UDP */
    } /* End destination is router */
  } /* End IP */

  else if (ethtype == ethertype_arp) { /* ARP */
    minlength += sizeof(sr_arp_hdr_t);
    if (length < minlength) {
      fprintf(stderr, "Failed to parse ARP header, insufficient length\n");
      return;
    }

    print_hdr_arp(packet + sizeof (sr_ethernet_hdr_t));

    sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t));

    if (ntohl(iface->ip) == ntohl(arp_hdr->ar_tip)) {
      if (ntohs(arp_hdr->ar_op) == arp_op_request){
        printf("In ARP, process request\n");
        int size=(sizeof(sr_ethernet_hdr_t)+sizeof(sr_arp_hdr_t));
        uint8_t *buf=(uint8_t *)malloc(size);
        memset(buf,0,size);
        sr_ethernet_hdr_t* ehdtogo = (sr_ethernet_hdr_t *) buf;
        memcpy(ehdtogo->ether_shost,iface->addr,ETHER_ADDR_LEN);
        memcpy(ehdtogo->ether_dhost,eth_hdr->ether_shost,ETHER_ADDR_LEN);
        ehdtogo->ether_type=htons(ethertype_arp);

        sr_arp_hdr_t *arphdtogo = (sr_arp_hdr_t *) (buf + sizeof(sr_ethernet_hdr_t));
        arphdtogo->ar_hrd=htons(arp_hrd_ethernet);
        arphdtogo->ar_pro=htons(ethertype_ip);
        arphdtogo->ar_hln=(ETHER_ADDR_LEN);
        arphdtogo->ar_pln=sizeof(uint32_t);
        arphdtogo->ar_op=htons(arp_op_reply);
        memcpy(arphdtogo->ar_tha,ehdtogo->ether_dhost,ETHER_ADDR_LEN);
        memcpy(arphdtogo->ar_sha,iface->addr,ETHER_ADDR_LEN);
        arphdtogo->ar_tip=arp_hdr->ar_sip;
        arphdtogo->ar_sip=iface->ip;
        printf("Sent ARP reply\n");
        print_hdr_eth(buf);
        print_hdr_arp(buf + sizeof(sr_ethernet_hdr_t));
        sr_send_packet(sr,buf,size,iface->name);
        free(buf);
      }
      if (ntohs(arp_hdr->ar_op) == arp_op_reply){
        struct sr_arpreq *cache_req=sr_arpcache_insert(&sr->cache,arp_hdr->ar_sha,arp_hdr->ar_sip);
        printf("Recvd ARP reply\n");
        if(cache_req!=NULL){
          printf("REQUEST ENTRY IS NOT NULL\n");
          struct sr_packet *packet_list=cache_req->packets;
          while(packet_list!=NULL){
            sr_ethernet_hdr_t* ehdtogo = (sr_ethernet_hdr_t *) (packet_list->buf);
            memcpy(ehdtogo->ether_dhost,arp_hdr->ar_sha,ETHER_ADDR_LEN);
            memcpy(ehdtogo->ether_shost,iface->addr,ETHER_ADDR_LEN);
            printf("Sent ARPreq queued packet\n");
            print_hdr_eth(packet_list->buf);
            print_hdr_ip(packet_list->buf + sizeof(sr_ethernet_hdr_t));
            sr_send_packet(sr,packet_list->buf,packet_list->len,iface->name);
            packet_list=packet_list->next;
          }
          sr_arpreq_destroy(&sr->cache,cache_req);
        }
      }
    }
    return;
  } /* End ARP */

  else
    fprintf(stderr, "Unrecognized Ethernet Type: %d\n", ethtype);
  return;
} /* end sr_ForwardPacket */