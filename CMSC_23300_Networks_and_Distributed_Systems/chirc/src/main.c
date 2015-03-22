#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#define HANDLER_ENTRY(NAME) { #NAME, handle_ ## NAME}

/*linked list to go in main channel_list struct to hold user socket and mode*/
typedef struct Channel_users channel_users;
struct Channel_users {
  int user_socket;
  int md_voice;
  int md_coper;
  channel_users *next;
};

/*Linked list struct for list of all available channels, includes channel name, topic, active users, and list of channel_users struct*/
typedef struct Channel_list channel_list;
struct Channel_list {
  char *channel;
  char *topic;
  int active;
  int md_moder;
  int md_topic;
  channel_users *users;
  channel_list *next;
};

typedef struct Client_channels client_channels;
struct Client_channels {
  char* channel;
  client_channels* next;
};

/* A user struct to store information about connected users. Will add values as necessary. */
typedef struct User user;
struct User {
  char *nick;
  char *username;
  char *fullname;
  char *away;
  int clientID;
  int md_oper;
  int registered;
  client_channels* channels;
  user *next;
};


struct workerArgs
{
  int socket;
};

typedef int (*handler_function)(char** ps, int clientSocket);

struct handler_entry
{
  char* name;
  handler_function func;
};

int num_channels=0;
/*used to store time server was created*/
char s_time[32];
/*mutex lock for list of users*/
pthread_mutex_t lock;
/*mutex lock for list of channels*/
pthread_mutex_t chlock;
/*beginning of user list*/
pthread_mutex_t mes;
user *head = NULL;
/*beginning of channel list*/
channel_list *channels_head=NULL;
char* password = "";

/*function to be run by client threads*/
void* client_commands(void* args);


user* ID_find(int clientSocket) {
  pthread_mutex_lock(&lock);
  user* user = head;
  while (user != NULL) {
    if (user->clientID == clientSocket) {
      pthread_mutex_unlock(&lock);
      return user;
    }
    else {
      user = user->next;
    }
  }
  pthread_mutex_unlock(&lock);
  return user;
}

void channelVis(channel_list *chan){
  while(chan!=NULL){
    if (chan->channel !=NULL) printf ("channel <%s>\n",chan->channel);
    if (chan->topic!=NULL) printf ("channel %s topic is: <%s>\n",chan->channel,chan->topic);
    if (chan->active!=0) printf ("channel %s active is: <%d>\n",chan->channel,chan->active);
    printf("channel %s md_moder: <%d>\n",chan->channel,chan->md_moder);
    printf("channel %s md_topic: <%d>\n",chan->channel,chan->md_topic);
    channel_users * tmp=chan->users;
    user *client;
    while(tmp!=NULL){
      client=ID_find(tmp->user_socket);
      printf("%s-%d ",client->nick,tmp->user_socket);
      tmp=tmp->next;
    }
  printf("\n");
  chan=chan->next;
  }
  return;
}


client_channels *client_channels_init() {
  client_channels *new = (client_channels *)malloc(sizeof(client_channels));
  new->channel = NULL;
  new->next = NULL;
  return new;
}

void client_channels_remove(user* usr, char* name) {
  client_channels* currc = usr->channels;
  client_channels* prevc = NULL;
  while (currc != NULL) {
    if (!strcmp(currc->channel, name)) {
      if (prevc == NULL){
        usr->channels = usr->channels->next;
      }
      else prevc->next = currc->next;
      
    }
    prevc=currc;
    currc=currc->next;
  }
      free(currc);
  return;
}

/* Returns a user struct, properly initialized in memory */
user *userInit(int id) {
  user *usr = (user *)malloc(sizeof(user));
  usr->nick = NULL;
  usr->username = NULL;
  usr->fullname = NULL;
  usr->clientID = id;
  usr->registered = 0;
  usr->md_oper = 0;
  usr->next = NULL;
  usr->away = NULL;
  usr->channels = NULL;
  return usr;
}


channel_users *channel_users_init() {
  channel_users *new = (channel_users *)malloc(sizeof(channel_users));
  new->user_socket = 0;
  new->md_voice = 0;
  new->md_coper = 0;
  new->next = NULL;
  return new;
}

void channel_users_free(channel_users* cusers) {
  channel_users* tmp;
  while (cusers != NULL) {
    tmp = cusers->next;
    free(cusers);
    cusers = tmp;
  }
}




/*initialize of a new node of channel_list linked list*/
channel_list *channel_list_init() {
  channel_list *new = (channel_list *)malloc(sizeof(channel_list));
  new->channel = NULL;
  new->topic = NULL;
  new->active = 0;
  new->md_topic = 0;
  new->md_moder = 0;
  new->users = NULL;
  new->next = NULL;
  return new;
}

/* Frees a user struct from memory */
void userFree(user* usr) {
  user* temp;
  client_channels *tmp;
  while(usr != NULL) {  
    temp = usr->next;
    free(usr->nick);
    free(usr->username);
    free(usr->fullname);
    free(usr->away);
    while(usr->channels != NULL){
      tmp = usr->channels->next;
      free(usr->channels->channel);
      free(usr->channels);
      usr->channels=tmp;
    }
    free(usr);
    usr = temp;
  }
}

void userVis(user* usr) {
  while (usr != NULL) {
    if (usr->nick != NULL) printf("user%d nick: <%s>\n", usr->clientID, usr->nick);
    if (usr->username != NULL) printf("user%d username: <%s>\n", usr->clientID, usr->username);
    if (usr->fullname != NULL) printf("user%d fullname: <%s>\n", usr->clientID, usr->fullname);
    if (usr->away != NULL) printf("user%d away: <%s>\n", usr->clientID, usr->away);
    printf("user%d operator: <%d>\n", usr->clientID, usr->md_oper);
    printf("user%d registered: <%d>\n", usr->clientID, usr->registered);
    printf("user%d channels:\n", usr->clientID);
    client_channels* tmp = usr->channels;
    while (tmp != NULL) {
      printf("%s ", tmp->channel);
      tmp = tmp->next;
    }
    printf("\n");
    usr = usr->next;
  }
  return;
}


char** psInit() {
  char** ps = (char**)malloc(16*sizeof(char*));
  int i;
  for (i = 0; i < 16; i++) {
    ps[i] = NULL;
  }
  return ps;
}

int ps_count(char**ps){
  int i = 0;
  int count = 0;
  while(ps[i] != NULL){
    i++;
    count++;
  }
  return count;
}

void psFree(char** ps) {
  int i = 0;
  while (ps[i] != NULL) {
    free(ps[i++]);
  }
  free(ps);
}



void s_send (char* msg, int clientSocket) {
  pthread_mutex_lock(&mes);
  user* usr = ID_find(clientSocket);
  printf("sending to %s: %s", usr->nick, msg);
  if (send(clientSocket, msg, strlen(msg), 0) <= 0)
    {
      perror("Socket send() failed");
      close(clientSocket);
      pthread_mutex_unlock(&mes);
      pthread_exit(NULL);
    }
  pthread_mutex_unlock(&mes);
  return;
}

void s_gethostname (char* serverhostname, int size) {
  if(gethostname(serverhostname,size*sizeof(char)) == -1) {
    perror("Host could not be resolved");
    pthread_exit(NULL);
  }
  return;
}

void s_getpeername (char* clienthostname, int size, int clientSocket) {
  struct sockaddr_in clientAddr;
  socklen_t sinSize = sizeof(clientAddr);
  if(getpeername(clientSocket,(struct sockaddr *) &clientAddr, &sinSize) != 0) {
    perror("Could not resolve client host");
    pthread_exit(NULL);
  }
  if(inet_ntop(AF_INET, &clientAddr.sin_addr, clienthostname, size*sizeof(char)) == 0) {
    perror("Address not valid");
    pthread_exit(NULL);
  }
  return;
}

channel_list* channel_find(char* name) {
  pthread_mutex_lock(&chlock);
  channel_list* chans = channels_head;
  while (chans != NULL) {
    if (chans->channel != NULL) {
      if (!(strcmp(chans->channel, name))) {
        pthread_mutex_unlock(&chlock);
        return chans;
      }
    }
    chans = chans->next;
  }
  pthread_mutex_unlock(&chlock);
  return chans;
}

channel_users* channel_users_find(channel_users* users, int id) {
  pthread_mutex_lock(&chlock);
  while (users != NULL) {
    if (users->user_socket == id) {
      pthread_mutex_unlock(&chlock);
      return users;
    }
    users = users->next;
  }
  pthread_mutex_unlock(&chlock);
  return users;
}

int in_client_channels(char* name, client_channels* chans) {
  pthread_mutex_lock(&chlock);
  while (chans != NULL) {
    if (chans->channel != NULL) {
      if (!(strcmp(chans->channel, name))) {
        pthread_mutex_unlock(&chlock);
        return 1;
      }
    }
    chans = chans->next;
  }
  pthread_mutex_unlock(&chlock);
  return 0;
}

user* Nick_find(char* nick) {
  pthread_mutex_lock(&lock);
  user* user = head;
  while (user != NULL) {
    if (user->nick != NULL) {
      if (!strcmp(user->nick, nick)) {
pthread_mutex_unlock(&lock);
return user;
      }
    }
    user = user->next;

  }
  pthread_mutex_unlock(&lock);
  return user;
}



void user_add(user* usr){
  pthread_mutex_lock(&lock);
  user* user = head;
  if (user != NULL) {
    while (user->next != NULL) {
      user = user->next;
    }
    user->next = usr;
  }
  else {
    head = usr;
  }
  pthread_mutex_unlock(&lock);
  return;
}

void channel_add(channel_list *channel) {
  pthread_mutex_lock(&chlock);
  channel_list *head = channels_head;
  if (head != NULL) {
    while (head->next != NULL) {
      head = head->next;
    }
    head->next = channel;
  }
  else {
    channels_head = channel;
  }
  pthread_mutex_unlock(&chlock);
  return;
}

void channel_user_add(channel_users *channel, channel_users *User) {
  pthread_mutex_lock(&chlock);
  if(channel==NULL){
    channel=User;
    pthread_mutex_unlock(&chlock);
    return;
  }

  channel_users *tmp=channel;
  channel_users *iter=NULL;
  while (channel!= NULL) {
    iter=channel;
    channel=channel->next;
  }
  iter->next=User;
  channel=tmp;

  pthread_mutex_unlock(&chlock);
  return;

}

void client_channels_add(user *client, client_channels *channel){
  pthread_mutex_lock(&lock);
  client_channels *user = client->channels;
  if (user != NULL){
    while (user->next != NULL){
      user = user->next;
    }
    user->next = channel;
  }
  else {
    client->channels = channel;
  }
  pthread_mutex_unlock(&lock);
  return;
}

void errCmd(char* cmd, int clientSocket) {
  user* new = ID_find(clientSocket);
  if (new == NULL) return;
  char msg[512];
  char hostname[64];
  s_gethostname(hostname, 64);
  if (new->nick != NULL) {
    snprintf(msg, sizeof(msg), ":%s 421 %s %s :Unknown command\r\n", hostname, new->nick, cmd);
    s_send(msg, clientSocket);
  }
  else {
    snprintf(msg, sizeof(msg), ":%s 421 * %s :Unknown command\r\n", hostname, cmd);
    s_send(msg, clientSocket);
  }
  return;
}

void errParam(char* cmd, int clientSocket) {
  user* new = ID_find(clientSocket);
  if (new == NULL) return;
  char msg[512];
  char hostname[64];
  s_gethostname(hostname, 64);
  if (new->nick != NULL) {
    snprintf(msg, sizeof(msg), ":%s 461 %s %s :Not enough parameters\r\n", hostname, new->nick, cmd);
    s_send(msg, clientSocket);
  }
  else {
    snprintf(msg, sizeof(msg), ":%s 461 * %s :Not enough parameters\r\n", hostname, cmd);
    s_send(msg, clientSocket);
  }
  return;
}

int handle_LUSERS(char **ps, int clientSocket) {
  user* client = ID_find(clientSocket);
  char* Nick = client->nick;
  char server[64];
  int total = 0;
  int reg = 0;
  user* front = head;
  s_gethostname(server, 64);
  while(front != NULL){
    if(front->registered){
      reg++;
    }
    total++;
    front = front->next;
  }
  char msg[512];

  snprintf(msg, sizeof(msg), ":%s 251 %s :There are %d users and 0 services on 1 servers\r\n", server, Nick, reg);
  s_send(msg, clientSocket);
  snprintf(msg, sizeof(msg), ":%s 252 %s 0 :operator(s) online\r\n", server, Nick);
  s_send(msg, clientSocket);
  snprintf(msg, sizeof(msg), ":%s 253 %s %d :unknown connection(s)\r\n", server, Nick, (total - reg));
  s_send(msg, clientSocket);
  snprintf(msg, sizeof(msg), ":%s 254 %s %d :channels formed\r\n", server, Nick, num_channels);
  s_send(msg,clientSocket);
  snprintf(msg, sizeof(msg), ":%s 255 %s :I have %d clients and 1 servers\r\n", server, Nick, total);
  s_send(msg, clientSocket);
  return 0;
}

int handle_MOTD (char** ps, int clientSocket) {
  user*find=ID_find(clientSocket);
  char msg[512];
  FILE* fp;
  char serverhostname[64];
  s_gethostname(serverhostname, 64);
  if ((fp = fopen("motd.txt", "r")) == NULL) {
    snprintf(msg, sizeof(msg), ":%s 422 %s :MOTD File is missing\r\n", serverhostname,find->nick);
    s_send(msg, clientSocket);
    return 0;
  }
  char text[100];
  int i, len;
  snprintf(msg, sizeof(msg), ":%s 375 %s :- %s Message of the day - \r\n", serverhostname,find->nick, serverhostname);
  s_send(msg, clientSocket);
  while (fgets(text, sizeof(text), fp) != NULL) {
    len = strlen(text);
    for (i = 0; i < len; i++) {
      if (text[i] == '\n') text[i] = '\0';
    }
    snprintf(msg, sizeof(msg), ":%s 372 %s :- %s\r\n", serverhostname,find->nick, text);
    s_send(msg, clientSocket);
  }
  fclose(fp);
  snprintf(msg, sizeof(msg), ":%s 376 %s :End of MOTD command\r\n", serverhostname,find->nick);
  s_send(msg, clientSocket);
  return 0;
}

/* Called once conditions are appropriate for the welcome message to be sent (nick and username established). Assembles necessary info, creates a well-formed welcome message, and sends it to a connected client. */
void sendWelcome(int clientSocket, user *usr) {
  char serverhostname[64];
  char clienthostname[64];
  char msg[512];

  s_gethostname(serverhostname, 64);
  s_getpeername(clienthostname, 64, clientSocket);
  
  snprintf(msg, sizeof(msg), ":%s 001 %s :Welcome to the Internet Relay Network %s!%s@%s\r\n", serverhostname, usr->nick, usr->nick, usr->username, clienthostname);
  s_send(msg, clientSocket);
  snprintf(msg, sizeof(msg), ":%s 002 %s :Your host is %s, running version chirc-0.1\r\n", serverhostname, usr->nick, serverhostname);
  s_send(msg, clientSocket);
  snprintf(msg, sizeof(msg), ":%s 003 %s :This server was created %s\r\n", serverhostname, usr->nick, s_time);
  s_send(msg, clientSocket);
  snprintf(msg, sizeof(msg), ":%s 004 %s %s chirc-0.1 ao mtov\r\n", serverhostname, usr->nick, serverhostname);
  s_send(msg, clientSocket);
  
  handle_LUSERS(NULL, clientSocket);
  handle_MOTD(NULL, clientSocket);

  return;
}

int handle_NICK(char** ps, int clientSocket) {
  if(ps_count(ps) != 1) {
    errParam("NICK",clientSocket);
    return 0;
  }
  char msg[512];
  user* find = Nick_find(ps[0]);
  user* new = ID_find(clientSocket);
  char serverhostname[64];
  s_gethostname(serverhostname, 64);
  if ((find != NULL) && (find->clientID != clientSocket)) {
    if (new->nick != NULL) {
      snprintf(msg, sizeof(msg), ":%s 433 %s %s :Nickname is already in use\r\n", serverhostname, new->nick, ps[0]);
    }
    else {
      snprintf(msg, sizeof(msg), ":%s 433 * %s :Nickname is already in use\r\n", serverhostname, ps[0]);
    }
    s_send(msg, clientSocket);
  }
  else {
    if (new != NULL) {
      if (new->username != NULL && new->nick != NULL) {
        char *prev_nick = new->nick;
        new->nick = strdup(ps[0]);
        client_channels *cchan = new->channels;
        while(cchan != NULL) {
          channel_list *chan = channel_find(cchan->channel);
          channel_users *temp = chan->users;
          snprintf(msg, sizeof(msg), ":%s!%s@%s NICK :%s\r\n", prev_nick, new->username, serverhostname, new->nick);
          while (temp != NULL){
            //if(temp->user_socket != clientSocket){
            s_send(msg, temp->user_socket);
            //}
            temp = temp->next;
          }
          cchan=cchan->next;
        }
      }
      else {
        if (new->username != NULL && new->nick == NULL) {
          new->nick = strdup(ps[0]);
          new->registered = 1;
          sendWelcome(clientSocket, new);
        }
        else {
          new->nick = strdup(ps[0]);
        }
      }
    }
  }
  return 0;
}

int handle_USER(char** ps, int clientSocket) {
  user* new = ID_find(clientSocket);
  char msg[512];
  char hostname[64];
  s_gethostname(hostname, 64);
  if (ps_count(ps) != 4){
    errParam("USER",clientSocket);
    return 0;
  }
  
  if (new->username != NULL) {
    snprintf(msg, sizeof(msg), ":%s 462 :Unauthorized command (already registered)\r\n", hostname);
    s_send(msg, clientSocket);
  }
  else {
    if ((new->nick) && (!new->username)) {
      new->username = ps[0];
      new->fullname = ps[3];
      new->registered = 1;
      sendWelcome(clientSocket, new);
    }
    
    else {
      new->username = ps[0];
      new->fullname = ps[3];
    }
  }
  return 0;
}

void channel_list_free(channel_list *tbf){
  while(tbf != NULL) {
    channel_list *tmp = tbf->next;
    free(tbf->channel);
    free(tbf->topic);
    channel_users_free(tbf->users);
    free(tbf);
    tbf = tmp;
  }
  return;
}

void channel_users_remove(channel_list* chan, int id) {
  pthread_mutex_lock(&chlock);
  channel_users* prevc = NULL;
  channel_users* currc = chan->users;
  while (currc != NULL) {
    if (currc->user_socket == id) {
      if (prevc == NULL) chan->users = currc->next;
      else prevc->next = currc->next;
      if (chan->active!=0)
      //channel_users_free(currc);
      chan->active -= 1;
      pthread_mutex_unlock(&chlock);
      return;
    }
    prevc = currc;
    currc = currc->next;
  }
  pthread_mutex_unlock(&chlock);
  return;
}

void channel_list_remove(char* name) {
  pthread_mutex_lock(&chlock);
  channel_list* prevc = NULL;
  channel_list* currc = channels_head;
  while (currc != NULL) {
    if (!strcmp(currc->channel, name)) {
      if (prevc == NULL) channels_head = currc->next;
      else prevc->next = currc->next;
      channel_list_free(currc);
      pthread_mutex_unlock(&chlock);
      return;
    }
    prevc = currc;
    currc = currc->next;
  }
  pthread_mutex_unlock(&chlock);
  return;
}

int handle_QUIT (char** ps, int clientSocket) {
  user* usr = ID_find(clientSocket);
  char msg[512];
  char hostname[64];
  s_gethostname(hostname, 64);
  snprintf(msg, sizeof(msg), "ERROR :Closing Link: %s (%s)\r\n", hostname, ps[0]);
  s_send(msg, clientSocket);
  close(clientSocket);
  channel_list* chan;
  channel_users* cuser;
  client_channels* cchan;
  char* quit_msg;
  user* prev = NULL;
  user* curr = head;
  while (curr != NULL) {
    if (curr->clientID == clientSocket) {
      cchan = curr->channels;
      while (cchan != NULL) {
        chan = channel_find(cchan->channel);
        channel_users_remove(chan, clientSocket);
        cuser = chan->users;
        while (cuser != NULL) {
          if (ps[0] == NULL) quit_msg = usr->nick;
          else quit_msg = ps[0];
          snprintf(msg, sizeof(msg), ":%s!%s@%s QUIT :%s\r\n", usr->nick, usr->username, hostname, quit_msg);
          s_send(msg, cuser->user_socket);
          cuser = cuser->next;
        }
        cchan = cchan->next;
      }
      if (prev == NULL) head = curr->next;
      else prev->next = curr->next;
      userFree(curr);
      chan = channels_head;
      while (chan != NULL) {
        if (chan->active == 0) {
          channel_list_remove(chan->channel);
        }
        chan = chan->next;
      }
      pthread_exit(NULL);
    }
    prev = curr;
    curr = curr->next;
  }
  return 1;
}

int handle_PRIVMSG(char **ps,int clientSocket) {
  user* find = Nick_find(ps[0]);
  channel_list *cfind = channel_find(ps[0]);
  channel_users* cuser;
  user* sender = ID_find(clientSocket);
  char serverhostname[64];
  char msg[512];
  s_gethostname(serverhostname, 64);
  int msg_perm = 1;
  if(find != NULL) {
    snprintf(msg, sizeof(msg), ":%s!%s@%s PRIVMSG %s :%s\r\n", sender->nick, sender->username, serverhostname, ps[0], ps[1]);
    s_send(msg, find->clientID);
    if (find->away != NULL) {
      snprintf(msg, sizeof(msg), ":%s 301 %s %s :%s\r\n", serverhostname, sender->nick, find->nick, find->away);
      s_send(msg, clientSocket);
    }
  }
  else if (cfind != NULL) {
    cuser = channel_users_find(cfind->users, sender->clientID);
    if (cfind->md_moder == 1 && cuser->md_voice != 1 && cuser->md_coper != 1 && sender->md_oper != 1) {
      msg_perm = 0;
    }
    if (in_client_channels(ps[0], sender->channels) && msg_perm == 1) {
      snprintf(msg, sizeof(msg), ":%s!%s@%s PRIVMSG %s :%s\r\n", sender->nick, sender->username, serverhostname, ps[0], ps[1]);
      channel_users* recip = cfind->users;
      while (recip != NULL) {
        if (!(recip->user_socket==clientSocket)){
        s_send(msg, recip->user_socket);
      }
        recip = recip->next;
      }
    }
    else {
      snprintf(msg, sizeof(msg), ":%s 404 %s %s :Cannot send to channel\r\n", serverhostname, sender->nick, ps[0]);
      s_send(msg, clientSocket);
    }
  }
  else {
    snprintf(msg, sizeof(msg), ":%s 401 %s %s :No such nick/channel\r\n", serverhostname, sender->nick, ps[0]);
    s_send(msg, clientSocket);
  }
  return 0;
}
      
int handle_NOTICE(char **ps, int clientSocket) {
  user* sender = ID_find(clientSocket);
  char serverhostname[64];
  s_gethostname(serverhostname, 64);
  int msg_perm = 1;
  user* find = Nick_find(ps[0]);
  channel_list *cfind = channel_find(ps[0]);
  channel_users* cuser;
  char msg[512];
  snprintf(msg, sizeof(msg), ":%s!%s@%s NOTICE %s :%s\r\n", sender->nick, sender->username, serverhostname, ps[0], ps[1]);
  if (find != NULL) {
    s_send(msg, find->clientID);
    return 0;
  }
  if (cfind != NULL) {
    cuser = channel_users_find(cfind->users, sender->clientID);
    if (cfind->md_moder == 1 && cuser->md_voice != 1 && cuser->md_coper != 1 && sender->md_oper != 1) {
      msg_perm = 0;
    }
    if (in_client_channels(ps[0], sender->channels) && msg_perm == 1) {
      channel_users* recip = cfind->users;
      while (recip != NULL) {
        s_send(msg, recip->user_socket);
        recip = recip->next;
      }
      return 0;
    }
  }
  return 1;
}
 
int handle_WHOIS(char **ps,int clientSocket) {
  user* find = Nick_find(ps[0]);
  user* me = ID_find(clientSocket);
  char server[64];
  char client[64];
  char msg[512];
  s_gethostname(server, 64);
  if (find != NULL) {
    s_getpeername(client, 64, find->clientID);
    snprintf(msg, sizeof(msg), ":%s 311 %s %s ~%s %s * :%s\r\n", server, me->nick, find->nick, find->username, client, find->fullname);
    s_send(msg, clientSocket);
    client_channels *chans = find->channels;
    while(chans != NULL){
      channel_list *search = channel_find(chans->channel);
      channel_users *user = search->users;
      user = channel_users_find(user, clientSocket);
      char mode[10];
      snprintf(mode,sizeof(mode),"%s","*");
      if (user->md_coper == 1){
        snprintf(mode,sizeof(mode),"%s","@");
      }
      if(user->md_voice == 1){
        snprintf(mode,sizeof(mode),"%s","+");
      }
      snprintf(msg, sizeof(msg), ":%s 319 %s %s :%s%s \r\n", server, me->nick, find->nick, mode, chans->channel);
      s_send(msg, clientSocket);
      chans=chans->next;
    }
    snprintf(msg, sizeof(msg), ":%s 312 %s %s %s:Chicago, IL\r\n", server, me->nick, ps[0], server);
    s_send(msg, clientSocket);
    if (find->away != NULL){
      snprintf(msg, sizeof(msg), ":%s 301 %s %s :%s\r\n", server, me->nick, find->nick, find->away);
      s_send(msg, clientSocket);
    }
    if (find->md_oper==1){
      snprintf(msg, sizeof(msg), ":%s 313 %s %s :is an IRC operator\r\n", server, me->nick, find->nick);
      s_send(msg, clientSocket);
    }
    snprintf(msg, sizeof(msg), ":%s 318 %s %s :End of WHOIS list\r\n", server, me->nick, ps[0]);
    s_send(msg, clientSocket);
  }
  else {
    snprintf(msg, sizeof(msg), ":%s 401 %s %s :No such nick/channel\r\n", server, me->nick, ps[0]);
    s_send(msg, clientSocket);
  }
  return 0;
}


int handle_PONG(char **ps,int clientSocket) {
  return 0;
}

int handle_PING(char **ps,int clientSocket) {
  char server[64];
  s_gethostname(server, 64);
  char msg[512];
  snprintf(msg, sizeof(msg), ":%s PONG %s\r\n", server, server);
  s_send(msg, clientSocket);
  return 0;
}

char* channel_names(channel_list* chan) {
  char nbuf[512];
  nbuf[0] = '\0';
  channel_users *cuser;
  user* usr;
  strcat(nbuf, "= ");
  strcat(nbuf, chan->channel);
  strcat(nbuf, " :");
  cuser = chan->users;
  while (cuser != NULL) {
    if (cuser->md_coper == 1) strcat(nbuf, "@");
    if (cuser->md_voice == 1) strcat(nbuf, "+");
    usr = ID_find(cuser->user_socket);
    strcat(nbuf, usr->nick);
    strcat(nbuf, " ");
    cuser = cuser->next;
  }
  nbuf[strlen(nbuf) - 1] = '\0';
  char* names = strdup(nbuf);
  return names;
}

int handle_JOIN(char **ps, int clientSocket) {
  user *client = ID_find(clientSocket);
  char msg[512];
  char server[64];
  s_gethostname(server, 64);
  char* names;
  //if (client->nick == NULL || client->username == NULL) {
  // snprintf(msg, sizeof(msg), ":%s 451 %s :You have not registered\r\n", server, client->nick);
  //}
  channel_list *find = channel_find(ps[0]);
  if (find == NULL) {
    num_channels++;
    channel_list *new = channel_list_init();
    channel_users *nuser = channel_users_init();
    nuser->user_socket = clientSocket;
    nuser->md_coper = 1;
    new->channel = strdup(ps[0]);
    new->active = 1;
    new->users = nuser;
    channel_add(new);
    client_channels *mem = client_channels_init();
    mem->channel = strdup(ps[0]);
    client_channels_add(client, mem);
    snprintf(msg, sizeof(msg), ":%s!%s@%s JOIN %s\r\n",client->nick,client->username, server, ps[0]);
    s_send(msg, clientSocket);
    names = channel_names(new);
    snprintf(msg, sizeof(msg),":%s 353 %s %s\r\n",server,client->nick,names);
    free(names);
    s_send(msg,clientSocket);
    snprintf(msg, sizeof(msg), ":%s 366 %s %s :End of NAMES list\r\n", server, client->nick, ps[0]);
    s_send(msg, clientSocket);
  }
  else if (channel_users_find(find->users, clientSocket) != NULL) {
    //Do nothing?
  }
  else {
    channel_users *new = channel_users_init();
    new->user_socket = clientSocket;
    find->active += 1;
    channel_user_add(find->users, new);
    client_channels *mem = client_channels_init();
    mem->channel = strdup(ps[0]);
    client_channels_add(client, mem);
    snprintf(msg, sizeof(msg),":%s!%s@%s JOIN %s\r\n",client->nick,client->username,server,ps[0]);
    channel_list *foo=(channel_find(ps[0]));
    channel_users *chan=foo->users;
    while(chan!=NULL){
    s_send(msg,chan->user_socket);
    chan=chan->next;
  }
    if (find->topic != NULL) {
      snprintf(msg, sizeof(msg), ":%s 332 %s %s :%s\r\n", server, client->nick, ps[0], find->topic);
      s_send(msg , clientSocket);
    }
    names = channel_names(find);
    snprintf(msg , sizeof(msg), ":%s 353 %s %s\r\n", server, client->nick, names);
    free(names);
    s_send(msg, clientSocket);
    snprintf(msg, sizeof(msg), ":%s 366 %s %s :End of NAMES list\r\n", server, client->nick, ps[0]);
    s_send(msg, clientSocket);
  }
  return 0;
}

int handle_PART(char **ps, int clientSocket) {
  char server[64];
  s_gethostname(server, 64);
  char msg[512];
  user *client = ID_find(clientSocket);
  channel_list *find = channel_find(ps[0]);
  if (find == NULL) {
    snprintf(msg, sizeof(msg), ":%s 403 %s %s :No such channel\r\n", server, client->nick, ps[0]);
    s_send(msg, clientSocket);
    return 0;
  }
  if (!in_client_channels(ps[0], client->channels)) {
    snprintf(msg, sizeof(msg), ":%s 442 %s %s :You're not on that channel\r\n", server, client->nick, ps[0]);
    s_send(msg, clientSocket);
    return 0;
  }
  pthread_mutex_lock(&chlock);
  client_channels_remove(client, find->channel);
  channel_users* cuser = find->users;
  while (cuser != NULL) {
    if (ps[1]==NULL){
      snprintf(msg, sizeof(msg), ":%s!%s@%s PART %s\r\n", client->nick, client->username, server, ps[0]);
    }
    if (ps[1]!=NULL){
      snprintf(msg, sizeof(msg), ":%s!%s@%s PART %s :%s\r\n", client->nick, client->username, server, ps[0], ps[1]);
    }
    s_send(msg, cuser->user_socket);
    cuser = cuser->next;
  }
  pthread_mutex_unlock(&chlock);
  channel_users_remove(find, clientSocket);
  if (find->active == 0) {
    channel_list_remove(find->channel);
  }
  return 0;
}

int handle_TOPIC(char **ps, int clientSocket) {
  char server[64];
  s_gethostname(server, 64);
  char msg[512];
  user *client = ID_find(clientSocket);
  channel_list *find = channel_find(ps[0]);
  if (find == NULL || !in_client_channels(ps[0], client->channels)) {
    snprintf(msg, sizeof(msg), ":%s 442 %s %s :You're not on that channel\r\n", server, client->nick, ps[0]);
    s_send(msg, clientSocket);
    return 0;
  }
  if (ps[1] == NULL) {
    if (find->topic != NULL) {
      snprintf(msg, sizeof(msg), ":%s 332 %s %s :%s\r\n", server, client->nick, ps[0], find->topic);
      s_send(msg, clientSocket);
      return 0;
    }
    else {
      snprintf(msg, sizeof(msg), ":%s 331 %s %s :No topic is set\r\n", server, client->nick, ps[0]);
      s_send(msg, clientSocket);
      return 0;
    }
  }
  channel_users* cuser = channel_users_find(find->users, client->clientID);
  if (find->md_topic == 1 && cuser->md_coper != 1 && client->md_oper != 1) {
    snprintf(msg, sizeof(msg), ":%s 482 %s %s :You're not channel operator\r\n", server, client->nick, ps[0]);
    s_send(msg, clientSocket);
    return 0;
  }
  else {
    if (!strcmp(ps[1], "")) {
      free(find->topic);
      find->topic = NULL;
      return 0;
    }
    else {
      find->topic = strdup(ps[1]);
      snprintf(msg, sizeof(msg), ":%s!%s@%s TOPIC %s :%s\r\n", client->nick,client->username,server, ps[0], find->topic);
      channel_users * temp=find->users;
      while(temp!=NULL){
       s_send(msg,temp->user_socket);
       temp=temp->next;
      }
    }
  return 0;
  }
  return 1;
}

int handle_OPER(char **ps, int clientSocket) {
  if (ps_count(ps) != 2) {
    errParam("OPER", clientSocket);
    return 1;
  }
  char server[64];
  s_gethostname(server, 64);
  char msg[512];
  user *client = ID_find(clientSocket);
  if (!strcmp(ps[1], password)) {
    pthread_mutex_lock(&lock);
    client->md_oper = 1;
    pthread_mutex_unlock(&lock);
    snprintf(msg, sizeof(msg), ":%s 381 %s :You are now an IRC operator\r\n", server, client->nick);
    s_send(msg, clientSocket);
    return 0;
  }
  else {
    snprintf(msg, sizeof(msg), ":%s 464 %s :Password incorrect\r\n", server, client->nick);
    s_send(msg, clientSocket);
    return 0;
  }
}

int handle_MODE(char **ps, int clientSocket) {
  char server[64];
  s_gethostname(server, 64);
  char msg[512];
  user *client = ID_find(clientSocket);
  channel_list *find = channel_find(ps[0]);
  int ct = ps_count(ps);
  if (ct <= 0 || ct >= 4) {
    errParam("MODE", clientSocket);
    return 0;
  }
  if (ct == 1) {
    if (find != NULL) {
      char mode_str[4];
      int i = 0;
      mode_str[i++] = '+';
      if (find->md_moder == 1) mode_str[i++] = 'm';
      if (find->md_topic == 1) mode_str[i++] = 't';
      mode_str[i] = '\0';
      snprintf(msg, sizeof(msg), ":%s 324 %s %s %s\r\n", server, client->nick, ps[0], mode_str);
      s_send(msg, clientSocket);
    }
    else {
      snprintf(msg, sizeof(msg), ":%s 403 %s %s :No such channel\r\n", server, client->nick, ps[0]);
      s_send(msg, clientSocket);
    }
    return 0;
  }
  if (ct == 2) {
    if (!strcmp(client->nick, ps[0])) {
      if (!strcmp(ps[1], "-o")) {
        pthread_mutex_lock(&lock);
        client->md_oper = 0;
        pthread_mutex_unlock(&lock);
        snprintf(msg, sizeof(msg), ":%s MODE %s :%s\r\n", client->nick, client->nick, ps[1]);
        s_send(msg, clientSocket);
      }
      else if (!strcmp(ps[1], "+o") || !strcmp(ps[1], "+a") || !strcmp(ps[1], "-a")) {
      }
      else {
        snprintf(msg, sizeof(msg), ":%s 501 %s :Unknown MODE flag\r\n", server, client->nick);
        s_send(msg, clientSocket);
      }
    }
    else if (find != NULL) {
      channel_users* cuser = channel_users_find(find->users, clientSocket);
      if (cuser != NULL && (cuser->md_coper == 1 || client->md_oper == 1)) {
        int new_val;
        if (ps[1][0] == '+') new_val = 1;
        else if (ps[1][0] == '-') new_val = 0;
        else {
          snprintf(msg, sizeof(msg), ":%s 472 %s %c :is unknown mode char to me for %s\r\n", server, client->nick, ps[1][0], ps[0]);
          s_send(msg, clientSocket);
          return 0;
        }
        if (ps[1][1] == 'm') find->md_moder = new_val;
        else if (ps[1][1] == 't') find->md_topic = new_val;
        else {
          snprintf(msg, sizeof(msg), ":%s 472 %s %c :is unknown mode char to me for %s\r\n", server, client->nick, ps[1][1], ps[0]);
          s_send(msg, clientSocket);
          return 0;
        }
        channel_users* eachuser = find->users;
        while (eachuser != NULL) {
          snprintf(msg, sizeof(msg), ":%s!%s@%s MODE %s %s\r\n", client->nick,client->username,server, ps[0], ps[1]);
          s_send(msg, eachuser->user_socket);
          eachuser = eachuser->next;
        }
      }
      else {
        snprintf(msg, sizeof(msg), ":%s 482 %s %s :You're not channel operator\r\n", server, client->nick, ps[0]);
        s_send(msg, clientSocket);
      }
    }
    else if (ps[0][0] == '#') {
      snprintf(msg, sizeof(msg), ":%s 403 %s %s :No such channel\r\n", server, client->nick, ps[0]);
      s_send(msg, clientSocket);
    }
    else {
      snprintf(msg, sizeof(msg), ":%s 502 %s :Cannot change mode for other users\r\n", server, client->nick);
      s_send(msg, clientSocket);
    }
    return 0;
  }
  if (ct == 3) {
    if (find != NULL) {
      channel_users* cuser = channel_users_find(find->users, clientSocket);
      user *target = Nick_find(ps[2]);
      channel_users* tuser;
      if (cuser != NULL && (cuser->md_coper == 1 || client->md_oper == 1)) {
        if (target != NULL && (tuser = channel_users_find(find->users, target->clientID)) != NULL) {
          int new_val;
          if (ps[1][0] == '+') new_val = 1;
          else if (ps[1][0] == '-') new_val = 0;
          else {
            snprintf(msg, sizeof(msg), ":%s 472 %s %c :is unknown mode char to me for %s\r\n", server, client->nick, ps[1][0], ps[0]);
            s_send(msg, clientSocket);
            return 0;
          }
          if (ps[1][1] == 'o') tuser->md_coper = new_val;
          else if (ps[1][1] == 'v') tuser->md_voice = new_val;
          else {
            snprintf(msg, sizeof(msg), ":%s 472 %s %c :is unknown mode char to me for %s\r\n", server, client->nick, ps[1][1], ps[0]);
            s_send(msg, clientSocket);
            return 0;
          }
          channel_users* eachuser = find->users;
          while (eachuser != NULL) {
            snprintf(msg, sizeof(msg), ":%s!%s@%s MODE %s %s %s\r\n", client->nick,client->username,server, ps[0], ps[1], ps[2]);
            s_send(msg, eachuser->user_socket);
            eachuser = eachuser->next;
          }
        }
        else {
          snprintf(msg, sizeof(msg), ":%s 441 %s %s %s :They aren't on that channel\r\n", server, client->nick, ps[2], ps[0]);
          s_send(msg, clientSocket);
        }
      }
      else {
        snprintf(msg, sizeof(msg), ":%s 482 %s %s :You're not channel operator\r\n", server, client->nick, ps[0]);
        s_send(msg, clientSocket);
      }
    }
    else {
      snprintf(msg, sizeof(msg), ":%s 403 %s %s :No such channel\r\n", server, client->nick, ps[0]);
      s_send(msg, clientSocket);
    }
    return 0;
  }
  return 1;
}

char *users_no_channels(){
  pthread_mutex_lock(&lock);
  char nbuf[512];
  nbuf[0] = '\0';
  strcat(nbuf, "* * :");
  user *usr = head;
  char* names;
  while (usr != NULL) {
    if (usr->channels == NULL) {
      strcat(nbuf, usr->nick);
      strcat(nbuf, " ");
    }
    usr = usr->next;
  }
  nbuf[strlen(nbuf) - 1] = '\0';
  pthread_mutex_unlock(&lock);
  return names = strdup(nbuf);
}

int handle_NAMES(char **ps, int clientSocket) {
  user *client = ID_find(clientSocket);
  char server[64];
  s_gethostname(server,64);
  char msg[512];
  char* names;
  int ct = ps_count(ps);
  if (ct > 1) {
    errParam("NAMES", clientSocket);
    return 0;
  }
  channel_list *chan;
  char* cless_names;
  if (ct == 0) {
    chan = channels_head;
    while (chan != NULL) {
      names = channel_names(chan);
      snprintf(msg, sizeof(msg),":%s 353 %s %s\r\n",server,client->nick,names);
      free(names);
      s_send(msg, clientSocket);
      chan = chan->next;
    }
    cless_names = users_no_channels();
    if (strcmp(cless_names, "* * ") != 0) {
      snprintf(msg, sizeof(msg),":%s 353 %s %s\r\n",server,client->nick,cless_names);
      s_send(msg,clientSocket);
    }
    snprintf(msg, sizeof(msg),":%s 366 %s %s :End of NAMES list\r\n",server,client->nick,"*");
    s_send(msg,clientSocket);
  }
  else {
    chan = channel_find(ps[0]);
    if (chan != NULL) {
      names = channel_names(chan);
      snprintf(msg, sizeof(msg),":%s 353 %s %s\r\n",server,client->nick,names);
      free(names);
      s_send(msg, clientSocket);
    }
    snprintf(msg, sizeof(msg),":%s 366 %s %s :End of NAMES list\r\n",server,client->nick,ps[0]);
    s_send(msg,clientSocket);
  }
  return 0;
}

int handle_LIST(char **ps, int clientSocket){
  pthread_mutex_lock(&chlock);
  user *find=ID_find(clientSocket);
  char server[64];
  s_gethostname(server,64);
  channel_list *head=channels_head;
  while(head!=NULL){
    char msg[512];
    snprintf(msg,sizeof(msg),":%s 322 %s %s %d :%s\r\n",server,find->nick,head->channel,head->active,head->topic);
    s_send(msg,clientSocket);
    head=head->next;
      }
   char msg1[512];
   snprintf(msg1,sizeof(msg1),":%s 323 %s :End of LIST\r\n",server,find->nick);
   s_send(msg1,clientSocket);
   pthread_mutex_unlock(&chlock);
   return 0;
}

int handle_AWAY(char **ps, int clientSocket) {
  char server[64];
  s_gethostname(server, 64);
  char msg[512];
  user *client = ID_find(clientSocket);
  int ct = ps_count(ps);
  if (ct > 1) {
    errParam("AWAY", clientSocket);
    return 0;
  }
  if (client->away != NULL) free(client->away);
  if (ct == 0) {
    client->away = NULL;
    snprintf(msg, sizeof(msg), ":%s 305 %s :You are no longer marked as being away\r\n", server, client->nick);
    s_send(msg, clientSocket);
  }
  if (ct == 1) {
    client->away = strdup(ps[0]);
    snprintf(msg, sizeof(msg), ":%s 306 %s :You have been marked as being away\r\n", server, client->nick);
    s_send(msg, clientSocket);
  }
  return 0;
}

char* make_who_flags(user *user, channel_list *find) {
  char flagbuf[4];
  char* flags;
  int i = 0;
  if (user->away != NULL) {
    flagbuf[i++] = 'G';
  }
  else {
    flagbuf[i++] = 'H';
  }
  if (user->md_oper == 1) {
    flagbuf[i++] = '*';
  }
  if (find != NULL) {
    channel_users* cuser = channel_users_find(find->users, user->clientID);
    if (cuser != NULL) {
      if (cuser->md_coper == 1) {
        flagbuf[i++] = '@';
      }
      else if (cuser->md_voice == 1) {
        flagbuf[i++] = '+';
      }
      else {
        //Do nothing
      }
    }
  }
  flagbuf[i++] = '\0';
  flags = strdup(flagbuf);
  return flags;
}

int handle_WHO(char **ps, int clientSocket) {
  int ct = ps_count(ps);
  if (ct > 1) {
    errParam("WHO", clientSocket);
    return 0;
  }
  int msg_sent = 0;
  int shared_chan = 0;
  char server[64];
  s_gethostname(server, 64);
  char msg[512];
  char host[64];
  char* flags;
  user* usr = head;
  user *client = ID_find(clientSocket);
  client_channels* tchans;
  if (ct == 0 || !strcmp(ps[0], "0") || !strcmp(ps[0], "*")) {
    while (usr != NULL) {
      tchans = usr->channels;
      while (tchans != NULL) {
        if (in_client_channels(tchans->channel, client->channels) == 1) {
          shared_chan = 1;
        }
        tchans = tchans->next;
      }
      if (shared_chan != 1) { //&& !(usr->clientID == client->clientID)) {
        s_getpeername(host, 64, usr->clientID);
        flags = make_who_flags(usr, NULL);
        snprintf(msg, sizeof(msg), ":%s 352 %s %s %s %s %s %s %s :0 %s\r\n", server, client->nick, "*", usr->username, host, server, usr->nick, flags, usr->fullname);
        s_send(msg, clientSocket);
        msg_sent = 1;
        free(flags);
      }
      shared_chan = 0;
      usr = usr->next;
    }
    if (msg_sent == 1) {
      snprintf(msg, sizeof(msg), ":%s 315 %s %s :End of WHO list\r\n", server, client->nick, "*");
      s_send(msg, clientSocket);
    }
    return 0;
  }
  if (ct == 1) {
    channel_list *find = channel_find(ps[0]);
    channel_users *cuser;
    if (find != NULL) {
      cuser = find->users;
      while (cuser != NULL) {
        usr = ID_find(cuser->user_socket);
        s_getpeername(host, 64, usr->clientID);
        flags = make_who_flags(usr, find);
        snprintf(msg, sizeof(msg), ":%s 352 %s %s %s %s %s %s %s :0 %s\r\n", server, client->nick, find->channel, usr->username, host, server, usr->nick, flags, usr->fullname);
        s_send(msg, clientSocket);
        msg_sent = 1;
        free(flags);
        cuser = cuser->next;
      }
      if (msg_sent == 1) {
        snprintf(msg, sizeof(msg), ":%s 315 %s %s :End of WHO list\r\n", server, client->nick, ps[0]);
        s_send(msg, clientSocket);
      }
    }
    return 0;
  }
  return 1;
}

struct handler_entry handlers[] = {
  HANDLER_ENTRY(NICK),
  HANDLER_ENTRY(USER),
  HANDLER_ENTRY(QUIT),
  HANDLER_ENTRY(PRIVMSG),
  HANDLER_ENTRY(NOTICE),
  HANDLER_ENTRY(WHOIS),
  HANDLER_ENTRY(LUSERS),
  HANDLER_ENTRY(PONG),
  HANDLER_ENTRY(PING),
  HANDLER_ENTRY(MOTD),
  HANDLER_ENTRY(JOIN),
  HANDLER_ENTRY(PART),
  HANDLER_ENTRY(TOPIC),
  HANDLER_ENTRY(OPER),
  HANDLER_ENTRY(MODE),
  HANDLER_ENTRY(NAMES),
  HANDLER_ENTRY(LIST),
  HANDLER_ENTRY(AWAY),
  HANDLER_ENTRY(WHO),
};
int num_handlers = sizeof(handlers) / sizeof(struct handler_entry);

/* Expects a well-formed message from the client (minus the '\r\n'). Parses the given command (currently NICK and USER), and updates the given user struct appropriately. */
int parseMsg(char *msg, int clientSocket) {
  /* Ignore any prefix */
  printf("message is :%s\n",msg);
  if (msg[0] == ':') {
    char prebuf[511];
    sscanf(msg, "%s", prebuf);
    int len = strlen(prebuf);
    if (len >= 510) {
      printf("bad msg 2: %s\n", msg);
      return 1;
    }
    msg = &msg[len+1];
  }
  char cmd[511];
  sscanf(msg, "%s", cmd);
  int i;
  for(i = 0; i < num_handlers; i++) {
    if (!strcmp(handlers[i].name, cmd)) {
      int pct = 0;
      char** ps = psInit();
      char* ptr;
      while ((ptr = strchr(msg, ' ')) != NULL) {
ptr++;
msg = ptr;
if (msg[0] == ':') {
ps[pct] = strdup(++msg);
          break;
}
else {
char pbuf[511];
sscanf(msg, "%s", pbuf);
ps[pct] = strdup(pbuf);
}
pct++;
      }
      handlers[i].func(ps, clientSocket);
      break;
    }
  }
  if (i == num_handlers) {
    errCmd(cmd,clientSocket);
    return 1;
  }
  return 0;
}

int main(int argc, char *argv[])
{
  time_t servertime;
  int serverSocket;
  int clientSocket;
  pthread_t client_thread;
  struct addrinfo hints, *res;
  struct sockaddr_storage *clientAddr;
  socklen_t sinSize = sizeof(struct sockaddr_storage);
  struct workerArgs *wa;
  int yes = 1;
  /* Parse command line arguments. */
  int opt;
  char *port = "6667";
  
  while ((opt = getopt(argc, argv, "p:o:h")) != -1)
    switch (opt)
      {
      case 'p':
port = strdup(optarg);
break;
      case 'o':
password = strdup(optarg);
break;
      default:
printf("ERROR: Unknown option -%c\n", opt);
exit(-1);
      }
  memset(&hints, 0, sizeof( hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  
  if (getaddrinfo(NULL, port, &hints, &res) != 0) {
    perror("getaddrinfo() failed");
    pthread_exit(NULL);
  }

  /* Listen for client connection. */
  serverSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if(serverSocket == -1) {
    perror("Could not open socket");
    exit(-1);
  }
  if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    perror("Socket setsockopt() failed");
    close(serverSocket);
    exit(-1);
  }
  if(bind(serverSocket, res->ai_addr, res->ai_addrlen) == -1) {
    perror("Socket bind() failed");
    close(serverSocket);
    exit(-1);
  }
  if (listen(serverSocket, 5) == -1) {
    perror("Socket listen() failed");
    close(serverSocket);

  }

  time(&servertime);
  ctime(&servertime);
  sprintf(s_time, "%s", ctime(&servertime));
  s_time[strlen(s_time) - 1] = '\0'; // Get rid of newline at end of the string
  
  if (pthread_mutex_init(&lock, NULL) != 0) {
    perror("Mutex init failed");
    close(serverSocket);
    exit(-1);
  }


  if (pthread_mutex_init(&chlock, NULL) != 0) {
    perror("Channel mutex init failed");
    close(serverSocket);
    exit(-1);
  }

  if (pthread_mutex_init(&mes, NULL) != 0) {
    perror("message mutex init failed");
    close(serverSocket);
    exit(-1);
  }

  sigset_t new;
  sigemptyset (&new);
  sigaddset(&new, SIGPIPE);
  if (pthread_sigmask(SIG_BLOCK, &new, NULL) != 0) {
    perror("Unable to mask SIGPIPE");
    exit(-1);
  }

  while(1) {
    clientAddr=malloc(sinSize);
    if((clientSocket = accept(serverSocket, (struct sockaddr *) &clientAddr, &sinSize)) == -1)
      {
free(clientAddr);
perror("Socket accept() failed");
close(serverSocket);
continue;
      } 

    wa=malloc(sizeof(struct workerArgs));
    wa->socket=clientSocket;
    if(pthread_create(&client_thread,NULL,client_commands,wa)!=0)
      {
perror("Could not create a client thread");
free(clientAddr);
free(wa);
close(clientSocket);
pthread_exit(NULL);


      }
  }
  pthread_mutex_destroy(&lock);
  pthread_mutex_destroy(&chlock);
  pthread_mutex_destroy(&mes);
}

void* client_commands(void* args){
  struct workerArgs* wa;
  int clientSocket;
  int bufct=0;
  int flag=0;
  int nbytes;
  char buffer[20]; // buffer for reading from client
  wa = (struct workerArgs*) args;
  clientSocket = wa->socket;

  pthread_detach(pthread_self());
  user* new = userInit(clientSocket);
  user_add(new);

  while(1) {
    /* Recieve message from client. */
    nbytes=recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (nbytes == 0)
      {
perror("Server closed the connection");
close(clientSocket);
pthread_exit(NULL);
      }
    else if (nbytes == -1)
      {
perror("Socket recv() failed");
close(clientSocket);
pthread_exit(NULL);
      }
    else
      {
buffer[nbytes] = '\0'; // tack on a '\0' so we can treat the buffer like a string
      }

    char buf[511];
    int i;
    for (i = 0; i < nbytes; i++) {
      buf[bufct] = buffer[i];
      if (buf[bufct] == '\r') flag = 1;
      if (buf[bufct] == '\n' && flag) {
buf[bufct-1] = '\0';
parseMsg(buf, clientSocket);
bufct = 0;
flag = 0;
continue;
      }
      if (bufct == 509) {
buf[bufct+1] = '\0';
parseMsg(buf, clientSocket);
bufct = 0;
flag = 0;
continue;
      }
      bufct++;
    }
  }
  return 0;
}
