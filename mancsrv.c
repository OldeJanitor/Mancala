#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* maximum length of message*/

int port = 3000;
int listenfd;

struct player {
    int fd;
    char name[MAXNAME+1];
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits
                        // pits[NPITS] is the end pit
    //other stuff undoubtedly needed here
    struct player *next;
    int turn;
    int waiting_for_username;
};
struct player *playerlist = NULL;
int num_players = 0;


extern void parseargs(int argc, char **argv);
int find_newline(const char *buf, int n);
extern void makelistener();
extern int compute_average_pebbles();
extern int remove_player(struct player *p, struct player **playerlist);
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */


// Send message out to all players in game
void broadcast(char *s){
  struct player *curr = playerlist;
  while (curr != NULL){
    if (curr->waiting_for_username == 0){
      if (write(curr->fd, s, strlen(s) + 1) == -1 ){
        perror("server: broadcast");
        exit(1);
      }
      struct player *c = playerlist;
      while (c != NULL){
        if (c->waiting_for_username == 0){
          char buf[MAXMESSAGE+1] = {'\0'};
          sprintf(buf, "%s: [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d [6]%d\r\n",
          c->name, c->pits[0], c->pits[1], c->pits[2], c->pits[3],
          c->pits[4], c->pits[5], c->pits[6]);
          if (write(curr->fd, buf, strlen(buf) + 1) == -1 ){
            perror("server: broadcast");
            exit(1);
          }
        }
        c = c->next;
      }
    }
    curr = curr->next;
  }
}



// accept connection, check if valid name, set their board and add to linked
// list

int accept_connection(int fd, struct player **playerlist) {
    // accept client
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    } // add client to list
    struct player *new_player = malloc(sizeof(struct player));
    new_player->fd = client_fd;
    new_player->waiting_for_username = 1;
    if (playerlist == NULL){ // first player
      for (int i = 0; i < NPITS; i++){
        new_player->pits[i] = 4;
      }
      new_player->pits[NPITS] = 0;
      new_player->next = NULL;
      *playerlist = new_player;
    } else { // not first player
      for (int i = 0; i < NPITS; i++){
        new_player->pits[i] = compute_average_pebbles();
      }
      new_player->pits[NPITS] = 0;
      new_player->next = *playerlist;
      *playerlist = new_player;
    }
    // prompt player to input name
    if (write(client_fd, "Welcome to Mancala. What is your name?\r\n", 41) == -1 ){
      perror("server: write intro");
      exit(1);
    }
    return client_fd;
}

// read client name input and check if it is valid
int read_name(int fd, struct player *p){
    // Cut off /r/n from name input and get name
    // if single read doesnt get full name
    char buf[MAXNAME + 1] = {'\0'};
    int inbuf = 0;           // How many bytes currently in buffer?
    int room = sizeof(buf);  // How many bytes remaining in buffer?
    char *after = buf;       // Pointer to position after the data in buf

    int nbytes; // bytes read
    while ((nbytes = read(fd, after, room)) > 0) {
        // Step 1: update inbuf (how many bytes were just added?)
        inbuf = strlen(buf);

        int where;
        if ((where = find_newline(buf, inbuf)) >= 0) {
            buf[where] = '\0';
            break;
        }
        after = &buf[inbuf];
        room = sizeof(buf) - inbuf;
    }
    if (nbytes == 0){ // client disconnected
      return fd;
    } else if (nbytes > MAXNAME || buf[0] == '\0' ) { // name too long
      return -1;
    } else { // check if no same name
      struct player *curr = playerlist;
      while (curr->next != NULL){
        if (curr->waiting_for_username == 0){
          if (strcmp(curr->name, buf) == 0){
            return -1;
          }
        }
        curr = curr->next;
      }
    } //  name is valid
    strncpy(p->name, buf, strlen(buf) + 1);
    // set turn
    // check if first player
    if (num_players == 0){
      p->turn = 1;
    } else { // not first player
      p->turn = 0;
    }
    return 0;
}


/* Read a move from client in linked list whos turn it is
 */
int read_move(struct player **p, struct player **playerlist) {
    struct player *player = *p;
    int fd = player->fd;
    char buf[MAXMESSAGE + 1];
    // keep reading until valid input
    // read player move input
    int num_read = read(fd, &buf, MAXMESSAGE);
    buf[num_read] = '\0';
    // connection is closed?
    if (num_read == 0) {
        fd = -1;
        return fd;
    }
    // empty input?
    if (buf[0] == '\r' || buf[0] == '\n'){
      if (write(player->fd, "Invalid move\r\n", 15) == -1 ){
        perror("server: write move invalid");
        exit(1);
      }
      return 1;
    }

    int move = strtol(buf, NULL, 10);
    //  if move is valid, update board, then return
    if (move >= 0 && move <= NPITS - 1 && player->pits[move] != 0){
      struct player *curr = player;
      int pebbles = player->pits[move];
      player->pits[move] = 0; // empty pit
      int index = move+1;
      while (pebbles > 0){ // distribute pebbles
        if (curr->waiting_for_username == 0){
          for (int i = index; i < NPITS + 1 && pebbles > 0; i++){
            // last pebble is put is players last pit, give extra turn
            if (pebbles == 1 && i == NPITS && curr->fd == fd){
              curr->pits[i] += 1;
              pebbles--;
              char mess[MAXMESSAGE+1] = {'\0'};
              sprintf(mess, "%s has moved!\r\n", player->name);
              broadcast(mess);
              if (write(player->fd, "Extra move! Your move?\r\n", 24) == -1 ){
                perror("server: write move");
                exit(1);
              }
              return 0;
            }
            curr->pits[i] += 1;
            pebbles--;
          }
        }
        index = 0; // if more pebbles, move to next players board
        if (curr->next == NULL){
          curr = *playerlist;
        } else {
          curr = curr->next;
        }
      }
      struct player *c = player;
      // update turns
      while (c != NULL){
        if (c->next != NULL && c->next->waiting_for_username == 0){
          player->turn = 0;
          (c->next)->turn = 1;
          char mess[MAXMESSAGE+1] = {'\0'};
          sprintf(mess, "%s has moved!\r\n", player->name);
          broadcast(mess);
          if (write((c->next)->fd, "Your move?\r\n", 13) == -1 ){
            perror("server: write move");
            exit(1);
          }
          return 0;
        } else if (c->next == NULL && (*playerlist)->waiting_for_username == 0){
          player->turn = 0;
          (*playerlist)->turn = 1;
          char mess[MAXMESSAGE+1] = {'\0'};
          sprintf(mess, "%s has moved!\r\n", player->name);
          broadcast(mess);
          if (write((*playerlist)->fd, "Your move?\r\n", 13) == -1 ){
            perror("server: write move");
            exit(1);
          }
          return 0;
        }
        if (c->next != NULL){
          c = c->next;
        }else{
          c = *playerlist;
        }
      }
      return 0;
    } // move was not valid
    if (write(player->fd, "Invalid move\r\n", 15) == -1 ){
      perror("server: write move invalid");
      exit(1);
    }
    return 1;
}


int main(int argc, char **argv) {
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();

    // FD_SET
    int max_fd = listenfd;
    // orignial fd set
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);

    // Start game
    while (!game_is_over()) {
        // select clients that are ready to be read from
        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }
        // NEW PLAYER! Create a new connection ...
        if (FD_ISSET(listenfd, &listen_fds)) {
            // accept client and add to the linkedlist
            int client_fd = accept_connection(listenfd, &playerlist);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
        }

        //check the clients.
        struct player *curr_player = playerlist;
        while (curr_player != NULL) {
          // Player is inputting name
            if (curr_player->waiting_for_username == 1
              && FD_ISSET(curr_player->fd, &listen_fds)) {
                int read_complete = read_name(curr_player->fd, curr_player);
                if (read_complete > 0) { // Client disconnected
                    FD_CLR(read_complete, &all_fds);
                    remove_player(curr_player, &playerlist);
                } else if (read_complete == -1) { // Invalid name input
                  if (write(curr_player->fd, "Invalid name input?\r\n", 22) == -1 ){
                    perror("server: invalid name");
                    exit(1);
                  }
                } else { // name inputted correctly, add to game
                  num_players++;
                  curr_player->waiting_for_username = 0;
                  // tell all players someone joined
                  char mess[MAXMESSAGE+1] = {'\0'};
                  sprintf(mess, "%s has joined!\r\n", curr_player->name);
                  broadcast(mess);
                  // prompt move message
                  if (num_players == 1){
                    if (write(curr_player->fd, "Your move?\r\n", 13) == -1 ){
                      perror("server: write move");
                      exit(1);
                    }
                  }
                }
              } // NOT this player's turn
              else if (curr_player->turn == 0
                && FD_ISSET(curr_player->fd, &listen_fds)
                && curr_player->waiting_for_username == 0) {
                  char buf[MAXMESSAGE +1];
                  // PLAYER DISCONNECTED
                  if (read(curr_player->fd, buf, MAXMESSAGE + 1) == 0){
                    if (curr_player->next != NULL){ // front player wasn't next
                      curr_player->next->turn = 1;
                      FD_CLR(curr_player->fd, &all_fds); // remove player
                      remove_player(curr_player, &playerlist);
                      num_players--;
                      // Notify other players someone has left
                      char mess[MAXMESSAGE+1] = {'\0'};
                      sprintf(mess, "%s has left!\r\n", curr_player->name);
                      broadcast(mess);
                      if (write(curr_player->next->fd, "Your move?\r\n", 13) == -1 ){
                        perror("server: write move");
                        exit(1);
                      }
                    } else { // front player was next
                      playerlist->turn = 1;
                      FD_CLR(curr_player->fd, &all_fds); // remove player
                      remove_player(curr_player, &playerlist);
                      num_players--;
                      // Notify other players someone has left
                      char mess[MAXMESSAGE+1] = {'\0'};
                      sprintf(mess, "%s has left!\r\n", curr_player->name);
                      broadcast(mess);
                      if (write(playerlist->fd, "Your move?\r\n", 13) == -1 ){
                        perror("server: write move");
                        exit(1);
                      }
                    }
                  } else {
                    // player connected but they inputed and is NOT their turn!
                    if (write(curr_player->fd, "It is not your move!\r\n", 22) == -1 ){
                      perror("server: write not your turn");
                      exit(1);
                    }
                  }
              } // It is this player's turn
              else if (curr_player->turn == 1
                && curr_player->waiting_for_username == 0
                && FD_ISSET(curr_player->fd, &listen_fds)) {
                  int move = read_move(&curr_player, &playerlist);
                  if (move == -1){ // PLAYER DISCONNECTED
                    if (curr_player->next != NULL){ // first player was not next
                      curr_player->next->turn = 1;
                      FD_CLR(curr_player->fd, &all_fds);
                      remove_player(curr_player, &playerlist);
                      num_players--;
                      char mess[MAXMESSAGE+1] = {'\0'};
                      sprintf(mess, "%s has left!\r\n", curr_player->name);
                      broadcast(mess);
                      if (write(curr_player->next->fd, "Your move?\r\n", 13) == -1 ){
                        perror("server: write move");
                        exit(1);
                      }
                    } else { // first player was next
                      if (num_players == 1){
                        FD_CLR(curr_player->fd, &all_fds); // player disconnected
                        remove_player(curr_player, &playerlist);
                        num_players--;
                      } else {
                        playerlist->turn = 1;
                        FD_CLR(curr_player->fd, &all_fds); // player disconnected
                        remove_player(curr_player, &playerlist);
                        num_players--;
                        char mess[MAXMESSAGE+1] = {'\0'};
                        sprintf(mess, "%s has left!\r\n", curr_player->name);
                        broadcast(mess);
                        if (write(playerlist->fd, "Your move?\r\n", 13) == -1 ){
                          perror("server: write move");
                          exit(1);
                        }
                      }
                    }
                  }
            }
            // Move to next player in linked list who inputted something
            curr_player = curr_player->next;
        }
    }

    //Game is Over, broadcast score to everyone
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        //broadcast(msg);
    }

    return 0;
}

// used to parse arguements when calling mancsrv
void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}

// set up local server
void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list
   gives new player an average of pebbles of all of the other players*/
int compute_average_pebbles() {
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}

// Remove player from game
int remove_player(struct player *p, struct player **playerlist){
  int flag = 0;
  struct player *curr = *playerlist;
  struct player *prev = NULL;
  // find player in linked list
  while (curr->next != NULL){
    if (curr->fd == p->fd){
      flag = 1;
      break;
    }
    prev = curr;
    curr = curr->next;
  }
  // if player is first
  if (prev == NULL){ // remove first player in linked list
    if (curr->next == NULL){
      *playerlist = NULL;
    }else {
      *playerlist = curr->next;
    }
  }
  // if player is last
  else if (curr->next == NULL){
    prev->next = NULL;
  } else {
    prev->next = curr->next;
  }
  if (flag == 1) { // found and removed
    return 0;
  }
  return 1;
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet play list is empty! */
    }

    // Check if a player has all of their pebbles in last pit
    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}

/*
 * Search the first n characters of buf for a newline (\r\ or \n).
 * Return the index of the '\n' or '\r' of the first newline,
 * or -1 if no network newline is found.
 */
int find_newline(const char *buf, int n) {
    for (int i = 0; i < n; i++){
      if ((buf[i] == '\r') || (buf[i] == '\n')){
        return i;
      }
    }
    return -1;
}
